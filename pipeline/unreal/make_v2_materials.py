# SF-4.3: the V2 material-import masters, built on the graph-authoring helper layer in
# `matgraph.py`. Design: docs/architecture/seam_map_material.md -> "Import" (master inventory,
# exposed-parameter table, post-lighting math, reflection contract, knob contract, class
# fallback). Build mechanics: import/design/phase4_mechanics.md section 3.
#
# Part 2 of SF-4.3 (commit series): the previous commit reconciled `M_V2_Lit` to the revised
# design (2026-08-31 "Revise the material import design after review" -- binding contract,
# defaults, class fallback); this commit adds `M_V2_LitTranslucent`, the same shading graph
# (`_build_lit`) under a different material-domain/blend-mode/translucency-lighting-mode property
# set -- design doc "Master inventory": blend mode, two-sidedness and the opacity clip value are
# per-instance overrides, not per-master, so the two masters share every graph pin.
# `M_V2_Unlit` and `M_V2_TwoTexture` land in the two commits that follow. Part 1 (`9daae29b`)
# authored `M_V2_Lit` against the pre-revision design; the header notes it left below (Clamp's
# unnamed primary pin, `connect_material_property`'s three-argument signature,
# `MaterialExpressionSine.Period` not being connectable, and the absent `MP_PIXEL_DEPTH_OFFSET`)
# are still the facts this file builds against.
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
