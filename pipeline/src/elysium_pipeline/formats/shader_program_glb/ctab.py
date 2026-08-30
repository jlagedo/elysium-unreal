"""The `CTAB` constant table an `fxc/` combo carries inside a comment token.

The table is a self-referential blob: a fixed header of DWORD offsets into itself, an array of
constant descriptions, and the strings, type records and default values those descriptions point
at. It is decoded into named fields and, at the same time, into an ordered `sections[]` account
that partitions the payload -- so the unit can state what the table means without keeping a copy
of it, and `encode` can put the exact bytes back together from what it stated.
"""

from __future__ import annotations

import struct
from typing import Any

FOURCC = b"CTAB"

#: `D3DXREGISTER_SET`: which register file a constant is bound into.
REGISTER_SETS = {0: "bool", 1: "int4", 2: "float4", 3: "sampler"}

#: `D3DXPARAMETER_CLASS`.
PARAMETER_CLASSES = {
    0: "scalar",
    1: "vector",
    2: "matrix-rows",
    3: "matrix-columns",
    4: "object",
    5: "struct",
}

#: `D3DXPARAMETER_TYPE`.
PARAMETER_TYPES = {
    0: "void",
    1: "bool",
    2: "int",
    3: "float",
    4: "string",
    5: "texture",
    6: "texture1d",
    7: "texture2d",
    8: "texture3d",
    9: "texturecube",
    10: "sampler",
    11: "sampler1d",
    12: "sampler2d",
    13: "sampler3d",
    14: "samplercube",
    15: "pixelshader",
    16: "vertexshader",
    17: "pixelfragment",
    18: "vertexfragment",
    19: "unsupported",
}

_HEADER_SIZE = 28
_CONSTANT_SIZE = 20
_TYPE_SIZE = 16


class ConstantTableError(ValueError):
    """The comment payload does not hold the constant table it announces."""


def _string_at(payload: bytes, offset: int) -> tuple[str, int]:
    end = payload.find(b"\0", offset)
    if end < 0:
        raise ConstantTableError(f"a constant-table string at {offset} is not terminated")
    return payload[offset:end].decode("latin-1"), end + 1 - offset


