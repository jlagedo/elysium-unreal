"""The Direct3D shader token encoding, decoded both ways.

The token stream a `.vcs` combo carries is bijective with its disassembly: every DWORD is a
version token, an instruction token, a parameter token, a literal a `def` declares, a comment
token with its payload, the phase marker or the end token. `decode_stream` states each one as
decoded fields beside its raw 32-bit value, which is what lets the unit publish no copy of the
bytecode and still pay the ledger's `mapped` claim over it: `reassemble` turns the decoded rows
back into the exact bytes they came from.
"""

from __future__ import annotations

import struct
from typing import Any

#: `opcode -> (mnemonic, destination count, source count)`. The three `None` entries are the
#: declarations, whose operand shape is fixed rather than counted.
OPCODES: dict[int, tuple[str, Any, Any]] = {
    0: ("nop", 0, 0),
    1: ("mov", 1, 1),
    2: ("add", 1, 2),
    3: ("sub", 1, 2),
    4: ("mad", 1, 3),
    5: ("mul", 1, 2),
    6: ("rcp", 1, 1),
    7: ("rsq", 1, 1),
    8: ("dp3", 1, 2),
    9: ("dp4", 1, 2),
    10: ("min", 1, 2),
    11: ("max", 1, 2),
    12: ("slt", 1, 2),
    13: ("sge", 1, 2),
    14: ("exp", 1, 1),
    15: ("log", 1, 1),
    16: ("lit", 1, 1),
    17: ("dst", 1, 2),
    18: ("lrp", 1, 3),
    19: ("frc", 1, 1),
    20: ("m4x4", 1, 2),
    21: ("m4x3", 1, 2),
    22: ("m3x4", 1, 2),
    23: ("m3x3", 1, 2),
    24: ("m3x2", 1, 2),
    25: ("call", 0, 1),
    26: ("callnz", 0, 2),
    27: ("loop", 0, 2),
    28: ("ret", 0, 0),
    29: ("endloop", 0, 0),
    30: ("label", 0, 1),
    31: ("dcl", None, None),
    32: ("pow", 1, 2),
    33: ("crs", 1, 2),
    34: ("sgn", 1, 3),
    35: ("abs", 1, 1),
    36: ("nrm", 1, 1),
    37: ("sincos", 1, 3),
    38: ("rep", 0, 1),
    39: ("endrep", 0, 0),
    40: ("if", 0, 1),
    41: ("ifc", 0, 2),
    42: ("else", 0, 0),
    43: ("endif", 0, 0),
    44: ("break", 0, 0),
    45: ("breakc", 0, 2),
    46: ("mova", 1, 1),
    47: ("defb", None, None),
    48: ("defi", None, None),
    64: ("texcoord", 1, 0),
    65: ("texkill", 1, 0),
    66: ("tex", 1, 0),
    67: ("texbem", 1, 1),
    68: ("texbeml", 1, 1),
    69: ("texreg2ar", 1, 1),
    70: ("texreg2gb", 1, 1),
    71: ("texm3x2pad", 1, 1),
    72: ("texm3x2tex", 1, 1),
    73: ("texm3x3pad", 1, 1),
    74: ("texm3x3tex", 1, 1),
    75: ("reserved0", 0, 0),
    76: ("texm3x3spec", 1, 2),
    77: ("texm3x3vspec", 1, 1),
    78: ("expp", 1, 1),
    79: ("logp", 1, 1),
    80: ("cnd", 1, 3),
    81: ("def", None, None),
    82: ("texreg2rgb", 1, 1),
    83: ("texdp3tex", 1, 1),
    84: ("texm3x2depth", 1, 1),
    85: ("texdp3", 1, 1),
    86: ("texm3x3", 1, 1),
    87: ("texdepth", 1, 0),
    88: ("cmp", 1, 3),
    89: ("bem", 1, 2),
    90: ("dp2add", 1, 3),
    91: ("dsx", 1, 1),
    92: ("dsy", 1, 1),
    93: ("texldd", 1, 4),
    94: ("setp", 1, 2),
    95: ("texldl", 1, 2),
    96: ("breakp", 0, 1),
}

