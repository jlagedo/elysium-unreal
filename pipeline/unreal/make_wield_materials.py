# Generates the wield master-material family under Content/ElysiumGenerated/Materials/
# (docs/architecture/wielded-weapon-integration.md -> "Materials and textures"):
#
#   M_Wield              opaque weapons (the common case; most of the corpus carries $envmap)
#   M_Wield_Masked        $alphatest cutouts (the flamethrower grate, both handleclaws)
#   M_Wield_Translucent   $translucent surfaces (the Steyr magazine)
#   M_Wield_Additive      $additive glow overlays (gio_spirit)
#
# Wielded-weapon material instances previously parented to M_PlayerBody, which carries no envmap,
# envmask, bump, or translucency parameters at all -- a live probe confirmed the mismatch. This
# family is deliberately the SIMPLE version of make_world_materials.py's $envmap contract: a
# weapon master is a texture-in/PBR-out surface, not the world uber-graph, so none of that
# generator's WorldVertexTransition blending, per-primitive Source fog, or wetness/weather
# machinery is reproduced here. Every master carries exactly three texture parameters --
#   Albedo    (tex, colour)   base colour; RGB -> BaseColor, A -> Opacity/OpacityMask where blended
#   Normal    (tex, normal)   tangent-space normal ($bumpmap); RGB -> Normal
#   EnvMask   (tex, linear)   $envmap reflectivity mask; drives the Roughness/Specular response
# -- plus one scalar, EnvStrength (default 1.0: unlike the world corpus, where most surfaces carry
# no $envmap at all, most of the wield corpus does, so unbound weapons default reflective rather
# than matte). EnvMask defaults WHITE, not the engine's mottled DefaultTexture: an unmasked
# $envmap reflects uniformly, the same reasoning make_world_materials.py's header records for its
# own EnvMask contract.
#
# Rebuilt by the umbrella (uv run elysium export bundle policy -> pipeline/unreal/build_content.py, which the
# export runs); also runnable standalone:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="pipeline/unreal/make_wield_materials.py" -unattended -nosplash -nopause
import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from elysium_pipeline import mounts

PKG = mounts.MATERIALS
DEFAULT_TEX = "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"
DEFAULT_NORMAL = "/Engine/EngineMaterials/DefaultNormal.DefaultNormal"

# The Lambert base / calibrated-reflective end make_world_materials.py's $envmap contract uses
# (ROUGH_BASE/ROUGH_REFLECT/SPEC_BASE/SPEC_REFLECT there). Restated here as plain constants
# rather than imported, so this generator has no runtime dependency on that module's execution;
# they are not exposed as instance parameters -- only EnvMask and EnvStrength are, per the frozen
# contract -- so a weapon instance tunes reflectivity through the mask and its strength alone.
ROUGH_BASE = 1.0
ROUGH_REFLECT = 0.15
SPEC_BASE = 0.0
SPEC_REFLECT = 0.5

mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
white = unreal.load_asset(DEFAULT_TEX)
normal_default = unreal.load_asset(DEFAULT_NORMAL)


def connect(src, output, dst, input_name):
    if not mel.connect_material_expressions(src, output, dst, input_name):
        raise SystemExit("[make_wield_materials] refused connection %s -> %s" % (output, input_name))


def connect_property(src, output, prop):
    if not mel.connect_material_property(src, output, prop):
        raise SystemExit("[make_wield_materials] refused material property %s" % prop)


def texture(mat, name, x, y, normal=False):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal
                             else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    node.set_editor_property("texture", normal_default if normal else white)
    return node


def mask_texture(mat, name, x, y):
    """A linear mask sampler, WHITE default. White decodes to 1.0 under either the sRGB or
    linear curve, so the engine's own WhiteSquareTexture is a genuine flat default here too --
    no separate linear-mask asset needs importing, unlike make_world_materials.py's EnvMask,
    which the wet/coarse-mip machinery this generator does not reproduce also samples."""
    node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    node.set_editor_property("texture", white)
    return node


