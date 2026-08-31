# SF-4.3 (part 1): the first V2 material-import master, `M_V2_Lit`, built on the graph-authoring
# helper layer in `matgraph.py`. Design: docs/architecture/seam_map_material.md -> "Import"
# (master inventory, exposed-parameter table, post-lighting math, reflection contract, knob
# contract). Build mechanics: import/design/phase4_mechanics.md section 3.
#
# Only M_V2_Lit is authored here. `build_lit_graph` is written to be reusable by
# `M_V2_LitTranslucent` (the same shading graph under a different material-domain/blend-mode
# property set -- see the design's "Master inventory" table) once that master lands; the other
# seven V2 families are one function each, added family-by-family in later slices, per the
# mechanics doc's commit-ordering note.
#
# --- Two contract decisions this file makes, both flagged for the owner --------------------
#
# 1. Static-switch naming/count. `phase4_mechanics.md` section 3d says "Static switches <= 4 per
#    master, named `bUse...`". The design's own exposed-parameter table
#    (seam_map_material.md -> "Exposed parameters, by master") lists FIFTEEN static switches for
#    M_V2_Lit, none of them `b`-prefixed (`UseBaseTexture`, not `bUseBaseTexture`; `MetallicTint`
#    and `IsDecalSurface` don't even carry `Use`). The task brief that assigned this work states
#    the design table's names are "the contract" -- and the "risks" section elsewhere in the same
#    mechanics doc reads as a cap on usage-flag x switch PERMUTATION testing scope, not a hard
#    ceiling on a master's switch count. This file follows the design table verbatim: fifteen
#    switches, spelled exactly as the table spells them. If the four-switch reading was meant
#    literally, that is a design gap for the owner to resolve, not something guessed at here.
#
# 2. `EnvMap`'s type. The design's exposed-parameter table lists `EnvMap` once, under "Textures",
#    with no cube/2D distinction. The reflection contract's fixed-cube term
#    ("Emissive += TextureSampleParameterCube(EnvMap, reflect(V, N)) * mask * EnvMapTint *
#    FixedCubeStrength") needs a TextureCube. `phase4_mechanics.md`'s illustrative stub-row table
#    proposes a DIFFERENT name, `EnvCube`, for exactly this binding. Since the design table (the
#    named contract) has only `EnvMap` and no `EnvCube`, this file binds one
#    `TextureSampleParameterCube` named `EnvMap` rather than inventing a second parameter the
#    stage (SF-4.4) would have nothing to write into. Flagged for the owner in case `EnvCube` was
#    meant to ship as a second, separate parameter.
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
from elysium_pipeline.paths import export_v2_root

connect = matgraph.connect

PKG = mounts.MATERIALS_V2

mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

DEFAULT_TEX = "/Engine/EngineResources/DefaultTexture.DefaultTexture"
DEFAULT_NORMAL = "/Engine/EngineMaterials/DefaultNormal.DefaultNormal"
DEFAULT_CUBE = "/Engine/EngineResources/DefaultTextureCube.DefaultTextureCube"

#: Bump the graph shape (node topology / algebra), not the parameter defaults, and a re-run picks
#: it up through the recipe stamp even when nothing on disk changed.
GRAPH_VERSION = 1

#: The class LUT is a 64-row, 1-tall texture; SF-4.1's UElysiumSurfaceCalibration regenerates it
#: for real once it lands. This generator only needs it to exist so SurfaceClassLUT binds
#: something real instead of nothing (see `_make_class_lut_placeholder`).
CLASS_LUT_ROWS = 64

