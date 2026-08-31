"""Independent structural validator for Map-lighting GLB units.

Nothing here imports the writer. The kind-independent half runs through
`formats.unit_contract.validate`; the kind-specific half re-reads the BSP with its own `struct`
calls when the export path hands it the map's bytes, and otherwise holds the published unit to
its own internal consistency -- every face span inside the `samples` accessor and adjacent to its
neighbours, every world light record inside its span, every displacement run inside the accessor
it names, and every accessor's digest against the lump it claims to be.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
import re
import struct
from typing import Any, Mapping, Sequence

from elysium_pipeline.formats.map_lighting_glb.model import (
    BUMP_LIGHTMAP_SETS,
    DETAIL_PROP_LIGHT_BYTES,
    DISPINFO_BYTES,
    FACE_BYTES,
    LIGHT_TYPE_NAMES,
    LUXEL_BYTES,
    MAP_LIGHTING_EXTENSION,
    SCHEMA_VERSION,
    SURF_BUMPLIGHT,
    TEXINFO_BYTES,
    UNUSED_STYLE,
    WORLD_LIGHT_BYTES,
)
from elysium_pipeline.formats.unit_contract import (
    UnitValidationError,
    completeness,
    generator,
    read_glb,
    validate_accessors,
    validate_capsules,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)
from elysium_pipeline.formats.unit_contract import warnings_for as contract_warnings

ASSET_PREFIX = "vtmb:map-lighting:"

#: The 64-row lump directory, `int fileofs, filelen, version; char fourCC[4]`, from byte 8.
LUMP_DIRECTORY_OFFSET = 8
LUMP_ENTRY_SIZE = 16
LIGHTING_LUMP = 8
DISP_ALPHA_LUMP = 32
DISP_SAMPLE_LUMP = 34
FACE_LUMP = 7
TEXINFO_LUMP = 6
DISPINFO_LUMP = 26

#: The roles whose member is copied into an accessor byte for byte -- "copied verbatim into a BIN
#: payload whose every byte the extension describes", the contract's `mapped` case. `faces[]`,
#: `displacements[]` and the ledger are that description, and the accessor-digest check below is
#: what holds these three members to it.
VERBATIM_ROLES = frozenset({"lighting", "disp-lightmap-alphas", "disp-lightmap-sample-positions"})

#: How a lump 8 range a face paid for is spelled, and the parse that reads it back.
FACE_RANGE_PREFIX = "lighting.faces["
FACE_RANGE_OWNER = re.compile(r"^lighting\.faces\[(\d+)\]\.set\[(\d+)\]\.style\[(\d+)\]$")

#: What may own a range of each member's ledger.
LEDGER_OWNER_PREFIXES = {
    "lighting": ("lighting.faces[", "lighting.orphan["),
    "worldlights": ("worldLights[", "worldLights.unused", "worldLights.padding"),
    "disp-lightmap-alphas": ("dispAlphas",),
    "disp-lightmap-sample-positions": ("dispSamplePositions",),
    "detail-prop-lighting": ("dplt.count", "dplt.records[", "dplt.unused", "dplt.padding"),
}

DEPENDENCY_ROLES = ("map", "map-entities")


class MapLightingGlbValidationError(ValueError):
    """A published map-lighting unit contradicts its source or itself."""


def _require(condition: Any, message: str) -> None:
    if not condition:
        raise MapLightingGlbValidationError(message)


def _accessor_bytes(document: Mapping[str, Any], binary: bytes, index: Any) -> bytes:
    accessors = document.get("accessors") or []
    _require(isinstance(index, int) and 0 <= index < len(accessors),
             f"accessor {index!r} is outside the unit")
    accessor = accessors[index]
    views = document.get("bufferViews") or []
    view_index = accessor.get("bufferView")
    _require(isinstance(view_index, int) and 0 <= view_index < len(views),
             f"accessor {index} names no bufferView")
    view = views[view_index]
    start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    return binary[start:start + int(accessor["count"])]


def _check_accessor_block(
    document: Mapping[str, Any], binary: bytes, block: Any, name: str, lump: int
) -> None:
    """One verbatim lump: the accessor is the whole of it, and its digest says so."""

    if block is None:
        return
    _require(isinstance(block, Mapping), f"{name} is not a record")
    length = int(block.get("byteLength", -1))
    _require(length >= 0, f"{name} declares {length} bytes")
    _require(int(block.get("lump", -1)) == lump, f"{name} does not name lump {lump}")
    if not length:
        _require(block.get("accessor") is None, f"{name} is empty and still names an accessor")
        return
    payload = _accessor_bytes(document, binary, block.get("accessor"))
    _require(len(payload) == length,
             f"{name} accessor holds {len(payload)} bytes against a declared {length}")
    _require(hashlib.sha256(payload).hexdigest() == str(block.get("sha256")),
             f"{name} accessor disagrees with the digest it publishes")


def _check_faces(root: Mapping[str, Any]) -> None:
    """Every face row is internally consistent and its spans lie inside the bake."""

    samples = root.get("samples")
    sample_bytes = int((samples or {}).get("byteLength", 0))
    faces = root.get("faces")
    _require(isinstance(faces, list), "the unit publishes no faces table")
    for face in faces:
        _require(isinstance(face, Mapping), "a face row is not a record")
        index = face.get("index")
        styles = list(face.get("styles") or [])
        _require(len(styles) == 8, f"face {index} publishes {len(styles)} style slots")
        live = [style for style in styles if style != UNUSED_STYLE]
        _require(int(face["styleCount"]) == len(live),
                 f"face {index} counts {face['styleCount']} live styles against {len(live)}")
        sets = BUMP_LIGHTMAP_SETS if face.get("bumped") else 1
        _require(int(face["lightmapSets"]) == sets,
                 f"face {index} claims {face['lightmapSets']} lightmap sets")
        size = list(face.get("lightmapSize") or [])
        _require(len(size) == 2, f"face {index} publishes no lightmap size")
        _require(int(face["luxelWidth"]) == size[0] + 1
                 and int(face["luxelHeight"]) == size[1] + 1,
                 f"face {index} luxel extent disagrees with its lightmap size")
        block = int(face["luxelWidth"]) * int(face["luxelHeight"]) * LUXEL_BYTES
        spans = list(face.get("spans") or [])
        if int(face["lightOffset"]) == -1:
            _require(not spans, f"face {index} has no samples and still publishes spans")
            _require(int(face["byteLength"]) == 0,
                     f"face {index} has no samples and claims {face['byteLength']} bytes")
            continue
        if not spans:
            # A face whose extent left the lump keeps its row and claims nothing; the anomaly
            # table is where that is stated.
            _require(int(face["byteLength"]) == 0,
                     f"face {index} publishes no span and still claims bytes")
            continue
        _require(len(spans) == sets * len(live),
                 f"face {index} publishes {len(spans)} spans against {sets * len(live)}")
        _require(int(face["byteLength"]) == block * sets * len(live),
                 f"face {index} byteLength disagrees with its luxel extent")
        cursor = int(face["lightOffset"])
        for order, span in enumerate(spans):
            _require(int(span["offset"]) == cursor,
                     f"face {index} span {order} starts at {span['offset']}, not {cursor}")
            _require(int(span["length"]) == block,
                     f"face {index} span {order} is {span['length']} bytes, not {block}")
            _require(int(span["set"]) == order % sets
                     and int(span["styleIndex"]) == order // sets,
                     f"face {index} span {order} is not style-major, set-minor")
            _require(int(span["style"]) == live[order // sets],
                     f"face {index} span {order} names style {span['style']}")
            cursor += block
        _require(cursor <= sample_bytes,
                 f"face {index} runs to {cursor} past the {sample_bytes}-byte samples accessor")


def _check_world_lights(root: Mapping[str, Any], members: Mapping[str, Mapping[str, Any]]) -> None:
    member = members.get("worldlights")
    lights = root.get("worldLights")
    _require(isinstance(lights, list), "the unit publishes no world light table")
    if member is None:
        _require(not lights, "world lights are published without a lump 15 member")
        return
    length = int(member.get("byteLength", 0))
    span = member.get("span") or {}
    base = int(span.get("offset", 0))
    _require(len(lights) * WORLD_LIGHT_BYTES <= length,
             f"{len(lights)} world lights do not fit the {length}-byte lump")
    previous = -1
    for light in lights:
        index = int(light["index"])
        # A record the JSON chunk cannot state keeps its anomaly row and no light row, so the
        # published indices ascend without being required to be consecutive.
        _require(index > previous, f"world light {index} is out of order")
        previous = index
        _require(int(light["sourceOffset"]) == base + index * WORLD_LIGHT_BYTES,
                 f"world light {index} does not keep the offset it was read from")
        expected = LIGHT_TYPE_NAMES.get(int(light["type"]))
        _require(light.get("typeName") == expected,
                 f"world light {index} names type {light.get('typeName')!r}")
    census = {int(row["type"]): int(row["count"]) for row in root.get("lightTypeCensus") or []}
    counted: dict[int, int] = {}
    for light in lights:
        counted[int(light["type"])] = counted.get(int(light["type"]), 0) + 1
    _require(census == counted, "lightTypeCensus disagrees with the world light rows")


def _check_style_census(root: Mapping[str, Any]) -> None:
    counted: dict[int, int] = {}
    for face in root.get("faces") or []:
        for style in {value for value in face.get("styles") or [] if value != UNUSED_STYLE}:
            counted[style] = counted.get(style, 0) + 1
    census = {int(row["style"]): int(row["faces"]) for row in root.get("styleCensus") or []}
    _require(census == counted, "styleCensus disagrees with the face rows")


def _check_displacements(root: Mapping[str, Any]) -> None:
    alphas = int((root.get("dispAlphas") or {}).get("byteLength", 0))
    samples = int((root.get("dispSamplePositions") or {}).get("byteLength", 0))
    for row in root.get("displacements") or []:
        for start, length, total, name in (
            (row["alphaStart"], row["alphaLength"], alphas, "dispAlphas"),
            (row["samplePositionStart"], row["samplePositionLength"], samples,
             "dispSamplePositions"),
        ):
            _require(int(length) >= 0, f"displacement {row['index']} claims {length} bytes")
            if not int(length):
                continue
            _require(0 <= int(start) and int(start) + int(length) <= total,
                     f"displacement {row['index']} runs outside {name}")


def _check_detail_prop_lighting(
    root: Mapping[str, Any], members: Mapping[str, Mapping[str, Any]]
) -> None:
    member = members.get("detail-prop-lighting")
    rows = root.get("detailPropLighting")
    _require(isinstance(rows, list), "the unit publishes no detail-prop lighting table")
    if member is None:
        _require(not rows, "detail-prop lighting is published without a dplt member")
        return
    length = int(member.get("byteLength", 0))
    base = int((member.get("span") or {}).get("offset", 0))
    if length < 4:
        # A payload too short to hold its own count word decodes no record and is carried whole.
        _require(not rows, f"a {length}-byte dplt payload publishes {len(rows)} record(s)")
        _require(
            any(str(row.get("role")) == "short-dplt-payload" for row in root.get("omissions") or ())
            or not length,
            f"a {length}-byte dplt payload is not stated as a short payload",
        )
        return
    _require(4 + len(rows) * DETAIL_PROP_LIGHT_BYTES <= length,
             f"{len(rows)} dplt records do not fit the {length}-byte payload")
    for index, row in enumerate(rows):
        _require(int(row["index"]) == index, f"dplt record {index} is out of order")
        _require(int(row["sourceOffset"]) == base + 4 + index * DETAIL_PROP_LIGHT_BYTES,
                 f"dplt record {index} does not keep the offset it was read from")
        _require(0 <= int(row["style"]) <= 255, f"dplt record {index} has style {row['style']}")


def _check_ledger_owners(root: Mapping[str, Any]) -> None:
    """Who paid for each range, and -- for lump 8 -- that the range is the span it names.

    The gapless sweep proves the range table partitions the lump and the face check proves each
    face row is internally consistent, but neither joins the two: a range of the right length at
    the wrong offset would satisfy both. So every `lighting.faces[i].set[k].style[s]` range is
    matched against the face row's own span, and the face rows and the lump 8 ranges are required
    to name each other one for one.
    """

    roles = {
        str(member["path"]): str(member["role"])
        for member in (root.get("sourceResolution") or {}).get("members") or []
    }
    expected: dict[str, tuple[int, int]] = {}
    for face in root.get("faces") or []:
        index = int(face["index"])
        for span in face.get("spans") or []:
            owner = f"lighting.faces[{index}].set[{span['set']}].style[{span['styleIndex']}]"
            _require(owner not in expected, f"face {index} publishes {owner} twice")
            expected[owner] = (int(span["offset"]), int(span["length"]))
    paid: set[str] = set()
    for ledger in (root.get("coverage") or {}).get("byteLedger") or []:
        role = roles.get(str(ledger.get("sourcePath")), "")
        allowed = LEDGER_OWNER_PREFIXES.get(role)
        _require(allowed is not None, f"ledger names member role {role!r}")
        for entry in ledger.get("ranges") or []:
            owner = str(entry.get("owner"))
            _require(any(owner.startswith(prefix) for prefix in allowed),
                     f"{ledger['sourcePath']}: {owner} is not an owner of a {role} range")
            if role != "lighting" or not owner.startswith(FACE_RANGE_PREFIX):
                continue
            _require(FACE_RANGE_OWNER.match(owner) is not None,
                     f"{owner} is not a (face, set, style) owner")
            _require(owner in expected, f"{owner} pays for a range no face row states")
            _require(owner not in paid, f"{owner} pays for two ranges")
            paid.add(owner)
            _require((int(entry["offset"]), int(entry["length"])) == expected[owner],
                     f"{owner} claims {entry['offset']}+{entry['length']} against the face row's "
                     f"{expected[owner][0]}+{expected[owner][1]}")
    missing = sorted(set(expected) - paid)
    _require(not missing, f"{len(missing)} face span(s) pay for no range, from {missing[:1]}")


def _check_dependencies(root: Mapping[str, Any], key: str) -> None:
    rows = root.get("dependencies") or []
    _require([str(row["role"]) for row in rows] == list(DEPENDENCY_ROLES),
             f"a lighting unit declares the dependencies {list(DEPENDENCY_ROLES)}")
    for row in rows:
        expected = f"vtmb:{row['role']}:{key}"
        _require(str(row["asset"]) == expected,
                 f"dependency {row['role']} names {row['asset']}, not {expected}")
        _require(bool(row["resolved"]), f"dependency {row['role']} did not resolve")


def _check_members(root: Mapping[str, Any], key: str) -> dict[str, Mapping[str, Any]]:
    """Every member is a span of this map's own BSP, and each role appears once."""

    members = (root.get("sourceResolution") or {}).get("members") or []
    _require(members, "the unit publishes no source member")
    by_role: dict[str, Mapping[str, Any]] = {}
    for member in members:
        role = str(member.get("role"))
        _require(role in LEDGER_OWNER_PREFIXES, f"member role {role!r} is not this seam's")
        _require(role not in by_role, f"two members fill the {role} role")
        by_role[role] = member
        _require(str(member.get("path", "")).startswith(f"maps/{key}.bsp#"),
                 f"member {member.get('path')!r} is not a span of maps/{key}.bsp")
        span = member.get("span")
        _require(isinstance(span, Mapping), f"member {member.get('path')} carries no span")
        _require(int(span["length"]) == int(member["byteLength"]),
                 f"member {member.get('path')} span disagrees with its byteLength")
    _require("lighting" in by_role, "the unit publishes no lump 8 member")
    _require("worldlights" in by_role, "the unit publishes no lump 15 member")
    return by_role