END_TOKEN = 0x0000FFFF
COMMENT_OPCODE = 0xFFFE
PHASE_OPCODE = 0xFFFD

_COISSUE_BIT = 0x40000000
_PREDICATED_BIT = 0x10000000
_LENGTH_MASK = 0x0F000000
_CONTROL_MASK = 0x00FF0000
_RELATIVE_BIT = 0x00002000

#: `registerType -> (name, disassembly prefix)`, in the D3D register-file vocabulary.
REGISTER_CLASSES = {
    0: ("temp", "r"),
    1: ("input", "v"),
    2: ("const", "c"),
    3: ("texture", "t"),
    4: ("rasterizer-out", "oPos"),
    5: ("attribute-out", "oD"),
    6: ("output", "o"),
    7: ("const-int", "i"),
    8: ("color-out", "oC"),
    9: ("depth-out", "oDepth"),
    10: ("sampler", "s"),
    11: ("const2", "c"),
    12: ("const3", "c"),
    13: ("const4", "c"),
    14: ("const-bool", "b"),
    15: ("loop-counter", "aL"),
    16: ("temp-float16", "h"),
    17: ("misc", "vMisc"),
    18: ("label", "l"),
    19: ("predicate", "p"),
}

#: The vertex-shader spelling of register type 3, which is the address register there.
_VERTEX_ADDRESS = ("address", "a")

SOURCE_MODIFIERS = {
    0: "none",
    1: "negate",
    2: "bias",
    3: "bias-negate",
    4: "sign",
    5: "sign-negate",
    6: "complement",
    7: "x2",
    8: "x2-negate",
    9: "dz",
    10: "dw",
    11: "abs",
    12: "abs-negate",
    13: "not",
}

_SOURCE_MODIFIER_TEXT = {
    0: ("", ""),
    1: ("-", ""),
    2: ("", "_bias"),
    3: ("-", "_bias"),
    4: ("", "_bx2"),
    5: ("-", "_bx2"),
    6: ("1-", ""),
    7: ("", "_x2"),
    8: ("-", "_x2"),
    9: ("", "_dz"),
    10: ("", "_dw"),
    11: ("", "_abs"),
    12: ("-", "_abs"),
    13: ("!", ""),
}

DESTINATION_MODIFIERS = {1: "saturate", 2: "partial-precision", 4: "centroid"}

_SHIFT_TEXT = {
    0: "",
    1: "_x2",
    2: "_x4",
    3: "_x8",
    0xD: "_d8",
    0xE: "_d4",
    0xF: "_d2",
}

_SWIZZLE_LETTERS = "xyzw"
_MASK_LETTERS = "xyzw"

#: `usage -> name` for a `dcl` declaration token in a 2.0+ shader.
DECLARATION_USAGES = {
    0: "position",
    1: "blend-weight",
    2: "blend-indices",
    3: "normal",
    4: "point-size",
    5: "texcoord",
    6: "tangent",
    7: "binormal",
    8: "tessellation-factor",
    9: "position-transformed",
    10: "color",
    11: "fog",
    12: "depth",
    13: "sample",
}

#: `textureType -> name` for a `dcl` on a sampler register.
SAMPLER_TYPES = {0: "unknown", 1: "1d", 2: "2d", 3: "cube", 4: "volume"}


class TokenStreamError(ValueError):
    """A combo's token stream is not the Direct3D encoding this seam decodes."""


def model_name(version_token: int) -> str:
    """`ps_2_0`, `vs_1_1`, ... from a version token, or `unknown-<hex>`."""

    kind = version_token >> 16
    major, minor = (version_token >> 8) & 0xFF, version_token & 0xFF
    if kind == 0xFFFF:
        return f"ps_{major}_{minor}"
    if kind == 0xFFFE:
        return f"vs_{major}_{minor}"
    return f"unknown-0x{version_token:08x}"


