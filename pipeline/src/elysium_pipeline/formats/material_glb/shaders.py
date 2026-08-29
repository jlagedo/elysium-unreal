"""Which shipped pixel and vertex program a material's parameters select.

The VMT names a shader *family*; the engine picks one of that family's shipped combos from the
material's parameter and flag state. `stdshader_dx8.dll` holds that decision as a short branch
cascade indexing pointer tables, and the tables' contents are the part no naming convention
states -- `docs/vtmb/shader_combos.md` owns the recovered rules and the evidence for them.

The tables below are transcribed from the shipped binary, read with
`research/tooling/probes/shader_combo_tables.py`. A family with no transcribed rule resolves
unresolved rather than guessing: only 11 of 52 family names match a `.psh` stem exactly, so a
name-to-file guess is wrong far more often than it is right.
"""

from __future__ import annotations

import dataclasses
from dataclasses import dataclass
from typing import Callable


#: A Source material flag is set by the `$<name>` parameter of the same name being present and
#: non-zero. Only the flags a transcribed selector actually reads are listed.
FLAG_PARAMETERS = {
    "VERTEXCOLOR": "$vertexcolor",
    "VERTEXALPHA": "$vertexalpha",
    "SELFILLUM": "$selfillum",
    "ADDITIVE": "$additive",
    "ALPHATEST": "$alphatest",
    "MODEL": "$model",
    "NOCULL": "$nocull",
    "DECAL": "$decal",
    "ENVMAPSPHERE": "$envmapsphere",
    "ENVMAPCAMERASPACE": "$envmapcameraspace",
    "BASEALPHAENVMAPMASK": "$basealphaenvmapmask",
    "TRANSLUCENT": "$translucent",
    "NORMALMAPALPHAENVMAPMASK": "$normalmapalphaenvmapmask",
    #: Not a Source material flag: a VtMB-only `SHADER_PARAM` the Eyes selector reads
    #: directly. Carried here so one vocabulary answers "is this switch on".
    "VAMPIRE": "$vampire",
    #: Read as a plain parameter by VertexLitGeneric's `InitShaderParams`, not as a flag bit.
    "ENVMAPOPTIONAL": "$envmapoptional",
    #: Read as a plain parameter by the Sprite selector.
    "IGNOREVERTEXCOLORS": "$ignorevertexcolors",
    #: Read as a plain parameter by the Water selector.
    "FORCECHEAP": "$forcecheap",
}


@dataclass(frozen=True)
class MaterialState:
    """What a selector reads: which parameters are bound to a texture, and the flag word."""

    #: Parameters whose value names a texture the install actually carries.
    bound: frozenset[str]
    #: Parameters the material declares at all, texture-valued or not.
    defined: frozenset[str]
    #: Material flag names that are set.
    flags: frozenset[str]
    #: Raw top-level parameter values, for the selectors that switch on a number.
    values: dict[str, str] = dataclasses.field(default_factory=dict)

    def is_bound(self, parameter: str) -> bool:
        return parameter in self.bound

    def is_defined(self, parameter: str) -> bool:
        return parameter in self.defined

    def flag(self, name: str) -> bool:
        return name in self.flags

    def value(self, parameter: str) -> str:
        return self.values.get(parameter, "")


#: Render-config conditions a selector branches on. These are engine state, not material state, so
#: one material can resolve to more than one program and the condition says which applies.
#: `mat_overbright 2` is VtMB's shipped default (`docs/vtmb/color_gamma.md`).
OVERBRIGHT_2 = "overbright==2"
#: The bump-mapping video toggle is on *and* the material binds a `$bumpmap`. It splits the
#: VertexLitGeneric draw into two passes and moves the envmap into the second.
BUMPMAPPING = "bumpmapping"
#: Inside that second pass, hardware that supports ps.1.4 takes the `_ps14` pair.
BUMPMAPPING_PS14 = "bumpmapping+ps14"
#: Water asks two hardware-capability queries and takes its "ps2.0 old" pair when both pass.
PS20 = "ps20"
DEFAULT_CONDITION = ""


@dataclass(frozen=True, slots=True)
class Program:
    """One shipped program pair the family can draw with, and when it applies.

    `draw_pass` separates alternatives from sequence: two rows sharing a condition and differing
    in `draw_pass` are both drawn, in that order, rather than one being chosen over the other.
    """

    pixel_shader: str
    vertex_shader: str
    condition: str = DEFAULT_CONDITION
    draw_pass: int = 0


