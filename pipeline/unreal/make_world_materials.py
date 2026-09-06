# Generates the local world master-material set under Content/ElysiumGenerated/Materials/ (roadmap 7.4):
#
#   M_World_Opaque       opaque world surfaces (the common case; grown from the old M_VtMB_World)
#   M_World_Masked       $alphatest scissor surfaces (fences, grates, foliage) -- two-sided
#   M_World_Translucent  $translucent surfaces (glass, water film) -- alpha-blended, lit per-pixel
#   M_World_Glass        semantic lit/reflective glass -- Thin Translucent + Pixel Normal Offset
#   M_Refract            Source Refract overlay -- clear Thin Translucent + authored DUDV PNO
#   M_Additive           $additive glow overlays (light-fixture "on" panes, neon) -- unlit, additive
#
# The three lit world masters (Opaque/Masked/Translucent) share one surface graph built by
# `build_world_graph`; they differ only in domain/blend/opacity. Each master carries the full
# VtMB world feature set as named, runtime-bindable parameters (so the C++ factory binds by fixed
# name, no reflection probe):
#
#   Albedo      (tex)     base colour; RGB->BaseColor (after the WVT blend), A used by Masked/Translucent
#   Emissive    (tex)     $selfillum alpha-masked emission map (map_Ke); RGB x EmissiveScale
#   EmissiveScale (float) self-illum brightness, 0 by default (a surface with no map_Ke never glows)
#   BumpMap     (tex)     $bumpmap tangent-space normal; blended toward flat by BumpAmount
#   BumpAmount  (float)   0 by default (unbound surface stays geometrically smooth)
#   EnvMask     (tex)     linear $envmap reflectivity mask
#   SourceCube  (cube)    source cubemap used by the patch-authored wetness endpoint
#   EnvStrength (float)   0 by default (unbound surface stays matte at the calibrated 0.5 roughness)
#   BaseTex2    (tex)     WorldVertexTransition second albedo; lerp(Albedo, BaseTex2, VertexColor.r x BlendAmount)
#   BlendAmount (float)   0 by default (non-WVT surface ignores BaseTex2 and its vertex colour)
#
# All four also carry Source's distance fog as a PER-PRIMITIVE term read from Custom Primitive
# Data (`mat_fog`), because the world and the 3D-skybox miniature are fogged differently and
# share screen depth, which no engine-side fog mechanism can separate. Unwritten data is zero,
# which is "not fogged", so the term is neutral on any primitive nobody wrote to.
#
# General $envmap surfaces keep the existing PBR/Lumen response. A static switch on the same
# shared graph selects the patch-authored wetness endpoint for sm_hub_1: it samples SourceCube as
# an additive primary-view emissive term while a Ray Tracing Quality Switch keeps that
# camera-dependent colour out of Lumen surface-cache and ray-hit evaluation. Enhanced wetness
# remains a tunable second term.
#
# A UMaterial graph only compiles in the editor, so these are authored here and committed; the
# runtime only instances them (FElysiumMaterialFactory picks the master by blend flag and binds the
# parameters above). Rebuilt by the umbrella (uv run elysium export bundle policy -> pipeline/unreal/build_content.py, which the
# export runs); also runnable standalone:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="pipeline/unreal/make_world_materials.py" -unattended -nosplash -nopause
import os
from pathlib import Path
import struct
import sys
import zlib

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import mat_fog
from elysium_pipeline import mounts
from elysium_pipeline.paths import export_root

# A refused pin compiles anyway against the input's constant default, so a wrong output name
# becomes a wrongly-rendering material instead of a failed build. These raise instead.
connect = mat_fog.connect


def connect_property(src, src_out, prop):
    if not unreal.MaterialEditingLibrary.connect_material_property(src, src_out, prop):
        raise SystemExit("[make_world_materials] no connection %s.%s -> %s" % (
            src.get_class().get_name(), src_out or "<out>", prop))

PKG = mounts.MATERIALS
DEFAULT_TEX = "/Engine/EngineResources/DefaultTexture.DefaultTexture"
DEFAULT_NORMAL = "/Engine/EngineMaterials/DefaultNormal.DefaultNormal"
DEFAULT_CUBE = "/Engine/EngineResources/DefaultTextureCube.DefaultTextureCube"
# EnvMask needs a WHITE default, not DefaultTexture: 228 of the game's reflective materials
# carry $envmap with no $envmapmask and reflect uniformly, and nothing overwrites the sampler
# for them. DefaultTexture is a 128x128 greenish-grey noise image (mean RGB 122/140/131), so
# it would both dim and mottle exactly those surfaces. Matches the runtime factory, which
# binds a 1x1 white for the same case.
LINEAR_WHITE_MASK = "%s/T_LinearWhiteMask" % PKG

# The $envmap-mask -> Lumen reflection channel (roadmap 7.5). These are the master's parameter
# DEFAULTS; the bake binds a per-material value over them.
#
# The non-reflective end is Lambert -- ROUGH_BASE 1.0 / SPEC_BASE 0.0 -- because that is what
# VtMB's world is (docs/vtmb/lighting.md: METALLIC 0, SPECULAR 0, ROUGHNESS 1), and what the light
# rig already assumes when it sets specular_scale = 0 on every source. A surface reflects
# because its VMT carries $envmap, not by default.
ROUGH_BASE = 1.0
ROUGH_REFLECT = 0.15
SPEC_BASE = 0.0
SPEC_REFLECT = 0.5

# Dedicated Thin Translucent glass defaults. Pixel Normal Offset maps 1.0 to neutral and 2.0
# to full normal offset, so 1.08 produces the mild large-pane distortion the authored ripple
# calls for without sampling far outside the scene colour. The texture's alpha remains the
# exact whole-surface coverage; only its nearly-opaque texels become a painted top layer.
GLASS_REFRACTION = 1.08
GLASS_TINT_STRENGTH = 0.25
GLASS_FRAME_EXPONENT = 8.0

mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
# unreal.load_asset force-loads by object path (LoadObject); the asset-registry-backed
# EditorAssetLibrary.load_asset does not index /Engine/EngineMaterials in a commandlet, and its
# miss both leaves the sampler defaultless and trips the commandlet's error-exit.
_default_tex = unreal.load_asset(DEFAULT_TEX)
_default_normal = unreal.load_asset(DEFAULT_NORMAL)
_default_cube = unreal.load_asset(DEFAULT_CUBE)


def _png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(
        ">I", zlib.crc32(kind + data) & 0xffffffff)


def make_height_placeholder():
    """The tracked authored G16 default (Content/ElysiumAuthored/VFX) keeps the height sampler
    valid before any map instance overrides it -- loaded, never generated."""
    asset = "/Game/ElysiumAuthored/VFX/T_RainHeightPlaceholder"
    existing = unreal.load_asset(asset)
    if not existing:
        raise SystemExit(
            "[make_world_materials] tracked height placeholder is missing: %s" % asset)
    return existing


def make_linear_white_mask():
    """A real linear 1x1 mask for genuinely unmasked $envmap materials."""
    existing = unreal.load_asset(LINEAR_WHITE_MASK)
    if existing:
        existing.set_editor_property("srgb", False)
        existing.set_editor_property(
            "compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        existing.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        unreal.EditorAssetLibrary.save_asset(LINEAR_WHITE_MASK, only_if_is_dirty=False)
        return existing
    source = Path(export_root()) / ".policy" / "linear_white_mask.png"
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 0, 0, 0, 0))
        + _png_chunk(b"IDAT", zlib.compress(b"\x00\xff"))
        + _png_chunk(b"IEND", b"")
    )
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", PKG)
    task.set_editor_property("destination_name", "T_LinearWhiteMask")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    tools.import_asset_tasks([task])
    texture = unreal.load_asset(LINEAR_WHITE_MASK)
    if not texture:
        raise SystemExit("[make_world_materials] could not import linear white mask")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    unreal.EditorAssetLibrary.save_asset(LINEAR_WHITE_MASK, only_if_is_dirty=False)
    return texture


_height_placeholder = make_height_placeholder()
_linear_white_mask = make_linear_white_mask()


def make_environment_collection():
    asset = "%s/MPC_ElysiumEnvironment" % PKG
    collection = unreal.load_asset(asset)
    if not collection:
        collection = tools.create_asset(
            "MPC_ElysiumEnvironment", PKG, unreal.MaterialParameterCollection,
            unreal.MaterialParameterCollectionFactoryNew())
    if not collection:
        raise SystemExit("[make_world_materials] could not create %s" % asset)
    values = {
        "GlobalWetness": 0.0,
        "WetnessOutputScale": 1.0,
        "RainEnhancement": 0.0,
        "RainWetDarken": 0.06,
        "RainWetRoughness": 0.10,
        "RainLightResponse": 0.25,
        "RainSourceRetain": 1.0,
        "RainWetSpecular": 0.50,
        "RainReflectionDebug": 0.0,
    }
    # Update existing rows in place rather than replacing the array: every
    # `FCollectionScalarParameter` carries the GUID a `CollectionParameter` node binds to, and a
    # fresh struct is a fresh GUID. Replacing the array on every run left every master built
    # before it (M_V2_Lit's wetness nodes) failing to compile with "CollectionParameter has
    # invalid parameter None" until the masters were rebuilt by hand.
    existing = {str(row.get_editor_property("parameter_name")): row
                for row in (collection.get_editor_property("scalar_parameters") or [])}
    parameters = []
    for name, default in values.items():
        parameter = existing.get(name)
        if parameter is None:
            parameter = unreal.CollectionScalarParameter()
            parameter.set_editor_property("parameter_name", name)
        parameter.set_editor_property("default_value", default)
        parameters.append(parameter)
    collection.set_editor_property("scalar_parameters", parameters)
    if not unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False):
        raise SystemExit("[make_world_materials] could not save %s" % asset)
    return collection


environment_collection = make_environment_collection()


def _fresh(name):
    """Delete + recreate the asset so a re-run authors a clean graph (idempotent)."""
    asset = "%s/%s" % (PKG, name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset):
        unreal.EditorAssetLibrary.delete_asset(asset)
    mat = tools.create_asset(name, PKG, unreal.Material, unreal.MaterialFactoryNew())
    if not mat:
        unreal.log_error("[make_world_materials] create_asset failed: %s" % asset)
        raise SystemExit(1)
    # Static props render as ISMs; without this usage flag UE compiles no ISM permutation and
    # every prop falls back to the default grey material (unrecoverable in a packaged build).
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    # The baked level's world and prop meshes are Nanite, and the same rule applies: outside the
    # editor no new permutation can be compiled, so a master without this flag renders the whole
    # map in default grey. The editor hides it by compiling on demand.
    mat.set_editor_property("used_with_nanite", True)
    # GAME_LUMP props whose authored rest sequence changes their stored geometry are baked as
    # skeletal meshes but deliberately reuse these exact map material instances. Every master
    # `_master_for` can select for a prop therefore needs a cooked skeletal permutation; otherwise
    # the editor compiles one on demand while a packaged build substitutes Default Material.
    mat.set_editor_property("used_with_skeletal_mesh", True)
    return mat, asset


def _tex_param(mat, name, x, y, normal=False, white=False, grayscale=False):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    n.set_editor_property("parameter_name", name)
    if normal:
        n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        if _default_normal:
            n.set_editor_property("texture", _default_normal)
    elif grayscale:
        n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        n.set_editor_property("texture", _height_placeholder)
    else:
        n.set_editor_property(
            "sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
            if white else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        fallback = _linear_white_mask if white else _default_tex
        if fallback:
            n.set_editor_property("texture", fallback)
    return n


def _cube_param(mat, name, x, y):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameterCube, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    if _default_cube:
        n.set_editor_property("texture", _default_cube)
    return n


def _static_switch(mat, name, true_value, false_value, x, y, default=False):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionStaticSwitchParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", default)
    connect(true_value, "", n, "True")
    connect(false_value, "", n, "False")
    return n


def _scalar(mat, name, default, x, y):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", default)
    return n


def _collection_scalar(mat, name, x, y):
    node = mel.create_material_expression(
        mat, unreal.MaterialExpressionCollectionParameter, x, y)
    node.set_editor_property("collection", environment_collection)
    node.set_editor_property("parameter_name", name)
    return node


def _vec_param(mat, name, x, y, default=(1.0, 1.0, 1.0)):
    """A float3 parameter. A VectorParameter is a float4 with no RGB output of its own, so it
    is masked here once -- an unmasked float4 will not coerce into a float3 multiply."""
    n = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", unreal.LinearColor(default[0], default[1], default[2], 1.0))
    rgb = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x + 180, y)
    for channel, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        rgb.set_editor_property(channel, on)
    connect(n, "", rgb, "")
    return rgb


