"""Isolated one-map Nav-graph GLB product writer.

Builds a scene-carrying unit when the map has nodes to place (one glTF node per graph node, one
mesh node holding the link primitive), and a scene-less extension-only unit when the graph is
empty -- eight of the retail corpus's 100 `.ain` files declare `NumNodes: 0`.
"""

from __future__ import annotations

import math
import struct
from pathlib import Path
from typing import Any

from elysium_pipeline.formats.nav_graph_glb.decode import decode_nav_graph
from elysium_pipeline.formats.nav_graph_glb.model import (
    NAV_GRAPH_EXTENSION,
    SCHEMA_VERSION,
    NavGraphModel,
    output_relative_path,
)
from elysium_pipeline.formats.nav_graph_glb.source import load_source_closure
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    coverage_block,
    extension_root,
    identity_block,
    source_resolution,
    write_glb,
)

#: The unit contract's fixed Source-inches -> glTF-metres rotation, shared by every spatial seam.
COORDINATE_TRANSFORM = {
    "source": "Source inches, Z-up, right-handed",
    "destination": "glTF metres, Y-up, right-handed",
    "scale": 0.0254,
    "position": "(x, y, z)_gltf = (x, z, -y)_source * 0.0254",
    "direction": "(x, y, z)_gltf = (x, z, -y)_source",
    "quaternion": "(x, y, z, w)_gltf = (x, z, -y, w)_source",
    "domains": {
        "nodes[].origin": "position",
        "nodes[].yaw": "quaternion, rotation about source +Z becomes glTF +Y",
    },
}

_INCH_TO_METRE = 0.0254


def _source_position_to_gltf(x: float, y: float, z: float) -> tuple[float, float, float]:
    return (x * _INCH_TO_METRE, z * _INCH_TO_METRE, -y * _INCH_TO_METRE)


def _yaw_to_gltf_quaternion(yaw_degrees: float) -> tuple[float, float, float, float]:
    """A Source yaw (rotation about +Z) becomes a rotation about glTF +Y."""

    half = math.radians(yaw_degrees) / 2.0
    # Source quaternion (0, 0, sin(half), cos(half)) -> (x, z, -y, w)_source = (0, sin(half), 0, cos(half)).
    return (0.0, math.sin(half), 0.0, math.cos(half))


def _header_json(model: NavGraphModel) -> dict[str, Any]:
    header = model.header
    num_hulls = header.num_hulls.value
    bits_value = header.used_hull_bits.value
    try:
        version = int(header.version)
    except ValueError:
        version = header.version
    return {
        "version": version,
        "numHulls": num_hulls,
        "usedHullBits": {
            "value": bits_value,
            "bits": [bool((bits_value >> bit) & 1) for bit in range(num_hulls)],
        },
        "zoneCount": header.zone_count.value,
        "numNodes": header.num_nodes.value,
        "totalNumLinks": header.total_num_links.value,
    }


def _clamp_index(value: int, num_nodes: int) -> int:
    """A link endpoint the decoder already flagged `link-index-out-of-range` still has to become
    a valid glTF index -- the BIN chunk cannot carry a negative or out-of-bounds unsigned int, and
    the anomaly (not the geometry) is what records the departure. Clamped to the nearest real
    node rather than dropped, so the index accessor keeps exactly two entries per published link.
    """

    if num_nodes <= 0:
        return 0
    return max(0, min(num_nodes - 1, value))


def _node_json(node) -> dict[str, Any]:
    return {
        "index": node.index,
        # Only the source-space value is stated here: the glTF position is recoverable from it
        # through `coordinateTransform` and is already stated once by the core POSITION accessor
        # and the core node's own `translation` -- the contract's "never stated twice" rule.
        "origin": {"source": list(node.origin_source)},
        "yaw": node.yaw,
        "hullOffsets": list(node.hull_offsets),
        "tail": list(node.tail),
        "lead": list(node.lead),
        "wcId": node.wc_id,
        "sourceLine": node.source_line,
        "sourceOffset": node.source_offset,
    }


def _link_json(link) -> dict[str, Any]:
    return {
        "index": link.index,
        "src": link.src,
        "dst": link.dst,
        "fields": list(link.fields),
        "sourceLine": link.source_line,
        "sourceOffset": link.source_offset,
    }


def _stamp_json(model: NavGraphModel) -> dict[str, Any] | None:
    stamp = model.stamp
    if stamp is None:
        return None
    return {
        "raw": stamp.raw,
        "value": stamp.value,
        "byteLength": stamp.byte_length,
        "sha256": stamp.sha256,
    }


def _source_offsets_json(model: NavGraphModel) -> dict[str, Any]:
    """Where each offset-derived record that is not already `sourceOffset`-carrying itself
    (`header.*`, `zones`, `wcLookup`, `stamp`) sits in the `.ain`/`.loc` byte stream, so a ledger
    range and the record it pays for can always be tied back to the same place, per the rule that
    numeric records that came from a file offset keep that offset. `nodes[]`/`links[]` already
    carry their own `sourceOffset` field and are not
    repeated here.
    """

    header = model.header
    offsets = {
        "header.version": {"offset": header.version_field.offset, "length": header.version_field.length},
        "header.numHulls": {"offset": header.num_hulls.offset, "length": header.num_hulls.length},
        "header.usedHullBits": {
            "offset": header.used_hull_bits.offset,
            "length": header.used_hull_bits.length,
        },
        "header.zoneCount": {"offset": header.zone_count.offset, "length": header.zone_count.length},
        "header.numNodes": {"offset": header.num_nodes.offset, "length": header.num_nodes.length},
        "header.totalNumLinks": {
            "offset": header.total_num_links.offset,
            "length": header.total_num_links.length,
        },
        "wcLookup": {"offset": model.wc_lookup.offset, "length": model.wc_lookup.length},
    }
    # An empty zone line (`ZoneCount: 0`) or a `.loc` companion that never produced a parsed
    # stamp (absent, empty or malformed) has no real file offset to state -- `model.zones.offset`
    # and `model.stamp.offset` default to `0` in that case, which is indistinguishable from a
    # genuine offset at the start of the file, so the entry is omitted rather than fabricated.
    if model.zones.length:
        offsets["zones"] = {"offset": model.zones.offset, "length": model.zones.length}
    if model.stamp is not None and model.stamp.length:
        offsets["stamp"] = {"offset": model.stamp.offset, "length": model.stamp.length}
    return offsets