@dataclass(frozen=True, slots=True)
class ShaderResolution:
    family: str
    resolved: bool
    #: Every program the material's own state admits. More than one row means the remaining
    #: choice is render configuration, not anything the VMT says.
    programs: tuple[Program, ...] = ()
    #: The parameters, flags and config the rule actually read, so a reader sees why it landed.
    inputs: tuple[str, ...] = ()
    reason: str = ""


def state_from_parameters(
    parameters, bound_textures: frozenset[str] | set[str]
) -> MaterialState:
    """Build the selector's view of a material from its top-level parameters.

    `bound_textures` names the parameters whose texture value the install resolves, because a
    selector asks `GetType() == MATERIAL_VAR_TYPE_TEXTURE`, which a missing texture fails.
    """

    defined = set()
    values: dict[str, str] = {}
    for parameter in parameters:
        if parameter.block:
            continue
        defined.add(parameter.key)
        values[parameter.key] = parameter.value
    flags = set()
    for flag, key in FLAG_PARAMETERS.items():
        raw = values.get(key)
        if raw is None:
            continue
        text = raw.strip().strip('"')
        if not text:
            flags.add(flag)                      # a valueless key is the flag itself
            continue
        try:
            if float(text) != 0.0:
                flags.add(flag)
        except ValueError:
            flags.add(flag)
    return MaterialState(
        frozenset(bound_textures), frozenset(defined), frozenset(flags), values
    )


# --------------------------------------------------------------------------------------------
# LightmappedGeneric  --  stdshader_dx8.dll, draw FUN_10006b10, pixel selector FUN_10006f50
# --------------------------------------------------------------------------------------------

#: Vertex table at 0x100214f0, indexed
#: VERTEXCOLOR | ENVMAPSPHERE<<1 | ENVMAPCAMERASPACE<<2 | ($envmap defined)<<3.
#: Indexes 2, 4 and 6 show the sphere/camera-space flags do nothing without `$envmap`, and
#: 14/15 show ENVMAPSPHERE overriding ENVMAPCAMERASPACE.
LIGHTMAPPED_VERTEX = (
    "LightmappedGeneric",
    "LightmappedGeneric_VertexColor",
    "LightmappedGeneric",
    "LightmappedGeneric_VertexColor",
    "LightmappedGeneric",
    "LightmappedGeneric_VertexColor",
    "LightmappedGeneric",
    "LightmappedGeneric_VertexColor",
    "LightmappedGeneric_EnvMap",
    "LightmappedGeneric_EnvMapVertexColor",
    "LightmappedGeneric_EnvMapSphere",
    "LightmappedGeneric_EnvMapSphereVertexColor",
    "LightmappedGeneric_EnvMapCameraSpace",
    "LightmappedGeneric_EnvMapCameraSpaceVertexColor",
    "LightmappedGeneric_EnvMapSphere",
    "LightmappedGeneric_EnvMapSphereVertexColor",
)

#: Pixel table at 0x10021620, indexed
#: SELFILLUM | BASEALPHAENVMAPMASK<<1 | ($envmapmask bound)<<2.
#: Indexes 6 and 7 are the load-bearing surprise: a bound `$envmapmask` texture overrides
#: `$basealphaenvmapmask`, so the base-alpha combo is unreachable whenever both are authored.
LIGHTMAPPED_PIXEL_ENVMAP = (
    "LightmappedGeneric_EnvMapV2",
    "LightmappedGeneric_SelfIlluminatedEnvMapV2",
    "LightmappedGeneric_BaseAlphaMaskedEnvMapV2",
    "LightmappedGeneric_SelfIlluminatedEnvMapV2",
    "LightmappedGeneric_MaskedEnvMapV2",
    "LightmappedGeneric_SelfIlluminatedMaskedEnvMapV2",
    "LightmappedGeneric_MaskedEnvMapV2",
    "LightmappedGeneric_SelfIlluminatedMaskedEnvMapV2",
)