def _directory(data: bytes, index: int) -> tuple[int, int]:
    offset, length = struct.unpack_from(
        "<ii", data, LUMP_DIRECTORY_OFFSET + index * LUMP_ENTRY_SIZE
    )
    return int(offset), int(length)


def _reread(root: Mapping[str, Any], data: bytes) -> None:
    """Re-decode the map's own tables and weigh the published rows against them.

    This is the export-time half: the `derived` face rows and the displacement runs are read from
    lumps this unit does not own, so the only place they can be proven is beside the file.
    """

    _require(data[:4] == b"VBSP", "the selected member does not open with the BSP magic")
    for row in (root.get("map") or {}).get("lumps") or []:
        offset, length = _directory(data, int(row["lump"]))
        _require((offset, length) == (int(row["offset"]), int(row["length"])),
                 f"lump {row['lump']} is {offset}+{length} in the file, not "
                 f"{row['offset']}+{row['length']}")
    face_offset, face_length = _directory(data, FACE_LUMP)
    texinfo_offset, texinfo_length = _directory(data, TEXINFO_LUMP)
    texinfo_count = texinfo_length // TEXINFO_BYTES
    faces = root.get("faces") or []
    _require(len(faces) == face_length // FACE_BYTES,
             f"the unit publishes {len(faces)} faces against {face_length // FACE_BYTES}")
    for index, face in enumerate(faces):
        offset = face_offset + index * FACE_BYTES
        _require(int(face["sourceOffset"]) == offset,
                 f"face {index} does not keep the offset it was read from")
        tex_info = struct.unpack_from("<h", data, offset + 42)[0]
        styles = list(struct.unpack_from("<8B", data, offset + 48))
        light_offset = struct.unpack_from("<i", data, offset + 72)[0]
        mins_x, mins_y, size_x, size_y = struct.unpack_from("<4i", data, offset + 80)
        flags = 0
        if 0 <= tex_info < texinfo_count:
            flags = struct.unpack_from(
                "<i", data, texinfo_offset + tex_info * TEXINFO_BYTES + 64
            )[0]
        _require(int(face["texInfo"]) == tex_info and list(face["styles"]) == styles,
                 f"face {index} disagrees with the root's face record")
        _require(int(face["lightOffset"]) == light_offset,
                 f"face {index} publishes lightofs {face['lightOffset']}, not {light_offset}")
        _require(list(face["lightmapMins"]) == [mins_x, mins_y]
                 and list(face["lightmapSize"]) == [size_x, size_y],
                 f"face {index} disagrees with the root's lightmap placement")
        _require(bool(face["bumped"]) == bool(flags & SURF_BUMPLIGHT),
                 f"face {index} disagrees with SURF_BUMPLIGHT on its texinfo")
        _require([list(sample) for sample in face["avgLightColor"]]
                 == [list(struct.unpack_from("<3Bb", data, offset + slot * 4))
                     for slot in range(8)],
                 f"face {index} disagrees with the root's average light colour")
    dispinfo_offset, dispinfo_length = _directory(data, DISPINFO_LUMP)
    rows = root.get("displacements") or []
    _require(len(rows) == dispinfo_length // DISPINFO_BYTES,
             f"the unit publishes {len(rows)} displacements against "
             f"{dispinfo_length // DISPINFO_BYTES}")
    for index, row in enumerate(rows):
        offset = dispinfo_offset + index * DISPINFO_BYTES
        alpha_start, sample_start = struct.unpack_from("<2i", data, offset + 40)
        _require(int(row["sourceOffset"]) == offset,
                 f"displacement {index} does not keep the offset it was read from")
        _require(int(row["alphaStart"]) == alpha_start
                 and int(row["samplePositionStart"]) == sample_start,
                 f"displacement {index} disagrees with the root's dispinfo record")
        _require(int(row["power"]) == struct.unpack_from("<i", data, offset + 20)[0],
                 f"displacement {index} disagrees with the root's displacement power")


