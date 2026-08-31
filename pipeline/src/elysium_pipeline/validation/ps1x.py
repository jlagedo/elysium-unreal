"""A pure-numpy ps.1.1 / ps.1.4 pixel-shader interpreter -- SF-4.6's parity oracle.

`Program` parses one `vtmb:shader-source:` unit's `ELYSIUM_vtmb_shader_source.source` block (the
schema `docs/architecture/seam_map_shader_program.md` documents) into a closed instruction list.
`evaluate` runs that program over `(N, 4)` float32 register arrays and returns `r0`, the pixel
colour every ps.1.x program's compiled twin ultimately writes.

This interpreter compares *algebra*, not compiled HLSL: it has no notion of lighting, of the
fixed-function pipeline state VtMB's engine sets around a shader, or of anything the compiled
`.vcs` combo table selects by `$`-flag. It exists to let a later seam (SF-4.3's Unreal-graph
dumper) assert that a hand-authored Unreal material reproduces the same post-lighting terms the
shipped ps.1.x program computes, on fixed synthetic inputs -- nothing more.

Register file
--------------
Four classes, matching the unit's `registerClass` spellings exactly (`"temp"`, `"input"`,
`"const"`, `"texture"`): `r0-1` / `r0-5` (ps.1.1 / ps.1.4), `t0-3` / `t0-5`, `c0-7`, `v0-1`. `c*`
is seeded from the unit's `defines[]` first, then by the caller's `constants` mapping, which
overrides a define at the same index. `t*` is populated only by `texcoord`/`tex` and the family
of instructions that read or write it; a stage no instruction ever samples reads as zero.

Source operand read order
--------------------------
`selector` (swizzle, including the single-letter replicate and the ps.1.4 arbitrary four-letter
swizzle) -> `negate` (`-x`) -> `complement` (`1-x`) -> source `modifier` (`_bx2 = (x-0.5)*2`,
`_bias = x-0.5`, `_x2 = x*2`). This is the literal order SF-4.6's design states; it is not always
the order a Direct3D 9 driver applies its atomic source-modifier encoding, but it is the order
this oracle commits to; the difference only shows up on operands stacking negate with a
modifier, `-r4_bx2` and its kin, which the corpus barely uses.

Destination write
------------------
Instruction modifiers `_x2 _x4 _x8 _d2 _d4 _d8` scale the computed value, `_sat` then clamps to
`[0, 1]`; every write is then clamped to the version's register range regardless of `_sat`
(`[-1, 1]` for ps.1.1, `[-8, 8]` for ps.1.4 per this deliverable's contract). Only the channels
named by the destination write mask are merged into the register; the rest keep their prior
value.

Co-issue
--------
`coIssued` carries no execution semantics here -- ps.1.x co-issue is a scheduling hint for
parallel colour/alpha pipes, not a data dependency this algebra needs to model. What *is*
checked: a co-issued instruction and the instruction immediately before it must write disjoint
channels of the same destination register, because two pipes racing to write the same channel is
not a real ps.1.x program. A unit that violates this is a `Ps1xError`, not silently accepted.

Excluded opcode families
-------------------------
`texm3x2*`, `texm3x3*`, `texreg2*`, `texdp3*` -- the matrix/register-combiner family that reads
one stage's sampled colour as a 3x3 (or 2x2) row and needs cross-instruction state this
interpreter does not track -- raise `Ps1xUnsupported` at parse time, naming the opcode and the
unit. Every other opcode absent from the supported list (`nop rcp rsq min max slt sge exp log
lit dst frc expp logp def bem texdepth`, and anything else the closed ps.1.x vocabulary admits)
raises the same way; none of them appear in the shipped corpus.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Mapping, Sequence

import numpy as np

from elysium_pipeline.formats.unit_contract import read_glb

#: The glTF extension name the shader-source unit publishes its parse under.
EXTENSION_NAME = "ELYSIUM_vtmb_shader_source"

#: `registerClass` spellings the writer publishes -- full words, not the `.psh` letters.
_TEMP, _INPUT, _CONST, _TEXTURE = "temp", "input", "const", "texture"
_REGISTER_CLASSES = (_TEMP, _INPUT, _CONST, _TEXTURE)

_CHANNEL_INDEX = {"r": 0, "g": 1, "b": 2, "a": 3, "x": 0, "y": 1, "z": 2, "w": 3}

_SCALE_MODIFIERS = {"_x2": 2.0, "_x4": 4.0, "_x8": 8.0, "_d2": 0.5, "_d4": 0.25, "_d8": 0.125}

_ALU_OPS = frozenset({"mov", "add", "sub", "mul", "mad", "lrp", "dp3", "dp4", "cnd", "cmp"})
_TEX_ALIASES = {"texcrd": "texcoord", "texld": "tex"}
_TEX_OPS = frozenset({"tex", "texkill", "texcoord", "texbem", "texbeml", "phase"})
_SUPPORTED_OPCODES = _ALU_OPS | _TEX_OPS | frozenset(_TEX_ALIASES)
_EXCLUDED_PREFIXES = ("texm3x2", "texm3x3", "texreg2", "texdp3")


class Ps1xError(ValueError):
    """A program or an evaluation call violates the ps.1.x contract this interpreter enforces."""


class Ps1xUnsupported(Ps1xError):
    """An opcode (or source modifier) outside this interpreter's modelled vocabulary.

    Raised by design for the `texm3x2*` / `texm3x3*` / `texreg2*` / `texdp3*` family and for any
    other opcode the closed ps.1.x vocabulary admits that this interpreter does not implement.
    Never raised for a bug -- it names the excluded opcode and the unit that used it. `opcode`
    carries the raw mnemonic so a caller (the corpus census, say) does not need to parse it back
    out of the message.
    """

    def __init__(self, message: str, *, opcode: str | None = None) -> None:
        super().__init__(message)
        self.opcode = opcode


# --------------------------------------------------------------------------------- instructions


@dataclass(frozen=True, slots=True)
class _Register:
    cls: str
    index: int


@dataclass(frozen=True, slots=True)
class _Source:
    register: _Register
    selector: str | None
    negate: bool
    complement: bool
    modifier: str | None


@dataclass(frozen=True, slots=True)
class _Destination:
    register: _Register
    write_mask: str


@dataclass(frozen=True, slots=True)
class Instruction:
    """One parsed instruction: the same field shape the unit's `instructions[]` row publishes."""

    line: int
    co_issued: bool
    opcode: str
    modifiers: tuple[str, ...]
    destination: _Destination | None
    sources: tuple[_Source, ...]


def _parse_register(row: dict[str, Any]) -> _Register:
    cls = row["registerClass"]
    if cls not in _REGISTER_CLASSES:
        raise Ps1xError(f"unrecognized registerClass {cls!r}")
    return _Register(cls=cls, index=int(row["registerIndex"]))


def _parse_source(row: dict[str, Any]) -> _Source:
    return _Source(
        register=_parse_register(row),
        selector=row.get("selector") or None,
        negate=bool(row.get("negate", False)),
        complement=bool(row.get("complement", False)),
        modifier=row.get("modifier") or None,
    )


def _parse_destination(row: dict[str, Any] | None) -> _Destination | None:
    if row is None:
        return None
    return _Destination(register=_parse_register(row), write_mask=row.get("writeMask") or "")


#: Per-version register-file sizes (last valid index), c* and v* fixed by the design.
_MAX_CONST = 7
_MAX_INPUT = 1


def _max_temp(version: tuple[int, int]) -> int:
    return 5 if version >= (1, 4) else 1


def _max_texture(version: tuple[int, int]) -> int:
    return 5 if version >= (1, 4) else 3


# --------------------------------------------------------------------------------- Program


@dataclass(frozen=True, slots=True)
class Program:
    """A closed ps.1.x instruction list, ready to `evaluate`.

    Construction is where every contract this interpreter enforces at parse time is checked:
    the closed opcode vocabulary, the excluded opcode families, register-file bounds for the
    declared version, and co-issue disjointness.
    """

    version: tuple[int, int]
    defines: dict[int, tuple[float, float, float, float]]
    instructions: tuple[Instruction, ...]
    unit_name: str = "<unit>"

    def __post_init__(self) -> None:
        self._check_registers()
        self._check_opcodes()
        self._check_co_issue()

    def _check_registers(self) -> None:
        max_temp, max_texture = _max_temp(self.version), _max_texture(self.version)
        limits = {_TEMP: max_temp, _INPUT: _MAX_INPUT, _CONST: _MAX_CONST, _TEXTURE: max_texture}
        for instr in self.instructions:
            registers = [instr.destination.register] if instr.destination else []
            registers += [source.register for source in instr.sources]
            for register in registers:
                limit = limits[register.cls]
                if register.index > limit:
                    raise Ps1xError(
                        f"{register.cls}{register.index} exceeds the ps.{self.version[0]}."
                        f"{self.version[1]} register file (unit {self.unit_name!r})"
                    )

    def _check_opcodes(self) -> None:
        for instr in self.instructions:
            opcode = instr.opcode
            if opcode in _SUPPORTED_OPCODES:
                continue
            if opcode.startswith(_EXCLUDED_PREFIXES):
                raise Ps1xUnsupported(
                    f"opcode {opcode!r} is excluded from the ps.1.x interpreter by design "
                    f"(unit {self.unit_name!r})",
                    opcode=opcode,
                )
            raise Ps1xUnsupported(
                f"opcode {opcode!r} is not supported by the ps.1.x interpreter "
                f"(unit {self.unit_name!r})",
                opcode=opcode,
            )

    def _check_co_issue(self) -> None:
        for index, instr in enumerate(self.instructions):
            if not instr.co_issued or index == 0:
                continue
            previous = self.instructions[index - 1]
            if instr.destination is None or previous.destination is None:
                continue
            if instr.destination.register != previous.destination.register:
                continue
            mask_a = set(previous.destination.write_mask or "rgba")
            mask_b = set(instr.destination.write_mask or "rgba")
            if mask_a & mask_b:
                raise Ps1xError(
                    f"co-issued instructions at lines {previous.line} and {instr.line} write "
                    f"overlapping channels of the same destination (unit {self.unit_name!r})"
                )

    @classmethod
    def from_unit(cls, extension_json: dict[str, Any], *, unit_name: str = "<unit>") -> "Program":
        """Parse the `ELYSIUM_vtmb_shader_source` extension body (the `source` block's parent)."""

        source = extension_json["source"]
        version_row = source.get("version") or {}
        version = (int(version_row.get("major", 1)), int(version_row.get("minor", 1)))
        defines: dict[int, tuple[float, float, float, float]] = {}
        for row in source.get("defines") or []:
            register = _parse_register(row)
            values = tuple(float(v) for v in row["values"])
            defines[register.index] = values  # type: ignore[assignment]
        instructions = tuple(
            Instruction(
                line=int(row.get("line", -1)),
                co_issued=bool(row.get("coIssued", False)),
                opcode=str(row["opcode"]),
                modifiers=tuple(row.get("modifiers") or ()),
                destination=_parse_destination(row.get("destination")),
                sources=tuple(_parse_source(s) for s in row.get("sources") or ()),
            )
            for row in source.get("instructions") or []
        )
        return cls(version=version, defines=defines, instructions=instructions, unit_name=unit_name)

    @classmethod
    def from_glb(cls, path: str | Path) -> "Program":
        """Parse a published `source/*.glb` unit straight off disk."""

        path = Path(path)
        document, _bin = read_glb(path)
        extension = (document.get("extensions") or {}).get(EXTENSION_NAME)
        if extension is None:
            raise Ps1xError(f"{path} does not publish the {EXTENSION_NAME} extension")
        return cls.from_unit(extension, unit_name=path.stem)