#: `MPC_ElysiumSurfaces` (SF-4.1, C++, not yet landed) -- the knob-contract's "settings scalar"
#: rows (docs/architecture/seam_map_material.md -> "Knob contract"). Created here so the master
#: can read them today; `UElysiumSurfaceSettings::PushToCollection` will own their *values* once
#: it lands (it only ever writes `ScalarParameters[i].DefaultValue` on rows that already exist --
#: see phase4_mechanics.md section 1a -- so this generator owns row creation, never clobbers a
#: tuned default on rerun).
SURFACES_COLLECTION = "MPC_ElysiumSurfaces"
SURFACES_KNOBS = {
    "DefaultSpecular": 0.5,
    "DefaultRoughness": 0.6,
    "DefaultMetallic": 0.0,
    "MaskRoughnessMin": 0.08,
    "MaskRoughnessMax": 0.9,
    "MaskSpecularScale": 1.0,
    "EnvTintScale": 1.0,
    "FixedCubeStrength": 1.0,
    "Overbright": 1.0,
    "DecalDepthOffset": 0.0,
    "LightSpecularScale": 1.0,
    "CaptureRadius": 1500.0,
}

# The shader-source units the Lit post-lighting math transcribes (design doc "Post-lighting math
# per master" -> M_V2_Lit / two passes). Their sha256 goes into the recipe so a shader-source
# GLB revision -- a corrected instruction dump, say -- invalidates the stamp even though nothing
# in this file changed.
CITED_SHADER_UNITS = [
    "lightmappedgeneric",
    "lightmappedgeneric_selfilluminated",
    "vertexlitgeneric_envmappedbumpmapv2",
]


def _fail(msg):
    unreal.log_error("[make_v2_materials] %s" % msg)
    raise SystemExit(1)


def _fresh(name):
    """Delete + recreate the asset so a re-run authors a clean graph (idempotent)."""
    asset = "%s/%s" % (PKG, name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset):
        unreal.EditorAssetLibrary.delete_asset(asset)
    mat = tools.create_asset(name, PKG, unreal.Material, unreal.MaterialFactoryNew())
    if not mat:
        _fail("create_asset failed: %s" % asset)
    # Static world props render as ISMs/Nanite; NPC sections (teeth, cable) as skeletal meshes.
    # Without the matching usage flag UE compiles no permutation outside the editor and the
    # affected primitives fall back to the default grey material in a packaged build.
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    mat.set_editor_property("used_with_nanite", True)
    mat.set_editor_property("used_with_skeletal_mesh", True)
    return mat, asset


def _png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(
        ">I", zlib.crc32(kind + data) & 0xffffffff)


