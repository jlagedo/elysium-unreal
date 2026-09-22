"""Reading one init body's operands, without executing it.

An `InitCustomSchedules` body is straight-line MSVC output around four stack-local vectors. It
appends `(name, id)` pairs and text pointers to them as literal immediates, sorts them, then walks
each vector in a loop whose call site names the space and the category. Everything this seam needs
is an immediate operand or a frame address, so the walk decodes instruction LENGTHS and a handful of
operand forms, and never models a register's value beyond the two the call sites read.

The one thing that has to be tracked is the stack pointer, because a vector is identified by where
it sits in the frame and every reference to it is `[ESP + disp]` at a different depth. The model is
small and exact for these bodies: a `PUSH` lowers ESP by four, a call consumes the pushes that fed
it (every callee here is callee-cleanup), and an explicit `ADD`/`SUB ESP` moves it by its immediate.
So a frame address is `disp - 4 * pushes_since_the_last_call`, and the walk proves the model rather
than assuming it -- the four vector constructors must yield four distinct frame addresses, and every
append and every register loop must bind to one of them, or the body is refused.

Anything the length decoder does not know raises. A body that quietly skipped an instruction would
mis-track ESP and bind a pair to the wrong category, which is the one error that would be invisible
in the output.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import struct

from elysium_pipeline.formats.ai_schedule_glb.image import PEImage, call_target, follow_jump


class WalkError(ValueError):
    """An init body is not the shape this walk can read."""


#: `INT3`, MSVC's inter-function padding, which is what bounds a body.
_PAD = 0xCC

#: Register numbers preserved across a call by the cdecl/stdcall/thiscall conventions:
#: EBX, EBP, ESI, EDI. EAX/ECX/EDX are volatile and are forgotten at every call.
_CALLEE_SAVED = frozenset({3, 5, 6, 7})


@dataclass(frozen=True, slots=True)
class FrameStore:
    """`MOV dword ptr [ESP+disp], imm32` -- one literal written into the frame."""

    va: int
    frame: int
    value: int


@dataclass(frozen=True, slots=True)
class CallSite:
    """One `CALL rel32`, with what the walk could read about how it was set up."""

    va: int
    target: int
    #: `[ESP+disp]` frame address of the `LEA ECX` that set `this`, when there was one.
    this_frame: int | None
    #: `MOV ECX, imm32` immediately before the call, when there was one.
    this_immediate: int | None
    #: The `PUSH imm32` / `PUSH imm8` operands feeding this call, innermost last.
    pushed_immediates: tuple[int, ...]
    #: Frame stores since the previous call -- the pair the append is about to read.
    stores: tuple[FrameStore, ...]
    #: `MOV reg, dword ptr [ESP+disp]` frame addresses read since the previous call.
    loads: tuple[int, ...]
    #: `MOV reg, dword ptr [imm32]` absolute addresses read since the previous call. This is how
    #: `CAI_BaseNPC` feeds its 64 texts: each call site reads its OWN cell of a static pointer
    #: table, and the cells are not in feed order, so the table cannot be walked instead.
    absolute_loads: tuple[int, ...] = ()


@dataclass
class BodyWalk:
    """Every call site in one body, in address order."""

    start: int
    end: int
    calls: list[CallSite] = field(default_factory=list)
    #: Every `MOV r32, dword ptr [ESP+disp]` in the body, as `(va, frame)`, in address order.
    #: A register loop loads its vector's data pointer ONCE, before the loop, so the load that
    #: names the vector is not inside the call's own setup and cannot be found in `CallSite.loads`.
    frame_loads: list[tuple[int, int]] = field(default_factory=list)

    def calls_to(self, targets: frozenset[int]) -> list[CallSite]:
        return [call for call in self.calls if call.target in targets]

    def vector_before(self, va: int, candidates: set[int], window: int = 0x60) -> int | None:
        """The nearest frame address in `candidates` loaded shortly before `va`.

        This is how a register loop is bound to the vector it walks: MSVC hoists the vector's data
        pointer into a register ahead of the loop, so the binding is the last such load before the
        call rather than one of the call's own arguments.
        """

        best: int | None = None
        best_va = -1
        for load_va, frame in self.frame_loads:
            if frame in candidates and best_va < load_va <= va and va - load_va <= window:
                best, best_va = frame, load_va
        return best


def body_bounds(image: PEImage, va: int) -> tuple[int, int]:
    """The `[start, end)` of the padded body containing `va`.

    MSVC pads between functions with `INT3`; two in a row is the boundary. One is not enough --
    `0xCC` occurs inside immediates -- and requiring two has held over all 57 bodies this seam reads.
    """

    text_start, text = image.section_bytes(".text")
    offset = image.va_to_offset(va)
    if offset is None:
        raise WalkError(f"{va:#010x} is not in a mapped section")
    if not (text_start <= offset < text_start + len(text)):
        raise WalkError(f"{va:#010x} is not in .text")

    cursor = offset
    while cursor - 2 >= text_start:
        if image.data[cursor - 1] == _PAD and image.data[cursor - 2] == _PAD:
            break
        cursor -= 1
    start_va = image.offset_to_va(cursor)

    limit = text_start + len(text)
    cursor = offset
    while cursor + 2 < limit:
        if image.data[cursor] == _PAD and image.data[cursor + 1] == _PAD:
            break
        cursor += 1
    end_va = image.offset_to_va(cursor)
    if start_va is None or end_va is None:
        raise WalkError(f"{va:#010x}: could not bound its body")
    return int(start_va), int(end_va)


_CLEANUP_CACHE: dict[tuple[int, int], int | None] = {}


def _instruction_length(data: bytes, cursor: int) -> int | None:
    """One instruction's length, for the forms these bodies and their callees use."""

    opcode = data[cursor]
    if opcode in (0x81, 0x83):                                         # grp1 r/m32, imm
        modrm = data[cursor + 1]
        width = 4 if opcode == 0x81 else 1
        return 2 + _modrm_length(data, cursor + 2) + width
    if opcode == 0xC7:                                                 # MOV r/m32, imm32
        return 2 + _modrm_length(data, cursor + 2) + 4
    if opcode in (0x8D, 0x8B):                                         # LEA / MOV r32, r/m32
        return 2 + _modrm_length(data, cursor + 2)
    if 0xB8 <= opcode <= 0xBF:                                         # MOV r32, imm32
        return 5
    if 0x50 <= opcode <= 0x5F:                                         # PUSH/POP r32
        return 1
    fixed = _FIXED_LENGTH.get(opcode)
    if fixed is not None:
        return fixed
    return _length_of(data, cursor)


