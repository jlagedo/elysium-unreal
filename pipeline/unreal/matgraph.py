# The shared graph-authoring helper layer for the V2 material masters (SF-4.3a,
# `import/design/phase4_mechanics.md` -> section 3a).
#
# Every V2 master (`make_v2_materials.py`) is built from the same small vocabulary of node
# constructors so the nine families read the same way and a reviewer checks the algebra, not the
# `unreal.MaterialEditingLibrary` boilerplate around it. This module is import-safe: importing it
# does no editor work and creates no asset, so `pipeline/tests/test_matgraph.py` exercises it
# against a fake `unreal` module exactly the way `test_import_textures_editor.py` exercises
# `import_textures.py`.
#
# `connect` is `mat_fog.connect` re-exported, not reimplemented: a refused pin
# (`connect_material_expressions` returning False for a pin name a node does not have) is a build
# error here too, never a silently-wrong default.
from __future__ import annotations

import unreal

from pipeline.unreal import mat_fog

#: A refused connection is a hard error -- see `mat_fog.connect`'s docstring.
connect = mat_fog.connect

#: Default horizontal/vertical spacing between hand-placed graph nodes (mechanics doc 3a).
LAYOUT_COL = 220
LAYOUT_ROW = 90

#: Texture-parameter "kind" -> (sampler type, default texture object path). `mask` has no
# universal default -- some slots want a neutral black mask, most want linear white -- so callers
# of `Graph.tex(..., kind="mask")` pass `default=` explicitly; see `make_v2_materials.py`.
_SAMPLER_DEFAULTS = {
    "color": (
        "SAMPLERTYPE_COLOR", "/Engine/EngineResources/DefaultTexture.DefaultTexture"),
    "mask": ("SAMPLERTYPE_MASKS", None),
    "normal": (
        "SAMPLERTYPE_NORMAL", "/Engine/EngineMaterials/DefaultNormal.DefaultNormal"),
    "linear": (
        "SAMPLERTYPE_LINEAR_COLOR", "/Engine/EngineResources/DefaultTexture.DefaultTexture"),
}


def _sampler_type(name):
    return getattr(unreal.MaterialSamplerType, name)