def _reread_members(
    root: Mapping[str, Any], members: Sequence[Any], document: Mapping[str, Any], binary: bytes
) -> None:
    """The world lights and the dplt records, re-read from the spans themselves."""

    by_role = {getattr(member, "role", ""): member for member in members}
    lights = root.get("worldLights") or []
    worldlights = by_role.get("worldlights")
    if worldlights is not None:
        for light in lights:
            offset = int(light["sourceOffset"]) - worldlights.span_offset
            values = struct.unpack_from("<9f3i7f3i", worldlights.data, offset)
            published = (
                tuple(light["origin"]["source"]) + tuple(light["intensity"])
                + tuple(light["normal"]["source"])
                + (light["cluster"], light["type"], light["style"])
                + (light["stopdot"], light["stopdot2"], light["exponent"],
                   light["radius"]["source"], light["constantAttn"], light["linearAttn"],
                   light["quadraticAttn"])
                + (light["flags"], light["texinfo"], light["owner"])
            )
            _require(tuple(values) == published,
                     f"world light {light['index']} disagrees with its 88 bytes")
    detail = by_role.get("detail-prop-lighting")
    rows = root.get("detailPropLighting") or []
    if detail is not None and detail.byte_length >= 4:
        declared = struct.unpack_from("<i", detail.data, 0)[0]
        anomalies = [
            row for row in root.get("anomalies") or []
            if str(row.get("role")) == "dplt-count-mismatch"
        ]
        _require(declared == len(rows) or anomalies,
                 f"the dplt payload declares {declared} records against {len(rows)}")
        for row in rows:
            offset = int(row["sourceOffset"]) - detail.span_offset
            red, green, blue, exponent, style = struct.unpack_from(
                "<3BbB", detail.data, offset
            )
            _require((red, green, blue, exponent, style)
                     == (row["r"], row["g"], row["b"], row["exponent"], row["style"]),
                     f"dplt record {row['index']} disagrees with its five bytes")
    for member in members:
        if getattr(member, "role", "") not in VERBATIM_ROLES or not member.byte_length:
            continue
        name = {
            "lighting": "samples",
            "disp-lightmap-alphas": "dispAlphas",
            "disp-lightmap-sample-positions": "dispSamplePositions",
        }[member.role]
        block = root.get(name) or {}
        payload = _accessor_bytes(document, binary, block.get("accessor"))
        _require(payload == member.data, f"the {name} accessor is not lump {block.get('lump')}")