def _callee_cleanup(image: PEImage, target: int) -> int | None:
    """The bytes a callee pops on return: its `RET imm16`, or 0 for a bare `RET`.

    Resolved through one thunk hop, and read at INSTRUCTION BOUNDARIES -- a byte scan finds `0xC2`
    and `0xC3` inside displacements and immediates and then reports two different cleanups for one
    callee, which reads as "undecidable" when the callee is perfectly clear.

    A callee carrying both forms, or none the decoder can reach, answers None; the caller refuses
    rather than guessing, because an unknown cleanup silently shifts every later frame address.
    """

    body = follow_jump(image, int(target))
    cached = _CLEANUP_CACHE.get((id(image), body))
    if cached is not None or (id(image), body) in _CLEANUP_CACHE:
        return cached
    answer = _scan_cleanup(image, body)
    _CLEANUP_CACHE[(id(image), body)] = answer
    return answer


def _scan_cleanup(image: PEImage, body: int) -> int | None:
    try:
        start, end = body_bounds(image, body)
    except WalkError:
        return None
    start_offset = image.va_to_offset(start)
    end_offset = image.va_to_offset(end)
    if start_offset is None or end_offset is None:
        return None
    data = image.data
    seen: set[int] = set()
    cursor = start_offset
    while cursor < end_offset:
        opcode = data[cursor]
        if opcode == 0xC3:
            seen.add(0)
        elif opcode == 0xC2:
            seen.add(struct.unpack_from("<H", data, cursor + 1)[0])
        length = _instruction_length(data, cursor)
        if length is None or length <= 0:
            return _epilogue_cleanup(data, end_offset)
        cursor += length
    if len(seen) == 1:
        return int(next(iter(seen)))
    return _epilogue_cleanup(data, end_offset)