# --------------------------------------------------------------------------------- evaluation


class Ps1xResult(np.ndarray):
    """`r0`, `(N, 4)` float32 -- plus the `texkill` mask, since a plain array has no room for it.

    Ordinary numpy code treats this as a plain `(N, 4)` array. `killed` is the extra: a `(N,)`
    boolean array, True for every pixel some `texkill` instruction observed a negative register
    component for. A program with no `texkill` returns an all-`False` mask.
    """

    killed: np.ndarray

    def __array_finalize__(self, obj: Any) -> None:
        if obj is None:
            return
        self.killed = getattr(obj, "killed", np.zeros(0, dtype=bool))


def _broadcast4(value: Any, n: int) -> np.ndarray:
    array = np.asarray(value, dtype=np.float32)
    if array.ndim == 1 and array.shape[0] == 4:
        return np.broadcast_to(array, (n, 4)).astype(np.float32).copy()
    if array.ndim == 2 and array.shape == (n, 4):
        return array.astype(np.float32).copy()
    raise Ps1xError(f"expected a (4,) or ({n}, 4) array, got shape {array.shape}")


def _pad4(value: Any, n: int) -> np.ndarray:
    array = np.asarray(value, dtype=np.float32)
    if array.ndim == 1:
        array = array.reshape(n, 1)
    if array.shape[0] != n:
        raise Ps1xError(f"expected {n} rows, got {array.shape[0]}")
    width = array.shape[1]
    if width < 4:
        array = np.concatenate([array, np.zeros((n, 4 - width), dtype=np.float32)], axis=1)
    elif width > 4:
        array = array[:, :4]
    return array.astype(np.float32)