def build_document(model: NavGraphModel) -> tuple[dict[str, Any], bytes]:
    source_paths = [model.ain_path] + ([model.loc_path] if model.loc_path else [])
    identity = identity_block(model.asset_id, source_paths)

    coverage = coverage_block(
        mapped=model.mapped,
        typed_unidentified=model.typed_unidentified,
        byte_ledger=model.byte_ledger,
        unresolved=[],
        unsupported=[],
    )

    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity,
        source_resolution=source_resolution(model.members),
        dependencies=model.dependencies,
        coverage=coverage,
        coordinateTransform=COORDINATE_TRANSFORM,
        header=_header_json(model),
        sourceOffsets=_source_offsets_json(model),
        zones=list(model.zones.values),
        nodes=[_node_json(node) for node in model.nodes],
        links=[_link_json(link) for link in model.links],
        wcLookup=list(model.wc_lookup.values),
        stamp=_stamp_json(model),
        anomalies=list(model.anomalies),
        omissions=list(model.omissions),
    )

    document: dict[str, Any] = {
        "asset": asset_block("Nav-graph"),
        "extensionsUsed": [NAV_GRAPH_EXTENSION],
        "extensionsRequired": [NAV_GRAPH_EXTENSION],
        "extensions": {NAV_GRAPH_EXTENSION: root},
    }

    num_nodes = len(model.nodes)
    if num_nodes == 0:
        return document, b""

    positions = bytearray()
    for node in model.nodes:
        positions.extend(struct.pack("<3f", *_source_position_to_gltf(*node.origin_source)))
    position_bytes = bytes(positions)

    indices = bytearray()
    for link in model.links:
        indices.extend(
            struct.pack(
                "<2I", _clamp_index(link.src, num_nodes), _clamp_index(link.dst, num_nodes)
            )
        )
    index_bytes = bytes(indices)

    scene_nodes = []
    for node in model.nodes:
        gltf_position = list(_source_position_to_gltf(*node.origin_source))
        scene_nodes.append(
            {"translation": gltf_position, "rotation": list(_yaw_to_gltf_quaternion(node.yaw))}
        )
    scene_nodes.append({"mesh": 0})

    document["scene"] = 0
    document["scenes"] = [{"nodes": list(range(len(scene_nodes)))}]
    document["nodes"] = scene_nodes
    document["meshes"] = [
        {"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "mode": 1}]}
    ]

    accessors: list[dict[str, Any]] = [
        {
            "bufferView": 0,
            "byteOffset": 0,
            "componentType": 5126,
            "type": "VEC3",
            "count": num_nodes,
        }
    ]
    if position_bytes:
        gltf_positions = [_source_position_to_gltf(*node.origin_source) for node in model.nodes]
        accessors[0]["min"] = [min(p[axis] for p in gltf_positions) for axis in range(3)]
        accessors[0]["max"] = [max(p[axis] for p in gltf_positions) for axis in range(3)]

    binary = bytearray(position_bytes)
    views = [{"buffer": 0, "byteOffset": 0, "byteLength": len(position_bytes)}]

    if index_bytes:
        views.append(
            {"buffer": 0, "byteOffset": len(binary), "byteLength": len(index_bytes)}
        )
        accessors.append(
            {
                "bufferView": 1,
                "byteOffset": 0,
                "componentType": 5125,
                "type": "SCALAR",
                "count": len(model.links) * 2,
            }
        )
        binary.extend(index_bytes)
    else:
        # An empty index accessor still needs a view a validator can weigh, so it is not omitted;
        # `mode: 1` (LINES) with zero indices draws nothing, which is exactly what zero links is.
        views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": 0})
        accessors.append(
            {"bufferView": 1, "byteOffset": 0, "componentType": 5125, "type": "SCALAR", "count": 0}
        )

    document["accessors"] = accessors
    document["bufferViews"] = views
    document["buffers"] = [{"byteLength": len(binary)}]

    return document, bytes(binary)


def source_keys(index: dict) -> list[str]:
    """Every map stem the install carries a nav-graph `.ain` for, UP-first, sorted."""

    from elysium_pipeline.formats.nav_graph_glb.model import normalize_key
    from elysium_pipeline.formats.nav_graph_glb.source import FAMILY_DIR

    prefix = FAMILY_DIR + "/"
    keys = set()
    for entry_key in index:
        if entry_key.startswith(prefix) and entry_key.endswith(".ain"):
            keys.add(normalize_key(entry_key))
    return sorted(keys)


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes=None,
) -> Path:
    closure = load_source_closure(index, key, read_bytes=read_bytes)
    model = decode_nav_graph(closure)
    document, binary = build_document(model)

    from elysium_pipeline.validation import nav_graph_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())

    destination = Path(output_root) / Path(*output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination
