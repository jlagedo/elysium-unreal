"""Independent structural validator for Map GLB units.

Nothing here imports the writer. The kind-independent half runs through
`formats.unit_contract.validate`; the kind-specific half re-reads the BSP with its own `struct`
calls when the export path hands it the selected member, and otherwise holds the published unit
to its own internal consistency -- every face's triangles inside the primitive they claim, every
displacement grid the size its power implies, every hull span inside the physics accessors, every
PAKFILE entry inside the lump it was cut from and every material name carrying a dependency row.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
import struct
from typing import Any, Mapping, Sequence

from elysium_pipeline.formats.map_glb.model import MAP_EXTENSION, SCHEMA_VERSION
from elysium_pipeline.formats.unit_contract import (
    MATERIAL_REFERENCE,
    MODEL_REFERENCE,
    TEXTURE_REFERENCE,
    UnitValidationError,
    completeness,
    generator,
    read_glb,
    reject_opaque_source,
    validate_accessors,
    validate_container,
    validate_extension_root,
    validate_ledgers,
)
from elysium_pipeline.formats.unit_contract import warnings_for as contract_warnings

ASSET_PREFIX = "vtmb:map:"
SCENE_NAMES = ("world", "brushModels", "displacements", "placements")
LUMP_DIRECTORY_OFFSET = 8
LUMP_COUNT = 64
FACE_BYTES = 104
TEXDATA_BYTES = 32
PLANE_BYTES = 20
MODEL_BYTES = 48
STATIC_PROP_BYTES = 56


class MapGlbValidationError(ValueError):
    """A published map unit contradicts its source or itself."""


def _require(condition: Any, message: str) -> None:
    if not condition:
        raise MapGlbValidationError(message)


def _accessor_count(document: Mapping[str, Any], index: Any) -> int:
    accessors = document.get("accessors") or []
    _require(isinstance(index, int) and 0 <= index < len(accessors),
             f"accessor {index!r} is outside the unit")
    return int(accessors[index]["count"])


def _check_scenes(document: Mapping[str, Any]) -> None:
    scenes = document.get("scenes") or []
    _require(
        [scene.get("name") for scene in scenes] == list(SCENE_NAMES),
        f"a map declares the scenes {list(SCENE_NAMES)}, not "
        f"{[scene.get('name') for scene in scenes]}",
    )
    nodes = document.get("nodes") or []
    seen: set[int] = set()
    for scene in scenes:
        for node in scene.get("nodes") or []:
            _require(isinstance(node, int) and 0 <= node < len(nodes),
                     f"scene {scene.get('name')!r} names node {node!r}")
            _require(node not in seen, f"node {node} is in two scenes")
            seen.add(node)
    meshes = document.get("meshes") or []
    materials = document.get("materials") or []
    for index, node in enumerate(nodes):
        if "mesh" in node:
            _require(0 <= int(node["mesh"]) < len(meshes), f"node {index} names no mesh")
        for name, extension in (node.get("extensions") or {}).items():
            _require(
                name in (MODEL_REFERENCE, TEXTURE_REFERENCE),
                f"node {index} binds {name}, which a placement does not use",
            )
            _require(str(extension.get("asset", "")).startswith("vtmb:"),
                     f"node {index} binds no stable identity")
    for index, material in enumerate(materials):
        reference = (material.get("extensions") or {}).get(MATERIAL_REFERENCE)
        _require(isinstance(reference, Mapping) and str(reference.get("asset", "")).startswith(
            "vtmb:material:"
        ), f"material {index} names no material unit")
    for index, mesh in enumerate(meshes):
        for slot, primitive in enumerate(mesh.get("primitives") or []):
            attributes = primitive.get("attributes") or {}
            _require("POSITION" in attributes, f"mesh {index} primitive {slot} has no POSITION")
            count = _accessor_count(document, attributes["POSITION"])
            for name, accessor in attributes.items():
                _require(
                    _accessor_count(document, accessor) == count,
                    f"mesh {index} primitive {slot}: {name} is not parallel to POSITION",
                )
            if "material" in primitive:
                _require(0 <= int(primitive["material"]) < len(materials),
                         f"mesh {index} primitive {slot} names no material")


def _check_partition(root: Mapping[str, Any], ledger: Mapping[str, Any]) -> None:
    regions = root.get("partition")
    _require(isinstance(regions, list) and regions, "the unit publishes no partition")
    cursor = 0
    units: dict[str, int] = {}
    for index, region in enumerate(regions):
        _require(isinstance(region, Mapping), f"partition row {index} is not a record")
        offset, length = int(region.get("offset", -1)), int(region.get("length", -1))
        _require(offset == cursor, f"partition row {index} starts at {offset}, not {cursor}")
        _require(length > 0, f"partition row {index} claims {length} bytes")
        units[str(region.get("unit"))] = units.get(str(region.get("unit")), 0) + length
        cursor += length
    _require(cursor == int(ledger["byteLength"]),
             f"the partition accounts {cursor} of {ledger['byteLength']} member bytes")
    header = root.get("header") or {}
    lumps = header.get("lumps") or []
    _require(len(lumps) == LUMP_COUNT, f"the directory has {len(lumps)} rows, not {LUMP_COUNT}")
    claimed = {
        (int(region["offset"]), int(region["length"]))
        for region in regions
        if region.get("lump") is not None
    }
    for row in lumps:
        if int(row["length"]) <= 0:
            continue
        covered = sum(
            length
            for offset, length in claimed
            if offset >= int(row["offset"]) and offset + length <= int(row["offset"]) + int(row["length"])
        )
        _require(
            covered == int(row["length"]),
            f"lump {row['lump']} is {row['length']} bytes and the partition claims {covered}",
        )
    for name in ("map-entities", "map-lighting", "map-visibility"):
        _require(name in units, f"the partition hands no span to {name}")


def _check_ledger_owners(root: Mapping[str, Any], ledger: Mapping[str, Any]) -> None:
    """Every ledger range lies inside one partition region, under an owner that region agrees to.

    The ledger is the partition restated, so a range that spans two regions -- two lumps, or a
    root lump and a sub-unit's span -- would name a record that did not pay for all of it.
    """

    regions = list(root.get("partition") or [])
    cursor = 0
    for index, entry in enumerate(ledger.get("ranges") or []):
        offset, length = int(entry["offset"]), int(entry["length"])
        owner = str(entry["owner"])
        while cursor < len(regions) and (
            int(regions[cursor]["offset"]) + int(regions[cursor]["length"]) <= offset
        ):
            cursor += 1
        _require(cursor < len(regions), f"byte range {index} lies past the partition")
        region = regions[cursor]
        start, size = int(region["offset"]), int(region["length"])
        _require(
            start <= offset and offset + length <= start + size,
            f"byte range {index} ({owner} {offset}+{length}) leaves the partition region "
            f"{region.get('owner')} {start}+{size}",
        )
        unit = str(region.get("unit"))
        if owner.startswith("subUnits["):
            _require(
                owner.startswith(f"subUnits[{unit}]"),
                f"byte range {index} names {owner} over bytes {unit} owns",
            )
        else:
            _require(unit == "map",
                     f"byte range {index} names {owner} over bytes {unit} owns")


SCALAR_TABLES = {
    "surfEdges": ("surfEdges",),
    "normalIndices": ("normalIndices",),
    "displacementTriangleTags": ("displacementTriangleTags",),
    "edges": ("edges",),
    "bsp.leafFaces": ("bsp", "leafFaces"),
    "bsp.leafBrushes": ("bsp", "leafBrushes"),
    "primitives.indices": ("primitives", "indices"),
    "water.leafMinDist": ("water", "leafMinDist"),
}


def _at(root: Mapping[str, Any], path: tuple[str, ...]) -> Any:
    node: Any = root
    for step in path:
        _require(isinstance(node, Mapping) and step in node,
                 f"the unit publishes nothing at {'.'.join(path)}")
        node = node[step]
    return node


def _check_tables(root: Mapping[str, Any], ledger: Mapping[str, Any]) -> None:
    """A ledger owner that names a record table names one the unit actually publishes.

    Every scalar lump the root claims `mapped` reaches the product as a published table, so the
    owner path, the table's `sourceOffset`/`stride`/`count` and the range that paid for it are
    three statements of one decode and have to agree.
    """

    ranges = ledger.get("ranges") or []
    for owner, path in SCALAR_TABLES.items():
        table = _at(root, path)
        _require(isinstance(table, Mapping), f"{owner} is not a record table")
        count, stride = int(table["count"]), int(table["stride"])
        _require(len(table.get("values") or []) == count,
                 f"{owner} states {count} records and holds {len(table.get('values') or [])}")
        rows = [entry for entry in ranges if str(entry["owner"]) == owner]
        if count == 0:
            _require(not rows, f"{owner} claims bytes for an empty table")
            continue
        _require(len(rows) == 1, f"{owner} is paid for by {len(rows)} ledger ranges, not one")
        _require(
            (int(rows[0]["offset"]), int(rows[0]["length"]), str(rows[0]["state"]))
            == (int(table["sourceOffset"]), count * stride, "mapped"),
            f"{owner} disagrees with the ledger range that pays for it",
        )
    owners = {str(entry["owner"]) for entry in ranges}
    for owner in owners:
        head = owner.split(".unreferenced")[0]
        _require(
            not (head in SCALAR_TABLES and owner != head),
            f"ledger owner {owner} names no table of this unit",
        )


def _check_vertex_runs(root: Mapping[str, Any], ledger: Mapping[str, Any], data: bytes) -> None:
    """The vertex and vertex-normal lumps, graded run by run against the indices that name them.

    A record no edge and no normal index addresses reaches no accessor, so its bytes are
    `omitted-proven`, not `mapped`. The runs are re-derived here from the lumps themselves.
    """

    directory = [
        struct.unpack_from("<iii4s", data, LUMP_DIRECTORY_OFFSET + index * 16)[:2]
        for index in range(LUMP_COUNT)
    ]
    ranges = ledger.get("ranges") or []
    for lump, index_lump, owner, code, width in (
        (3, 12, "meshes.position", "H", 2),
        (30, 31, "meshes.normal", "H", 2),
    ):
        offset, length = directory[lump]
        count = length // 12
        index_offset, index_length = directory[index_lump]
        raw = struct.unpack_from(
            f"<{index_length // width}{code}", data, index_offset
        ) if index_length else ()
        referenced = set(raw)
        expected: list[tuple[int, int, str]] = []
        start = 0
        while start < count:
            present = start in referenced
            stop = start + 1
            while stop < count and (stop in referenced) == present:
                stop += 1
            expected.append((
                offset + start * 12,
                (stop - start) * 12,
                "mapped" if present else "omitted-proven",
            ))
            start = stop
        published = [
            (int(entry["offset"]), int(entry["length"]), str(entry["state"]))
            for entry in ranges
            if str(entry["owner"]) in (owner, owner + ".unreferenced")
        ]
        _require(published == expected,
                 f"lump {lump} is not graded run by run against the indices that name it")
        for entry in ranges:
            if str(entry["owner"]) == owner + ".unreferenced":
                _require(str(entry["state"]) == "omitted-proven",
                         f"an unreferenced run of lump {lump} is not omitted-proven")
    proven = (root.get("coverage") or {}).get("omittedProven") or []
    for row in proven:
        if not str(row.get("role", "")).startswith("unreferenced-"):
            continue
        total = sum(int(entry["byteLength"]) for entry in row.get("ranges") or [])
        _require(total == int(row["count"]) * 12,
                 f"{row['role']} counts {row['count']} records over {total} bytes")


def _check_faces(root: Mapping[str, Any], document: Mapping[str, Any]) -> None:
    meshes = document.get("meshes") or []
    faces = root.get("faces") or []
    models = root.get("models") or []
    nodes = document.get("nodes") or []
    mesh_of_model: dict[int, int] = {}
    for model in models:
        node = model.get("node")
        if node is None:
            continue
        _require(0 <= int(node) < len(nodes), f"model {model['index']} names node {node}")
        mesh_of_model[int(model["index"])] = int(nodes[int(node)]["mesh"])
    owner: dict[int, int] = {}
    for model in models:
        for face in range(int(model["firstFace"]), int(model["firstFace"]) + int(model["numFaces"])):
            owner.setdefault(face, int(model["index"]))
    for face in faces:
        if face.get("primitive") is None:
            _require(
                face.get("firstIndex") is None and face.get("indexCount") is None,
                f"face {face['index']} claims a triangle span in no primitive",
            )
            continue
        model = owner.get(int(face["index"]))
        _require(model is not None, f"face {face['index']} belongs to no brush model")
        mesh = mesh_of_model.get(model)
        _require(mesh is not None, f"model {model} publishes no mesh for its faces")
        primitives = meshes[mesh].get("primitives") or []
        slot = int(face["primitive"])
        _require(0 <= slot < len(primitives),
                 f"face {face['index']} names primitive {slot} of mesh {mesh}")
        primitive = primitives[slot]
        indices = _accessor_count(document, primitive["indices"])
        positions = _accessor_count(document, primitive["attributes"]["POSITION"])
        _require(
            int(face["firstIndex"]) + int(face["indexCount"]) <= indices,
            f"face {face['index']} triangles run past its primitive's index accessor",
        )
        _require(
            int(face["firstVertex"]) + int(face["vertexCount"]) <= positions,
            f"face {face['index']} vertices run past its primitive's POSITION accessor",
        )
        _require(
            int(face["indexCount"]) == (int(face["vertexCount"]) - 2) * 3,
            f"face {face['index']} is not a fan over its own winding",
        )


def _check_displacements(root: Mapping[str, Any], document: Mapping[str, Any]) -> None:
    meshes = document.get("meshes") or []
    for row in root.get("displacements") or []:
        side = (1 << int(row["power"])) + 1
        _require(int(row["vertexCount"]) == side * side,
                 f"displacement {row['index']} declares {row['vertexCount']} vertices")
        _require(int(row["triangleCount"]) == 2 * (side - 1) * (side - 1),
                 f"displacement {row['index']} declares {row['triangleCount']} triangles")
        if row.get("mesh") is None:
            continue
        mesh = meshes[int(row["mesh"])]
        primitive = (mesh.get("primitives") or [])[0]
        _require(
            _accessor_count(document, primitive["attributes"]["POSITION"]) == side * side,
            f"displacement {row['index']} grid disagrees with its POSITION accessor",
        )
        _require(
            _accessor_count(document, primitive["indices"]) == int(row["triangleCount"]) * 3,
            f"displacement {row['index']} index count disagrees with its triangles",
        )


def _check_physics(root: Mapping[str, Any], document: Mapping[str, Any]) -> None:
    physics = root.get("physics") or {}
    positions = physics.get("positionAccessor")
    indices = physics.get("indexAccessor")
    vertex_total = _accessor_count(document, positions) if positions is not None else 0
    index_total = _accessor_count(document, indices) if indices is not None else 0
    for model in physics.get("models") or []:
        for solid in model.get("solids") or []:
            if solid.get("kind") == "havok-mopp":
                _require("moppCode" in solid, "a MOPP solid publishes no payload span")
                continue
            for ledge in solid.get("ledges") or []:
                _require(
                    int(ledge["firstVertex"]) + int(ledge["vertexCount"]) <= vertex_total,
                    f"a hull of physics model {model['index']} runs past the position accessor",
                )
                _require(
                    int(ledge["firstIndex"]) + int(ledge["indexCount"]) <= index_total,
                    f"a hull of physics model {model['index']} runs past the index accessor",
                )
                _require(
                    int(ledge["indexCount"]) == int(ledge["triangleCount"]) * 3,
                    "a hull's index count is not three per triangle",
                )
    mopp = physics.get("moppCode")
    if mopp:
        total = _accessor_count(document, mopp["accessor"])
        _require(total == int(mopp["byteLength"]),
                 "the MOPP accessor disagrees with the payload it carries")


def _check_dependencies(root: Mapping[str, Any]) -> None:
    rows = root.get("dependencies") or []
    by_asset = {str(row["asset"]) for row in rows}
    roles = {str(row["role"]) for row in rows}
    for name in ("map-entities", "map-lighting", "map-visibility"):
        _require(name in roles, f"the unit declares no {name} dependency")
    for texture in root.get("textures") or []:
        _require(str(texture["asset"]) in by_asset,
                 f"material {texture['asset']} carries no dependency row")
        if texture.get("baseAsset"):
            _require(str(texture["baseAsset"]) in by_asset,
                     f"base material {texture['baseAsset']} carries no dependency row")
    for cubemap in root.get("cubemaps") or []:
        _require(str(cubemap["asset"]) in by_asset,
                 f"cubemap texture {cubemap['asset']} carries no dependency row")
    proven = {
        str(row.get("asset"))
        for row in ((root.get("coverage") or {}).get("omittedProven") or [])
        if row.get("asset")
    }
    placements = [
        ("static prop", prop)
        for prop in (root.get("staticProps") or {}).get("props") or []
    ] + [
        ("detail prop", record)
        for record in (root.get("detailProps") or {}).get("records") or []
    ]
    for label, placement in placements:
        asset = placement.get("asset")
        if asset is None:
            continue
        if str(asset).startswith("vtmb:missing-"):
            _require(str(asset) not in by_asset,
                     f"{asset} is a sentinel and may not carry a dependency row")
            _require(str(asset) in proven,
                     f"{asset} is a sentinel with no omitted-proven row")
        else:
            _require(str(asset) in by_asset,
                     f"{label} model {asset} carries no dependency row")
    for entry in (root.get("pakfile") or {}).get("entries") or []:
        if entry.get("unit") is None:
            continue
        _require(str(entry["unit"]) in by_asset,
                 f"PAKFILE member {entry['name']} routes to a unit with no dependency row")


def _check_pakfile(root: Mapping[str, Any], data: bytes | None) -> None:
    pak = root.get("pakfile") or {}
    entries = pak.get("entries") or []
    _require(int(pak.get("entryCount", -1)) == len(entries),
             "the PAKFILE entry count disagrees with its table")
    if data is None:
        return
    for entry in entries:
        local = entry["localHeader"]
        offset = int(local["sourceOffset"])
        _require(
            struct.unpack_from("<I", data, offset)[0] == 0x04034B50,
            f"PAKFILE member {entry['name']} names no local header at {offset}",
        )
        central = entry["centralHeader"]
        _require(
            struct.unpack_from("<I", data, int(central["sourceOffset"]))[0] == 0x02014B50,
            f"PAKFILE member {entry['name']} names no central record",
        )
        span = entry["data"]
        payload = data[int(span["sourceOffset"]):int(span["sourceOffset"]) + int(span["byteLength"])]
        _require(
            hashlib.sha256(payload).hexdigest() == entry["sha256"],
            f"PAKFILE member {entry['name']} digest disagrees with the lump",
        )


def _placement(origin, angles) -> tuple[list[float], list[float]]:
    """The glTF translation and rotation of one Source placement, derived here from scratch.

    The maths is restated rather than imported so that a decoder that transformed a placement
    wrongly cannot agree with itself: this is `AngleQuaternion` followed by the unit contract's
    axis map, and the metre scale on the position.
    """

    import math

    scale = 0.0254
    translation = [origin[0] * scale, origin[2] * scale, -origin[1] * scale]
    pitch, yaw, roll = (math.radians(value) * 0.5 for value in angles)
    sin_y, cos_y = math.sin(yaw), math.cos(yaw)
    sin_p, cos_p = math.sin(pitch), math.cos(pitch)
    sin_r, cos_r = math.sin(roll), math.cos(roll)
    x = sin_r * cos_p * cos_y - cos_r * sin_p * sin_y
    y = cos_r * sin_p * cos_y + sin_r * cos_p * sin_y
    z = cos_r * cos_p * sin_y - sin_r * sin_p * cos_y
    w = cos_r * cos_p * cos_y + sin_r * sin_p * sin_y
    rotation = [x, z, -y, w]
    length = math.sqrt(sum(value * value for value in rotation)) or 1.0
    return translation, [value / length for value in rotation]


def _reread(root: Mapping[str, Any], data: bytes) -> None:
    """Re-read the BSP with this module's own struct calls and compare what the unit states."""

    ident, version = struct.unpack_from("<4si", data, 0)
    header = root.get("header") or {}
    _require(ident.decode("ascii", "replace") == header.get("ident"), "ident disagrees")
    _require(version == int(header.get("version", -1)), "BSP version disagrees")
    revision = struct.unpack_from("<i", data, LUMP_DIRECTORY_OFFSET + LUMP_COUNT * 16)[0]
    _require(revision == int(header.get("mapRevision", -1)), "mapRevision disagrees")
    directory = []
    for index in range(LUMP_COUNT):
        offset, length, lump_version, four_cc = struct.unpack_from(
            "<iii4s", data, LUMP_DIRECTORY_OFFSET + index * 16
        )
        directory.append((offset, length, lump_version, four_cc))
        row = (header.get("lumps") or [])[index]
        _require(
            (int(row["offset"]), int(row["length"]), int(row["version"])) == (offset, length, lump_version),
            f"directory row {index} disagrees with the file",
        )
    faces = root.get("faces") or []
    _require(len(faces) == directory[7][1] // FACE_BYTES,
             f"the unit states {len(faces)} faces; lump 7 holds {directory[7][1] // FACE_BYTES}")
    _require(len(root.get("originalFaces") or []) == directory[27][1] // FACE_BYTES,
             "the originalFaces count disagrees with lump 27")
    _require(len(root.get("planes") or []) == directory[1][1] // PLANE_BYTES,
             "the planes count disagrees with lump 1")
    _require(len(root.get("texinfos") or []) == directory[6][1] // 72,
             "the texinfos count disagrees with lump 6")
    _require(len(root.get("textures") or []) == directory[2][1] // TEXDATA_BYTES,
             "the textures count disagrees with lump 2")
    _require(len(root.get("models") or []) == directory[14][1] // MODEL_BYTES,
             "the models count disagrees with lump 14")
    for face in faces:
        offset = int(face["sourceOffset"])
        first_edge, num_edges, tex_info, disp_info = struct.unpack_from("<i3h", data, offset + 36)
        _require(
            (first_edge, num_edges, tex_info, disp_info)
            == (int(face["firstEdge"]), int(face["numEdges"]), int(face["texInfo"]), int(face["dispInfo"])),
            f"face {face['index']} disagrees with lump 7",
        )
    blob_offset, blob_length = directory[43][0], directory[43][1]
    blob = data[blob_offset:blob_offset + blob_length]
    table_offset = directory[44][0]
    # The name is published once, in `textureStrings.names`; a texdata row names it by index.
    names = (root.get("textureStrings") or {}).get("names") or []
    _require(len(names) == directory[44][1] // 4,
             "textureStrings.names disagrees with the string table's row count")
    for name_id, published in enumerate(names):
        string_offset = struct.unpack_from("<i", data, table_offset + name_id * 4)[0]
        stop = blob.find(bytes([0]), string_offset)
        _require(
            blob[string_offset:stop].decode("latin-1") == published,
            f"string {name_id} disagrees with the string blob",
        )
    for texture in root.get("textures") or []:
        _require(
            "name" not in texture,
            f"texdata {texture['index']} restates a string textureStrings.names holds",
        )
        _require(0 <= int(texture["nameStringTableID"]) < len(names),
                 f"texdata {texture['index']} names a string outside the table")
    props = (root.get("staticProps") or {}).get("props") or []
    for prop in props:
        offset = int(prop["sourceOffset"])
        prop_type, first_leaf, leaf_count, solid, flags = struct.unpack_from(
            "<3H2B", data, offset + 24
        )
        _require(
            (prop_type, first_leaf, leaf_count, solid, flags)
            == (int(prop["propType"]), int(prop["firstLeaf"]), int(prop["leafCount"]),
                int(prop["solid"]), int(prop["flags"])),
            f"static prop {prop['index']} disagrees with the sprp payload",
        )


