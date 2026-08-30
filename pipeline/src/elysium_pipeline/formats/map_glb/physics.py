"""Lump 29: the VPhysics collision of the world and every brush model.

The lump is a run of `dphysmodel_t` headers, each followed by its solids' IVPS collision data and
a flat KeyValues tail, closed by a header whose `modelIndex` is -1. Every solid is a compact
surface whose ledge tree's leaves are convex hulls; the hulls' vertices and triangles go to two
BIN accessors and the ledge table locates each hull in them.

Positions stay in `IVP metres, axis-only`, exactly as a model's PHY does: the axis map is
`(x, y, z)_gltf = (x, -y, -z)_ivp` and no scale is applied, because the IVPS solver's units are
already metres.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import math
import struct
from typing import Any, Callable

MODEL_HEADER = struct.Struct("<4i")
SURFACE_HEADER_BYTES = 48
LEDGE_NODE_BYTES = 28
LEDGE_HEADER_BYTES = 16
TRIANGLE_BYTES = 16
POINT_BYTES = 16
IVPS_MAGIC = b"IVPS"
#: 174 of the 7,070 solids in the corpus are Havok MOPP code rather than an IVPS compact
#: surface: the same 48-byte legacy header, `MOPP` where the IVPS id sits, and a payload no
#: public description covers. The bytes are carried verbatim in a described BIN accessor and the
#: solid enters `typedUnidentified`, because dropping them would leave the lump unaccounted.
MOPP_MAGIC = b"MOPP"

#: The header that closes the lump. Both fields are -1 on every one of the 108 maps.
TERMINATOR_MODEL_INDEX = -1


class MapPhysicsError(ValueError):
    """The PHYSCOLLIDE lump is not the compact-surface container this seam accounts for."""


@dataclass(slots=True)
class Window:
    """A byte window that hands out each of its bytes to exactly one owner.

    A compact surface addresses its points through per-ledge offsets, so two ledges can name one
    point range. The window trims a claim to the bytes nobody has claimed yet, which keeps the
    ledger free of overlaps without inventing a second owner for a shared byte.
    """

    offset: int
    length: int
    covered: bytearray = field(init=False)
    claims: list[tuple[int, int, str, str]] = field(default_factory=list)

    def __post_init__(self) -> None:
        self.covered = bytearray(self.length)

    def claim(self, start: int, size: int, state: str, owner: str) -> None:
        if size <= 0:
            return
        local = start - self.offset
        if local < 0 or local + size > self.length:
            raise MapPhysicsError(f"{owner} range {start}+{size} leaves the PHYSCOLLIDE lump")
        run_start = None
        for position in range(local, local + size):
            if self.covered[position]:
                if run_start is not None:
                    self._emit(run_start, position - run_start, state, owner)
                    run_start = None
                continue
            self.covered[position] = 1
            if run_start is None:
                run_start = position
        if run_start is not None:
            self._emit(run_start, local + size - run_start, state, owner)

    def _emit(self, local: int, size: int, state: str, owner: str) -> None:
        self.claims.append((self.offset + local, size, state, owner))

    def fill(self, classify: Callable[[int, int], tuple[str, str]]) -> None:
        """Give every byte still unowned to the classifier, one maximal run at a time."""

        position = 0
        while position < self.length:
            if self.covered[position]:
                position += 1
                continue
            end = position
            while end < self.length and not self.covered[end]:
                end += 1
            state, owner = classify(self.offset + position, end - position)
            for index in range(position, end):
                self.covered[index] = 1
            self._emit(position, end - position, state, owner)
            position = end


@dataclass(slots=True)
class PhysicsDecode:
    models: list[dict[str, Any]]
    vertices: list[tuple[float, float, float]]
    indices: list[int]
    claims: list[tuple[int, int, str, str]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    surface_properties: list[str]
    mopp: list[tuple[int, int, str]]


def _finite(values, where: str):
    for value in values:
        if not math.isfinite(value):
            raise MapPhysicsError(f"{where} holds a non-finite float")
    return values


def _ledge_offsets(
    data: bytes, root: int, window_end: int, nodes: list[dict[str, Any]]
) -> list[int]:
    """Walk the ledge tree iteratively; recursion depth is a property of the compiler, not us."""

    found: list[int] = []
    visited: set[int] = set()
    stack = [root]
    while stack:
        node = stack.pop()
        if node in visited:
            raise MapPhysicsError(f"ledge tree revisits node at {node}")
        if node < 0 or node + LEDGE_NODE_BYTES > window_end:
            raise MapPhysicsError(f"ledge node at {node} runs outside the solid")
        visited.add(node)
        right, ledge = struct.unpack_from("<ii", data, node)
        nodes.append((node, LEDGE_NODE_BYTES))
        if right == 0:
            found.append(node + ledge)
            continue
        stack.append(node + right)
        stack.append(node + LEDGE_NODE_BYTES)
    return found


def _keyvalue_blocks(text: str) -> list[dict[str, Any]]:
    """Flat `name { "key" "value" }` blocks, the only shape the lump's tail carries."""

    blocks: list[dict[str, Any]] = []
    position = 0
    length = len(text)
    while position < length:
        while position < length and (text[position].isspace() or text[position] == "\0"):
            position += 1
        if position >= length:
            break
        start = position
        while position < length and not text[position].isspace() and text[position] != "{":
            position += 1
        name = text[start:position].strip()
        while position < length and text[position].isspace():
            position += 1
        if position >= length or text[position] != "{":
            raise MapPhysicsError(f"PHYSCOLLIDE key-value block {name!r} has no opening brace")
        position += 1
        pairs: list[dict[str, str]] = []
        while True:
            while position < length and (text[position].isspace() or text[position] == "\0"):
                position += 1
            if position >= length:
                raise MapPhysicsError(f"PHYSCOLLIDE block {name!r} has no closing brace")
            if text[position] == "}":
                position += 1
                break
            tokens = []
            for _ in range(2):
                while position < length and text[position].isspace():
                    position += 1
                if position < length and text[position] == '"':
                    position += 1
                    token_start = position
                    while position < length and text[position] != '"':
                        position += 1
                    tokens.append(text[token_start:position])
                    position += 1
                else:
                    token_start = position
                    while (
                        position < length
                        and not text[position].isspace()
                        and text[position] not in '{}"'
                    ):
                        position += 1
                    tokens.append(text[token_start:position])
            pairs.append({"key": tokens[0], "value": tokens[1]})
        blocks.append({"type": name.lower(), "pairs": pairs})
    return blocks


