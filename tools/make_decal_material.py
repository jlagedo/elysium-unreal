# Generates Content/VtMB/Materials/M_Decal.uasset: the master material for roadmap 7.2 decals.
# VtMB's `infodecal` layer (blood, bullet holes, graffiti, posters, stains) is exported as a
# `<map>.decals` projector sidecar; the runtime builds one deferred UDecalComponent per line and
# gives each a MID off this master. A DEFERRED-DECAL material writes into the GBuffer before the
# lighting pass, so a decal is lit exactly like the wall it projects onto -- Lumen indirect bounce
# included, which the fully-dynamic render path relies on.
#
# BaseColor = Albedo.rgb, Opacity = Albedo.a (alpha-blended overlay), plus the same alpha-masked
# $selfillum emissive path as M_VtMB_World (Emissive x EmissiveScale, off by default). A UMaterial
# graph only compiles offline, so this is authored here once and committed; the runtime only
# instances it (FElysiumMaterialFactory::BuildDecal binds Albedo / Emissive / EmissiveScale).
#
# Normally rebuilt by the umbrella (content.bat -> tools/build_content.py, which the export runs);
# also runnable standalone:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="tools/make_decal_material.py" -unattended -nosplash -nopause
import unreal

PKG = "/Game/VtMB/Materials"
NAME = "M_Decal"
ASSET = "%s/%s" % (PKG, NAME)
DEFAULT_TEX = "/Engine/EngineResources/DefaultTexture.DefaultTexture"

mel = unreal.MaterialEditingLibrary

if unreal.EditorAssetLibrary.does_asset_exist(ASSET):
    unreal.EditorAssetLibrary.delete_asset(ASSET)

tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = tools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
if not mat:
    unreal.log_error("[make_decal_material] create_asset failed: %s" % ASSET)
    raise SystemExit(1)

# Deferred decal domain + Translucent blend: blend the full material into the GBuffer (BaseColor +
# Opacity + Emissive), then let deferred lighting (Lumen included) light it like the receiving
# surface. Since UE 5.2 the old `decal_blend_mode` (DecalBlendMode) is deprecated/no-op; a deferred
# decal's blend is the material's REGULAR blend_mode (BLEND_TRANSLUCENT == the old DBM_Translucent).
mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)

default_tex = unreal.EditorAssetLibrary.load_asset(DEFAULT_TEX)

# Decal UV = (U, 1 - V). A deferred decal's projected V axis maps to the component's local +Y (up),
# but VtMB (like Unreal) authors texture V top-down, so a decal arrives vertically flipped. Rebuild
# the UV as (U, 1-V) once here so every decal reads upright. Built as TexCoord*(1,-1) + (0,1).
uv = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1080, 40)
uv.set_editor_property("coordinate_index", 0)
uv_mul = mel.create_material_expression(mat, unreal.MaterialExpressionConstant2Vector, -1080, 200)
uv_mul.set_editor_property("r", 1.0)
uv_mul.set_editor_property("g", -1.0)
uv_scaled = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -900, 80)
mel.connect_material_expressions(uv, "", uv_scaled, "A")
mel.connect_material_expressions(uv_mul, "", uv_scaled, "B")
uv_add = mel.create_material_expression(mat, unreal.MaterialExpressionConstant2Vector, -1080, 320)
uv_add.set_editor_property("r", 0.0)
uv_add.set_editor_property("g", 1.0)
decal_uv = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -740, 100)
mel.connect_material_expressions(uv_scaled, "", decal_uv, "A")
mel.connect_material_expressions(uv_add, "", decal_uv, "B")

# Albedo: the decal's colour+alpha, bound at runtime via a MID. RGB -> BaseColor, A -> Opacity
# (the alpha channel is the decal's coverage mask). A default engine texture lets the graph compile.
albedo = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -520, -40)
albedo.set_editor_property("parameter_name", "Albedo")
albedo.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
if default_tex:
    albedo.set_editor_property("texture", default_tex)
mel.connect_material_expressions(decal_uv, "", albedo, "UVs")
mel.connect_material_property(albedo, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
mel.connect_material_property(albedo, "A", unreal.MaterialProperty.MP_OPACITY)

# Emissive: the per-decal alpha-masked self-illum map (rare -- glowing signs/graffiti), mirroring
# M_VtMB_World. EmissiveScale defaults to 0, so a decal without a bound `map_Ke` never glows; the
# material factory sets it to elysium.EmissiveScale only for a decal that carries one.
emis = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -520, 300)
emis.set_editor_property("parameter_name", "Emissive")
emis.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
if default_tex:
    emis.set_editor_property("texture", default_tex)
mel.connect_material_expressions(decal_uv, "", emis, "UVs")

scale = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -520, 500)
scale.set_editor_property("parameter_name", "EmissiveScale")
scale.set_editor_property("default_value", 0.0)

mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 340)
mel.connect_material_expressions(emis, "RGB", mul, "A")
mel.connect_material_expressions(scale, "", mul, "B")
mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

mel.recompile_material(mat)
if unreal.EditorAssetLibrary.save_asset(ASSET, only_if_is_dirty=False):
    unreal.log("[make_decal_material] saved %s" % ASSET)
else:
    unreal.log_error("[make_decal_material] save failed")
    raise SystemExit(1)
