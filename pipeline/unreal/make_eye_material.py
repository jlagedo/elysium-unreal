# Generates the eye master for VtMB's `Eyes` shader. The iris is not a UV-mapped feature of the
# mesh: the original renderer rebuilds an eye basis every frame from the model's StudioEyeball
# record and the character's gaze point, and hands the shader two 4-component planes the vertex
# program dots against the world position. This master reproduces that, per pixel.
#
# The planes arrive in COMPONENT-LOCAL space, not world. Dotting an absolute (LWC) world position
# against a plane whose normal is ~1.6 cm^-1 makes both terms reach ~48,000 on a large map and
# cancel to ~0.5, where float32 resolution is ~0.4% of the iris UV range -- a quantised, crawling
# iris far from the origin and a clean one near it. Since the plane's own w is
# `0.5 - dot(n, eyeOrigin)`, the algebra collapses to `dot(P - eyeOrigin, n) + 0.5`, and that
# difference is small. TransformPosition World->Local is LWC-safe and, on a skinned mesh, yields
# the post-skinning component-space position -- which is what the eyeball geometry is.
#
# `IrisOrigin` and `NormalOrigin` are deliberately two parameters. The original recomputes the
# shading origin WITHOUT the renderer's eye shift while the plane terms include it; they coincide
# only while the shift is zero.
#
# `Vampire` is a SCALAR, not a static switch: the runtime drives this through a
# UMaterialInstanceDynamic, and a MID cannot carry static parameter values -- a switch would be
# stuck at its default forever. It selects the composite order, which is the whole of the
# `$vampire` effect:
#     stock   ((sclera lerp iris) * illum)              -> the iris is lit
#     vampire ((sclera * illum) lerp iris)              -> the iris is not
# Emissive bypasses lighting, so `BaseColor = sclera*(1-a)`, `Emissive = iris*a` is not an
# approximation of the second form, it is algebraically the same expression.
#
# There is no glint. VtMB regenerated a 32x32 texture per eye per frame to fake a specular
# highlight from the first two lights; under HWRT Lumen and MegaLights the flattened eye normal
# produces one from every light. Recorded as a divergence in `docs/vtmb/facial_animation.md`.
#
# Format and the system this material serves: `docs/vtmb/facial_animation.md` -> Eyes.
import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from elysium_pipeline import mounts

PKG = mounts.MATERIALS
ASSET = PKG + "/M_Eyes"
DEFAULT_TEX = "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"

mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
white = unreal.load_asset(DEFAULT_TEX)


def connect(src, output, dst, input_name):
    if not mel.connect_material_expressions(src, output, dst, input_name):
        raise SystemExit("[make_eye_material] refused connection %s -> %s" % (output, input_name))


def connect_property(src, output, prop):
    if not mel.connect_material_property(src, output, prop):
        raise SystemExit("[make_eye_material] refused material property %s" % prop)


