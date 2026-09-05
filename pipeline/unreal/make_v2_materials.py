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
GRAPH_VERSION = 12

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
    # R7.1: the water extinction scale (`_build_water`), on Water only.
    "WaterFogScale",
    # R7.5 look pass: the two DUDV warp scales (`_build_water`), on Water only -- screen offset
    # per authored `$refractamount` unit, normal tilt per authored `$reflectamount` unit.
    "WaterWarpScale", "WaterReflectWarpScale",
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


def _fresh(name, *, ism=False, nanite=False, skeletal=False, morph=False, clothing=False, niagara_sprites=False):
    """Delete + recreate the asset so a re-run authors a clean graph (idempotent). Usage flags are
    per family (design doc "Master inventory" + mechanics doc section 3b): without the matching
    flag UE compiles no permutation outside the editor and the affected primitives fall back to
    the default grey material in a packaged build. `M_V2_Decal` (not authored here) must NOT set
    Nanite -- R7.2 ruling 3: the projector instance is non-Nanite by construction, the wall
    underneath carries whatever Nanite split its own master already set -- which is why this is
    opt-in per call site rather than a blanket default.

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
    if clothing:
        mat.set_editor_property("used_with_clothing", True)
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


def _make_default_dudv_frames_array(force=False):
    """`T_V2_DefaultDuDvFrames` -- the `DuDvMapFrames` default (R7.5 G3).

    Its own asset rather than a second binding of `T_V2_DefaultNormalFrames`, because the two are
    sampled through DIFFERENT sampler types and Unreal refuses the mismatch at compile time. The
    DUDV lane samples `SAMPLERTYPE_LINEAR_COLOR`: `TA_water_dudv` is staged
    `TC_VECTOR_DISPLACEMENTMAP` (`water_audit/phase0/U1b_textures.md`), whose default sampler type
    IS linear colour, and the graph applies Source's own `value x 2 - 1` DUDV bias explicitly
    rather than letting `UnpackNormalMap` do it -- the normal lane's `SAMPLERTYPE_NORMAL` object
    would be a "Sampler type is Normal, should be Linear Color" error against that texture, and
    `T_V2_DefaultNormalFrames` is `TC_NORMALMAP`, which is the same error the other way round.

    The payload is `(128, 128, 255, 255)`, the same bytes as the flat-normal default: biased that
    is `(0.0039, 0.0039)`, the closest an 8-bit texel gets to a zero offset (exactly 0.5 is not
    representable), and it is multiplied by a warp strength of `(RefractAmount + ReflectAmount) /
    200` before it reaches the normal, so an instance that turns `UseAnimatedDuDvFrames` on without
    binding a real array perturbs nothing visible. Same two-slice DX10 DDS shape as
    `_make_default_frames_array` (the `arraySize = 1` collapse-to-`Texture2D` finding documented
    there applies identically) and the same existing-asset re-verify / `-PolicyForce` contract."""
    asset = "%s/T_V2_DefaultDuDvFrames" % PKG
    existing = unreal.load_asset(asset)
    if existing:
        if not force and existing.get_class().get_name() == "Texture2DArray":
            return existing
        if not force:
            unreal.log_warning(
                "[make_v2_materials] %s is a %s, not Texture2DArray -- rebuilding"
                % (asset, existing.get_class().get_name()))
        unreal.EditorAssetLibrary.delete_asset(asset)
    pixel = b"\x80\x80\xff\xff" * 2  # two identical zero-offset RGBA8 texels, one per slice
    dds = b"DDS " + _dds_header(1, 1, 4) + _dds_dx10_header(2) + pixel
    source = _policy_scratch_dir() / "v2_default_dudv_frames.dds"
    source.write_bytes(dds)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", PKG)
    task.set_editor_property("destination_name", "T_V2_DefaultDuDvFrames")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(asset)
    if not texture:
        unreal.log_warning(
            "[make_v2_materials] could not author T_V2_DefaultDuDvFrames; "
            "DuDvMapFrames ships with no default texture")
        return None
    if texture.get_class().get_name() != "Texture2DArray":
        unreal.log_warning(
            "[make_v2_materials] T_V2_DefaultDuDvFrames imported as %s, not Texture2DArray -- "
            "DuDvMapFrames ships with no default texture" % texture.get_class().get_name())
        return None
    texture.set_editor_property("srgb", False)
    texture.set_editor_property(
        "compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
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


def _uv_lanes(g, tex_scale_offset_name, *, base_scroll_names, bump_scroll_names=None,
              sine_wave=None, sine_translate_name=None):
    """`TexScaleOffset` transform (shared by every UV-consuming slot on the material), then one
    independent `Panner` per lane it is asked for. `(0, 0)` scroll rates are an exact `Panner`
    no-op, so no static switch gates them (design doc "The animation and scroll lanes"). Returns
    `(base_uv, bump_uv_or_None, tex_scale_offset_param, uv0)` -- `uv0` (M5 review fix) is the raw,
    untransformed `TextureCoordinate` node, for a caller (`M_V2_TwoTexture`'s `BaseTexture2` layer)
    whose own scale/offset vector must be independent of `TexScaleOffset`'s, not composed on top of
    the already-scaled/panned `base_uv`.

    `sine_wave` (R7.1 ruling J, Lit/LitTranslucent only) is the sine lane's own 0..1 wave node; with
    it the base coordinate becomes `transformed_uv + (SineUVTranslate.rg x wave + SineUVTranslate.ba)`
    -- a `sine` -> `texturetransform` chain's UV slide, on the BASE lane alone. The bump lane keeps
    the untranslated coordinate: Source's `$baseTextureTransform` moves the base texture and leaves
    `$bumpTransform` alone, and the pier's surf cards carry no normal map anyway. The default
    `(0,0,0,0)` makes the whole term an exact zero add on every other instance."""
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

    base_coord = transformed_uv
    if sine_wave is not None:
        translate = g.vec4(sine_translate_name, (0.0, 0.0, 0.0, 0.0), -1900, 300)
        # A `VectorParameter`'s default output is RGB only (see `mask`'s docstring), so the
        # offset half needs the explicit "RGBA" source output to reach the 4th channel.
        amplitude = g.mask(translate, "rg", -1700, 320)
        sine_offset = g.mask(translate, "ba", -1700, 380, src_out="RGBA")
        slide = g.add(g.mul(amplitude, "", sine_wave, "", -1500, 330), "", sine_offset, "",
                      -1300, 350)
        base_coord = g.add(transformed_uv, "", slide, "", -1100, 200)

    def _panner(names, coordinate, y):
        u = g.scalar(names[0], 0.0, -1900, y)
        v = g.scalar(names[1], 0.0, -1900, y + 60)
        speed = g.append(u, "", v, "", -1700, y + 30)
        p = g.panner(time_node, speed, -1500, y + 30)
        connect(coordinate, "", p, "Coordinate")
        return p

    base_uv = _panner(base_scroll_names, base_coord, 500)
    bump_uv = _panner(bump_scroll_names, transformed_uv, 620) if bump_scroll_names else None
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

    # `wave` is the bare 0..1 wave, before `SineMin`/`SineMax` scale it into a shading factor:
    # R7.1 ruling J's UV slide carries its own amplitude and offset (`SineUVTranslate`), so it
    # rides the wave itself rather than a value another target's min/max already stretched.
    return {"value": sine_value, "wave": wave_norm, "apply": apply}


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
    `M_V2_LitTranslucent`, `M_V2_TwoTexture`, `M_V2_Eyes`, `M_V2_Water`, `M_V2_Refract`); the two
    Unlit masters (`M_V2_Unlit`, `M_V2_Sprite`) still call the plain `_class_lut` (or, for
    `M_V2_Unlit`, declare it unwired) because an Unlit shading model ignores those pins entirely,
    per each function's own docstring. `M_V2_Decal` is DefaultLit but calls the plain `_class_lut`
    too (R7.2 ruling 1): Roughness/Specular/Metallic/Normal are deliberately left unconnected so
    the wall keeps its own surface under the decal, which is what a lightmapped `$decal` face
    did -- there is nothing for `ClassInfluence` to lerp into."""
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


#: R7.5 G6 (`water_audit/AUDIT.md` section 9 G6, owner decision 4): the Custom Primitive Data slot
#: the runtime's lightstyle clock writes into. Declared beside `ElysiumFog`'s own slots 0-5
#: (colour 0..3, start 4, 1/range 5) as `ElysiumLightStyle::SlotBrightness` on the C++ side; stated
#: here as the one literal this generator uses, never repeated at a call site.
LIGHT_STYLE_CPD_SLOT = 6


def _light_style_brightness(g, param_name, x, y):
    """The per-primitive lightstyle brightness (R7.5 G6), as one CPD-backed scalar.

    VtMB animates a face's lighting by swapping which lightmap page the face samples --
    `Mod_LoadFaces` flags a lightstyle-bearing face `surfflags |= 0x2000` and `CWorld::vfunc104`
    (`vampire.dll 0x1023c020`) registers the pattern string; on `sm_pier_1` all 34 `objects/surf`
    foam cards carry style 1 (`"mmnmmommommnonmmonqnmmo"`) and 21 also carry the switchable style
    32. Owner decision 4: **Lumen replaces lightmaps project-wide**, so there is no page to swap.
    The port keeps the authored *motion* -- the same pattern clock the runtime already runs
    (`UElysiumLightRig::StyleIntensity` / `SetLightStylePattern`) -- and applies it as a brightness
    multiplier on the lit result of the tagged component.

    A CPD-BACKED PARAMETER ALWAYS READS THE SLOT. `UMaterialExpressionScalarParameter::Compile`
    emits `Compiler->CustomPrimitiveData(index, MCT_Float)` when `bUseCustomPrimitiveData` is set
    (`UE_5.8 MaterialExpressions.cpp:8427`) and never falls back to `default_value` -- the default
    below is the editor preview and nothing else. An unwritten slot reads 0, and this is a
    MULTIPLIER, so 0 is black. Every producer of a primitive that binds these masters must write
    the neutral 1.0 into slot 6: the bake through `bake_map.set_fog`, the runtime through
    `ElysiumLightStyle::StampUnstyled` (`ElysiumFog.h`) at each component's construction. That is
    the opposite of the fog block's convention underneath it, where an unwritten 0 means "no fog"
    and needs no writer at all."""
    return g.scalar(param_name, 1.0, x, y, cpd=LIGHT_STYLE_CPD_SLOT)


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
        # R7.5 G5: `$envmapcontrast`, honoured on the fixed-cube path (named modernization).
        EnvMapContrast = "EnvMapContrast"
        # R7.5 G6: Custom Primitive Data slot 6, the lightstyle brightness.
        LightStyleBrightness = "LightStyleBrightness"

    class Vectors:
        Color = "Color"
        SelfIllumTint = "SelfIllumTint"
        EnvMapTint = "EnvMapTint"
        TexScaleOffset = "TexScaleOffset"
        SineTargetMask = "SineTargetMask"
        SineChannelMask = "SineChannelMask"
        SineUVTranslate = "SineUVTranslate"
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


class LitSkinnedParams(LitParams):
    class Scalars(LitParams.Scalars):
        ModelAlpha = "ModelAlpha"

    class Switches(LitParams.Switches):
        UseAlphaTest = "UseAlphaTest"


LIT_SKINNED_PARAM_TABLE = {key: list(values) for key, values in LIT_PARAM_TABLE.items()}
LIT_SKINNED_PARAM_TABLE["scalars"] = sorted([*LIT_SKINNED_PARAM_TABLE["scalars"], "ModelAlpha"])
LIT_SKINNED_PARAM_TABLE["switches"] = sorted([*LIT_SKINNED_PARAM_TABLE["switches"], "UseAlphaTest"])


def _build_lit(mat, collection, environment_collection, lut_texture, default_frames,
               default_normal_frames, *, translucent, skinned=False):
    """The shared M_V2_Lit / M_V2_LitTranslucent shading graph. `translucent` only changes how
    Opacity is wired (design doc: blend mode itself is a per-instance override, not a
    material-only property, so the two masters share every other pin)."""
    g = Graph(mat, collection=collection)
    P = LitParams

    # The sine lane comes first here (and only here): its wave is an input to the base UV lane
    # (R7.1 ruling J's `SineUVTranslate`), so it has to exist before `_uv_lanes` builds the panner.
    sine = _sine_lane(
        g, min_name=P.Scalars.SineMin, max_name=P.Scalars.SineMax,
        period_name=P.Scalars.SinePeriod, offset_name=P.Scalars.SineTimeOffset,
        target_mask_name=P.Vectors.SineTargetMask, channel_mask_name=P.Vectors.SineChannelMask)
    base_uv, bump_uv, _, _ = _uv_lanes(
        g, P.Vectors.TexScaleOffset,
        base_scroll_names=(P.Scalars.BaseScrollRateU, P.Scalars.BaseScrollRateV),
        bump_scroll_names=(P.Scalars.BumpScrollRateU, P.Scalars.BumpScrollRateV),
        sine_wave=sine["wave"], sine_translate_name=P.Vectors.SineUVTranslate)

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
    # R7.5 G5 -- NAMED MODERNIZATION. `$envmapcontrast` is authored on 19 corpus units (17x `1`,
    # `water/blackwater` `0.85`, one as a stray vector) and VtMB's own renderer never implemented
    # it: `docs/vtmb/reflections.md` ("`$envmapcontrast` and `$envmapsaturation` do not exist")
    # read every shipped `lightmappedgeneric*envmap*.psh` / `vertexlitgeneric*envmap*.psh` and
    # found no term for either. So this is authored intent the 2004 engine dropped on the floor,
    # restored with Source's own stated meaning -- 0 leaves the sample alone, 1 is `cube x cube`,
    # in between is the lerp -- rather than a VtMB behaviour reproduced. It applies only where a
    # cube is actually sampled, which is the authored-fixed-cube branch; an `env_cubemap` unit
    # hands its reflection to Lumen through Specular/Roughness and has no sample to contrast.
    # Default 0, so every instance that does not author the key is bit-identical to before.
    env_contrast = g.scalar(P.Scalars.EnvMapContrast, 0.0, -1700, 2120)
    envcube_rgb = g.mask(envcube, "rgb", -1600, 2200)
    envcube_contrasted = g.lerp(envcube_rgb, "", g.mul(envcube_rgb, "", envcube_rgb, "",
                                                       -1600, 2160), "",
                                env_contrast, "", -1540, 2200)
    fixed_cube_strength = g.mpc("FixedCubeStrength", -1900, 2300)
    fixed_raw = g.mul(g.mul(envcube_contrasted, "", mask_sat, "", -1500, 2240), "",
                      g.mul(env_tint, "", fixed_cube_strength, "", -1500, 2320), "", -1300, 2280)
    lumen_safe_fixed = g.node(unreal.MaterialExpressionRayTracingQualitySwitch, -1100, 2200)
    connect(fixed_raw, "", lumen_safe_fixed, "Normal")
    connect(black3, "", lumen_safe_fixed, "RayTraced")
    fixed_emissive = g.switch(P.Switches.UseFixedCube, lumen_safe_fixed, black3, -900, 2200,
                              default=False)

    total_emissive = g.add(selfillum_emissive, "", fixed_emissive, "", -300, 780)

    # -- Lightstyle (R7.5 G6, owner decision 4): the per-primitive brightness the style clock
    # writes into CPD slot 6, applied to the lit result BEFORE the fog -- VtMB modulates the
    # face's own lightmap page, not the haze in front of it, so the fog must not be scaled by it.
    light_style = _light_style_brightness(g, P.Scalars.LightStyleBrightness, -1900, 2900)
    base_color_styled = g.mul(base_color_final, "", light_style, "", -1700, 2900)
    emissive_styled = g.mul(total_emissive, "", light_style, "", -1700, 2980)

    # -- Scene fog (R5.4): the per-primitive CPD term, last, over the three outputs it fades -----
    base_color_fogged, emissive_fogged, specular_fogged = _scene_fog(
        g, base_color_styled, "", emissive_styled, "", specular_final, "", -1600, 3000)
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
    if skinned:
        model_alpha = g.scalar(LitSkinnedParams.Scalars.ModelAlpha, 1.0, 1700, -100)
        alpha_with_sine = g.mul(alpha_with_sine, "", model_alpha, "", 2000, -100)

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
    if skinned:
        authored_mask = g.switch(LitSkinnedParams.Switches.UseAlphaTest, base_tex_a, one_const,
                                 1900, 200, default=False)
        opacity_mask = g.mul(authored_mask, "", alpha_with_sine, "", 2100, 200)
    else:
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


def _make_lit_master(name, *, translucent, skinned=False):
    asset = "%s/%s" % (PKG, name)
    # Loaded -- and its required rows validated -- before the skip check, so a rerun that would
    # otherwise report "up to date, skipping" still fails loudly if a prerequisite asset (or one
    # of its rows) has since gone missing, rather than only checking it on the slow rebuild path.
    collection = _load_surfaces_collection()
    environment_collection = _load_environment_collection()
    lut_texture = _load_class_lut()
    recipe = _lit_recipe(LIT_CITED_SHADER_UNITS)
    params = LIT_SKINNED_PARAM_TABLE if skinned else LIT_PARAM_TABLE
    recipe.update(skinned=skinned, translucent=translucent, params=params)
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset, producer='v2-masters') == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    _make_linear_white_mask(force=force)
    default_frames = _make_default_frames_array(force=force)
    default_normal_frames = _make_default_normal_frames_array(force=force)

    mat, asset = _fresh(name, ism=not skinned, nanite=not skinned, skeletal=True, morph=True, clothing=skinned)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    if translucent:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property(
            "translucency_lighting_mode",
            unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    else:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED if skinned else unreal.BlendMode.BLEND_OPAQUE)
    if skinned:
        mat.set_editor_property("dither_opacity_mask", True)
        mat.set_editor_property("opacity_mask_clip_value", 0.333)
    mat.set_editor_property("two_sided", False)

    _build_lit(mat, collection, environment_collection, lut_texture, default_frames,
              default_normal_frames, translucent=translucent, skinned=skinned)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, params["switches"])

    bl.stamp_recipe(mat, fingerprint, producer='v2-masters')
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


