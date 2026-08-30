"""Complete projection of one model's legacy VPhysics PHY companion.

A static prop and a ragdolled body carry the same container: solids of convex ledges plus a
flat KeyValues tail. Neither is treated as the other's special case here.
"""

from __future__ import annotations

import struct
from typing import Any


class ModelPhysicsError(ValueError):
    """A PHY companion is malformed or names an unsupported container shape."""


HEADER = struct.Struct("<iiii")
LEDGE = struct.Struct("<iiIhh")
LEDGE_NODE_SIZE = 28
LEDGE_SIZE = 16
TRIANGLE_SIZE = 16
POINT_SIZE = 16


def _leaf_ledges(
    data: bytes,
    node: int,
    found: list[int],
    visited: set[int],
    nodes: list[dict[str, Any]] | None = None,
) -> None:
    if node in visited:
        raise ModelPhysicsError(f"ledge tree revisits node at {node}")
    if node < 0 or node + LEDGE_NODE_SIZE > len(data):
        raise ModelPhysicsError(f"ledge node at {node} runs outside the PHY")
    visited.add(node)
    right, ledge = struct.unpack_from("<ii", data, node)
    if nodes is not None:
        nodes.append(
            {
                "offset": node,
                "rightNodeOffset": right,
                "compactLedgeOffset": ledge,
                "center": struct.unpack_from("<3f", data, node + 8),
                "radius": struct.unpack_from("<f", data, node + 20)[0],
                "boxSizes": list(data[node + 24:node + 27]),
                "padding": data[node + 27],
                "leaf": right == 0,
            }
        )
    if right == 0:
        found.append(node + ledge)
        return
    _leaf_ledges(data, node + LEDGE_NODE_SIZE, found, visited, nodes)
    _leaf_ledges(data, node + right, found, visited, nodes)


def _ledge(data: bytes, offset: int) -> dict[str, Any]:
    point_offset, node_offset, packed, triangle_count, padding = LEDGE.unpack_from(
        data, offset
    )
    if triangle_count < 1:
        raise ModelPhysicsError(
            f"ledge at {offset} declares {triangle_count} triangles"
        )
    source_triangles = []
    triangle_records = []
    for triangle in range(triangle_count):
        base = offset + LEDGE_SIZE + triangle * TRIANGLE_SIZE
        triangle_packed = struct.unpack_from("<I", data, base)[0]
        edges = []
        for edge in range(3):
            raw = struct.unpack_from("<I", data, base + 4 + edge * 4)[0]
            opposite = (raw >> 16) & 0x7FFF
            if opposite & 0x4000:
                opposite -= 0x8000
            edges.append(
                {
                    "raw": raw,
                    "startPoint": raw & 0xFFFF,
                    "oppositeIndex": opposite,
                    "virtual": bool(raw & 0x80000000),
                }
            )
        source_triangles.append(tuple(edge["startPoint"] for edge in edges))
        triangle_records.append({"packed": triangle_packed, "edges": edges})
    point_base = offset + point_offset
    remap: dict[int, int] = {}
    vertices = []
    source_points = []
    for triangle in source_triangles:
        for source in triangle:
            if source in remap:
                continue
            remap[source] = len(vertices)
            x, y, z, w = struct.unpack_from(
                "<4f", data, point_base + source * POINT_SIZE
            )
            # IVP -> Unreal is (x,-z,-y); Unreal -> glTF is (x,z,y).
            position = (float(x), float(-y), float(-z))
            vertices.append(position)
            # The position itself becomes the hull's core accessor entry at this same ordinal;
            # only the fourth IVP component has nowhere else to live.
            source_points.append({"index": source, "ivpW": float(w)})
    triangles = [tuple(remap[index] for index in triangle) for triangle in source_triangles]
    if len(triangles) != 2 * len(vertices) - 4:
        raise ModelPhysicsError(
            f"ledge at {offset} is not convex: {len(vertices)} vertices, "
            f"{len(triangles)} triangles"
        )
    return {
        # The ledge header's own file offset, so the `phy.solids[i].ledges[j].header` ledger
        # range and the record it paid for name the same place.
        "sourceOffset": offset,
        "nodeOffset": node_offset,
        "flags": packed & 0xFF,
        "sizeDiv16": packed >> 8,
        "padding": padding,
        "sourcePoints": source_points,
        "triangleRecords": triangle_records,
        "vertices": vertices,
        "triangles": triangles,
    }


def _keyvalue_tokens(text: str) -> list[tuple[str, int]]:
    """Tokenise flat Valve KeyValues without treating path backslashes as escapes."""

    tokens: list[tuple[str, int]] = []
    position = 0
    line = 1
    while position < len(text):
        char = text[position]
        if char in " \t\r\0":
            position += 1
            continue
        if char == "\n":
            line += 1
            position += 1
            continue
        if text.startswith("//", position):
            end = text.find("\n", position + 2)
            position = len(text) if end < 0 else end
            continue
        if char in "{}":
            tokens.append((char, line))
            position += 1
            continue
        if char == '"':
            token_line = line
            position += 1
            value = []
            while position < len(text):
                char = text[position]
                if char == '"':
                    position += 1
                    break
                if char == "\n":
                    line += 1
                if char == "\\" and position + 1 < len(text) and text[position + 1] in {
                    '"', "\\"
                }:
                    value.append(text[position + 1])
                    position += 2
                    continue
                value.append(char)
                position += 1
            else:
                raise ModelPhysicsError(
                    f"PHY KeyValues line {token_line}: unterminated quoted string"
                )
            tokens.append(("".join(value), token_line))
            continue
        token_line = line
        start = position
        while position < len(text):
            if text[position].isspace() or text[position] in "{}\0":
                break
            if text.startswith("//", position):
                break
            position += 1
        if position == start:
            raise ModelPhysicsError(
                f"PHY KeyValues line {line}: unexpected character {text[position]!r}"
            )
        tokens.append((text[start:position], token_line))
    return tokens


def _blocks(text: str) -> list[dict[str, Any]]:
    """Parse flat KeyValues blocks regardless of where braces and fields line-wrap."""

    tokens = _keyvalue_tokens(text)
    blocks = []
    cursor = 0
    while cursor < len(tokens):
        name, line = tokens[cursor]
        cursor += 1
        if name in {"{", "}"}:
            raise ModelPhysicsError(
                f"PHY KeyValues line {line}: expected block name, got {name!r}"
            )
        if cursor >= len(tokens) or tokens[cursor][0] != "{":
            raise ModelPhysicsError(
                f"PHY KeyValues line {line}: block {name!r} has no '{{'"
            )
        cursor += 1
        values: dict[str, str] = {}
        pairs: list[dict[str, str]] = []
        while cursor < len(tokens) and tokens[cursor][0] != "}":
            key, key_line = tokens[cursor]
            cursor += 1
            if key == "{":
                raise ModelPhysicsError(
                    f"PHY KeyValues line {key_line}: nested blocks are unsupported"
                )
            if cursor >= len(tokens) or tokens[cursor][0] in {"{", "}"}:
                raise ModelPhysicsError(
                    f"PHY KeyValues line {key_line}: field {key!r} has no value"
                )
            value, _value_line = tokens[cursor]
            cursor += 1
            values[key] = value
            pairs.append({"key": key, "value": value})
        if cursor >= len(tokens):
            raise ModelPhysicsError(
                f"PHY KeyValues line {line}: block {name!r} has no closing '}}'"
            )
        cursor += 1
        blocks.append({"type": name.lower(), "values": values, "pairs": pairs})
    return blocks


def _typed_values(kind: str, values: dict[str, str]) -> dict[str, Any]:
    integer = {"index", "health"}
    floating = {
        "mass", "damping", "rotdamping", "inertia", "volume", "massbias",
        "totalmass", "xmin", "xmax", "xfriction", "ymin", "ymax",
        "yfriction", "zmin", "zmax", "zfriction",
    }
    vectors = {"origin", "angles"}
    if kind == "ragdollconstraint":
        integer.update(("parent", "child"))
    result: dict[str, Any] = {}
    for key, value in values.items():
        lowered = key.lower()
        try:
            if lowered in integer:
                result[key] = int(value, 0)
            elif lowered in floating:
                result[key] = float(value)
            elif lowered in vectors:
                components = [float(component) for component in value.split()]
                if len(components) != 3:
                    raise ValueError("expected three components")
                result[key] = components
            else:
                result[key] = value
        except ValueError as error:
            raise ModelPhysicsError(
                f"PHY {kind} field {key!r} has invalid value {value!r}"
            ) from error
    return result


def decode(data: bytes) -> dict[str, Any]:
    """Decode binary solids, convex ledges, and the complete flat KeyValues tail."""
    if len(data) < HEADER.size:
        raise ModelPhysicsError(f"PHY is only {len(data)} bytes")
    header_size, ident, solid_count, checksum = HEADER.unpack_from(data)
    if header_size < HEADER.size or solid_count < 1:
        raise ModelPhysicsError(
            f"PHY header size={header_size}, solidCount={solid_count}"
        )
    position = header_size
    solids = []
    for index in range(solid_count):
        if position + 4 > len(data):
            raise ModelPhysicsError(f"solid {index} has no size field")
        solid_size = struct.unpack_from("<i", data, position)[0]
        body = position + 4
        if solid_size < 48 or body + solid_size > len(data):
            raise ModelPhysicsError(
                f"solid {index} size {solid_size} overruns {len(data)} bytes"
            )
        mass_center = struct.unpack_from("<3f", data, body)
        rotation_inertia = struct.unpack_from("<3f", data, body + 12)
        radius = struct.unpack_from("<f", data, body + 24)[0]
        packed = struct.unpack_from("<I", data, body + 28)[0]
        byte_size = packed >> 8
        if byte_size != solid_size:
            raise ModelPhysicsError(
                f"solid {index} byte_size {byte_size} != size {solid_size}"
            )
        root = struct.unpack_from("<i", data, body + 32)[0]
        dummy = struct.unpack_from("<2i", data, body + 36)
        magic = data[body + 44:body + 48]
        if magic not in (b"IVPS", b"\0\0\0\0"):
            raise ModelPhysicsError(f"solid {index} has magic {magic!r}")
        ledge_offsets: list[int] = []
        ledge_nodes: list[dict[str, Any]] = []
        _leaf_ledges(data, body + root, ledge_offsets, set(), ledge_nodes)
        solids.append(
            {
                "index": index,
                "sourceOffset": position,
                "size": solid_size,
                "byteSize": byte_size,
                "massCenter": mass_center,
                "rotationInertia": rotation_inertia,
                "upperLimitRadius": radius,
                "maxDeviation": packed & 0xFF,
                "dummy": dummy,
                "ledgeTreeRootOffset": root,
                "magic": magic.decode("ascii", "replace").rstrip("\0"),
                "ledgeNodes": ledge_nodes,
                "hulls": [_ledge(data, offset) for offset in ledge_offsets],
            }
        )
        position = body + solid_size

    tail = data[position:].decode("ascii", "replace")
    blocks = _blocks(tail)
    solid_blocks = [block for block in blocks if block["type"] == "solid"]
    # A KeyValues tail that names a different number of solids than the binary declares is a
    # retail fact on some props, so it is reported as `phy-solid-count-mismatch` rather than
    # refused: the binary solids are still complete and the blocks still pair in file order.
    for solid, block in zip(solids, solid_blocks):
        solid["properties"] = _typed_values("solid", block["values"])
    for solid in solids[len(solid_blocks):]:
        solid["properties"] = {}
    return {
        "header": {
            "size": header_size,
            "id": ident,
            "solidCount": solid_count,
            "solidBlockCount": len(solid_blocks),
            "checksum": checksum,
        },
        "coordinateSystem": "IVP metres, axis-only",
        "solids": solids,
        "editParams": [
            _typed_values("editparams", block["values"])
            for block in blocks
            if block["type"] == "editparams"
        ],
        "constraints": [
            _typed_values("ragdollconstraint", block["values"])
            for block in blocks
            if block["type"] == "ragdollconstraint"
        ],
        "breaks": [
            _typed_values("break", block["values"])
            for block in blocks
            if block["type"] == "break"
        ],
        # The ordered, verbatim projection of the tail: one row per block, in file order, with the
        # key/value pairs exactly as the file spells them. The typed reading of a block lives in
        # `solids[i].properties`, `editParams`, `constraints` or `breaks` and is not repeated
        # here, so no block is stated twice.
        "keyValues": [
            {"type": block["type"], "pairs": block["pairs"]}
            for block in blocks
        ],
    }