def build_world_graph(mat):
    """Author the shared lit world-surface graph; wire BaseColor/Emissive/Normal/Roughness/
    Specular/Metallic. Returns the Albedo sampler so a caller can also drive Opacity/
    OpacityMask from its alpha."""
    def mask(source, channel, x, y):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x, y)
        for name in ("r", "g", "b", "a"):
            node.set_editor_property(name, name == channel)
        connect(source, "", node, "")
        return node

    def constant(value, x, y):
        node = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y)
        node.set_editor_property("r", value)
        return node

    zero = constant(0.0, -1080, 2080)
    one = constant(1.0, -1080, 2160)
    black = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -1080, 2000)
    black.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))

    # Raw and deliberately coarse linear masks. The bake derives EnvMaskCoarseMip from each
    # imported mask's dimensions so this second sample is approximately an 8x8 footprint.
    env_mask = _tex_param(mat, "EnvMask", -900, 1060, white=True)
    # UE's Python material API cannot refresh the dynamic MipLevel input after MipValueMode is
    # changed. The sm_hub_1 wet-mask corpus needs only mip 6 (512 axis) and mip 7 (1024 axis),
    # so author both constant-level samples and select between them per material instance. This
    # still resolves every current mask to an approximately 8x8 coarse footprint.
    env_mask_mip6 = _tex_param(mat, "EnvMask", -900, 1140, white=True)
    env_mask_mip6.set_editor_property(
        "mip_value_mode", unreal.TextureMipValueMode.TMVM_MIP_LEVEL)
    env_mask_mip6.set_editor_property("const_mip_value", 6)
    env_mask_mip7 = _tex_param(mat, "EnvMask", -900, 1220, white=True)
    env_mask_mip7.set_editor_property(
        "mip_value_mode", unreal.TextureMipValueMode.TMVM_MIP_LEVEL)
    env_mask_mip7.set_editor_property("const_mip_value", 7)
    coarse_mip = _scalar(mat, "EnvMaskCoarseMip", 6.0, -900, 1300)
    coarse_high = mel.create_material_expression(mat, unreal.MaterialExpressionStep, -660, 1260)
    coarse_high.set_editor_property("const_x", 6.5)
    connect(coarse_mip, "", coarse_high, "Y")
    env_mask_coarse = mel.create_material_expression(
        mat, unreal.MaterialExpressionLinearInterpolate, -460, 1180)
    connect(env_mask_mip6, "R", env_mask_coarse, "A")
    connect(env_mask_mip7, "R", env_mask_coarse, "B")
    connect(coarse_high, "", env_mask_coarse, "Alpha")
    env_str = _scalar(mat, "EnvStrength", 0.0, -900, 1400)
    env_amt = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -640, 1100)
    connect(env_mask, "R", env_amt, "A")
    connect(env_str, "", env_amt, "B")

    # Authoritative wetness is shared by both the source endpoint and the enhancement branch.
    global_wetness = _collection_scalar(mat, "GlobalWetness", -900, 2160)
    wetness_scale = _scalar(mat, "WetnessScale", 0.0, -900, 2240)
    wetness_driven = _scalar(mat, "WetnessDriven", 0.0, -900, 2320)
    wetness_output_scale = _collection_scalar(mat, "WetnessOutputScale", -900, 2400)
    wet_authored = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -660, 2200)
    connect(global_wetness, "", wet_authored, "A")
    connect(wetness_scale, "", wet_authored, "B")
    wet_scaled = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -500, 2160)
    connect(wet_authored, "", wet_scaled, "A")
    connect(wetness_output_scale, "", wet_scaled, "B")
    wet_amount = mel.create_material_expression(mat, unreal.MaterialExpressionSaturate, -340, 2160)
    connect(wet_scaled, "", wet_amount, "")
    wet_selector = mel.create_material_expression(
        mat, unreal.MaterialExpressionLinearInterpolate, -160, 2240)
    connect(one, "", wet_selector, "A")
    connect(wet_amount, "", wet_selector, "B")
    connect(wetness_driven, "", wet_selector, "Alpha")
    env_wet = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -420, 1100)
    connect(env_amt, "", env_wet, "A")
    connect(wet_selector, "", env_wet, "B")
    env = mel.create_material_expression(mat, unreal.MaterialExpressionSaturate, -260, 1100)
    connect(env_wet, "", env, "")

    # Patch-authored endpoint: sample the imported VTF-order cube with the one established
    # handedness correction (UE.X, -UE.Y, UE.Z). Enhancement controls retention, not identity.
    reflection = mel.create_material_expression(
        mat, unreal.MaterialExpressionReflectionVectorWS, -900, 1760)
    handedness = mel.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -900, 1840)
    handedness.set_editor_property("constant", unreal.LinearColor(1.0, -1.0, 1.0, 0.0))
    source_direction = mel.create_material_expression(
        mat, unreal.MaterialExpressionMultiply, -660, 1780)
    connect(reflection, "", source_direction, "A")
    connect(handedness, "", source_direction, "B")
    source_cube = _cube_param(mat, "SourceCube", -420, 1760)
    connect(source_direction, "", source_cube, "UVs")
    enhancement = _collection_scalar(mat, "RainEnhancement", 740, 3000)
    source_retain = _collection_scalar(mat, "RainSourceRetain", -420, 1940)
    source_weight = mel.create_material_expression(
        mat, unreal.MaterialExpressionLinearInterpolate, -160, 1900)
    connect(one, "", source_weight, "A")
    connect(source_retain, "", source_weight, "B")
    connect(enhancement, "", source_weight, "Alpha")
    source_mask = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -80, 1760)
    connect(env_mask, "R", source_mask, "A")
    connect(wet_amount, "", source_mask, "B")
    source_mask_weight = mel.create_material_expression(
        mat, unreal.MaterialExpressionMultiply, 100, 1760)
    connect(source_mask, "", source_mask_weight, "A")
    connect(source_weight, "", source_mask_weight, "B")
    source_contribution = mel.create_material_expression(
        mat, unreal.MaterialExpressionMultiply, 280, 1760)
    connect(source_cube, "RGB", source_contribution, "A")
    connect(source_mask_weight, "", source_contribution, "B")
    source_view = _static_switch(
        mat, "WetnessUsesSourceCube", source_contribution, black, 480, 1760)

    # Enhanced-only exposure: upward surfaces at the top-down cover height. At tuning zero this
    # whole branch is mathematically zero; the source cubemap endpoint above remains active.
    world_pos = mel.create_material_expression(mat, unreal.MaterialExpressionWorldPosition,
                                               -1480, 2500)
    world_x = mask(world_pos, "r", -1280, 2440)
    world_y = mask(world_pos, "g", -1280, 2520)
    world_z = mask(world_pos, "b", -1280, 2600)
    min_x = _scalar(mat, "RainBoundsMinX", 0.0, -1480, 2700)
    min_y = _scalar(mat, "RainBoundsMinY", 0.0, -1480, 2780)
    size_x = _scalar(mat, "RainBoundsSizeX", 1.0, -1480, 2860)
    size_y = _scalar(mat, "RainBoundsSizeY", 1.0, -1480, 2940)
    sub_x = mel.create_material_expression(mat, unreal.MaterialExpressionSubtract, -1060, 2440)
    sub_y = mel.create_material_expression(mat, unreal.MaterialExpressionSubtract, -1060, 2520)
    connect(world_x, "", sub_x, "A"); connect(min_x, "", sub_x, "B")
    connect(world_y, "", sub_y, "A"); connect(min_y, "", sub_y, "B")
    uv_x = mel.create_material_expression(mat, unreal.MaterialExpressionDivide, -860, 2440)
    uv_y = mel.create_material_expression(mat, unreal.MaterialExpressionDivide, -860, 2520)
    connect(sub_x, "", uv_x, "A"); connect(size_x, "", uv_x, "B")
    connect(sub_y, "", uv_y, "A"); connect(size_y, "", uv_y, "B")
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -660, 2480)
    connect(uv_x, "", uv, "A"); connect(uv_y, "", uv, "B")
    height_tex = _tex_param(mat, "RainHeightTexture", -440, 2480, grayscale=True)
    connect(uv, "", height_tex, "UVs")
    sample_r = mask(height_tex, "r", -220, 2480)
    max_u16 = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -220, 2680)
    max_u16.set_editor_property("r", 65535.0)
    sample_u16 = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 0, 2480)
    connect(sample_r, "", sample_u16, "A"); connect(max_u16, "", sample_u16, "B")
    one_u16 = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, 0, 2680)
    one_u16.set_editor_property("r", 1.0)
    sample_index = mel.create_material_expression(mat, unreal.MaterialExpressionSubtract, 180, 2480)
    connect(sample_u16, "", sample_index, "A"); connect(one_u16, "", sample_index, "B")
    height_scale = _scalar(mat, "RainHeightZScale", 1.0, -220, 2780)
    height_min = _scalar(mat, "RainHeightMinZ", 0.0, -220, 2860)
    decoded_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 380, 2480)
    connect(sample_index, "", decoded_mul, "A"); connect(height_scale, "", decoded_mul, "B")
    decoded_height = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, 560, 2480)
    connect(decoded_mul, "", decoded_height, "A"); connect(height_min, "", decoded_height, "B")
    tolerance = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, 560, 2660)
    tolerance.set_editor_property("r", 5.0)
    cover_floor = mel.create_material_expression(mat, unreal.MaterialExpressionSubtract, 740, 2480)
    connect(decoded_height, "", cover_floor, "A"); connect(tolerance, "", cover_floor, "B")
    exposed = mel.create_material_expression(mat, unreal.MaterialExpressionStep, 920, 2480)
    connect(cover_floor, "", exposed, "X"); connect(world_z, "", exposed, "Y")
    sentinel = mel.create_material_expression(mat, unreal.MaterialExpressionStep, 920, 2600)
    sentinel.set_editor_property("const_x", 0.000001)
    connect(sample_r, "", sentinel, "Y")
    exposed_valid = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 1100, 2520)
    connect(exposed, "", exposed_valid, "A"); connect(sentinel, "", exposed_valid, "B")
    normal_ws = mel.create_material_expression(mat, unreal.MaterialExpressionPixelNormalWS, 740, 2760)
    up = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, 740, 2860)
    up.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
    upward_dot = mel.create_material_expression(mat, unreal.MaterialExpressionDotProduct, 920, 2800)
    connect(normal_ws, "", upward_dot, "A"); connect(up, "", upward_dot, "B")
    upward = mel.create_material_expression(mat, unreal.MaterialExpressionSaturate, 1100, 2800)
    connect(upward_dot, "", upward, "")
    enhanced_a = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 1280, 2640)
    connect(wet_amount, "", enhanced_a, "A"); connect(wetness_driven, "", enhanced_a, "B")
    enhanced_b = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 1460, 2640)
    connect(enhanced_a, "", enhanced_b, "A"); connect(env_mask_coarse, "", enhanced_b, "B")
    enhanced_c = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 1640, 2640)
    connect(enhanced_b, "", enhanced_c, "A"); connect(exposed_valid, "", enhanced_c, "B")
    enhanced_d = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 1820, 2640)
    connect(enhanced_c, "", enhanced_d, "A"); connect(upward, "", enhanced_d, "B")
    enhanced_wet = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 1820, 2640)
    connect(enhanced_d, "", enhanced_wet, "A"); connect(enhancement, "", enhanced_wet, "B")

    # General $envmap surfaces keep the existing metal/PBR response. Wet source-cube
    # permutations bypass it; their authored mask never drives Metallic/Specular/Roughness.
    metal_mask = _scalar(mat, "MetalMask", 0.0, -900, 1420)
    metal = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -420, 1420)
    connect(env, "", metal, "A")
    connect(metal_mask, "", metal, "B")
    metal_output = _static_switch(mat, "WetnessUsesSourceCube", zero, metal, -220, 1420)
    connect_property(metal_output, "", unreal.MaterialProperty.MP_METALLIC)

    # --- WorldVertexTransition base colour: lerp(Albedo, BaseTex2, VertexColor.r x BlendAmount) ---
    albedo = _tex_param(mat, "Albedo", -900, -260)
    basetex2 = _tex_param(mat, "BaseTex2", -900, -80)
    blend_amt = _scalar(mat, "BlendAmount", 0.0, -900, 100)
    vcol = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -900, 220)
    wvt_alpha = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -640, 160)
    connect(vcol, "R", wvt_alpha, "A")
    connect(blend_amt, "", wvt_alpha, "B")
    wvt = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -420, -160)
    connect(albedo, "RGB", wvt, "A")
    connect(basetex2, "RGB", wvt, "B")
    connect(wvt_alpha, "", wvt, "Alpha")

    # Legacy metal tint. The source-cube permutation uses the unmodified WVT albedo instead.
    env_tint = _vec_param(mat, "EnvTint", -900, 1600)
    white = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -900, 1740)
    white.set_editor_property("constant", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    tint_mix = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -560, 1660)
    connect(white, "", tint_mix, "A")
    connect(env_tint, "", tint_mix, "B")
    connect(metal, "", tint_mix, "Alpha")
    base_color = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, -100)
    connect(wvt, "", base_color, "A")
    connect(tint_mix, "", base_color, "B")

    # --- $selfillum emissive: Emissive.rgb x EmissiveScale (0 by default -> black) ---
    emis = _tex_param(mat, "Emissive", -900, 380)
    emis_scale = _scalar(mat, "EmissiveScale", 0.0, -900, 560)
    emis_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -420, 420)
    mel.connect_material_expressions(emis, "RGB", emis_mul, "A")
    mel.connect_material_expressions(emis_scale, "", emis_mul, "B")

    # Enhanced base darkening is applied only to source-cube wet materials.
    f, inv_f, fog_color = mat_fog.fog_from_primitive(mat)
    wet_darken = _collection_scalar(mat, "RainWetDarken", -120, -20)
    dark_amount = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 80, -20)
    connect(enhanced_wet, "", dark_amount, "A"); connect(wet_darken, "", dark_amount, "B")
    dark_factor = mel.create_material_expression(mat, unreal.MaterialExpressionOneMinus, 240, -20)
    connect(dark_amount, "", dark_factor, "")
    darkened_base = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 400, -100)
    connect(wvt, "", darkened_base, "A"); connect(dark_factor, "", darkened_base, "B")
    # Source's environment term is additive after the surface texture. Putting it in UE Base
    # Color makes the cube disappear wherever the rebuilt dynamic-light solution is dark. Keep
    # Base Color stable and carry the view-dependent term through Emissive instead. UE 5.8's
    # Ray Tracing Quality Switch selects RayTraced for both ray shaders and LUMEN_CARD_CAPTURE,
    # so the black replacement prevents the camera vector from entering secondary lighting.
    selected_base = _static_switch(
        mat, "WetnessUsesSourceCube", darkened_base, base_color, 960, -100)
    lumen_safe_source = mel.create_material_expression(
        mat, unreal.MaterialExpressionRayTracingQualitySwitch, 760, 120)
    connect(source_view, "", lumen_safe_source, "Normal")
    connect(black, "", lumen_safe_source, "RayTraced")
    primary_emissive = mel.create_material_expression(
        mat, unreal.MaterialExpressionAdd, 580, 420)
    connect(emis_mul, "", primary_emissive, "A")
    connect(lumen_safe_source, "", primary_emissive, "B")
    fogged_base = mat_fog.fade(mat, selected_base, "", inv_f, 1160, -160)
    fogged_emissive = mat_fog.inscatter(
        mat, mat_fog.fade(mat, primary_emissive, "", inv_f, -180, 420),
        f, fog_color, 40, 420)

    # General PBR specular versus enhanced source-cube specular.
    spec_base = _scalar(mat, "SpecBase", SPEC_BASE, -900, 1900)
    spec_reflect = _scalar(mat, "SpecReflect", SPEC_REFLECT, -900, 1980)
    spec = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -640, 1940)
    connect(spec_base, "", spec, "A")
    connect(spec_reflect, "", spec, "B")
    connect(env, "", spec, "Alpha")
    wet_specular = _collection_scalar(mat, "RainWetSpecular", -420, 2060)
    enhanced_spec = mel.create_material_expression(
        mat, unreal.MaterialExpressionLinearInterpolate, -160, 2020)
    connect(spec_base, "", enhanced_spec, "A")
    connect(wet_specular, "", enhanced_spec, "B")
    connect(enhanced_wet, "", enhanced_spec, "Alpha")
    selected_spec = _static_switch(
        mat, "WetnessUsesSourceCube", enhanced_spec, spec, 40, 1980)
    connect_property(mat_fog.specular(mat, inv_f, 220, 1900, source=selected_spec),
                     "", unreal.MaterialProperty.MP_SPECULAR)

    # --- $bumpmap normal: lerp(flat (0,0,1), BumpMap, BumpAmount) ---
    bump = _tex_param(mat, "BumpMap", -900, 720, normal=True)
    bump_amt = _scalar(mat, "BumpAmount", 0.0, -900, 900)
    flat_n = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -640, 660)
    flat_n.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
    normal = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -420, 720)
    mel.connect_material_expressions(flat_n, "", normal, "A")
    mel.connect_material_expressions(bump, "RGB", normal, "B")
    mel.connect_material_expressions(bump_amt, "", normal, "Alpha")
    mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)

    # General PBR roughness versus enhanced source-cube roughness.
    r_base = _scalar(mat, "RoughBase", ROUGH_BASE, -900, 1240 + 60)
    r_reflect = _scalar(mat, "RoughReflect", ROUGH_REFLECT, -900, 1320 + 60)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -420, 1160)
    connect(r_base, "", rough, "A")
    connect(r_reflect, "", rough, "B")
    connect(env, "", rough, "Alpha")
    wet_roughness = _collection_scalar(mat, "RainWetRoughness", -80, 1260)
    rough_delta = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 120, 1260)
    connect(enhanced_wet, "", rough_delta, "A"); connect(wet_roughness, "", rough_delta, "B")
    wet_rough = mel.create_material_expression(mat, unreal.MaterialExpressionSubtract, 300, 1160)
    connect(r_base, "", wet_rough, "A"); connect(rough_delta, "", wet_rough, "B")
    wet_rough_sat = mel.create_material_expression(mat, unreal.MaterialExpressionSaturate, 480, 1160)
    connect(wet_rough, "", wet_rough_sat, "")
    selected_rough = _static_switch(
        mat, "WetnessUsesSourceCube", wet_rough_sat, rough, 660, 1160)
    connect_property(selected_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    # Reflection diagnostics. Integer modes 1..6 select one channel; mode zero is Final.
    debug = _collection_scalar(mat, "RainReflectionDebug", 600, 2160)

    def debug_select(index, x, y):
        lower = mel.create_material_expression(mat, unreal.MaterialExpressionStep, x, y)
        lower.set_editor_property("const_y", index - 0.5)
        connect(debug, "", lower, "X")
        upper = mel.create_material_expression(mat, unreal.MaterialExpressionStep, x, y + 80)
        upper.set_editor_property("const_y", index + 0.5)
        connect(debug, "", upper, "X")
        before_upper = mel.create_material_expression(mat, unreal.MaterialExpressionOneMinus, x + 160, y + 80)
        connect(upper, "", before_upper, "")
        selected = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x + 320, y)
        connect(lower, "", selected, "A"); connect(before_upper, "", selected, "B")
        return selected

    debug_values = [env_mask, env_mask_coarse, wet_amount, source_cube,
                    source_contribution, enhanced_wet]
    debug_outputs = ["R", "", "", "RGB", "", ""]
    debug_sum = black
    for index, (value, output) in enumerate(zip(debug_values, debug_outputs), 1):
        selector = debug_select(index, 820 + (index % 2) * 520, 2140 + index * 180)
        weighted = mel.create_material_expression(
            mat, unreal.MaterialExpressionMultiply, 1740, 2140 + index * 180)
        connect(value, output, weighted, "A"); connect(selector, "", weighted, "B")
        added = mel.create_material_expression(
            mat, unreal.MaterialExpressionAdd, 1940, 2140 + index * 180)
        connect(debug_sum, "", added, "A"); connect(weighted, "", added, "B")
        debug_sum = added
    wet_debug_color = _static_switch(
        mat, "WetnessUsesSourceCube", debug_sum, black, 2160, 2520)
    debug_enabled = mel.create_material_expression(mat, unreal.MaterialExpressionStep, 2160, 2340)
    debug_enabled.set_editor_property("const_y", 0.5)
    connect(debug, "", debug_enabled, "X")
    wet_debug_enabled = _static_switch(
        mat, "WetnessUsesSourceCube", debug_enabled, zero, 2340, 2340)
    final_base = mel.create_material_expression(
        mat, unreal.MaterialExpressionLinearInterpolate, 2520, -100)
    connect(fogged_base, "", final_base, "A"); connect(black, "", final_base, "B")
    connect(wet_debug_enabled, "", final_base, "Alpha")
    final_emissive = mel.create_material_expression(
        mat, unreal.MaterialExpressionLinearInterpolate, 2520, 420)
    connect(fogged_emissive, "", final_emissive, "A")
    connect(wet_debug_color, "", final_emissive, "B")
    connect(wet_debug_enabled, "", final_emissive, "Alpha")
    connect_property(final_base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(final_emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    return albedo


def make_opaque():
    mat, asset = _fresh("M_World_Opaque")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    build_world_graph(mat)
    _save(mat, asset)


def make_masked():
    mat, asset = _fresh("M_World_Masked")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    # Scissor surfaces (chain-link, grates, foliage cards) read from both sides.
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("opacity_mask_clip_value", 0.333)
    albedo = build_world_graph(mat)
    mel.connect_material_property(albedo, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    _save(mat, asset)


def make_translucent():
    mat, asset = _fresh("M_World_Translucent")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # Per-pixel lit translucency so glass still takes the real-time rig + Lumen, not a flat bed.
    mat.set_editor_property("translucency_lighting_mode",
                            unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    albedo = build_world_graph(mat)
    mel.connect_material_property(albedo, "A", unreal.MaterialProperty.MP_OPACITY)
    _save(mat, asset)


def make_glass():
    """Author the stable UE5 thin-glass path without changing generic transparency.

    Albedo.A is the authored pane/frame coverage. The custom Thin Translucent output owns that
    coverage and coloured transmission; root Opacity is only the optional painted layer on top.
    Raising alpha to the eighth power makes low-alpha panes contribute essentially no diffuse
    card while leaving the texture's fully opaque mullions and grime intact.
    """
    mat, asset = _fresh("M_World_Glass")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_THIN_TRANSLUCENT)
    mat.set_editor_property(
        "translucency_lighting_mode",
        unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mat.set_editor_property("refraction_method", unreal.RefractionMode.RM_PIXEL_NORMAL_OFFSET)

    albedo = _tex_param(mat, "Albedo", -1100, -300)

    # Thin transmission: lerp(white, authored RGB, 0.25). This keeps the dirty blue-green
    # authored tint without treating its dark RGB as an opaque diffuse pane.
    white = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -850, -80)
    white.set_editor_property("constant", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    tint_strength = _scalar(
        mat, "GlassTintStrength", GLASS_TINT_STRENGTH, -1100, 40)
    transmittance = mel.create_material_expression(
        mat, unreal.MaterialExpressionLinearInterpolate, -600, -180)
    connect(white, "", transmittance, "A")
    connect(albedo, "RGB", transmittance, "B")
    connect(tint_strength, "", transmittance, "Alpha")

    thin = mel.create_material_expression(
        mat, unreal.MaterialExpressionThinTranslucentMaterialOutput, -300, -240)
    connect(transmittance, "", thin, "TransmittanceColor")
    connect(albedo, "A", thin, "SurfaceCoverage")

    # Root opacity is the coloured/diffuse layer on top of thin glass, not the pane's whole
    # coverage. alpha^8 retains fully opaque painted frames while a 0.27 pane becomes ~0.
    frame_exp = _scalar(mat, "GlassFrameExponent", GLASS_FRAME_EXPONENT, -1100, 180)
    frame_coverage = mel.create_material_expression(mat, unreal.MaterialExpressionPower, -600, 80)
    connect(albedo, "A", frame_coverage, "Base")
    connect(frame_exp, "", frame_coverage, "Exp")
    connect_property(frame_coverage, "", unreal.MaterialProperty.MP_OPACITY)

    # Thin Translucent's custom coverage is independent of the root opacity input. Explicitly
    # premultiply the diffuse layer by the same frame coverage so low-alpha pane pixels cannot
    # expose the authored blue albedo as a texture card. Fully painted frames and dirt retain
    # their authored RGB, while pane tint remains exclusively in TransmittanceColor above.
    diffuse_layer = mel.create_material_expression(
        mat, unreal.MaterialExpressionMultiply, -430, 160)
    connect(albedo, "RGB", diffuse_layer, "A")
    connect(frame_coverage, "", diffuse_layer, "B")

    # Preserve the same per-primitive Source fog term used by the other lit masters.
    emis = _tex_param(mat, "Emissive", -1100, 360)
    emis_scale = _scalar(mat, "EmissiveScale", 0.0, -1100, 540)
    emis_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -600, 400)
    connect(emis, "RGB", emis_mul, "A")
    connect(emis_scale, "", emis_mul, "B")
    f, inv_f, fog_color = mat_fog.fog_from_primitive(mat)
    connect_property(
        mat_fog.fade(mat, diffuse_layer, "", inv_f, -180, -120),
        "", unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(
        mat_fog.inscatter(
            mat, mat_fog.fade(mat, emis_mul, "", inv_f, -180, 400),
            f, fog_color, 40, 400),
        "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # Existing $envmap/Lumen reflection contract. The mask localises Fresnel/specular and
    # roughness exactly as on the other world masters; glass remains dielectric (Metallic=0).
    env_mask = _tex_param(mat, "EnvMask", -1100, 760, white=True)
    env_strength = _scalar(mat, "EnvStrength", 0.0, -1100, 940)
    env_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -820, 800)
    connect(env_mask, "R", env_mul, "A")
    connect(env_strength, "", env_mul, "B")
    env = mel.create_material_expression(mat, unreal.MaterialExpressionSaturate, -660, 800)
    connect(env_mul, "", env, "")

    rough_base = _scalar(mat, "RoughBase", ROUGH_BASE, -1100, 1080)
    rough_reflect = _scalar(mat, "RoughReflect", ROUGH_REFLECT, -1100, 1160)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -520, 1060)
    connect(rough_base, "", rough, "A")
    connect(rough_reflect, "", rough, "B")
    connect(env, "", rough, "Alpha")
    connect_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    spec_base = _scalar(mat, "SpecBase", SPEC_BASE, -1100, 1300)
    spec_reflect = _scalar(mat, "SpecReflect", SPEC_REFLECT, -1100, 1380)
    spec = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -520, 1320)
    connect(spec_base, "", spec, "A")
    connect(spec_reflect, "", spec, "B")
    connect(env, "", spec, "Alpha")
    connect_property(
        mat_fog.specular(mat, inv_f, -280, 1300, source=spec),
        "", unreal.MaterialProperty.MP_SPECULAR)

    # Tangent normal drives both lighting and Pixel Normal Offset. Derived glass normals and
    # real authored $bumpmap textures share this binding; an unbound surface remains flat.
    bump = _tex_param(mat, "BumpMap", -1100, 1540, normal=True)
    bump_amount = _scalar(mat, "BumpAmount", 0.0, -1100, 1720)
    flat_n = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -820, 1500)
    flat_n.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
    normal = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -520, 1560)
    connect(flat_n, "", normal, "A")
    connect(bump, "RGB", normal, "B")
    connect(bump_amount, "", normal, "Alpha")
    connect_property(normal, "", unreal.MaterialProperty.MP_NORMAL)

    refraction = _scalar(mat, "GlassRefraction", GLASS_REFRACTION, -520, 1780)
    connect_property(refraction, "", unreal.MaterialProperty.MP_REFRACTION)
    _save(mat, asset)


