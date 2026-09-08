"""Independent structural validator for Nav-graph GLB products.

Re-decodes the source `.ain`/`.loc` pair through `formats.nav_graph_glb.decode_nav_graph` when
export-time source members are supplied, rather than importing the exporter's `build_document` to
compare against -- a writer bug that also lives in `build_document` would otherwise pass silently.
"""

from __future__ import annotations

import math
import struct
from pathlib import Path
from typing import Any, Sequence

from elysium_pipeline.formats.nav_graph_glb.model import NAV_GRAPH_EXTENSION, SCHEMA_VERSION
from elysium_pipeline.formats.unit_contract import (
    SourceMember,
    UnitValidationError,
    completeness,
    read_glb,
    validate_accessors,
    validate_capsules,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
    warnings_for as _contract_warnings_for,
)

_INCH_TO_METRE = 0.0254
_TOLERANCE = 1e-4


class NavGraphGlbValidationError(UnitValidationError):
    pass


def _source_position_to_gltf(x: float, y: float, z: float) -> tuple[float, float, float]:
    return (x * _INCH_TO_METRE, z * _INCH_TO_METRE, -y * _INCH_TO_METRE)


def _yaw_to_gltf_quaternion(yaw_degrees: float) -> tuple[float, float, float, float]:
    half = math.radians(yaw_degrees) / 2.0
    return (0.0, math.sin(half), 0.0, math.cos(half))


def _close(a: Sequence[float], b: Sequence[float], tolerance: float = _TOLERANCE) -> bool:
    return len(a) == len(b) and all(abs(x - y) <= tolerance for x, y in zip(a, b))


def _clamp_index(value: int, num_nodes: int) -> int:
    """Mirrors `exporters.nav_graph_glb._clamp_index`: what an out-of-range link endpoint is
    clamped to in the BIN chunk, so the independent consistency check below compares against what
    the writer actually had to publish rather than the raw (possibly negative or overrunning)
    declared value.
    """

    if num_nodes <= 0:
        return 0
    return max(0, min(num_nodes - 1, value))


