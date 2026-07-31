# Generates the local world master-material set under Content/VtMB/Materials/ (roadmap 7.4):
#
#   M_World_Opaque       opaque world surfaces (the common case; grown from the old M_VtMB_World)
#   M_World_Masked       $alphatest scissor surfaces (fences, grates, foliage) -- two-sided
#   M_World_Translucent  $translucent surfaces (glass, water film) -- alpha-blended, lit per-pixel
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
#   EnvMask     (tex)     $envmap reflectivity mask; .r lowers Roughness so Lumen reflections appear
#   EnvStrength (float)   0 by default (unbound surface stays matte at the calibrated 0.5 roughness)
#   BaseTex2    (tex)     WorldVertexTransition second albedo; lerp(Albedo, BaseTex2, VertexColor.r x BlendAmount)
#   BlendAmount (float)   0 by default (non-WVT surface ignores BaseTex2 and its vertex colour)
#
# All four also carry Source's distance fog as a PER-PRIMITIVE term read from Custom Primitive
# Data (`mat_fog`), because the world and the 3D-skybox miniature are fogged differently and
# share screen depth, which no engine-side fog mechanism can separate. Unwritten data is zero,
# which is "not fogged", so the term is neutral on any primitive nobody wrote to.
#
# $envmap is handled the modern way (roadmap decision 2026-07-24): NOT a baked-cube sample but a
# reflectivity mask that lowers Roughness, so the fully-dynamic Lumen path produces the reflection
# (roadmap 7.5 tunes it). The exported tex/cube/ faces stay unused by the world path (the 2D sky
# still uses them). Base Roughness is held at 0.5 -- the value the old unconnected pin defaulted to
# -- so non-$envmap surfaces keep their calibrated look; only masked surfaces drop toward 0.15.
#
# A UMaterial graph only compiles in the editor, so these are authored here and committed; the
# runtime only instances them (FElysiumMaterialFactory picks the master by blend flag and binds the
# parameters above). Rebuilt by the umbrella (uv run elysium export bundle policy -> pipeline/unreal/build_content.py, which the
# export runs); also runnable standalone:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="pipeline/unreal/make_world_materials.py" -unattended -nosplash -nopause
import os
import sys

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import mat_fog

# A refused pin compiles anyway against the input's constant default, so a wrong output name
# becomes a wrongly-rendering material instead of a failed build. These raise instead.
connect = mat_fog.connect


def connect_property(src, src_out, prop):
    if not unreal.MaterialEditingLibrary.connect_material_property(src, src_out, prop):
        raise SystemExit("[make_world_materials] no connection %s.%s -> %s" % (
            src.get_class().get_name(), src_out or "<out>", prop))

PKG = "/Game/VtMB/Materials"
DEFAULT_TEX = "/Engine/EngineResources/DefaultTexture.DefaultTexture"
DEFAULT_NORMAL = "/Engine/EngineMaterials/DefaultNormal.DefaultNormal"
# EnvMask needs a WHITE default, not DefaultTexture: 228 of the game's reflective materials
# carry $envmap with no $envmapmask and reflect uniformly, and nothing overwrites the sampler
# for them. DefaultTexture is a 128x128 greenish-grey noise image (mean RGB 122/140/131), so
# it would both dim and mottle exactly those surfaces. Matches the runtime factory, which
# binds a 1x1 white for the same case.
WHITE_TEX = "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"

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

mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
# unreal.load_asset force-loads by object path (LoadObject); the asset-registry-backed
# EditorAssetLibrary.load_asset does not index /Engine/EngineMaterials in a commandlet, and its
# miss both leaves the sampler defaultless and trips the commandlet's error-exit.
_default_tex = unreal.load_asset(DEFAULT_TEX)
_default_normal = unreal.load_asset(DEFAULT_NORMAL)
_white_tex = unreal.load_asset(WHITE_TEX)


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
    return mat, asset


def _tex_param(mat, name, x, y, normal=False, white=False):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    n.set_editor_property("parameter_name", name)
    if normal:
        n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        if _default_normal:
            n.set_editor_property("texture", _default_normal)
    else:
        n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        fallback = _white_tex if white else _default_tex
        if fallback:
            n.set_editor_property("texture", fallback)
    return n