def make_refract():
    """Author Source's framebuffer-distortion card as a clear UE5 thin surface.

    A Refract VMT has no albedo in the common case: it samples the framebuffer through an
    authored signed DUDV/normal field. White transmittance and full surface coverage preserve
    that meaning without painting the vector texture onto the pane. Pixel Normal Offset is the
    stable UE path for these large flat cards; 1.0 is neutral, so the original
    ``$refractamount`` is added to one rather than interpreted as glass IOR.
    """
    mat, asset = _fresh("M_Refract")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_THIN_TRANSLUCENT)
    mat.set_editor_property(
        "translucency_lighting_mode",
        unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    mat.set_editor_property("refraction_method", unreal.RefractionMode.RM_PIXEL_NORMAL_OFFSET)

    refract_map = _tex_param(mat, "RefractMap", -760, -160, normal=True)
    connect_property(refract_map, "RGB", unreal.MaterialProperty.MP_NORMAL)

    amount = _scalar(mat, "SourceRefractAmount", 0.0, -760, 40)
    neutral = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -760, 140)
    neutral.set_editor_property("r", 1.0)
    refraction = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -480, 60)
    connect(neutral, "", refraction, "A")
    connect(amount, "", refraction, "B")
    connect_property(refraction, "", unreal.MaterialProperty.MP_REFRACTION)

    # The overlay contributes no colour or attenuation of its own. It exists only to offset
    # the scene sample; the actual pane tint/frame remains on the glass material behind it.
    white = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -760, 300)
    white.set_editor_property("constant", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    coverage = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -760, 420)
    coverage.set_editor_property("r", 1.0)
    thin = mel.create_material_expression(
        mat, unreal.MaterialExpressionThinTranslucentMaterialOutput, -420, 320)
    connect(white, "", thin, "TransmittanceColor")
    connect(coverage, "", thin, "SurfaceCoverage")

    no_top_layer = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -480, 500)
    no_top_layer.set_editor_property("r", 0.0)
    connect_property(no_top_layer, "", unreal.MaterialProperty.MP_OPACITY)
    _save(mat, asset)