def _check_core_geometry(document: dict[str, Any], binary: bytes, root: dict[str, Any]) -> None:
    header = root.get("header") or {}
    nodes_ext = root.get("nodes") or []
    links_ext = root.get("links") or []
    anomalies = root.get("anomalies") or []
    num_nodes = int(header.get("numNodes", -1))
    if num_nodes != len(nodes_ext):
        raise NavGraphGlbValidationError("header.numNodes disagrees with nodes[] length")
    # A declared `TotalNumLinks` that disagrees with the actual link region is the seam's
    # `link-count-mismatch` anomaly -- published, never fatal, because a unit that is
    # recoverable only in part publishes what the install holds and warns. `links[]` itself is
    # always internally consistent with what was decoded,
    # so only an *undeclared* disagreement is a writer bug worth failing on here.
    link_count_mismatch = any(row.get("role") == "link-count-mismatch" for row in anomalies)
    if not link_count_mismatch and int(header.get("totalNumLinks", -1)) != len(links_ext):
        raise NavGraphGlbValidationError("header.totalNumLinks disagrees with links[] length")

    if num_nodes == 0:
        validate_sceneless(document)
        if document.get("buffers") or binary:
            raise NavGraphGlbValidationError("an empty graph carries no BIN payload")
        if links_ext:
            # A zero-node graph is scene-less, so there is no mesh, no primitive and no index
            # accessor for a declared link to live in -- a link naming nodes that do not exist is
            # not a departure this seam can recover into a drawable product, unlike an in-range
            # count disagreement above.
            raise NavGraphGlbValidationError(
                "a zero-node graph declares links[] with no mesh to hold them"
            )
        return

    core_nodes = document.get("nodes") or []
    if len(core_nodes) != num_nodes + 1:
        raise NavGraphGlbValidationError(
            f"scene carries {len(core_nodes)} nodes, expected {num_nodes} graph nodes + 1 mesh node"
        )
    meshes = document.get("meshes") or []
    if len(meshes) != 1:
        raise NavGraphGlbValidationError("Nav-graph GLB carries one mesh")
    primitive = (meshes[0].get("primitives") or [{}])[0]
    if primitive.get("mode") != 1:
        raise NavGraphGlbValidationError("the link primitive is not mode 1 (LINES)")
    position_accessor_index = primitive.get("attributes", {}).get("POSITION")
    index_accessor_index = primitive.get("indices")
    accessors = document.get("accessors") or []
    views = document.get("bufferViews") or []
    if position_accessor_index is None or index_accessor_index is None:
        raise NavGraphGlbValidationError("the link primitive names no POSITION/indices accessor")

    position_accessor = accessors[position_accessor_index]
    if position_accessor.get("count") != num_nodes or position_accessor.get("type") != "VEC3":
        raise NavGraphGlbValidationError("POSITION accessor does not describe every graph node")
    position_view = views[position_accessor["bufferView"]]
    position_start = int(position_view.get("byteOffset", 0)) + int(position_accessor.get("byteOffset", 0))
    position_values = struct.unpack_from(f"<{num_nodes * 3}f", binary, position_start)

    for index, node in enumerate(nodes_ext):
        expected = _source_position_to_gltf(*node["origin"]["source"])
        buffer_position = position_values[index * 3 : index * 3 + 3]
        if not _close(expected, buffer_position):
            raise NavGraphGlbValidationError(f"POSITION accessor entry {index} disagrees with nodes[]")
        core_node = core_nodes[index]
        if not _close(core_node.get("translation", []), expected):
            raise NavGraphGlbValidationError(f"core node {index}.translation disagrees with nodes[]")
        expected_rotation = _yaw_to_gltf_quaternion(node["yaw"])
        if not _close(core_node.get("rotation", []), expected_rotation):
            raise NavGraphGlbValidationError(f"core node {index}.rotation disagrees with yaw")

    mesh_node = core_nodes[num_nodes]
    if mesh_node.get("mesh") != 0:
        raise NavGraphGlbValidationError("the trailing scene node does not hold the link mesh")

    index_accessor = accessors[index_accessor_index]
    link_count = len(links_ext)
    if index_accessor.get("count") != link_count * 2:
        raise NavGraphGlbValidationError("index accessor count disagrees with links[]")
    if link_count:
        index_view = views[index_accessor["bufferView"]]
        index_start = int(index_view.get("byteOffset", 0)) + int(index_accessor.get("byteOffset", 0))
        index_values = struct.unpack_from(f"<{link_count * 2}I", binary, index_start)
        for value in index_values:
            if not (0 <= value < num_nodes):
                raise NavGraphGlbValidationError(
                    "the link primitive's index accessor addresses a vertex outside the "
                    "POSITION accessor's range"
                )
        for index, link in enumerate(links_ext):
            expected_pair = (
                _clamp_index(link["src"], num_nodes),
                _clamp_index(link["dst"], num_nodes),
            )
            if tuple(index_values[index * 2 : index * 2 + 2]) != expected_pair:
                raise NavGraphGlbValidationError(f"index accessor pair {index} disagrees with links[]")


def _expected_source_offsets(model: Any) -> dict[str, dict[str, int]]:
    """Mirrors `exporters.nav_graph_glb._source_offsets_json`'s omission rule, but computed
    straight from the independently re-decoded model rather than imported from the writer, so a
    writer that fabricates or drops an offset is still caught here."""

    header = model.header
    offsets: dict[str, dict[str, int]] = {
        "header.version": {"offset": header.version_field.offset, "length": header.version_field.length},
        "header.numHulls": {"offset": header.num_hulls.offset, "length": header.num_hulls.length},
        "header.usedHullBits": {
            "offset": header.used_hull_bits.offset, "length": header.used_hull_bits.length,
        },
        "header.zoneCount": {"offset": header.zone_count.offset, "length": header.zone_count.length},
        "header.numNodes": {"offset": header.num_nodes.offset, "length": header.num_nodes.length},
        "header.totalNumLinks": {
            "offset": header.total_num_links.offset, "length": header.total_num_links.length,
        },
        "wcLookup": {"offset": model.wc_lookup.offset, "length": model.wc_lookup.length},
    }
    if model.zones.length:
        offsets["zones"] = {"offset": model.zones.offset, "length": model.zones.length}
    if model.stamp is not None and model.stamp.length:
        offsets["stamp"] = {"offset": model.stamp.offset, "length": model.stamp.length}
    return offsets