def _make_linear_white_mask():
    """A real linear 1x1 white mask -- the same trick `make_world_materials.py` uses, kept as its
    own V2-package copy rather than a cross-package reference (the V2 masters must not depend on
    the legacy `make_*_materials.py` set)."""
    asset = "%s/T_LinearWhiteMask" % PKG
    existing = unreal.load_asset(asset)
    if existing:
        return existing
    png = (b"\x89PNG\r\n\x1a\n"
           + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 0, 0, 0, 0))
           + _png_chunk(b"IDAT", zlib.compress(b"\x00\xff"))
           + _png_chunk(b"IEND", b""))
    source = Path(export_v2_root()).parent / "exports" / ".policy" / "v2_linear_white_mask.png"
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_bytes(png)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", PKG)
    task.set_editor_property("destination_name", "T_LinearWhiteMask")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(asset)
    if not texture:
        _fail("could not author T_LinearWhiteMask")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property(
        "compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    texture.set_editor_property(
        "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False)
    return texture


def _make_class_lut_placeholder():
    """A real 64x1 placeholder for `T_SurfaceClassLUT`, imported from a generated PNG rather than
    authored through `FTextureSource` (C++-only, per phase4_mechanics.md section 0) -- the same
    approach `make_world_materials.make_linear_white_mask` uses for its own single-pixel texture,
    which proves `unreal.AssetToolsHelpers` + `AssetImportTask` CAN author a texture headlessly.
    Every texel carries the calibration table's own default row (Roughness 0.6, Specular 0.5,
    Metallic 0.0 -- phase4_mechanics.md section 1b's `FElysiumSurfaceClassRow` defaults) until
    `UElysiumSurfaceCalibration::RegenerateLut` (SF-4.1, not yet landed) overwrites this same
    asset path with the real per-class table. TODO(SF-4.1): once UElysiumSurfaceCalibration
    lands, its RegenerateLut is the sole writer of this asset; this placeholder only exists so
    SurfaceClassLUT has something real to sample in the meantime."""
    asset = "%s/T_SurfaceClassLUT" % PKG
    existing = unreal.load_asset(asset)
    if existing:
        return existing
    r, g, b, a = int(round(0.6 * 255)), int(round(0.5 * 255)), 0, 255
    scanline = b"\x00" + bytes([r, g, b, a]) * CLASS_LUT_ROWS  # filter type 0 (None)
    png = (b"\x89PNG\r\n\x1a\n"
           + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", CLASS_LUT_ROWS, 1, 8, 6, 0, 0, 0))
           + _png_chunk(b"IDAT", zlib.compress(scanline))
           + _png_chunk(b"IEND", b""))
    source = Path(export_v2_root()).parent / "exports" / ".policy" / "surface_class_lut_placeholder.png"
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_bytes(png)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", PKG)
    task.set_editor_property("destination_name", "T_SurfaceClassLUT")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(asset)
    if not texture:
        unreal.log_warning(
            "[make_v2_materials] could not author T_SurfaceClassLUT placeholder; "
            "SurfaceClassLUT binds no default texture until SF-4.1 lands")
        return None
    texture.set_editor_property("srgb", False)
    texture.set_editor_property(
        "compression_settings", unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    texture.set_editor_property(
        "mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property(
        "lossy_compression_amount", unreal.TextureLossyCompressionAmount.TLCA_NONE)
    unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False)
    return texture


def make_surfaces_collection():
    """Create/extend `MPC_ElysiumSurfaces`, adding only rows this run's masters need and never
    touching a row that already exists -- SF-4.1's `PushToCollection` is the values' owner once
    it lands; this generator only owns row *existence*."""
    asset = "%s/%s" % (PKG, SURFACES_COLLECTION)
    collection = unreal.load_asset(asset)
    if not collection:
        collection = tools.create_asset(
            SURFACES_COLLECTION, PKG, unreal.MaterialParameterCollection,
            unreal.MaterialParameterCollectionFactoryNew())
    if not collection:
        _fail("could not create %s" % asset)
    existing = list(collection.get_editor_property("scalar_parameters"))
    have = {p.get_editor_property("parameter_name") for p in existing}
    missing = [name for name in SURFACES_KNOBS if name not in have]
    if missing:
        for name in missing:
            parameter = unreal.CollectionScalarParameter()
            parameter.set_editor_property("parameter_name", name)
            parameter.set_editor_property("default_value", SURFACES_KNOBS[name])
            existing.append(parameter)
        collection.set_editor_property("scalar_parameters", existing)
        if not unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False):
            _fail("could not save %s" % asset)
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


def _cited_unit_hashes():
    hashes = {}
    for stem in CITED_SHADER_UNITS:
        sha = _unit_sha256(stem)
        hashes[stem] = sha if sha else "unavailable"
    return hashes