def _lightmapped_generic(state: MaterialState) -> ShaderResolution:
    """`FUN_10006f50`, confirmed against its disassembly.

    Two details the decompiled C alone does not settle:

    - The selector's second argument gates both envmap branches (`TEST AL,AL; JNZ`). It has one
      caller, the draw routine `FUN_10006b10`, which zeroes it before the call, so the envmap
      combos are always reachable in shipped code.
    - `$envmapmask` is tested two different ways. With a base texture the selector calls
      `GetType() == TEXTURE`, so the mask must resolve to a real texture; without one it calls
      `IsDefined()`, where merely naming the parameter is enough.
    """

    inputs = (
        "$basetexture", "$envmap", "$envmapmask", "SELFILLUM", "BASEALPHAENVMAPMASK",
        "VERTEXCOLOR", "ENVMAPSPHERE", "ENVMAPCAMERASPACE",
    )
    vertex_index = (
        int(state.flag("VERTEXCOLOR"))
        | int(state.flag("ENVMAPSPHERE")) << 1
        | int(state.flag("ENVMAPCAMERASPACE")) << 2
        | int(state.is_defined("$envmap")) << 3
    )
    vertex = LIGHTMAPPED_VERTEX[vertex_index]
    if state.is_bound("$basetexture"):
        if state.is_bound("$envmap"):
            index = (
                int(state.flag("SELFILLUM"))
                | int(state.flag("BASEALPHAENVMAPMASK")) << 1
                | int(state.is_bound("$envmapmask")) << 2
            )
            pixel = LIGHTMAPPED_PIXEL_ENVMAP[index]
        elif state.flag("SELFILLUM"):
            pixel = "LightmappedGeneric_SelfIlluminated"
        else:
            pixel = "LightmappedGeneric"
    elif state.is_bound("$envmap"):
        pixel = (
            "LightmappedGeneric_MaskedEnvmapNoTexture"
            if state.is_defined("$envmapmask")
            else "LightmappedGeneric_EnvmapNoTexture"
        )
    else:
        pixel = "LightmappedGeneric_NoTexture"
    return ShaderResolution(
        "lightmappedgeneric", True, (Program(pixel, vertex),), inputs
    )


# --------------------------------------------------------------------------------------------
# VertexLitGeneric  --  stdshader_dx8.dll, draw 0x10011cf0, selectors 0x10011ea0 / 0x10011f00
# --------------------------------------------------------------------------------------------

#: Vertex table at 0x10023488, indexed
#: ($envmap bound and not suppressed) | ENVMAPSPHERE<<1 | ENVMAPCAMERASPACE<<2.
#: The programs are named for `VertexLitTexture`, not for the family.
VERTEXLIT_VERTEX = (
    "VertexLitTexture",
    "VertexLitEnvMappedTexture",
    "VertexLitTexture",
    "VertexLitEnvMappedTexture_Spheremap",
    "VertexLitTexture",
    "VertexLitEnvMappedTexture_CameraSpace",
    "VertexLitTexture",
    "VertexLitEnvMappedTexture_Spheremap",
)

#: Pixel table at 0x10023590, indexed
#: SELFILLUM | BASEALPHAENVMAPMASK<<1 | ($envmapmask bound)<<2. Same two collapses as
#: LightmappedGeneric: a bound mask overrides the base-alpha mask, and self-illumination
#: absorbs it, because both would claim the base texture's alpha channel.
VERTEXLIT_PIXEL_ENVMAP = (
    "VertexLitGeneric_EnvmapV2",
    "VertexLitGeneric_SelfIlluminatedEnvmapV2",
    "VertexLitGeneric_BaseAlphaMaskedEnvmapV2",
    "VertexLitGeneric_SelfIlluminatedEnvmapV2",
    "VertexLitGeneric_MaskedEnvmapV2",
    "VertexLitGeneric_SelfIlluminatedMaskedEnvmapV2",
    "VertexLitGeneric_MaskedEnvmapV2",
    "VertexLitGeneric_SelfIlluminatedMaskedEnvmapV2",
)


def _vertexlit_pair(state: MaterialState, envmap: bool) -> tuple[str, str]:
    """One (pixel, vertex) pair for a pass, given whether the envmap is live in it."""

    vertex = VERTEXLIT_VERTEX[
        int(envmap)
        | int(state.flag("ENVMAPSPHERE")) << 1
        | int(state.flag("ENVMAPCAMERASPACE")) << 2
    ]
    if state.is_bound("$basetexture"):
        if envmap:
            pixel = VERTEXLIT_PIXEL_ENVMAP[
                int(state.flag("SELFILLUM"))
                | int(state.flag("BASEALPHAENVMAPMASK")) << 1
                | int(state.is_bound("$envmapmask")) << 2
            ]
        elif state.flag("SELFILLUM"):
            pixel = "VertexLitGeneric_SelfIlluminated"
        else:
            pixel = "VertexLitGeneric"
    elif envmap:
        pixel = (
            "VertexLitGeneric_MaskedEnvmapNoTexture"
            if state.is_bound("$envmapmask")
            else "VertexLitGeneric_EnvmapNoTexture"
        )
    else:
        pixel = "VertexLitGeneric_NoTexture"
    return pixel, vertex