class Graph:
    """Node-authoring surface bound to one `unreal.Material`.

    `collection` is the `MPC_ElysiumSurfaces` asset (or `None` while it does not exist yet --
    `Graph.mpc` raises rather than authoring a dangling CollectionParameter)."""

    def __init__(self, material, collection=None):
        self.mat = material
        self.collection = collection
        self.mel = unreal.MaterialEditingLibrary

    # -- raw node creation ------------------------------------------------------------------
    def node(self, cls, x, y):
        return self.mel.create_material_expression(self.mat, cls, x, y)

    # -- parameters ---------------------------------------------------------------------------
    def tex(self, name, x, y, *, kind="color", default=None):
        """A `TextureSampleParameter2D`. `kind` picks the sampler type and, unless `default`
        overrides it, the fallback texture a fresh instance samples before anything binds it."""
        sampler_name, default_path = _SAMPLER_DEFAULTS[kind]
        n = self.node(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("sampler_type", _sampler_type(sampler_name))
        path = default if default is not None else default_path
        if path:
            texture = unreal.load_asset(path)
            if texture:
                n.set_editor_property("texture", texture)
        return n

    def cube(self, name, x, y, *, default=None):
        n = self.node(unreal.MaterialExpressionTextureSampleParameterCube, x, y)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("sampler_type", _sampler_type("SAMPLERTYPE_COLOR"))
        path = default or "/Engine/EngineResources/DefaultTextureCube.DefaultTextureCube"
        texture = unreal.load_asset(path)
        if texture:
            n.set_editor_property("texture", texture)
        return n

    def tex_object(self, name, x, y, texture, *, sampler=None):
        """A `TextureObjectParameter` -- a texture handed to a `TextureSample` node rather than
        sampled directly (the class LUT, read at a computed UV with a fixed mip; the animation
        lane's `BaseTextureFrames`/`NormalMapFrames`). Unlike `tex`/`cube`,
        `MaterialExpressionTextureObjectParameter` never auto-derives a sampler type from the
        bound texture asset (confirmed against the real editor: it stays `SAMPLERTYPE_Color`
        regardless of what is assigned) -- `sampler` (one of `_SAMPLER_DEFAULTS`'s keys) sets it
        explicitly. Left `None` only where the caller does not care (there is currently no such
        caller)."""
        n = self.node(unreal.MaterialExpressionTextureObjectParameter, x, y)
        n.set_editor_property("parameter_name", name)
        if sampler is not None:
            n.set_editor_property("sampler_type", _sampler_type(_SAMPLER_DEFAULTS[sampler][0]))
        if texture:
            n.set_editor_property("texture", texture)
        return n

    def sample(self, tex_object, uv, x, y, *, mip=None):
        """A `TextureSample` fed by a `TextureObjectParameter` (see `tex_object`). `mip`, when
        given, is a constant mip level (`TMVM_MipLevel`) -- used for the class LUT, never
        filtered across its 64 texel rows."""
        n = self.node(unreal.MaterialExpressionTextureSample, x, y)
        connect(tex_object, "", n, "Tex")
        if uv is not None:
            connect(uv, "", n, "UVs")
        if mip is not None:
            n.set_editor_property(
                "mip_value_mode", unreal.TextureMipValueMode.TMVM_MIP_LEVEL)
            n.set_editor_property("const_mip_value", float(mip))
        return n

    def scalar(self, name, default, x, y, *, cpd=None):
        n = self.node(unreal.MaterialExpressionScalarParameter, x, y)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("default_value", default)
        if cpd is not None:
            n.set_editor_property("use_custom_primitive_data", True)
            n.set_editor_property("primitive_data_index", cpd)
        return n

    def vec3(self, name, default, x, y):
        """A `VectorParameter` masked to RGB -- the float4 parameter has no float3 output of its
        own, so every consumer downstream gets the masked node."""
        n = self.node(unreal.MaterialExpressionVectorParameter, x, y)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property(
            "default_value", unreal.LinearColor(default[0], default[1], default[2],
                                                 default[3] if len(default) > 3 else 1.0))
        m = self.node(unreal.MaterialExpressionComponentMask, x + LAYOUT_COL, y)
        for channel, on in (("r", True), ("g", True), ("b", True), ("a", False)):
            m.set_editor_property(channel, on)
        connect(n, "", m, "")
        return m

    def vec4(self, name, default, x, y):
        """The raw `VectorParameter`, unmasked -- for a parameter whose `.a`/4th channel is read
        downstream (`SineTargetMask`, `SineChannelMask`), where `vec3`'s RGB mask would drop it."""
        n = self.node(unreal.MaterialExpressionVectorParameter, x, y)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property(
            "default_value", unreal.LinearColor(default[0], default[1], default[2],
                                                 default[3] if len(default) > 3 else 1.0))
        return n

    def switch(self, name, true_node, false_node, x, y, default=False, *,
               true_out="", false_out=""):
        """`true_out`/`false_out` name the source output on each branch -- pass `"RGBA"` when a
        branch is fed straight from a `VectorParameter`/`TextureSample`(`Parameter`)/`VertexColor`
        node and a downstream consumer needs the 4th (alpha) channel: those three node types'
        default (`""`) output is RGB only (real-editor fact, not the C++ field name), so a `Switch`
        fed through the bare default output silently drops alpha for everything downstream of it,
        including its own default output."""
        n = self.node(unreal.MaterialExpressionStaticSwitchParameter, x, y)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("default_value", default)
        connect(true_node, true_out, n, "True")
        connect(false_node, false_out, n, "False")
        return n

    def mpc(self, name, x, y):
        if self.collection is None:
            raise SystemExit("[matgraph] no MPC_ElysiumSurfaces bound; cannot read %s" % name)
        n = self.node(unreal.MaterialExpressionCollectionParameter, x, y)
        n.set_editor_property("collection", self.collection)
        n.set_editor_property("parameter_name", name)
        return n

    # -- constants ------------------------------------------------------------------------------
    def const(self, value, x, y):
        n = self.node(unreal.MaterialExpressionConstant, x, y)
        n.set_editor_property("r", value)
        return n

    def const3(self, r, g, b, x, y):
        n = self.node(unreal.MaterialExpressionConstant3Vector, x, y)
        n.set_editor_property("constant", unreal.LinearColor(r, g, b, 0.0))
        return n

    def const4(self, r, g, b, a, x, y):
        n = self.node(unreal.MaterialExpressionConstant4Vector, x, y)
        n.set_editor_property("constant", unreal.LinearColor(r, g, b, a))
        return n

    # -- binary / unary algebra -------------------------------------------------------------
    def _binop(self, cls, a, a_out, b, b_out, x, y):
        n = self.node(cls, x, y)
        connect(a, a_out, n, "A")
        connect(b, b_out, n, "B")
        return n

    def mul(self, a, a_out, b, b_out, x, y):
        return self._binop(unreal.MaterialExpressionMultiply, a, a_out, b, b_out, x, y)

    def add(self, a, a_out, b, b_out, x, y):
        return self._binop(unreal.MaterialExpressionAdd, a, a_out, b, b_out, x, y)

    def sub(self, a, a_out, b, b_out, x, y):
        return self._binop(unreal.MaterialExpressionSubtract, a, a_out, b, b_out, x, y)

    def div(self, a, a_out, b, b_out, x, y):
        return self._binop(unreal.MaterialExpressionDivide, a, a_out, b, b_out, x, y)

    def dot(self, a, a_out, b, b_out, x, y):
        return self._binop(unreal.MaterialExpressionDotProduct, a, a_out, b, b_out, x, y)

    def pow(self, a, a_out, b, b_out, x, y):
        # Not `_binop`: `UMaterialExpressionPower` names its inputs `Base`/`Exponent`, not
        # `A`/`B`, and `connect` raises on a pin a node does not have. The exponent pin is
        # reached as `Exp`, not `Exponent`: `ConnectMaterialExpressions` matches against
        # `UMaterialGraphNode::GetShortenPinName`, which rewrites `Exponent` -> `Exp`
        # (`MaterialGraphNode.cpp:614-617`), the same shortening that makes `Coordinates`
        # reachable only as `UVs`. (R7.1: this helper had no call site until `_build_water`
        # gamma-decoded its fog colour through it, so both spellings shipped untested.)
        n = self.node(unreal.MaterialExpressionPower, x, y)
        connect(a, a_out, n, "Base")
        connect(b, b_out, n, "Exp")
        return n

    def lerp(self, a, a_out, b, b_out, alpha, alpha_out, x, y):
        n = self.node(unreal.MaterialExpressionLinearInterpolate, x, y)
        connect(a, a_out, n, "A")
        connect(b, b_out, n, "B")
        connect(alpha, alpha_out, n, "Alpha")
        return n

    def sat(self, a, a_out, x, y):
        n = self.node(unreal.MaterialExpressionSaturate, x, y)
        connect(a, a_out, n, "")
        return n

    def one_minus(self, a, a_out, x, y):
        n = self.node(unreal.MaterialExpressionOneMinus, x, y)
        connect(a, a_out, n, "")
        return n

    def floor(self, a, a_out, x, y):
        n = self.node(unreal.MaterialExpressionFloor, x, y)
        connect(a, a_out, n, "")
        return n

    def frac(self, a, a_out, x, y):
        n = self.node(unreal.MaterialExpressionFrac, x, y)
        connect(a, a_out, n, "")
        return n

    def clamp(self, a, a_out, lo, lo_out, hi, hi_out, x, y):
        n = self.node(unreal.MaterialExpressionClamp, x, y)
        # `Clamp`'s primary input pin has no name (`""`) -- confirmed against the real editor's
        # `get_material_expression_input_names`, not guessed from the C++ field name `Input`.
        connect(a, a_out, n, "")
        connect(lo, lo_out, n, "Min")
        connect(hi, hi_out, n, "Max")
        return n

    def append(self, a, a_out, b, b_out, x, y):
        n = self.node(unreal.MaterialExpressionAppendVector, x, y)
        connect(a, a_out, n, "A")
        connect(b, b_out, n, "B")
        return n

    def mask(self, a, channels, x, y, *, src_out=""):
        """`src_out` names `a`'s source output; pass `"RGBA"` when `a` is a bare
        `VectorParameter`/`TextureSample`(`Parameter`)/`VertexColor` node and `channels` needs the
        4th component -- see `switch`'s docstring for why the plain default output is not enough."""
        n = self.node(unreal.MaterialExpressionComponentMask, x, y)
        for ch in ("r", "g", "b", "a"):
            n.set_editor_property(ch, ch in channels)
        connect(a, src_out, n, "")
        return n

    # -- time / motion / surface terms -------------------------------------------------------
    def time(self, x, y):
        return self.node(unreal.MaterialExpressionTime, x, y)

    def sine(self, a, a_out, x, y):
        n = self.node(unreal.MaterialExpressionSine, x, y)
        connect(a, a_out, n, "")
        return n

    def panner(self, time_node, speed, x, y):
        n = self.node(unreal.MaterialExpressionPanner, x, y)
        connect(time_node, "", n, "Time")
        connect(speed, "", n, "Speed")
        return n

    def vertex_color(self, x, y):
        return self.node(unreal.MaterialExpressionVertexColor, x, y)

    def particle_color(self, x, y):
        """The per-particle colour a Niagara/Cascade emitter writes, for sprite/mesh-particle
        masters. NOT interchangeable with `vertex_color`: `NiagaraSpriteVertexFactory.ush`'s
        `GetMaterialPixelParameters` hardcodes `Result.VertexColor = 1` and only fills
        `Result.Particle.Color`, so a *pixel*-shader expression (Emissive, Opacity) reading
        `VertexColor` on a Niagara sprite silently reads white and the particle's own colour and
        alpha are discarded. Outputs are `""` (rgb, float3), `"R"`, `"G"`, `"B"`, `"A"` -- the
        default output is already three-wide, so it needs no ComponentMask."""
        return self.node(unreal.MaterialExpressionParticleColor, x, y)

    def dynamic_parameter(self, x, y, *, defaults=(1.0, 1.0, 1.0, 1.0), names=None, index=0):
        """A `DynamicParameter` -- the per-particle float4 a Cascade/Niagara emitter writes into
        `Particles.DynamicMaterialParameter` (Niagara's stock `DynamicMaterialParameters` module,
        bound on the renderer's `Dynamic Material Parameter Binding`). Its four outputs are named
        by `ParamNames`, so `connect(node, "Param1", ...)` picks the X lane.

        `defaults` is what every *non*-particle draw reads: `MaterialTemplate.ush`'s
        `GetDynamicParameter` returns the compiled-in default whenever the vertex factory is not a
        Niagara one, or when the emitter never writes that lane (`DynamicParameterValidMask`). So
        a default of 1.0 leaves world geometry and un-modulated emitters exactly where they were,
        and only an emitter that actually authors the ramp moves off it.

        Real-editor gotcha: `get_material_expression_output_names` lists `Param1..Param4, RGB,
        RGBA` and `connect_material_expressions(node, "Param1", ...)` succeeds on a bare material,
        but refuses (returns False) once the graph has other expressions in it -- measured, not
        guessed. Take the lane through `Graph.mask(node, "r", ..., src_out="RGBA")` instead; the
        `RGBA` output is the same float4 and the mask is stable."""
        n = self.node(unreal.MaterialExpressionDynamicParameter, x, y)
        n.set_editor_property("default_value", unreal.LinearColor(*defaults))
        n.set_editor_property("parameter_index", index)
        if names is not None:
            n.set_editor_property("param_names", [str(name) for name in names])
        return n

    def fresnel(self, x, y, *, exponent=None):
        """`exponent`, not `exponent_in` -- real-editor fact, confirmed against
        `MaterialExpressionFresnel.h`: the static property is `Exponent`, and `ExponentIn` (like
        `BaseReflectFractionIn`) is a separate *connectable* `FExpressionInput` pin, named for its
        C++ field verbatim, not a property `set_editor_property` can reach. To drive either from a
        parameter node, `connect(param, "", fresnel_node, "ExponentIn")` /
        `"BaseReflectFractionIn"` directly, bypassing this static-only helper."""
        n = self.node(unreal.MaterialExpressionFresnel, x, y)
        if exponent is not None:
            n.set_editor_property("exponent", exponent)
        return n

    def reflection_ws(self, x, y):
        return self.node(unreal.MaterialExpressionReflectionVectorWS, x, y)

    # -- material property sink -------------------------------------------------------------
    def to(self, node, out, prop):
        """Connect `node.out` to a `MaterialProperty` pin. A refused connection is an error --
        exactly the same contract as `connect`, just for the property sinks instead of an
        expression's own input pins."""
        if not self.mel.connect_material_property(node, out, prop):
            raise SystemExit("[matgraph] refused material property %s" % prop)


def class_lut_uv(graph, index_param, x, y, *, rows=128):
    """`Append((SurfaceClassIndex + 0.5) / rows, 0.5)` -- the class-LUT texel centre for a
    `rows`-row, 1-tall lookup texture. `rows` is 128 (`MaxRows`,
    `UElysiumSurfaceCalibration::MaxRows`) -- the 72 seeded class rows already exceed the earlier
    64-row placeholder (mechanics doc 1b; design doc "Class index and physical material")."""
    half = graph.const(0.5, x, y + LAYOUT_ROW)
    rows = graph.const(float(rows), x, y + 2 * LAYOUT_ROW)
    offset = graph.add(index_param, "", half, "", x + LAYOUT_COL, y)
    u = graph.div(offset, "", rows, "", x + 2 * LAYOUT_COL, y)
    v = graph.const(0.5, x + 2 * LAYOUT_COL, y + LAYOUT_ROW)
    return graph.append(u, "", v, "", x + 3 * LAYOUT_COL, y)


def read_class_lut(graph, lut_texture_object, uv, x, y):
    """Sample the class LUT at `uv` with a hard-pinned mip 0 (`TMVM_MipLevel`), so its 128 texel
    rows (`MaxRows`, see `class_lut_uv`) never blend into each other regardless of what mip chain
    the texture carries."""
    return graph.sample(lut_texture_object, uv, x, y, mip=0)