def _register(token: int, is_pixel: bool) -> tuple[str, str, int]:
    register_type = ((token & 0x70000000) >> 28) | ((token & 0x00001800) >> 8)
    index = token & 0x000007FF
    if register_type == 3 and not is_pixel:
        name, prefix = _VERTEX_ADDRESS
    else:
        name, prefix = REGISTER_CLASSES.get(register_type, (f"unknown-{register_type}", "?"))
    return name, f"{prefix}{index}", index


def _write_mask(token: int) -> tuple[str, str]:
    bits = (token & 0x000F0000) >> 16
    letters = "".join(letter for position, letter in enumerate(_MASK_LETTERS) if bits & (1 << position))
    if bits == 0xF:
        return "xyzw", ""
    return letters, ("." + letters if letters else ".")


def _swizzle(token: int) -> tuple[str, str]:
    bits = (token & 0x00FF0000) >> 16
    letters = "".join(_SWIZZLE_LETTERS[(bits >> (2 * position)) & 3] for position in range(4))
    if letters == "xyzw":
        return letters, ""
    if letters[0] * 4 == letters:
        return letters, "." + letters[0]
    return letters, "." + letters


def destination_token(token: int, is_pixel: bool) -> dict[str, Any]:
    register_class, text, index = _register(token, is_pixel)
    mask, mask_text = _write_mask(token)
    modifier_bits = (token & 0x00F00000) >> 20
    modifiers = [name for bit, name in DESTINATION_MODIFIERS.items() if modifier_bits & bit]
    shift = (token & _LENGTH_MASK) >> 24
    row: dict[str, Any] = {
        "kind": "destination",
        "registerClass": register_class,
        "registerIndex": index,
        "writeMask": mask,
        "modifiers": modifiers,
        "shift": _SHIFT_TEXT.get(shift, f"unknown-{shift}"),
        "text": text + mask_text,
    }
    return row


def source_token(token: int, is_pixel: bool) -> dict[str, Any]:
    register_class, text, index = _register(token, is_pixel)
    swizzle, swizzle_text = _swizzle(token)
    modifier = (token & 0x0F000000) >> 24
    prefix, suffix = _SOURCE_MODIFIER_TEXT.get(modifier, ("", f"_unknown{modifier}"))
    relative = bool(token & _RELATIVE_BIT)
    return {
        "kind": "source",
        "registerClass": register_class,
        "registerIndex": index,
        "swizzle": swizzle,
        "modifier": SOURCE_MODIFIERS.get(modifier, f"unknown-{modifier}"),
        "relativeAddress": relative,
        # Shader model 1 has one address component and states no token for it; model 2 states an
        # address token, and `_address_text` rewrites this text with the component it names.
        "text": f"{prefix}{text}{suffix}{swizzle_text}" + ("[a0.x]" if relative else ""),
    }


def _address_text(source: dict[str, Any], address: dict[str, Any]) -> None:
    """Respell a relatively-addressed source with the address register its own token names.

    `c48[a0]` is not a disassembly of anything: the address token carries the component, and four
    of them are in use across the shipped `fxc/` bundles, so the source's text states the one it
    actually indexes with.
    """

    base = source["text"]
    source["text"] = base[: base.rindex("[")] + "[" + address["text"] + "]"


def _instruction_text(opcode: str, modifiers: list[str], rows: list[dict[str, Any]]) -> str:
    """The disassembly line: the predicate, the mnemonic with its modifiers, then the operands."""

    mnemonic = opcode + "".join(modifiers)
    predicate = next((row for row in rows if row["kind"] == "predicate"), None)
    if predicate is not None:
        mnemonic = f"({predicate['text']}) {mnemonic}"
    operands = [row["text"] for row in rows if row["kind"] in ("destination", "source")]
    literals = [row["text"] for row in rows if row["kind"] == "literal"]
    parts = operands + literals
    return f"{mnemonic} {', '.join(parts)}".strip()