def _vertex_lit_generic(state: MaterialState) -> ShaderResolution:
    """`0x10011f00` for the pixel program and `0x10011ea0` for the vertex program.

    Two preconditions run before selection and can delete the envmap outright:

    - `$envmapoptional` non-zero calls `SetUndefined()` on `$envmap` unconditionally. Its help
      text says "only apply to dx9 and higher", but this install's `stdshader_dx9.dll` carries no
      VertexLitGeneric at all, so for VtMB it is simply an envmap kill.
    - `NORMALMAPALPHAENVMAPMASK` without a bound `$bumpmap` clears the flag and deletes `$envmap`.

    One precondition is *not* modelled because the VMT does not carry the evidence: a base texture
    whose image has no alpha channel clears `SELFILLUM` and `BASEALPHAENVMAPMASK` at load. Reading
    that needs the texture, so a material relying on it resolves to the pre-clear program here.
    """

    inputs = (
        "$basetexture", "$envmap", "$envmapmask", "$envmapoptional", "$bumpmap",
        "SELFILLUM", "BASEALPHAENVMAPMASK", "ENVMAPSPHERE", "ENVMAPCAMERASPACE",
        "NORMALMAPALPHAENVMAPMASK", BUMPMAPPING,
    )
    bump = state.is_bound("$bumpmap")
    envmap = state.is_bound("$envmap")
    if state.is_defined("$envmapoptional") and state.flag("ENVMAPOPTIONAL"):
        envmap = False
    if state.flag("NORMALMAPALPHAENVMAPMASK") and not bump:
        envmap = False

    programs = [Program(*_vertexlit_pair(state, envmap))]
    if bump:
        # The bump path draws the lit base with the envmap suppressed, then adds it back in a
        # second pass, so the base combo never folds the envmap in.
        pixel, vertex = _vertexlit_pair(state, False)
        programs.append(Program(pixel, vertex, BUMPMAPPING, 0))
        if envmap:
            multiplied = state.flag("NORMALMAPALPHAENVMAPMASK")
            programs.append(Program(
                "VertexLitGeneric_EnvmappedBumpmapV2_MultByAlpha" if multiplied
                else "VertexLitGeneric_EnvmappedBumpmapV2",
                "VertexLitGeneric_EnvmappedBumpmap_NoLighting",
                BUMPMAPPING, 1,
            ))
            programs.append(Program(
                "VertexLitGeneric_EnvmappedBumpmapV2_MultByAlpha_ps14" if multiplied
                else "VertexLitGeneric_EnvmappedBumpmapV2_ps14",
                "VertexLitGeneric_EnvmappedBumpmap_NoLighting_ps14",
                BUMPMAPPING_PS14, 1,
            ))
    return ShaderResolution("vertexlitgeneric", True, tuple(programs), inputs)


# --------------------------------------------------------------------------------------------
# UnlitGeneric  --  stdshader_dx8.dll, draw 0x10001e60, selectors 0x10001dd0 / 0x10001e10
# --------------------------------------------------------------------------------------------

#: Pixel table at 0x10020588, indexed
#: ($basetexture bound) | ($envmap bound)<<1 | ($envmap and $envmapmask bound)<<2.
#: Indexes 4 and 5 are unreachable: the mask bit cannot be set while the envmap bit is clear.
UNLIT_PIXEL = (
    "UnlitGeneric_NoTexture",
    "UnlitGeneric",
    "UnlitGeneric_EnvMapNoTexture",
    "UnlitGeneric_EnvMap",
    "UnlitGeneric_NoTexture",
    "UnlitGeneric",
    "UnlitGeneric_EnvMapMaskNoTexture",
    "UnlitGeneric_EnvMapMask",
)

