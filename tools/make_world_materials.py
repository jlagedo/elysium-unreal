# Generates the committed world master-material set under Content/VtMB/Materials/ (roadmap 7.4):
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
# $envmap is handled the modern way (roadmap decision 2026-07-24): NOT a baked-cube sample but a
# reflectivity mask that lowers Roughness, so the fully-dynamic Lumen path produces the reflection
# (roadmap 7.5 tunes it). The exported tex/cube/ faces stay unused by the world path (the 2D sky
# still uses them). Base Roughness is held at 0.5 -- the value the old unconnected pin defaulted to
# -- so non-$envmap surfaces keep their calibrated look; only masked surfaces drop toward 0.15.
#
# A UMaterial graph only compiles in the editor, so these are authored here and committed; the
# runtime only instances them (FElysiumMaterialFactory picks the master by blend flag and binds the
# parameters above). Rebuilt by the umbrella (content.bat -> tools/build_content.py, which the
# export runs); also runnable standalone:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="tools/make_world_materials.py" -unattended -nosplash -nopause
import unreal

PKG = "/Game/VtMB/Materials"
DEFAULT_TEX = "/Engine/EngineResources/DefaultTexture.DefaultTexture"
DEFAULT_NORMAL = "/Engine/EngineMaterials/DefaultNormal.DefaultNormal"

# Base/reflective roughness for the $envmap-mask -> Lumen path. 0.5 is what the old material's
# unconnected Roughness pin defaulted to, so unmasked surfaces are unchanged.
ROUGH_BASE = 0.5
ROUGH_REFLECT = 0.15

mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
# unreal.load_asset force-loads by object path (LoadObject); the asset-registry-backed
# EditorAssetLibrary.load_asset does not index /Engine/EngineMaterials in a commandlet, and its
# miss both leaves the sampler defaultless and trips the commandlet's error-exit.
_default_tex = unreal.load_asset(DEFAULT_TEX)
_default_normal = unreal.load_asset(DEFAULT_NORMAL)


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


def _tex_param(mat, name, x, y, normal=False):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    n.set_editor_property("parameter_name", name)
    if normal:
        n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        if _default_normal:
            n.set_editor_property("texture", _default_normal)
    else:
        n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        if _default_tex:
            n.set_editor_property("texture", _default_tex)
    return n


def _scalar(mat, name, default, x, y):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", default)
    return n


def build_world_graph(mat):
    """Author the shared lit world-surface graph; wire BaseColor/Emissive/Normal/Roughness.
    Returns the Albedo sampler so a caller can also drive Opacity/OpacityMask from its alpha."""
    # --- WorldVertexTransition base colour: lerp(Albedo, BaseTex2, VertexColor.r x BlendAmount) ---
    albedo = _tex_param(mat, "Albedo", -900, -260)
    basetex2 = _tex_param(mat, "BaseTex2", -900, -80)
    blend_amt = _scalar(mat, "BlendAmount", 0.0, -900, 100)
    vcol = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -900, 220)
    wvt_alpha = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -640, 160)
    mel.connect_material_expressions(vcol, "R", wvt_alpha, "A")
    mel.connect_material_expressions(blend_amt, "", wvt_alpha, "B")
    base_color = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -420, -160)
    mel.connect_material_expressions(albedo, "RGB", base_color, "A")
    mel.connect_material_expressions(basetex2, "RGB", base_color, "B")
    mel.connect_material_expressions(wvt_alpha, "", base_color, "Alpha")
    mel.connect_material_property(base_color, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # --- $selfillum emissive: Emissive.rgb x EmissiveScale (0 by default -> black) ---
    emis = _tex_param(mat, "Emissive", -900, 380)
    emis_scale = _scalar(mat, "EmissiveScale", 0.0, -900, 560)
    emis_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -420, 420)
    mel.connect_material_expressions(emis, "RGB", emis_mul, "A")
    mel.connect_material_expressions(emis_scale, "", emis_mul, "B")
    mel.connect_material_property(emis_mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

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

    # --- $envmap -> Lumen: EnvMask.r x EnvStrength lowers Roughness toward ROUGH_REFLECT ---
    env_mask = _tex_param(mat, "EnvMask", -900, 1060)
    env_str = _scalar(mat, "EnvStrength", 0.0, -900, 1240)
    env_amt = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -640, 1100)
    mel.connect_material_expressions(env_mask, "R", env_amt, "A")
    mel.connect_material_expressions(env_str, "", env_amt, "B")
    env_sat = mel.create_material_expression(mat, unreal.MaterialExpressionSaturate, -520, 1100)
    mel.connect_material_expressions(env_amt, "", env_sat, "")
    rough = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -420, 1160)
    r_base = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -640, 1240)
    r_base.set_editor_property("r", ROUGH_BASE)
    r_reflect = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -640, 1320)
    r_reflect.set_editor_property("r", ROUGH_REFLECT)
    mel.connect_material_expressions(r_base, "", rough, "A")
    mel.connect_material_expressions(r_reflect, "", rough, "B")
    mel.connect_material_expressions(env_sat, "", rough, "Alpha")
    mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

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
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
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
