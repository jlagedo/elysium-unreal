# Source's distance fog, as a per-primitive material term (docs/vtmb/sky-ambience.md B8b).
#
# VtMB fogs the world and the 3D-skybox miniature with two DIFFERENT linear fogs, pushed and
# popped around two separate renders (RE-A8/RE-A9). Ported as one scene the two share screen
# depth: measured over the exported set, the placed miniature's bounds sit 0-4,868 cm from the
# world's own, against world diagonals of 11,124-40,334 cm, and on 6 of the 8 maps with a
# miniature its geometry is INSIDE the world's box. So no distance-based engine mechanism --
# FogCutoffDistance, a LocalFogVolume, a second fog actor -- can tell the two apart. The fog
# has to be a per-primitive term, and a deferred fog pass has no such thing.
#
# It therefore lives in the material and is driven per primitive by Custom Primitive Data,
# which the bake writes onto every world/sky/prop component and the runtime re-derives at load:
#
#   CPD 0..3  fog colour as a float4 -- LINEAR RGB, A unused. `.env` transports the authored
#             colour verbatim (/255) and the writer decodes it with a plain 2.2, because VtMB's
#             colours are gamma-encoded and its own math decodes them that way (RE-A5). See
#             Source/ElysiumUE/Public/ElysiumFog.h for the magnitudes that settle it.
#   CPD 4     fog start, cm from the camera
#   CPD 5     1 / (end - start), or 0 for "this primitive is not fogged"
#
#   f = saturate((PixelDepth - start) * invRange)
#
# which is Source's own formula, so this reproduces the original fog rather than approximating
# it. PixelDepth is view-space depth = the `w` D3D8 pixel fog reads, not a radial distance.
#
# NEUTRAL BY CONSTRUCTION: an unwritten Custom Primitive Data slot reads as 0 (the engine
# memzeroes the whole block when a primitive carries none), so invRange 0 gives f = 0 and every
# output passes through untouched. A primitive nobody wrote to renders exactly as it did before
# the term existed.
#
# Applied so that the shaded result is exactly lerp(shaded, fogColour, f):
#
#   BaseColor *= (1 - f)                     diffuse response scales down
#   Specular   = 0.5 * (1 - f)               ...and so does the specular response, or a
#                                            Lumen reflection would shine through the fog
#   Emissive   = Emissive * (1 - f) + C * f  the surface fades out and the inscatter fades in
#
# The 2D backdrop is exempt game-wide (RE-A9: every sky face carries `$nofog 1`), so M_Sky
# never gets this term.
import unreal

mel = unreal.MaterialEditingLibrary


def connect(src, src_out, dst, dst_in):
    """connect_material_expressions, but a refused connection is an error rather than a shrug.

    It returns False for a pin name the node does not have, and the graph then compiles anyway
    against that input's constant default -- so a typo becomes a material that renders wrongly
    instead of a build that fails. (A VectorParameter, for one, has no "RGB" output.)"""
    if not mel.connect_material_expressions(src, src_out, dst, dst_in):
        raise SystemExit("[mat_fog] no connection %s.%s -> %s.%s" % (
            src.get_class().get_name(), src_out or "<out>",
            dst.get_class().get_name(), dst_in or "<in>"))



# Keep in sync with FOG_CPD_* in pipeline/unreal/bake_map.py and ElysiumFog.h in the runtime.
CPD_COLOR = 0      # float4 (consumes 0..3)
CPD_START = 4
CPD_INV_RANGE = 5

# UE's own default specular, which an unconnected Specular pin writes.
SPECULAR_DEFAULT = 0.5

# Parameter names for the material-instance variant (decals; see fog_from_params).
P_COLOR = "FogColor"
P_START = "FogStart"
P_INV_RANGE = "FogInvRange"


def _scalar(mat, name, x, y, cpd_index=None, default=0.0):
    n = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", default)
    if cpd_index is not None:
        n.set_editor_property("use_custom_primitive_data", True)
        n.set_editor_property("primitive_data_index", cpd_index)
    return n