#: Vertex table at 0x10020548, indexed
#: VERTEXCOLOR | ($envmap bound)<<1 | ENVMAPSPHERE<<2 | ENVMAPCAMERASPACE<<3.
#: Indexes 14 and 15 show ENVMAPSPHERE overriding ENVMAPCAMERASPACE, and every index without the
#: envmap bit falls back to the plain pair whatever the two projection flags say.
UNLIT_VERTEX = (
    "UnlitGeneric",
    "UnlitGeneric_VertexColor",
    "UnlitGeneric_EnvMap",
    "UnlitGeneric_EnvMapVertexColor",
    "UnlitGeneric",
    "UnlitGeneric_VertexColor",
    "UnlitGeneric_EnvMapSphere",
    "UnlitGeneric_EnvMapSphereVertexColor",
    "UnlitGeneric",
    "UnlitGeneric_VertexColor",
    "UnlitGeneric_EnvMapCameraSpace",
    "UnlitGeneric_EnvMapCameraSpaceVertexColor",
    "UnlitGeneric",
    "UnlitGeneric_VertexColor",
    "UnlitGeneric_EnvMapSphere",
    "UnlitGeneric_EnvMapSphereVertexColor",
)


def _unlit_generic(state: MaterialState) -> ShaderResolution:
    """`0x10001e10` for the pixel program, `0x10001dd0` for the vertex program.

    The base-alpha mask is a branch ahead of the table rather than a bit inside it, so a bound
    `$envmapmask` still wins: the branch requires the mask to be absent.
    """

    base = state.is_bound("$basetexture")
    envmap = state.is_bound("$envmap")
    mask = envmap and state.is_bound("$envmapmask")
    if envmap and not mask and base and state.flag("BASEALPHAENVMAPMASK"):
        pixel = "UnlitGeneric_BaseAlphaMaskedEnvMap"
    else:
        pixel = UNLIT_PIXEL[int(base) | int(envmap) << 1 | int(mask) << 2]
    vertex = UNLIT_VERTEX[
        int(state.flag("VERTEXCOLOR"))
        | int(envmap) << 1
        | int(state.flag("ENVMAPSPHERE")) << 2
        | int(state.flag("ENVMAPCAMERASPACE")) << 3
    ]
    return ShaderResolution(
        "unlitgeneric",
        True,
        (Program(pixel, vertex),),
        ("$basetexture", "$envmap", "$envmapmask", "BASEALPHAENVMAPMASK", "VERTEXCOLOR",
         "ENVMAPSPHERE", "ENVMAPCAMERASPACE"),
    )


# --------------------------------------------------------------------------------------------
# Sprite  --  stdshader_dx8.dll, selector 0x1000eca0
# --------------------------------------------------------------------------------------------

#: A `switch` on `$spriterendermode`, not a table. The values line up with Source's `RenderMode_t`
#: (0 normal ... 9 world glow); mode 6 is the one the shader does not implement -- it warns and
#: binds nothing. The pixel programs are Sprite's own; the vertex programs are UnlitGeneric's.
SPRITE_MODES = {
    0: ("SpriteRenderNormal", "unlitgeneric"),
    1: ("SpriteRenderTransColor", "unlitgeneric_vertexcolor"),
    2: ("SpriteRenderTransColor", "unlitgeneric_vertexcolor"),
    3: ("SpriteRenderTransColor", "unlitgeneric_vertexcolor"),
    4: ("SpriteRenderTransColor", "unlitgeneric_vertexcolor"),
    7: ("SpriteRenderTransAdd", "unlitgeneric_vertexcolor"),
    8: ("SpriteRenderTransColor", "unlitgeneric_vertexcolor"),
    9: ("SpriteRenderTransColor", "unlitgeneric_vertexcolor"),
}


def _sprite(state: MaterialState) -> ShaderResolution:
    """Mode 5 is the one case `$ignorevertexcolors` changes; mode 7 ignores it."""

    raw = state.value("$spriterendermode").strip().strip('"')
    try:
        mode = int(float(raw)) if raw else 0
    except ValueError:
        return ShaderResolution("sprite", False, reason="sprite-render-mode-is-not-a-number")
    if mode == 5:
        pixel = "SpriteRenderTransAdd"
        vertex = (
            "unlitgeneric" if state.flag("IGNOREVERTEXCOLORS") else "unlitgeneric_vertexcolor"
        )
    elif mode in SPRITE_MODES:
        pixel, vertex = SPRITE_MODES[mode]
    else:
        # The shader logs "Unknown sprite rendermode" and binds no program at all.
        return ShaderResolution(
            "sprite", False, reason=f"sprite-render-mode-{mode}-binds-no-program"
        )
    return ShaderResolution(
        "sprite", True, (Program(pixel, vertex),),
        ("$spriterendermode", "$ignorevertexcolors"),
    )


# --------------------------------------------------------------------------------------------
# UnlitTwoTexture  --  stdshader_dx8.dll, selector 0x10010940
# --------------------------------------------------------------------------------------------


