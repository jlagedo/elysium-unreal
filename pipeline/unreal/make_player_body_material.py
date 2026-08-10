# Generates the masked glTFRuntime-compatible player body master. The body is present during
# scripted cameras and fades at the true first-person endpoint through ModelAlpha. UE 5.8's
# DitherOpacityMask turns the continuous opacity mask into a temporal screen-door pattern, so the
# body remains fully lit/masked and never enters the translucent render path.
import unreal

PKG = "/Game/VtMB/Materials"
ASSET = PKG + "/M_PlayerBody"
DEFAULT_TEX = "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"
DEFAULT_NORMAL = "/Engine/EngineMaterials/DefaultNormal.DefaultNormal"

mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
white = unreal.load_asset(DEFAULT_TEX)
normal_default = unreal.load_asset(DEFAULT_NORMAL)


def connect(src, output, dst, input_name):
    if not mel.connect_material_expressions(src, output, dst, input_name):
        raise SystemExit("[make_player_body_material] refused connection %s -> %s" % (output, input_name))


def connect_property(src, output, prop):
    if not mel.connect_material_property(src, output, prop):
        raise SystemExit("[make_player_body_material] refused material property %s" % prop)


def texture(mat, name, x, y, normal=False):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal
                             else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    node.set_editor_property("texture", normal_default if normal else white)
    return node


def scalar(mat, name, default, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def vector(mat, name, default, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", unreal.LinearColor(*default))
    return node


def mask(mat, source, channels, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x, y)
    for channel in ("r", "g", "b", "a"):
        node.set_editor_property(channel, channel in channels)
    connect(source, "", node, "")
    return node


def multiply(mat, a, a_out, b, b_out, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x, y)
    connect(a, a_out, node, "A")
    connect(b, b_out, node, "B")
    return node


if unreal.EditorAssetLibrary.does_asset_exist(ASSET):
    unreal.EditorAssetLibrary.delete_asset(ASSET)
mat = tools.create_asset("M_PlayerBody", PKG, unreal.Material, unreal.MaterialFactoryNew())
if not mat:
    raise SystemExit("[make_player_body_material] create_asset failed")
mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
mat.set_editor_property("two_sided", True)
mat.set_editor_property("used_with_skeletal_mesh", True)
# REQUIRED, and the failure is delayed and confusing without it. A rigged model carries its
# morph-target list on every primitive, so every body section has morph targets; the first time a
# face blinks, UpdateMorphMaterialUsageOnProxy swaps any material lacking this usage for the
# default grey and never swaps it back.
mat.set_editor_property("used_with_morph_targets", True)
# REQUIRED for the same reason, one flag over. A garment reaches the frame on a cloth component,
# and FSkeletalMeshSceneProxy gates a cloth section on MATUSAGE_Clothing -- without it the section
# draws in the engine's default grey checker. The editor adds the usage and recompiles on demand,
# so this only fails in `-game`, where the material cannot be recompiled and the fallback is
# permanent. Every character body material is a garment material on the models that author one.
mat.set_editor_property("used_with_clothing", True)
mat.set_editor_property("dither_opacity_mask", True)
mat.set_editor_property("opacity_mask_clip_value", 0.333)

base_tex = texture(mat, "baseColorTexture", -1000, -240)
base_factor = vector(mat, "baseColorFactor", (1.0, 1.0, 1.0, 1.0), -1000, -60)
base_rgb = mask(mat, base_factor, "rgb", -760, -80)
base = multiply(mat, base_tex, "RGB", base_rgb, "", -500, -200)
connect_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

model_alpha = scalar(mat, "ModelAlpha", 1.0, -760, 120)
# Characters in this pipeline are opaque; the glTF baseColorFactor alpha is not an authored body
# fade. Reading A from the vector parameter compiled as a float3 preshader under SM6, so opacity is
# the texture's authored mask multiplied only by the runtime camera fade.
opacity = multiply(mat, base_tex, "A", model_alpha, "", -260, 80)
connect_property(opacity, "", unreal.MaterialProperty.MP_OPACITY_MASK)

mr_tex = texture(mat, "metallicRoughnessTexture", -1000, 360)
# glTF's own default for this factor is 1.0, and that default is wrong for every body this master
# serves: VtMB characters are skin and cloth with no metal, `mdl_gltf.py` writes `metallicFactor: 0`
# into each `.glb`, and the bake binds `baseColorTexture` alone -- so an instance inherits whatever
# stands here. At 1.0, with the unbound `metallicRoughnessTexture` sampling white, Metallic resolves
# to 1 and the body has no diffuse at all: it renders black under every light while its base colour
# still reads correctly in a base-colour view.
metal_factor = scalar(mat, "metallicFactor", 0.0, -760, 300)
rough_factor = scalar(mat, "roughnessFactor", 1.0, -760, 440)
metal = multiply(mat, mr_tex, "B", metal_factor, "", -500, 320)
rough = multiply(mat, mr_tex, "G", rough_factor, "", -500, 460)
connect_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
connect_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

normal_tex = texture(mat, "normalTexture", -1000, 620, normal=True)
connect_property(normal_tex, "RGB", unreal.MaterialProperty.MP_NORMAL)

emissive_tex = texture(mat, "emissiveTexture", -1000, 820)
emissive_factor = vector(mat, "emissiveFactor", (0.0, 0.0, 0.0, 1.0), -1000, 1000)
emissive_rgb = mask(mat, emissive_factor, "rgb", -760, 980)
emissive = multiply(mat, emissive_tex, "RGB", emissive_rgb, "", -500, 860)
connect_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

specular = scalar(mat, "specularFactor", 0.5, -500, 1120)
connect_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)

mel.recompile_material(mat)
if not unreal.EditorAssetLibrary.save_asset(ASSET, only_if_is_dirty=False):
    raise SystemExit("[make_player_body_material] save failed")
unreal.log("[make_player_body_material] saved %s" % ASSET)
