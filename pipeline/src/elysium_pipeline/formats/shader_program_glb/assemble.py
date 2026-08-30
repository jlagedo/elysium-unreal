"""The ps.1.x assembler that turns a parsed `.psh` back into Direct3D tokens.

It exists for one purpose: a `shaders/psh/<stem>.vcs` whose stem matches a readable source is
compared combo by combo against the tokens that source assembles to, and a difference is recorded
as drift rather than corrected on either side. It assembles the decoder's own instruction records,
not the text a second time, so a source the parser could not fully classify cannot be silently
half-assembled here.
"""

from __future__ import annotations

import struct
from typing import Sequence

from elysium_pipeline.formats.shader_program_glb.model import (
    Define,
    Destination,
    Instruction,
    Operand,
    ShaderVersion,
)
from elysium_pipeline.formats.shader_program_glb.tokens import END_TOKEN, PHASE_OPCODE

#: Mnemonic to opcode, for the vocabulary the shipped ps.1.1 and ps.1.4 sources use.
MNEMONIC_OPCODES = {
    "nop": 0, "mov": 1, "add": 2, "sub": 3, "mad": 4, "mul": 5, "rcp": 6, "rsq": 7,
    "dp3": 8, "dp4": 9, "min": 10, "max": 11, "slt": 12, "sge": 13, "exp": 14, "log": 15,
    "lit": 16, "dst": 17, "lrp": 18, "frc": 19, "texcoord": 64, "texcrd": 64, "texkill": 65,
    "tex": 66, "texld": 66, "texbem": 67, "texbeml": 68, "texreg2ar": 69, "texreg2gb": 70,
    "texm3x2pad": 71, "texm3x2tex": 72, "texm3x3pad": 73, "texm3x3tex": 74, "texm3x3spec": 76,
    "texm3x3vspec": 77, "expp": 78, "logp": 79, "cnd": 80, "def": 81, "texreg2rgb": 82,
    "texdp3tex": 83, "texm3x2depth": 84, "texdp3": 85, "texm3x3": 86, "texdepth": 87,
    "cmp": 88, "bem": 89, "phase": PHASE_OPCODE,
}

REGISTER_TYPES = {"temp": 0, "input": 1, "const": 2, "texture": 3}

_WRITE_MASK_BITS = {"r": 0x1, "g": 0x2, "b": 0x4, "a": 0x8, "x": 0x1, "y": 0x2, "z": 0x4, "w": 0x8}
_REPLICATE_SWIZZLE = {"r": 0x00, "g": 0x55, "b": 0xAA, "a": 0xFF,
                      "x": 0x00, "y": 0x55, "z": 0xAA, "w": 0xFF}
_FULL_SWIZZLE = 0xE4

_DESTINATION_SHIFTS = {"_x2": 1, "_x4": 2, "_x8": 3, "_d2": 0xF, "_d4": 0xE, "_d8": 0xD}
_SOURCE_MODIFIERS = {None: 0, "_bias": 2, "_bx2": 4, "_x2": 7, "_dz": 9, "_dw": 10}
_NEGATED = {0: 1, 2: 3, 4: 5, 7: 8}


class AssemblyError(ValueError):
    """A parsed source record has no Direct3D encoding this assembler knows."""


def version_token(version: ShaderVersion) -> int:
    return 0xFFFF0000 | (version.major << 8) | version.minor


def destination_token(destination: Destination) -> int:
    register_type = REGISTER_TYPES.get(destination.register.register_class)
    if register_type is None:
        raise AssemblyError(f"no destination encoding for {destination.text!r}")
    mask = 0xF
    if destination.write_mask:
        mask = 0
        for letter in destination.write_mask:
            if letter not in _WRITE_MASK_BITS:
                raise AssemblyError(f"no write mask encoding for {destination.text!r}")
            mask |= _WRITE_MASK_BITS[letter]
    return 0x80000000 | destination.register.index | (register_type << 28) | (mask << 16)


def source_token(operand: Operand) -> int:
    register_type = REGISTER_TYPES.get(operand.register.register_class)
    if register_type is None:
        raise AssemblyError(f"no source encoding for {operand.text!r}")
    swizzle = _FULL_SWIZZLE
    if operand.selector:
        if len(operand.selector) == 1:
            if operand.selector not in _REPLICATE_SWIZZLE:
                raise AssemblyError(f"no swizzle encoding for {operand.text!r}")
            swizzle = _REPLICATE_SWIZZLE[operand.selector]
        elif operand.selector not in ("rgb", "xyz", "rgba", "xyzw"):
            raise AssemblyError(f"no swizzle encoding for {operand.text!r}")
    if operand.modifier not in _SOURCE_MODIFIERS:
        raise AssemblyError(f"no source modifier encoding for {operand.text!r}")
    modifier = _SOURCE_MODIFIERS[operand.modifier]
    if operand.complement:
        if modifier:
            raise AssemblyError(f"{operand.text!r} spells a complement and a modifier")
        modifier = 6
    if operand.negate:
        if modifier not in _NEGATED:
            raise AssemblyError(f"{operand.text!r} negates a modifier that has no negated form")
        modifier = _NEGATED[modifier]
    return (
        0x80000000
        | operand.register.index
        | (register_type << 28)
        | (swizzle << 16)
        | (modifier << 24)
    )


def assemble(
    version: ShaderVersion,
    records: Sequence[Instruction | Define],
) -> list[int]:
    """The token stream one parsed source assembles to, version token through end token.

    `records` is the source's instructions and `def` declarations in the order they were written,
    which is the order the assembler emits them in.
    """

    words = [version_token(version)]
    for record in records:
        if isinstance(record, Define):
            words.append(MNEMONIC_OPCODES["def"])
            words.append(
                0x80000000
                | record.register.index
                | (REGISTER_TYPES[record.register.register_class] << 28)
                | (0xF << 16)
            )
            for value in record.values:
                words.append(struct.unpack("<I", struct.pack("<f", float(value)))[0])
            continue
        opcode = MNEMONIC_OPCODES.get(record.opcode)
        if opcode is None:
            raise AssemblyError(f"no opcode encoding for {record.opcode!r}")
        if opcode == PHASE_OPCODE:
            words.append(PHASE_OPCODE)
            continue
        instruction = opcode | (0x40000000 if record.co_issued else 0)
        words.append(instruction)
        if record.destination is None:
            raise AssemblyError(f"{record.opcode!r} carries no destination to encode")
        saturate = "_sat" in record.modifiers
        shifts = [modifier for modifier in record.modifiers if modifier != "_sat"]
        token = destination_token(record.destination)
        if saturate:
            token |= 1 << 20
        for shift in shifts:
            if shift not in _DESTINATION_SHIFTS:
                raise AssemblyError(f"no destination modifier encoding for {shift!r}")
            token |= _DESTINATION_SHIFTS[shift] << 24
        words.append(token)
        for operand in record.sources:
            words.append(source_token(operand))
    words.append(END_TOKEN)
    return words