def _grouped(rows: Sequence[Mapping[str, Any]]) -> list[dict[str, Any]]:
    """One row per role, so an operator reads `orphan-lighting-bytes` once and not ten thousand
    times. The rows themselves stay in the unit, one per contiguous run."""

    order: list[str] = []
    totals: dict[str, list[int]] = {}
    for row in rows:
        role = str(row.get("role", "")) if isinstance(row, Mapping) else str(row)
        if role not in totals:
            order.append(role)
            totals[role] = [0, 0]
        totals[role][0] += 1
        totals[role][1] += int(row.get("byteLength", 0) or 0) if isinstance(row, Mapping) else 0
    grouped: list[dict[str, Any]] = []
    for role in order:
        count, byte_length = totals[role]
        reason = role if count == 1 else f"{role} ({count} rows)"
        grouped.append({"reason": f"{reason}, {byte_length} bytes" if byte_length else reason})
    return grouped


def validate_document(
    document: Mapping[str, Any],
    binary: bytes,
    *,
    source_members: Sequence[Any] | None = None,
    map_bytes: bytes | None = None,
) -> dict[str, Any]:
    """Every check a published map-lighting unit answers to, in one pass.

    `map_bytes` is the export-time addition: the four units of one map share a file, and the
    `derived` rows this unit restates come from lumps the root owns, so proving them needs the
    file rather than the spans.
    """

    try:
        root = validate_extension_root(
            document,
            MAP_LIGHTING_EXTENSION,
            asset_prefix=ASSET_PREFIX,
            schema_version=SCHEMA_VERSION,
        )
        _require(document.get("asset", {}).get("generator") == generator("Map-lighting"),
                 "asset.generator does not name the Map-lighting GLB Exporter")
        _require(list(document.get("extensionsUsed") or []) == [MAP_LIGHTING_EXTENSION],
                 "a lighting unit declares its own extension and no other")
        _require(list(document.get("extensionsRequired") or []) == [MAP_LIGHTING_EXTENSION],
                 f"{MAP_LIGHTING_EXTENSION} is not the unit's one required extension")
        validate_sceneless(document)
        validate_container(document, binary)
        validate_accessors(document, binary)
        validate_ledgers(root, source_members)
        validate_capsules(document, binary, root, source_members)
    except UnitValidationError as error:
        raise MapLightingGlbValidationError(str(error)) from error
    counts = completeness(root)
    _require(not counts["unresolved"], "the unit publishes unresolved rows")
    _require(not counts["unsupported"], "the unit publishes unsupported rows")
    identity = root["identity"]
    key = str(identity["asset"])[len(ASSET_PREFIX):]
    _require(key and str(identity.get("map", "")) == key,
             f"identity {identity['asset']!r} does not name the map it was cut from")
    _require(str(identity.get("sourcePath", "")) == f"maps/{key}.bsp",
             f"identity names the source {identity.get('sourcePath')!r}, not maps/{key}.bsp")
    members = _check_members(root, key)
    ledgers = (root.get("coverage") or {}).get("byteLedger") or []
    _require(len(ledgers) == len(members),
             f"{len(ledgers)} ledger(s) against {len(members)} member(s)")
    _check_accessor_block(document, binary, root.get("samples"), "samples", LIGHTING_LUMP)
    _check_accessor_block(document, binary, root.get("dispAlphas"), "dispAlphas", DISP_ALPHA_LUMP)
    _check_accessor_block(
        document, binary, root.get("dispSamplePositions"), "dispSamplePositions",
        DISP_SAMPLE_LUMP,
    )
    for name, role in (
        ("samples", "lighting"),
        ("dispAlphas", "disp-lightmap-alphas"),
        ("dispSamplePositions", "disp-lightmap-sample-positions"),
    ):
        block = root.get(name)
        member = members.get(role)
        _require((block is None) == (member is None),
                 f"{name} and its {role} member do not agree on being present")
        if block is not None and member is not None:
            _require(int(block["byteLength"]) == int(member["byteLength"])
                     and str(block["sha256"]) == str(member["sha256"]),
                     f"{name} disagrees with the {role} member it publishes")
    _check_faces(root)
    _check_style_census(root)
    _check_world_lights(root, members)
    _check_displacements(root)
    _check_detail_prop_lighting(root, members)
    _check_ledger_owners(root)
    _check_dependencies(root, key)
    if source_members is not None:
        _reread_members(root, source_members, document, binary)
    if map_bytes is not None:
        _reread(root, map_bytes)
    return {
        "asset": str(identity["asset"]),
        "map": key,
        "sourcePaths": [str(member["path"]) for member in
                        (root.get("sourceResolution") or {}).get("members") or []],
        "sourceBytes": sum(int(row["byteLength"]) for row in ledgers),
        "accountedBytes": sum(int(row["accountedBytes"]) for row in ledgers),
        "byteCoveragePercent": [float(row["coveragePercent"]) for row in ledgers],
        "faces": len(root.get("faces") or []),
        "litFaces": sum(1 for face in root.get("faces") or [] if face.get("spans")),
        "worldLights": len(root.get("worldLights") or []),
        "displacements": len(root.get("displacements") or []),
        "detailPropLighting": len(root.get("detailPropLighting") or []),
        "orphanRuns": sum(1 for row in root.get("omissions") or []
                          if str(row.get("role")) == "orphan-lighting-bytes"),
        "orphanBytes": sum(int(row.get("byteLength", 0)) for row in root.get("omissions") or []
                           if str(row.get("role")) == "orphan-lighting-bytes"),
        "dependencies": len(root.get("dependencies") or []),
        "unresolved": counts["unresolved"],
        "unsupported": counts["unsupported"],
        "typedUnidentified": counts["typedUnidentified"],
        "warnings": contract_warnings(
            {
                "coverage": root.get("coverage") or {},
                "anomalies": _grouped(root.get("anomalies") or []),
                "omissions": _grouped(root.get("omissions") or []),
            }
        ),
    }


def validate(path: Path) -> dict[str, Any]:
    """Read a published unit with no install present and hold it to the contract."""

    document, binary = read_glb(Path(path))
    return validate_document(document, binary)


def warnings_for(summary: Mapping[str, Any]) -> list[str]:
    """What the published unit could not resolve, phrased for the operator."""

    return list(summary.get("warnings") or [])


__all__ = [
    "MapLightingGlbValidationError",
    "read_glb",
    "validate",
    "validate_document",
    "warnings_for",
]