def _unlit_two_texture(state: MaterialState) -> ShaderResolution:
    """No branching at all: the pair is fixed and only a numeric combo index varies.

    `$texture2` changes the blend mode and a shader boolean, never the program name.
    """

    del state
    return ShaderResolution(
        "unlittwotexture", True,
        (Program("UnlitTwoTexture", "UnlitTwoTexture"),),
        ("(fixed)",),
    )


# --------------------------------------------------------------------------------------------
# Water  --  stdshader_dx8.dll, draw 0x100138a0 (refract) / 0x10013b30 (reflect) / 0x10013d30
# --------------------------------------------------------------------------------------------


def _water(state: MaterialState) -> ShaderResolution:
    """Literal string selection, no pointer table.

    Two capability queries pick between the ps.1.1 pair and the "ps2.0 old" pair. Ahead of that,
    `$forcecheap` short-circuits to the cheap pair and skips the refract and reflect passes
    entirely -- but only when `$envmap` is also defined, which `docs/vtmb/water.md` did not state.

    The `$cheapwaterstartdistance` / `$cheapwaterenddistance` blend is not a branch here: no code
    path conditions on it, and it reaches the pixel shader as a constant for a GPU-side lerp.
    """

    inputs = ("$envmap", "$forcecheap", "$reflecttexture", PS20)
    cheap = state.is_defined("$envmap") and state.flag("FORCECHEAP")
    if cheap:
        return ShaderResolution(
            "water", True,
            (
                Program("WaterCheap_ps11", "WaterCheap_vs11"),
                Program("WaterCheap_ps20_old", "WaterCheap_vs20_old", PS20),
            ),
            inputs,
        )
    programs = [
        Program("WaterRefract_old", "WaterWarp_old"),
        Program("WaterRefract_ps20_old", "Water_vs20_old", PS20),
    ]
    if state.is_defined("$reflecttexture"):
        programs.append(Program("WaterReflect_old", "WaterWarp_old", DEFAULT_CONDITION, 1))
        programs.append(Program("WaterReflect_ps20_old", "Water_vs20_old", PS20, 1))
    return ShaderResolution("water", True, tuple(programs), inputs)


# --------------------------------------------------------------------------------------------
# Eyes  --  stdshader_dx8.dll, selector FUN_100053f0
# --------------------------------------------------------------------------------------------


def _eyes(state: MaterialState) -> ShaderResolution:
    """A flat 2x2 on `$vampire` and the overbright config; no pointer table.

    The vertex program is always `Eyes`. `$iris` never selects a program -- it only supplies the
    texture bound at stage 1 in every branch. `docs/vtmb/facial_animation.md` owns what the
    programs then compute.
    """

    vampire = state.flag("VAMPIRE")
    return ShaderResolution(
        "eyes",
        True,
        (
            Program("Eyes_Vampire_Overbright2" if vampire else "Eyes_Overbright2",
                    "Eyes", OVERBRIGHT_2),
            Program("Eyes_Vampire" if vampire else "Eyes", "Eyes"),
        ),
        ("$vampire", OVERBRIGHT_2),
    )


# --------------------------------------------------------------------------------------------
# Teeth  --  stdshader_dx8.dll, selector FUN_1000f7d0
# --------------------------------------------------------------------------------------------


def _teeth(state: MaterialState) -> ShaderResolution:
    """No material parameter selects the program; only the overbright config does.

    Teeth ships its own vertex program but **no pixel program of its own** -- it draws with the
    generic `VertexLitTexture` pair, so the pixel program is outside the family's own name.
    """

    del state
    return ShaderResolution(
        "teeth",
        True,
        (
            Program("VertexLitTexture_Overbright2", "Teeth", OVERBRIGHT_2),
            Program("VertexLitTexture", "Teeth"),
        ),
        (OVERBRIGHT_2,),
    )


#: Transcribed selectors, keyed by the lowercased VMT shader name.
RULES: dict[str, Callable[[MaterialState], ShaderResolution]] = {
    "lightmappedgeneric": _lightmapped_generic,
    "vertexlitgeneric": _vertex_lit_generic,
    "unlitgeneric": _unlit_generic,
    "sprite": _sprite,
    "unlittwotexture": _unlit_two_texture,
    "water": _water,
    "eyes": _eyes,
    "teeth": _teeth,
}


def resolve(shader: str, state: MaterialState) -> ShaderResolution:
    rule = RULES.get(shader)
    if rule is None:
        return ShaderResolution(shader, False, reason="selector-not-transcribed")
    return rule(state)