def decode(payload: bytes, source_offset: int) -> dict[str, Any]:
    """Decode one `CTAB` payload. `source_offset` is where the payload starts in the member."""

    if len(payload) < 4 + _HEADER_SIZE or payload[:4] != FOURCC:
        raise ConstantTableError("the comment payload does not open with CTAB")
    body = payload[4:]
    size, creator, version, constants, constant_info, flags, target = struct.unpack_from(
        "<7I", body, 0
    )
    if size != _HEADER_SIZE:
        raise ConstantTableError(f"the constant table declares a {size}-byte header")
    sections: list[dict[str, Any]] = [
        {
            "kind": "four-cc",
            "payloadOffset": 0,
            "sourceOffset": source_offset,
            "byteLength": 4,
            "text": "CTAB",
        },
        {
            "kind": "header",
            "payloadOffset": 4,
            "sourceOffset": source_offset + 4,
            "byteLength": _HEADER_SIZE,
            "fields": {
                "size": size,
                "creatorOffset": creator,
                "version": version,
                "constants": constants,
                "constantInfoOffset": constant_info,
                "flags": flags,
                "targetOffset": target,
            },
        },
    ]
    strings: dict[int, dict[str, Any]] = {}
    types: dict[int, dict[str, Any]] = {}
    members: dict[int, dict[str, Any]] = {}
    defaults: dict[int, dict[str, Any]] = {}

    def string_section(offset: int) -> str:
        if offset in strings:
            return strings[offset]["text"]
        text, length = _string_at(body, offset)
        strings[offset] = {
            "kind": "string",
            "payloadOffset": 4 + offset,
            "sourceOffset": source_offset + 4 + offset,
            "byteLength": length,
            "text": text,
        }
        return text

    def type_section(offset: int) -> dict[str, Any]:
        """The type record at `offset`, and every record it points at.

        A `struct` type names its members through a table of `(name, type)` pairs, and those
        member types are themselves records elsewhere in the payload; following them is what
        keeps the table's own bytes accounted for instead of left over as fill.
        """

        if offset in types:
            return types[offset]["fields"]
        klass, kind, type_rows, columns, elements, count, member_info = struct.unpack_from(
            "<6HI", body, offset
        )
        fields: dict[str, Any] = {
            "class": PARAMETER_CLASSES.get(klass, f"unknown-{klass}"),
            "classValue": klass,
            "type": PARAMETER_TYPES.get(kind, f"unknown-{kind}"),
            "typeValue": kind,
            "rows": type_rows,
            "columns": columns,
            "elements": elements,
            "structMembers": count,
            "structMemberInfoOffset": member_info,
        }
        types[offset] = {
            "kind": "type-info",
            "payloadOffset": 4 + offset,
            "sourceOffset": source_offset + 4 + offset,
            "byteLength": _TYPE_SIZE,
            "fields": fields,
        }
        if count and member_info:
            pairs = [
                struct.unpack_from("<2I", body, member_info + 8 * position)
                for position in range(count)
            ]
            members[member_info] = {
                "kind": "struct-member-info",
                "payloadOffset": 4 + member_info,
                "sourceOffset": source_offset + 4 + member_info,
                "byteLength": 8 * count,
                "members": [
                    {"nameOffset": name_at, "typeInfoOffset": type_at}
                    for name_at, type_at in pairs
                ],
            }
            rows_out = []
            for name_at, type_at in pairs:
                member_fields = type_section(type_at)
                rows_out.append(
                    {
                        "name": string_section(name_at),
                        "nameOffset": name_at,
                        "typeInfoOffset": type_at,
                        "class": member_fields["class"],
                        "type": member_fields["type"],
                        "rows": member_fields["rows"],
                        "columns": member_fields["columns"],
                        "elements": member_fields["elements"],
                    }
                )
            fields["members"] = rows_out
        return fields

    creator_text = string_section(creator)
    target_text = string_section(target)

    rows: list[dict[str, Any]] = []
    for position in range(constants):
        at = constant_info + _CONSTANT_SIZE * position
        if at + _CONSTANT_SIZE > len(body):
            raise ConstantTableError(f"constant {position} runs past the constant table")
        name, register_set, register_index, register_count, reserved, type_info, default = (
            struct.unpack_from("<IHHHHII", body, at)
        )
        name_text = string_section(name)
        type_row = type_section(type_info)
        default_values: list[float] | None = None
        if default:
            words = 4 * max(register_count, 1)
            if default not in defaults:
                raw = struct.unpack_from(f"<{words}I", body, default)
                values = [struct.unpack("<f", struct.pack("<I", word))[0] for word in raw]
                defaults[default] = {
                    "kind": "default-value",
                    "payloadOffset": 4 + default,
                    "sourceOffset": source_offset + 4 + default,
                    "byteLength": 4 * words,
                    "values": values,
                }
            default_values = list(defaults[default]["values"])
        rows.append(
            {
                "index": position,
                "sourceOffset": source_offset + 4 + at,
                "name": name_text,
                "registerSet": REGISTER_SETS.get(register_set, f"unknown-{register_set}"),
                "registerSetValue": register_set,
                "registerIndex": register_index,
                "registerCount": register_count,
                "reserved": reserved,
                "class": type_row["class"],
                "type": type_row["type"],
                "rows": type_row["rows"],
                "columns": type_row["columns"],
                "elements": type_row["elements"],
                "structMembers": type_row["structMembers"],
                "members": type_row.get("members"),
                "typeInfoOffset": type_info,
                "nameOffset": name,
                "defaultValueOffset": default,
                "defaultValues": default_values,
            }
        )
    if constants:
        sections.append(
            {
                "kind": "constant-info",
                "payloadOffset": 4 + constant_info,
                "sourceOffset": source_offset + 4 + constant_info,
                "byteLength": _CONSTANT_SIZE * constants,
                "constants": [
                    {
                        "nameOffset": row["nameOffset"],
                        "registerSetValue": row["registerSetValue"],
                        "registerIndex": row["registerIndex"],
                        "registerCount": row["registerCount"],
                        "reserved": row["reserved"],
                        "typeInfoOffset": row["typeInfoOffset"],
                        "defaultValueOffset": row["defaultValueOffset"],
                    }
                    for row in rows
                ],
            }
        )
    sections.extend(types.values())
    sections.extend(members.values())
    sections.extend(defaults.values())
    sections.extend(strings.values())
    sections.sort(key=lambda section: section["payloadOffset"])
    sections, unaccounted = _close_gaps(sections, payload, source_offset)
    return {
        "sourceOffset": source_offset,
        "byteLength": len(payload),
        "creator": creator_text,
        "target": target_text,
        "version": version,
        "shaderVersionToken": version,
        "flags": flags,
        "constants": rows,
        "sections": sections,
        "unaccountedBytes": unaccounted,
    }