def make_additive():
    mat, asset = _fresh("M_Additive")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # REQUIRED, and the failure is cooked-build-only. This master is instanced as the sprite
    # material for every VtMB emitter (make_particle_systems.py), and
    # FNiagaraRendererSprites::IsMaterialValid gates on MATUSAGE_NiagaraSprites. In the editor
    # CheckMaterialUsage_Concurrent adds the flag and dirties a package that is regenerated from
    # scratch on the next content build, so it is silently re-lost every export; in a cook the
    # permutation is simply absent and every sprite falls back to the default grey surface.
    mat.set_editor_property("used_with_niagara_sprites", True)
    # $additive: the base texture RGB is added onto the framebuffer, full-bright (no lighting).
    albedo = _tex_param(mat, "Albedo", -520, -40)
    emis_scale = _scalar(mat, "EmissiveScale", 1.0, -520, 200)
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 40)
    mel.connect_material_expressions(albedo, "RGB", mul, "A")
    mel.connect_material_expressions(emis_scale, "", mul, "B")
    # Fog fades an additive glow OUT and adds no inscatter of its own: additive blending can only
    # brighten what is already in the framebuffer, and the surface behind the glow has already
    # contributed the fog's light once. A distant neon sign dims into the haze; it does not tint it.
    _, inv_f, _ = mat_fog.fog_from_primitive(mat, x=-1200, y=900)
    mel.connect_material_property(mat_fog.fade(mat, mul, "", inv_f, -60, 40),
                                  "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # Additive blend weights the added colour by Opacity; the glow panes carry a coverage alpha.
    mel.connect_material_property(albedo, "A", unreal.MaterialProperty.MP_OPACITY)
    _save(mat, asset)


def _save(mat, asset):
    mel.recompile_material(mat)
    if unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False):
        unreal.log("[make_world_materials] saved %s" % asset)
    else:
        unreal.log_error("[make_world_materials] save failed: %s" % asset)
        raise SystemExit(1)


make_opaque()
make_masked()
make_translucent()
make_glass()
make_refract()
make_additive()

# Retire the old single master: the runtime now loads M_World_Opaque. Leaving M_VtMB_World behind
# would ship a dead asset the factory no longer references.
_OLD = "%s/%s" % (PKG, "M_VtMB_World")
if unreal.EditorAssetLibrary.does_asset_exist(_OLD):
    unreal.EditorAssetLibrary.delete_asset(_OLD)
    unreal.log("[make_world_materials] removed stale %s" % _OLD)