def _check_placements(root: Mapping[str, Any], document: Mapping[str, Any], data: bytes) -> None:
    """Every static-prop node against the record it was placed from."""

    nodes = document.get("nodes") or []
    for prop in (root.get("staticProps") or {}).get("props") or []:
        if prop.get("node") is None:
            continue
        offset = int(prop["sourceOffset"])
        origin = struct.unpack_from("<3f", data, offset)
        angles = struct.unpack_from("<3f", data, offset + 12)
        translation, rotation = _placement(origin, angles)
        node = nodes[int(prop["node"])]
        for expected, published in zip(translation, node["translation"]):
            _require(abs(expected - published) <= 1e-6 * max(1.0, abs(expected)),
                     f"static prop {prop['index']} node translation disagrees with its record")
        for expected, published in zip(rotation, node["rotation"]):
            _require(abs(expected - published) <= 1e-6,
                     f"static prop {prop['index']} node rotation disagrees with its record")


def validate_document(
    document: Mapping[str, Any],
    binary: bytes,
    *,
    source_members: Sequence[Any] | None = None,
) -> dict[str, Any]:
    """Every check a published map unit answers to, in one pass."""

    try:
        root = validate_extension_root(
            document, MAP_EXTENSION, asset_prefix=ASSET_PREFIX, schema_version=SCHEMA_VERSION
        )
        _require(document.get("asset", {}).get("generator") == generator("Map"),
                 "asset.generator does not name the Map GLB Exporter")
        used = document.get("extensionsUsed") or []
        _require(MAP_EXTENSION in used, f"{MAP_EXTENSION} is not declared in extensionsUsed")
        bound = {
            name
            for group in ("materials", "nodes", "meshes")
            for entry in (document.get(group) or [])
            for name in (entry.get("extensions") or {})
        }
        for name in (MATERIAL_REFERENCE, MODEL_REFERENCE, TEXTURE_REFERENCE):
            _require(
                (name in used) == (name in bound),
                f"{name} is declared in extensionsUsed but bound by no object"
                if name in used
                else f"{name} is bound but not declared in extensionsUsed",
            )
        validate_container(document, binary)
        validate_accessors(document, binary)
        validate_ledgers(root, source_members)
        reject_opaque_source(document, binary, source_members)
    except UnitValidationError as error:
        raise MapGlbValidationError(str(error)) from error
    ledgers = (root.get("coverage") or {}).get("byteLedger") or []
    _require(len(ledgers) == 1, f"a map root publishes one ledger, not {len(ledgers)}")
    counts = completeness(root)
    _require(not counts["unresolved"], "the unit publishes unresolved rows")
    _require(not counts["unsupported"], "the unit publishes unsupported rows")
    _check_scenes(document)
    _check_partition(root, ledgers[0])
    _check_ledger_owners(root, ledgers[0])
    _check_tables(root, ledgers[0])
    _check_faces(root, document)
    _check_displacements(root, document)
    _check_physics(root, document)
    _check_dependencies(root)
    data = None
    for member in source_members or ():
        if getattr(member, "role", None) == "bsp":
            data = member.data
    _check_pakfile(root, data)
    if data is not None:
        _reread(root, data)
        _check_placements(root, document, data)
        _check_vertex_runs(root, ledgers[0], data)
    identity = root["identity"]
    return {
        "asset": str(identity["asset"]),
        "map": str(identity.get("map", "")),
        "sourcePath": str(identity["sourcePath"]),
        "sourceBytes": int(ledgers[0]["byteLength"]),
        "accountedBytes": int(ledgers[0]["accountedBytes"]),
        "byteCoveragePercent": float(ledgers[0]["coveragePercent"]),
        "faces": len(root.get("faces") or []),
        "brushModels": len(root.get("models") or []),
        "displacements": len(root.get("displacements") or []),
        "staticProps": len((root.get("staticProps") or {}).get("props") or []),
        "detailProps": len((root.get("detailProps") or {}).get("records") or []),
        "pakfileMembers": len((root.get("pakfile") or {}).get("entries") or []),
        "dependencies": len(root.get("dependencies") or []),
        "subUnits": [row["asset"] for row in root.get("subUnits") or []],
        "unresolved": counts["unresolved"],
        "unsupported": counts["unsupported"],
        "typedUnidentified": counts["typedUnidentified"],
        "warnings": contract_warnings(root),
    }


def validate(path: Path) -> dict[str, Any]:
    """Read a published unit with no install present and hold it to the contract."""

    document, binary = read_glb(Path(path))
    return validate_document(document, binary)


def warnings_for(summary: Mapping[str, Any]) -> list[str]:
    """What the published unit could not resolve, phrased for the operator."""

    return list(summary.get("warnings") or [])


__all__ = [
    "MapGlbValidationError",
    "read_glb",
    "validate",
    "validate_document",
    "warnings_for",
]