def _scalar(mat, name, default, x, y):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", default)
    return n


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
    # --- $envmap reach: env = saturate(EnvMask.r x EnvStrength) ---
    # Computed first because three outputs read it: Roughness, Specular, and -- through the
    # metal branch -- BaseColor. EnvStrength defaults to 0, so a surface with no $envmap is
    # untouched by every one of them.
    env_mask = _tex_param(mat, "EnvMask", -900, 1060, white=True)
    env_str = _scalar(mat, "EnvStrength", 0.0, -900, 1240)
    env_amt = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -640, 1100)
    connect(env_mask, "R", env_amt, "A")
    connect(env_str, "", env_amt, "B")
    env = mel.create_material_expression(mat, unreal.MaterialExpressionSaturate, -520, 1100)
    connect(env_amt, "", env, "")

    # --- Metallic: env x MetalMask ---
    # VtMB names its own metals. $envmaptint multiplies the reflection, and on 102 of the
    # game's 2,610 reflective materials it is CHROMATIC -- brass 0.65/0.5/0, copper
    # 0.74/0.57/0.31 -- which is a hand-authored statement that the surface is metal and what
    # colour it reflects (docs/vtmb/reflections.md). MetalMask defaults to 0, so metalness is never
    # inferred: the bake sets it only where the game's own tint says so, and the $envmapmask
    # then localises it to the metal texels rather than the whole surface.
    metal_mask = _scalar(mat, "MetalMask", 0.0, -900, 1420)
    metal = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -420, 1420)
    connect(env, "", metal, "A")
    connect(metal_mask, "", metal, "B")
    connect_property(metal, "", unreal.MaterialProperty.MP_METALLIC)

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

    # A metal's BaseColor IS its reflection colour, so the tint lands here rather than on a
    # specular level UE ignores once Metallic is up. Away from the metal branch the lerp
    # resolves to white and this is a multiply by 1.
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

    # --- Source's distance fog, per primitive (mat_fog / sky-ambience B8b) ---
    # The world and the 3D-skybox miniature carry two different fogs and share screen depth, so
    # the term is driven by each primitive's own Custom Primitive Data. Unwritten data reads as
    # zero, which is f = 0, so this passes BaseColor/Specular/Emissive through unchanged.
    f, inv_f, fog_color = mat_fog.fog_from_primitive(mat)
    connect_property(mat_fog.fade(mat, base_color, "", inv_f, -180, -160),
                     "", unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(
        mat_fog.inscatter(mat, mat_fog.fade(mat, emis_mul, "", inv_f, -180, 420),
                          f, fog_color, 40, 420),
        "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # --- Specular: lerp(SpecBase, SpecReflect, env), faded by the fog ---
    # SpecBase 0 is the Lambert floor; the reflection channel raises it only where the mask
    # does. The grey half of $envmaptint (362 materials) is a reflection-strength dim-down and
    # the bake folds its luminance into SpecReflect, so it needs no node of its own.
    spec_base = _scalar(mat, "SpecBase", SPEC_BASE, -900, 1900)
    spec_reflect = _scalar(mat, "SpecReflect", SPEC_REFLECT, -900, 1980)
    spec = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -640, 1940)
    connect(spec_base, "", spec, "A")
    connect(spec_reflect, "", spec, "B")
    connect(env, "", spec, "Alpha")
    connect_property(mat_fog.specular(mat, inv_f, -400, 1900, source=spec),
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

    # --- Roughness: lerp(RoughBase, RoughReflect, env) ---
    # Both ends are parameters, not constants, so the calibration is a bake binding (and a
    # live cvar over the baked instance) rather than a re-authored graph.
    r_base = _scalar(mat, "RoughBase", ROUGH_BASE, -900, 1240 + 60)
    r_reflect = _scalar(mat, "RoughReflect", ROUGH_REFLECT, -900, 1320 + 60)
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -420, 1160)
    connect(r_base, "", rough, "A")
    connect(r_reflect, "", rough, "B")
    connect(env, "", rough, "Alpha")
    connect_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

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


def make_additive():
    mat, asset = _fresh("M_Additive")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
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
make_additive()

# Retire the old single master: the runtime now loads M_World_Opaque. Leaving M_VtMB_World behind
# would ship a dead asset the factory no longer references.
_OLD = "%s/%s" % (PKG, "M_VtMB_World")
if unreal.EditorAssetLibrary.does_asset_exist(_OLD):
    unreal.EditorAssetLibrary.delete_asset(_OLD)
    unreal.log("[make_world_materials] removed stale %s" % _OLD)
