# SF-4.3: the V2 material-import masters, built on the graph-authoring helper layer in
# `matgraph.py`. Design: docs/architecture/seam_map_material.md -> "Import" (master inventory,
# exposed-parameter table, post-lighting math, reflection contract, knob contract, class
# fallback). Build mechanics: import/design/phase4_mechanics.md section 3.
#
# Part 2 of SF-4.3 (commit series, complete): the three previous commits reconciled `M_V2_Lit` to
# the revised design (2026-08-31 "Revise the material import design after review" -- binding
# contract, defaults, class fallback) and added `M_V2_LitTranslucent`, `M_V2_Unlit` and
# `M_V2_TwoTexture`. Part 1 (`9daae29b`) authored `M_V2_Lit` against the pre-revision design; the
# header notes it left below (Clamp's unnamed primary pin, `connect_material_property`'s
# three-argument signature, `MaterialExpressionSine.Period` not being connectable, and the absent
# `MP_PIXEL_DEPTH_OFFSET`) are still the facts this file builds against.
#
# Part 3 (this commit series) adds the remaining five masters -- `M_V2_Eyes`, `M_V2_Water`,
# `M_V2_Sprite`, `M_V2_Refract`, `M_V2_Decal` -- one commit each, plus a closing cross-cutting
# ruling commit (design doc "Import" -> the reflection-contract knob contract, the LUT default,
# `T_V2_DefaultFrames`, and the per-unit divergence allowlist).
#
# --- Doc gaps hit while reconciling, flagged for the owner --------------------------------
#
# 1. `DefaultSpecular`/`DefaultRoughness`/`DefaultMetallic` (the "non-$envmap surfaces still
#    reflect" knobs) are declared in `MPC_ElysiumSurfaces` but the pre-revision `M_V2_Lit` never
#    read them for a non-`$envmap` surface -- its `UseEnvMap`-off branch is the class LUT row
#    alone. That gap predates this commit and is not in the instructed delta list, so it is left
#    alone here and only noted. `M_V2_TwoTexture` (new, this commit) follows the same
#    class-LUT-alone convention for consistency with the existing masters rather than inventing a
#    third convention; if the owner wants `Default*` folded in, that is one change across three
#    masters, not a per-master divergence.
# 2. `M_V2_TwoTexture`'s post-lighting math section states the BaseColor lerp and the Overbright
#    multiply but never gives `AlphaBias`/`Opacity` a formula. This file wires
#    `Opacity = Alpha + AlphaBias` (optionally x VertexColor.a under `UseVertexAlpha`), the
#    simplest reading of "$alpha_bias, an additive bias" consistent with the master's own
#    `Alpha`/`UseVertexAlpha` contract; flagged as an interpretation, not a transcription.
import hashlib
from pathlib import Path
import struct
import zlib

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl
from pipeline.unreal import mat_fog
from pipeline.unreal import matgraph
from pipeline.unreal.matgraph import Graph, class_lut_uv, read_class_lut
from elysium_pipeline import mounts
from elysium_pipeline.paths import export_v2_root, scratch_root

connect = matgraph.connect

PKG = mounts.MATERIALS_V2

mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

DEFAULT_CUBE = "/Engine/EngineResources/DefaultTextureCube.DefaultTextureCube"

#: Bump the graph shape (node topology / algebra), not the parameter defaults, and a re-run picks
#: it up through the recipe stamp even when nothing on disk changed. `_source_hash()` below is the
#: exhaustive safety net (it catches an edit this constant was not bumped for); this constant
#: stays as the human-readable marker of the shape revision.
GRAPH_VERSION = 5

#: `MPC_ElysiumSurfaces` (SF-4.1, C++, landed) owns every one of these rows and their defaults --
#: `make_surface_knobs.py` (`build_content.py` runs it before this file). This generator is a
#: *reader*, never a second writer: `_load_surfaces_collection` fails hard if the collection or
#: any row this file's masters read is missing, rather than seeding a value SF-4.1 already owns.
SURFACES_COLLECTION = "MPC_ElysiumSurfaces"
#: `DefaultRoughness`/`DefaultSpecular`/`DefaultMetallic`/`ClassInfluence` closed doc gap #1 (this
#: file's own module docstring, part 2): the pre-revision `M_V2_Lit` never read the `Default*` rows
#: for a non-`$envmap` surface, and `ClassInfluence` (added to `MPC_ElysiumSurfaces` by the
#: concurrent C++ fixer named in the SF-4.3-part-3 task) did not exist yet. Part 3's cross-cutting
#: ruling (a) closes both: `_class_lut_influenced` reads all four.
REQUIRED_MPC_SCALARS = sorted([
    "Overbright", "MaskRoughnessMin", "MaskRoughnessMax", "MaskSpecularScale",
    "MaskMetallicMax", "ChromaticTintStrength", "EnvTintScale", "FixedCubeStrength",
    "DefaultRoughness", "DefaultSpecular", "DefaultMetallic", "ClassInfluence",
    # R6.3: the detail-sway amplitude (`_detail_sway`), on Lit/LitTranslucent/Unlit.
    "DetailSwayAmplitude",
])

# The shader-source units each master's post-lighting math transcribes (design doc "Post-lighting
# math per master"). Their sha256 goes into that master's recipe so a shader-source GLB revision
# invalidates the stamp even though nothing in this file changed.
LIT_CITED_SHADER_UNITS = [
    "lightmappedgeneric",
    "lightmappedgeneric_selfilluminated",
    "vertexlitgeneric_envmappedbumpmapv2",
]
UNLIT_CITED_SHADER_UNITS = [
    "unlitgeneric",
    "unlitgeneric_envmapmask",
]
TWOTEXTURE_CITED_SHADER_UNITS = [
    "worldvertextransition",
]
EYES_CITED_SHADER_UNITS = [
    "eyes",
    "eyes_overbright2",
]
#: Both shipped water pixel programs (`WaterRefract_old`/`WaterReflect_old` and their `_ps20_old`
#: spellings) are compiled-only -- no readable `.psh` ships for any of them -- but `waterrefract.psh`
#: and `waterreflect.psh` (a design-era readable pair the doc cites by name, "M_V2_Water" post-
#: lighting math) carry the shape this master transcribes. `watercheap` ships no readable source at
#: all and is not cited.
WATER_CITED_SHADER_UNITS = [
    "waterrefract",
    "waterreflect",
]


def _fail(msg):
    unreal.log_error("[make_v2_materials] %s" % msg)
    raise SystemExit(1)


def _fresh(name, *, ism=False, nanite=False, skeletal=False, morph=False, niagara_sprites=False):
    """Delete + recreate the asset so a re-run authors a clean graph (idempotent). Usage flags are
    per family (design doc "Master inventory" + mechanics doc section 3b): without the matching
    flag UE compiles no permutation outside the editor and the affected primitives fall back to
    the default grey material in a packaged build. `M_V2_Decal` (not authored here) must NOT set
    Nanite -- BLEND_Modulate is not Nanite-compatible -- which is why this is opt-in per call site
    rather than a blanket default.

    M8 review fix: `delete_asset`'s return is now checked -- a failed delete used to fall through
    silently into `create_asset` at the same path, and whatever `MI_` instances the stage already
    parented to the stale master in this same editor session stay parented to the object that
    *was* at that path (Unreal reparents nothing on a failed delete; the in-memory `UMaterial` the
    stale `MI_`s still reference is not the fresh one this call is about to author). This function
    cannot itself fix that -- the deletion genuinely failed -- so it fails loudly instead of
    authoring a second, disconnected master silently. See the module docstring's idempotency note:
    a rebuild of any master orphans its `MI_` children in-session regardless (this function's own
    delete-then-recreate always swaps the `UMaterial` object under the same path, which the editor
    session's already-loaded `MI_` instances do not automatically re-resolve against), so
    `uv run elysium import materials` must re-run after any `make_v2_materials.py` rebuild to
    re-parent them, and `build_content.py` (which runs this generator) must never share a process
    with `import_materials.py` (which parents the `MI_`s) for exactly that reason."""
    asset = "%s/%s" % (PKG, name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset):
        if not unreal.EditorAssetLibrary.delete_asset(asset):
            _fail("could not delete stale %s before rebuilding -- rerun once the asset is free "
                  "(no other editor session/reference holding it), then re-run "
                  "`uv run elysium import materials` afterwards to re-parent its MI_ children"
                  % asset)
    mat = tools.create_asset(name, PKG, unreal.Material, unreal.MaterialFactoryNew())
    if not mat:
        _fail("create_asset failed: %s" % asset)
    if ism:
        mat.set_editor_property("used_with_instanced_static_meshes", True)
    if nanite:
        mat.set_editor_property("used_with_nanite", True)
    if skeletal:
        mat.set_editor_property("used_with_skeletal_mesh", True)
    if morph:
        mat.set_editor_property("used_with_morph_targets", True)
    if niagara_sprites:
        mat.set_editor_property("used_with_niagara_sprites", True)
    return mat, asset


def _png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(
        ">I", zlib.crc32(kind + data) & 0xffffffff)


def _policy_scratch_dir():
    """Scratch import sources go under the work-root scratch tree (`elysium_pipeline.paths`'s
    path contract: no module constructs a repository- or export-relative output path), never
    under the export corpus, which is read-and-never-written."""
    d = scratch_root() / "policy" / "materials-v2"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _source_hash():
    """sha256 of this file plus `matgraph.py`'s and `mat_fog.py`'s own text -- the exhaustive half
    of the recipe stamp: any edit to any of the three files invalidates every master's stamp even
    when `GRAPH_VERSION` was not bumped for it. `mat_fog.py` is in scope (review fix) because
    `matgraph.connect` is `mat_fog.connect` re-exported, not reimplemented -- a change to its
    refused-pin behaviour changes every master's graph exactly as much as a `matgraph.py` edit
    does."""
    digest = hashlib.sha256()
    for name in ("make_v2_materials.py", "matgraph.py", "mat_fog.py"):
        digest.update(Path(__file__).with_name(name).read_bytes())
    return digest.hexdigest()


def _import_png(name, png_bytes, *, srgb, compression, filter_nearest=False, force=False,
                expected_class="Texture2D"):
    """Review fix: the early-return path used to hand back whatever asset already sat at `name`
    with no class check at all -- a stray non-`Texture2D` object left over from a prior defect (or
    a hand-authored asset colliding with the generated name) would be silently reused forever. The
    existing asset's class is now checked on every call, and `-PolicyForce` (`force=True`) always
    deletes and re-creates regardless of what is already there, exactly like every generated
    master itself."""
    asset = "%s/%s" % (PKG, name)
    existing = unreal.load_asset(asset)
    if existing:
        if not force and existing.get_class().get_name() == expected_class:
            return existing
        if not force:
            unreal.log_warning(
                "[make_v2_materials] %s is a %s, not %s -- rebuilding"
                % (asset, existing.get_class().get_name(), expected_class))
        unreal.EditorAssetLibrary.delete_asset(asset)
    source = _policy_scratch_dir() / ("%s.png" % name)
    source.write_bytes(png_bytes)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", PKG)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(asset)
    if not texture:
        _fail("could not author %s" % asset)
    texture.set_editor_property("srgb", srgb)
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    if filter_nearest:
        texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
        texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
        texture.set_editor_property(
            "lossy_compression_amount", unreal.TextureLossyCompressionAmount.TLCA_NONE)
    unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False)
    return texture


def _make_linear_white_mask(force=False):
    """A real linear 1x1 white mask -- the same trick `make_world_materials.py` uses, kept as its
    own V2-package copy rather than a cross-package reference."""
    png = (b"\x89PNG\r\n\x1a\n"
           + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 0, 0, 0, 0))
           + _png_chunk(b"IDAT", zlib.compress(b"\x00\xff"))
           + _png_chunk(b"IEND", b""))
    return _import_png("T_LinearWhiteMask", png, srgb=False,
                       compression=unreal.TextureCompressionSettings.TC_MASKS, force=force)


def _load_class_lut():
    """`T_SurfaceClassLUT` (128x1, `UElysiumSurfaceCalibration::MaxRows`) is SF-4.1's own asset,
    authored by `make_surface_knobs.py` (which `build_content.py` runs before this file). Read
    through `DA_SurfaceCalibration`'s own `Lut` property -- never a hard-coded texture path --
    because that is robust to *where* the texture object actually lives (its own top-level asset
    at `<PKG>/T_SurfaceClassLUT`, or, once SF-4.1 folds it into the calibration asset's own
    package, an inner object at `<PKG>/DA_SurfaceCalibration.T_SurfaceClassLUT`): the calibration
    data asset is the one stable name either way. This generator never seeds a placeholder for it:
    a silently-substituted placeholder LUT would make every class read back roughness/specular/
    metallic from invented numbers with no warning, which is worse than failing loudly and naming
    the missing prerequisite."""
    calibration_asset = "%s/%s" % (PKG, "DA_SurfaceCalibration")
    calibration = unreal.load_asset(calibration_asset)
    if not calibration:
        _fail("%s not found -- run make_surface_knobs.py (SF-4.1) before make_v2_materials.py"
              % calibration_asset)
    texture = calibration.get_editor_property("lut")
    if not texture:
        _fail("%s has no Lut -- run make_surface_knobs.py (SF-4.1) before make_v2_materials.py"
              % calibration_asset)
    return texture


# --- T_V2_DefaultFrames: a 2-slice Texture2DArray, white -----------------------------------


def _dds_header(width, height, pitch):
    parts = [
        struct.pack("<I", 124),      # dwSize
        struct.pack("<I", 0x1007),   # dwFlags: CAPS|HEIGHT|WIDTH|PIXELFORMAT
        struct.pack("<I", height),
        struct.pack("<I", width),
        struct.pack("<I", pitch),
        struct.pack("<I", 0),        # dwDepth
        struct.pack("<I", 1),        # dwMipMapCount
        b"\x00" * 44,                # dwReserved1[11]
        struct.pack("<I", 32),       # DDS_PIXELFORMAT.dwSize
        struct.pack("<I", 0x4),      # DDPF_FOURCC
        b"DX10",                     # dwFourCC
        struct.pack("<5I", 0, 0, 0, 0, 0),  # dwRGBBitCount + 4 bit masks
        struct.pack("<I", 0x1000),   # dwCaps: DDSCAPS_TEXTURE
        struct.pack("<3I", 0, 0, 0),  # dwCaps2/3/4
        struct.pack("<I", 0),        # dwReserved2
    ]
    return b"".join(parts)


def _dds_dx10_header(array_size):
    return struct.pack(
        "<5I",
        28,          # DXGI_FORMAT_R8G8B8A8_UNORM
        3,           # D3D10_RESOURCE_DIMENSION_TEXTURE2D
        0,           # miscFlag (not a cubemap)
        array_size,
        0,           # miscFlags2: DDS_ALPHA_MODE_UNKNOWN
    )


def _make_default_frames_array(force=False):
    """A real, white `Texture2DArray` -- the `BaseTextureFrames`/`NormalMapFrames` default (design
    doc "The animation and scroll lanes"), authored from a hand-built minimal DX10 DDS the same way
    `_make_linear_white_mask` hand-builds a PNG.

    SF-4.3-part-3 cross-cutting ruling (c), two real-editor findings in order:

    1. `AssetTools.create_asset` with a real `unreal.Texture2DArrayFactory` -- the design doc's
       other named route -- does **not** work from Python in this engine build:
       `UTexture2DArrayFactory::InitialTextures` carries no `EditAnywhere`/`BlueprintReadWrite`
       specifier (`Texture2DArrayFactory.h`), so it is not part of the Python property surface at
       all (`Failed to find property 'initial_textures'`, confirmed live).
    2. The DDS-import route (`AssetImportTask`, matching the real texture lane's own
       `import_textures.py::_import_chunk`) **does** work, but not at `arraySize = 1`: a DX10 DDS
       array header with exactly one slice collapses to a plain `Texture2D` on import (confirmed
       live: `T_V2_DefaultFrames imported as Texture2D, not Texture2DArray` at `arraySize = 1`),
       the same "a one-element array is ambiguous with a plain 2D texture" heuristic several DDS
       importers use. **Two** identical white slices (`arraySize = 2`) is therefore the smallest
       DDS that reliably imports as a real `Texture2DArray` -- semantically identical to "one white
       slice" for this default (every consumer clamps `SliceIndex` via `floor(frac(...) *
       FrameCount)` with the shipped default `FrameCount = 1.0`, so slice 1 is never sampled by any
       default-configured material; it exists purely so the importer classifies the asset
       correctly). The class is still verified after creation, and a genuine failure logs and
       returns `None` rather than raising, so a content build never blocks on this one texture.

    Review fix: the class was previously verified only on the *first* run's creation path -- a
    later run that found an already-existing (possibly wrongly-classed, from an earlier defect or
    a hand-authored collision) asset at this name returned it unchecked. The existing asset's
    class is now re-verified on every call, and `-PolicyForce` (`force=True`) always deletes and
    rebuilds regardless of what is already there."""
    asset = "%s/T_V2_DefaultFrames" % PKG
    existing = unreal.load_asset(asset)
    if existing:
        if not force and existing.get_class().get_name() == "Texture2DArray":
            return existing
        if not force:
            unreal.log_warning(
                "[make_v2_materials] %s is a %s, not Texture2DArray -- rebuilding"
                % (asset, existing.get_class().get_name()))
        unreal.EditorAssetLibrary.delete_asset(asset)
    pixel = b"\xff\xff\xff\xff" * 2  # two identical white RGBA8 texels, one per slice
    dds = b"DDS " + _dds_header(1, 1, 4) + _dds_dx10_header(2) + pixel
    source = _policy_scratch_dir() / "v2_default_frames.dds"
    source.write_bytes(dds)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", PKG)
    task.set_editor_property("destination_name", "T_V2_DefaultFrames")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(asset)
    if not texture:
        unreal.log_warning(
            "[make_v2_materials] could not author T_V2_DefaultFrames; "
            "BaseTextureFrames/NormalMapFrames bind no default texture")
        return None
    if texture.get_class().get_name() != "Texture2DArray":
        unreal.log_warning(
            "[make_v2_materials] T_V2_DefaultFrames imported as %s, not Texture2DArray -- "
            "BaseTextureFrames/NormalMapFrames ship with no default texture "
            "(doc gap, see this file's module docstring)" % texture.get_class().get_name())
        return None
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False)
    return texture


