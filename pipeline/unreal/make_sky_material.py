# Generates Content/VtMB/Materials/M_Sky.uasset: the six-face 2D skybox master material.
# UMaterial (a shading graph) can only be compiled offline, so it is authored here once and
# committed; the runtime only ever instances it (a UTextureCube bound per map to the SkyCube
# parameter). It is unlit and two-sided, and samples the cube along the per-pixel view ray so
# the sky reads as infinitely far regardless of the mesh it is drawn on.
#
# Normally rebuilt by the umbrella (uv run elysium export bundle policy -> pipeline/unreal/build_content.py, which the export
# runs); also runnable standalone:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="pipeline/unreal/make_sky_material.py" -unattended -nosplash -nopause
import unreal

PKG = "/Game/VtMB/Materials"
NAME = "M_Sky"
ASSET = "%s/%s" % (PKG, NAME)
DEFAULT_CUBE = "/Engine/EngineResources/DefaultTextureCube.DefaultTextureCube"

mel = unreal.MaterialEditingLibrary

if unreal.EditorAssetLibrary.does_asset_exist(ASSET):
    unreal.EditorAssetLibrary.delete_asset(ASSET)

tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = tools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
if not mat:
    unreal.log_error("[make_sky_material] create_asset failed")
    raise SystemExit(1)

mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)

# SkyCube: the per-map cubemap, bound at runtime via a MID. A default engine cube lets the
# graph compile with a valid texture.
cube = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameterCube, -500, 0)
cube.set_editor_property("parameter_name", "SkyCube")
cube.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
default_cube = unreal.EditorAssetLibrary.load_asset(DEFAULT_CUBE)
if default_cube:
    cube.set_editor_property("texture", default_cube)

# View ray = -CameraVector (CameraVectorWS points from the pixel toward the camera). Sampling
# the cube by this world-space direction makes the sky orientation-correct and distance-free.
cam = mel.create_material_expression(mat, unreal.MaterialExpressionCameraVectorWS, -900, 150)
neg = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -700, 150)
neg.set_editor_property("const_b", -1.0)

# Brightness: the 2D skyboxes are near-black night skies whose linear emissive tonemaps to
# nothing; a runtime-tunable scalar lifts them so the sky reads. Default 1 (no change).
bright = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -300, 200)
bright.set_editor_property("parameter_name", "Brightness")
bright.set_editor_property("default_value", 1.0)
scale = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -120, 60)

mel.connect_material_expressions(cam, "", neg, "A")
# The TextureSample UV pin is named "UVs" (matches make_decal_material.py); "Coordinates" matches no
# input, so the connection silently no-ops and the cube compiles to "needs UV input" → default material.
mel.connect_material_expressions(neg, "", cube, "UVs")
mel.connect_material_expressions(cube, "RGB", scale, "A")
mel.connect_material_expressions(bright, "", scale, "B")
mel.connect_material_property(scale, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

mel.recompile_material(mat)
ok = unreal.EditorAssetLibrary.save_asset(ASSET)
unreal.log("[make_sky_material] saved %s: %s" % (ASSET, "ok" if ok else "FAILED"))