def _build(mat, collection, lut_texture):
    """The shared M_V2_Lit / M_V2_LitTranslucent shading graph: BaseColor, Normal, Emissive,
    Roughness, Specular, Metallic, Opacity, OpacityMask. `M_V2_LitTranslucent` (not authored yet)
    calls this same function -- it differs from `M_V2_Lit` only in material domain/blend/
    translucency-lighting-mode, which the caller sets on `mat` before/after calling this, since
    the shading algebra itself does not change (design doc "Master inventory": blend mode,
    two-sidedness and the opacity clip value are per-INSTANCE overrides, not per-master)."""
    g = Graph(mat, collection=collection)

    # -- shared UV: TexScaleOffset transform, then an optional Panner ------------------------
    uv0 = g.node(unreal.MaterialExpressionTextureCoordinate, -1900, 0)
    # A full float4 vector (scale.xy, offset.xy) -- `Graph.vec3` masks to RGB and would drop the
    # w channel this needs, so it is authored directly rather than through the helper.
    tex_scale_offset = g.node(unreal.MaterialExpressionVectorParameter, -1900, 200)
    tex_scale_offset.set_editor_property("parameter_name", "TexScaleOffset")
    tex_scale_offset.set_editor_property(
        "default_value", unreal.LinearColor(1.0, 1.0, 0.0, 0.0))
    scale = g.mask(tex_scale_offset, "rg", -1700, 160)
    offset = g.mask(tex_scale_offset, "ba", -1700, 260)
    scaled_uv = g.mul(uv0, "", scale, "", -1500, 0)
    transformed_uv = g.add(scaled_uv, "", offset, "", -1300, 40)

    time_node = g.time(-1900, 420)
    scroll_u = g.scalar(Params.Scalars.ScrollRateU, 0.0, -1900, 500)
    scroll_v = g.scalar(Params.Scalars.ScrollRateV, 0.0, -1900, 580)
    scroll_speed = g.append(scroll_u, "", scroll_v, "", -1700, 540)
    panned_uv = g.panner(time_node, scroll_speed, -1500, 460)
    connect(transformed_uv, "", panned_uv, "Coordinate")
    final_uv = g.switch(Params.Switches.UseScroll, panned_uv, transformed_uv, -1100, 200,
                        default=False)

    # -- neutral-by-construction sine modulation on Alpha (the dominant `sine` proxy target;
    # design doc "Proxy policy" -> `sine`) -- SineMin=SineMax=1 by default makes this an exact
    # no-op, so an instance the stage never touches renders exactly as if the term did not
    # exist, the same contract `mat_fog`'s Custom Primitive Data slots use. ------------------
    sine_min = g.scalar(Params.Scalars.SineMin, 1.0, -1900, 700)
    sine_max = g.scalar(Params.Scalars.SineMax, 1.0, -1900, 780)
    sine_period = g.scalar(Params.Scalars.SinePeriod, 1.0, -1900, 860)
    sine_offset = g.scalar(Params.Scalars.SineTimeOffset, 0.0, -1900, 940)
    phase_time = g.add(time_node, "", sine_offset, "", -1700, 900)
    # `MaterialExpressionSine.Period` is a plain float promoted through `ShowAsInputPin`
    # metadata, not a real `FExpressionInput` -- `connect_material_expressions` refuses it (UE
    # 5.8; confirmed against the real editor, not just the header). Dividing the phase by the
    # period ourselves and leaving `Period` at its own default (1.0) gets the identical
    # `sin(2*pi * phase / period)`, since Sine's Input pin is already in units of its Period.
    period_floor = g.const(0.0001, -1700, 1020)  # guards an authored SinePeriod of exactly 0
    period_ceiling = g.const(3.4e38, -1700, 1080)
    safe_period = g.clamp(sine_period, "", period_floor, "", period_ceiling, "", -1700, 1140)
    normalized_time = g.div(phase_time, "", safe_period, "", -1500, 900)
    wave = g.sine(normalized_time, "", -1300, 900)
    wave_norm = g.mul(g.add(wave, "", g.const(1.0, -1500, 980), "", -1300, 940), "",
                      g.const(0.5, -1300, 1020), "", -1100, 940)
    sine_range = g.sub(sine_max, "", sine_min, "", -1300, 780)
    sine_value = g.add(g.mul(wave_norm, "", sine_range, "", -900, 860), "", sine_min, "",
                       -700, 860)

    # -- BaseTexture ---------------------------------------------------------------------------
    base_tex = g.tex(Params.Textures.BaseTexture, -1100, -400, kind="color")
    connect(final_uv, "", base_tex, "UVs")
    base_tex_rgb = g.mask(base_tex, "rgb", -900, -420)
    base_tex_a = g.mask(base_tex, "a", -900, -340)
    white3 = g.const3(1.0, 1.0, 1.0, -900, -260)
    base_selected = g.switch(Params.Switches.UseBaseTexture, base_tex_rgb, white3, -700, -360,
                             default=True)

    detail_tex = g.tex(Params.Textures.Detail, -1100, -180, kind="color")
    connect(final_uv, "", detail_tex, "UVs")
    detail_scale = g.scalar(Params.Scalars.DetailScale, 0.0, -1100, -80)
    detail_mod2x = g.mul(base_selected, "", g.mul(detail_tex, "RGB", g.const(2.0, -900, -140),
                                                   "", -820, -160), "", -640, -260)
    detailed = g.lerp(base_selected, "", detail_mod2x, "", detail_scale, "", -440, -260)
    base_with_detail = g.switch(Params.Switches.UseDetail, detailed, base_selected, -240, -260,
                                default=False)

    color = g.vec3(Params.Vectors.Color, (1.0, 1.0, 1.0, 1.0), -1100, -560)
    tinted = g.mul(base_with_detail, "", color, "", -40, -360)
    vertex_color = g.vertex_color(-1100, -680)
    vc_rgb = g.mask(vertex_color, "rgb", -900, -680)
    with_vc = g.mul(tinted, "", vc_rgb, "", 160, -400)
    vc_selected = g.switch(Params.Switches.UseVertexColor, with_vc, tinted, 360, -360,
                           default=False)

    overbright = g.mpc("Overbright", -1100, -820)
    overbright_base = g.mul(vc_selected, "", overbright, "", 560, -360)

    # -- surface class lookup: ClassRoughness/ClassSpecular/ClassMetallic --------------------
    class_index = g.scalar(Params.Scalars.SurfaceClassIndex, 0.0, -1900, 1400)
    lut_object = g.tex_object("SurfaceClassLUT", -1900, 1480, lut_texture)
    lut_uv = class_lut_uv(g, class_index, -1700, 1400)
    lut_sample = read_class_lut(g, lut_object, lut_uv, -1300, 1400)
    class_roughness = g.mask(lut_sample, "r", -1100, 1360)
    class_specular = g.mask(lut_sample, "g", -1100, 1400)
    class_metallic = g.mask(lut_sample, "b", -1100, 1440)

    # -- reflection contract: mask term (design doc "Reflection contract") -------------------
    normal_tex = g.tex(Params.Textures.NormalMap, -1100, 60, kind="normal")
    connect(final_uv, "", normal_tex, "UVs")
    normal_tex_rgb = g.mask(normal_tex, "rgb", -900, 60)
    normal_tex_a = g.mask(normal_tex, "a", -900, 140)
    envmapmask_tex = g.tex(Params.Textures.EnvMapMask, -1100, 260, kind="mask",
                           default="%s/T_LinearWhiteMask" % PKG)
    connect(final_uv, "", envmapmask_tex, "UVs")
    luma_weights = g.const3(0.299, 0.587, 0.114, -900, 300)
    envmapmask_luma = g.dot(envmapmask_tex, "RGB", luma_weights, "", -700, 300)
    mask_none = g.const(1.0, -900, 380)
    mask_basealpha = g.one_minus(base_tex, "A", -700, 380)
    mask_step1 = g.switch(Params.Switches.UseBaseAlphaEnvMapMask, mask_basealpha, mask_none,
                          -500, 380, default=False)
    mask_step2 = g.switch(Params.Switches.UseNormalMapAlphaEnvMapMask, normal_tex_a, mask_step1,
                          -300, 340, default=False)
    mask_step3 = g.switch(Params.Switches.UseEnvMapMask, envmapmask_luma, mask_step2, -100, 340,
                          default=False)
    env_mask_scale = g.scalar(Params.Scalars.EnvMapMaskScale, 1.0, -900, 460)
    mask_scaled = g.mul(mask_step3, "", env_mask_scale, "", 100, 380)
    mask_sat = g.sat(mask_scaled, "", 300, 380)

    # -- specular / roughness / metallic -------------------------------------------------------
    mask_spec_scale = g.mpc("MaskSpecularScale", -1900, 1560)
    mask_rough_min = g.mpc("MaskRoughnessMin", -1900, 1640)
    mask_rough_max = g.mpc("MaskRoughnessMax", -1900, 1720)
    env_tint_scale = g.mpc("EnvTintScale", -1900, 1800)
    env_tint = g.vec3(Params.Vectors.EnvMapTint, (1.0, 1.0, 1.0, 1.0), -1900, 1880)
    tint_luma = g.dot(env_tint, "", luma_weights, "", -1700, 1880)

    spec_common = g.mul(class_specular, "",
                        g.add(g.const(1.0, 500, 1560), "",
                              g.mul(mask_spec_scale, "", mask_sat, "", 500, 1620), "",
                              700, 1600), "", 900, 1560)
    spec_grey = g.mul(spec_common, "", g.mul(tint_luma, "", env_tint_scale, "", 900, 1720),
                      "", 1100, 1680)
    spec_envmap = g.switch(Params.Switches.MetallicTint, spec_common, spec_grey, 1300, 1600,
                           default=False)
    specular_final = g.switch(Params.Switches.UseEnvMap, spec_envmap, class_specular, 1500, 1500,
                              default=False)

    rough_reflective = g.lerp(mask_rough_max, "", mask_rough_min, "", mask_sat, "", 900, 1760)
    roughness_final = g.switch(Params.Switches.UseEnvMap, rough_reflective, class_roughness,
                               1500, 1760, default=False)

    metallic_envmap = g.switch(Params.Switches.MetallicTint, mask_sat, class_metallic, 1300, 1840,
                               default=False)
    metallic_final = g.switch(Params.Switches.UseEnvMap, metallic_envmap, class_metallic,
                              1500, 1840, default=False)

    # -- BaseColor: chromatic $envmaptint multiplies BaseColor; grey does not (it only scales
    # Specular, above) -------------------------------------------------------------------------
    base_tinted_chromatic = g.mul(overbright_base, "", env_tint, "", 900, -280)
    base_color_envmap = g.switch(Params.Switches.MetallicTint, base_tinted_chromatic,
                                 overbright_base, 1100, -280, default=False)
    base_color_final = g.switch(Params.Switches.UseEnvMap, base_color_envmap, overbright_base,
                                1300, -280, default=False)
    g.to(base_color_final, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # -- Normal ---------------------------------------------------------------------------------
    flat_normal = g.const3(0.0, 0.0, 1.0, -700, 140)
    bump_scale = g.scalar(Params.Scalars.BumpScale, 1.0, -700, 220)
    normal_blended = g.lerp(flat_normal, "", normal_tex_rgb, "", bump_scale, "", -500, 180)
    normal_final = g.switch(Params.Switches.UseNormalMap, normal_blended, flat_normal, -700, 180,
                            default=False)
    g.to(normal_final, "", unreal.MaterialProperty.MP_NORMAL)

    # -- Emissive: self-illum + fixed-cube reflection (RayTracingQualitySwitch-gated) ---------
    selfillum_amount = g.scalar(Params.Scalars.SelfIllumAmount, 0.0, -1100, 620)
    selfillum_tint = g.vec3(Params.Vectors.SelfIllumTint, (1.0, 1.0, 1.0, 1.0), -1100, 700)
    selfillum_raw = g.mul(g.mul(base_tex, "RGB", selfillum_tint, "", -900, 660), "",
                          g.mul(base_tex, "A", selfillum_amount, "", -900, 740), "", -700, 700)
    black3 = g.const3(0.0, 0.0, 0.0, -900, 820)
    selfillum_emissive = g.switch(Params.Switches.UseSelfIllum, selfillum_raw, black3, -500, 740,
                                  default=False)

    reflect_dir = g.reflection_ws(-1900, 2000)
    envcube = g.cube(Params.Textures.EnvMap, -1700, 2000, default=DEFAULT_CUBE)
    connect(reflect_dir, "", envcube, "UVs")
    fixed_cube_strength = g.mpc("FixedCubeStrength", -1900, 2100)
    fixed_raw = g.mul(g.mul(envcube, "RGB", mask_sat, "", -1500, 2040), "",
                      g.mul(env_tint, "", fixed_cube_strength, "", -1500, 2120), "", -1300, 2080)
    lumen_safe_fixed = g.node(unreal.MaterialExpressionRayTracingQualitySwitch, -1100, 2000)
    connect(fixed_raw, "", lumen_safe_fixed, "Normal")
    connect(black3, "", lumen_safe_fixed, "RayTraced")
    fixed_emissive = g.switch(Params.Switches.UseFixedCube, lumen_safe_fixed, black3, -900, 2000,
                              default=False)

    total_emissive = g.add(selfillum_emissive, "", fixed_emissive, "", -300, 780)
    g.to(total_emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.to(specular_final, "", unreal.MaterialProperty.MP_SPECULAR)
    g.to(roughness_final, "", unreal.MaterialProperty.MP_ROUGHNESS)
    g.to(metallic_final, "", unreal.MaterialProperty.MP_METALLIC)

    # -- Opacity / OpacityMask: harmless on the Opaque default, load-bearing once a per-instance
    # BasePropertyOverride switches an MI_ to Masked/Translucent (design doc "Master inventory")
    alpha_param = g.scalar(Params.Scalars.Alpha, 1.0, 1700, 0)
    alpha_with_sine = g.mul(alpha_param, "", sine_value, "", 1900, 0)
    vc_alpha = g.mask(vertex_color, "a", 1700, 80)
    alpha_with_vc = g.mul(alpha_with_sine, "", vc_alpha, "", 2100, 40)
    opacity_final = g.switch(Params.Switches.UseVertexAlpha, alpha_with_vc, alpha_with_sine,
                             2300, 40, default=False)
    g.to(opacity_final, "", unreal.MaterialProperty.MP_OPACITY)

    one_const = g.const(1.0, 1700, 200)
    opacity_mask = g.switch(Params.Switches.UseSelfIllum, one_const, base_tex_a, 1900, 200,
                            default=False)
    g.to(opacity_mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    # -- Decal depth bias: $decal surfaces only (design doc knob contract: DecalDepthOffset) --
    # `unreal.MaterialProperty` in this engine build exposes no `MP_PIXEL_DEPTH_OFFSET` (checked
    # against the real editor's `dir(unreal.MaterialProperty)`, not assumed) -- Python's material
    # graph API has no pin for it here, so DecalDepthOffset/IsDecalSurface are declared (the
    # Policy test resolves them) but not wired to anything, same as the block below.
    g.scalar(Params.Scalars.DecalDepthOffset, 0.0, 1700, 320)
    zero = g.const(0.0, 1700, 400)
    g.switch(Params.Switches.IsDecalSurface, g.const(1.0, 1700, 440), zero, 1900, 360,
            default=False)

    # -- Declared-but-not-yet-wired parameters (contract-complete, behaviour deferred) --------
    # MinLight/MaxLight: no Lit formula anywhere in the design doc (Lumen owns lighting, and the
    # doc's post-lighting-math section never mentions either key for this master) -- exposed so
    # the stage can bind them and the Policy test resolves them, wired to nothing.
    g.scalar(Params.Scalars.MinLight, 0.0, 1700, 480)
    g.scalar(Params.Scalars.MaxLight, 1.0, 1700, 560)
    # WetnessScale: listed on M_V2_Lit's exposed-parameter row but never given a formula (the
    # World masters' own WetnessScale, `make_world_materials.py`, is a different, non-portable
    # graph); exposed only.
    g.scalar(Params.Scalars.WetnessScale, 0.0, 1700, 640)
    # FrameRate/UseAnimatedFrames: the `animatedtexture` proxy's flipbook needs a
    # Texture2DArray-sampling path this slice does not build (BaseTexture stays a single frame);
    # exposed so the parameter contract holds, animation deferred to the family's next slice.
    g.scalar(Params.Scalars.FrameRate, 0.0, 1700, 720)
    g.switch(Params.Switches.UseAnimatedFrames, one_const, zero, 1700, 800, default=False)


class Params:
    """Re-exports `ElysiumSurfaceParams.h`'s FNames as plain strings for the Python side. Kept as
    a nested shim (rather than parsing the header) because Python authors the graph and C++ reads
    it back; both sides are checked against the same design-doc table by
    `Elysium.Policy.V2MasterParams`, not by sharing a source file."""

    class Textures:
        BaseTexture = "BaseTexture"
        Detail = "Detail"
        NormalMap = "NormalMap"
        EnvMapMask = "EnvMapMask"
        EnvMap = "EnvMap"

    class Scalars:
        Alpha = "Alpha"
        SelfIllumAmount = "SelfIllumAmount"
        DetailScale = "DetailScale"
        EnvMapMaskScale = "EnvMapMaskScale"
        BumpScale = "BumpScale"
        MinLight = "MinLight"
        MaxLight = "MaxLight"
        FrameRate = "FrameRate"
        ScrollRateU = "ScrollRateU"
        ScrollRateV = "ScrollRateV"
        SineMin = "SineMin"
        SineMax = "SineMax"
        SinePeriod = "SinePeriod"
        SineTimeOffset = "SineTimeOffset"
        WetnessScale = "WetnessScale"
        DecalDepthOffset = "DecalDepthOffset"
        SurfaceClassIndex = "SurfaceClassIndex"

    class Vectors:
        Color = "Color"
        SelfIllumTint = "SelfIllumTint"
        EnvMapTint = "EnvMapTint"
        TexScaleOffset = "TexScaleOffset"

    class Switches:
        UseBaseTexture = "UseBaseTexture"
        UseDetail = "UseDetail"
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
        UseScroll = "UseScroll"
        IsDecalSurface = "IsDecalSurface"


PARAM_TABLE = {
    "textures": sorted(vars(Params.Textures)[k] for k in vars(Params.Textures) if not k.startswith("_")),
    "scalars": sorted(vars(Params.Scalars)[k] for k in vars(Params.Scalars) if not k.startswith("_")),
    "vectors": sorted(vars(Params.Vectors)[k] for k in vars(Params.Vectors) if not k.startswith("_")),
    "switches": sorted(vars(Params.Switches)[k] for k in vars(Params.Switches) if not k.startswith("_")),
}


def make_lit():
    name = "M_V2_Lit"
    asset = "%s/%s" % (PKG, name)
    recipe = {
        "graphVersion": GRAPH_VERSION,
        "citedUnits": _cited_unit_hashes(),
        "params": PARAM_TABLE,
        "mpcScalars": sorted(SURFACES_KNOBS),
    }
    fingerprint = bl.recipe_fingerprint("materials-v2", asset, recipe)
    force = _flag(_cmdline_arg("PolicyForce", ""))
    if not force and unreal.EditorAssetLibrary.does_asset_exist(asset) \
            and bl.stored_recipe(asset) == fingerprint:
        unreal.log("[make_v2_materials] %s up to date, skipping" % asset)
        return unreal.load_asset(asset)

    collection = make_surfaces_collection()
    lut_texture = _make_class_lut_placeholder()
    _make_linear_white_mask()

    mat, asset = _fresh(name)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property("two_sided", False)

    _build(mat, collection, lut_texture)

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