def node(mat, cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def texture(mat, name, x, y, clamp=False):
    n = node(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    n.set_editor_property("texture", white)
    if clamp:
        # The iris planes are unbounded: they read ~0.5 at the eye centre and keep going across the
        # rest of the eyeball, so the UV leaves [0,1] almost immediately. The iris art is a disc
        # with a fully transparent surround (alpha 0 in every corner), which is only correct under
        # CLAMP -- under wrap the disc tiles and the eye grows a ring of extra irises. Forced here
        # rather than on the texture because these are runtime-created UTexture2Ds that default to
        # wrap, and the material must not depend on how they happened to be built.
        n.set_editor_property(
            "sampler_source", unreal.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS)
    return n


def scalar(mat, name, default, x, y):
    n = node(mat, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", default)
    return n


def vec3(mat, name, default, x, y):
    """A VectorParameter masked to RGB -- the dot products below take float3."""
    p = node(mat, unreal.MaterialExpressionVectorParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", unreal.LinearColor(*default))
    m = node(mat, unreal.MaterialExpressionComponentMask, x + 180, y)
    for ch in ("r", "g", "b", "a"):
        m.set_editor_property(ch, ch in "rgb")
    connect(p, "", m, "")
    return m


def const(mat, value, x, y):
    n = node(mat, unreal.MaterialExpressionConstant, x, y)
    n.set_editor_property("r", value)
    return n


def binop(mat, cls, a, a_out, b, b_out, x, y):
    n = node(mat, cls, x, y)
    connect(a, a_out, n, "A")
    connect(b, b_out, n, "B")
    return n


def mul(mat, a, a_out, b, b_out, x, y):
    return binop(mat, unreal.MaterialExpressionMultiply, a, a_out, b, b_out, x, y)


def sub(mat, a, a_out, b, b_out, x, y):
    return binop(mat, unreal.MaterialExpressionSubtract, a, a_out, b, b_out, x, y)


def add(mat, a, a_out, b, b_out, x, y):
    return binop(mat, unreal.MaterialExpressionAdd, a, a_out, b, b_out, x, y)


def dot(mat, a, a_out, b, b_out, x, y):
    return binop(mat, unreal.MaterialExpressionDotProduct, a, a_out, b, b_out, x, y)


def lerp(mat, a, a_out, b, b_out, alpha, alpha_out, x, y):
    n = node(mat, unreal.MaterialExpressionLinearInterpolate, x, y)
    connect(a, a_out, n, "A")
    connect(b, b_out, n, "B")
    connect(alpha, alpha_out, n, "Alpha")
    return n


if unreal.EditorAssetLibrary.does_asset_exist(ASSET):
    unreal.EditorAssetLibrary.delete_asset(ASSET)
mat = tools.create_asset("M_Eyes", PKG, unreal.Material, unreal.MaterialFactoryNew())
if not mat:
    raise SystemExit("[make_eye_material] create_asset failed")

mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
# Parity with every other NPC material: VtMB character meshes have open geometry.
mat.set_editor_property("two_sided", True)
mat.set_editor_property("used_with_skeletal_mesh", True)
# REQUIRED, and the failure is delayed and confusing without it. The exporter writes the same
# morph-target list on every primitive of a rigged model, so the eye sections carry morph targets;
# the first time a face blinks, UpdateMorphMaterialUsageOnProxy swaps any material lacking this
# usage for the default grey and never swaps it back.
mat.set_editor_property("used_with_morph_targets", True)
# The eye normal below is built in world space and there is no TANGENT attribute on the exported
# mesh, so a fabricated tangent basis -- worst exactly here, on a sphere with a seam -- is avoided.
mat.set_editor_property("tangent_space_normal", False)

# --- the component-local position every plane is measured from ---------------------------------
world_pos = node(mat, unreal.MaterialExpressionWorldPosition, -1500, 0)
local_pos = node(mat, unreal.MaterialExpressionTransformPosition, -1300, 0)
local_pos.set_editor_property(
    "transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
local_pos.set_editor_property(
    "transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
connect(world_pos, "", local_pos, "")

# --- iris UV: u = dot(P_local - IrisOrigin, IrisU) + 0.5 ---------------------------------------
iris_origin = vec3(mat, "IrisOrigin", (0.0, 0.0, 0.0, 0.0), -1500, 200)
iris_u = vec3(mat, "IrisU", (1.0, 0.0, 0.0, 0.0), -1500, 340)
iris_v = vec3(mat, "IrisV", (0.0, 1.0, 0.0, 0.0), -1500, 480)
d_iris = sub(mat, local_pos, "", iris_origin, "", -1040, 120)
half = const(mat, 0.5, -1040, 620)
u = add(mat, dot(mat, d_iris, "", iris_u, "", -840, 260), "", half, "", -640, 260)
v = add(mat, dot(mat, d_iris, "", iris_v, "", -840, 420), "", half, "", -640, 420)
uv = node(mat, unreal.MaterialExpressionAppendVector, -440, 340)
connect(u, "", uv, "A")
connect(v, "", uv, "B")

iris = texture(mat, "IrisTexture", -240, 340, clamp=True)
connect(uv, "", iris, "UVs")
# glTFRuntime injects the mesh's own albedo under this name, which for an eye section is the
# sclera. Keeping the glTF parameter name is what lets the plugin's material path fill it in.
sclera = texture(mat, "baseColorTexture", -240, -220)

# --- composite ---------------------------------------------------------------------------------
vampire = scalar(mat, "Vampire", 0.0, -240, 760)
lit_iris = lerp(mat, sclera, "RGB", iris, "RGB", iris, "A", 200, -100)
base_rgb = lerp(mat, lit_iris, "", sclera, "RGB", vampire, "", 400, 0)
unlit_a = mul(mat, iris, "A", vampire, "", 200, 620)
keep = node(mat, unreal.MaterialExpressionOneMinus, 400, 620)
connect(unlit_a, "", keep, "")
base = mul(mat, base_rgb, "", keep, "", 640, 200)
connect_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
emissive = mul(mat, iris, "RGB", unlit_a, "", 640, 620)
connect_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

# --- the flattened eye normal ------------------------------------------------------------------
# n = normalize(D - Flatten * dot(D, EyeUp) * EyeUp), D = P_local - NormalOrigin. Removing part of
# the up component flattens the sphere normal vertically so the eyeball does not shade dark at the
# top and bottom. Flatten is a parameter rather than the literal 0.5 because the original's
# `$eyeup` constant is a pre-halved vector, which leaves the effective coefficient ambiguous
# between 0.5 and 0.125 -- the vertex program that would settle it ships compiled only.
normal_origin = vec3(mat, "NormalOrigin", (0.0, 0.0, 0.0, 0.0), -1500, 1000)
eye_up = vec3(mat, "EyeUpN", (0.0, 0.0, 1.0, 0.0), -1500, 1140)
flatten = scalar(mat, "Flatten", 0.5, -1500, 1280)
d_n = sub(mat, local_pos, "", normal_origin, "", -1040, 1000)
up_amount = mul(mat, dot(mat, d_n, "", eye_up, "", -840, 1100), "", flatten, "", -640, 1100)
proj = mul(mat, up_amount, "", eye_up, "", -440, 1140)
n_local = node(mat, unreal.MaterialExpressionNormalize, -60, 1040)
connect(sub(mat, d_n, "", proj, "", -240, 1040), "", n_local, "")
n_world = node(mat, unreal.MaterialExpressionTransform, 160, 1040)
n_world.set_editor_property(
    "transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
n_world.set_editor_property(
    "transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
connect(n_local, "", n_world, "")
connect_property(n_world, "", unreal.MaterialProperty.MP_NORMAL)

# --- surface response --------------------------------------------------------------------------
# The cornea is the one part of a VtMB face that should catch a highlight; it stands in for the
# procedural glint the original drew by hand.
connect_property(scalar(mat, "SpecularFactor", 0.6, 640, 1400), "",
                 unreal.MaterialProperty.MP_SPECULAR)
connect_property(scalar(mat, "Roughness", 0.15, 640, 1520), "",
                 unreal.MaterialProperty.MP_ROUGHNESS)
connect_property(scalar(mat, "Metallic", 0.0, 640, 1640), "",
                 unreal.MaterialProperty.MP_METALLIC)

mel.recompile_material(mat)
if not unreal.EditorAssetLibrary.save_asset(ASSET, only_if_is_dirty=False):
    raise SystemExit("[make_eye_material] save failed")
unreal.log("[make_eye_material] saved %s" % ASSET)