def _vector(mat, name, x, y, cpd_index=None):
    """The fog colour as a float3. A VectorParameter is a float4 with no RGB output of its own,
    so it is masked here once -- an unmasked float4 will not coerce into Emissive."""
    n = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    n.set_editor_property("parameter_name", name)
    n.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    if cpd_index is not None:
        # A vector parameter reads a float4, so it consumes cpd_index .. cpd_index + 3.
        n.set_editor_property("use_custom_primitive_data", True)
        n.set_editor_property("primitive_data_index", cpd_index)
    rgb = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, x + 180, y)
    for channel, on in (("r", True), ("g", True), ("b", True), ("a", False)):
        rgb.set_editor_property(channel, on)
    connect(n, "", rgb, "")
    return rgb


def _term(mat, color, start, inv_range, x, y):
    """f = saturate((PixelDepth - start) * invRange); returns (f, 1 - f, colour)."""
    depth = mel.create_material_expression(mat, unreal.MaterialExpressionPixelDepth, x, y)
    past = mel.create_material_expression(mat, unreal.MaterialExpressionSubtract, x + 200, y + 40)
    connect(depth, "", past, "A")
    connect(start, "", past, "B")
    scaled = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x + 360, y + 60)
    connect(past, "", scaled, "A")
    connect(inv_range, "", scaled, "B")
    f = mel.create_material_expression(mat, unreal.MaterialExpressionSaturate, x + 500, y + 60)
    connect(scaled, "", f, "")
    inv_f = mel.create_material_expression(mat, unreal.MaterialExpressionOneMinus, x + 640, y + 60)
    connect(f, "", inv_f, "")
    return f, inv_f, color


def fog_from_primitive(mat, x=-1600, y=1560):
    """The per-primitive term: colour/start/range come from Custom Primitive Data."""
    color = _vector(mat, P_COLOR, x, y - 180, cpd_index=CPD_COLOR)
    start = _scalar(mat, P_START, x, y + 200, cpd_index=CPD_START)
    inv_range = _scalar(mat, P_INV_RANGE, x, y + 300, cpd_index=CPD_INV_RANGE)
    return _term(mat, color, start, inv_range, x + 220, y)


def fog_from_params(mat, x=-1600, y=1560):
    """The material-instance term, for a domain with no Custom Primitive Data of its own.

    A UDecalComponent is a USceneComponent, not a UPrimitiveComponent, so it carries no custom
    primitive data at all. A decal is only ever a world surface -- never miniature -- so it needs
    no per-primitive scoping either: one per-map value on the instance is the whole requirement,
    and the bake binds it from the same `worldspawn` fog every world primitive gets."""
    color = _vector(mat, P_COLOR, x, y - 180)
    start = _scalar(mat, P_START, x, y + 200)
    inv_range = _scalar(mat, P_INV_RANGE, x, y + 300)
    return _term(mat, color, start, inv_range, x + 220, y)


def fade(mat, node, out, inv_f, x, y):
    """node.out * (1 - f) -- what a surface's own contribution fades by."""
    n = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x, y)
    connect(node, out, n, "A")
    connect(inv_f, "", n, "B")
    return n


def inscatter(mat, faded, f, color, x, y):
    """faded + colour * f -- the fog's own light, added once the surface has faded out."""
    lit = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x, y + 120)
    connect(color, "", lit, "A")
    connect(f, "", lit, "B")
    n = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, x + 180, y + 40)
    connect(faded, "", n, "A")
    connect(lit, "", n, "B")
    return n


def specular(mat, inv_f, x, y, source=None):
    """spec * (1 - f). Without the fade a Lumen reflection keeps its full strength through the
    fog, because scaling BaseColor alone leaves the specular response untouched.

    `source` is the specular level being faded. The world masters hand in their own
    reflection-channel node -- lerp(SpecBase, SpecReflect, env), driven by $envmapmask
    (docs/vtmb/reflections.md). Omitted, it is UE's own 0.5 default, which is what a surface with
    no reflection channel of its own (a decal) wants."""
    if source is None:
        source = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y)
        source.set_editor_property("r", SPECULAR_DEFAULT)
    n = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, x + 180, y + 40)
    connect(source, "", n, "A")
    connect(inv_f, "", n, "B")
    return n