class _RegisterFile:
    """The four register classes, lazily zero-filled on first read of an untouched index."""

    def __init__(self, n: int) -> None:
        self.n = n
        self._tables: dict[str, dict[int, np.ndarray]] = {cls: {} for cls in _REGISTER_CLASSES}

    def get(self, cls: str, index: int) -> np.ndarray:
        table = self._tables[cls]
        if index not in table:
            table[index] = np.zeros((self.n, 4), dtype=np.float32)
        return table[index]

    def set(self, cls: str, index: int, value: np.ndarray) -> None:
        self._tables[cls][index] = value

    def seed(self, cls: str, index: int, value: np.ndarray) -> None:
        self._tables[cls][index] = value


def _apply_selector(raw: np.ndarray, selector: str | None) -> np.ndarray:
    if not selector:
        return raw
    try:
        if len(selector) == 1:
            column = raw[:, _CHANNEL_INDEX[selector] : _CHANNEL_INDEX[selector] + 1]
            return np.repeat(column, 4, axis=1)
        letters = selector
        if len(letters) < 4:
            letters = letters + letters[-1] * (4 - len(letters))
        else:
            letters = letters[:4]
        columns = [_CHANNEL_INDEX[letter] for letter in letters]
    except KeyError as exc:
        raise Ps1xError(f"unrecognized swizzle letter in selector {selector!r}") from exc
    return raw[:, columns]


def _read_source(regs: _RegisterFile, source: _Source) -> np.ndarray:
    raw = regs.get(source.register.cls, source.register.index)
    value = _apply_selector(raw, source.selector)
    if source.negate:
        value = -value
    if source.complement:
        value = 1.0 - value
    if source.modifier is not None:
        if source.modifier == "_bx2":
            value = (value - 0.5) * 2.0
        elif source.modifier == "_bias":
            value = value - 0.5
        elif source.modifier == "_x2":
            value = value * 2.0
        else:
            raise Ps1xUnsupported(f"source modifier {source.modifier!r} is not supported")
    return value