def make_lit():
    return _make_lit_master("M_V2_Lit", translucent=False)


def make_lit_translucent():
    return _make_lit_master("M_V2_LitTranslucent", translucent=True)


def make_lit_skinned():
    return _make_lit_master("M_V2_LitSkinned", translucent=False, skinned=True)


def make_lit_skinned_translucent():
    return _make_lit_master("M_V2_LitSkinnedTranslucent", translucent=True, skinned=True)


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
        ModelAlpha = "ModelAlpha"
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
    model_alpha = g.scalar(P.Scalars.ModelAlpha, 1., 1700, -100)
    alpha_with_sine = g.mul(alpha_with_sine, "", model_alpha, "", 2000, -100)
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
            and bl.stored_recipe(asset, producer='v2-masters') == fingerprint:
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

    bl.stamp_recipe(mat, fingerprint, producer='v2-masters')
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
        LightStyleBrightness = "LightStyleBrightness"
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

    # -- Lightstyle (R7.5 G6, owner decision 4). Contract 3 named the Lit pair and Water, but the
    # bake splits a styled face into its own (material, style) chunk whatever family it binds:
    # `sm_pier_1`'s `blends/blend_pier*` and `sp_soc_3`'s `blends/seablend`,
    # `blends/searock_tunnel` and `blends/twrconwllab` are eight styled sections on THIS master,
    # and without the read they would sit dead still against the flickering plain sections they
    # abut. Applied to the base colour BEFORE the fog, exactly as on M_V2_Lit -- VtMB modulates the
    # face's own lightmap page, not the haze in front of it. This master has no emissive term of
    # its own (the fogged emissive below is inscatter alone), so there is nothing else to scale.
    light_style = _light_style_brightness(g, P.Scalars.LightStyleBrightness, -1900, 2820)
    base_color_styled = g.mul(overbright_base, "", light_style, "", -1700, 2820)

    # -- Scene fog (R5.4): this master has no emissive term of its own, so the fogged emissive is
    # the inscatter alone (`black * (1 - f) + fogColour * f`) ------------------------------------
    black3 = g.const3(0.0, 0.0, 0.0, -1700, 2900)
    base_color_fogged, emissive_fogged, specular_fogged = _scene_fog(
        g, base_color_styled, "", black3, "", class_specular, "", -1600, 3000)
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
            and bl.stored_recipe(asset, producer='v2-masters') == fingerprint:
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

    bl.stamp_recipe(mat, fingerprint, producer='v2-masters')
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
        Vampire = "Vampire"
        Flatten = "Flatten"
        ModelAlpha = "ModelAlpha"

    class Vectors:
        Color = "Color"
        IrisOrigin = "IrisOrigin"
        IrisU = "IrisU"
        IrisV = "IrisV"
        NormalOrigin = "NormalOrigin"
        EyeUpN = "EyeUpN"

    class Switches:
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

    `Vampire` scalar at 1 (`psh/eyes_vampire`, compiled-only, disassembled by the review):

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
    iris_tex.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS)
    glint_tex = g.tex(P.Textures.Glint, -1100, 100, kind="color",
                      default="/Engine/EngineResources/Black.Black")

    world = g.node(unreal.MaterialExpressionWorldPosition, -2500, -200)
    local = g.node(unreal.MaterialExpressionTransformPosition, -2300, -200)
    local.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(world, "", local, "")
    origin = g.vec3(P.Vectors.IrisOrigin, (0., 0., 0., 0.), -2500, 0)
    iris_u = g.vec3(P.Vectors.IrisU, (1., 0., 0., 0.), -2500, 200)
    iris_v = g.vec3(P.Vectors.IrisV, (0., 1., 0., 0.), -2500, 400)
    delta = g.sub(local, "", origin, "", -2100, 0)
    half = g.const(.5, -2100, 400)
    u = g.add(g.dot(delta, "", iris_u, "", -1900, 100), "", half, "", -1700, 100)
    v = g.add(g.dot(delta, "", iris_v, "", -1900, 300), "", half, "", -1700, 300)
    uv = g.node(unreal.MaterialExpressionAppendVector, -1500, 200)
    connect(u, "", uv, "A"); connect(v, "", uv, "B")
    connect(uv, "", iris_tex, "UVs")

    normal_origin = g.vec3(P.Vectors.NormalOrigin, (0., 0., 0., 0.), -2500, 650)
    up = g.vec3(P.Vectors.EyeUpN, (0., 0., 1., 0.), -2500, 850)
    flatten = g.scalar(P.Scalars.Flatten, .5, -2500, 1050)
    direction = g.sub(local, "", normal_origin, "", -2100, 650)
    amount = g.mul(g.dot(direction, "", up, "", -1900, 800), "", flatten, "", -1700, 800)
    projected_up = g.mul(amount, "", up, "", -1500, 800)
    flat = g.sub(direction, "", projected_up, "", -1300, 650)
    normal_local = g.node(unreal.MaterialExpressionNormalize, -1100, 650)
    connect(flat, "", normal_local, "")
    normal_world = g.node(unreal.MaterialExpressionTransform, -900, 650)
    normal_world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    normal_world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(normal_local, "", normal_world, "")
    g.to(normal_world, "", unreal.MaterialProperty.MP_NORMAL)
    vampire = g.scalar(P.Scalars.Vampire, 0., -900, 850)

    base_rgb = g.mask(base_tex, "rgb", -900, -400)
    base_a = g.mask(base_tex, "a", -900, -320, src_out="RGBA")
    iris_rgb = g.mask(iris_tex, "rgb", -900, -160)
    iris_a = g.mask(iris_tex, "a", -900, -80, src_out="RGBA")

    blended = g.lerp(base_rgb, "", iris_rgb, "", iris_a, "", -700, -280)

    color = g.vec3(P.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -1100, -560)
    tinted = g.mul(blended, "", color, "", -500, -320)

    # `Vampire` at 1: the sclera alone (darkened by the iris coverage it lost, `1 - Iris.a`) in
    # BaseColor -- lit, like every other master's BaseColor -- with the iris term moved to
    # Emissive below (self-illuminated, matching the disassembly). See the function docstring.
    sclera_coverage = g.one_minus(iris_a, "", -700, -160)
    sclera_only = g.mul(base_rgb, "", sclera_coverage, "", -500, -200)
    base_color_vampire = g.mul(sclera_only, "", color, "", -300, -240)
    base_color_final = g.lerp(tinted, "", base_color_vampire, "", vampire, "", -100, -280)
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

    # `Vampire`: the iris term the scalar blend above moved out of BaseColor, self-illuminated
    # (`Iris.rgb x Iris.a`, additive alongside Glint) -- see the function docstring.
    iris_emissive_raw = g.mul(iris_rgb, "", iris_a, "", -700, 260)
    iris_emissive = g.mul(iris_emissive_raw, "", vampire, "", -500, 300)
    total_emissive = g.add(glint_emissive, "", iris_emissive, "", -300, 200)
    g.to(total_emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # `IrisFrame` is declared per the exposed-parameter table but has nothing in the corpus to
    # wire (see the function docstring) -- declared, not wired.
    g.scalar(P.Scalars.IrisFrame, 0.0, -1100, 300)

    # -- Opacity / OpacityMask: eyes.psh's `mov r0.a, t0.a` -- BaseTexture.a, times the shared
    # Alpha knob (the four-parameter shared table: "Alpha ... feeds MP_OPACITY") ----------------
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    model_alpha = g.scalar(P.Scalars.ModelAlpha, 1.0, 1700, 160)
    opacity = g.mul(g.mul(alpha_param, "", model_alpha, "", 1800, 100), "", base_a, "", 1900, 0)
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
            and bl.stored_recipe(asset, producer='v2-masters') == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    # Usage flags: skeletal mesh + morph targets only (design doc "Master inventory": eyes are
    # always a character part, and morph targets drive the eye blend shapes) -- no ISM/Nanite/
    # Niagara flags, unlike the world-and-character masters above.
    mat, asset = _fresh(name, skeletal=True, morph=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("tangent_space_normal", False)
    mat.set_editor_property("dither_opacity_mask", True)
    mat.set_editor_property("opacity_mask_clip_value", .333)

    _build_eyes(mat, collection, lut_texture)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, EYES_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint, producer='v2-masters')
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


# ============================================================================================
# M_V2_Water
# ============================================================================================


class WaterParams:
    """The binding contract's Python side for `M_V2_Water`; `ElysiumSurfaceParamsWater` is the C++
    side and `importers.materials.EXPOSED_PARAMS["M_V2_Water"]` the stage's, all three pinned equal
    by `test_water_master_exposed_params_pinned_against_cpp_header`.

    **Every scalar here is either wired below or named in this table as declared-not-wired, with
    the VtMB fact behind it** (R7.5, verdict U2). Wired: `RefractAmount`/`ReflectAmount` (the DUDV
    warp strength, `/100` each, folded into one normal), `BaseReflectFract` (the cheap program's
    Fresnel base fraction), `WaterMurkiness`, the fog quadruple, the two scroll rates, the two
    flipbook lanes, `LightStyleBrightness`.

    Declared, not wired, and why -- the whole set traces to ONE measurement (`water_audit/phase0/
    U2_primitives.md`): **VtMB's shipped water vertex program never moves a vertex.** DX9 binds
    `Water_vs20_old` (`stdshader_dx8.dll 100138a0`), which writes `oPos = dp4(v0, cModelViewProj)`
    on the untouched input position; no water VS in the corpus contains a `sincos` or a time
    constant, and `BuildMSurfacePrimVerts` (`engine.dll FUN_20074f40`) copies every compiled
    primvert position verbatim. The wave block the VMTs author was read by a `Water_Old` vertex
    path that did not ship:

    * `WaterBaseFactor`, `WaterBaseMovementDist`, `WaterBaseMovementFreq`, `WaterTimeFreq1`,
      `WaterTimeFreq2`, `WaterWaveHeight`, `WaterWaveLength` -- the dead vertex-wave block. There
      is nothing to translate: reproducing a wave here would be inventing motion VtMB did not draw.
    * `WaterSpecularMin`/`WaterSpecularMax` -- the same block's specular ramp, read by the same
      absent program; the port's specular is `class_specular x luma(ReflectTint)`.
    * `WaterDepth` -- VBSP's per-instance depth hint. The map lane stages the volume's real depth
      (`water.volumes[].surfaceZCm`/`minZCm`), which is the number anything downstream must use.
    * `CheapWaterStartDistance`/`CheapWaterEndDistance` -- the distance at which the 2004 renderer
      swapped the expensive program for `WaterCheap_ps11`. A named modernization to drop: 5.8
      compiles one shading model per material, and Lumen already LODs its own reflection by
      distance, so the two-program split has no port. `CheapWater` (`$forcecheap`) still selects
      the cheap LOOK for a unit that authored it.
    * `UseEnvMap` -- `$envmap env_cubemap` means "the map's own probe", which in this port is
      Lumen's job and binds no texture. An AUTHORED fixed cube (`UseFixedCube`) is the branch that
      has a sample to read, and it is wired both into the emissive add and into the cheap lerp.
    * `DuDvMap` -- the plain `Texture2D` slot. Every DUDV in the corpus (`dev/water_dudv`) is a
      29-frame VTF that stages as a `Texture2DArray`, so the bound lane is `DuDvMapFrames`; the 2D
      slot is kept declared because the binding contract's key -> parameter map still names it.
    """

    class Textures:
        BaseTexture = "BaseTexture"
        DuDvMap = "DuDvMap"
        NormalMap = "NormalMap"
        EnvMap = "EnvMap"
        NormalMapFrames = "NormalMapFrames"
        DuDvMapFrames = "DuDvMapFrames"
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
        DuDvFrameRate = "DuDvFrameRate"
        DuDvFrameCount = "DuDvFrameCount"
        LightStyleBrightness = "LightStyleBrightness"

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
        UseAnimatedDuDvFrames = "UseAnimatedDuDvFrames"
        # R7.5 contract 1: the down-facing half of a water brush -- no reflection (specular 0,
        # roughness 1), volume coefficients as the surface's. Set only on the `MI_<unit>_Underside`
        # twin the material stage writes beside every water instance.
        Underside = "Underside"


WATER_PARAM_TABLE = {
    "textures": sorted(vars(WaterParams.Textures)[k] for k in vars(WaterParams.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(WaterParams.Scalars)[k] for k in vars(WaterParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(WaterParams.Vectors)[k] for k in vars(WaterParams.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(WaterParams.Switches)[k] for k in vars(WaterParams.Switches) if not k.startswith("_")),
}


def _build_water(mat, collection, lut_texture, default_normal_frames, default_dudv_frames):
    """Single Layer Water, rebuilt to `Water_Old`'s own program (R7.5 look pass,
    `E:/elysium-work/scratch/water_audit/LOOK_SPEC.md`; the transcription lives in
    `docs/vtmb/water.md` -> "The live program").

    The live class is `Water_Old_dx80_dx81_dx90` in `stdshader_dx8.dll`; its three draw
    functions (FUN_100138a0 refract, FUN_10013b30 reflect, FUN_10013d30 cheap) bind the SM2
    `_old` programs whose readable twins are the SDK's `WaterRefract_ps11.psh`,
    `WaterReflect_ps11.psh`, `WaterCheap_ps20.fxc` and `water_dx80.cpp`:

    1. refract pass: `refractRT(projUV + dudv x $refractamount) x $refracttint`, the RT being the
       scene below the plane rendered with the water fog (`WaterFog`, linear over the
       through-water distance toward `$fogcolor`);
    2. reflect pass, SRC_ALPHA-blended: `reflectRT(projUV + dudv x $reflectamount) x
       $reflecttint`, alpha `(1 - N.V)^5` on the `$normalmap` normal, R0 = 0 (PS c3 = 1,0,0,0);
    3. cheap pass, SRC_ALPHA-blended by `saturate((dist_in - $cheapwaterstartdistance) /
       (end - start))` (defaults 500 / 1000 in): `$fogcolor + cube(reflect(eye, N)) x fresnel`.
       `$envmap` defaults to `engine/defaultcubemap` when the VMT names none, so EVERY expensive
       unit carries this overlay and is fully cheap past `end`; `$forcecheap` draws only this
       pass, alpha 1. Water has no diffuse term: nothing in the program is lit.

    The translation, pin by pin (the numbers stay authored on the instance; the graph converts):

    - **Base Color**: BLACK unless a base texture is bound (four corpus units, none placed on a
      water map): `lerp(base x Color x RefractTint, WaterColor, WaterMurkiness)`, scaled by
      `LightStyleBrightness`. A white base was the pier's tan sheet and the basin's blue.
    - **Refraction** (`RM_2D_OFFSET`): `dudv(bumpUV, frame) x RefractAmount x WaterWarpScale` --
      pass 1's screen-space warp, which SLW's base pass applies through
      `ComputeBufferUVDistortion` (scaled by `saturate(thickness_cm / 30)`). Gated
      `UseAnimatedDuDvFrames`. `WaterWarpScale` is an `MPC_ElysiumSurfaces` knob (0.01/unit).
    - **Normal**: `dev/water_normal`'s 29 frames at the `animatedtexture` rate over the
      `texturescroll` panner, gated `UseNormalMap`, tilted by `dudv x ReflectAmount x
      WaterReflectWarpScale` and renormalised -- pass 2's warp of the reflection image, as a
      reflection-direction tilt (named modernization: there is no reflection image to displace).
      It drives SLW's Schlick (pass 2's alpha) and the Lumen mirror (pass 2's RT).
    - **Specular** = `class_specular x luma(ReflectTint)`, zero under `Underside`; **Roughness**
      0.05 (a planar mirror render; Lumen forces mirror on SLW anyway), 1 under `Underside`
      (`Mod_LoadFaces` calls `$reflecttexture->SetUndefined()` on every down-facing face: the
      underside has no reflect pass). `ReflectTint` has no pin of its own -- recorded divergence.
    - **Absorption / Scattering** (1/cm): `range = max((FogEnd - FogStart) x 2.54, 1)`;
      `sigma = WaterFogScale / range` (default 2 ln 2, where SLW's exponential and VtMB's linear
      fog agree at the half-fog distance); `c = pow(FogColor.rgb, 2.2)`; Scattering = `c x sigma`,
      Absorption = `(1 - c) x sigma`; both 0 under `UseFogEnable` off or `CheapWater`. The
      underside keeps the surface's coefficients (R7.5 contract 1).
    - **Color Scale Behind Water** = `RefractTint` (pass 1's tint). **PhaseG** = 0.
    - **Emissive** = pass 3: `(fog_decoded + cube(reflect(V, N)) x EnvMapTint x fresnel^5) x
      blend`, `blend` the distance blend (1 under `CheapWater`), the cube gated `UseFixedCube`
      (the stage always binds one on a water instance: the authored cube, the map's VBSP probe,
      or `engine/defaultcubemap`; `$forceexpensive` alone leaves it unbound). Times
      `LightStyleBrightness`.
    - **Opacity** (coverage, `WaterVisibility = 1 - Opacity`): `saturate(textured coverage +
      blend)` -- past `$cheapwaterenddistance` nothing refracts through, as under the 2004
      SRC_ALPHA overlay; a `$forcecheap` unit never refracts at all.
    - **LightStyleBrightness** (R7.5 G6): Custom Primitive Data slot 6, see
      `_light_style_brightness`.

    Not carried: the map's range fog on the surface itself (`CalcFog RANGE` in `Water_vs11`) --
    the water master's `FogStart`/`FogEnd`/`FogColor` names are the VMT volume keys, so the
    per-primitive scene-fog lane (`_scene_fog`) cannot share them; recorded in
    `water-architecture.md`. Every parameter that stays declared-not-wired is named, with its
    VtMB fact, in `WaterParams`' own docstring -- that table is the contract, not this docstring.
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

    # NAMED DIVERGENCE (R7.5, owner decision 3): this panner runs. The 2004 water shader IGNORED
    # `$bumpoffset` on 13 water units including the sewer -- the `TextureScroll` proxy wrote a
    # register `Water_Old` never read, so the authored scroll did not move on screen
    # (`docs/vtmb/water.md`'s decompile pass). Authored intent versus 2004 result; the owner's
    # call is intent, because the alternative drops motion the author explicitly wrote. The stage
    # records the same divergence on the proxy that resolves the rate
    # (`materials._apply_proxies`'s `texturescroll` branch).
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
    # BLACK, not white, when no base texture is bound (R7.5 look pass, `water_audit/LOOK_SPEC.md`):
    # `Water_Old` has no diffuse term at all -- its three passes are the refracted scene, the
    # mirrored scene and the cheap cube, none of them lit -- so the untextured water family must
    # put nothing into BaseColor for Lumen to light. A white base here was the pier's tan sheet and
    # the basin's blue: the beach and the cave lighting a surface VtMB never shades. `default=False`
    # (review fix): the shipped water corpus is mostly untextured (18/24 units), so a grey-checker
    # default on the missing-parameter path is the wrong failure mode here.
    black_base = g.const3(0.0, 0.0, 0.0, -900, -240)
    base_selected = g.switch(P.Switches.UseBaseTexture, base_tex_rgb, black_base, -700, -360,
                             default=False)
    base_a_selected = g.switch(P.Switches.UseBaseTexture, base_tex_a, g.const(1.0, -900, -160),
                               -700, -200, default=False)

    color = g.vec3(P.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -1100, -560)
    refract_tint = g.vec3(P.Vectors.RefractTint, (1.0, 1.0, 1.0, 1.0), -1100, -680)
    tinted = g.mul(g.mul(base_selected, "", color, "", -500, -420), "", refract_tint, "",
                   -300, -460)

    # NAMED MODERNIZATION (R7.5, verdict C4): `$watermurkiness` was registered by no shipped VtMB
    # shader -- the 2004 renderer read the key and did nothing with it. Four corpus units author a
    # non-default value (`dev/dev_water` 0.85, `dev/nether01_water` 0.8, `water/oilfieldwatera`
    # and `water/oilfieldwaterb` 1.0; none on the three maps in R7.5's scope). The owner's call is
    # to KEEP the wire: it is authored intent with an obvious meaning, un-wiring it puts nothing
    # in its place, and the port's rule is to replace a 2004 engine limit with the native
    # expression rather than to reproduce the limit. Base colour lerps toward `WaterColor` by it.
    water_color = g.vec3(P.Vectors.WaterColor, (0.0, 0.0, 0.0, 0.0), -1100, -800)
    murkiness = g.scalar(P.Scalars.WaterMurkiness, 0.0, -1100, -880)
    base_color_murky = g.lerp(tinted, "", water_color, "", murkiness, "", -100, -600)
    light_style = _light_style_brightness(g, P.Scalars.LightStyleBrightness, -1100, -960)
    base_color_final = g.mul(base_color_murky, "", light_style, "", 100, -600)
    g.to(base_color_final, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # -- Normal: ONE normal carrying both of VtMB's warps (R7.5 G3). The ripple half is the
    # flipbook lane (`dev/water_normal`'s own 29 frames -- the stage binds `$normalmap`'s array,
    # not the DUDV's), gated UseNormalMap. The refraction half is `dev/water_dudv`'s 29 signed
    # slices, biased and added into the tangent XY -- see the docstring for why the two per-pass
    # strengths fold into their mean here ------------------------------------------------------
    dudv_tex = g.tex(P.Textures.DuDvMap, -1100, 60, kind="normal")
    connect(bump_uv, "", dudv_tex, "UVs")

    normal_tex_2d = g.tex(P.Textures.NormalMap, -1100, 260, kind="normal")
    connect(bump_uv, "", normal_tex_2d, "UVs")
    normal_tex = _flipbook_sample(
        g, normal_tex_2d, P.Textures.NormalMapFrames, bump_uv,
        P.Scalars.NormalFrameRate, P.Scalars.NormalFrameCount,
        P.Switches.UseAnimatedNormalFrames, default_normal_frames, -1100, 340, sampler="normal")
    flat_normal = g.const3(0.0, 0.0, 1.0, -700, 140)
    normal_lit = g.switch(P.Switches.UseNormalMap, g.mask(normal_tex, "rgb", -700, 260),
                          flat_normal, -500, 220, default=False)

    # `sampler="linear"`, not `"normal"`: `TA_water_dudv` stages `TC_VECTOR_DISPLACEMENTMAP`
    # (whose default sampler type is linear colour), and its payload is Source's own unsigned-byte
    # DUDV encoding -- "signed" is the shader's `x * 2 - 1` reading of an unsigned texel
    # (`water_audit/phase0/U1b_textures.md` section 4, verified byte-exact against the VTF). The
    # bias is therefore explicit below rather than borrowed from `UnpackNormalMap`.
    dudv_object = g.tex_object(P.Textures.DuDvMapFrames, -1100, 620, default_dudv_frames,
                               sampler="linear")
    dudv_slice = _flipbook_slice(g, P.Scalars.DuDvFrameRate, P.Scalars.DuDvFrameCount, -1100, 700)
    dudv_uv = g.append(bump_uv, "", dudv_slice, "", -700, 860)
    dudv_sample = g.sample(dudv_object, dudv_uv, -500, 860)
    dudv_biased = g.sub(g.mul(g.mask(dudv_sample, "rg", -300, 860), "",
                              g.const(2.0, -300, 940), "", -100, 860),
                        "", g.const(1.0, -100, 940), "", 100, 860)
    # `Water_Old` warps the two render targets in SCREEN space: `WaterRefract_ps11` /
    # `WaterReflect_ps11` (`texbem` off the DUDV, matrix = `$refractamount` / `$reflectamount`,
    # `water_dx80.cpp::DrawRefraction/DrawReflection`; the SM2 `_old` twins take the same amounts
    # through VS c44, FUN_100138a0 / FUN_10013b30). The two warps land on two different pins here
    # (R7.5 look pass, `water_audit/LOOK_SPEC.md`):
    #
    # - the REFRACTION warp is the Refraction pin in `RM_2D_OFFSET` mode -- the same explicit
    #   screen-UV offset `Water_Old` applied to `_rt_WaterRefraction`, which SLW's base pass reads
    #   through `ComputeBufferUVDistortion` (`SingleLayerWaterShading.ush`, scaled by
    #   `saturate(thickness_cm / 30)`); folding it into the normal instead gave the physical
    #   model's millimetre offset, which is the "flat transparency" the owner saw;
    # - the REFLECTION warp tilts the normal Lumen mirrors off, `N + dudv x amount x scale`,
    #   renormalised (named modernization: no reflection image to displace, so the displacement
    #   becomes a reflection-direction tilt of the same authored strength).
    #
    # `WaterWarpScale` / `WaterReflectWarpScale` are `MPC_ElysiumSurfaces` knobs (default 0.01 per
    # authored unit: the amounts are authored 15-100 and VtMB's frames show a warp of 1-2 % of the
    # frame against a DUDV whose signed rms is 0.027).
    refract_amount = g.scalar(P.Scalars.RefractAmount, 20.0, -1100, 1020)
    reflect_amount = g.scalar(P.Scalars.ReflectAmount, 50.0, -1100, 1080)
    warp_scale = g.mpc("WaterWarpScale", -1100, 1140)
    reflect_warp_scale = g.mpc("WaterReflectWarpScale", -1100, 1200)
    zero2 = g.append(g.const(0.0, -300, 1000), "", g.const(0.0, -300, 1060), "", -100, 1000)
    dudv_gated = g.switch(P.Switches.UseAnimatedDuDvFrames, dudv_biased, zero2, 100, 960,
                          default=False)
    refract_offset = g.mul(dudv_gated, "", g.mul(refract_amount, "", warp_scale, "", -700, 1040),
                           "", 300, 900)
    g.to(refract_offset, "", unreal.MaterialProperty.MP_REFRACTION)
    reflect_offset = g.mul(dudv_gated, "",
                           g.mul(reflect_amount, "", reflect_warp_scale, "", -700, 1160),
                           "", 300, 1080)
    reflect_offset3 = g.append(reflect_offset, "", g.const(0.0, 300, 1160), "", 500, 1100)
    normal_final = g.normalize(g.add(normal_lit, "", reflect_offset3, "", 700, 260), "", 900, 260)
    g.to(normal_final, "", unreal.MaterialProperty.MP_NORMAL)

    # -- surface class lookup; Specular = class specular x luma(ReflectTint), stripped on the
    # underside (Mod_LoadFaces' `$reflecttexture->SetUndefined()` on every down-facing face) ----
    class_roughness, class_specular, class_metallic = _class_lut_influenced(
        g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)

    reflect_tint = g.vec3(P.Vectors.ReflectTint, (1.0, 1.0, 1.0, 1.0), -1900, 2080)
    luma_weights = g.const3(0.299, 0.587, 0.114, -1900, 2160)
    reflect_tint_luma = g.dot(reflect_tint, "", luma_weights, "", -1700, 2080)
    specular_water = g.mul(class_specular, "", reflect_tint_luma, "", -1300, 2000)
    specular_final = g.switch(P.Switches.Underside, g.const(0.0, -1100, 2060), specular_water,
                              -900, 1960, default=False)
    g.to(specular_final, "", unreal.MaterialProperty.MP_SPECULAR)
    # R7.5 contract 1: the underside has NO reflection, which on a deferred renderer takes two
    # pins, not one. Specular 0 removes the direct highlight; roughness 1 removes the mirror Lumen
    # would otherwise still resolve off a smooth surface. VtMB's own response to a down-facing
    # water face is `$reflecttexture->SetUndefined()` (`Mod_LoadFaces`), which removes the whole
    # reflection pass for that face -- both pins together are that statement.
    # Roughness: a MIRROR on the surface (`_rt_WaterReflection` is a planar mirror render, and
    # Lumen forces mirror reflections on this shading model anyway); the class row is not a water
    # fact. The underside keeps roughness 1 for the reason above.
    roughness_final = g.switch(P.Switches.Underside, g.const(1.0, -1100, 2140),
                               g.const(0.05, -1100, 2200), -900, 2100, default=False)
    g.to(roughness_final, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to(class_metallic, "", unreal.MaterialProperty.MP_METALLIC)

    # -- The cube. `EnvMap` is always bound on a water instance by the stage: the authored fixed
    # cube, else the map's VBSP-patched probe (`maps/<map>/c...`), else `engine/defaultcubemap` --
    # `water.cpp::SHADER_INIT_PARAMS` writes that default itself when the VMT names none and does
    # not force expensive. The `env_cubemap -> Lumen` policy does not apply to water: the cheap
    # pass samples a texture, and VtMB's probe IS the authored reflection (R7.5 look pass). The
    # cube is read once, below, by the cheap overlay; the old "fixed cube add" emissive is gone
    # (it double-counted the same sample against the overlay).
    env_tint = g.vec3(P.Vectors.EnvMapTint, (1.0, 1.0, 1.0, 1.0), -1900, 2240)
    reflect_dir = g.reflection_ws(-1900, 2400)
    envcube = g.cube(P.Textures.EnvMap, -1700, 2400, default=DEFAULT_CUBE)
    connect(reflect_dir, "", envcube, "UVs")
    # The emissive pin is written at the end of the fog block below, which needs the decoded
    # `$fogcolor` for the cheap overlay and would otherwise decode it twice.

    # -- The volume: the VMT fog keys as SLW extinction (see the docstring) ---------------------
    fog_color = g.vec4(P.Vectors.FogColor, (0.0, 0.0, 0.0, 0.0), -1900, 4120)
    fog_start = g.scalar(P.Scalars.FogStart, 1.0, -1900, 3960)
    fog_end = g.scalar(P.Scalars.FogEnd, 400.0, -1900, 4040)
    fog_color_rgb = g.mask(fog_color, "rgb", -1700, 4120, src_out="RGBA")
    fog_decoded = g.pow(fog_color_rgb, "", g.const(2.2, -1700, 4200), "", -1500, 4140)
    fog_span_in = g.sub(fog_end, "", fog_start, "", -1700, 4000)
    fog_span_cm = g.mul(fog_span_in, "", g.const(2.54, -1700, 3920), "", -1500, 4000)
    fog_range = g.clamp(fog_span_cm, "", g.const(1.0, -1500, 3860), "",
                        g.const(1.0e9, -1500, 3900), "", -1300, 4000)
    fog_scale = g.mpc("WaterFogScale", -1500, 3800)
    sigma = g.div(fog_scale, "", fog_range, "", -1100, 3960)
    scattering = g.mul(fog_decoded, "", sigma, "", -500, 4100)
    absorption = g.mul(g.one_minus(fog_decoded, "", -700, 4200), "", sigma, "", -500, 4220)
    zero3 = g.const3(0.0, 0.0, 0.0, -500, 4320)
    scattering_fogged = g.switch(P.Switches.UseFogEnable, scattering, zero3, -300, 4100,
                                 default=False)
    absorption_fogged = g.switch(P.Switches.UseFogEnable, absorption, zero3, -300, 4220,
                                 default=False)
    # `CheapWater` is not a murkier volume, it is no volume at all: `WaterCheap_ps11` never reads
    # `_rt_WaterRefraction` and its whole output is `lrp r0.rgb, fresnel, cube x $reflecttint,
    # c0($fogcolor)` on a `SURF_NOLIGHT` face (`docs/vtmb/water.md` -> "Expensive vs cheap"). So
    # the coefficients go to zero and the colour is EMITTED, not scattered. Scattering would have
    # been wrong twice over: it needs incident light, and an unlit basin (`sp_soc_3`, witnessed
    # 2026-09-04) rendered black where VtMB draws its `$fogcolor`.
    scattering_final = g.switch(P.Switches.CheapWater, zero3, scattering_fogged, -300, 4160,
                                default=False)
    absorption_final = g.switch(P.Switches.CheapWater, zero3, absorption_fogged, -300, 4280,
                                default=False)
    # R7.5 contract 1: `Underside` does NOT zero the coefficients any more. The underside is the
    # same body of water seen from below, and its extinction is the surface's -- VtMB's own
    # down-facing-face rule touches `$reflecttexture` and nothing else. (5.8's SLW
    # camera-under-water branch is hardcoded off, so an underside face still integrates the volume
    # as if the eye were above it; `M_ElysiumUnderwater`, the post-process, is what actually fogs
    # the view from inside. Phase 4 witnesses the pair together on `sm_hub_1`.)

    # -- The cheap pass, as `Water_Old` draws it on EVERY water unit (R7.5 look pass, from
    # `water.cpp::SHADER_DRAW` + `WaterCheap_ps20.fxc`, the SM2 program `FUN_10013d30` binds):
    #
    #   rgb = $fogcolor + cube(reflect(eye, N)) x fresnel,   fresnel = (1 - N.V)^5, R0 = 0
    #   a   = saturate((dist_in - $cheapwaterstartdistance) / (end - start))   [SRC_ALPHA blend]
    #
    # drawn OVER the expensive result, so a canal is the mirrored/refracted scene near the eye
    # and fog colour plus cube glints past `$cheapwaterenddistance` (defaults 500 / 1000 in,
    # `SHADER_INIT_PARAMS`). `$forcecheap` units draw only this pass, with alpha 1. The blend is
    # `Opacity` here (coverage: 1 = nothing refracts through) and the colour is Emissive, the
    # only pin that survives an unlit face (`SURF 0x408` = `WARP|NOLIGHT`). The `ps11` twin's
    # `lrp(fresnel, cube x $reflecttint, $fogcolor)` differs only at grazing angles; the SM2
    # program is the live one on DX9 hardware.
    #
    # `$fogcolor` is authored as display bytes and VtMB writes them to the framebuffer as-is; the
    # port decodes them to linear (`pow 2.2`) so the same bytes come back out of the display
    # transform -- the witness measures this against the reference frames (basin (27,25,15) for an
    # authored {22 20 10}). `EnvMapTint` scales the cube (authored on the LMG water fakes only).
    camera_pos = g.node(unreal.MaterialExpressionCameraPositionWS, -1900, 4700)
    world_pos = g.node(unreal.MaterialExpressionWorldPosition, -1900, 4780)
    eye_dist = g.node(unreal.MaterialExpressionDistance, -1700, 4740)
    connect(camera_pos, "", eye_dist, "A")
    connect(world_pos, "", eye_dist, "B")
    eye_dist_in = g.div(eye_dist, "", g.const(2.54, -1700, 4820), "", -1500, 4740)
    cheap_start = g.scalar(P.Scalars.CheapWaterStartDistance, 500.0, -1900, 3800)
    cheap_end = g.scalar(P.Scalars.CheapWaterEndDistance, 1000.0, -1900, 3880)
    cheap_span = g.clamp(g.sub(cheap_end, "", cheap_start, "", -1700, 3840), "",
                         g.const(1.0, -1700, 3920), "", g.const(1.0e9, -1700, 3960), "",
                         -1500, 3860)
    cheap_blend_expensive = g.sat(
        g.div(g.sub(eye_dist_in, "", cheap_start, "", -1300, 4740), "", cheap_span, "",
              -1100, 4760), "", -900, 4760)
    cheap_blend = g.switch(P.Switches.CheapWater, g.const(1.0, -900, 4680),
                           cheap_blend_expensive, -700, 4720, default=False)

    cheap_fresnel = g.fresnel(-500, 4460, exponent=5.0)
    connect(g.scalar(P.Scalars.BaseReflectFract, 0.0, -700, 4460), "", cheap_fresnel,
            "BaseReflectFractionIn")
    cheap_glint = g.mul(g.mul(envcube, "RGB", env_tint, "", -500, 4560), "", cheap_fresnel, "",
                        -300, 4560)
    cheap_glint_bound = g.switch(P.Switches.UseFixedCube, cheap_glint,
                                 g.const3(0.0, 0.0, 0.0, -300, 4640), -100, 4580, default=False)
    cheap_colour = g.add(fog_decoded, "", cheap_glint_bound, "", 100, 4500)
    cheap_emissive = g.mul(cheap_colour, "", cheap_blend, "", 300, 4520)
    g.to(g.mul(cheap_emissive, "", light_style, "", 500, 2400), "",
         unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # The output node is created here, immediately before its four inputs: every graph mutation
    # made while it exists with nothing connected raises `No inputs to Single Layer Water Material`
    # (`MaterialExpressions.cpp:21330` -- `CompileCustomOutputs` compiles every gathered custom
    # output whatever the shading model is), and every mutation made while the master is already
    # MSM_SINGLE_LAYER_WATER with no output node raises `SingleLayerWater materials requires the
    # use of SingleLayerWaterMaterial output node` (`MaterialShared.cpp:6447`). Creating it last
    # and wiring it at once closes the first window; the caller setting the shading model only
    # after this function returns closes the second.
    slw = g.node(unreal.MaterialExpressionSingleLayerWaterMaterialOutput, 300, 4160)
    connect(scattering_final, "", slw, "ScatteringCoefficients")
    connect(absorption_final, "", slw, "AbsorptionCoefficients")
    connect(g.const(0.0, 100, 4300), "", slw, "PhaseG")
    connect(refract_tint, "", slw, "ColorScaleBehindWater")

    # -- Opacity: coverage. 0 for water; a base-textured unit keeps Alpha x BaseTexture.a --------
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    opacity_textured = g.mul(alpha_param, "", base_a_selected, "", 1900, 0)
    opacity_textured_or_none = g.switch(P.Switches.UseBaseTexture, opacity_textured,
                                        g.const(0.0, 1900, 80), 2300, 20, default=False)
    # Coverage rises with the cheap blend (`WaterVisibility = 1 - Opacity`): the cheap pass is
    # alpha-blended OVER the refracted scene, so past `$cheapwaterenddistance` nothing behind the
    # surface shows through, and a `$forcecheap` unit (blend 1) never refracts at all. The surface
    # BRDF and the emitted fog colour are the whole picture there, which is what the 2004 pass
    # outputs.
    opacity_final = g.clamp(g.add(opacity_textured_or_none, "", cheap_blend, "", 2300, 60), "",
                            g.const(0.0, 2300, 140), "", g.const(1.0, 2300, 180), "", 2500, 40)
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY)

    # -- Declared, not wired -- every one of these is named, with the VtMB measurement behind it,
    # in `WaterParams`' own docstring. `RefractAmount`/`ReflectAmount`/`BaseReflectFract` left
    # this block in R7.5 (they are the DUDV warp strength and the cheap Fresnel's base fraction),
    # `CheapWaterStart/EndDistance` left it in the look pass (they are the cheap overlay's blend);
    # what remains is the dead `Water_Old` vertex-wave block, VBSP's depth hint and the
    # `env_cubemap` switch (a water instance always binds a cube, see the cube block) ------------
    g.switch(P.Switches.UseEnvMap, g.const(1.0, -1900, 2840), g.const(1.0, -1900, 2900),
             -1700, 2860, default=False)
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


# ============================================================================================
# M_ElysiumUnderwater -- the post-process the water actor blends in below the plane (R7.1 D)
# ============================================================================================


class UnderwaterParams:
    """`ElysiumSurfaceParamsDecal`'s fog triple, restated: `FogColor` (linear, already decoded by
    `ElysiumFog::Pack`), `FogStart` (cm) and `FogInvRange` (1 / (end - start), 0 = off) -- the
    same three names `ElysiumFog::ApplyToDecalMID` writes, so one packer serves the scene fog, the
    decals and the underwater view."""

    class Scalars:
        FogStart = "FogStart"
        FogInvRange = "FogInvRange"

    class Vectors:
        FogColor = "FogColor"


UNDERWATER_PARAM_TABLE = {
    "scalars": sorted(vars(UnderwaterParams.Scalars)[k] for k in vars(UnderwaterParams.Scalars)
                      if not k.startswith("_")),
    "vectors": sorted(vars(UnderwaterParams.Vectors)[k] for k in vars(UnderwaterParams.Vectors)
                      if not k.startswith("_")),
}


def _build_underwater(mat):
    """`ViewDrawScene_EyeUnderWater` draws the below-water world and the water surfaces under
    `SetFogVolumeState(id, false)` -- `MATERIAL_FOG_LINEAR` over the whole scene with the volume's
    `$fogcolor`/`$fogstart`/`$fogend` -- and nothing else: no warp, no tint, no reflection pass.
    Transcribed as one scene-depth lerp, applied before DOF so it works in linear HDR like the
    per-primitive scene fog does: `lerp(scene, FogColor, saturate((SceneDepth - FogStart) x
    FogInvRange))`. The above-water world is seen through the surface, whose depth SLW writes, so
    the whole refracted view fogs at the plane's distance -- what pass 2 does to the surface."""
    g = Graph(mat)
    P = UnderwaterParams

    scene = g.node(unreal.MaterialExpressionSceneTexture, -900, -200)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    scene_rgb = g.mask(scene, "rgb", -700, -200, src_out="Color")

    # Depth through `SceneTexture:SceneDepth`, not the bare `SceneDepth` node: the engine's own
    # `M_UnderWater_PostProcess_Volume` (Water plugin, the shipped precedent ruling D cites)
    # carries no `MaterialExpressionSceneDepth` at all and reads its depth this way.
    #
    # Witnessed 2026-09-04 (`water-architecture.md` section 11): the fog is applied, and on the two
    # converted maps it is nearly invisible *by the authored numbers*, not by a defect. The
    # underside surface writes its own depth, so every pixel that sees the above-water world
    # through the plane fogs at the plane's distance -- 20 cm overhead, one to three metres along
    # the rows just above the horizon -- against a 2,600 cm range, which is also what VtMB's pass
    # 2 does to the surface it draws. The below-water world fogs by its true depth, and in a
    # 50 cm canal that is a hand's breadth of floor. The proof was a red 1 m override through the
    # map's unbound volume at a priority above the water volume's: the whole below-plane band went
    # red at once. Below the water volume's priority the same override changed nothing, because
    # the engine merges every blendable of one material into one node and the higher-priority
    # values win -- the trap the first witness fell into. Deep basins (`hw_warrens_*`,
    # `la_hub_1`) are where the volume fog becomes a look.
    depth_texture = g.node(unreal.MaterialExpressionSceneTexture, -900, 200)
    depth_texture.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_SCENE_DEPTH)
    depth = g.mask(depth_texture, "r", -700, 200, src_out="Color")

    fog_color = g.vec4(P.Vectors.FogColor, (0.0, 0.0, 0.0, 1.0), -900, 0)
    fog_color_rgb = g.mask(fog_color, "rgb", -700, 0, src_out="RGBA")
    fog_start = g.scalar(P.Scalars.FogStart, 0.0, -900, 320)
    fog_inv_range = g.scalar(P.Scalars.FogInvRange, 0.0, -900, 400)
    fog_factor = g.sat(
        g.mul(g.sub(depth, "", fog_start, "", -700, 260), "", fog_inv_range, "", -500, 300),
        "", -300, 300)
    fogged = g.lerp(scene_rgb, "", fog_color_rgb, "", fog_factor, "", -100, 0)
    g.to(fogged, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def make_underwater():
    name = "M_ElysiumUnderwater"
    asset = "%s/%s" % (PKG, name)
    recipe = {
        "graphVersion": GRAPH_VERSION,
        "sourceHash": _source_hash(),
        "params": UNDERWATER_PARAM_TABLE,
    }
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset, producer='v2-masters') == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    mat, asset = _fresh(name)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("blendable_location",
                            unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)

    _build_underwater(mat)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    bl.stamp_recipe(mat, fingerprint, producer='v2-masters')
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


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
            and bl.stored_recipe(asset, producer='v2-masters') == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    # Water has no BaseTextureFrames slot at all -- only NormalMapFrames -- so its only default
    # flipbook array is the normal one (H3 review fix: sampled `sampler="normal"`, so it needs the
    # flat-normal-packed default, not the all-white `T_V2_DefaultFrames`).
    default_normal_frames = _make_default_normal_frames_array(force=force)
    # R7.5 G3: the DUDV lane's own default array -- a separate asset from the normal lane's
    # because the two are sampled through different sampler types (see
    # `_make_default_dudv_frames_array`).
    default_dudv_frames = _make_default_dudv_frames_array(force=force)

    # R7.1 ruling A: Single Layer Water -- Opaque (the shading model's own rule, and what the
    # material lane's per-instance `Opaque` override already said), one-sided (ruling E: the
    # underside is its own set of faces), never Nanite (`NaniteResources.cpp` rejects the shading
    # model; the water faces already live in the non-Nanite `T_` chunk bucket). The blend is set
    # before the shading model: a fresh `UMaterial` is Opaque already, and SLW compiles only on an
    # opaque or masked material, so no intermediate compile ever sees an invalid pair.
    # used_with_instanced_static_meshes stays on (the placement lane may use ISM for water planes).
    # The shading model is set *after* the graph: a fresh `UMaterial` is MSM_DEFAULT_LIT, and each
    # of the ~95 expression writes `_build_water` makes triggers a compile of the graph as it
    # stands, so authoring under MSM_SINGLE_LAYER_WATER logged 96 `Failed to compile Material`
    # warnings per run for a master that compiles clean at the end (see `_build_water`'s note on
    # the output node). Nothing in the graph reads the shading model, and the blend mode is opaque
    # before and after, so the pair is never invalid.
    mat, asset = _fresh(name, ism=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", False)
    # `RM_2D_OFFSET`: the Refraction pin is `Water_Old`'s own screen-space DUDV warp of the
    # refracted scene (see `_build_water`'s warp block), which SLW's base pass applies through
    # `ComputeBufferUVDistortion`. `refraction_depth_bias` stays 0.
    mat.set_editor_property("refraction_method", unreal.RefractionMode.RM_2D_OFFSET)

    _build_water(mat, collection, lut_texture, default_normal_frames, default_dudv_frames)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, WATER_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint, producer='v2-masters')
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
    # `ParticleColor` x `VertexColor`, both of them, because this one master draws through two
    # different vertex factories and each hardcodes the *other* term to white:
    #   - a Niagara sprite (`NiagaraSpriteVertexFactory.ush`'s `GetMaterialPixelParameters`) sets
    #     `Result.VertexColor = 1` and fills only `Result.Particle.Color`. Reading `VertexColor`
    #     alone is what drew the barrel fire as an opaque saturated ball under an opaque black
    #     smoke card while its `ScaleColor` ramps held the right 0.235-peak numbers;
    #   - the `env_sprite` billboard (`ElysiumSpriteComponent.cpp`, an `FDynamicMeshBuilder` on the
    #     local vertex factory) carries no particle data at all, so `Particle.Color` compiles in as
    #     `(1,1,1,1)` (`MaterialTemplate.ush`) and the entity's `rendercolor`/`renderamt` rides
    #     `VertexColor` alone -- which is exactly how VtMB itself draws it: `CMeshBuilder::
    #     Color4ubv` (`1008232b`) writes the mode's blend colour per corner, with no material
    #     colour modulation anywhere in the Sprite program.
    # The product is exact on both lanes: each factory contributes its own term and reads 1 for the
    # other. `UseVertexColor`/`UseVertexAlpha` keep their names (they are the design's
    # exposed-parameter table) and gate both terms together.
    particle_color = g.particle_color(-1100, -680)
    vertex_color = g.vertex_color(-1100, -780)
    vc_rgb = g.mask(vertex_color, "rgb", -900, -780)
    lane_rgb = g.mul(particle_color, "", vc_rgb, "", -700, -720)
    with_vc = g.mul(tinted, "", lane_rgb, "", -500, -440)
    vc_selected = g.switch(P.Switches.UseVertexColor, with_vc, tinted, -300, -400, default=False)
    g.to(vc_selected, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.to(vc_selected, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # -- class LUT declared for contract completeness ("every master exposes SurfaceClassLUT");
    # Unlit shading model ignores MP_ROUGHNESS/SPECULAR/METALLIC, not wired to anything ---------
    _class_lut(g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)

    # -- Opacity: Alpha x BaseTexture.a, x ParticleColor.a x VertexColor.a under UseVertexAlpha --
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    alpha_with_basetex = g.mul(alpha_param, "", base_a, "", 1900, 0)
    # Both alphas, for the same two-factory reason as the RGB lane above -- an `env_sprite`'s
    # `renderamt` is a vertex alpha, a particle's own fade is `Particles.Color.a`. `ParticleColor`'s
    # and `VertexColor`'s named "A" outputs are both already 1-wide, so neither needs a mask.
    lane_a = g.mul(particle_color, "A", vertex_color, "A", 2100, 120)
    alpha_with_vc = g.mul(alpha_with_basetex, "", lane_a, "", 2100, 40)
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
            and bl.stored_recipe(asset, producer='v2-masters') == fingerprint:
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

    bl.stamp_recipe(mat, fingerprint, producer='v2-masters')
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
    # `ParticleColor` x `VertexColor`, both terms -- see `_build_sprite`'s own note. A Niagara
    # sprite compiles `VertexColor` in as 1 and an `env_sprite` billboard compiles
    # `Particle.Color` in as 1, so only the product carries both the particle lane's
    # `red/green/blue/color` ramps and the entity's `rendercolor`/`renderamt`.
    particle_color = g.particle_color(-1100, -680)
    vertex_color = g.vertex_color(-1100, -780)
    vc_rgb = g.mask(vertex_color, "rgb", -900, -780)
    lane_rgb = g.mul(particle_color, "", vc_rgb, "", -700, -720)
    with_vc = g.mul(tinted, "", lane_rgb, "", -500, -440)
    vc_selected = g.switch(P.Switches.UseVertexColor, with_vc, tinted, -300, -400, default=False)

    _class_lut(g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)

    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    alpha_with_basetex = g.mul(alpha_param, "", base_a, "", 1900, 0)
    lane_a = g.mul(particle_color, "A", vertex_color, "A", 2100, 120)
    alpha_with_vc = g.mul(alpha_with_basetex, "", lane_a, "", 2100, 40)
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
            and bl.stored_recipe(asset, producer='v2-masters') == fingerprint:
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

    bl.stamp_recipe(mat, fingerprint, producer='v2-masters')
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
            "masterRecipe": bl.stored_recipe(master_asset, producer='v2-masters'),
            "switches": switches,
            "blendMode": blend,
            "twoSided": True,
        }
        fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
        force = _flag(_cmdline_arg("PolicyForce", ""))
        if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
                and bl.stored_recipe(asset, producer='v2-masters') == fingerprint:
            unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
            made.append(unreal.load_asset(asset))
            continue
        # A stale instance (recipe changed, or `-PolicyForce`) is deleted first: `create_asset`
        # over an existing package refuses unattended, and a failed delete must not fall through
        # to an instance still parented to the previous master object.
        # `load_asset`, not `does_asset_exist`: a commandlet's asset registry has not scanned
        # the mount, so the file on disk reads as missing and `create_asset` then refuses it.
        if unreal.load_asset(asset) is not None:
            if not unreal.EditorAssetLibrary.delete_asset(asset):
                _fail("could not delete stale %s before rebuilding" % asset)
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
        bl.stamp_recipe(mic, fingerprint, producer='v2-masters')
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


#: Screen-offset units per `RefractAmount x DuDvMap.rg x DynamicParameter.x`. `RM_2DOffset` hands
#: the Refraction pin straight to `DistortionCommon.ush`, which then multiplies by the viewport
#: width and Epic's own `OffsetFudgeFactor` (0.00023) -- so a 1920-wide view turns one unit of
#: Refraction into roughly *half a screen* of displacement, and this constant is what brings that
#: back to a heat haze. Tuned by eye against `Fire_Heat` on the witness pedestal with a striped
#: backdrop (`sheet.py --stripes`, the only way a refraction card is visible in a still frame at
#: all -- against a flat wall it displaces nothing you can see): 0.02 tears the background into
#: blobs, 0.003 still reads as glass, 0.001 is invisible; 0.002 bends the stripe edges around the
#: flame and leaves the rest of the wall alone.
REFRACT_OFFSET_SCALE = 0.002

REFRACT_PARAM_TABLE = {
    "textures": sorted(vars(RefractParams.Textures)[k] for k in vars(RefractParams.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(RefractParams.Scalars)[k] for k in vars(RefractParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(RefractParams.Vectors)[k] for k in vars(RefractParams.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(RefractParams.Switches)[k] for k in vars(RefractParams.Switches) if not k.startswith("_")),
}


def _build_refract(mat, collection, lut_texture):
    """No shipped source and no transcribed selector for this family (design doc "M_V2_Refract"):
    "the master is Unreal `Refraction` from `$dudvmap`/`$normalmap` scaled by `RefractAmount`,
    tinted by `RefractTint`. Stated as a reconstruction, not a transcription."

    `DuDvMap` drives the refraction directly, as an **explicit 2D screen offset**
    (`RM_2DOffset`), not through the shared pixel normal. That is what a DUDV card *is*: VtMB's
    `engine/particlerefract` (and the world Refract/heatglow programs) displace the framebuffer
    lookup by the map's own centred RG, scaled by `$refractamount` -- `EngineTypes.h`'s
    "Explicit 2D screen offset ... the user is in charge of any strength and fading" is the one
    Unreal mode that says the same thing. Pixel Normal Offset (what this master used before) is
    the wrong shape for the particle case in particular: a camera-facing Niagara sprite's vertex
    normal *is* the view direction, so the whole distortion collapses onto whatever survives the
    normal round-trip, and the strength knob squares itself (it scaled the perturbation *and* the
    `1 + RefractAmount/100` refraction magnitude).

    `DuDvMap` stays on the **normal sampler** even though the corpus binds plain colour art to it
    (`Fire_Heat`'s `T_cloud`). That is deliberate and not a mislabel: on desktop neither
    `DXT5_NORMALMAPS` nor `LA_NORMALMAPS` is defined, so `Common.ush`'s `UnpackNormalMap` is
    exactly `rg * 2 - 1` -- the centring a DUDV read needs, for free -- and it keeps
    `DefaultNormal` (a valid, flat, `VerifySamplerType`-legal default) as the unbound-slot
    texture, so a refract unit with no `$dudvmap` reads an exact `(0,0)` offset instead of
    whatever a colour sampler's grey-checker default would smear across the screen.

    `NormalMap` keeps the lit bump lane on `MP_NORMAL` alone (gated `UseNormalMap`, matching every
    other master). The old `NormalMap.a x RefractAmount` scale on the DuDv term is gone with it:
    under a normal sampler `UnpackNormalMap` returns `w = 1` unconditionally, so that factor was
    structurally 1.0 and never carried `fxc/refract_ps20`'s intent at all.

    `DynamicParameter.x` is the per-particle strength multiplier -- VtMB's `refract` ramp
    (`Fire_Heat`: 3 -> 0 over a 2 s life), written by the emitter's stock
    `DynamicMaterialParameters` module. It defaults to 1.0, which is what every world draw and
    every emitter that does not author the ramp reads (`matgraph.Graph.dynamic_parameter`).

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
    tinted = g.mul(g.mul(base_selected, "", color, "", -500, -420), "", refract_tint, "",
                   -300, -460)
    # The particle lane's own tint, the same `ParticleColor` (not `VertexColor`) the sprite
    # masters read -- a refract leaf's `red/green/blue/color` ramps land in `Particles.Color`, and
    # every non-particle draw reads the compiled-in `(1,1,1,1)` (`MaterialTemplate.ush`).
    particle_color = g.particle_color(-1100, -760)
    base_color_final = g.mul(tinted, "", particle_color, "", -100, -460)

    # -- Normal: NormalMap alone, the lit bump (gated UseNormalMap). DuDvMap is no longer folded
    # in here -- it drives MP_REFRACTION as an explicit 2D offset instead; see the docstring. ----
    refract_amount = g.scalar(P.Scalars.RefractAmount, 20.0, -1100, 620)
    normal_tex = g.tex(P.Textures.NormalMap, -1100, 260, kind="normal")
    connect(uv0, "", normal_tex, "UVs")
    flat_normal = g.const3(0.0, 0.0, 1.0, -700, 140)
    normal_lit = g.switch(P.Switches.UseNormalMap, g.mask(normal_tex, "rgb", -700, 260),
                          flat_normal, -500, 220, default=False)
    g.to(normal_lit, "", unreal.MaterialProperty.MP_NORMAL)

    # -- Refraction: the DUDV card, as `RM_2DOffset`'s explicit screen offset ---------------------
    # `DuDvMap.rg` (already centred to [-1,1] by the normal sampler's `UnpackNormalMap`), scaled by
    # `RefractAmount x DynamicParameter.x x REFRACT_OFFSET_SCALE`, and masked by the card's own
    # alpha -- the distortion pass does *not* mask by Opacity (`DistortAccumulatePS.usf` only reads
    # Opacity as coverage under Substrate), so an unmasked card would push a hard rectangle of
    # background around. VtMB's `Fire_Heat` is exactly that: `color 0, mask 0`, invisible, and the
    # sprite's alpha is the only thing that shapes the distortion.
    dudv_tex = g.tex(P.Textures.DuDvMap, -1100, 60, kind="normal")
    connect(uv0, "", dudv_tex, "UVs")
    dudv_xy = g.mask(dudv_tex, "rg", -900, 60)
    refract_dynamic = g.dynamic_parameter(-1300, 780)
    # `.x` through a ComponentMask on the `RGBA` output, not the node's own `Param1` output pin --
    # see `Graph.dynamic_parameter`'s docstring for why the named pin refuses the connection.
    refract_dynamic_x = g.mask(refract_dynamic, "r", -1100, 780, src_out="RGBA")
    refract_strength = g.mul(
        g.mul(refract_amount, "", refract_dynamic_x, "", -900, 700), "",
        g.const(REFRACT_OFFSET_SCALE, -900, 860), "", -700, 720)
    refract_offset = g.mul(g.mul(dudv_xy, "", refract_strength, "", -500, 120), "",
                           base_a_selected, "", -300, 100)
    g.to(refract_offset, "", unreal.MaterialProperty.MP_REFRACTION)

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

    # -- Opacity: Alpha x BaseTexture.a x ParticleColor.a. The particle factor is what makes a
    # `color 0, mask 0` heat card actually invisible: the distortion pass runs off MP_REFRACTION
    # regardless of Opacity, so the card can distort while contributing no pixels at all. Without
    # it `Fire_Heat` drew `T_cloud` as white speckle over the flame. Non-particle draws read
    # `ParticleColor = (1,1,1,1)`, so world refract units are untouched.
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    opacity = g.mul(g.mul(alpha_param, "", base_a_selected, "", 1900, 0), "",
                    particle_color, "A", 2100, 40)
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
            and bl.stored_recipe(asset, producer='v2-masters') == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    # BLEND_Translucent is not Nanite-compatible -- this master deliberately does not set
    # used_with_nanite (review fix). used_with_instanced_static_meshes stays on.
    # `niagara_sprites` is on because `MI_ParticleRefract` is this master's child and every
    # `normal` + `refract` leaf (`Fire_Heat` and the seven others) draws through it: without the
    # flag the sprite permutation is compiled on demand in the editor and not at all in a
    # packaged build, where the heat cards would fall back to the default grey material.
    mat, asset = _fresh(name, ism=True, skeletal=True, morph=True, niagara_sprites=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property(
        "translucency_lighting_mode",
        unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    # `RM_2D_OFFSET`, not Pixel Normal Offset: the Refraction pin is the DUDV card's own screen
    # displacement (see `_build_refract`'s docstring). `refraction_depth_bias` stays at the
    # default 0 -- `DistortionCommon.ush::PostProcessUVDistortion` reads it as a soft depth
    # threshold, and 0 means "fade only what is level with the surface behind it", which is what
    # keeps a heat card sitting on a barrel from smearing the barrel's own silhouette.
    mat.set_editor_property("refraction_method", unreal.RefractionMode.RM_2D_OFFSET)
    mat.set_editor_property("two_sided", False)

    _build_refract(mat, collection, lut_texture)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, REFRACT_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint, producer='v2-masters')
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
        Emissive = "Emissive"
        SurfaceClassLUT = "SurfaceClassLUT"

    class Scalars:
        Alpha = "Alpha"
        EmissiveScale = "EmissiveScale"
        SurfaceClassIndex = "SurfaceClassIndex"
        FogStart = mat_fog.P_START
        FogInvRange = mat_fog.P_INV_RANGE

    class Vectors:
        Color = "Color"
        FogColor = mat_fog.P_COLOR

    class Switches:
        Unlit = "Unlit"


DECAL_PARAM_TABLE = {
    "textures": sorted(vars(DecalParams.Textures)[k] for k in vars(DecalParams.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(DecalParams.Scalars)[k] for k in vars(DecalParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(DecalParams.Vectors)[k] for k in vars(DecalParams.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(DecalParams.Switches)[k] for k in vars(DecalParams.Switches) if not k.startswith("_")),
}


def _build_decal(mat, collection, lut_texture):
    """R7.2 (`seam_map_material.md` -> "M_V2_Decal"; `seam_migration.md` -> "R7.2 Decals", ruling
    1): the projector master, re-cut to `MD_DeferredDecal` / `BLEND_Translucent` / DefaultLit --
    the one deferred-decal domain a `UDecalComponent` actually draws (`FDeferredDecalProxy`
    substitutes the engine default for anything else, and DBuffer rewrites a would-be Modulate to
    Translucent regardless -- `DecalRenderingCommon.cpp` 47-49). This retires the two-instance
    conflation R5.4 found: the `decalmodulate` family and every `$decal` surface now share one
    projector shape instead of the modulate master picking up only the 38 `decalmodulate` units.

    `BaseTexture` RGB x `Color` (the shared four-parameter table) -> BaseColor, `BaseTexture` A x
    `Alpha` (shared) -> Opacity. `Emissive` x `EmissiveScale` (default 0, the 3 `$selfillum`
    units) -> the self-illum term, always added to whatever lands on Emissive. `Unlit` (the 28
    `unlitgeneric` projector units, `seam_map_material.md` -> "The eight real unresolved
    families") routes the (fogged) base colour into Emissive instead and leaves BaseColor black --
    DefaultLit has no separate unlit shading model to fall back to, so this is the same
    "fake it through Emissive" trick this file already plays for every Unlit-look-under-DefaultLit
    surface (`_build_lit`'s own self-illum term). `UseVertexColor` is dropped: a projected decal
    has no vertex colour. `DecalDepthOffset` is not a knob anywhere in this revision either --
    ruling 3 retires it outright (it biased the old flat-projector's z-fight; the projector
    geometry is coplanar with the wall now, so there is nothing left to bias).

    UV is rebuilt as `(U, 1-V)` -- a deferred decal's projected V axis maps to the component's
    local +Y (up), but the texture is authored top-down like every other slot in this file, so an
    unflipped sample arrives upside-down. Ported verbatim from the retiring
    `pipeline/unreal/make_decal_material.py` (`Constant2Vector(1, -1)` scale + `Constant2Vector(0,
    1)` offset on the base `TextureCoordinate`).

    Fog is `mat_fog.fog_from_params`, R5.3's chosen home (`seam_map_material.md` -> "Decal fog and
    wetness homes"), unchanged by this ruling: a `UDecalComponent` is a `USceneComponent`, not a
    `UPrimitiveComponent`, so it carries no Custom Primitive Data; the three named instance
    parameters default neutral (unfogged) and the placement lane (later, the runtime `Lay()`
    caller too) sets them per decal instance from the map's own fog, never from a per-map material
    package. Fade only reaches whatever is on BaseColor (a decal blends its albedo into the
    GBuffer the wall already fogged); fade + inscatter (the fog colour added back once the
    surface has faded out) reaches whatever is on Emissive -- exactly the retired `M_Decal`
    graph's own split. The fog specular kill is dropped along with the rest of
    Roughness/Specular/Metallic/Normal (ruling 1): the wall keeps its own surface under the decal,
    which is what a lightmapped `$decal` face did -- an unconnected property pin means this
    material never writes that DBuffer channel at all, leaving the receiver's own value alone.
    """
    g = Graph(mat, collection=collection)
    P = DecalParams

    # -- UV = (U, 1-V) -------------------------------------------------------------------------
    uv0 = g.node(unreal.MaterialExpressionTextureCoordinate, -1500, -700)
    flip_scale = g.node(unreal.MaterialExpressionConstant2Vector, -1500, -540)
    flip_scale.set_editor_property("r", 1.0)
    flip_scale.set_editor_property("g", -1.0)
    flip_offset = g.node(unreal.MaterialExpressionConstant2Vector, -1500, -460)
    flip_offset.set_editor_property("r", 0.0)
    flip_offset.set_editor_property("g", 1.0)
    uv_scaled = g.mul(uv0, "", flip_scale, "", -1280, -580)
    uv = g.add(uv_scaled, "", flip_offset, "", -1080, -520)

    # -- BaseTexture x Color -> BaseColor branch, BaseTexture.A x Alpha -> Opacity --------------
    base_tex = g.tex(P.Textures.BaseTexture, -880, -400, kind="color")
    connect(uv, "", base_tex, "UVs")
    base_rgb = g.mask(base_tex, "rgb", -680, -420)
    base_a = g.mask(base_tex, "a", -680, -320, src_out="RGBA")

    color = g.vec3(P.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -880, -560)
    raw_base = g.mul(base_rgb, "", color, "", -460, -460)

    alpha = g.scalar(P.Scalars.Alpha, 1.0, -880, -200)
    opacity = g.mul(base_a, "", alpha, "", -460, -260)

    # -- Emissive x EmissiveScale -- the alpha-masked $selfillum term (3 units) -----------------
    emissive_tex = g.tex(P.Textures.Emissive, -880, 160, kind="color")
    connect(uv, "", emissive_tex, "UVs")
    emissive_rgb = g.mask(emissive_tex, "rgb", -680, 180)
    emissive_scale = g.scalar(P.Scalars.EmissiveScale, 0.0, -880, 320)
    raw_emissive = g.mul(emissive_rgb, "", emissive_scale, "", -460, 240)

    # -- Unlit: route the base-colour term into Emissive and zero BaseColor instead -------------
    black3 = g.const3(0.0, 0.0, 0.0, -200, -620)
    base_pin_raw = g.switch(P.Switches.Unlit, black3, raw_base, -20, -460, default=False)
    routed_base = g.switch(P.Switches.Unlit, raw_base, black3, -20, -100, default=False)
    emissive_pin_raw = g.add(raw_emissive, "", routed_base, "", 220, 60)

    # -- fog: fade on whatever reaches BaseColor, fade + inscatter on whatever reaches Emissive -
    fog_f, fog_inv, fog_color = mat_fog.fog_from_params(g.mat, x=-1500, y=1200)
    base_faded = mat_fog.fade(g.mat, base_pin_raw, "", fog_inv, 460, -460)
    emissive_faded = mat_fog.fade(g.mat, emissive_pin_raw, "", fog_inv, 460, 100)
    emissive_fogged = mat_fog.inscatter(g.mat, emissive_faded, fog_f, fog_color, 660, 100)

    g.to(base_faded, "", unreal.MaterialProperty.MP_BASE_COLOR)
    g.to(emissive_fogged, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.to(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    # -- class LUT declared for contract completeness; Roughness/Specular/Metallic/Normal are not
    # connected -- ruling 1: the wall keeps its own surface under the decal -------------------
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
            and bl.stored_recipe(asset, producer='v2-masters') == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    # R7.2 ruling 3: the projector instance is non-Nanite by construction (the master sets no
    # used_with_nanite) -- the isDecalSurface face group's Nanite split already follows the mesh's
    # bound *surface* master, not this one, so a decal surface the map lane wants Nanite on is a
    # placement error, not a material one.
    mat, asset = _fresh(name, ism=True)
    # Blend mode FIRST, domain second: every `set_editor_property` fires a PostEditChange and
    # recompiles, and a fresh `UMaterial` is BLEND_Opaque, so setting the domain first spends one
    # intermediate compile in the invalid DeferredDecal+Opaque state and logs the engine's
    # "DeferredDecal domain can only use the Blend Modes ..." warning for a material that is about
    # to be valid. The final state is identical either way; the order is what keeps the policy log
    # clean, which is how a real decal-master failure stays visible.
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("two_sided", False)

    _build_decal(mat, collection, lut_texture)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    _probe_all_switches_true(mat, asset, DECAL_PARAM_TABLE["switches"])

    bl.stamp_recipe(mat, fingerprint, producer='v2-masters')
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
            and bl.stored_recipe(asset, producer='v2-masters') == fingerprint:
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
    # The single refresh this instance's `bl.set_tex_param` above deferred to it.
    bl.finish_material_instance(mic)

    bl.stamp_recipe(mic, fingerprint, producer='v2-masters')
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
make_lit_skinned()
make_lit_skinned_translucent()
make_unlit()
make_two_texture()
make_eyes()
make_water()
make_underwater()
make_sprite()
make_sprite_z()
make_sprite_z_lit()
make_refract()
make_decal()
make_missing()
make_particle_children()