def decode(data: bytes, offset: int, length: int) -> PhysicsDecode:
    """Decode the whole lump in file coordinates and account for every one of its bytes."""

    window = Window(offset, length)
    end = offset + length
    models: list[dict[str, Any]] = []
    vertices: list[tuple[float, float, float]] = []
    indices: list[int] = []
    anomalies: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    surface_properties: list[str] = []
    mopp: list[tuple[int, int, str]] = []
    cursor = offset
    while cursor + MODEL_HEADER.size <= end:
        model_index, data_size, keydata_size, solid_count = MODEL_HEADER.unpack_from(data, cursor)
        window.claim(cursor, MODEL_HEADER.size, "mapped", f"physics.models[{len(models)}].header")
        if model_index == TERMINATOR_MODEL_INDEX:
            cursor += MODEL_HEADER.size
            break
        if data_size < 0 or keydata_size < 0 or solid_count < 0:
            raise MapPhysicsError(f"physics model {model_index} declares negative sizes")
        body = cursor + MODEL_HEADER.size
        if body + data_size + keydata_size > end:
            raise MapPhysicsError(f"physics model {model_index} runs past the lump")
        solids: list[dict[str, Any]] = []
        solid_cursor = body
        for solid_index in range(solid_count):
            if solid_cursor + 4 > body + data_size:
                raise MapPhysicsError(
                    f"physics model {model_index} solid {solid_index} has no size field"
                )
            solid_size = struct.unpack_from("<i", data, solid_cursor)[0]
            surface = solid_cursor + 4
            if solid_size < SURFACE_HEADER_BYTES or surface + solid_size > body + data_size:
                raise MapPhysicsError(
                    f"physics model {model_index} solid {solid_index} declares {solid_size} bytes"
                )
            owner = f"physics.models[{len(models)}].solids[{solid_index}]"
            window.claim(solid_cursor, 4, "mapped", f"{owner}.size")
            window.claim(surface, SURFACE_HEADER_BYTES, "mapped", f"{owner}.surfaceHeader")
            mass_center = _finite(struct.unpack_from("<3f", data, surface), f"{owner}.massCenter")
            inertia = _finite(struct.unpack_from("<3f", data, surface + 12), f"{owner}.inertia")
            radius = _finite(struct.unpack_from("<f", data, surface + 24), f"{owner}.radius")[0]
            packed = struct.unpack_from("<I", data, surface + 28)[0]
            root = struct.unpack_from("<i", data, surface + 32)[0]
            dummy = struct.unpack_from("<2i", data, surface + 36)
            magic = data[surface + 44:surface + 48]
            if magic not in (IVPS_MAGIC, MOPP_MAGIC):
                raise MapPhysicsError(
                    f"{owner} opens with {magic!r}, not {IVPS_MAGIC!r} or {MOPP_MAGIC!r}"
                )
            solid_end = surface + solid_size
            record = {
                "index": solid_index,
                "sourceOffset": solid_cursor,
                "byteLength": solid_size + 4,
                "massCenter": mass_center,
                "rotationInertia": inertia,
                "upperLimitRadius": radius,
                "maxDeviation": packed & 0xFF,
                "sizeDiv16": packed >> 8,
                "ledgeTreeRootOffset": int(root),
                "dummy": [int(value) for value in dummy],
                "magic": magic.decode("ascii"),
            }
            if magic == MOPP_MAGIC:
                payload_offset = surface + SURFACE_HEADER_BYTES
                payload_length = solid_end - payload_offset
                window.claim(payload_offset, payload_length, "mapped", f"{owner}.moppCode")
                record["kind"] = "havok-mopp"
                record["moppCode"] = {
                    "sourceOffset": payload_offset,
                    "byteLength": payload_length,
                    "sha256": hashlib.sha256(
                        data[payload_offset:payload_offset + payload_length]
                    ).hexdigest(),
                }
                mopp.append((payload_offset, payload_length, owner))
                solids.append(record)
                solid_cursor = solid_end
                continue
            node_spans: list[tuple[int, int]] = []
            ledge_offsets = _ledge_offsets(data, surface + root, solid_end, node_spans)
            for node_offset, node_size in node_spans:
                window.claim(node_offset, node_size, "mapped", f"{owner}.ledgeTree")
            ledges: list[dict[str, Any]] = []
            for ledge_index, ledge_offset in enumerate(sorted(ledge_offsets)):
                ledges.append(
                    _ledge(
                        data,
                        ledge_offset,
                        solid_end,
                        window,
                        f"{owner}.ledges[{ledge_index}]",
                        ledge_index,
                        vertices,
                        indices,
                        anomalies,
                    )
                )
            record["kind"] = "ivps-compact-surface"
            record["nodeCount"] = len(node_spans)
            record["ledges"] = ledges
            solids.append(record)
            solid_cursor = solid_end
        key_offset = body + data_size
        key_text = data[key_offset:key_offset + keydata_size].decode("latin-1")
        blocks = _keyvalue_blocks(key_text)
        if keydata_size:
            window.claim(key_offset, keydata_size, "mapped-text", f"physics.models[{len(models)}].keyValues")
        for block in blocks:
            for pair in block["pairs"]:
                if pair["key"].lower() == "surfaceprop":
                    surface_properties.append(pair["value"])
        models.append(
            {
                "index": len(models),
                "sourceOffset": cursor,
                "modelIndex": int(model_index),
                "dataSize": int(data_size),
                "keydataSize": int(keydata_size),
                "solidCount": int(solid_count),
                "keyValues": blocks,
                "keyValuesSourceOffset": key_offset,
                "solids": solids,
            }
        )
        cursor = key_offset + keydata_size

    unreferenced: list[tuple[int, int]] = []

    def _classify(start: int, size: int) -> tuple[str, str]:
        chunk = data[start:start + size]
        if not any(chunk):
            return "padding-zero", "physics.padding"
        unreferenced.append((start, size))
        return "omitted-proven", "physics.unreferenced"

    window.fill(_classify)
    if unreferenced:
        # One row for the whole class: a compact surface stores its points as one cloud per
        # ledge and the tree names only the ledges it uses, so the storage no triangle indexes
        # is dead the same way everywhere in the lump.
        omissions.append(
            {
                "role": "unused-lump-bytes",
                "lump": 29,
                "runs": len(unreferenced),
                "byteLength": sum(size for _, size in unreferenced),
                "sourceOffsets": [start for start, _ in unreferenced[:32]],
                "evidence": "no compact-surface record of lump 29 addresses these ranges",
            }
        )
    return PhysicsDecode(
        models=models,
        vertices=vertices,
        indices=indices,
        claims=sorted(window.claims),
        anomalies=anomalies,
        omissions=omissions,
        surface_properties=surface_properties,
        mopp=mopp,
    )