def _write(
    regs: _RegisterFile,
    destination: _Destination | None,
    value: np.ndarray,
    modifiers: Sequence[str],
    clamp: tuple[float, float],
) -> None:
    if destination is None:
        return
    for modifier in modifiers:
        scale = _SCALE_MODIFIERS.get(modifier)
        if scale is not None:
            value = value * scale
    if "_sat" in modifiers:
        value = np.clip(value, 0.0, 1.0)
    value = np.clip(value, clamp[0], clamp[1])
    current = regs.get(destination.register.cls, destination.register.index).copy()
    mask = destination.write_mask or "rgba"
    for letter in mask:
        column = _CHANNEL_INDEX[letter]
        current[:, column] = value[:, column]
    regs.set(destination.register.cls, destination.register.index, current)


def _eval_alu(regs: _RegisterFile, instr: Instruction, clamp: tuple[float, float]) -> None:
    values = [_read_source(regs, source) for source in instr.sources]
    opcode = instr.opcode
    if opcode == "mov":
        (result,) = values
    elif opcode == "add":
        result = values[0] + values[1]
    elif opcode == "sub":
        result = values[0] - values[1]
    elif opcode == "mul":
        result = values[0] * values[1]
    elif opcode == "mad":
        result = values[0] * values[1] + values[2]
    elif opcode == "lrp":
        # D3D9: lrp dst, t, a, b == t*a + (1-t)*b
        t, a, b = values
        result = t * a + (1.0 - t) * b
    elif opcode == "dp3":
        dot = np.sum(values[0][:, :3] * values[1][:, :3], axis=1, keepdims=True)
        result = np.repeat(dot, 4, axis=1)
    elif opcode == "dp4":
        dot = np.sum(values[0] * values[1], axis=1, keepdims=True)
        result = np.repeat(dot, 4, axis=1)
    elif opcode == "cnd":
        # ps.1.x: cnd dst, r0.a, src1, src2 -- per-component select on cond > 0.5.
        cond, a, b = values
        result = np.where(cond > 0.5, a, b)
    elif opcode == "cmp":
        # cmp dst, src0, src1, src2 -- per-component select on src0 >= 0.
        cond, a, b = values
        result = np.where(cond >= 0.0, a, b)
    else:  # pragma: no cover - guarded by Program._check_opcodes at parse time
        raise Ps1xUnsupported(f"opcode {opcode!r} is not an ALU op this interpreter models")
    _write(regs, instr.destination, result, instr.modifiers, clamp)


def _texture_stage_of(instr: Instruction) -> int:
    if instr.sources and instr.sources[0].register.cls == _TEXTURE:
        return instr.sources[0].register.index
    if instr.destination is not None:
        return instr.destination.register.index
    raise Ps1xError("instruction names no texture stage to read")


def _eval_texcoord(regs: _RegisterFile, instr: Instruction, texcoords: Mapping[int, Any], clamp: tuple[float, float]) -> None:
    stage = _texture_stage_of(instr)
    uv = texcoords.get(stage)
    if uv is None:
        raise Ps1xError(f"texcoord references stage t{stage} but no texcoords were supplied")
    array = np.asarray(uv, dtype=np.float32)
    n = array.shape[0]
    _write(regs, instr.destination, _pad4(array, n), instr.modifiers, clamp)