def _epilogue_cleanup(data: bytes, end_offset: int) -> int | None:
    """The cleanup of the body's last instruction.

    The linear scan cannot cross a body the decoder does not fully know -- the schedule parser
    itself is one -- but every one of these callees has a single MSVC epilogue at its tail, so the
    last instruction states the contract even when the middle is unreadable.
    """

    while end_offset - 1 >= 0 and data[end_offset - 1] in (0x90, _PAD):
        end_offset -= 1                            # MSVC pads a body's tail with NOPs too
    if end_offset - 1 >= 0 and data[end_offset - 1] == 0xC3:
        return 0
    if end_offset - 3 >= 0 and data[end_offset - 3] == 0xC2:
        cleanup = struct.unpack_from("<H", data, end_offset - 2)[0]
        if cleanup % 4 == 0 and cleanup <= 64:
            return int(cleanup)
    return None


def _sib_esp_displacement(data: bytes, offset: int, modrm: int) -> tuple[int | None, int]:
    """For a ModRM naming `[ESP+disp]` through a SIB byte, the displacement and the operand length.

    Answers `(None, length)` when the memory operand is not ESP-relative, so the caller can skip an
    instruction it does not care about without having to understand it.
    """

    mod = modrm >> 6
    rm = modrm & 7
    if rm != 4:                                   # not a SIB form, so not [ESP+...]
        if mod == 0:
            return None, 0 if rm != 5 else 4
        if mod == 1:
            return None, 1
        if mod == 2:
            return None, 4
        return None, 0
    sib = data[offset]
    base = sib & 7
    index = (sib >> 3) & 7
    length = 1
    if mod == 0:
        displacement = 0
        if base == 5:
            displacement = struct.unpack_from("<i", data, offset + 1)[0]
            length += 4
    elif mod == 1:
        displacement = struct.unpack_from("<b", data, offset + 1)[0]
        length += 1
    elif mod == 2:
        displacement = struct.unpack_from("<i", data, offset + 1)[0]
        length += 4
    else:
        return None, 0
    if base != 4 or index != 4:                   # base must be ESP with no scaled index
        return None, length
    return int(displacement), length


# Instruction lengths for the forms these bodies use, keyed by opcode. A value of -1 marks an
# opcode the walk decodes by hand below; anything absent raises.
_FIXED_LENGTH = {
    0x90: 1,                                       # NOP
    0xC3: 1,                                       # RET
    0xCC: 1,                                       # INT3
    0x99: 1,                                       # CDQ
    0xC2: 3,                                       # RET imm16
    0xEB: 2,                                       # JMP rel8
    0xE9: 5,                                       # JMP rel32
    0xE8: 5,                                       # CALL rel32
    0x6A: 2,                                       # PUSH imm8
    0x68: 5,                                       # PUSH imm32
    0xA0: 5,                                       # MOV AL, moffs8
    0xA1: 5,                                       # MOV EAX, moffs32
    0xA2: 5,                                       # MOV moffs8, AL
    0xA3: 5,                                       # MOV moffs32, EAX
}