def _close_gaps(
    sections: list[dict[str, Any]], payload: bytes, source_offset: int
) -> tuple[list[dict[str, Any]], int]:
    """Fill the payload's unclaimed regions so `sections[]` partitions it end to end.

    A run of one repeated byte becomes a `fill` section, which is how the D3DX writer pads with
    `0xAB`; anything else is carried verbatim as a hex `unaccounted` section, so the payload is
    still reproducible and the departure is visible rather than silent.
    """

    closed: list[dict[str, Any]] = []
    cursor = 0
    unaccounted = 0
    for section in sections + [{"payloadOffset": len(payload), "byteLength": 0}]:
        start = int(section["payloadOffset"])
        if start > cursor:
            gap = payload[cursor:start]
            if len(set(gap)) == 1:
                closed.append(
                    {
                        "kind": "fill",
                        "payloadOffset": cursor,
                        "sourceOffset": source_offset + cursor,
                        "byteLength": len(gap),
                        "fillByte": gap[0],
                    }
                )
            else:
                unaccounted += len(gap)
                closed.append(
                    {
                        "kind": "unaccounted",
                        "payloadOffset": cursor,
                        "sourceOffset": source_offset + cursor,
                        "byteLength": len(gap),
                        "hex": gap.hex(),
                    }
                )
            cursor = start
        elif start < cursor:
            raise ConstantTableError(
                f"constant-table sections overlap at payload offset {start}"
            )
        if section.get("kind") is None:
            break
        cursor = start + int(section["byteLength"])
        closed.append(section)
    if cursor != len(payload):
        raise ConstantTableError(
            f"constant-table sections account {cursor}/{len(payload)} payload bytes"
        )
    return closed, unaccounted


def encode(table: dict[str, Any]) -> bytes:
    """The payload bytes back, section by section.

    The sections partition the payload, so writing each one at its own offset reproduces it
    exactly; this is what lets the combo's token stream be reassembled without the unit keeping a
    copy of the compiled bytes.
    """

    out = bytearray(int(table["byteLength"]))
    written = 0
    for section in table["sections"]:
        offset = int(section["payloadOffset"])
        length = int(section["byteLength"])
        kind = section["kind"]
        if kind == "four-cc":
            chunk = str(section["text"]).encode("latin-1")
        elif kind == "header":
            fields = section["fields"]
            chunk = struct.pack(
                "<7I",
                int(fields["size"]),
                int(fields["creatorOffset"]),
                int(fields["version"]),
                int(fields["constants"]),
                int(fields["constantInfoOffset"]),
                int(fields["flags"]),
                int(fields["targetOffset"]),
            )
        elif kind == "constant-info":
            chunk = b"".join(
                struct.pack(
                    "<IHHHHII",
                    int(row["nameOffset"]),
                    int(row["registerSetValue"]),
                    int(row["registerIndex"]),
                    int(row["registerCount"]),
                    int(row["reserved"]),
                    int(row["typeInfoOffset"]),
                    int(row["defaultValueOffset"]),
                )
                for row in section["constants"]
            )
        elif kind == "type-info":
            fields = section["fields"]
            chunk = struct.pack(
                "<6HI",
                int(fields["classValue"]),
                int(fields["typeValue"]),
                int(fields["rows"]),
                int(fields["columns"]),
                int(fields["elements"]),
                int(fields["structMembers"]),
                int(fields["structMemberInfoOffset"]),
            )
        elif kind == "struct-member-info":
            chunk = b"".join(
                struct.pack("<2I", int(row["nameOffset"]), int(row["typeInfoOffset"]))
                for row in section["members"]
            )
        elif kind == "default-value":
            chunk = b"".join(
                struct.pack("<f", float(value)) for value in section["values"]
            )
        elif kind == "string":
            chunk = str(section["text"]).encode("latin-1") + b"\0"
        elif kind == "fill":
            chunk = bytes([int(section["fillByte"])]) * length
        elif kind == "unaccounted":
            chunk = bytes.fromhex(str(section["hex"]))
        else:
            raise ConstantTableError(f"unknown constant-table section {kind!r}")
        if len(chunk) != length:
            raise ConstantTableError(
                f"constant-table section {kind} encodes {len(chunk)} of {length} bytes"
            )
        out[offset:offset + length] = chunk
        written += length
    if written != len(out):
        raise ConstantTableError(f"constant-table sections encode {written}/{len(out)} bytes")
    return bytes(out)