def _eval_tex(
    regs: _RegisterFile,
    instr: Instruction,
    textures: Mapping[int, Callable[[np.ndarray], np.ndarray]],
    texcoords: Mapping[int, Any],
    clamp: tuple[float, float],
) -> None:
    if instr.sources and instr.sources[0].register.cls != _TEXTURE:
        # ps.1.4 phase-2 form: `texld rN, rM` samples using rM's value as arbitrary UV
        # coordinates; the texture unit sampled is the destination register's own index, the
        # documented ps.1.4 convention (one texture stage per r-register slot).
        source = instr.sources[0]
        stage = instr.destination.register.index if instr.destination else source.register.index
        uv = regs.get(source.register.cls, source.register.index)
    else:
        stage = _texture_stage_of(instr)
        stage_uv = texcoords.get(stage)
        if stage_uv is None:
            raise Ps1xError(f"tex t{stage} has no texcoords supplied for that stage")
        uv = _pad4(np.asarray(stage_uv, dtype=np.float32), np.asarray(stage_uv).shape[0])
    sampler = textures.get(stage)
    if sampler is None:
        raise Ps1xError(f"no texture callable supplied for stage {stage}")
    sample = np.asarray(sampler(uv), dtype=np.float32)
    sample = _pad4(sample, sample.shape[0])
    _write(regs, instr.destination, sample, instr.modifiers, clamp)


def _eval_texkill(regs: _RegisterFile, instr: Instruction, kill_mask: np.ndarray) -> None:
    if instr.destination is None:
        raise Ps1xError("texkill names no register to test")
    register = instr.destination.register
    value = regs.get(register.cls, register.index)
    kill_mask |= np.any(value < 0.0, axis=1)


def _eval_texbem(
    regs: _RegisterFile,
    instr: Instruction,
    textures: Mapping[int, Callable[[np.ndarray], np.ndarray]],
    texcoords: Mapping[int, Any],
    bumpenv: Mapping[int, Sequence[float]] | None,
    clamp: tuple[float, float],
) -> None:
    """`texbem`/`texbeml`: perturb stage-`n`'s texcoords by the previous stage's r,g through a
    2x2 bump-environment matrix, then sample; `texbeml` additionally scales by a per-pixel
    luminance factor. `bumpenv[n]` supplies `(bem00, bem01, bem10, bem11[, lumScale, lumOffset])`
    -- the D3D `D3DTSS_BUMPENVMAT*` / `D3DTSS_BUMPENVL*` texture-stage state, passed by the
    caller the way `constants`/`texcoords` are, since no unit in the corpus carries it inline.
    """

    if instr.destination is None or not instr.sources:
        raise Ps1xError(f"{instr.opcode} needs both a destination stage and a perturbation source")
    stage = instr.destination.register.index
    env = None if bumpenv is None else bumpenv.get(stage)
    if env is None:
        raise Ps1xError(f"{instr.opcode} on stage t{stage} needs a bump-environment matrix")
    bem00, bem01, bem10, bem11 = (float(v) for v in env[:4])
    perturb = regs.get(instr.sources[0].register.cls, instr.sources[0].register.index)
    base_uv = texcoords.get(stage)
    if base_uv is None:
        raise Ps1xError(f"{instr.opcode} t{stage} has no base texcoords supplied")
    base = _pad4(np.asarray(base_uv, dtype=np.float32), np.asarray(base_uv).shape[0])
    u = base[:, 0] + bem00 * perturb[:, 0] + bem01 * perturb[:, 1]
    v = base[:, 1] + bem10 * perturb[:, 0] + bem11 * perturb[:, 1]
    uv = np.stack([u, v, base[:, 2], base[:, 3]], axis=1)
    sampler = textures.get(stage)
    if sampler is None:
        raise Ps1xError(f"no texture callable supplied for stage {stage}")
    sample = _pad4(np.asarray(sampler(uv), dtype=np.float32), uv.shape[0])
    if instr.opcode == "texbeml":
        if len(env) < 6:
            raise Ps1xError("texbeml needs a 6-value bump environment (matrix + luminance)")
        lum_scale, lum_offset = float(env[4]), float(env[5])
        sample = sample * (perturb[:, 2] * lum_scale + lum_offset).reshape(-1, 1)
    _write(regs, instr.destination, sample, instr.modifiers, clamp)