def walk_body(image: PEImage, start: int, end: int) -> BodyWalk:
    """Decode one body into its call sites and the operands that set each one up."""

    data = image.data
    start_offset = image.va_to_offset(start)
    end_offset = image.va_to_offset(end)
    if start_offset is None or end_offset is None:
        raise WalkError(f"{start:#010x}-{end:#010x}: unmapped body bounds")

    result = BodyWalk(start=int(start), end=int(end))
    pushes = 0                                     # dwords pushed since the last call
    persistent = 0                                 # dwords the body pushed and has not popped
    pushed_immediates: list[int] = []
    stores: list[FrameStore] = []
    loads: list[int] = []
    absolute_loads: list[int] = []
    this_frame: int | None = None
    this_immediate: int | None = None
    # `MOV r32, imm32` values seen since the last call. MSVC hoists a repeated pair id into a
    # register and stores it with `MOV [ESP+disp], r32`, so without this the pair reads as
    # "not literal" and the body is refused over an operand that is perfectly recoverable.
    registers: dict[int, int] = {}

    def frame_of(displacement: int) -> int:
        return int(displacement) - 4 * (pushes + persistent)

    cursor = start_offset
    while cursor < end_offset:
        va = image.offset_to_va(cursor)
        opcode = data[cursor]

        # --- the forms the walk reads -------------------------------------------------------
        if opcode == 0xE8:                                                 # CALL rel32
            target = call_target(image, cursor)
            if target is None:
                raise WalkError(f"{va:#010x}: CALL rel32 with an unresolvable target")
            hopped = follow_jump(image, target)
            result.calls.append(
                CallSite(
                    va=int(va or 0),
                    target=int(hopped),
                    this_frame=this_frame,
                    this_immediate=this_immediate,
                    pushed_immediates=tuple(pushed_immediates),
                    stores=tuple(stores),
                    loads=tuple(loads),
                    absolute_loads=tuple(absolute_loads),
                )
            )
            # Only the pushes that fed the call go away, and the callee says how many: every
            # callee here is callee-cleanup and ends `RET imm16`. Resetting the depth instead
            # would swallow the prologue's callee-saved register pushes, which survive the call
            # and shift every later frame address by four bytes each.
            cleanup = _callee_cleanup(image, target)
            if cleanup is None:
                if pushes:
                    raise WalkError(
                        f"{va:#010x}: the callee {target:#010x} does not state its stack cleanup "
                        f"and {pushes} dword(s) are pending; the frame cannot be tracked"
                    )
            elif cleanup % 4:
                raise WalkError(f"{va:#010x}: callee {target:#010x} cleans {cleanup}, not a dword")
            else:
                pushes = max(0, pushes - cleanup // 4)
            pushed_immediates = []
            stores = []
            loads = []
            absolute_loads = []
            this_frame = None
            this_immediate = None
            # EAX/ECX/EDX are volatile and the callee may clobber them; EBX/EBP/ESI/EDI are
            # callee-saved, and MSVC relies on that -- it hoists a repeated pair id into ESI once
            # and stores it across several appends.
            registers = {reg: value for reg, value in registers.items() if reg in _CALLEE_SAVED}
            cursor += 5
            continue

        if opcode == 0x6A:                                                 # PUSH imm8
            pushed_immediates.append(struct.unpack_from("<b", data, cursor + 1)[0])
            pushes += 1
            cursor += 2
            continue

        if opcode == 0x68:                                                 # PUSH imm32
            pushed_immediates.append(struct.unpack_from("<I", data, cursor + 1)[0])
            pushes += 1
            cursor += 5
            continue

        if 0x50 <= opcode <= 0x57:                                         # PUSH r32
            pushes += 1
            cursor += 1
            continue

        if 0x58 <= opcode <= 0x5F:                                         # POP r32
            if pushes > 0:
                pushes -= 1
            elif persistent > 0:
                persistent -= 1
            cursor += 1
            continue

        if 0xB8 <= opcode <= 0xBF:                                         # MOV r32, imm32
            value = struct.unpack_from("<I", data, cursor + 1)[0]
            registers[opcode - 0xB8] = int(value)
            if opcode == 0xB9:                                             # ECX: the `this` operand
                this_immediate = int(value)
            cursor += 5
            continue

        if opcode == 0x89:                                                 # MOV r/m32, r32
            modrm = data[cursor + 1]
            displacement, extra = _sib_esp_displacement(data, cursor + 2, modrm)
            tail = _modrm_tail(modrm, extra)
            source = (modrm >> 3) & 7
            if displacement is not None and source in registers:
                stores.append(
                    FrameStore(
                        va=int(va or 0),
                        frame=frame_of(displacement),
                        value=int(registers[source]),
                    )
                )
            cursor += 2 + extra + tail
            continue

        if opcode in (0x81, 0x83):                                         # grp1 r/m32, imm
            modrm = data[cursor + 1]
            width = 4 if opcode == 0x81 else 1
            if modrm in (0xC4, 0xEC):                                      # ADD/SUB ESP, imm
                if width == 1:
                    immediate = struct.unpack_from("<b", data, cursor + 2)[0]
                else:
                    immediate = struct.unpack_from("<i", data, cursor + 2)[0]
                delta = immediate if modrm == 0xC4 else -immediate
                if cursor == start_offset:
                    pass                           # the prologue establishes the frame; depth 0
                elif delta % 4 == 0:
                    moved = delta // 4
                    if moved > 0:                  # ADD ESP -- releasing pushed dwords
                        take = min(moved, pushes)
                        pushes -= take
                        persistent = max(0, persistent - (moved - take))
                    else:
                        pushes += -moved
                cursor += 2 + width
                continue
            displacement, extra = _sib_esp_displacement(data, cursor + 2, modrm)
            cursor += 2 + extra + width + _modrm_tail(modrm, extra)
            continue

        if opcode == 0xC7:                                                 # MOV r/m32, imm32
            modrm = data[cursor + 1]
            displacement, extra = _sib_esp_displacement(data, cursor + 2, modrm)
            tail = _modrm_tail(modrm, extra)
            immediate = struct.unpack_from("<I", data, cursor + 2 + extra + tail)[0]
            if displacement is not None:
                stores.append(
                    FrameStore(va=int(va or 0), frame=frame_of(displacement), value=int(immediate))
                )
            cursor += 2 + extra + tail + 4
            continue

        if opcode == 0x8D:                                                 # LEA r32, m
            modrm = data[cursor + 1]
            displacement, extra = _sib_esp_displacement(data, cursor + 2, modrm)
            tail = _modrm_tail(modrm, extra)
            if displacement is not None and ((modrm >> 3) & 7) == 1:       # LEA ECX, [ESP+disp]
                this_frame = frame_of(displacement)
            cursor += 2 + extra + tail
            continue

        if opcode == 0xA1:                                                 # MOV EAX, moffs32
            # The short encoding of an absolute load. `CAI_BaseNPC`'s feed loop uses it for the
            # cells that happen to land in EAX and the ModRM form below for the rest, so both
            # have to be read or the body feeds fewer texts than it does.
            absolute_loads.append(struct.unpack_from("<I", data, cursor + 1)[0])
            cursor += 5
            continue

        if opcode == 0x8B:                                                 # MOV r32, r/m32
            modrm = data[cursor + 1]
            if (modrm >> 6) == 0 and (modrm & 7) == 5:                     # [imm32], absolute
                absolute_loads.append(struct.unpack_from("<I", data, cursor + 2)[0])
                cursor += 6
                continue
            displacement, extra = _sib_esp_displacement(data, cursor + 2, modrm)
            tail = _modrm_tail(modrm, extra)
            if displacement is not None:
                frame = frame_of(displacement)
                loads.append(frame)
                result.frame_loads.append((int(va or 0), frame))
            cursor += 2 + extra + tail
            continue

        # --- the forms the walk only needs a length for -------------------------------------
        fixed = _FIXED_LENGTH.get(opcode)
        if fixed is not None:
            cursor += fixed
            continue
        length = _length_of(data, cursor)
        if length is None:
            raise WalkError(
                f"{va:#010x}: opcode {opcode:#04x} is not in the ai-schedule walk's decoder; "
                "the body cannot be read without guessing at its stack depth"
            )
        cursor += length

    return result


def _modrm_tail(modrm: int, sib_extra: int) -> int:
    """The displacement bytes a non-SIB ModRM carries, when `_sib_esp_displacement` skipped it."""

    if sib_extra:
        return 0
    mod = modrm >> 6
    rm = modrm & 7
    if mod == 0:
        return 4 if rm == 5 else 0
    if mod == 1:
        return 1
    if mod == 2:
        return 4
    return 0


def _length_of(data: bytes, cursor: int) -> int | None:
    """The length of one instruction the walk does not read, or None when it does not know it."""

    opcode = data[cursor]

    if opcode == 0x0F:                                                     # two-byte opcodes
        second = data[cursor + 1]
        if 0x80 <= second <= 0x8F:                                         # Jcc rel32
            return 6
        if 0x90 <= second <= 0x9F:                                         # SETcc r/m8
            return 3 + _modrm_length(data, cursor + 2)
        if second in (0xAF, 0xB6, 0xB7, 0xBE, 0xBF):                       # IMUL / MOVZX / MOVSX
            return 3 + _modrm_length(data, cursor + 2)
        return None

    if 0x70 <= opcode <= 0x7F:                                             # Jcc rel8
        return 2
    if 0x40 <= opcode <= 0x4F:                                             # INC/DEC r32
        return 1
    if opcode in (0x88, 0x8A, 0x38, 0x39, 0x3A, 0x3B, 0x84, 0x85,
                  0x00, 0x01, 0x02, 0x03, 0x28, 0x29, 0x2A, 0x2B,
                  0x20, 0x21, 0x22, 0x23, 0x08, 0x09, 0x0A, 0x0B,
                  0x30, 0x31, 0x32, 0x33, 0x86, 0x87):
        return 2 + _modrm_length(data, cursor + 2)
    if opcode in (0x04, 0x0C, 0x14, 0x1C, 0x24, 0x2C, 0x34, 0x3C, 0xA8):   # op AL, imm8
        return 2
    if opcode in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D, 0xA9):   # op EAX, imm32
        return 5
    if opcode in (0xC6,):                                                  # MOV r/m8, imm8
        return 2 + _modrm_length(data, cursor + 2) + 1
    if opcode in (0x80,):                                                  # grp1 r/m8, imm8
        return 2 + _modrm_length(data, cursor + 2) + 1
    if opcode in (0xC0, 0xC1):                                             # shift r/m32, imm8
        return 2 + _modrm_length(data, cursor + 2) + 1
    if opcode in (0xD0, 0xD1, 0xD2, 0xD3):                                 # shift r/m32, 1|CL
        return 2 + _modrm_length(data, cursor + 2)
    if opcode in (0xF6, 0xF7):                                             # grp3
        modrm = data[cursor + 1]
        reg = (modrm >> 3) & 7
        length = 2 + _modrm_length(data, cursor + 2)
        if reg in (0, 1):                                                  # TEST r/m, imm
            length += 1 if opcode == 0xF6 else 4
        return length
    if opcode in (0xFE, 0xFF):                                             # grp4/grp5
        return 2 + _modrm_length(data, cursor + 2)
    if opcode in (0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF):         # x87
        return 2 + _modrm_length(data, cursor + 2)
    return None


def _modrm_length(data: bytes, after_modrm: int) -> int:
    """The bytes a ModRM at `after_modrm - 1` carries after itself (SIB and displacement)."""

    modrm = data[after_modrm - 1]
    mod = modrm >> 6
    rm = modrm & 7
    if mod == 3:
        return 0
    length = 0
    if rm == 4:
        length += 1
        base = data[after_modrm] & 7
        if mod == 0 and base == 5:
            return length + 4
    elif mod == 0 and rm == 5:
        return 4
    if mod == 1:
        length += 1
    elif mod == 2:
        length += 4
    return length