def _operand_row(at: int, base_offset: int, word: int, fields: dict[str, Any]) -> dict[str, Any]:
    """One parameter token, stated the way every other token row is: place, value, then meaning."""

    return {"index": (at - base_offset) // 4, "sourceOffset": at, "value": word, **fields}


def decode_stream(data: bytes, base_offset: int = 0) -> list[dict[str, Any]]:
    """Every token of one combo stream, in order, with its file offset and raw value.

    `base_offset` is where the stream starts in the member, so a token row and the ledger range
    that pays for it name the same place.
    """

    if len(data) < 8 or len(data) % 4:
        raise TokenStreamError(f"a combo stream of {len(data)} bytes is not a token stream")
    words = struct.unpack(f"<{len(data) // 4}I", data)
    version = words[0]
    is_pixel = (version >> 16) == 0xFFFF
    major, minor = (version >> 8) & 0xFF, version & 0xFF
    if (version >> 16) not in (0xFFFF, 0xFFFE):
        raise TokenStreamError(f"a combo stream opens with 0x{version:08x}, not a version token")
    rows: list[dict[str, Any]] = [
        {
            "index": 0,
            "sourceOffset": base_offset,
            "value": version,
            "kind": "version",
            "model": model_name(version),
            "major": major,
            "minor": minor,
        }
    ]
    position = 1
    total = len(words)
    while True:
        if position >= total:
            raise TokenStreamError("the token stream ends without an end token")
        token = words[position]
        offset = base_offset + 4 * position
        if token == END_TOKEN:
            rows.append({"index": position, "sourceOffset": offset, "value": token, "kind": "end"})
            position += 1
            break
        opcode = token & 0xFFFF
        if opcode == COMMENT_OPCODE:
            payload_words = (token >> 16) & 0x7FFF
            if position + 1 + payload_words > total:
                raise TokenStreamError(f"a comment token at {offset} runs past the stream")
            payload = data[4 * (position + 1): 4 * (position + 1 + payload_words)]
            rows.append(
                {
                    "index": position,
                    "sourceOffset": offset,
                    "value": token,
                    "kind": "comment",
                    "payloadDwords": payload_words,
                    "payloadOffset": offset + 4,
                    "fourCC": payload[:4].decode("latin-1") if len(payload) >= 4 else None,
                }
            )
            position += 1 + payload_words
            continue
        if opcode == PHASE_OPCODE:
            rows.append({"index": position, "sourceOffset": offset, "value": token, "kind": "phase"})
            position += 1
            continue
        if opcode not in OPCODES:
            raise TokenStreamError(f"unknown opcode {opcode} at {offset}")
        mnemonic, destinations, sources = _operand_shape(opcode, major, minor)
        control = (token & _CONTROL_MASK) >> 16
        declared_length = (token & _LENGTH_MASK) >> 24
        operands: list[dict[str, Any]] = []
        cursor = position + 1

        def take() -> tuple[int, int]:
            nonlocal cursor
            if cursor >= total:
                raise TokenStreamError(f"an instruction at {offset} runs past the stream")
            word = words[cursor]
            at = base_offset + 4 * cursor
            cursor += 1
            return word, at

        if token & _PREDICATED_BIT:
            word, at = take()
            operands.append(_operand_row(at, base_offset, word,
                                         dict(source_token(word, is_pixel), kind="predicate")))
        if opcode in (81, 48, 47):                       # def, defi, defb
            word, at = take()
            operands.append(_operand_row(at, base_offset, word, destination_token(word, is_pixel)))
            literals = 4 if opcode in (81, 48) else 1
            for _ in range(literals):
                word, at = take()
                operands.append(_literal_row(opcode, word, at, base_offset))
        elif opcode == 31:                               # dcl
            word, at = take()
            operands.append(_declaration_row(word, at, base_offset))
            word, at = take()
            operands.append(_operand_row(at, base_offset, word, destination_token(word, is_pixel)))
        else:
            for _ in range(destinations or 0):
                word, at = take()
                operands.append(
                    _operand_row(at, base_offset, word, destination_token(word, is_pixel))
                )
            for _ in range(sources or 0):
                word, at = take()
                row = _operand_row(at, base_offset, word, source_token(word, is_pixel))
                operands.append(row)
                if major >= 2 and row["relativeAddress"]:
                    word, at = take()
                    address = _operand_row(
                        at, base_offset, word,
                        dict(source_token(word, is_pixel), kind="relative-address"),
                    )
                    _address_text(row, address)
                    operands.append(address)
        extra = cursor - position - 1
        if major >= 2 and declared_length != extra:
            raise TokenStreamError(
                f"instruction {mnemonic} at {offset} declares {declared_length} operand "
                f"token(s) and carries {extra}"
            )
        rows.append(
            {
                "index": position,
                "sourceOffset": offset,
                "value": token,
                "kind": "instruction",
                "opcode": mnemonic,
                "opcodeValue": opcode,
                "coIssue": bool(token & _COISSUE_BIT),
                "predicated": bool(token & _PREDICATED_BIT),
                "control": control,
                "operandTokens": extra,
            }
        )
        rows.extend(operands)
        position = cursor
    if position != total:
        raise TokenStreamError(
            f"the token stream carries {total - position} token(s) after its end token"
        )
    return rows


def _operand_shape(opcode: int, major: int, minor: int) -> tuple[str, Any, Any]:
    """The spelling and operand shape one opcode takes in the model that declared it.

    Three opcodes were respelled as the models grew: the texture-sampling opcode is `tex rN` in
    ps.1.1, `texld rN, tN` in ps.1.4 and `texld rN, tN, sN` from ps_2_0 on, `texcoord` became
    `texcrd` with a source in ps.1.4, and `sincos` dropped its two constant operands in 3.0.
    """

    mnemonic, destinations, sources = OPCODES[opcode]
    if opcode == 66:
        if major >= 2:
            return "texld", 1, 2
        if (major, minor) >= (1, 4):
            return "texld", 1, 1
        return "tex", 1, 0
    if opcode == 64 and (major, minor) >= (1, 4):
        return "texcrd", 1, 1
    if opcode == 37 and major >= 3:
        return "sincos", 1, 1
    return mnemonic, destinations, sources


def _literal_row(opcode: int, word: int, at: int, base_offset: int) -> dict[str, Any]:
    row: dict[str, Any] = {
        "index": (at - base_offset) // 4,
        "sourceOffset": at,
        "value": word,
        "kind": "literal",
    }
    if opcode == 81:
        value = struct.unpack("<f", struct.pack("<I", word))[0]
        row["float"] = value
        row["text"] = repr(value)
    elif opcode == 48:
        value = struct.unpack("<i", struct.pack("<I", word))[0]
        row["int"] = value
        row["text"] = str(value)
    else:
        row["bool"] = bool(word)
        row["text"] = "true" if word else "false"
    return row


def _declaration_row(word: int, at: int, base_offset: int) -> dict[str, Any]:
    usage = word & 0x0000001F
    usage_index = (word & 0x000F0000) >> 16
    texture_type = (word & 0x78000000) >> 27
    return {
        "index": (at - base_offset) // 4,
        "sourceOffset": at,
        "value": word,
        "kind": "declaration",
        "usage": DECLARATION_USAGES.get(usage, f"unknown-{usage}"),
        "usageIndex": usage_index,
        "samplerType": SAMPLER_TYPES.get(texture_type, f"unknown-{texture_type}"),
    }


def disassemble(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """The token rows regrouped into one record per instruction.

    The field shape is the readable source unit's: opcode, modifiers, destination, sources and
    the instruction text, so the two kinds answer in one vocabulary. A compiled source carries
    two fields the assembly text has no spelling for -- `relativeAddress` and, where shader model
    2 stated one, the `addressRegister` token it indexes with.
    """

    instructions: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    operands: list[dict[str, Any]] = []

    def flush() -> None:
        if current is None:
            return
        destination = next((row for row in operands if row["kind"] == "destination"), None)
        sources = _source_views(operands)
        literals = [row for row in operands if row["kind"] == "literal"]
        declaration = next((row for row in operands if row["kind"] == "declaration"), None)
        modifiers: list[str] = []
        if destination is not None:
            modifiers = ["_sat" if name == "saturate" else "_" + name
                         for name in destination["modifiers"]]
            if destination["shift"]:
                modifiers.append(destination["shift"])
        instructions.append(
            {
                "index": len(instructions),
                "tokenIndex": current["index"],
                "sourceOffset": current["sourceOffset"],
                "opcode": current["opcode"],
                "modifiers": modifiers,
                "coIssued": current["coIssue"],
                "predicated": current["predicated"],
                "predicate": _operand_view(
                    next((row for row in operands if row["kind"] == "predicate"), None)
                ),
                "destination": _operand_view(destination),
                "sources": sources,
                "literals": [row.get("text") for row in literals],
                "declaration": None if declaration is None else {
                    "usage": declaration["usage"],
                    "usageIndex": declaration["usageIndex"],
                    "samplerType": declaration["samplerType"],
                },
                "text": _instruction_text(current["opcode"], modifiers, operands),
            }
        )

    for row in rows:
        kind = row["kind"]
        if kind in ("version", "end"):
            flush()
            current, operands = None, []
            continue
        if kind == "phase":
            flush()
            current, operands = None, []
            instructions.append(
                {
                    "index": len(instructions),
                    "tokenIndex": row["index"],
                    "sourceOffset": row["sourceOffset"],
                    "opcode": "phase",
                    "modifiers": [],
                    "coIssued": False,
                    "predicated": False,
                    "predicate": None,
                    "destination": None,
                    "sources": [],
                    "literals": [],
                    "declaration": None,
                    "text": "phase",
                }
            )
            continue
        if kind == "comment":
            flush()
            current, operands = None, []
            continue
        if kind == "instruction":
            flush()
            current, operands = row, []
            continue
        operands.append(row)
    flush()
    return instructions


def _source_views(operands: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """One view per source operand, and no more.

    A relative-address token is not an operand of its own: it names the address register the
    source before it indexes with, so it is nested in that source's view as `addressRegister`
    rather than published as a source the instruction does not have. `dp4 r0.x, v0, c48[a0]`
    therefore states two sources, which is what the opcode declares and what `text` spells.
    """

    views: list[dict[str, Any]] = []
    for row in operands:
        if row["kind"] == "source":
            views.append(_operand_view(row))
        elif row["kind"] == "relative-address":
            address = _operand_view(row)
            if views:
                views[-1]["addressRegister"] = address
            else:
                views.append(dict(address, kind="relative-address"))
    return views


def _operand_view(row: dict[str, Any] | None) -> dict[str, Any] | None:
    if row is None:
        return None
    view = {
        "registerClass": row["registerClass"],
        "registerIndex": row["registerIndex"],
        "text": row["text"],
    }
    if row["kind"] == "destination":
        view["writeMask"] = row["writeMask"]
    else:
        view["swizzle"] = row["swizzle"]
        view["modifier"] = row["modifier"]
        view["relativeAddress"] = row["relativeAddress"]
    return view


def reassemble(rows: list[dict[str, Any]], payloads: dict[int, bytes] | None = None) -> bytes:
    """The exact bytes the decoded rows came from.

    A comment token's payload is not restated token by token, so `payloads` supplies it keyed by
    the comment token's index; export-time validation passes the payloads it decoded from the
    constant table and the comment table, which is what proves the round trip.
    """

    payloads = payloads or {}
    out = bytearray()
    for row in rows:
        out.extend(struct.pack("<I", int(row["value"]) & 0xFFFFFFFF))
        if row["kind"] == "comment":
            payload = payloads.get(int(row["index"]))
            if payload is None:
                raise TokenStreamError(
                    f"comment token {row['index']} has no payload to reassemble"
                )
            if len(payload) != 4 * int(row["payloadDwords"]):
                raise TokenStreamError(
                    f"comment token {row['index']} declares {row['payloadDwords']} DWORD(s) "
                    f"and carries {len(payload)} byte(s)"
                )
            out.extend(payload)
    return bytes(out)