def _make_default_normal_frames_array(force=False):
    """`T_V2_DefaultNormalFrames` -- the `NormalMapFrames` default (H3 review fix). Every normal
    flipbook lane now samples its `TextureObjectParameter` through `SAMPLERTYPE_NORMAL`
    (`_flipbook_sample(..., sampler="normal")`), so `HLSLMaterialTranslator.cpp`'s
    `UnpackNormalMap` (only emitted for `SAMPLERTYPE_Normal`, ~line 6951) now actually runs on
    whatever this slot samples -- including the default, for any instance that turns
    `UseAnimatedNormalFrames` on without binding a real animated normal array. `T_V2_DefaultFrames`
    (the base/colour lane's own default, still `sampler="linear"`) is pure white
    (`0xff,0xff,0xff,0xff`); unpacked (`2*x - 1`) that is `(1,1,1)`, not the flat `(0,0,1)` a
    "no perturbation" default must decode to. This is therefore its own asset, not a second binding
    of the same texture: `(128, 128, 255, 255)` packed -- the identical flat-normal byte triple
    `/Engine/EngineMaterials/DefaultNormal` itself carries -- with `TC_Normalmap` compression,
    matching every other `NormalMap` default in this file. Same two-slice DX10 DDS shape as
    `_make_default_frames_array` (the `arraySize = 1` collapse-to-`Texture2D` finding documented
    there applies identically here), and the same existing-asset re-verify / `-PolicyForce`
    contract."""
    asset = "%s/T_V2_DefaultNormalFrames" % PKG
    existing = unreal.load_asset(asset)
    if existing:
        if not force and existing.get_class().get_name() == "Texture2DArray":
            return existing
        if not force:
            unreal.log_warning(
                "[make_v2_materials] %s is a %s, not Texture2DArray -- rebuilding"
                % (asset, existing.get_class().get_name()))
        unreal.EditorAssetLibrary.delete_asset(asset)
    pixel = b"\x80\x80\xff\xff" * 2  # two identical flat-normal RGBA8 texels, one per slice
    dds = b"DDS " + _dds_header(1, 1, 4) + _dds_dx10_header(2) + pixel
    source = _policy_scratch_dir() / "v2_default_normal_frames.dds"
    source.write_bytes(dds)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", PKG)
    task.set_editor_property("destination_name", "T_V2_DefaultNormalFrames")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(asset)
    if not texture:
        unreal.log_warning(
            "[make_v2_materials] could not author T_V2_DefaultNormalFrames; "
            "NormalMapFrames ships with no default texture")
        return None
    if texture.get_class().get_name() != "Texture2DArray":
        unreal.log_warning(
            "[make_v2_materials] T_V2_DefaultNormalFrames imported as %s, not Texture2DArray -- "
            "NormalMapFrames ships with no default texture (doc gap, see this file's module "
            "docstring)" % texture.get_class().get_name())
        return None
    texture.set_editor_property("srgb", False)
    texture.set_editor_property(
        "compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False)
    return texture


def _load_surfaces_collection():
    """`MPC_ElysiumSurfaces` is SF-4.1's own asset (`UElysiumSurfaceSettings::PushToCollection` is
    its values' sole writer); this generator reads it and never seeds a row -- a row this file's
    masters read that is missing is a build error naming the missing generator, not a silently
    invented default (`Overbright` at some Python literal instead of the tuned settings value,
    say)."""
    asset = "%s/%s" % (PKG, SURFACES_COLLECTION)
    collection = unreal.load_asset(asset)
    if not collection:
        _fail("%s not found -- run make_surface_knobs.py (SF-4.1) before make_v2_materials.py"
              % asset)
    # `str(...)` is load-bearing: `parameter_name` reads back as `unreal.Name`, whose `__hash__`
    # does not agree with `str`'s even though `Name("X") == "X"` is `True` -- a set built from raw
    # `Name` objects fails every `"X" in have` membership check (hash bucketing never finds the
    # matching bucket), so this loop would otherwise report every required row missing regardless
    # of what the collection actually holds (confirmed live: `MPC_ElysiumSurfaces` already carried
    # every required row, and this check still reported all eight absent before the `str()` fix).
    have = {str(p.get_editor_property("parameter_name"))
            for p in collection.get_editor_property("scalar_parameters")}
    missing = sorted(name for name in REQUIRED_MPC_SCALARS if name not in have)
    if missing:
        _fail("%s is missing required scalar row(s): %s -- run make_surface_knobs.py"
              % (asset, ", ".join(missing)))
    return collection


#: `MPC_ElysiumEnvironment` (make_world_materials.py::make_environment_collection) is the live
#: weather system's own collection -- `AElysiumMapActor::ApplyWeatherTuning` is its sole writer,
#: pushing `GlobalWetness`/`WetnessOutputScale` once a tick, world-scoped, never per-map or
#: per-instance. It is a *different* asset from `MPC_ElysiumSurfaces` (owned by
#: `UElysiumSurfaceSettings`, "single writer" ruling in the knob contract), so wetness reads it
#: through its own `CollectionParameter` node rather than folding it into the surfaces collection.
#: This is R5.3's chosen home for the wetness axis (seam_map_material.md -> "Decal fog and
#: wetness homes (R5.3)"): a global live value, never a per-map material instance.
ENVIRONMENT_COLLECTION = "MPC_ElysiumEnvironment"


def _load_environment_collection():
    asset = "%s/%s" % (mounts.MATERIALS, ENVIRONMENT_COLLECTION)
    collection = unreal.load_asset(asset)
    if not collection:
        _fail("%s not found -- run make_world_materials.py before make_v2_materials.py" % asset)
    have = {str(p.get_editor_property("parameter_name"))
            for p in collection.get_editor_property("scalar_parameters")}
    missing = sorted(name for name in ("GlobalWetness", "WetnessOutputScale") if name not in have)
    if missing:
        _fail("%s is missing required scalar row(s): %s -- run make_world_materials.py"
              % (asset, ", ".join(missing)))
    return collection


def _env_scalar(g, environment_collection, name, x, y):
    """A `CollectionParameter` read off `MPC_ElysiumEnvironment` rather than `g.collection`
    (`MPC_ElysiumSurfaces`) -- see `ENVIRONMENT_COLLECTION`'s own docstring above."""
    n = g.node(unreal.MaterialExpressionCollectionParameter, x, y)
    n.set_editor_property("collection", environment_collection)
    n.set_editor_property("parameter_name", name)
    return n


def _wetness_response(g, environment_collection, P, x, y):
    """`saturate(GlobalWetness x WetnessScale x WetnessOutputScale)`, gated by `WetnessDriven`
    (`lerp(1, wet_amount, WetnessDriven)`) -- the exact term `make_world_materials.py`'s own
    world masters already carry, ported onto a V2 master. `WetnessScale` is a real per-instance
    scalar the stage now writes from the unit's own `globalwetness` proxy (never provenance-only
    on a master that declares this lane); `WetnessDriven` is 0 on every instance that authors no
    such proxy, so an unwetted surface's reflectiveness is untouched (`lerp(1, x, 0) == 1`)."""
    global_wetness = _env_scalar(g, environment_collection, "GlobalWetness", x, y)
    wetness_scale = g.scalar(P.Scalars.WetnessScale, 0.0, x, y + 80)
    wetness_driven = g.scalar(P.Scalars.WetnessDriven, 0.0, x, y + 160)
    wetness_output_scale = _env_scalar(g, environment_collection, "WetnessOutputScale", x, y + 240)
    wet_authored = g.mul(global_wetness, "", wetness_scale, "", x + 220, y + 40)
    wet_scaled = g.mul(wet_authored, "", wetness_output_scale, "", x + 380, y + 80)
    wet_amount = g.sat(wet_scaled, "", x + 540, y + 80)
    one = g.const(1.0, x + 540, y - 80)
    return g.lerp(one, "", wet_amount, "", wetness_driven, "", x + 700, y)


def _unit_sha256(stem):
    path = Path(export_v2_root()) / "shader-programs" / "source" / ("%s.glb" % stem)
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _cited_unit_hashes(stems):
    hashes = {}
    for stem in stems:
        sha = _unit_sha256(stem)
        hashes[stem] = sha if sha else "unavailable"
    return hashes


def _probe_all_switches_true(mat, asset, switch_names):
    """Compile-prove every static-switch branch, not just the all-false default
    `recompile_material` already checks. A throwaway `MaterialInstanceConstant` parented to `mat`
    with *every* switch in `switch_names` forced `True`, one shader compile
    (`update_material_instance`), and a real-instruction-count assertion (`get_statistics`,
    `import_materials.py::_compile_probe`'s own pattern) -- this is exactly the permutation the
    flipbook-sampler defect needed to surface (`UseAnimatedFrames=true` is the one branch that
    ever visits the `Texture2DArray` array-sample node; the same is true of `UseFixedCube` on
    Lit/Unlit/Water/Refract, whose `RayTracingQualitySwitch`-gated cube-reflection branch
    `recompile_material`'s default-false compile never reaches either). Deleted again immediately
    after the probe, so it never lingers as a stray asset in the package."""
    if not switch_names:
        return
    probe_name = "MI_V2CompileProbe_%s" % asset.rsplit("/", 1)[-1]
    probe_asset = "%s/%s" % (PKG, probe_name)
    if unreal.EditorAssetLibrary.does_asset_exist(probe_asset):
        unreal.EditorAssetLibrary.delete_asset(probe_asset)
    probe = bl.make_material_instance(probe_name, PKG, mat)
    if probe is None:
        _fail("%s: could not create the all-switches-true compile probe" % asset)
    for name in sorted(set(switch_names)):
        mel.set_material_instance_static_switch_parameter_value(
            probe, name, True, update_material_instance=False)
    mel.update_material_instance(probe)
    stats = mel.get_statistics(probe)
    instructions = getattr(stats, "num_pixel_shader_instructions", None)
    if instructions is None:
        instructions = stats.get_editor_property("num_pixel_shader_instructions")
    unreal.EditorAssetLibrary.delete_asset(probe_asset)
    if not instructions or instructions <= 0:
        _fail("%s: all-switches-true compile probe reports %s pixel-shader instructions"
              % (asset, instructions))
    unreal.log("[make_v2_materials] %s all-switches-true probe: %s pixel-shader instructions"
              % (asset, instructions))


# ============================================================================================
# Shared graph lanes: UV/scroll, sine, flipbook animation.
# ============================================================================================


def _detail_sway(g, switch_name, x, y):
    """R6.3 (`seam_map_material.md` -> "Detail sway on the model masters"): one World Position
    Offset term behind the static switch `switch_name` (default off):

        sway   = PerInstanceCustomData[0]           -- swayAmount / 255; 0 on a non-instanced draw
        weight = saturate((local.z - min.z) / (max.z - min.z))
                                                    -- the base stays put, the tip moves
        phase  = (world.x + world.y) / 2.54         -- Source's per-object phase, in its inches
        WPO    = sin(Time + phase) * sway * weight * DetailSwayAmplitude, along world (1, 1, 0)

    Off, the branch compiles to the constant zero the translator does NOT count as a WPO use
    (`IsMaterialPropertyUsed`), so every Nanite chunk, prop and character on the master keeps the
    shader it had; only the map bake's `MI_DetailSway_*` children turn it on. `Time` is the shared
    wind clock, `DetailSwayAmplitude` (cm) the one knob, off `MPC_ElysiumSurfaces`."""

    custom = g.node(unreal.MaterialExpressionPerInstanceCustomData, x, y)
    custom.set_editor_property("data_index", 0)
    custom.set_editor_property("const_default_value", 0.0)

    world_pos = g.node(unreal.MaterialExpressionWorldPosition, x, y + 120)
    local = g.node(unreal.MaterialExpressionTransformPosition, x + 200, y + 120)
    local.set_editor_property(
        "transform_source_type",
        unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local.set_editor_property(
        "transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_INSTANCE)
    # The unnamed first input: `GetExpressionInputByName` takes "" as "the first pin".
    connect(world_pos, "", local, "")
    bounds = g.node(unreal.MaterialExpressionObjectLocalBounds, x, y + 260)
    local_z = g.mask(local, "b", x + 400, y + 120)
    min_z = g.mask(bounds, "b", x + 200, y + 260, src_out="Min")
    max_z = g.mask(bounds, "b", x + 200, y + 340, src_out="Max")
    height = g.sub(max_z, "", min_z, "", x + 400, y + 300)
    rise = g.sub(local_z, "", min_z, "", x + 600, y + 200)
    weight = g.sat(g.div(rise, "", height, "", x + 800, y + 240), "", x + 1000, y + 240)

    phase_sum = g.add(g.mask(world_pos, "r", x + 200, y + 440), "",
                      g.mask(world_pos, "g", x + 200, y + 520), "", x + 400, y + 480)
    phase = g.mul(phase_sum, "", g.const(1.0 / 2.54, x + 400, y + 560), "", x + 600, y + 480)
    clock = g.add(g.time(x + 600, y + 400), "", phase, "", x + 800, y + 440)
    wave = g.sine(clock, "", x + 1000, y + 440)
    # `Period` 0 is the raw sine of the input (`UMaterialExpressionSine::Compile`); the default
    # 1.0 would wrap the clock once per second.
    wave.set_editor_property("period", 0.0)

    amplitude = g.mpc("DetailSwayAmplitude", x + 1000, y + 600)
    magnitude = g.mul(g.mul(wave, "", custom, "", x + 1200, y + 440), "",
                      g.mul(weight, "", amplitude, "", x + 1200, y + 560), "", x + 1400, y + 500)
    planar = g.append(magnitude, "", magnitude, "", x + 1600, y + 500)
    offset = g.append(planar, "", g.const(0.0, x + 1600, y + 580), "", x + 1800, y + 520)
    still = g.const3(0.0, 0.0, 0.0, x + 1800, y + 640)
    final = g.switch(switch_name, offset, still, x + 2000, y + 540, default=False)
    g.to(final, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)


def _uv_lanes(g, tex_scale_offset_name, *, base_scroll_names, bump_scroll_names=None):
    """`TexScaleOffset` transform (shared by every UV-consuming slot on the material), then one
    independent `Panner` per lane it is asked for. `(0, 0)` scroll rates are an exact `Panner`
    no-op, so no static switch gates them (design doc "The animation and scroll lanes"). Returns
    `(base_uv, bump_uv_or_None, tex_scale_offset_param, uv0)` -- `uv0` (M5 review fix) is the raw,
    untransformed `TextureCoordinate` node, for a caller (`M_V2_TwoTexture`'s `BaseTexture2` layer)
    whose own scale/offset vector must be independent of `TexScaleOffset`'s, not composed on top of
    the already-scaled/panned `base_uv`."""
    uv0 = g.node(unreal.MaterialExpressionTextureCoordinate, -1900, 0)
    tex_scale_offset = g.node(unreal.MaterialExpressionVectorParameter, -1900, 200)
    tex_scale_offset.set_editor_property("parameter_name", tex_scale_offset_name)
    tex_scale_offset.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 0.0, 0.0))
    # `VectorParameter`'s default ("") output is RGB only (3-wide), a real-editor fact; masking
    # "ba" needs the 4-wide "RGBA" output explicitly, or a ComponentMask asking for the 4th
    # channel off a 3-wide source is a real compile error ("Not enough components").
    scale = g.mask(tex_scale_offset, "rg", -1700, 160)
    offset = g.mask(tex_scale_offset, "ba", -1700, 260, src_out="RGBA")
    scaled_uv = g.mul(uv0, "", scale, "", -1500, 0)
    transformed_uv = g.add(scaled_uv, "", offset, "", -1300, 40)

    time_node = g.time(-1900, 420)

    def _panner(names, y):
        u = g.scalar(names[0], 0.0, -1900, y)
        v = g.scalar(names[1], 0.0, -1900, y + 60)
        speed = g.append(u, "", v, "", -1700, y + 30)
        p = g.panner(time_node, speed, -1500, y + 30)
        connect(transformed_uv, "", p, "Coordinate")
        return p

    base_uv = _panner(base_scroll_names, 500)
    bump_uv = _panner(bump_scroll_names, 620) if bump_scroll_names else None
    return base_uv, bump_uv, tex_scale_offset, uv0


def _sine_lane(g, *, min_name, max_name, period_name, offset_name, target_mask_name,
               channel_mask_name, x=-2900, y=700):
    """Shader-time sine modulation (design doc "The sine lane"), neutral by construction:
    `SineMin == SineMax == 1` and `SineTargetMask == (0,0,0,0)` by default make every apply() call
    an exact no-op. Returns a dict with the raw `SineValue` node and an `apply(node, node_out,
    target_channel, out_channels, x, y)` helper that gates `node`'s `out_channels` by
    `lerp(1, SineValue, SineTargetMask.<target_channel> * SineChannelMask)`.

    `x` defaults to its own column (`-2900`, review fix), clear of `_class_lut_influenced`'s fixed
    `-1900` column every master that also calls this lane places its class-LUT read at -- the two
    used to land on top of each other in the material editor (this lane's own `one4`/`channel_mask`
    nodes at `y+640..720` sat directly under `_class_lut_influenced`'s first row at its callers'
    `y=1400`, close enough to visually overlap). Purely a node-graph layout fix; neither lane reads
    the other's nodes."""
    sine_min = g.scalar(min_name, 1.0, x, y)
    sine_max = g.scalar(max_name, 1.0, x, y + 80)
    sine_period = g.scalar(period_name, 1.0, x, y + 160)
    sine_offset = g.scalar(offset_name, 0.0, x, y + 240)
    time_node = g.time(x, y + 320)
    phase_time = g.add(time_node, "", sine_offset, "", x + 220, y + 240)
    period_floor = g.const(0.0001, x + 220, y + 320)
    period_ceiling = g.const(3.4e38, x + 220, y + 400)
    safe_period = g.clamp(sine_period, "", period_floor, "", period_ceiling, "", x + 220, y + 480)
    normalized_time = g.div(phase_time, "", safe_period, "", x + 440, y + 240)
    wave = g.sine(normalized_time, "", x + 660, y + 240)
    one = g.const(1.0, x + 660, y + 320)
    half = g.const(0.5, x + 660, y + 400)
    wave_norm = g.mul(g.add(wave, "", one, "", x + 880, y + 240), "", half, "", x + 1100, y + 240)
    sine_range = g.sub(sine_max, "", sine_min, "", x + 220, y + 80)
    sine_value = g.add(g.mul(wave_norm, "", sine_range, "", x + 1320, y + 240), "", sine_min, "",
                       x + 1540, y + 240)

    target_mask = g.vec4(target_mask_name, (0.0, 0.0, 0.0, 0.0), x, y + 560)
    channel_mask = g.vec4(channel_mask_name, (1.0, 1.0, 1.0, 1.0), x, y + 640)
    # H2 review fix: the identity side of the `Lerp` must match the *result* width the caller
    # actually wants, not a blanket float4 -- `MaterialExpressionConstant4Vector`'s output is
    # float4 (`MaterialExpressions.cpp`'s own `Constant4Vector` compile path), and lerping that
    # against a float3 `Alpha` (`scaled`, below, for every vector target) makes the translator
    # coerce `Alpha` down to its first component instead of masking per-channel -- `$color[1]`
    # masks nothing, `$color[0]` modulates every channel, and the non-analytic path is a real HLSL
    # type error. `one3` keeps the vector-target lerp float3-in/float3-out/float3-alpha throughout;
    # `one` is the separate float1 identity for a scalar target (`Alpha` -- `out_channels == "r"`
    # length 1), where every term of the lerp is a true 1-wide scalar instead.
    one3 = g.const3(1.0, 1.0, 1.0, x, y + 720)
    one = g.const(1.0, x, y + 800)

    def apply(node, node_out, target_channel, out_channels, ax, ay):
        # `target_mask` is a raw VectorParameter; its default output is RGB only, so reading its
        # `.w`/alpha component (the EnvMapTint target) needs the explicit "RGBA" output. Always
        # requesting it here is simplest and correct for every channel, not just the 4th.
        target_component = g.mask(target_mask, target_channel, ax, ay, src_out="RGBA")
        if len(out_channels) == 1:
            # Scalar target (`Alpha`): keep every lerp term float1 so `Alpha` and the result stay
            # width-matched throughout -- a float3 `scaled` term into a float1 result is exactly
            # the H2 defect the vector branch below fixes, just on the other side of the mismatch.
            mask_component = g.mask(channel_mask, out_channels, ax + 220, ay + 40)
            scaled = g.mul(mask_component, "", target_component, "", ax + 220, ay)
            factor = g.lerp(one, "", sine_value, "", scaled, "", ax + 440, ay)
            return g.mul(node, node_out, factor, "", ax + 880, ay)
        scaled = g.mul(channel_mask, "", target_component, "", ax + 220, ay)
        factor3 = g.lerp(one3, "", sine_value, "", scaled, "", ax + 440, ay)
        factor = g.mask(factor3, out_channels, ax + 660, ay)
        return g.mul(node, node_out, factor, "", ax + 880, ay)

    return {"value": sine_value, "apply": apply}


def _flipbook_slice(g, rate_name, count_name, x, y):
    """`SliceIndex = floor(frac(Time * FrameRate) * FrameCount)` (design doc "The animation and
    scroll lanes"). `animationnowrap` (1 corpus unit) would clamp instead of wrapping; not built
    here -- no master's own graph distinguishes it, it is a stage-side clamp of the same scalar."""
    rate = g.scalar(rate_name, 0.0, x, y)
    count = g.scalar(count_name, 1.0, x, y + 80)
    time_node = g.time(x, y + 160)
    scaled_time = g.mul(time_node, "", rate, "", x + 220, y + 160)
    wrapped = g.frac(scaled_time, "", x + 440, y + 160)
    scaled = g.mul(wrapped, "", count, "", x + 660, y + 160)
    return g.floor(scaled, "", x + 880, y + 160)


def _flipbook_sample(g, sample2d, frames_param_name, uv, rate_name, count_name, use_switch_name,
                     default_frames_texture, x, y, *, sampler):
    """The flipbook lane for one texture slot: a `Texture2DArray` `TextureObjectParameter` sampled
    at `Append(uv, SliceIndex)`, selected over the plain 2D sample by `use_switch_name`. Every
    *base* caller passes `sampler="linear"` -- `BaseTextureFrames` binds `T_V2_DefaultFrames`, a
    non-sRGB `TC_Default` `Texture2DArray` (`_make_default_frames_array`), and
    `TextureObjectParameter` never auto-derives a sampler type from the bound texture asset (see
    `Graph.tex_object`'s docstring), unlike `tex`. A base-lane object explicitly typed
    `SAMPLERTYPE_COLOR` against that non-sRGB source is a real compile error ("Sampler type is
    Color, should be Linear Color") on every `UseAnimatedFrames=true` permutation -- the one
    `recompile_material`'s own default-false compile never visits, which is why the all-switches
    compile probe (`_probe_all_switches_true`) exists.

    Every *normal* caller (`NormalMapFrames`) passes `sampler="normal"` instead (H3 review fix):
    `SAMPLERTYPE_NORMAL` is what makes `HLSLMaterialTranslator.cpp` emit `UnpackNormalMap` for this
    sample at all (only for that sampler type, ~line 6951) -- sampled as `SAMPLERTYPE_LINEAR_COLOR`
    like the base lane, the tangent-normal texel never left `[0,1]` and every animated normal
    frame read as a washed-out, un-unpacked normal. `NormalMapFrames` binds its own default,
    `T_V2_DefaultNormalFrames` (`_make_default_normal_frames_array`), a flat-normal-packed
    `Texture2DArray` distinct from `T_V2_DefaultFrames` -- the base lane's all-white default would
    unpack to `(1,1,1)`, not flat `(0,0,1)`, if it were ever bound to a `SAMPLERTYPE_NORMAL` object.

    Both switch branches are connected through the explicit `"RGBA"` output: `sample2d` is a bare
    `TextureSampleParameter2D` and `array_sample` a bare `TextureSample`, and both node types'
    *default* output is RGB only -- feeding the switch through it would silently drop alpha for
    everything downstream of the returned node, including its own default output (see
    `Graph.switch`'s docstring)."""
    frames_object = g.tex_object(frames_param_name, x, y, default_frames_texture, sampler=sampler)
    slice_index = _flipbook_slice(g, rate_name, count_name, x, y + 160)
    array_uv = g.append(uv, "", slice_index, "", x + 220, y + 320)
    array_sample = g.sample(frames_object, array_uv, x + 440, y + 320)
    return g.switch(use_switch_name, array_sample, sample2d, x + 660, y + 320, default=False,
                    true_out="RGBA", false_out="RGBA")


def _class_lut(g, lut_param_name, lut_texture, index_name, x, y):
    class_index = g.scalar(index_name, 0.0, x, y)
    lut_object = g.tex_object(lut_param_name, x, y + 80, lut_texture, sampler="linear")
    lut_uv = class_lut_uv(g, class_index, x + 220, y)
    lut_sample = read_class_lut(g, lut_object, lut_uv, x + 660, y)
    # `.r`/`.g`/`.b` alone never need the 4th component, so the default RGB-only output suffices.
    return (g.mask(lut_sample, "r", x + 880, y - 40),
            g.mask(lut_sample, "g", x + 880, y),
            g.mask(lut_sample, "b", x + 880, y + 40))


def _class_lut_influenced(g, lut_param_name, lut_texture, index_name, x, y):
    """SF-4.3-part-3 cross-cutting ruling (a): every master's Roughness/Specular/Metallic
    computation becomes `lerp(DefaultRoughness, ClassRoughness, ClassInfluence)` and the same
    shape for Specular/Metallic, replacing the raw `_class_lut` triple at its source so every
    downstream user (the reflection-contract mask math, the envmap switch, the final `g.to()`)
    inherits the lerped values automatically. `ClassInfluence` is an `MPC_ElysiumSurfaces` scalar
    (default `1.0`) -- at the default the lerp is an exact identity, collapsing to the raw class
    row exactly as before this ruling landed. Used by every master whose shading model actually
    reads Roughness/Specular/Metallic (the five Default Lit masters: `M_V2_Lit`/
    `M_V2_LitTranslucent`, `M_V2_TwoTexture`, `M_V2_Eyes`, `M_V2_Water`, `M_V2_Refract`); the four
    Unlit masters (`M_V2_Unlit`, `M_V2_Sprite`, `M_V2_Decal`) still call the plain `_class_lut`
    (or, for `M_V2_Unlit`, declare it unwired) because an Unlit shading model ignores those pins
    entirely, per each function's own docstring."""
    class_roughness, class_specular, class_metallic = _class_lut(
        g, lut_param_name, lut_texture, index_name, x, y)
    class_influence = g.mpc("ClassInfluence", x, y + 900)
    default_roughness = g.mpc("DefaultRoughness", x, y + 980)
    default_specular = g.mpc("DefaultSpecular", x, y + 1060)
    default_metallic = g.mpc("DefaultMetallic", x, y + 1140)
    roughness = g.lerp(default_roughness, "", class_roughness, "", class_influence, "",
                       x + 1100, y + 980)
    specular = g.lerp(default_specular, "", class_specular, "", class_influence, "",
                      x + 1100, y + 1060)
    metallic = g.lerp(default_metallic, "", class_metallic, "", class_influence, "",
                      x + 1100, y + 1140)
    return roughness, specular, metallic


# ============================================================================================
# M_V2_Lit / M_V2_LitTranslucent
# ============================================================================================


def _scene_fog(g, base_color, base_out, emissive, emissive_out, specular, specular_out, x, y):
    """R5.4 (`seam_map_material.md` -> "Scene fog on the world masters"): Source's per-map distance
    fog as the per-primitive Custom Primitive Data term every world / 3D-skybox / prop primitive
    carries -- `mat_fog.fog_from_primitive`, the exact graph the legacy `M_World_*` masters run,
    reading the slots `ElysiumFog::Pack` writes (colour 0..3, start 4, 1/range 5) and
    `UElysiumMapVisuals::ApplySceneFog` re-stamps live. Neutral by construction: an unwritten slot
    reads 0, so `f = 0` and every output passes through untouched.

    Applied so the shaded result is exactly `lerp(shaded, fogColour, f)`, as the legacy graph does:
    `BaseColor *= 1 - f`, `Specular *= 1 - f` (or a Lumen reflection shines through the haze),
    `Emissive = Emissive * (1 - f) + fogColour * f * FogInscatter`. `FogInscatter` is the one
    instance-side value (`ElysiumSurfaceParams*::Scalars::FogInscatter`, default 1): the stage
    writes 0 for an `Additive` blend, because Source forces the fog colour to black under additive
    blending -- an additive surface fades out in fog rather than adding the haze on top.

    Returns `(base_color, emissive, specular)` nodes; `specular` is None when none was handed in
    (an Unlit shading model has no specular pin to fade).
    """
    fog_f, fog_inv, fog_color = mat_fog.fog_from_primitive(g.mat, x=x, y=y)
    inscatter_gate = g.scalar(mat_fog.P_INSCATTER, 1.0, x + 900, y - 180)
    fog_color_gated = g.mul(fog_color, "", inscatter_gate, "", x + 1080, y - 160)
    base_faded = mat_fog.fade(g.mat, base_color, base_out, fog_inv, x + 1100, y)
    emissive_faded = mat_fog.fade(g.mat, emissive, emissive_out, fog_inv, x + 1100, y + 120)
    emissive_fogged = mat_fog.inscatter(g.mat, emissive_faded, fog_f, fog_color_gated,
                                        x + 1300, y + 120)
    specular_faded = None
    if specular is not None:
        specular_faded = mat_fog.fade(g.mat, specular, specular_out, fog_inv, x + 1100, y + 300)
    return base_faded, emissive_fogged, specular_faded


class LitParams:
    class Textures:
        BaseTexture = "BaseTexture"
        NormalMap = "NormalMap"
        EnvMapMask = "EnvMapMask"
        EnvMap = "EnvMap"
        BaseTextureFrames = "BaseTextureFrames"
        NormalMapFrames = "NormalMapFrames"
        SurfaceClassLUT = "SurfaceClassLUT"

    class Scalars:
        Alpha = "Alpha"
        SurfaceClassIndex = "SurfaceClassIndex"
        SelfIllumAmount = "SelfIllumAmount"
        EnvMapMaskScale = "EnvMapMaskScale"
        BumpScale = "BumpScale"
        BaseScrollRateU = "BaseScrollRateU"
        BaseScrollRateV = "BaseScrollRateV"
        BumpScrollRateU = "BumpScrollRateU"
        BumpScrollRateV = "BumpScrollRateV"
        FrameRate = "FrameRate"
        FrameCount = "FrameCount"
        NormalFrameRate = "NormalFrameRate"
        NormalFrameCount = "NormalFrameCount"
        SineMin = "SineMin"
        SineMax = "SineMax"
        SinePeriod = "SinePeriod"
        SineTimeOffset = "SineTimeOffset"
        WetnessScale = "WetnessScale"
        WetnessDriven = "WetnessDriven"
        FogStart = mat_fog.P_START
        FogInvRange = mat_fog.P_INV_RANGE
        FogInscatter = mat_fog.P_INSCATTER

    class Vectors:
        Color = "Color"
        SelfIllumTint = "SelfIllumTint"
        EnvMapTint = "EnvMapTint"
        TexScaleOffset = "TexScaleOffset"
        SineTargetMask = "SineTargetMask"
        SineChannelMask = "SineChannelMask"
        FogColor = mat_fog.P_COLOR

    class Switches:
        UseBaseTexture = "UseBaseTexture"
        UseNormalMap = "UseNormalMap"
        UseSelfIllum = "UseSelfIllum"
        UseVertexColor = "UseVertexColor"
        UseVertexAlpha = "UseVertexAlpha"
        UseEnvMap = "UseEnvMap"
        UseEnvMapMask = "UseEnvMapMask"
        UseBaseAlphaEnvMapMask = "UseBaseAlphaEnvMapMask"
        UseNormalMapAlphaEnvMapMask = "UseNormalMapAlphaEnvMapMask"
        UseFixedCube = "UseFixedCube"
        MetallicTint = "MetallicTint"
        UseAnimatedFrames = "UseAnimatedFrames"
        UseAnimatedNormalFrames = "UseAnimatedNormalFrames"
        UseDetailSway = "UseDetailSway"


LIT_PARAM_TABLE = {
    "textures": sorted(vars(LitParams.Textures)[k] for k in vars(LitParams.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(LitParams.Scalars)[k] for k in vars(LitParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(LitParams.Vectors)[k] for k in vars(LitParams.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(LitParams.Switches)[k] for k in vars(LitParams.Switches) if not k.startswith("_")),
}

# Backwards-compatible alias: part 1 exposed a single flat `Params`/`PARAM_TABLE` for the one
# master it authored. Kept pointing at the Lit table so any external reference from that slice
# still resolves.
Params = LitParams
PARAM_TABLE = LIT_PARAM_TABLE


def _build_lit(mat, collection, environment_collection, lut_texture, default_frames,
               default_normal_frames, *, translucent):
    """The shared M_V2_Lit / M_V2_LitTranslucent shading graph. `translucent` only changes how
    Opacity is wired (design doc: blend mode itself is a per-instance override, not a
    material-only property, so the two masters share every other pin)."""
    g = Graph(mat, collection=collection)
    P = LitParams

    base_uv, bump_uv, _, _ = _uv_lanes(
        g, P.Vectors.TexScaleOffset,
        base_scroll_names=(P.Scalars.BaseScrollRateU, P.Scalars.BaseScrollRateV),
        bump_scroll_names=(P.Scalars.BumpScrollRateU, P.Scalars.BumpScrollRateV))
    sine = _sine_lane(
        g, min_name=P.Scalars.SineMin, max_name=P.Scalars.SineMax,
        period_name=P.Scalars.SinePeriod, offset_name=P.Scalars.SineTimeOffset,
        target_mask_name=P.Vectors.SineTargetMask, channel_mask_name=P.Vectors.SineChannelMask)

    # -- BaseTexture (+ flipbook) --------------------------------------------------------------
    base_tex_2d = g.tex(P.Textures.BaseTexture, -1100, -400, kind="color")
    connect(base_uv, "", base_tex_2d, "UVs")
    base_tex = _flipbook_sample(
        g, base_tex_2d, P.Textures.BaseTextureFrames, base_uv,
        P.Scalars.FrameRate, P.Scalars.FrameCount, P.Switches.UseAnimatedFrames, default_frames,
        -1100, -600, sampler="linear")
    # `base_tex` is now a Switch node, not a bare TextureSample -- it has no named "RGB"/"A"
    # sub-pins of its own (those exist only on the two node types it switches between), so every
    # downstream read goes through these two masked values rather than a named-output access on
    # `base_tex` directly.
    base_tex_rgb = g.mask(base_tex, "rgb", -900, -420)
    base_tex_a = g.mask(base_tex, "a", -900, -340)
    white3 = g.const3(1.0, 1.0, 1.0, -900, -260)
    base_selected = g.switch(P.Switches.UseBaseTexture, base_tex_rgb, white3, -700, -360,
                             default=True)

    # -- self-illum also darkens BaseColor by (1 - BaseTexture.a) -- the plain
    # lightmappedgeneric_selfilluminated.psh transcription (design doc "Post-lighting math per
    # master": `BaseColor *= (1 - BaseTexture.a); Emissive = BaseTexture.rgb * SelfIllumTint *
    # BaseTexture.a * SelfIllumAmount`) ------------------------------------------------------
    one_minus_base_a = g.one_minus(base_tex_a, "", -700, -140)
    one3 = g.const3(1.0, 1.0, 1.0, -700, -60)
    selfillum_basecolor_factor = g.switch(P.Switches.UseSelfIllum, one_minus_base_a, one3,
                                          -500, -140, default=False)
    base_darkened = g.mul(base_selected, "", selfillum_basecolor_factor, "", -300, -300)

    color = g.vec3(P.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -1100, -560)
    color = sine["apply"](color, "", "g", "rgb", -1100, -500)
    tinted = g.mul(base_darkened, "", color, "", -40, -360)
    vertex_color = g.vertex_color(-1100, -680)
    vc_rgb = g.mask(vertex_color, "rgb", -900, -680)
    with_vc = g.mul(tinted, "", vc_rgb, "", 160, -400)
    vc_selected = g.switch(P.Switches.UseVertexColor, with_vc, tinted, 360, -360, default=False)

    overbright = g.mpc("Overbright", -1100, -820)
    overbright_base = g.mul(vc_selected, "", overbright, "", 560, -360)

    # -- surface class lookup ------------------------------------------------------------------
    class_roughness, class_specular, class_metallic = _class_lut_influenced(
        g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)

    # -- reflection contract: mask term -------------------------------------------------------
    normal_tex_2d = g.tex(P.Textures.NormalMap, -1100, 60, kind="normal")
    connect(bump_uv, "", normal_tex_2d, "UVs")
    normal_tex = _flipbook_sample(
        g, normal_tex_2d, P.Textures.NormalMapFrames, bump_uv,
        P.Scalars.NormalFrameRate, P.Scalars.NormalFrameCount,
        P.Switches.UseAnimatedNormalFrames, default_normal_frames, -1100, 260, sampler="normal")
    normal_tex_rgb = g.mask(normal_tex, "rgb", -900, 60)
    normal_tex_a = g.mask(normal_tex, "a", -900, 140)
    envmapmask_tex = g.tex(P.Textures.EnvMapMask, -1100, 460, kind="mask",
                           default="%s/T_LinearWhiteMask" % PKG)
    connect(base_uv, "", envmapmask_tex, "UVs")
    luma_weights = g.const3(0.299, 0.587, 0.114, -900, 500)
    envmapmask_luma = g.dot(envmapmask_tex, "RGB", luma_weights, "", -700, 500)
    mask_none = g.const(1.0, -900, 580)
    mask_basealpha = g.one_minus(base_tex_a, "", -700, 580)
    mask_step1 = g.switch(P.Switches.UseBaseAlphaEnvMapMask, mask_basealpha, mask_none,
                          -500, 580, default=False)
    mask_step2 = g.switch(P.Switches.UseNormalMapAlphaEnvMapMask, normal_tex_a, mask_step1,
                          -300, 540, default=False)
    mask_step3 = g.switch(P.Switches.UseEnvMapMask, envmapmask_luma, mask_step2, -100, 540,
                          default=False)
    env_mask_scale = g.scalar(P.Scalars.EnvMapMaskScale, 1.0, -900, 660)
    mask_scaled = g.mul(mask_step3, "", env_mask_scale, "", 100, 580)
    # Authored wetness (R5.3, `seam_map_material.md` -> "Decal fog and wetness homes (R5.3)")
    # raises the reflection mask exactly like the legacy `M_World_*` masters' own `env_wet` term:
    # a rain-slicked surface reads reflective even where its own $envmapmask does not say so. A
    # surface with no `globalwetness` proxy carries `WetnessDriven` 0, so `wet_response` is
    # exactly 1 and this multiply is a no-op.
    wet_response = _wetness_response(g, environment_collection, P, -900, 2440)
    mask_wet = g.mul(mask_scaled, "", wet_response, "", 200, 620)
    mask_sat = g.sat(mask_wet, "", 300, 580)

    # -- specular / roughness / metallic -------------------------------------------------------
    mask_spec_scale = g.mpc("MaskSpecularScale", -1900, 1560)
    mask_rough_min = g.mpc("MaskRoughnessMin", -1900, 1640)
    mask_rough_max = g.mpc("MaskRoughnessMax", -1900, 1720)
    env_tint_scale = g.mpc("EnvTintScale", -1900, 1800)
    mask_metallic_max = g.mpc("MaskMetallicMax", -1900, 1880)
    chromatic_tint_strength = g.mpc("ChromaticTintStrength", -1900, 1960)
    env_tint = g.vec3(P.Vectors.EnvMapTint, (1.0, 1.0, 1.0, 1.0), -1900, 2040)
    env_tint = sine["apply"](env_tint, "", "a", "rgb", -1900, 2120)
    tint_luma = g.dot(env_tint, "", luma_weights, "", -1700, 2040)

    spec_common = g.mul(class_specular, "",
                        g.add(g.const(1.0, 500, 1560), "",
                              g.mul(mask_spec_scale, "", mask_sat, "", 500, 1620), "",
                              700, 1600), "", 900, 1560)
    spec_grey = g.mul(spec_common, "", g.mul(tint_luma, "", env_tint_scale, "", 900, 1720),
                      "", 1100, 1680)
    spec_envmap = g.switch(P.Switches.MetallicTint, spec_common, spec_grey, 1300, 1600,
                           default=False)
    specular_final = g.switch(P.Switches.UseEnvMap, spec_envmap, class_specular, 1500, 1500,
                              default=False)

    rough_reflective = g.lerp(mask_rough_max, "", mask_rough_min, "", mask_sat, "", 900, 1760)
    roughness_final = g.switch(P.Switches.UseEnvMap, rough_reflective, class_roughness,
                               1500, 1760, default=False)

    # Chromatic metallic branch (`Metallic = mask * MaskMetallicMax`), suppressed under
    # `UseFixedCube` (design doc "The authored fixed cube ... beats the chromatic branch"): a
    # fixed-cube unit's `MetallicTint` is already off at the stage, and this nested switch is
    # defence-in-depth against the two ever being set true together.
    metallic_chromatic = g.mul(mask_sat, "", mask_metallic_max, "", 1100, 1840)
    metallic_chromatic_gated = g.switch(P.Switches.MetallicTint, metallic_chromatic, class_metallic,
                                        1300, 1840, default=False)
    metallic_envmap = g.switch(P.Switches.UseFixedCube, class_metallic, metallic_chromatic_gated,
                               1400, 1840, default=False)
    metallic_final = g.switch(P.Switches.UseEnvMap, metallic_envmap, class_metallic,
                              1500, 1840, default=False)

    # -- BaseColor: chromatic $envmaptint lerps BaseColor toward BaseColor*EnvMapTint by
    # ChromaticTintStrength; grey does not (it only scales Specular, above); suppressed under
    # UseFixedCube for the same reason as the metallic branch --------------------------------
    base_tinted_chromatic = g.mul(overbright_base, "", env_tint, "", 900, -280)
    base_color_chromatic = g.lerp(overbright_base, "", base_tinted_chromatic, "",
                                  chromatic_tint_strength, "", 1000, -220)
    base_color_chromatic_gated = g.switch(P.Switches.MetallicTint, base_color_chromatic,
                                          overbright_base, 1100, -280, default=False)
    base_color_envmap = g.switch(P.Switches.UseFixedCube, overbright_base,
                                 base_color_chromatic_gated, 1200, -280, default=False)
    base_color_final = g.switch(P.Switches.UseEnvMap, base_color_envmap, overbright_base,
                                1300, -280, default=False)

    # -- Normal ---------------------------------------------------------------------------------
    flat_normal = g.const3(0.0, 0.0, 1.0, -700, 140)
    bump_scale = g.scalar(P.Scalars.BumpScale, 1.0, -700, 220)
    normal_blended = g.lerp(flat_normal, "", normal_tex_rgb, "", bump_scale, "", -500, 180)
    normal_final = g.switch(P.Switches.UseNormalMap, normal_blended, flat_normal, -700, 180,
                            default=False)
    g.to(normal_final, "", unreal.MaterialProperty.MP_NORMAL)

    # -- Emissive: self-illum + fixed-cube reflection (RayTracingQualitySwitch-gated) ---------
    selfillum_amount = g.scalar(P.Scalars.SelfIllumAmount, 0.0, -1100, 620)
    selfillum_tint = g.vec3(P.Vectors.SelfIllumTint, (1.0, 1.0, 1.0, 1.0), -1100, 700)
    selfillum_tint = sine["apply"](selfillum_tint, "", "b", "rgb", -1100, 760)
    selfillum_raw = g.mul(g.mul(base_tex_rgb, "", selfillum_tint, "", -900, 660), "",
                          g.mul(base_tex_a, "", selfillum_amount, "", -900, 740), "", -700, 700)
    black3 = g.const3(0.0, 0.0, 0.0, -900, 820)
    selfillum_emissive = g.switch(P.Switches.UseSelfIllum, selfillum_raw, black3, -500, 740,
                                  default=False)

    reflect_dir = g.reflection_ws(-1900, 2200)
    envcube = g.cube(P.Textures.EnvMap, -1700, 2200, default=DEFAULT_CUBE)
    connect(reflect_dir, "", envcube, "UVs")
    fixed_cube_strength = g.mpc("FixedCubeStrength", -1900, 2300)
    fixed_raw = g.mul(g.mul(envcube, "RGB", mask_sat, "", -1500, 2240), "",
                      g.mul(env_tint, "", fixed_cube_strength, "", -1500, 2320), "", -1300, 2280)
    lumen_safe_fixed = g.node(unreal.MaterialExpressionRayTracingQualitySwitch, -1100, 2200)
    connect(fixed_raw, "", lumen_safe_fixed, "Normal")
    connect(black3, "", lumen_safe_fixed, "RayTraced")
    fixed_emissive = g.switch(P.Switches.UseFixedCube, lumen_safe_fixed, black3, -900, 2200,
                              default=False)

    total_emissive = g.add(selfillum_emissive, "", fixed_emissive, "", -300, 780)

    # -- Scene fog (R5.4): the per-primitive CPD term, last, over the three outputs it fades -----
    base_color_fogged, emissive_fogged, specular_fogged = _scene_fog(
        g, base_color_final, "", total_emissive, "", specular_final, "", -1600, 3000)
    g.to(base_color_fogged, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.to(emissive_fogged, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.to(specular_fogged, "", unreal.MaterialProperty.MP_SPECULAR)
    g.to(roughness_final, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to(metallic_final, "", unreal.MaterialProperty.MP_METALLIC)

    # -- Opacity / OpacityMask -----------------------------------------------------------------
    # `VertexColor`'s five outputs are all UNNAMED FNames -- `GetExpressionOutputIndexByName`
    # only resolves an unnamed output by single-letter R/G/B/A (mask-pattern fallback); there is
    # no "RGBA" output to request at all (unlike VectorParameter/TextureSample, which really do
    # have one). Connecting straight to the node's own "A" output (already exactly 1-wide) rather
    # than wrapping it in a `ComponentMask` requesting "a" is also the fix for the second half of
    # the same bug: a `ComponentMask` positionally indexes channel 3 for "A", so masking "a" off
    # an already-1-wide source is itself a "not enough components" error, independent of the
    # naming question.
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    alpha_with_sine = sine["apply"](alpha_param, "", "r", "r", 1900, 0)

    if translucent:
        # LitTranslucent: Opacity = Alpha x BaseTexture.a (x VertexColor.a under UseVertexAlpha)
        # -- design doc "Author M_V2_LitTranslucent".
        alpha_x_basetex = g.mul(alpha_with_sine, "", base_tex_a, "", 2100, 0)
        alpha_with_vc = g.mul(alpha_x_basetex, "", vertex_color, "A", 2300, 40)
        opacity_final = g.switch(P.Switches.UseVertexAlpha, alpha_with_vc, alpha_x_basetex,
                                 2500, 20, default=False)
    else:
        alpha_with_vc = g.mul(alpha_with_sine, "", vertex_color, "A", 2100, 40)
        opacity_final = g.switch(P.Switches.UseVertexAlpha, alpha_with_vc, alpha_with_sine,
                                 2300, 20, default=False)
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY)

    one_const = g.const(1.0, 1700, 200)
    opacity_mask = g.switch(P.Switches.UseSelfIllum, one_const, base_tex_a, 1900, 200,
                            default=False)
    g.to(opacity_mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    # -- Detail sway (R6.3): the one World Position Offset term, behind UseDetailSway ----------
    _detail_sway(g, P.Switches.UseDetailSway, -1900, 3400)


def _lit_recipe(cited_units):
    return {
        "graphVersion": GRAPH_VERSION,
        "sourceHash": _source_hash(),
        "citedUnits": _cited_unit_hashes(cited_units),
        "params": LIT_PARAM_TABLE,
        "mpcScalars": REQUIRED_MPC_SCALARS,
    }


def _make_lit_master(name, *, translucent):
    asset = "%s/%s" % (PKG, name)
    # Loaded -- and its required rows validated -- before the skip check, so a rerun that would
    # otherwise report "up to date, skipping" still fails loudly if a prerequisite asset (or one
    # of its rows) has since gone missing, rather than only checking it on the slow rebuild path.
    collection = _load_surfaces_collection()
    environment_collection = _load_environment_collection()
    lut_texture = _load_class_lut()
    recipe = _lit_recipe(LIT_CITED_SHADER_UNITS)
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset) == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    _make_linear_white_mask(force=force)
    default_frames = _make_default_frames_array(force=force)
    default_normal_frames = _make_default_normal_frames_array(force=force)

    mat, asset = _fresh(name, ism=True, nanite=True, skeletal=True, morph=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    if translucent:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property(
            "translucency_lighting_mode",
            unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    else:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", False)

    _build_lit(mat, collection, environment_collection, lut_texture, default_frames,
              default_normal_frames, translucent=translucent)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, LIT_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint)
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


def make_lit():
    return _make_lit_master("M_V2_Lit", translucent=False)


def make_lit_translucent():
    return _make_lit_master("M_V2_LitTranslucent", translucent=True)


# ============================================================================================
# M_V2_Unlit
# ============================================================================================


class UnlitParams:
    class Textures:
        BaseTexture = "BaseTexture"
        EnvMapMask = "EnvMapMask"
        EnvMap = "EnvMap"
        CloudAlphaTexture = "CloudAlphaTexture"
        BaseTextureFrames = "BaseTextureFrames"
        SurfaceClassLUT = "SurfaceClassLUT"

    class Scalars:
        Alpha = "Alpha"
        SurfaceClassIndex = "SurfaceClassIndex"
        EnvMapMaskScale = "EnvMapMaskScale"
        BaseScrollRateU = "BaseScrollRateU"
        BaseScrollRateV = "BaseScrollRateV"
        FrameRate = "FrameRate"
        FrameCount = "FrameCount"
        SineMin = "SineMin"
        SineMax = "SineMax"
        SinePeriod = "SinePeriod"
        SineTimeOffset = "SineTimeOffset"
        FogStart = mat_fog.P_START
        FogInvRange = mat_fog.P_INV_RANGE
        FogInscatter = mat_fog.P_INSCATTER

    class Vectors:
        Color = "Color"
        EnvMapTint = "EnvMapTint"
        TexScaleOffset = "TexScaleOffset"
        CloudScale = "CloudScale"
        SineTargetMask = "SineTargetMask"
        SineChannelMask = "SineChannelMask"
        FogColor = mat_fog.P_COLOR

    class Switches:
        UseBaseTexture = "UseBaseTexture"
        UseVertexColor = "UseVertexColor"
        UseVertexAlpha = "UseVertexAlpha"
        UseEnvMap = "UseEnvMap"
        UseEnvMapMask = "UseEnvMapMask"
        UseBaseAlphaEnvMapMask = "UseBaseAlphaEnvMapMask"
        UseFixedCube = "UseFixedCube"
        MetallicTint = "MetallicTint"
        UseAnimatedFrames = "UseAnimatedFrames"
        UseCloudAlpha = "UseCloudAlpha"
        UseDetailSway = "UseDetailSway"


UNLIT_PARAM_TABLE = {
    "textures": sorted(vars(UnlitParams.Textures)[k] for k in vars(UnlitParams.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(UnlitParams.Scalars)[k] for k in vars(UnlitParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(UnlitParams.Vectors)[k] for k in vars(UnlitParams.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(UnlitParams.Switches)[k] for k in vars(UnlitParams.Switches) if not k.startswith("_")),
}


def _build_unlit(mat, collection, lut_texture, default_frames):
    g = Graph(mat, collection=collection)
    P = UnlitParams

    base_uv, _, _, _ = _uv_lanes(
        g, P.Vectors.TexScaleOffset,
        base_scroll_names=(P.Scalars.BaseScrollRateU, P.Scalars.BaseScrollRateV))
    sine = _sine_lane(
        g, min_name=P.Scalars.SineMin, max_name=P.Scalars.SineMax,
        period_name=P.Scalars.SinePeriod, offset_name=P.Scalars.SineTimeOffset,
        target_mask_name=P.Vectors.SineTargetMask, channel_mask_name=P.Vectors.SineChannelMask)

    base_tex_2d = g.tex(P.Textures.BaseTexture, -1100, -400, kind="color")
    connect(base_uv, "", base_tex_2d, "UVs")
    base_tex = _flipbook_sample(
        g, base_tex_2d, P.Textures.BaseTextureFrames, base_uv,
        P.Scalars.FrameRate, P.Scalars.FrameCount, P.Switches.UseAnimatedFrames, default_frames,
        -1100, -600, sampler="linear")
    base_tex_rgb = g.mask(base_tex, "rgb", -900, -420)
    base_tex_a = g.mask(base_tex, "a", -900, -340)
    white3 = g.const3(1.0, 1.0, 1.0, -900, -260)
    base_selected = g.switch(P.Switches.UseBaseTexture, base_tex_rgb, white3, -700, -360,
                             default=True)

    color = g.vec3(P.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -1100, -560)
    color = sine["apply"](color, "", "g", "rgb", -1100, -500)
    tinted = g.mul(base_selected, "", color, "", -40, -360)
    vertex_color = g.vertex_color(-1100, -680)
    vc_rgb = g.mask(vertex_color, "rgb", -900, -680)
    with_vc = g.mul(tinted, "", vc_rgb, "", 160, -400)
    vc_selected = g.switch(P.Switches.UseVertexColor, with_vc, tinted, 360, -360, default=False)

    # -- reflection contract mask term (no NormalMap on this master) -------------------------
    envmapmask_tex = g.tex(P.Textures.EnvMapMask, -1100, 260, kind="mask",
                           default="%s/T_LinearWhiteMask" % PKG)
    connect(base_uv, "", envmapmask_tex, "UVs")
    luma_weights = g.const3(0.299, 0.587, 0.114, -900, 300)
    envmapmask_luma = g.dot(envmapmask_tex, "RGB", luma_weights, "", -700, 300)
    mask_none = g.const(1.0, -900, 380)
    mask_basealpha = g.one_minus(base_tex_a, "", -700, 380)
    mask_step1 = g.switch(P.Switches.UseBaseAlphaEnvMapMask, mask_basealpha, mask_none,
                          -500, 380, default=False)
    mask_step2 = g.switch(P.Switches.UseEnvMapMask, envmapmask_luma, mask_step1, -300, 380,
                          default=False)
    env_mask_scale = g.scalar(P.Scalars.EnvMapMaskScale, 1.0, -900, 460)
    mask_scaled = g.mul(mask_step2, "", env_mask_scale, "", -100, 400)
    mask_sat = g.sat(mask_scaled, "", 100, 400)

    env_tint = g.vec3(P.Vectors.EnvMapTint, (1.0, 1.0, 1.0, 1.0), -1900, 1880)
    env_tint = sine["apply"](env_tint, "", "a", "rgb", -1900, 1960)

    # -- unlit surface: the cube term IS transcribed as the literal additive Emissive of the
    # reflection contract whenever a cube exists (design doc "M_V2_Unlit" post-lighting math --
    # there is no Lumen lighting for an unlit surface to replace, so no RayTracingQualitySwitch
    # gate is needed here either) --------------------------------------------------------------
    reflect_dir = g.reflection_ws(-1900, 2000)
    envcube = g.cube(P.Textures.EnvMap, -1700, 2000, default=DEFAULT_CUBE)
    connect(reflect_dir, "", envcube, "UVs")
    # `cube x mask x EnvMapTint x FixedCubeStrength` -- the reflection contract's full triple
    # (review fix: FixedCubeStrength was not read at all on this master).
    fixed_cube_strength = g.mpc("FixedCubeStrength", -1900, 2280)
    cube_add = g.mul(
        g.mul(g.mul(envcube, "RGB", mask_sat, "", -1500, 2040), "", env_tint, "", -1300, 2080),
        "", fixed_cube_strength, "", -1100, 2120)
    cube_emissive = g.switch(P.Switches.UseEnvMap, cube_add, g.const3(0.0, 0.0, 0.0, -1100, 2040),
                             -900, 2000, default=False)
    # UseFixedCube is declared per the exposed-parameter table but this master has no second cube
    # path to switch onto (a single EnvMap slot covers both the concrete-image and env_cubemap
    # cases; there is no Lit-style Fresnel/RayTracingQualitySwitch split here) -- UseEnvMap alone
    # gates the cube term above, and this is a declared-not-wired node like
    # M_V2_TwoTexture's UseBumpOnBaseTexture2.
    g.switch(P.Switches.UseFixedCube, g.const(1.0, -900, 2120), g.const(0.0, -900, 2200),
            -700, 2160, default=False)
    # MetallicTint: declared per the exposed-parameter table; this master has no Metallic pin at
    # all (Unlit shading model), so like UseFixedCube above it is declared, not wired.
    g.switch(P.Switches.MetallicTint, g.const(1.0, -500, 2120), g.const(0.0, -500, 2200),
            -300, 2160, default=False)

    total_emissive = g.add(vc_selected, "", cube_emissive, "", 560, -200)

    # -- Scene fog (R5.4): an Unlit shading model renders Emissive alone, so the inscatter lands
    # there; BaseColor takes the same fade for the property's own consumers (no specular pin) ----
    base_color_fogged, emissive_fogged, _ = _scene_fog(
        g, vc_selected, "", total_emissive, "", None, "", -1600, 3000)
    g.to(emissive_fogged, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.to(base_color_fogged, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # -- Opacity: BaseTexture.a x VertexColor.a (design doc "M_V2_Unlit" post-lighting math --
    # unlitgeneric_envmapmask.psh's co-issued `mul r0.a, t0, v0`), gated by Alpha and the sine
    # lane like every other master -------------------------------------------------------------
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    alpha_with_sine = sine["apply"](alpha_param, "", "r", "r", 1900, 0)
    opacity_base = g.mul(alpha_with_sine, "", base_tex_a, "", 2100, 40)
    # `VertexColor`'s outputs are all unnamed FNames -- connect straight to its own "A" output
    # (already 1-wide) rather than through a ComponentMask; see M_V2_Lit's Opacity section for
    # why that combination is a "not enough components" error either way it is broken.
    opacity_with_vc = g.mul(opacity_base, "", vertex_color, "A", 2300, 80)
    opacity_final = g.switch(P.Switches.UseVertexAlpha, opacity_with_vc, opacity_base,
                             2500, 60, default=False)
    # Minor review fix: the plain `g.to(opacity_final, ...)` pair used to land here, then get
    # immediately overridden by the cloud-gated pair below on every master build (a dead double
    # connect -- `MP_OPACITY`/`MP_OPACITY_MASK` end up wired to `opacity_with_cloud` regardless).
    # `opacity_final` still feeds `opacity_with_cloud`'s `False` branch, so nothing about the
    # result changes; only the redundant first connect is gone.

    # -- CloudAlphaTexture / CloudScale: $cloudalphatexture drives opacity, $cloudscale the UV
    # scale (design doc "The eight real unresolved families" -> cloud). Declared and sampled so
    # UseCloudAlpha has a real term to gate even though only 2 corpus materials use it. --------
    cloud_scale = g.vec3(P.Vectors.CloudScale, (1.0, 1.0, 1.0, 0.0), -1100, 900)
    cloud_uv = g.mul(base_uv, "", g.mask(cloud_scale, "rg", -900, 940), "", -700, 900)
    cloud_tex = g.tex(P.Textures.CloudAlphaTexture, -1100, 1020, kind="mask",
                      default="%s/T_LinearWhiteMask" % PKG)
    connect(cloud_uv, "", cloud_tex, "UVs")
    # Minor review fix: the corpus's own two `cloud` sidecars (`import/textures/shadertest/
    # cloudalpha*.provenance.json`) stage `$cloudalphatexture` as DXT5/BC3 -- a format chosen for
    # its real per-pixel alpha, not for RGB -- and the name is "cloud**alpha**texture", not
    # "cloudmasktexture": the payload this slot exists to carry is the texture's Alpha channel, not
    # its Red. `.r` was never verified against either shipped unit and silently read the wrong
    # channel. `TextureSampleParameter2D`'s default output is RGB-only (real-editor fact repeated
    # throughout this file), so reading `.a` needs the explicit `"RGBA"` output, same as every
    # other alpha read in this module.
    cloud_alpha = g.mask(cloud_tex, "a", -700, 1020, src_out="RGBA")
    cloud_gated_opacity = g.mul(opacity_final, "", cloud_alpha, "", 2700, 60)
    opacity_with_cloud = g.switch(P.Switches.UseCloudAlpha, cloud_gated_opacity, opacity_final,
                                  2900, 40, default=False)
    # UseCloudAlpha overrides the plain opacity wiring above -- reconnect both property sinks to
    # the cloud-gated result rather than leaving the two switches racing each other.
    g.to(opacity_with_cloud, "", unreal.MaterialProperty.MP_OPACITY)
    g.to(opacity_with_cloud, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    # class LUT declared for contract completeness ("every master exposes SurfaceClassLUT"); an
    # Unlit shading model ignores MP_ROUGHNESS/SPECULAR/METALLIC, so it is not wired to anything.
    _class_lut(g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)

    # -- Detail sway (R6.3): every corpus detail material is `unlitgeneric`, so this is the master
    # the weeds actually swing on. Same term, same switch, as the Lit pair. --------------------
    _detail_sway(g, P.Switches.UseDetailSway, -1900, 2400)


def make_unlit():
    name = "M_V2_Unlit"
    asset = "%s/%s" % (PKG, name)
    collection = _load_surfaces_collection()
    lut_texture = _load_class_lut()
    recipe = {
        "graphVersion": GRAPH_VERSION,
        "sourceHash": _source_hash(),
        "citedUnits": _cited_unit_hashes(UNLIT_CITED_SHADER_UNITS),
        "params": UNLIT_PARAM_TABLE,
        "mpcScalars": REQUIRED_MPC_SCALARS,
    }
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset) == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    _make_linear_white_mask(force=force)
    default_frames = _make_default_frames_array(force=force)

    # H4 review fix: the stage routes every model family with no master of its own (
    # `vertexlitgeneric_dx6`, `eyeball`, `shadowmodel`, `camo`, `burnpeel`, `redvision`,
    # `gooinglass` and the rest of `NO_MASTER_FAMILIES`, `importers/materials.py`) onto
    # `M_V2_Unlit` -- several of those are character/prop model geometry, so this master needs
    # `used_with_skeletal_mesh`/`used_with_morph_targets` the same as `M_V2_Lit`/`M_V2_Eyes`, not
    # only the world/ISM flags it already carried. Checked read-only against `importers/
    # materials.py`'s own routing (owned by another agent, not edited here): no `NO_MASTER_FAMILIES`
    # or model-family unit routes to `M_V2_TwoTexture` (its own routing is `worldvertextransition`/
    # `worldtwotextureblend` only), so that master does not need the same flags added.
    # `used_with_niagara_sprites` is dropped -- `M_V2_Sprite` is the design's own Niagara-sprite
    # master (doc "M_V2_Sprite" -> `used_with_instanced_static_meshes`), and nothing in the doc's
    # usage-flag notes requires it on Unlit as well.
    mat, asset = _fresh(name, ism=True, nanite=True, skeletal=True, morph=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", False)

    _build_unlit(mat, collection, lut_texture, default_frames)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, UNLIT_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint)
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


# ============================================================================================
# M_V2_TwoTexture
# ============================================================================================


class TwoTextureParams:
    class Textures:
        BaseTexture = "BaseTexture"
        BaseTexture2 = "BaseTexture2"
        NormalMap = "NormalMap"
        SurfaceClassLUT = "SurfaceClassLUT"

    class Scalars:
        Alpha = "Alpha"
        SurfaceClassIndex = "SurfaceClassIndex"
        AlphaBias = "AlphaBias"
        BaseScrollRateU = "BaseScrollRateU"
        BaseScrollRateV = "BaseScrollRateV"
        SineMin = "SineMin"
        SineMax = "SineMax"
        SinePeriod = "SinePeriod"
        SineTimeOffset = "SineTimeOffset"
        FogStart = mat_fog.P_START
        FogInvRange = mat_fog.P_INV_RANGE
        FogInscatter = mat_fog.P_INSCATTER

    class Vectors:
        Color = "Color"
        TexScaleOffset = "TexScaleOffset"
        Texture2ScaleOffset = "Texture2ScaleOffset"
        SineTargetMask = "SineTargetMask"
        SineChannelMask = "SineChannelMask"
        FogColor = mat_fog.P_COLOR

    class Switches:
        UseBaseTexture2 = "UseBaseTexture2"
        UseNormalMap = "UseNormalMap"
        UseBumpOnBaseTexture2 = "UseBumpOnBaseTexture2"
        UseVertexColor = "UseVertexColor"
        UseVertexAlpha = "UseVertexAlpha"


TWOTEXTURE_PARAM_TABLE = {
    "textures": sorted(vars(TwoTextureParams.Textures)[k] for k in vars(TwoTextureParams.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(TwoTextureParams.Scalars)[k] for k in vars(TwoTextureParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(TwoTextureParams.Vectors)[k] for k in vars(TwoTextureParams.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(TwoTextureParams.Switches)[k] for k in vars(TwoTextureParams.Switches) if not k.startswith("_")),
}


def _build_two_texture(mat, collection, lut_texture):
    g = Graph(mat, collection=collection)
    P = TwoTextureParams

    base_uv, _, _, uv0 = _uv_lanes(
        g, P.Vectors.TexScaleOffset,
        base_scroll_names=(P.Scalars.BaseScrollRateU, P.Scalars.BaseScrollRateV))
    sine = _sine_lane(
        g, min_name=P.Scalars.SineMin, max_name=P.Scalars.SineMax,
        period_name=P.Scalars.SinePeriod, offset_name=P.Scalars.SineTimeOffset,
        target_mask_name=P.Vectors.SineTargetMask, channel_mask_name=P.Vectors.SineChannelMask)

    # M5 review fix: BaseTexture2 rides its own TexScaleOffset-shaped transform
    # (Texture2ScaleOffset), independent of TexScaleOffset -- derived from the raw, untransformed
    # `uv0` (`_uv_lanes`'s 4th return), not composed on top of `base_uv` (which already carries
    # TexScaleOffset's own scale/offset plus its scroll panner). Composing the two made layer 2's
    # UV a function of layer 1's transform, contradicting the design doc's "two independent
    # vectors" framing for `TexScaleOffset`/`Texture2ScaleOffset`. Laid out on its own row
    # (`y=900`, review fix), clear of the UV lane's own scale/offset/panner nodes above it.
    tex2_scale_offset = g.vec4(P.Vectors.Texture2ScaleOffset, (1.0, 1.0, 0.0, 0.0), -1900, 900)
    tex2_scale = g.mask(tex2_scale_offset, "rg", -1700, 860)
    tex2_offset = g.mask(tex2_scale_offset, "ba", -1700, 960, src_out="RGBA")
    base_uv2 = g.add(g.mul(uv0, "", tex2_scale, "", -1500, 900), "", tex2_offset, "",
                     -1300, 940)

    base_tex = g.tex(P.Textures.BaseTexture, -1100, -400, kind="color")
    connect(base_uv, "", base_tex, "UVs")
    base_tex2 = g.tex(P.Textures.BaseTexture2, -1100, -180, kind="color")
    connect(base_uv2, "", base_tex2, "UVs")

    vertex_color = g.vertex_color(-1100, -680)
    # `VertexColor`'s outputs are all unnamed FNames -- connect straight to its own "A" output
    # (already 1-wide) rather than through a ComponentMask; see M_V2_Lit's Opacity section.
    blend_alpha = g.one_minus(vertex_color, "A", -700, -680)
    blended = g.lerp(base_tex, "RGB", base_tex2, "RGB", blend_alpha, "", -500, -300)
    base_selected = g.switch(P.Switches.UseBaseTexture2, blended, g.mask(base_tex, "rgb", -700, -420),
                             -300, -360, default=False)

    color = g.vec3(P.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -1100, -560)
    color = sine["apply"](color, "", "g", "rgb", -1100, -500)
    tinted = g.mul(base_selected, "", color, "", -40, -360)
    vc_rgb = g.mask(vertex_color, "rgb", -900, -600)
    with_vc = g.mul(tinted, "", vc_rgb, "", 160, -400)
    vc_selected = g.switch(P.Switches.UseVertexColor, with_vc, tinted, 360, -360, default=False)

    overbright = g.mpc("Overbright", -1100, -820)
    overbright_base = g.mul(vc_selected, "", overbright, "", 560, -360)

    # -- Normal: one shared NormalMap slot, but which layer's UV feeds it is now real (minor
    # review fix: `UseBumpOnBaseTexture2` used to be declared and never wired to anything --
    # a no-op switch node with no consumer). `true`: sample through `base_uv2` (BaseTexture2's own
    # transform); `false` (default): sample through `base_uv`, the prior unconditional behaviour.
    normal_uv = g.switch(P.Switches.UseBumpOnBaseTexture2, base_uv2, base_uv, -1300, 0,
                         default=False)
    normal_tex = g.tex(P.Textures.NormalMap, -1100, 60, kind="normal")
    connect(normal_uv, "", normal_tex, "UVs")
    flat_normal = g.const3(0.0, 0.0, 1.0, -700, 140)
    normal_final = g.switch(P.Switches.UseNormalMap, g.mask(normal_tex, "rgb", -700, 60),
                            flat_normal, -500, 100, default=False)
    g.to(normal_final, "", unreal.MaterialProperty.MP_NORMAL)

    # -- surface class lookup, unwired past declaration (this master's Specular/Roughness/
    # Metallic follow the same class-LUT-alone convention M_V2_Lit's non-$envmap branch already
    # uses; see the module docstring's doc-gap note #1) ---------------------------------------
    class_roughness, class_specular, class_metallic = _class_lut_influenced(
        g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)

    # -- Scene fog (R5.4): this master has no emissive term of its own, so the fogged emissive is
    # the inscatter alone (`black * (1 - f) + fogColour * f`) ------------------------------------
    black3 = g.const3(0.0, 0.0, 0.0, -1700, 2900)
    base_color_fogged, emissive_fogged, specular_fogged = _scene_fog(
        g, overbright_base, "", black3, "", class_specular, "", -1600, 3000)
    g.to(base_color_fogged, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.to(emissive_fogged, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.to(class_roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to(specular_fogged, "", unreal.MaterialProperty.MP_SPECULAR)
    g.to(class_metallic, "", unreal.MaterialProperty.MP_METALLIC)

    # -- Opacity / OpacityMask: Alpha + AlphaBias (x VertexColor.a under UseVertexAlpha), saturated
    # -- see the module docstring's doc-gap note #2. M6 review fix: the unsaturated sum could push
    # past `[0, 1]` (`AlphaBias` is an additive bias, not a fraction), and OpacityMask had no source
    # at all -- a masked-override instance of this master clipped nothing regardless of Alpha. Both
    # property sinks now read the same saturated term, the same pattern every other Unlit-shaded
    # master in this file (Unlit/Sprite/Eyes) already uses for its own Opacity/OpacityMask pair. --
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    alpha_bias = g.scalar(P.Scalars.AlphaBias, 0.0, 1700, 80)
    alpha_with_sine = sine["apply"](alpha_param, "", "r", "r", 1900, 0)
    alpha_biased = g.add(alpha_with_sine, "", alpha_bias, "", 2100, 40)
    alpha_biased_sat = g.sat(alpha_biased, "", 2100, 120)
    # `VertexColor`'s outputs are all unnamed FNames -- connect straight to its own "A" output
    # (already 1-wide) rather than through a ComponentMask; see M_V2_Lit's Opacity section.
    alpha_with_vc = g.mul(alpha_biased_sat, "", vertex_color, "A", 2300, 100)
    opacity_final = g.switch(P.Switches.UseVertexAlpha, alpha_with_vc, alpha_biased_sat, 2500, 80,
                             default=False)
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY)
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY_MASK)


def make_two_texture():
    name = "M_V2_TwoTexture"
    asset = "%s/%s" % (PKG, name)
    collection = _load_surfaces_collection()
    lut_texture = _load_class_lut()
    recipe = {
        "graphVersion": GRAPH_VERSION,
        "sourceHash": _source_hash(),
        "citedUnits": _cited_unit_hashes(TWOTEXTURE_CITED_SHADER_UNITS),
        "params": TWOTEXTURE_PARAM_TABLE,
        "mpcScalars": REQUIRED_MPC_SCALARS,
    }
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset) == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    mat, asset = _fresh(name, ism=True, nanite=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", False)

    _build_two_texture(mat, collection, lut_texture)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, TWOTEXTURE_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint)
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


# ============================================================================================
# M_V2_Eyes
# ============================================================================================


class EyesParams:
    class Textures:
        BaseTexture = "BaseTexture"
        Iris = "Iris"
        Glint = "Glint"
        SurfaceClassLUT = "SurfaceClassLUT"

    class Scalars:
        Alpha = "Alpha"
        SurfaceClassIndex = "SurfaceClassIndex"
        IrisFrame = "IrisFrame"

    class Vectors:
        Color = "Color"

    class Switches:
        VampireEyes = "VampireEyes"
        UseGlint = "UseGlint"


EYES_PARAM_TABLE = {
    "textures": sorted(vars(EyesParams.Textures)[k] for k in vars(EyesParams.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(EyesParams.Scalars)[k] for k in vars(EyesParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(EyesParams.Vectors)[k] for k in vars(EyesParams.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(EyesParams.Switches)[k] for k in vars(EyesParams.Switches) if not k.startswith("_")),
}


def _build_eyes(mat, collection, lut_texture):
    """`eyes.psh` (design doc "M_V2_Eyes" post-lighting math):

        tex t0 / tex t1 / tex t2
        lrp r0, t1.a, t1, t0      ; ps.1.x lrp d,t,a,b -> LinearInterpolate(A=b, B=a, Alpha=t):
                                   ; BaseColor = Lerp(A=BaseTexture(sclera, t0), B=Iris(t1),
                                   ;             Alpha=Iris.a) -- the task's own transcription
        mad r0.rgb, r0, v0, t2    ; v0 (vertex lighting) dropped -- Lumen's; t2 (Glint) is additive
                                   ; Emissive, gated by UseGlint
        mov r0.a, t0.a            ; Opacity(Mask) = BaseTexture.a

    `IrisFrame` (design doc: "the iris slot the character lane writes at runtime when it swaps eye
    colour; 0 means the $iris the VMT bound") has no shipped-source formula -- SF-6's runtime lane
    writes it on the MID, and this generator has nothing in the corpus to wire it to. Declared, not
    wired, exactly like `M_V2_Unlit`'s `UseFixedCube`/`MetallicTint` or `M_V2_TwoTexture`'s
    `UseBumpOnBaseTexture2`.

    `VampireEyes` (review fix -- `psh/eyes_vampire`, compiled-only, disassembled by the review):

        mul r0, t0, v0            ; BaseColor(lit) = BaseTexture(sclera, t0) x vertex lighting v0
        lrp r0, t1.w, t1, r0      ; ps.1.x lrp d,t,a,b -> LinearInterpolate(A=b, B=a, Alpha=t):
                                   ; result = Lerp(A=r0 (lit sclera), B=Iris(t1), Alpha=Iris.a)
        add r0.xyz, r0, t2        ; + Glint (t2), additive
        mov r0.w, t0.w            ; Opacity(Mask) = BaseTexture.a

    Read against the non-vampire `eyes.psh` above, the only real difference is *where* lighting
    applies: the non-vampire program lights the whole lerped result (`BaseColor = Lerp(sclera,
    iris, iris.a)`, lit uniformly by Lumen once it leaves this graph); the vampire program lights
    the sclera *before* the lerp and the iris never receives `v0` at all -- the iris is
    self-illuminated (unlit), the sclera alone is lit. There is no post-lighting `mul` by `v0` a
    node graph can reproduce (Lumen lights whatever lands in `MP_BASE_COLOR` uniformly), so the
    equivalent split under this pipeline routes the iris term to Emissive (bypassing lighting
    entirely, the node-graph meaning of "unlit") and leaves only the sclera, darkened by the iris
    coverage it lost, in BaseColor: `BaseColor = (1 - Iris.a) x BaseTexture`, `Emissive += Iris.rgb
    x Iris.a`. `docs/vtmb/facial_animation.md:503` documents the vampire eye program pair by name.

    `Color` (`$color`'s VtMB `c3.rgb`) is multiplied into BaseColor for the same reason every other
    master multiplies it in, even though `eyes.psh`'s own register legend has no `c3` term at all
    ("no constants; everything is a texture stage") -- the parameter is one of the four exposed on
    every master, its default is an exact no-op, and no shipped eyes material authors `$color`, so
    wiring it costs nothing and keeps the knob live if one ever does.
    """
    g = Graph(mat, collection=collection)
    P = EyesParams

    base_tex = g.tex(P.Textures.BaseTexture, -1100, -400, kind="color")
    iris_tex = g.tex(P.Textures.Iris, -1100, -160, kind="color")
    glint_tex = g.tex(P.Textures.Glint, -1100, 100, kind="color",
                      default="/Engine/EngineResources/Black.Black")

    base_rgb = g.mask(base_tex, "rgb", -900, -400)
    base_a = g.mask(base_tex, "a", -900, -320, src_out="RGBA")
    iris_rgb = g.mask(iris_tex, "rgb", -900, -160)
    iris_a = g.mask(iris_tex, "a", -900, -80, src_out="RGBA")

    blended = g.lerp(base_rgb, "", iris_rgb, "", iris_a, "", -700, -280)

    color = g.vec3(P.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -1100, -560)
    tinted = g.mul(blended, "", color, "", -500, -320)

    # `VampireEyes`: the sclera alone (darkened by the iris coverage it lost, `1 - Iris.a`) in
    # BaseColor -- lit, like every other master's BaseColor -- with the iris term moved to
    # Emissive below (self-illuminated, matching the disassembly). See the function docstring.
    sclera_coverage = g.one_minus(iris_a, "", -700, -160)
    sclera_only = g.mul(base_rgb, "", sclera_coverage, "", -500, -200)
    base_color_vampire = g.mul(sclera_only, "", color, "", -300, -240)
    base_color_final = g.switch(P.Switches.VampireEyes, base_color_vampire, tinted, -100, -280,
                                default=False)
    g.to(base_color_final, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # -- surface class lookup: Eyes has no $envmap lane at all (not in the design's exposed-
    # parameter table for this master), so Roughness/Specular/Metallic are always the class row --
    class_roughness, class_specular, class_metallic = _class_lut_influenced(
        g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)
    g.to(class_roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to(class_specular, "", unreal.MaterialProperty.MP_SPECULAR)
    g.to(class_metallic, "", unreal.MaterialProperty.MP_METALLIC)

    # -- Emissive: Glint is additive, gated by UseGlint. Set by no shipped material (406/406 off);
    # the slot exists for completeness (design doc "M_V2_Eyes") ---------------------------------
    glint_rgb = g.mask(glint_tex, "rgb", -900, 100)
    black3 = g.const3(0.0, 0.0, 0.0, -900, 180)
    glint_emissive = g.switch(P.Switches.UseGlint, glint_rgb, black3, -700, 140, default=False)

    # `VampireEyes`: the iris term the switch above moved out of BaseColor, self-illuminated
    # (`Iris.rgb x Iris.a`, additive alongside Glint) -- see the function docstring.
    iris_emissive_raw = g.mul(iris_rgb, "", iris_a, "", -700, 260)
    iris_emissive = g.switch(P.Switches.VampireEyes, iris_emissive_raw, black3, -500, 300,
                             default=False)
    total_emissive = g.add(glint_emissive, "", iris_emissive, "", -300, 200)
    g.to(total_emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # `IrisFrame` is declared per the exposed-parameter table but has nothing in the corpus to
    # wire (see the function docstring) -- declared, not wired.
    g.scalar(P.Scalars.IrisFrame, 0.0, -1100, 300)

    # -- Opacity / OpacityMask: eyes.psh's `mov r0.a, t0.a` -- BaseTexture.a, times the shared
    # Alpha knob (the four-parameter shared table: "Alpha ... feeds MP_OPACITY") ----------------
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    opacity = g.mul(alpha_param, "", base_a, "", 1900, 0)
    g.to(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    g.to(opacity, "", unreal.MaterialProperty.MP_OPACITY_MASK)


def make_eyes():
    name = "M_V2_Eyes"
    asset = "%s/%s" % (PKG, name)
    collection = _load_surfaces_collection()
    lut_texture = _load_class_lut()
    recipe = {
        "graphVersion": GRAPH_VERSION,
        "sourceHash": _source_hash(),
        "citedUnits": _cited_unit_hashes(EYES_CITED_SHADER_UNITS),
        "params": EYES_PARAM_TABLE,
        "mpcScalars": REQUIRED_MPC_SCALARS,
    }
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset) == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    # Usage flags: skeletal mesh + morph targets only (design doc "Master inventory": eyes are
    # always a character part, and morph targets drive the eye blend shapes) -- no ISM/Nanite/
    # Niagara flags, unlike the world-and-character masters above.
    mat, asset = _fresh(name, skeletal=True, morph=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", False)

    _build_eyes(mat, collection, lut_texture)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, EYES_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint)
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


# ============================================================================================
# M_V2_Water
# ============================================================================================


class WaterParams:
    class Textures:
        BaseTexture = "BaseTexture"
        DuDvMap = "DuDvMap"
        NormalMap = "NormalMap"
        EnvMap = "EnvMap"
        NormalMapFrames = "NormalMapFrames"
        SurfaceClassLUT = "SurfaceClassLUT"

    class Scalars:
        Alpha = "Alpha"
        SurfaceClassIndex = "SurfaceClassIndex"
        RefractAmount = "RefractAmount"
        ReflectAmount = "ReflectAmount"
        BaseReflectFract = "BaseReflectFract"
        WaterDepth = "WaterDepth"
        WaterMurkiness = "WaterMurkiness"
        WaterBaseFactor = "WaterBaseFactor"
        WaterBaseMovementDist = "WaterBaseMovementDist"
        WaterBaseMovementFreq = "WaterBaseMovementFreq"
        WaterSpecularMin = "WaterSpecularMin"
        WaterSpecularMax = "WaterSpecularMax"
        WaterTimeFreq1 = "WaterTimeFreq1"
        WaterTimeFreq2 = "WaterTimeFreq2"
        WaterWaveHeight = "WaterWaveHeight"
        WaterWaveLength = "WaterWaveLength"
        CheapWaterStartDistance = "CheapWaterStartDistance"
        CheapWaterEndDistance = "CheapWaterEndDistance"
        FogStart = "FogStart"
        FogEnd = "FogEnd"
        BumpScrollRateU = "BumpScrollRateU"
        BumpScrollRateV = "BumpScrollRateV"
        NormalFrameRate = "NormalFrameRate"
        NormalFrameCount = "NormalFrameCount"

    class Vectors:
        Color = "Color"
        WaterColor = "WaterColor"
        RefractTint = "RefractTint"
        ReflectTint = "ReflectTint"
        FogColor = "FogColor"
        EnvMapTint = "EnvMapTint"
        TexScaleOffset = "TexScaleOffset"

    class Switches:
        CheapWater = "CheapWater"
        UseFogEnable = "UseFogEnable"
        UseEnvMap = "UseEnvMap"
        UseFixedCube = "UseFixedCube"
        UseBaseTexture = "UseBaseTexture"
        UseNormalMap = "UseNormalMap"
        UseAnimatedNormalFrames = "UseAnimatedNormalFrames"


WATER_PARAM_TABLE = {
    "textures": sorted(vars(WaterParams.Textures)[k] for k in vars(WaterParams.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(WaterParams.Scalars)[k] for k in vars(WaterParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(WaterParams.Vectors)[k] for k in vars(WaterParams.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(WaterParams.Switches)[k] for k in vars(WaterParams.Switches) if not k.startswith("_")),
}


def _build_water(mat, collection, lut_texture, default_normal_frames):
    """`waterrefract.psh`/`waterreflect.psh` (design doc "M_V2_Water" post-lighting math) are both
    render-target passes over `_rt_WaterRefraction`/`_rt_WaterReflection`, and the design states
    both are **replaced**, not transcribed: Unreal `Refraction` (`RM_PIXEL_NORMAL_OFFSET`, the same
    technique `make_world_materials.py::make_refract` uses for its legacy `M_Refract`) and Lumen
    reflection on one translucent surface, with the authored constants carried as scalars driving
    the same shapes. What *is* transcribed literally is the one place VtMB has a real Fresnel term:
    `waterreflect.psh`'s quintic `mad r0.a, r0.a, 1-c3.a, c3.a` is Schlick with R0 = `c3.a`, exactly
    Unreal's `Fresnel` node with `BaseReflectFraction` = `BaseReflectFract` and `ExponentIn` = 5.

    Reconstruction decisions, named as such (this master has no shipped pixel source for either
    final program -- `WaterRefract_old`/`WaterReflect_old` and their `_ps20_old` twins are all
    compiled-only; only the design-era `waterrefract.psh`/`waterreflect.psh` pair is readable):

    - Two independent bump slots feed one `MP_NORMAL` (Pixel Normal Offset refraction has no
      second normal channel to offset against): `NormalMap` is the lit bump (reflections/specular,
      gated `UseNormalMap`, matching every other master's normal lane); `DuDvMap` contributes only
      its own *deviation from flat* (`DuDvMap.rgb - (0,0,1)`), scaled by `RefractAmount/100` and
      added on top. An unbound `DuDvMap` samples its default (`DefaultNormal`, already flat), so
      that deviation -- and therefore the whole perturbation -- is an exact `(0,0,0)` no-op
      regardless of `RefractAmount`, the same "default makes the knob inert" shape every other
      lane in this file uses.
    - `MP_REFRACTION = 1 + RefractAmount/100`, the same "1.0 is neutral, `RefractAmount` is added
      to one" convention `M_V2_Refract`'s own refraction pin uses (review fix: this master's
      `MP_REFRACTION` was a flat `1.0` -- `RefractAmount` drove only the `DuDvMap` perturbation
      below, never the refraction pin itself, so the knob never actually bent light).
      `RefractAmount` still drives the `DuDvMap` perturbation too, so refraction strength and the
      DuDv ripple share one coherent knob. `CheapWater` "drops the refraction pass" for both: it
      zeroes the perturbation outright (a `Switch` ahead of the `Add`) *and* forces `MP_REFRACTION`
      back to the flat neutral `1.0`, so a cheap-water instance is genuinely undistorted, not just
      unrippled.
    - `BaseReflectFract` feeds the Fresnel node; its output scales `ReflectAmount/100` into
      `MP_SPECULAR` (gated `UseEnvMap`, mirroring every other master's envmap-gated specular
      branch) rather than a literal reflection-image blend, since Lumen already supplies the
      reflection image once Specular/Roughness are physically plausible. `ReflectTint`'s luma
      scales that same specular term, the same "grey tint scales Specular" shape `M_V2_Lit`'s own
      reflection contract uses for a non-chromatic `$envmaptint`. `BaseReflectFract`'s own default
      is `0.0` and the Fresnel node's static `exponent` is `5.0` -- the shipped binary's own
      values, not the design-era readable source's: `waterreflect_old`/`waterreflect_ps20_old`
      read no `c3` register at all (register legend review: "`c3.a` is in the unshipped
      `waterreflect.psh` source only"), which is R0 = 0 at Schlick's ps.1.1 exponent 5 (ps.2.0/
      cheap use exponent 4, not reproduced here -- one exponent, matching the ps.1.1 default pass).
    - `WaterColor`/`WaterMurkiness`/`RefractTint`/`Color` combine into BaseColor: `BaseTexture ×
      Color × RefractTint` (the un-murky look, `RefractTint` transcribing `waterrefract.psh`'s
      `mul r0, t2, c1`) `Lerp`'d toward the flat `WaterColor` by `WaterMurkiness` -- an exact no-op
      at the shipped default (`WaterMurkiness` 0.0), same shape as every other neutral-by-default
      lane in this file.
    - `UseFogEnable` -> `Emissive += FogColor.rgb * saturate((PixelDepth - FogStart) / (FogEnd -
      FogStart))`, `Opacity` blended toward `FogColor.a` by the same distance term when enabled --
      the transcription of the shipped cheap program's tail (`watercheap_ps11`/
      `watercheap_ps20_old`: `mad r0.xyz, F, reflect, c0(g_FogColor)` / `mov r0.w, c0.w`, review
      fix). The wave-animation scalars (`WaterBaseFactor`, `WaterBaseMovementDist/Freq`,
      `WaterTimeFreq1/2`, `WaterWaveHeight/Length`, `WaterSpecularMin/Max`,
      `CheapWaterStartDistance/EndDistance`, `WaterDepth`) remain **declared, not wired**: they are
      vertex/World-Position-Offset concerns, out of this generator's scope, the same way the sprite
      lane owns `$spriteorigin`.
    """
    g = Graph(mat, collection=collection)
    P = WaterParams

    # -- UV: TexScaleOffset transform (shared by every UV-consuming slot), then one Panner on the
    # bump lane only -- Water has no BaseScrollRateU/V (not in the exposed-parameter table) -----
    uv0 = g.node(unreal.MaterialExpressionTextureCoordinate, -1900, 0)
    tex_scale_offset = g.node(unreal.MaterialExpressionVectorParameter, -1900, 200)
    tex_scale_offset.set_editor_property("parameter_name", P.Vectors.TexScaleOffset)
    tex_scale_offset.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 0.0, 0.0))
    scale = g.mask(tex_scale_offset, "rg", -1700, 160)
    offset = g.mask(tex_scale_offset, "ba", -1700, 260, src_out="RGBA")
    scaled_uv = g.mul(uv0, "", scale, "", -1500, 0)
    base_uv = g.add(scaled_uv, "", offset, "", -1300, 40)

    time_node = g.time(-1900, 420)
    bump_u = g.scalar(P.Scalars.BumpScrollRateU, 0.0, -1900, 500)
    bump_v = g.scalar(P.Scalars.BumpScrollRateV, 0.0, -1900, 560)
    bump_speed = g.append(bump_u, "", bump_v, "", -1700, 530)
    bump_uv = g.panner(time_node, bump_speed, -1500, 530)
    connect(base_uv, "", bump_uv, "Coordinate")

    # -- BaseTexture -----------------------------------------------------------------------------
    base_tex = g.tex(P.Textures.BaseTexture, -1100, -400, kind="color")
    connect(base_uv, "", base_tex, "UVs")
    base_tex_rgb = g.mask(base_tex, "rgb", -900, -400)
    base_tex_a = g.mask(base_tex, "a", -900, -320, src_out="RGBA")
    white3 = g.const3(1.0, 1.0, 1.0, -900, -240)
    # `default=False` (review fix): the shipped water corpus is mostly untextured (18/24 units),
    # so a grey-checker default on the missing-parameter path is the wrong failure mode here --
    # unlike Lit/Unlit, where most units do bind BaseTexture.
    base_selected = g.switch(P.Switches.UseBaseTexture, base_tex_rgb, white3, -700, -360,
                             default=False)
    base_a_selected = g.switch(P.Switches.UseBaseTexture, base_tex_a, g.const(1.0, -900, -160),
                               -700, -200, default=False)

    color = g.vec3(P.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -1100, -560)
    refract_tint = g.vec3(P.Vectors.RefractTint, (1.0, 1.0, 1.0, 1.0), -1100, -680)
    tinted = g.mul(g.mul(base_selected, "", color, "", -500, -420), "", refract_tint, "",
                   -300, -460)

    water_color = g.vec3(P.Vectors.WaterColor, (0.0, 0.0, 0.0, 0.0), -1100, -800)
    murkiness = g.scalar(P.Scalars.WaterMurkiness, 0.0, -1100, -880)
    base_color_final = g.lerp(tinted, "", water_color, "", murkiness, "", -100, -600)
    g.to(base_color_final, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # -- Normal / refraction: NormalMap (lit bump) plus DuDvMap's own deviation from flat
    # (unconditional ripple perturbation, its DefaultNormal default already flat) -- see the
    # function docstring --------------------------------------------------------------------
    dudv_tex = g.tex(P.Textures.DuDvMap, -1100, 60, kind="normal")
    connect(bump_uv, "", dudv_tex, "UVs")
    dudv_rgb = g.mask(dudv_tex, "rgb", -900, 60)

    normal_tex_2d = g.tex(P.Textures.NormalMap, -1100, 260, kind="normal")
    connect(bump_uv, "", normal_tex_2d, "UVs")
    normal_tex = _flipbook_sample(
        g, normal_tex_2d, P.Textures.NormalMapFrames, bump_uv,
        P.Scalars.NormalFrameRate, P.Scalars.NormalFrameCount,
        P.Switches.UseAnimatedNormalFrames, default_normal_frames, -1100, 340, sampler="normal")
    flat_normal = g.const3(0.0, 0.0, 1.0, -700, 140)
    normal_lit = g.switch(P.Switches.UseNormalMap, g.mask(normal_tex, "rgb", -700, 260),
                          flat_normal, -500, 220, default=False)

    # `RefractAmount` scales how far DuDvMap's own deviation from flat perturbs the shared normal
    # -- an unbound DuDvMap (flat DefaultNormal) makes this delta exactly (0,0,0) regardless of
    # RefractAmount, and CheapWater zeroes it outright ("drops the refraction pass"). Pixel Normal
    # Offset reads its offset direction from this same MP_NORMAL, so the perturbation is what
    # actually drives the refraction distortion; MP_REFRACTION itself stays the flat M_Refract-style
    # neutral 1.0 (see the function docstring) ---------------------------------------------------
    refract_amount = g.scalar(P.Scalars.RefractAmount, 20.0, -1100, 620)
    refract_scale = g.div(refract_amount, "", g.const(100.0, -900, 700), "", -700, 660)
    dudv_delta = g.sub(dudv_rgb, "", flat_normal, "", -700, 580)
    dudv_perturbation = g.mul(dudv_delta, "", refract_scale, "", -500, 620)
    dudv_perturbation_gated = g.switch(P.Switches.CheapWater, g.const3(0.0, 0.0, 0.0, -300, 700),
                                       dudv_perturbation, -300, 640, default=False)
    combined_normal = g.add(normal_lit, "", dudv_perturbation_gated, "", -100, 400)
    g.to(combined_normal, "", unreal.MaterialProperty.MP_NORMAL)

    # `MP_REFRACTION = 1 + RefractAmount/100` (review fix -- see the function docstring), zeroed
    # back to the flat neutral 1.0 under CheapWater, the same "drops the refraction pass" gate the
    # DuDv perturbation above already uses.
    refraction_from_amount = g.add(g.const(1.0, -300, 780), "", refract_scale, "", -100, 780)
    refraction_final = g.switch(P.Switches.CheapWater, g.const(1.0, -300, 860),
                                refraction_from_amount, 100, 820, default=False)
    g.to(refraction_final, "", unreal.MaterialProperty.MP_REFRACTION)

    # -- surface class lookup + reflection: BaseReflectFract -> Fresnel -> ReflectAmount/
    # ReflectTint into Specular, gated UseEnvMap (mirrors every other master's envmap branch) ---
    class_roughness, class_specular, class_metallic = _class_lut_influenced(
        g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)

    # Default 0.0 (review fix): the shipped `waterreflect_old`/`_ps20_old` binaries read no `c3`
    # register at all -- R0 = 0 in the shipped game, not the 0.2 the design-era readable source
    # implied. See the function docstring's Fresnel bullet.
    base_reflect_fract = g.scalar(P.Scalars.BaseReflectFract, 0.0, -1900, 1900)
    fresnel_node = g.node(unreal.MaterialExpressionFresnel, -1700, 1900)
    # `MaterialExpressionFresnel`'s static exponent property is `exponent` (real-editor fact --
    # `Exponent`, C++ `UMaterialExpressionFresnel.h`); `ExponentIn`/`BaseReflectFractionIn` are the
    # two *connectable* `FExpressionInput` pins, named for their C++ field verbatim (Unreal's
    # default `GetInputName` reflects the field name -- neither is overridden on this node).
    fresnel_node.set_editor_property("exponent", 5.0)
    connect(base_reflect_fract, "", fresnel_node, "BaseReflectFractionIn")

    reflect_amount = g.scalar(P.Scalars.ReflectAmount, 50.0, -1900, 2000)
    reflect_tint = g.vec3(P.Vectors.ReflectTint, (1.0, 1.0, 1.0, 1.0), -1900, 2080)
    luma_weights = g.const3(0.299, 0.587, 0.114, -1900, 2160)
    reflect_tint_luma = g.dot(reflect_tint, "", luma_weights, "", -1700, 2080)
    specular_water = g.mul(
        g.mul(fresnel_node, "", g.div(reflect_amount, "", g.const(100.0, -1500, 2000), "",
                                      -1500, 2040), "", -1300, 2000),
        "", reflect_tint_luma, "", -1100, 2000)
    specular_final = g.switch(P.Switches.UseEnvMap, specular_water, class_specular, -900, 1960,
                              default=False)
    g.to(specular_final, "", unreal.MaterialProperty.MP_SPECULAR)
    g.to(class_roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to(class_metallic, "", unreal.MaterialProperty.MP_METALLIC)

    # -- Emissive: the authored fixed cube add, identical shape to M_V2_Lit's (design doc "The
    # authored fixed cube ... beats the chromatic branch" -- Water has no chromatic branch at all,
    # so there is nothing to beat, just the literal cube add gated UseFixedCube) ----------------
    env_tint = g.vec3(P.Vectors.EnvMapTint, (1.0, 1.0, 1.0, 1.0), -1900, 2240)
    fixed_cube_strength = g.mpc("FixedCubeStrength", -1900, 2320)
    reflect_dir = g.reflection_ws(-1900, 2400)
    envcube = g.cube(P.Textures.EnvMap, -1700, 2400, default=DEFAULT_CUBE)
    connect(reflect_dir, "", envcube, "UVs")
    # `cube x EnvMapTint x FixedCubeStrength` (review fix: FixedCubeStrength was not read at all on
    # this master) -- Water has no separate reflection-mask texture to fold in (unlike M_V2_Lit's
    # mask_sat), so this is the reflection contract's cube/tint/strength triple without a mask
    # term; ReflectTint keeps scaling the authored cube on top, unchanged from before this fix.
    fixed_raw = g.mul(
        g.mul(g.mul(envcube, "RGB", env_tint, "", -1500, 2400), "", fixed_cube_strength, "",
             -1400, 2420),
        "", reflect_tint, "", -1300, 2440)
    lumen_safe_fixed = g.node(unreal.MaterialExpressionRayTracingQualitySwitch, -1100, 2400)
    connect(fixed_raw, "", lumen_safe_fixed, "Normal")
    connect(g.const3(0.0, 0.0, 0.0, -1100, 2480), "", lumen_safe_fixed, "RayTraced")
    fixed_emissive = g.switch(P.Switches.UseFixedCube, lumen_safe_fixed,
                              g.const3(0.0, 0.0, 0.0, -900, 2400), -900, 2360, default=False)

    # -- Fog: the shipped cheap program's own tail (`watercheap_ps11`/`watercheap_ps20_old`:
    # `mad r0.xyz, F, reflect, c0(g_FogColor)` / `mov r0.w, c0.w`, review fix) -- `F` is the
    # distance term `saturate((PixelDepth - FogStart) / (FogEnd - FogStart))`, additive on
    # Emissive and blending Opacity toward FogColor.a, both gated UseFogEnable ------------------
    fog_color = g.vec4(P.Vectors.FogColor, (0.0, 0.0, 0.0, 0.0), -1900, 4120)
    fog_start = g.scalar(P.Scalars.FogStart, 1.0, -1900, 3960)
    fog_end = g.scalar(P.Scalars.FogEnd, 400.0, -1900, 4040)
    pixel_depth = g.node(unreal.MaterialExpressionPixelDepth, -1900, 4200)
    fog_span = g.sub(fog_end, "", fog_start, "", -1700, 4000)
    fog_numerator = g.sub(pixel_depth, "", fog_start, "", -1700, 4200)
    fog_ratio = g.div(fog_numerator, "", fog_span, "", -1500, 4100)
    fog_factor = g.sat(fog_ratio, "", -1300, 4100)
    fog_color_rgb = g.mask(fog_color, "rgb", -1700, 4280, src_out="RGBA")
    fog_color_a = g.mask(fog_color, "a", -1700, 4360, src_out="RGBA")
    fog_emissive_raw = g.mul(fog_color_rgb, "", fog_factor, "", -1100, 4200)
    fog_emissive = g.switch(P.Switches.UseFogEnable, fog_emissive_raw,
                            g.const3(0.0, 0.0, 0.0, -900, 4280), -900, 4240, default=False)
    total_emissive = g.add(fixed_emissive, "", fog_emissive, "", -700, 2400)
    g.to(total_emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # -- Opacity: Alpha x BaseTexture.a (translucent, no vertex-color/alpha lane on this master),
    # blended toward FogColor.a by the same distance term when UseFogEnable is set -------------
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    opacity_base = g.mul(alpha_param, "", base_a_selected, "", 1900, 0)
    opacity_fogged = g.lerp(opacity_base, "", fog_color_a, "", fog_factor, "", 2100, 40)
    opacity_final = g.switch(P.Switches.UseFogEnable, opacity_fogged, opacity_base, 2300, 20,
                             default=False)
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY)

    # -- Declared, not wired -- vertex/World-Position-Offset concerns, out of this generator's
    # scope; see the function docstring's fog bullet ------------------------------------------
    g.scalar(P.Scalars.WaterDepth, 64.0, -1900, 3000)
    g.scalar(P.Scalars.WaterBaseFactor, 0.0, -1900, 3080)
    g.scalar(P.Scalars.WaterBaseMovementDist, 0.0, -1900, 3160)
    g.scalar(P.Scalars.WaterBaseMovementFreq, 0.0, -1900, 3240)
    g.scalar(P.Scalars.WaterSpecularMin, 0.0, -1900, 3320)
    g.scalar(P.Scalars.WaterSpecularMax, 1.0, -1900, 3400)
    g.scalar(P.Scalars.WaterTimeFreq1, 0.0, -1900, 3480)
    g.scalar(P.Scalars.WaterTimeFreq2, 0.0, -1900, 3560)
    g.scalar(P.Scalars.WaterWaveHeight, 0.0, -1900, 3640)
    g.scalar(P.Scalars.WaterWaveLength, 0.0, -1900, 3720)
    g.scalar(P.Scalars.CheapWaterStartDistance, 0.0, -1900, 3800)
    g.scalar(P.Scalars.CheapWaterEndDistance, 0.0, -1900, 3880)


def make_water():
    name = "M_V2_Water"
    asset = "%s/%s" % (PKG, name)
    collection = _load_surfaces_collection()
    lut_texture = _load_class_lut()
    recipe = {
        "graphVersion": GRAPH_VERSION,
        "sourceHash": _source_hash(),
        "citedUnits": _cited_unit_hashes(WATER_CITED_SHADER_UNITS),
        "params": WATER_PARAM_TABLE,
        "mpcScalars": REQUIRED_MPC_SCALARS,
    }
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset) == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    # Water has no BaseTextureFrames slot at all -- only NormalMapFrames -- so its only default
    # flipbook array is the normal one (H3 review fix: sampled `sampler="normal"`, so it needs the
    # flat-normal-packed default, not the all-white `T_V2_DefaultFrames`).
    default_normal_frames = _make_default_normal_frames_array(force=force)

    # BLEND_Modulate/Translucent surfaces are not Nanite-compatible -- this master
    # deliberately does not set used_with_nanite (review fix, matches M_V2_Decal's own
    # note below). used_with_instanced_static_meshes stays on (the placement lane may use
    # ISM for water planes).
    mat, asset = _fresh(name, ism=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property(
        "translucency_lighting_mode",
        unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mat.set_editor_property("refraction_method", unreal.RefractionMode.RM_PIXEL_NORMAL_OFFSET)
    mat.set_editor_property("two_sided", False)

    _build_water(mat, collection, lut_texture, default_normal_frames)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, WATER_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint)
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


# ============================================================================================
# M_V2_Sprite
# ============================================================================================


class SpriteParams:
    class Textures:
        BaseTexture = "BaseTexture"
        BaseTextureFrames = "BaseTextureFrames"
        SurfaceClassLUT = "SurfaceClassLUT"

    class Scalars:
        Alpha = "Alpha"
        SurfaceClassIndex = "SurfaceClassIndex"
        FrameRate = "FrameRate"
        FrameCount = "FrameCount"

    class Vectors:
        Color = "Color"

    class Switches:
        UseVertexColor = "UseVertexColor"
        # `UseVertexAlpha` is the SF-4.3-part-3 orchestrator ruling: it absorbs the rerouted
        # `$ignorez` unit that authors `$vertexalpha` (design doc "Master inventory" -> the 5
        # `unlitgeneric` `$ignorez` units re-routed here; one, `engine/vertexcolorblend`, also
        # authors `$vertexalpha`) so that unit stages cleanly instead of failing "not exposed".
        UseVertexAlpha = "UseVertexAlpha"
        UseAnimatedFrames = "UseAnimatedFrames"


SPRITE_PARAM_TABLE = {
    "textures": sorted(vars(SpriteParams.Textures)[k] for k in vars(SpriteParams.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(SpriteParams.Scalars)[k] for k in vars(SpriteParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(SpriteParams.Vectors)[k] for k in vars(SpriteParams.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(SpriteParams.Switches)[k] for k in vars(SpriteParams.Switches) if not k.startswith("_")),
}


def _set_disable_depth_test(mat):
    """The property name has moved across engine versions (`make_gizmo_material.py`'s own
    precedent); guard so the asset still builds if it is absent, rather than failing the whole
    content build over a rename. `M_V2_Sprite` sets this unconditionally, on the master itself --
    the design doc's own "Master inventory" states `bDisableDepthTest` (from `$ignorez`) is
    material-only, never a per-instance override, unlike `BlendMode`/`TwoSided`/the opacity clip."""
    for prop in ("disable_depth_test", "b_disable_depth_test"):
        try:
            mat.set_editor_property(prop, True)
            return
        except Exception:
            continue
    unreal.log_warning(
        "[make_v2_materials] M_V2_Sprite: no disable_depth_test property found on this engine "
        "build -- shipped Sprite depth-test-off behaviour is not reproduced")


def _build_sprite(mat, collection, lut_texture, default_frames):
    """The `SpriteRender*` programs are all compiled-only (design doc "M_V2_Sprite" post-lighting
    math): "Base Color -> Emissive, Color x vertex colour, blend from the row" -- `$spriterendermode`
    picks the *blend state* (a per-instance override, design doc's blend table), not a material
    parameter, so nothing about it lives in this graph. There is no `UseBaseTexture` switch on this
    master (unlike Lit/Unlit/Water/Refract) -- every one of the design's 67 units binds a base
    texture, so the slot is always sampled, unconditionally."""
    g = Graph(mat, collection=collection)
    P = SpriteParams

    uv0 = g.node(unreal.MaterialExpressionTextureCoordinate, -1100, -600)
    base_tex_2d = g.tex(P.Textures.BaseTexture, -1100, -400, kind="color")
    connect(uv0, "", base_tex_2d, "UVs")
    base_tex = _flipbook_sample(
        g, base_tex_2d, P.Textures.BaseTextureFrames, uv0,
        P.Scalars.FrameRate, P.Scalars.FrameCount, P.Switches.UseAnimatedFrames, default_frames,
        -1100, -600, sampler="linear")
    base_rgb = g.mask(base_tex, "rgb", -900, -420)
    base_a = g.mask(base_tex, "a", -900, -340)

    color = g.vec3(P.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -1100, -560)
    tinted = g.mul(base_rgb, "", color, "", -700, -400)
    vertex_color = g.vertex_color(-1100, -680)
    vc_rgb = g.mask(vertex_color, "rgb", -900, -680)
    with_vc = g.mul(tinted, "", vc_rgb, "", -500, -440)
    vc_selected = g.switch(P.Switches.UseVertexColor, with_vc, tinted, -300, -400, default=False)
    g.to(vc_selected, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.to(vc_selected, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # -- class LUT declared for contract completeness ("every master exposes SurfaceClassLUT");
    # Unlit shading model ignores MP_ROUGHNESS/SPECULAR/METALLIC, not wired to anything ---------
    _class_lut(g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)

    # -- Opacity: Alpha x BaseTexture.a, x VertexColor.a under UseVertexAlpha -------------------
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    alpha_with_basetex = g.mul(alpha_param, "", base_a, "", 1900, 0)
    # `VertexColor`'s outputs are all unnamed FNames -- connect straight to its own "A" output
    # (already 1-wide) rather than through a ComponentMask; see M_V2_Lit's Opacity section.
    alpha_with_vc = g.mul(alpha_with_basetex, "", vertex_color, "A", 2100, 40)
    opacity_final = g.switch(P.Switches.UseVertexAlpha, alpha_with_vc, alpha_with_basetex,
                             2300, 20, default=False)
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY)
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY_MASK)


def make_sprite():
    name = "M_V2_Sprite"
    asset = "%s/%s" % (PKG, name)
    collection = _load_surfaces_collection()
    lut_texture = _load_class_lut()
    recipe = {
        "graphVersion": GRAPH_VERSION,
        "sourceHash": _source_hash(),
        "citedUnits": {},  # SpriteRender* ships compiled-only; no readable shader-source unit
        "params": SPRITE_PARAM_TABLE,
        "mpcScalars": REQUIRED_MPC_SCALARS,
    }
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset) == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    default_frames = _make_default_frames_array(force=force)

    # used_with_instanced_static_meshes (review fix): the placement lane may use ISM for
    # sprite-shaped world geometry, not only Niagara particles.
    mat, asset = _fresh(name, ism=True, niagara_sprites=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    _set_disable_depth_test(mat)

    _build_sprite(mat, collection, lut_texture, default_frames)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, SPRITE_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint)
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


# ============================================================================================
# M_V2_SpriteZ / M_V2_SpriteZLit -- the particle floor's depth-tested twins (R7.3)
# ============================================================================================
#
# `docs/architecture/effects-architecture.md` section 5.4: VtMB draws every particle through
# `$spriterendermode 8` -- `BlendFunc(ONE, ONE_MINUS_SRC_ALPHA)`, depth test ON, depth write off --
# and `M_V2_Sprite` is depth-test-off on the master itself (`bDisableDepthTest` is material-only,
# never a per-instance override), so the floor needs a twin with the same graph and the depth test
# left on. `M_V2_SpriteZLit` is the same twin under `TLM_VolumetricPerVertexNonDirectional` for the
# six `lighting` leaves. Both carry the `ElysiumFog` parameters (`FogColor` / `FogStart` /
# `FogInvRange`, `mat_fog.fog_from_params` -- a Niagara sprite has no Custom Primitive Data) with the
# inscatter scaled by the pixel's opacity, so an additive card (`mask 0`) fogs to black the way
# Source's sprite shader does. Neither is in `importers/materials.py`'s routing: no VMT unit lands
# on them; only the four `MI_Particle*` children below do.


class SpriteZParams:
    Textures = SpriteParams.Textures
    Vectors = type("Vectors", (), {"Color": "Color", "FogColor": mat_fog.P_COLOR})
    Scalars = type("Scalars", (), {
        "Alpha": "Alpha", "SurfaceClassIndex": "SurfaceClassIndex", "FrameRate": "FrameRate",
        "FrameCount": "FrameCount", "FogStart": mat_fog.P_START, "FogInvRange": mat_fog.P_INV_RANGE,
    })
    Switches = SpriteParams.Switches


SPRITE_Z_PARAM_TABLE = {
    "textures": SPRITE_PARAM_TABLE["textures"],
    "scalars": sorted(vars(SpriteZParams.Scalars)[k] for k in vars(SpriteZParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(SpriteZParams.Vectors)[k] for k in vars(SpriteZParams.Vectors) if not k.startswith("_")),
    "switches": SPRITE_PARAM_TABLE["switches"],
}


def _build_sprite_z(mat, collection, lut_texture, default_frames):
    """`_build_sprite`'s graph plus the fog term: `Emissive = shaded x (1 - f) + FogColor x f x
    Opacity`, `BaseColor = shaded x (1 - f)`; the opacity is untouched."""
    g = Graph(mat, collection=collection)
    P = SpriteParams

    uv0 = g.node(unreal.MaterialExpressionTextureCoordinate, -1100, -600)
    base_tex_2d = g.tex(P.Textures.BaseTexture, -1100, -400, kind="color")
    connect(uv0, "", base_tex_2d, "UVs")
    base_tex = _flipbook_sample(
        g, base_tex_2d, P.Textures.BaseTextureFrames, uv0,
        P.Scalars.FrameRate, P.Scalars.FrameCount, P.Switches.UseAnimatedFrames, default_frames,
        -1100, -600, sampler="linear")
    base_rgb = g.mask(base_tex, "rgb", -900, -420)
    base_a = g.mask(base_tex, "a", -900, -340)

    color = g.vec3(P.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -1100, -560)
    tinted = g.mul(base_rgb, "", color, "", -700, -400)
    vertex_color = g.vertex_color(-1100, -680)
    vc_rgb = g.mask(vertex_color, "rgb", -900, -680)
    with_vc = g.mul(tinted, "", vc_rgb, "", -500, -440)
    vc_selected = g.switch(P.Switches.UseVertexColor, with_vc, tinted, -300, -400, default=False)

    _class_lut(g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)

    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    alpha_with_basetex = g.mul(alpha_param, "", base_a, "", 1900, 0)
    alpha_with_vc = g.mul(alpha_with_basetex, "", vertex_color, "A", 2100, 40)
    opacity_final = g.switch(P.Switches.UseVertexAlpha, alpha_with_vc, alpha_with_basetex,
                             2300, 20, default=False)
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY)
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    # -- the fog term, by parameter (no primitive data on a Niagara sprite), inscatter x opacity --
    fog_f, fog_inv, fog_color = mat_fog.fog_from_params(mat, x=-1600, y=1600)
    faded = mat_fog.fade(mat, vc_selected, "", fog_inv, -200, 1600)
    haze = g.mul(g.mul(fog_color, "", fog_f, "", -200, 1800), "", opacity_final, "", 0, 1800)
    emissive = g.add(faded, "", haze, "", 200, 1700)
    g.to(faded, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.to(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def _make_sprite_z(name, *, lit):
    asset = "%s/%s" % (PKG, name)
    collection = _load_surfaces_collection()
    lut_texture = _load_class_lut()
    recipe = {
        "graphVersion": GRAPH_VERSION,
        "sourceHash": _source_hash(),
        "citedUnits": {},
        "params": SPRITE_Z_PARAM_TABLE,
        "mpcScalars": REQUIRED_MPC_SCALARS,
        "lit": lit,
    }
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset) == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    default_frames = _make_default_frames_array(force=force)
    mat, asset = _fresh(name, ism=True, niagara_sprites=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    if lit:
        # Default Lit is the material's own default shading model (as `M_V2_Lit` leaves it).
        mat.set_editor_property(
            "translucency_lighting_mode",
            unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_PER_VERTEX_NON_DIRECTIONAL)
    else:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # The depth test stays ON: that is the whole difference from `M_V2_Sprite`.

    _build_sprite_z(mat, collection, lut_texture, default_frames)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, SPRITE_Z_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint)
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


def make_sprite_z():
    return _make_sprite_z("M_V2_SpriteZ", lit=False)


def make_sprite_z_lit():
    return _make_sprite_z("M_V2_SpriteZLit", lit=True)


#: The four particle material children (`effects-architecture.md` section 5.4), authored below the
#: material lane's package root beside the corpus instances (`importers/materials.py` names them
#: under `keep`, so its prune leaves them). `(name, master, blend, switches on)`; the sprite
#: children turn the two vertex switches on -- the tint and the mask ride the quad's vertex colour
#: -- and take `BLEND_AlphaComposite`, VtMB's mode 8 (`src + dst x (1 - src.a)`); the refract child
#: keeps `M_V2_Refract`'s translucent blend, its `DuDvMap` and `RefractAmount` are bound per slot
#: at runtime (`User.Leaf<ii>.Normal`, the `Refract` ramp).
PARTICLE_MATERIAL_PACKAGE = "%s/Materials/particles" % mounts.BAKED
PARTICLE_CHILDREN = (
    ("MI_Particle", "M_V2_SpriteZ", "BLEND_ALPHA_COMPOSITE", ("UseVertexColor", "UseVertexAlpha")),
    ("MI_ParticleLit", "M_V2_SpriteZLit", "BLEND_ALPHA_COMPOSITE",
     ("UseVertexColor", "UseVertexAlpha")),
    ("MI_ParticleNoZ", "M_V2_Sprite", "BLEND_ALPHA_COMPOSITE", ("UseVertexColor", "UseVertexAlpha")),
    ("MI_ParticleRefract", "M_V2_Refract", "BLEND_TRANSLUCENT", ("UseBaseTexture",)),
)


def make_particle_children():
    """The four `MI_Particle*` children, one tracked instance each, recipe-stamped on the master's
    own recipe, the blend and the switches (the `MI_V2_Missing` pattern)."""
    made = []
    for name, master_name, blend, switches_on in PARTICLE_CHILDREN:
        asset = "%s/%s" % (PARTICLE_MATERIAL_PACKAGE, name)
        master_asset = "%s/%s" % (PKG, master_name)
        master = unreal.load_asset(master_asset)
        if not master:
            _fail("%s not found -- the masters must run before make_particle_children()"
                  % master_asset)
        # The master's switch set is its own param table (the graph was built from it); every
        # switch is stated explicitly on the instance, as the material lane's own instances do.
        table = REFRACT_PARAM_TABLE if master_name == "M_V2_Refract" else SPRITE_PARAM_TABLE
        switch_names = list(table["switches"])
        missing = [name_ for name_ in switches_on if name_ not in switch_names]
        if missing:
            _fail("%s exposes no %s switch for %s" % (master_asset, "/".join(missing), name))
        switches = {name_: (name_ in switches_on) for name_ in switch_names}
        recipe = {
            "sourceHash": _source_hash(),
            "master": master_asset,
            "masterRecipe": bl.stored_recipe(master_asset),
            "switches": switches,
            "blendMode": blend,
            "twoSided": True,
        }
        fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
        force = _flag(_cmdline_arg("PolicyForce", ""))
        if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
                and bl.stored_recipe(asset) == fingerprint:
            unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
            made.append(unreal.load_asset(asset))
            continue
        mic = bl.make_material_instance(name, PARTICLE_MATERIAL_PACKAGE, master)
        if not mic:
            _fail("could not author %s" % asset)
        for switch, value in sorted(switches.items()):
            mel.set_material_instance_static_switch_parameter_value(
                mic, switch, value, update_material_instance=False)
        bpo = mic.get_editor_property("base_property_overrides")
        bpo.set_editor_property("override_blend_mode", True)
        bpo.set_editor_property("blend_mode", getattr(unreal.BlendMode, blend))
        bpo.set_editor_property("override_two_sided", True)
        bpo.set_editor_property("two_sided", True)
        mic.set_editor_property("base_property_overrides", bpo)
        mel.update_material_instance(mic)
        bl.stamp_recipe(mic, fingerprint)
        if not bl.save(asset):
            _fail("save failed: %s" % asset)
        unreal.log("[make_v2_materials] saved %s" % asset)
        made.append(mic)
    return made


# ============================================================================================
# M_V2_Refract
# ============================================================================================


class RefractParams:
    class Textures:
        BaseTexture = "BaseTexture"
        DuDvMap = "DuDvMap"
        NormalMap = "NormalMap"
        EnvMap = "EnvMap"
        SurfaceClassLUT = "SurfaceClassLUT"

    class Scalars:
        Alpha = "Alpha"
        SurfaceClassIndex = "SurfaceClassIndex"
        RefractAmount = "RefractAmount"
        FogStart = mat_fog.P_START
        FogInvRange = mat_fog.P_INV_RANGE
        FogInscatter = mat_fog.P_INSCATTER

    class Vectors:
        Color = "Color"
        RefractTint = "RefractTint"
        EnvMapTint = "EnvMapTint"
        FogColor = mat_fog.P_COLOR

    class Switches:
        UseBaseTexture = "UseBaseTexture"
        UseNormalMap = "UseNormalMap"
        UseEnvMap = "UseEnvMap"
        UseFixedCube = "UseFixedCube"


REFRACT_PARAM_TABLE = {
    "textures": sorted(vars(RefractParams.Textures)[k] for k in vars(RefractParams.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(RefractParams.Scalars)[k] for k in vars(RefractParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(RefractParams.Vectors)[k] for k in vars(RefractParams.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(RefractParams.Switches)[k] for k in vars(RefractParams.Switches) if not k.startswith("_")),
}


def _build_refract(mat, collection, lut_texture):
    """No shipped source and no transcribed selector for this family (design doc "M_V2_Refract"):
    "the master is Unreal `Refraction` from `$dudvmap`/`$normalmap` scaled by `RefractAmount`,
    tinted by `RefractTint`. Stated as a reconstruction, not a transcription." This reuses the
    legacy `M_Refract`'s (`make_world_materials.py::make_refract`) own Pixel Normal Offset
    technique and its "1.0 is neutral, `$refractamount` is added to one" convention verbatim,
    rather than `M_V2_Water`'s different (normal-perturbation) wiring -- `M_Refract` is the closer
    precedent here since this master has no `CheapWater`-style bypass switch to route around.

    `DuDvMap` and `NormalMap` both feed the one shared `MP_NORMAL` -- `NormalMap` is the lit bump
    (gated `UseNormalMap`, matching every other master's normal lane) and `DuDvMap` contributes an
    additive ripple, sampled through the same UV, scaled by `NormalMap.a x RefractAmount` (review
    fix: `fxc/refract_ps20`'s own `scale = normalMap.a x RefractAmount`, not wired at all before
    this fix). DuDvMap's own default (`DefaultNormal`, flat) makes its delta from flat an exact
    `(0,0,0)` no-op regardless of the scale for an unbound slot, the same way `M_V2_Water`'s does.

    `UseEnvMap` is declared per the exposed-parameter table but not separately wired: this master's
    per-family table states no reflection-mask/specular formula at all (unlike Lit/Water), so
    `UseEnvMap`'s only real effect is Lumen's own implicit reflection off the class-LUT Specular/
    Roughness -- nothing here to connect it to. `UseFixedCube` gates the literal authored-cube
    emissive add, identical in shape to `M_V2_Lit`'s and `M_V2_Water`'s.

    `ForceRefract` is dropped (design doc: `$forcerefract` has zero corpus authors and no proxy) --
    there is accordingly no such parameter in this master's table at all, not even declared.
    """
    g = Graph(mat, collection=collection)
    P = RefractParams

    uv0 = g.node(unreal.MaterialExpressionTextureCoordinate, -1100, -600)

    base_tex = g.tex(P.Textures.BaseTexture, -1100, -400, kind="color")
    connect(uv0, "", base_tex, "UVs")
    base_rgb = g.mask(base_tex, "rgb", -900, -400)
    base_a = g.mask(base_tex, "a", -900, -320, src_out="RGBA")
    white3 = g.const3(1.0, 1.0, 1.0, -900, -240)
    # `default=False` (review fix, matching M_V2_Water): the shipped Refract/heatglow corpus is
    # mostly untextured (both heatglow units and most refract units) -- a grey-checker default on
    # the missing-parameter path is the wrong failure mode here.
    base_selected = g.switch(P.Switches.UseBaseTexture, base_rgb, white3, -700, -360, default=False)
    base_a_selected = g.switch(P.Switches.UseBaseTexture, base_a, g.const(1.0, -900, -160),
                               -700, -200, default=False)

    color = g.vec3(P.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -1100, -560)
    refract_tint = g.vec3(P.Vectors.RefractTint, (1.0, 1.0, 1.0, 1.0), -1100, -680)
    base_color_final = g.mul(g.mul(base_selected, "", color, "", -500, -420), "", refract_tint, "",
                             -300, -460)

    # -- Normal: NormalMap (lit bump, gated UseNormalMap) plus DuDvMap's ripple, scaled by
    # `NormalMap.a x RefractAmount` -- `fxc/refract_ps20`'s own `scale = normalMap.a x
    # RefractAmount` (review fix: this master's DuDv delta was previously unconditional and
    # unscaled, wired to neither NormalMap.a nor RefractAmount at all) ---------------------------
    refract_amount = g.scalar(P.Scalars.RefractAmount, 20.0, -1100, 620)
    dudv_tex = g.tex(P.Textures.DuDvMap, -1100, 60, kind="normal")
    connect(uv0, "", dudv_tex, "UVs")
    dudv_rgb = g.mask(dudv_tex, "rgb", -900, 60)
    normal_tex = g.tex(P.Textures.NormalMap, -1100, 260, kind="normal")
    connect(uv0, "", normal_tex, "UVs")
    normal_tex_a = g.mask(normal_tex, "a", -900, 340, src_out="RGBA")
    flat_normal = g.const3(0.0, 0.0, 1.0, -700, 140)
    normal_lit = g.switch(P.Switches.UseNormalMap, g.mask(normal_tex, "rgb", -700, 260),
                          flat_normal, -500, 220, default=False)
    dudv_delta = g.sub(dudv_rgb, "", flat_normal, "", -700, 400)
    dudv_scale = g.mul(normal_tex_a, "", refract_amount, "", -700, 480)
    dudv_perturbation = g.mul(dudv_delta, "", dudv_scale, "", -500, 440)
    combined_normal = g.add(normal_lit, "", dudv_perturbation, "", -300, 300)
    g.to(combined_normal, "", unreal.MaterialProperty.MP_NORMAL)

    # -- Refraction: `M_Refract`'s own "1.0 is neutral" convention -- `RefractAmount` is added to
    # one rather than interpreted as glass IOR --------------------------------------------------
    neutral = g.const(1.0, -1100, 700)
    refraction_magnitude = g.add(neutral, "", g.div(refract_amount, "", g.const(100.0, -900, 780),
                                                     "", -900, 700), "", -700, 700)
    g.to(refraction_magnitude, "", unreal.MaterialProperty.MP_REFRACTION)

    # -- surface class lookup: no reflection-mask formula for this master, so Roughness/Specular/
    # Metallic are always the class row (like M_V2_Eyes/M_V2_TwoTexture) -----------------------
    class_roughness, class_specular, class_metallic = _class_lut_influenced(
        g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)
    g.to(class_roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to(class_metallic, "", unreal.MaterialProperty.MP_METALLIC)

    # `UseEnvMap` is declared but not wired -- see the function docstring -----------------------
    g.switch(P.Switches.UseEnvMap, g.const(1.0, -900, 2000), g.const(0.0, -900, 2080),
            -700, 2040, default=False)

    # -- Emissive: the authored fixed cube add, identical shape to M_V2_Lit's/M_V2_Water's -------
    env_tint = g.vec3(P.Vectors.EnvMapTint, (1.0, 1.0, 1.0, 1.0), -1900, 2240)
    fixed_cube_strength = g.mpc("FixedCubeStrength", -1900, 2320)
    reflect_dir = g.reflection_ws(-1900, 2400)
    envcube = g.cube(P.Textures.EnvMap, -1700, 2400, default=DEFAULT_CUBE)
    connect(reflect_dir, "", envcube, "UVs")
    fixed_raw = g.mul(g.mul(envcube, "RGB", env_tint, "", -1500, 2400), "", fixed_cube_strength, "",
                      -1300, 2440)
    lumen_safe_fixed = g.node(unreal.MaterialExpressionRayTracingQualitySwitch, -1100, 2400)
    connect(fixed_raw, "", lumen_safe_fixed, "Normal")
    connect(g.const3(0.0, 0.0, 0.0, -1100, 2480), "", lumen_safe_fixed, "RayTraced")
    fixed_emissive = g.switch(P.Switches.UseFixedCube, lumen_safe_fixed,
                              g.const3(0.0, 0.0, 0.0, -900, 2400), -900, 2360, default=False)

    # -- Scene fog (R5.4), last, over the three outputs it fades ---------------------------------
    base_color_fogged, emissive_fogged, specular_fogged = _scene_fog(
        g, base_color_final, "", fixed_emissive, "", class_specular, "", -1600, 3000)
    g.to(base_color_fogged, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.to(emissive_fogged, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.to(specular_fogged, "", unreal.MaterialProperty.MP_SPECULAR)

    # -- Opacity: Alpha x BaseTexture.a (translucent, no vertex-color/alpha lane on this master) -
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    opacity = g.mul(alpha_param, "", base_a_selected, "", 1900, 0)
    g.to(opacity, "", unreal.MaterialProperty.MP_OPACITY)


def make_refract():
    name = "M_V2_Refract"
    asset = "%s/%s" % (PKG, name)
    collection = _load_surfaces_collection()
    lut_texture = _load_class_lut()
    recipe = {
        "graphVersion": GRAPH_VERSION,
        "sourceHash": _source_hash(),
        "citedUnits": {},  # no shipped source and no transcribed selector for this family
        "params": REFRACT_PARAM_TABLE,
        "mpcScalars": REQUIRED_MPC_SCALARS,
    }
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset) == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    # BLEND_Translucent is not Nanite-compatible -- this master deliberately does not set
    # used_with_nanite (review fix). used_with_instanced_static_meshes stays on.
    mat, asset = _fresh(name, ism=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property(
        "translucency_lighting_mode",
        unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mat.set_editor_property("refraction_method", unreal.RefractionMode.RM_PIXEL_NORMAL_OFFSET)
    mat.set_editor_property("two_sided", False)

    _build_refract(mat, collection, lut_texture)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, REFRACT_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint)
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


# ============================================================================================
# M_V2_Decal
# ============================================================================================


class DecalParams:
    class Textures:
        BaseTexture = "BaseTexture"
        SurfaceClassLUT = "SurfaceClassLUT"

    class Scalars:
        Alpha = "Alpha"
        SurfaceClassIndex = "SurfaceClassIndex"
        FogStart = mat_fog.P_START
        FogInvRange = mat_fog.P_INV_RANGE

    class Vectors:
        Color = "Color"
        FogColor = mat_fog.P_COLOR

    class Switches:
        UseVertexColor = "UseVertexColor"


DECAL_PARAM_TABLE = {
    "textures": sorted(vars(DecalParams.Textures)[k] for k in vars(DecalParams.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(DecalParams.Scalars)[k] for k in vars(DecalParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(DecalParams.Vectors)[k] for k in vars(DecalParams.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(DecalParams.Switches)[k] for k in vars(DecalParams.Switches) if not k.startswith("_")),
}


def _build_decal(mat, collection, lut_texture):
    """`decalmodulate` ships no program at all -- the string is absent from `stdshader_dx8.dll`
    and the 38 materials fell back to `wireframe` in retail (design doc "M_V2_Decal", "The eight
    real unresolved families"). Imported as `BLEND_Modulate` of `BaseTexture` over the receiver --
    a **named deliberate divergence** from retail, which drew them as wireframe.

    `DecalDepthOffset` is not on this master -- it is a knob only, in `MPC_ElysiumSurfaces`
    (design doc "Four parameters that left the masters": `MP_PIXEL_DEPTH_OFFSET` is not reachable
    from `unreal.MaterialProperty` in this build), applied by the decal component the placement
    lane spawns, not by this graph.

    A `BLEND_Modulate` Unlit surface has no `MP_BASE_COLOR`/`MP_OPACITY` distinction the renderer
    reads separately -- the final `MP_EMISSIVE_COLOR` output *is* what gets multiplied onto the
    receiver, exactly like every other Unlit master in this file wires both property sinks to the
    same value. There is no Opacity pin to wire at all for this blend mode.

    Carries the world's own distance fog as three named instance parameters
    (`mat_fog.fog_from_params`) rather than Custom Primitive Data -- a `UDecalComponent` is a
    `USceneComponent`, not a `UPrimitiveComponent`, so it carries none. This is R5.3's chosen home
    for the fog axis (`seam_map_material.md` -> "Decal fog and wetness homes (R5.3)"): the three
    params default neutral (unfogged), and the placement lane sets them per decal instance from
    the map's own `UElysiumMapEnvironment` (R4.4) fog, never from a per-map material package.
    """
    g = Graph(mat, collection=collection)
    P = DecalParams

    uv0 = g.node(unreal.MaterialExpressionTextureCoordinate, -1100, -600)
    base_tex = g.tex(P.Textures.BaseTexture, -1100, -400, kind="color")
    connect(uv0, "", base_tex, "UVs")
    base_rgb = g.mask(base_tex, "rgb", -900, -400)

    color = g.vec3(P.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -1100, -560)
    tinted = g.mul(base_rgb, "", color, "", -700, -440)
    vertex_color = g.vertex_color(-1100, -680)
    vc_rgb = g.mask(vertex_color, "rgb", -900, -680)
    with_vc = g.mul(tinted, "", vc_rgb, "", -500, -480)
    vc_selected = g.switch(P.Switches.UseVertexColor, with_vc, tinted, -300, -440, default=False)

    # `Alpha` is declared (the shared four-parameter table) but a modulate-blend Unlit surface has
    # no opacity pin to feed -- declared, not wired, like every other master's genuinely inert knob.
    g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)

    fog_f, fog_inv, fog_color = mat_fog.fog_from_params(g.mat, x=-1400, y=1300)
    faded = mat_fog.fade(g.mat, vc_selected, "", fog_inv, -100, 1120)
    fogged = mat_fog.inscatter(g.mat, faded, fog_f, fog_color, 80, 1120)

    g.to(fogged, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.to(fogged, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.to(mat_fog.specular(g.mat, fog_inv, -450, 1600), "", unreal.MaterialProperty.MP_SPECULAR)

    # -- class LUT declared for contract completeness; Unlit ignores Roughness/Specular/Metallic -
    _class_lut(g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)


def make_decal():
    name = "M_V2_Decal"
    asset = "%s/%s" % (PKG, name)
    collection = _load_surfaces_collection()
    lut_texture = _load_class_lut()
    recipe = {
        "graphVersion": GRAPH_VERSION,
        "sourceHash": _source_hash(),
        "citedUnits": {},  # decalmodulate ships no program at all
        "params": DECAL_PARAM_TABLE,
        "mpcScalars": REQUIRED_MPC_SCALARS,
    }
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset) == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    # Modulated surfaces are excluded from the Lumen surface cache (they contribute nothing to
    # global illumination and cannot themselves be seen in a Lumen reflection) and BLEND_Modulate
    # is not Nanite-compatible -- this master deliberately does NOT set used_with_nanite, unlike
    # every other world/ISM master in this file. A decal surface the map lane wants Nanite on is a
    # placement error, not a material one (design doc "M_V2_Decal").
    mat, asset = _fresh(name, ism=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MODULATE)
    mat.set_editor_property("two_sided", False)

    _build_decal(mat, collection, lut_texture)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, DECAL_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint)
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


# ============================================================================================
# MI_V2_Missing -- the one instance every proven-missing material slot binds
# ============================================================================================

#: The checker's tile count across the texture and the two colours of a square. VtMB substitutes
#: its own `___error` magenta/black checkerboard for exactly the slots this instance binds, so
#: this is that checkerboard's stand-in rather than a divergence from it.
MISSING_CHECKER_SIZE = 64
MISSING_CHECKER_SQUARE = 8
MISSING_CHECKER_COLOURS = ((255, 0, 255), (0, 0, 0))


def _make_missing_checker(force=False):
    """`T_V2_MissingChecker`: a generated magenta/black checkerboard, authored here beside the
    masters exactly as `T_LinearWhiteMask` and `T_V2_DefaultFrames` are."""
    rows = []
    for y in range(MISSING_CHECKER_SIZE):
        row = bytearray(b"\x00")  # PNG filter type 0
        for x in range(MISSING_CHECKER_SIZE):
            odd = ((x // MISSING_CHECKER_SQUARE) + (y // MISSING_CHECKER_SQUARE)) % 2
            row.extend(bytes(MISSING_CHECKER_COLOURS[odd]))
        rows.append(bytes(row))
    png = (b"\x89PNG\r\n\x1a\n"
           + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", MISSING_CHECKER_SIZE,
                                             MISSING_CHECKER_SIZE, 8, 2, 0, 0, 0))
           + _png_chunk(b"IDAT", zlib.compress(b"".join(rows)))
           + _png_chunk(b"IEND", b""))
    return _import_png("T_V2_MissingChecker", png, srgb=True,
                       compression=unreal.TextureCompressionSettings.TC_DEFAULT, force=force)


#: Every switch `M_V2_Unlit` exposes, stated explicitly on the instance: only the base texture is
#: on, so the checker is the whole surface and nothing else -- env map, cloud alpha, animated
#: frames -- can quietly change what a missing slot looks like.
MISSING_SWITCHES = {name: (name == UnlitParams.Switches.UseBaseTexture)
                    for name in UNLIT_PARAM_TABLE["switches"]}


def make_missing():
    """`MI_V2_Missing`: one tracked `M_V2_Unlit` instance wearing the checker.

    Every `vtmb:missing-material:` sentinel slot in the model corpus binds this one asset
    (`docs/architecture/seam_map_model.md` -> "Import" -> "Material binding"): 1,328 slots over 461
    referenced units. It is deliberately loud, and being `BLEND_Opaque` it never vetoes Nanite.
    """
    name = "MI_V2_Missing"
    asset = "%s/%s" % (PKG, name)
    master_asset = "%s/M_V2_Unlit" % PKG
    recipe = {
        "sourceHash": _source_hash(),
        "master": master_asset,
        "texture": "%s/T_V2_MissingChecker" % PKG,
        "checker": {"size": MISSING_CHECKER_SIZE, "square": MISSING_CHECKER_SQUARE,
                    "colours": MISSING_CHECKER_COLOURS},
        "switches": MISSING_SWITCHES,
        "blendMode": "Opaque",
        "twoSided": False,
    }
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset) == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    master = unreal.load_asset(master_asset)
    if not master:
        _fail("%s not found -- make_unlit() must run before make_missing()" % master_asset)
    texture = _make_missing_checker(force=force)

    mic = bl.make_material_instance(name, PKG, master)
    if not mic:
        _fail("could not author %s" % asset)
    bl.set_tex_param(mic, UnlitParams.Textures.BaseTexture, texture)
    for switch, value in sorted(MISSING_SWITCHES.items()):
        mel.set_material_instance_static_switch_parameter_value(
            mic, switch, value, update_material_instance=False)

    # Stated explicitly rather than inherited: this instance's whole job is to look the same
    # wherever it lands, so a later change to the master's own blend or sidedness must not move it.
    bpo = mic.get_editor_property("base_property_overrides")
    bpo.set_editor_property("override_blend_mode", True)
    bpo.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    bpo.set_editor_property("override_two_sided", True)
    bpo.set_editor_property("two_sided", False)
    mic.set_editor_property("base_property_overrides", bpo)
    mel.update_material_instance(mic)

    bl.stamp_recipe(mic, fingerprint)
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mic


def _cmdline_arg(key, default=""):
    needle = "-%s=" % key
    for token in unreal.SystemLibrary.get_command_line().split():
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def _flag(value):
    return str(value).strip().strip('"').lower() in ("1", "true", "yes", "on")



make_lit()
make_lit_translucent()
make_unlit()
make_two_texture()
make_eyes()
make_water()
make_sprite()
make_sprite_z()
make_sprite_z_lit()
make_refract()
make_decal()
make_missing()
make_particle_children()