def evaluate(
    program: Program,
    *,
    textures: Mapping[int, Callable[[np.ndarray], np.ndarray]],
    constants: Mapping[int, Any],
    vertex: Mapping[int, Any],
    texcoords: Mapping[int, Any],
    bumpenv: Mapping[int, Sequence[float]] | None = None,
) -> Ps1xResult:
    """Run `program` over `(N, 4)` float32 registers and return `r0`.

    `textures[n]` is called with the `(N, 4)` UV array a `tex`/`texld` instruction samples stage
    `n` with, and must return an `(N, k)` array (`k` in `{1..4}`, padded with zeros). `constants`
    and `vertex` seed `c*`/`v*`; a `constants` entry overrides a same-index `defines[]` value.
    `texcoords[n]` is the raw per-vertex UV `texcoord`/`texcrd` copies into a register, and is
    also read as a `tex` instruction's UV when no earlier `tex` sampled that stage. `bumpenv`
    supplies the D3D bump-environment matrix `texbem`/`texbeml` needs (see `_eval_texbem`).

    Returns a `Ps1xResult` -- a plain `(N, 4)` array for callers that only want colour, with a
    `.killed` `(N,)` boolean mask attached for callers that care about `texkill`.
    """

    n = _infer_n(constants, vertex, texcoords)
    clamp = (-1.0, 1.0) if program.version < (1, 4) else (-8.0, 8.0)
    regs = _RegisterFile(n)
    for index, values in program.defines.items():
        regs.seed(_CONST, index, np.broadcast_to(np.array(values, dtype=np.float32), (n, 4)).copy())
    for index, value in constants.items():
        regs.seed(_CONST, index, _broadcast4(value, n))
    for index, value in vertex.items():
        regs.seed(_INPUT, index, _broadcast4(value, n))

    kill_mask = np.zeros(n, dtype=bool)
    for instr in program.instructions:
        opcode = _TEX_ALIASES.get(instr.opcode, instr.opcode)
        if opcode in _ALU_OPS:
            _eval_alu(regs, instr, clamp)
        elif opcode == "phase":
            continue
        elif opcode == "texcoord":
            _eval_texcoord(regs, instr, texcoords, clamp)
        elif opcode == "tex":
            _eval_tex(regs, instr, textures, texcoords, clamp)
        elif opcode == "texkill":
            _eval_texkill(regs, instr, kill_mask)
        elif opcode in ("texbem", "texbeml"):
            _eval_texbem(regs, instr, textures, texcoords, bumpenv, clamp)
        else:  # pragma: no cover - Program construction already refused anything else
            raise Ps1xUnsupported(f"opcode {instr.opcode!r} is not supported")

    r0 = regs.get(_TEMP, 0).astype(np.float32).copy().view(Ps1xResult)
    r0.killed = kill_mask
    return r0


def _infer_n(*mappings: Mapping[int, Any]) -> int:
    """N comes from any `(N, k)` array in the inputs; a bare `(4,)` vector is a per-channel
    constant meant to broadcast, not a one-row batch, so it is not a candidate here.
    """

    for mapping in mappings:
        for value in mapping.values():
            array = np.asarray(value)
            if array.ndim == 2:
                return array.shape[0]
    return 1


def evaluate_graph(dag: Any, inputs: Mapping[str, Any]) -> np.ndarray:
    """Evaluate a dumped Unreal material graph against fixed inputs. Not implemented yet.

    SF-4.3's material-graph dumper (which would produce the `dag` this function expects) has not
    landed. This name is kept public and importable now so that a future test module -- and this
    module's own corpus test, which reaches for it only once that dumper exists -- has a stable
    symbol to import against, rather than needing a later signature-guessing change.
    """

    raise NotImplementedError(
        "evaluate_graph awaits the SF-4.3 material-graph dumper; only the ps.1.x interpreter "
        "(Program.from_glb / evaluate) ships as part of SF-4.6 part 1"
    )
