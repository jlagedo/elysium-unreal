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
GRAPH_VERSION = 2

#: `MPC_ElysiumSurfaces` (SF-4.1, C++, landed) owns every one of these rows and their defaults --
#: `make_surface_knobs.py` (`build_content.py` runs it before this file). This generator is a
#: *reader*, never a second writer: `_load_surfaces_collection` fails hard if the collection or
#: any row this file's masters read is missing, rather than seeding a value SF-4.1 already owns.
SURFACES_COLLECTION = "MPC_ElysiumSurfaces"
REQUIRED_MPC_SCALARS = sorted([
    "Overbright", "MaskRoughnessMin", "MaskRoughnessMax", "MaskSpecularScale",
    "MaskMetallicMax", "ChromaticTintStrength", "EnvTintScale", "FixedCubeStrength",
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
    rather than a blanket default."""
    asset = "%s/%s" % (PKG, name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset):
        unreal.EditorAssetLibrary.delete_asset(asset)
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
    """sha256 of this file plus `matgraph.py`'s own text -- the exhaustive half of the recipe
    stamp: any edit to either file invalidates every master's stamp even when `GRAPH_VERSION` was
    not bumped for it."""
    digest = hashlib.sha256()
    for name in ("make_v2_materials.py", "matgraph.py"):
        digest.update(Path(__file__).with_name(name).read_bytes())
    return digest.hexdigest()


def _import_png(name, png_bytes, *, srgb, compression, filter_nearest=False):
    asset = "%s/%s" % (PKG, name)
    existing = unreal.load_asset(asset)
    if existing:
        return existing
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


def _make_linear_white_mask():
    """A real linear 1x1 white mask -- the same trick `make_world_materials.py` uses, kept as its
    own V2-package copy rather than a cross-package reference."""
    png = (b"\x89PNG\r\n\x1a\n"
           + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 0, 0, 0, 0))
           + _png_chunk(b"IDAT", zlib.compress(b"\x00\xff"))
           + _png_chunk(b"IEND", b""))
    return _import_png("T_LinearWhiteMask", png, srgb=False,
                       compression=unreal.TextureCompressionSettings.TC_MASKS)


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


# --- T_V2_DefaultFrames: a 1-slice Texture2DArray, white -----------------------------------


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


def _make_default_frames_array():
    """A real 1x1, one-slice, white `Texture2DArray` -- the `BaseTextureFrames`/`NormalMapFrames`
    default (design doc "The animation and scroll lanes"), authored from a hand-built minimal DX10
    DDS the same way `_make_linear_white_mask` hand-builds a PNG. Whether editor Python's DDS
    importer classifies a 1-slice DX10-array header as `Texture2DArray` (rather than folding a
    single slice down to a plain `Texture2D`, which some importers do) is unverified before a real
    editor run; if it does not, this returns `None`, the two `TextureObjectParameter`s bind no
    default texture, and that is recorded as a doc/implementation gap in the commit report rather
    than silently accepted."""
    asset = "%s/T_V2_DefaultFrames" % PKG
    existing = unreal.load_asset(asset)
    if existing:
        return existing
    pixel = b"\xff\xff\xff\xff"
    dds = b"DDS " + _dds_header(1, 1, 4) + _dds_dx10_header(1) + pixel
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
            "editor Python's DDS importer did not build a 1-slice DX10 array as an array "
            "texture; BaseTextureFrames/NormalMapFrames ship with no default texture "
            "(doc gap, see this file's module docstring)" % texture.get_class().get_name())
        return None
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
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


# ============================================================================================
# Shared graph lanes: UV/scroll, sine, flipbook animation.
# ============================================================================================


def _uv_lanes(g, tex_scale_offset_name, *, base_scroll_names, bump_scroll_names=None):
    """`TexScaleOffset` transform (shared by every UV-consuming slot on the material), then one
    independent `Panner` per lane it is asked for. `(0, 0)` scroll rates are an exact `Panner`
    no-op, so no static switch gates them (design doc "The animation and scroll lanes"). Returns
    `(base_uv, bump_uv_or_None, tex_scale_offset_param)`."""
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
    return base_uv, bump_uv, tex_scale_offset


def _sine_lane(g, *, min_name, max_name, period_name, offset_name, target_mask_name,
               channel_mask_name, x=-1900, y=700):
    """Shader-time sine modulation (design doc "The sine lane"), neutral by construction:
    `SineMin == SineMax == 1` and `SineTargetMask == (0,0,0,0)` by default make every apply() call
    an exact no-op. Returns a dict with the raw `SineValue` node and an `apply(node, node_out,
    target_channel, out_channels, x, y)` helper that gates `node`'s `out_channels` by
    `lerp(1, SineValue, SineTargetMask.<target_channel> * SineChannelMask)`."""
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
    one4 = g.const4(1.0, 1.0, 1.0, 1.0, x, y + 720)

    def apply(node, node_out, target_channel, out_channels, ax, ay):
        # `target_mask` is a raw VectorParameter; its default output is RGB only, so reading its
        # `.w`/alpha component (the EnvMapTint target) needs the explicit "RGBA" output. Always
        # requesting it here is simplest and correct for every channel, not just the 4th.
        target_component = g.mask(target_mask, target_channel, ax, ay, src_out="RGBA")
        scaled = g.mul(channel_mask, "", target_component, "", ax + 220, ay)
        factor4 = g.lerp(one4, "", sine_value, "", scaled, "", ax + 440, ay)
        factor = g.mask(factor4, out_channels, ax + 660, ay)
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
    at `Append(uv, SliceIndex)`, selected over the plain 2D sample by `use_switch_name`. `sampler`
    is `"color"` (`BaseTextureFrames`) or `"linear"` (`NormalMapFrames`) --
    `TextureObjectParameter` never auto-derives a sampler type (see `Graph.tex_object`'s
    docstring), unlike `tex`. Both switch branches are connected through the explicit `"RGBA"`
    output: `sample2d` is a bare `TextureSampleParameter2D` and `array_sample` a bare
    `TextureSample`, and both node types' *default* output is RGB only -- feeding the switch
    through it would silently drop alpha for everything downstream of the returned node, including
    its own default output (see `Graph.switch`'s docstring)."""
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


# ============================================================================================
# M_V2_Lit / M_V2_LitTranslucent
# ============================================================================================


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

    class Vectors:
        Color = "Color"
        SelfIllumTint = "SelfIllumTint"
        EnvMapTint = "EnvMapTint"
        TexScaleOffset = "TexScaleOffset"
        SineTargetMask = "SineTargetMask"
        SineChannelMask = "SineChannelMask"

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


def _build_lit(mat, collection, lut_texture, default_frames, *, translucent):
    """The shared M_V2_Lit / M_V2_LitTranslucent shading graph. `translucent` only changes how
    Opacity is wired (design doc: blend mode itself is a per-instance override, not a
    material-only property, so the two masters share every other pin)."""
    g = Graph(mat, collection=collection)
    P = LitParams

    base_uv, bump_uv, _ = _uv_lanes(
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
        -1100, -600, sampler="color")
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
    class_roughness, class_specular, class_metallic = _class_lut(
        g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)

    # -- reflection contract: mask term -------------------------------------------------------
    normal_tex_2d = g.tex(P.Textures.NormalMap, -1100, 60, kind="normal")
    connect(bump_uv, "", normal_tex_2d, "UVs")
    normal_tex = _flipbook_sample(
        g, normal_tex_2d, P.Textures.NormalMapFrames, bump_uv,
        P.Scalars.NormalFrameRate, P.Scalars.NormalFrameCount,
        P.Switches.UseAnimatedNormalFrames, default_frames, -1100, 260, sampler="linear")
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
    mask_sat = g.sat(mask_scaled, "", 300, 580)

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
    g.to(base_color_final, "", unreal.MaterialProperty.MP_BASE_COLOR)

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
    g.to(total_emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.to(specular_final, "", unreal.MaterialProperty.MP_SPECULAR)
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
    lut_texture = _load_class_lut()
    recipe = _lit_recipe(LIT_CITED_SHADER_UNITS)
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset) == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    _make_linear_white_mask()
    default_frames = _make_default_frames_array()

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

    _build_lit(mat, collection, lut_texture, default_frames, translucent=translucent)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

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

    class Vectors:
        Color = "Color"
        EnvMapTint = "EnvMapTint"
        TexScaleOffset = "TexScaleOffset"
        CloudScale = "CloudScale"
        SineTargetMask = "SineTargetMask"
        SineChannelMask = "SineChannelMask"

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


UNLIT_PARAM_TABLE = {
    "textures": sorted(vars(UnlitParams.Textures)[k] for k in vars(UnlitParams.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(UnlitParams.Scalars)[k] for k in vars(UnlitParams.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(UnlitParams.Vectors)[k] for k in vars(UnlitParams.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(UnlitParams.Switches)[k] for k in vars(UnlitParams.Switches) if not k.startswith("_")),
}


def _build_unlit(mat, collection, lut_texture, default_frames):
    g = Graph(mat, collection=collection)
    P = UnlitParams

    base_uv, _, _ = _uv_lanes(
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
        -1100, -600, sampler="color")
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
    cube_add = g.mul(g.mul(envcube, "RGB", mask_sat, "", -1500, 2040), "",
                     env_tint, "", -1300, 2080)
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
    g.to(total_emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.to(vc_selected, "", unreal.MaterialProperty.MP_BASE_COLOR)

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
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY)
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    # -- CloudAlphaTexture / CloudScale: $cloudalphatexture drives opacity, $cloudscale the UV
    # scale (design doc "The eight real unresolved families" -> cloud). Declared and sampled so
    # UseCloudAlpha has a real term to gate even though only 2 corpus materials use it. --------
    cloud_scale = g.vec3(P.Vectors.CloudScale, (1.0, 1.0, 1.0, 0.0), -1100, 900)
    cloud_uv = g.mul(base_uv, "", g.mask(cloud_scale, "rg", -900, 940), "", -700, 900)
    cloud_tex = g.tex(P.Textures.CloudAlphaTexture, -1100, 1020, kind="mask",
                      default="%s/T_LinearWhiteMask" % PKG)
    connect(cloud_uv, "", cloud_tex, "UVs")
    cloud_alpha = g.mask(cloud_tex, "r", -700, 1020)
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

    _make_linear_white_mask()
    default_frames = _make_default_frames_array()

    mat, asset = _fresh(name, ism=True, nanite=True, niagara_sprites=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", False)

    _build_unlit(mat, collection, lut_texture, default_frames)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

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

    class Vectors:
        Color = "Color"
        TexScaleOffset = "TexScaleOffset"
        Texture2ScaleOffset = "Texture2ScaleOffset"
        SineTargetMask = "SineTargetMask"
        SineChannelMask = "SineChannelMask"

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

    base_uv, _, _ = _uv_lanes(
        g, P.Vectors.TexScaleOffset,
        base_scroll_names=(P.Scalars.BaseScrollRateU, P.Scalars.BaseScrollRateV))
    sine = _sine_lane(
        g, min_name=P.Scalars.SineMin, max_name=P.Scalars.SineMax,
        period_name=P.Scalars.SinePeriod, offset_name=P.Scalars.SineTimeOffset,
        target_mask_name=P.Vectors.SineTargetMask, channel_mask_name=P.Vectors.SineChannelMask)

    # BaseTexture2 rides its own TexScaleOffset-shaped transform (Texture2ScaleOffset), on the
    # same scrolled UV lane -- worldvertextransition has one lightmap-alpha blend, not two
    # independently scrolling layers.
    tex2_scale_offset = g.vec4(P.Vectors.Texture2ScaleOffset, (1.0, 1.0, 0.0, 0.0), -1900, 260)
    tex2_scale = g.mask(tex2_scale_offset, "rg", -1700, 220)
    tex2_offset = g.mask(tex2_scale_offset, "ba", -1700, 320, src_out="RGBA")
    base_uv2 = g.add(g.mul(base_uv, "", tex2_scale, "", -1500, 260), "", tex2_offset, "",
                     -1300, 300)

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
    g.to(overbright_base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # -- Normal: one slot, shared by both layers (`UseBumpOnBaseTexture2` is declared per the
    # exposed-parameter table but has no second slot to switch onto -- "the second layer is
    # sampled through the same tangent normal as the first", design doc "M_V2_TwoTexture") ----
    normal_tex = g.tex(P.Textures.NormalMap, -1100, 60, kind="normal")
    connect(base_uv, "", normal_tex, "UVs")
    flat_normal = g.const3(0.0, 0.0, 1.0, -700, 140)
    normal_final = g.switch(P.Switches.UseNormalMap, g.mask(normal_tex, "rgb", -700, 60),
                            flat_normal, -500, 100, default=False)
    g.to(normal_final, "", unreal.MaterialProperty.MP_NORMAL)
    g.switch(P.Switches.UseBumpOnBaseTexture2, g.const(1.0, -300, 60), g.const(0.0, -300, 140),
            -100, 100, default=False)  # declared, not wired -- see the docstring above.

    # -- surface class lookup, unwired past declaration (this master's Specular/Roughness/
    # Metallic follow the same class-LUT-alone convention M_V2_Lit's non-$envmap branch already
    # uses; see the module docstring's doc-gap note #1) ---------------------------------------
    class_roughness, class_specular, class_metallic = _class_lut(
        g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)
    g.to(class_roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to(class_specular, "", unreal.MaterialProperty.MP_SPECULAR)
    g.to(class_metallic, "", unreal.MaterialProperty.MP_METALLIC)

    # -- Opacity: Alpha + AlphaBias (x VertexColor.a under UseVertexAlpha) -- see the module
    # docstring's doc-gap note #2 -----------------------------------------------------------
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    alpha_bias = g.scalar(P.Scalars.AlphaBias, 0.0, 1700, 80)
    alpha_with_sine = sine["apply"](alpha_param, "", "r", "r", 1900, 0)
    alpha_biased = g.add(alpha_with_sine, "", alpha_bias, "", 2100, 40)
    # `VertexColor`'s outputs are all unnamed FNames -- connect straight to its own "A" output
    # (already 1-wide) rather than through a ComponentMask; see M_V2_Lit's Opacity section.
    alpha_with_vc = g.mul(alpha_biased, "", vertex_color, "A", 2300, 100)
    opacity_final = g.switch(P.Switches.UseVertexAlpha, alpha_with_vc, alpha_biased, 2500, 80,
                             default=False)
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY)


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

    `VampireEyes` is a switch with a deliberately empty body (design doc, **named divergence**):
    the `Eyes_Vampire`/`Eyes_Vampire_Overbright2` programs (12 materials) ship compiled-only, with
    no readable `.psh` and no other family's source revealing what they do, so the graph behind the
    switch is identical to the non-vampire path pending decompilation of `psh/eyes_vampire*`.

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
    g.to(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # -- surface class lookup: Eyes has no $envmap lane at all (not in the design's exposed-
    # parameter table for this master), so Roughness/Specular/Metallic are always the class row --
    class_roughness, class_specular, class_metallic = _class_lut(
        g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)
    g.to(class_roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to(class_specular, "", unreal.MaterialProperty.MP_SPECULAR)
    g.to(class_metallic, "", unreal.MaterialProperty.MP_METALLIC)

    # -- Emissive: Glint is additive, gated by UseGlint. Set by no shipped material (406/406 off);
    # the slot exists for completeness (design doc "M_V2_Eyes") ---------------------------------
    glint_rgb = g.mask(glint_tex, "rgb", -900, 100)
    black3 = g.const3(0.0, 0.0, 0.0, -900, 180)
    glint_emissive = g.switch(P.Switches.UseGlint, glint_rgb, black3, -700, 140, default=False)
    g.to(glint_emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # `IrisFrame`/`VampireEyes` are declared per the exposed-parameter table but have nothing in
    # the corpus to wire (see the function docstring) -- declared, not wired.
    g.scalar(P.Scalars.IrisFrame, 0.0, -1100, 300)
    g.switch(P.Switches.VampireEyes, g.const(1.0, -900, 340), g.const(0.0, -900, 420),
            -700, 380, default=False)

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


def _build_water(mat, collection, lut_texture, default_frames):
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
    - `MP_REFRACTION` is a flat `1.0` (`M_Refract`'s "1.0 is neutral" convention) -- `RefractAmount`
      does not drive the refraction *magnitude* pin at all. It drives the `DuDvMap` perturbation
      above instead, so refraction strength and the DuDv ripple share one coherent knob rather than
      splitting across two overlapping pins. `CheapWater` "drops the refraction pass": it zeroes
      that perturbation outright (a `Switch` ahead of the `Add`), leaving the plain `NormalMap` (or
      flat) normal and no distortion.
    - `BaseReflectFract` feeds the Fresnel node; its output scales `ReflectAmount/100` into
      `MP_SPECULAR` (gated `UseEnvMap`, mirroring every other master's envmap-gated specular
      branch) rather than a literal reflection-image blend, since Lumen already supplies the
      reflection image once Specular/Roughness are physically plausible. `ReflectTint`'s luma
      scales that same specular term, the same "grey tint scales Specular" shape `M_V2_Lit`'s own
      reflection contract uses for a non-chromatic `$envmaptint`.
    - `WaterColor`/`WaterMurkiness`/`RefractTint`/`Color` combine into BaseColor: `BaseTexture ×
      Color × RefractTint` (the un-murky look, `RefractTint` transcribing `waterrefract.psh`'s
      `mul r0, t2, c1`) `Lerp`'d toward the flat `WaterColor` by `WaterMurkiness` -- an exact no-op
      at the shipped default (`WaterMurkiness` 0.0), same shape as every other neutral-by-default
      lane in this file.
    - `UseFogEnable`/`FogColor`/`FogStart`/`FogEnd` and the wave-animation scalars
      (`WaterBaseFactor`, `WaterBaseMovementDist/Freq`, `WaterTimeFreq1/2`, `WaterWaveHeight/
      Length`, `WaterSpecularMin/Max`, `CheapWaterStartDistance/EndDistance`, `WaterDepth`) are
      **declared, not wired**: the design's post-lighting section states no pixel-graph formula for
      any of them (fog is a distance/height effect and the wave terms are vertex/World-Position-
      Offset concerns, both out of this generator's scope, the same way the sprite lane owns
      `$spriteorigin`), and wiring an invented formula for a knob the design never states one for
      would be a guess dressed as a transcription. Flagged here rather than silently absent.
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
    base_selected = g.switch(P.Switches.UseBaseTexture, base_tex_rgb, white3, -700, -360,
                             default=True)
    base_a_selected = g.switch(P.Switches.UseBaseTexture, base_tex_a, g.const(1.0, -900, -160),
                               -700, -200, default=True)

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
        P.Switches.UseAnimatedNormalFrames, default_frames, -1100, 340, sampler="linear")
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
    g.to(g.const(1.0, -300, 760), "", unreal.MaterialProperty.MP_REFRACTION)

    # -- surface class lookup + reflection: BaseReflectFract -> Fresnel -> ReflectAmount/
    # ReflectTint into Specular, gated UseEnvMap (mirrors every other master's envmap branch) ---
    class_roughness, class_specular, class_metallic = _class_lut(
        g, P.Textures.SurfaceClassLUT, lut_texture, P.Scalars.SurfaceClassIndex, -1900, 1400)

    base_reflect_fract = g.scalar(P.Scalars.BaseReflectFract, 0.2, -1900, 1900)
    fresnel_node = g.node(unreal.MaterialExpressionFresnel, -1700, 1900)
    fresnel_node.set_editor_property("exponent_in", 5.0)
    connect(base_reflect_fract, "", fresnel_node, "BaseReflectFraction")

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
    reflect_dir = g.reflection_ws(-1900, 2400)
    envcube = g.cube(P.Textures.EnvMap, -1700, 2400, default=DEFAULT_CUBE)
    connect(reflect_dir, "", envcube, "UVs")
    fixed_raw = g.mul(g.mul(envcube, "RGB", env_tint, "", -1500, 2400), "", reflect_tint, "",
                      -1300, 2440)
    lumen_safe_fixed = g.node(unreal.MaterialExpressionRayTracingQualitySwitch, -1100, 2400)
    connect(fixed_raw, "", lumen_safe_fixed, "Normal")
    connect(g.const3(0.0, 0.0, 0.0, -1100, 2480), "", lumen_safe_fixed, "RayTraced")
    fixed_emissive = g.switch(P.Switches.UseFixedCube, lumen_safe_fixed,
                              g.const3(0.0, 0.0, 0.0, -900, 2400), -900, 2360, default=False)
    g.to(fixed_emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # -- Opacity: Alpha x BaseTexture.a (translucent, no vertex-color/alpha lane on this master) -
    alpha_param = g.scalar(P.Scalars.Alpha, 1.0, 1700, 0)
    opacity = g.mul(alpha_param, "", base_a_selected, "", 1900, 0)
    g.to(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    # -- Declared, not wired -- see the function docstring's last bullet ------------------------
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
    g.scalar(P.Scalars.FogStart, 1.0, -1900, 3960)
    g.scalar(P.Scalars.FogEnd, 400.0, -1900, 4040)
    g.vec4(P.Vectors.FogColor, (0.0, 0.0, 0.0, 0.0), -1900, 4120)
    g.switch(P.Switches.UseFogEnable, g.const(1.0, -1700, 4200), g.const(0.0, -1700, 4280),
            -1500, 4240, default=False)


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

    default_frames = _make_default_frames_array()

    mat, asset = _fresh(name, ism=True, nanite=True)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property(
        "translucency_lighting_mode",
        unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mat.set_editor_property("refraction_method", unreal.RefractionMode.RM_PIXEL_NORMAL_OFFSET)
    mat.set_editor_property("two_sided", False)

    _build_water(mat, collection, lut_texture, default_frames)

    errors = mel.recompile_material(mat)
    if errors:
        _fail("%s failed to compile:\n%s" % (asset, "\n".join(errors)))

    bl.stamp_recipe(mat, fingerprint)
    if not bl.save(asset):
        _fail("save failed: %s" % asset)
    unreal.log("[make_v2_materials] saved %s" % asset)
    return mat


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