def _check_against_decode(root: dict[str, Any], source_members: Sequence[SourceMember]) -> None:
    """Re-decode the selected members independently and compare the structural facts."""

    from elysium_pipeline.formats.nav_graph_glb.decode import decode_nav_graph
    from elysium_pipeline.formats.nav_graph_glb.model import asset_id as nav_asset_id, normalize_key
    from elysium_pipeline.formats.nav_graph_glb.source import NavGraphSourceClosure

    ain = next((member for member in source_members if member.role == "ain"), None)
    if ain is None:
        raise NavGraphGlbValidationError("prepublication source members omit the .ain")
    loc = next((member for member in source_members if member.role == "loc"), None)

    # The key and identity are re-derived from the `.ain` member's own path, never read from the
    # document under test -- feeding the declared identity back in would make this check compare
    # the writer's own answer against itself. `map_resolved` is likewise not taken from the
    # document: this validator has no install index to re-resolve `maps/<key>.bsp` against, so the
    # dependency rows themselves are outside what this re-decode can independently prove and are
    # not compared below.
    key = normalize_key(ain.path)
    expected_asset = nav_asset_id(key)
    identity = root.get("identity") or {}
    if str(identity.get("asset")) != expected_asset:
        raise NavGraphGlbValidationError("declared identity disagrees with an independent re-decode")
    closure = NavGraphSourceClosure(
        key=key, asset_id=expected_asset, ain=ain, loc=loc, map_resolved=False,
    )
    model = decode_nav_graph(closure)

    header = root.get("header") or {}
    if header.get("numNodes") != len(model.nodes):
        raise NavGraphGlbValidationError("declared numNodes disagrees with an independent re-decode")
    link_count_mismatch = any(row.get("role") == "link-count-mismatch" for row in model.anomalies)
    if not link_count_mismatch and header.get("totalNumLinks") != len(model.links):
        raise NavGraphGlbValidationError("declared totalNumLinks disagrees with an independent re-decode")
    try:
        declared_version = int(header.get("version"))
    except (TypeError, ValueError):
        declared_version = header.get("version")
    try:
        decoded_version: Any = int(model.header.version)
    except ValueError:
        decoded_version = model.header.version
    if declared_version != decoded_version:
        raise NavGraphGlbValidationError("declared header.version disagrees with an independent re-decode")
    if header.get("numHulls") != model.header.num_hulls.value:
        raise NavGraphGlbValidationError("declared header.numHulls disagrees with an independent re-decode")
    declared_bits = header.get("usedHullBits") or {}
    if declared_bits.get("value") != model.header.used_hull_bits.value:
        raise NavGraphGlbValidationError(
            "declared header.usedHullBits disagrees with an independent re-decode"
        )
    if header.get("zoneCount") != model.header.zone_count.value:
        raise NavGraphGlbValidationError("declared header.zoneCount disagrees with an independent re-decode")
    if list(root.get("wcLookup") or []) != list(model.wc_lookup.values):
        raise NavGraphGlbValidationError("declared wcLookup disagrees with an independent re-decode")
    if list(root.get("zones") or []) != list(model.zones.values):
        raise NavGraphGlbValidationError("declared zones disagree with an independent re-decode")
    if (root.get("sourceOffsets") or {}) != _expected_source_offsets(model):
        raise NavGraphGlbValidationError("declared sourceOffsets disagree with an independent re-decode")
    declared_stamp = root.get("stamp")
    if model.stamp is None:
        if declared_stamp is not None:
            raise NavGraphGlbValidationError("declared stamp disagrees with an independent re-decode")
    else:
        if not declared_stamp or declared_stamp.get("value") != model.stamp.value:
            raise NavGraphGlbValidationError("declared stamp disagrees with an independent re-decode")
    for index, (declared, decoded) in enumerate(zip(root.get("nodes") or [], model.nodes)):
        if declared.get("wcId") != decoded.wc_id:
            raise NavGraphGlbValidationError(f"nodes[{index}].wcId disagrees with an independent re-decode")
        if list(declared.get("tail") or []) != list(decoded.tail):
            raise NavGraphGlbValidationError(f"nodes[{index}].tail disagrees with an independent re-decode")
        if list(declared.get("lead") or []) != list(decoded.lead):
            raise NavGraphGlbValidationError(f"nodes[{index}].lead disagrees with an independent re-decode")
        declared_origin = tuple((declared.get("origin") or {}).get("source") or ())
        if not _close(declared_origin, decoded.origin_source, tolerance=1e-9):
            raise NavGraphGlbValidationError(f"nodes[{index}].origin disagrees with an independent re-decode")
        if declared.get("yaw") != decoded.yaw:
            raise NavGraphGlbValidationError(f"nodes[{index}].yaw disagrees with an independent re-decode")
        if list(declared.get("hullOffsets") or []) != list(decoded.hull_offsets):
            raise NavGraphGlbValidationError(
                f"nodes[{index}].hullOffsets disagrees with an independent re-decode"
            )
        if declared.get("sourceLine") != decoded.source_line:
            raise NavGraphGlbValidationError(
                f"nodes[{index}].sourceLine disagrees with an independent re-decode"
            )
        if declared.get("sourceOffset") != decoded.source_offset:
            raise NavGraphGlbValidationError(
                f"nodes[{index}].sourceOffset disagrees with an independent re-decode"
            )
    for index, (declared, decoded) in enumerate(zip(root.get("links") or [], model.links)):
        if (declared.get("src"), declared.get("dst")) != (decoded.src, decoded.dst):
            raise NavGraphGlbValidationError(f"links[{index}] disagrees with an independent re-decode")
        if list(declared.get("fields") or []) != list(decoded.fields):
            raise NavGraphGlbValidationError(f"links[{index}].fields disagrees with an independent re-decode")
        if declared.get("sourceLine") != decoded.source_line:
            raise NavGraphGlbValidationError(
                f"links[{index}].sourceLine disagrees with an independent re-decode"
            )
        if declared.get("sourceOffset") != decoded.source_offset:
            raise NavGraphGlbValidationError(
                f"links[{index}].sourceOffset disagrees with an independent re-decode"
            )


def validate_document(
    document: dict[str, Any], binary: bytes, *, source_members: Sequence[SourceMember] | None = None
) -> dict[str, Any]:
    root = validate_extension_root(
        document,
        NAV_GRAPH_EXTENSION,
        asset_prefix="vtmb:nav-graph:",
        schema_version=SCHEMA_VERSION,
    )
    validate_container(document, binary)
    validate_accessors(document, binary)
    _check_core_geometry(document, binary, root)
    validate_ledgers(root, source_members)
    validate_capsules(document, binary, root, source_members)

    incomplete = completeness(root)
    if incomplete["unresolved"] or incomplete["unsupported"]:
        raise NavGraphGlbValidationError(f"nav-graph unit is incomplete: {incomplete}")

    if source_members is not None:
        _check_against_decode(root, source_members)

    return {
        "asset": root["identity"]["asset"],
        "numNodes": (root.get("header") or {}).get("numNodes"),
        "totalNumLinks": (root.get("header") or {}).get("totalNumLinks"),
        "typedUnidentified": incomplete["typedUnidentified"],
        "warnings": _contract_warnings_for(root),
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: dict[str, Any]) -> list[str]:
    """What a published unit could not resolve, phrased for the operator."""

    return list(summary.get("warnings") or [])