def _ledge(
    data: bytes,
    offset: int,
    window_end: int,
    window: Window,
    owner: str,
    index: int,
    vertices: list[tuple[float, float, float]],
    indices: list[int],
    anomalies: list[dict[str, Any]],
) -> dict[str, Any]:
    if offset + LEDGE_HEADER_BYTES > window_end:
        raise MapPhysicsError(f"{owner} header runs outside the solid")
    point_offset, node_offset, packed, triangle_count, reserved = struct.unpack_from(
        "<iiIhh", data, offset
    )
    if triangle_count < 1:
        raise MapPhysicsError(f"{owner} declares {triangle_count} triangles")
    triangle_start = offset + LEDGE_HEADER_BYTES
    if triangle_start + triangle_count * TRIANGLE_BYTES > window_end:
        raise MapPhysicsError(f"{owner} triangles run outside the solid")
    window.claim(offset, LEDGE_HEADER_BYTES, "mapped", f"{owner}.header")
    window.claim(
        triangle_start, triangle_count * TRIANGLE_BYTES, "mapped", f"{owner}.triangles"
    )
    point_base = offset + point_offset
    remap: dict[int, int] = {}
    first_vertex = len(vertices)
    first_index = len(indices)
    virtual_edges = 0
    for triangle in range(triangle_count):
        base = triangle_start + triangle * TRIANGLE_BYTES
        corners = []
        for edge in range(3):
            raw = struct.unpack_from("<I", data, base + 4 + edge * 4)[0]
            if raw & 0x80000000:
                virtual_edges += 1
            corners.append(raw & 0xFFFF)
        for source in corners:
            if source in remap:
                continue
            point = point_base + source * POINT_BYTES
            if point + POINT_BYTES > window_end:
                raise MapPhysicsError(f"{owner} point {source} runs outside the solid")
            window.claim(point, POINT_BYTES, "mapped", f"{owner}.points")
            x, y, z, _w = _finite(
                struct.unpack_from("<4f", data, point), f"{owner}.points[{source}]"
            )
            remap[source] = len(vertices)
            vertices.append((float(x), float(-y), float(-z)))
        indices.extend(remap[source] for source in corners)
    vertex_count = len(vertices) - first_vertex
    if triangle_count != 2 * vertex_count - 4:
        anomalies.append(
            {
                "role": "non-convex-hull",
                "owner": owner,
                "sourceOffset": offset,
                "vertices": vertex_count,
                "triangles": triangle_count,
                "evidence": "a convex hull satisfies triangles = 2*vertices - 4",
            }
        )
    return {
        "index": index,
        "sourceOffset": offset,
        "pointOffset": int(point_offset),
        "ledgeTreeNodeOffset": int(node_offset),
        "flags": packed & 0xFF,
        "sizeDiv16": packed >> 8,
        "reserved": int(reserved),
        "triangleCount": int(triangle_count),
        "virtualEdges": virtual_edges,
        "firstVertex": first_vertex,
        "vertexCount": vertex_count,
        "firstIndex": first_index,
        "indexCount": triangle_count * 3,
    }