def scalar(mat, name, default, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", default)
    return node


def constant(mat, value, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property("r", value)
    return node


def multiply(mat, a, a_out, b, b_out, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x, y)
    connect(a, a_out, node, "A")
    connect(b, b_out, node, "B")
    return node


def lerp(mat, a, b, alpha, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, x, y)
    connect(a, "", node, "A")
    connect(b, "", node, "B")
    connect(alpha, "", node, "Alpha")
    return node


def build_wield_graph(mat):
    """Author the shared lit weapon-surface graph: Albedo -> BaseColor, Normal -> Normal, EnvMask
    (x EnvStrength) -> the Roughness/Specular reflection response. Returns the Albedo sampler so
    a caller can also drive Opacity/OpacityMask from its alpha."""
    albedo = texture(mat, "Albedo", -600, -120)
    connect_property(albedo, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)

    normal = texture(mat, "Normal", -600, 80, normal=True)
    connect_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)

    env_mask = mask_texture(mat, "EnvMask", -600, 260)
    env_strength = scalar(mat, "EnvStrength", 1.0, -600, 380)
    env_amt = multiply(mat, env_mask, "R", env_strength, "", -360, 300)
    env = mel.create_material_expression(mat, unreal.MaterialExpressionSaturate, -200, 300)
    connect(env_amt, "", env, "")

    rough_base = constant(mat, ROUGH_BASE, -360, 460)
    rough_reflect = constant(mat, ROUGH_REFLECT, -360, 520)
    rough = lerp(mat, rough_base, rough_reflect, env, -80, 480)
    connect_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    spec_base = constant(mat, SPEC_BASE, -360, 620)
    spec_reflect = constant(mat, SPEC_REFLECT, -360, 680)
    spec = lerp(mat, spec_base, spec_reflect, env, -80, 640)
    connect_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    return albedo


def _fresh(name):
    """Delete + recreate the asset so a re-run authors a clean graph (idempotent)."""
    asset = "%s/%s" % (PKG, name)
    if unreal.EditorAssetLibrary.does_asset_exist(asset):
        unreal.EditorAssetLibrary.delete_asset(asset)
    mat = tools.create_asset(name, PKG, unreal.Material, unreal.MaterialFactoryNew())
    if not mat:
        raise SystemExit("[make_wield_materials] create_asset failed: %s" % asset)
    # REQUIRED. Every instance of these masters is bound to a wielded-weapon USkeletalMesh
    # component; without this usage flag a cooked build cannot compile the skeletal permutation
    # on demand the way the editor does, and the weapon renders in the default grey silently.
    mat.set_editor_property("used_with_skeletal_mesh", True)
    return mat, asset


def _save(mat, asset):
    mel.recompile_material(mat)
    if not unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False):
        raise SystemExit("[make_wield_materials] save failed: %s" % asset)
    unreal.log("[make_wield_materials] saved %s" % asset)


def make_wield():
    mat, asset = _fresh("M_Wield")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    build_wield_graph(mat)
    _save(mat, asset)


def make_wield_masked():
    mat, asset = _fresh("M_Wield_Masked")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    # Matches M_World_Masked/M_PlayerBody's clip value; the manifest records which weapon
    # materials carry $alphatest (the flamethrower grate, both handleclaws) but no per-weapon
    # $alphatestreference threshold, so there is nothing more specific to reproduce yet.
    mat.set_editor_property("opacity_mask_clip_value", 0.333)
    albedo = build_wield_graph(mat)
    connect_property(albedo, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    _save(mat, asset)


def make_wield_translucent():
    mat, asset = _fresh("M_Wield_Translucent")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # Per-pixel lit translucency, matching M_World_Translucent/M_World_Glass, so the Steyr
    # magazine still takes the real-time lighting rig + Lumen rather than a flat unlit bed.
    mat.set_editor_property("translucency_lighting_mode",
                            unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    albedo = build_wield_graph(mat)
    connect_property(albedo, "A", unreal.MaterialProperty.MP_OPACITY)
    _save(mat, asset)


def make_wield_additive():
    mat, asset = _fresh("M_Wield_Additive")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

    # $additive (gio_spirit): the albedo RGB is added onto the framebuffer full-bright, weighted
    # by its own alpha as coverage -- the same two connections make_world_materials.py's
    # M_Additive makes for the identical VMT flag.
    albedo = texture(mat, "Albedo", -600, -120)
    connect_property(albedo, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    connect_property(albedo, "A", unreal.MaterialProperty.MP_OPACITY)

    # Normal/EnvMask/EnvStrength exist for parameter-name parity with the other three masters --
    # the bake binds every wield material instance by these fixed names regardless of which
    # master it parents to -- but are left unconnected: MSM_UNLIT has no Normal/Roughness/
    # Specular response for them to drive.
    texture(mat, "Normal", -600, 80, normal=True)
    mask_texture(mat, "EnvMask", -600, 260)
    scalar(mat, "EnvStrength", 1.0, -600, 380)

    _save(mat, asset)


make_wield()
make_wield_masked()
make_wield_translucent()
make_wield_additive()
