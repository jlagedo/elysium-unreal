"""Independent structural validator for Map-visibility GLB products.

Nothing here imports the exporter, and nothing here reuses the writer's arithmetic: the
run-length decompressor, the `ceil(numClusters / 8)` row width and the set-bit count below are
all second implementations, written out again so an export-time run really does re-derive every
published number rather than re-run the function that produced it. Only the record layouts --
which lump is which, how wide a `dleaf_t` is -- come from the format package, because those are
facts about the source rather than steps of the decode.

Standalone -- with no install present -- the validator still checks the container, the scene-less
rule, every accessor's length against `rowByteLength`, every published set-bit count against the
BIN chunk, the ledger's continuity and owner vocabulary, and the internal agreement between the
cluster table, the shared-row rule, the leaf partition and the portal entries.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
import re
from typing import Any, Mapping, Sequence

from elysium_pipeline.formats.map_visibility_glb.model import (
    ANOMALY_ROLES,
    BYTEOFS_ENTRY_BYTES,
    KIND,
    LEAF_CLUSTER_OFFSET,
    LEAF_RECORD_BYTES,
    MAP_VISIBILITY_EXTENSION,
    MAPPED_STATES,
    OMISSION_ROLES,
    OWNED_LUMPS,
    PORTAL_CANDIDATES,
    PORTAL_KEYS,
    SCHEMA_VERSION,
    VIS_HEADER_BYTES,
    VISIBILITY_LUMP,
)
from elysium_pipeline.formats.unit_contract import (
    SourceMember,
    UnitValidationError,
    completeness,
    read_glb,
    reject_opaque_source,
    validate_accessors,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
    warnings_for as _contract_warnings_for,
)

ASSET_PREFIX = f"vtmb:{KIND}:"

#: `maps/<map>.bsp#lump<n>` -- the identity a span member of one BSP is published under.
_MEMBER_PATH = re.compile(r"^maps/(?P<stem>[^/]+)\.bsp#lump(?P<lump>\d+)$")

_HEADER_OWNER = "vis.header"
_BYTEOFS_OWNER = re.compile(r"^vis\.byteofs\[(\d+)\]$")
_ROW_OWNER = re.compile(r"^vis\.(pvs|pas)\[(\d+)\]$")
_UNREFERENCED_OWNER = re.compile(r"^vis\.unreferenced\[(\d+)\]$")

_UNSIGNED_BYTE = 5121

#: The digest an empty selecting member's omission row carries, worked out here rather than
#: taken from the writer.
_EMPTY_SHA256 = hashlib.sha256(b"").hexdigest()


class MapVisibilityGlbValidationError(UnitValidationError):
    """A published map-visibility unit contradicts its seam or the unit contract."""


def _decompress(vis: bytes, offset: int, row_bytes: int) -> tuple[bytes, int]:
    """The run-length scheme, written out again so this file owes the decoder nothing."""

    produced = bytearray()
    reader = offset
    while len(produced) < row_bytes and reader < len(vis):
        value = vis[reader]
        reader += 1
        if value:
            produced.append(value)
        elif reader < len(vis):
            produced.extend(b"\0" * vis[reader])
            reader += 1
        else:
            break
    del produced[row_bytes:]
    return bytes(produced), min(reader, len(vis)) - offset


def _row_width(num_clusters: int) -> int:
    """`ceil(numClusters / 8)`, worked out again rather than asked of the writer."""

    if num_clusters <= 0:
        return 0
    whole, spare = divmod(int(num_clusters), 8)
    return whole + (1 if spare else 0)


def _count_bits(row: bytes, limit: int) -> int:
    """The row's set bits below `limit`, counted byte by byte rather than as one big integer."""

    total = 0
    for index in range(min(int(limit), 8 * len(row))):
        if row[index // 8] & (1 << (index % 8)):
            total += 1
    return total


def _require(condition: Any, message: str) -> None:
    if not condition:
        raise MapVisibilityGlbValidationError(message)


def _accessor_bytes(document: Mapping[str, Any], binary: bytes, index: int) -> bytes:
    accessor = (document.get("accessors") or [])[index]
    view = (document.get("bufferViews") or [])[int(accessor["bufferView"])]
    start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    return bytes(binary[start:start + int(accessor.get("count", 0))])


def _check_identity(root: Mapping[str, Any]) -> str:
    identity = root["identity"]
    asset = str(identity["asset"])
    stem = asset[len(ASSET_PREFIX):]
    _require(stem and stem == stem.lower(), f"{asset} is not a folded map key")
    _require(
        identity.get("sourcePath") == f"maps/{stem}.bsp",
        "the identity's source path is not this map's BSP",
    )
    _require(identity.get("map") == stem, "the identity's map key disagrees with its asset ID")
    _require(
        (root.get("map") or {}).get("stem") == stem,
        "map.stem disagrees with the unit's identity",
    )
    return stem


def _check_members(root: Mapping[str, Any], stem: str) -> dict[int, dict[str, Any]]:
    """Every member is one owned lump's span of this map's BSP, at most one per lump."""

    members = list((root["sourceResolution"]).get("members") or [])
    _require(members, "a map-visibility unit owns at least the VISIBILITY span")
    by_lump: dict[int, dict[str, Any]] = {}
    for member in members:
        match = _MEMBER_PATH.match(str(member.get("path", "")))
        _require(match is not None, f"source member {member.get('path')!r} is not a lump span")
        assert match is not None
        _require(match["stem"] == stem, "a source member is cut from another map's BSP")
        lump = int(match["lump"])
        _require(lump in OWNED_LUMPS, f"lump {lump} is not owned by the map-visibility unit")
        _require(lump not in by_lump, f"lump {lump} is published as two source members")
        span = member.get("span")
        _require(isinstance(span, Mapping), f"lump {lump} member carries no span")
        _require(
            int(span["length"]) == int(member["byteLength"]),
            f"lump {lump} span length disagrees with the member",
        )
        by_lump[lump] = member
    _require(VISIBILITY_LUMP in by_lump, "the unit does not own the VISIBILITY span")
    return by_lump


def _check_map_block(root: Mapping[str, Any], by_lump: dict[int, dict[str, Any]]) -> None:
    block = root.get("map") or {}
    lumps = block.get("lumps")
    _require(isinstance(lumps, list), "map.lumps is not a table")
    _require(
        [int(row["lump"]) for row in lumps] == list(OWNED_LUMPS),
        "map.lumps does not list the five owned lumps in order",
    )
    for row in lumps:
        lump = int(row["lump"])
        present = bool(row["present"])
        # Lump 4 selects the unit and is always a member, empty or not; a portal lump is a
        # member exactly when the directory gave it bytes.
        if lump == VISIBILITY_LUMP:
            _require(
                lump in by_lump,
                "map.lumps[4] does not name the VISIBILITY member that selects the unit",
            )
            _require(
                present == (int(by_lump[lump]["byteLength"]) > 0),
                "map.lumps[4].present disagrees with the size of its member",
            )
        else:
            _require(
                present == (lump in by_lump),
                f"map.lumps[{lump}].present disagrees with the source member table",
            )
        if present:
            member = by_lump[lump]
            _require(
                int(row["offset"]) == int(member["span"]["offset"])
                and int(row["length"]) == int(member["byteLength"]),
                f"map.lumps[{lump}] disagrees with its span",
            )
        else:
            _require(
                int(row["length"]) == 0,
                f"map.lumps[{lump}] is absent and still declares a length",
            )
            _require(
                lump not in by_lump or int(by_lump[lump]["byteLength"]) == 0,
                f"map.lumps[{lump}] is absent and its member still holds bytes",
            )
            # An absent lump still has a directory row, and where it points is published
            # verbatim; a unit that names one place in the lump table and another in the
            # member's span states the same directory row two ways.
            _require(
                lump not in by_lump
                or int(row["offset"]) == int(by_lump[lump]["span"]["offset"]),
                f"map.lumps[{lump}] is absent and its offset disagrees with its span",
            )
    empty = int(by_lump[VISIBILITY_LUMP]["byteLength"]) == 0
    named = any(
        row.get("role") == "empty-member" for row in root.get("omissions") or []
    )
    _require(
        empty == named,
        "an empty-member omission is published without an empty selecting member, or the "
        "other way round",
    )
    for entry in root.get("omissions") or []:
        if entry.get("role") != "empty-member":
            continue
        _require(
            int(entry.get("byteLength", -1)) == 0
            and entry.get("sha256") == _EMPTY_SHA256
            and entry.get("owner") == by_lump[VISIBILITY_LUMP]["path"],
            "the empty-member omission does not carry the empty selecting member's evidence",
        )
    leaf = block.get("leafTable") or {}
    _require(
        int(leaf.get("recordBytes", 0)) == LEAF_RECORD_BYTES,
        "map.leafTable does not describe the 32-byte dleaf_t",
    )
    _require(
        int(leaf.get("count", -1)) == int(leaf.get("byteLength", -1)) // LEAF_RECORD_BYTES,
        "map.leafTable's record count disagrees with its byte length",
    )


def _check_portals(
    root: Mapping[str, Any], by_lump: dict[int, dict[str, Any]]
) -> list[dict[str, Any]]:
    portals = root.get("portals")
    _require(isinstance(portals, dict), "portals is not a record")
    typed = list((root["coverage"]).get("typedUnidentified") or [])
    typed_fields = {str(row.get("field")) for row in typed}
    typed_by_field = {str(row.get("field")): row for row in typed}
    for lump, key in PORTAL_KEYS.items():
        member = by_lump.get(lump)
        if member is None:
            _require(key not in portals, f"portals.{key} is published for an absent lump")
            _require(
                f"portals.{key}" not in typed_fields,
                f"portals.{key} is graded typed-unidentified for an absent lump",
            )
            continue
        entry = portals.get(key)
        _require(isinstance(entry, Mapping), f"lump {lump} has a span and no portals.{key} entry")
        assert isinstance(entry, Mapping)
        candidate, record_bytes = PORTAL_CANDIDATES[lump]
        length = int(member["byteLength"])
        _require(int(entry["lump"]) == lump, f"portals.{key} names lump {entry['lump']}")
        _require(entry["member"] == member["path"], f"portals.{key} names another member")
        _require(
            int(entry["byteLength"]) == length,
            f"portals.{key} length disagrees with its span",
        )
        # The entry restates neither the lump's position nor its digest -- the member row and
        # map.lumps[] already state both -- so the identity it is filed under is checked on the
        # typedUnidentified row that carries it.
        _require(
            {"offset", "sha256"}.isdisjoint(entry),
            f"portals.{key} restates a position or digest its member row already states",
        )
        _require(
            entry["candidate"] == {"struct": candidate, "recordBytes": record_bytes},
            f"portals.{key} names a candidate this seam does not test against",
        )
        _require(
            bool(entry["divides"]) == (length % record_bytes == 0)
            and int(entry["remainder"]) == length % record_bytes
            and int(entry["wholeRecords"]) == length // record_bytes,
            f"portals.{key} restates the divisibility test wrongly",
        )
        _require(
            f"portals.{key}" in typed_fields,
            f"portals.{key} is carried without a typedUnidentified row",
        )
        graded = typed_by_field[f"portals.{key}"]
        _require(
            graded.get("member") == member["path"]
            and graded.get("sha256") == member["sha256"]
            and int(graded.get("byteLength", -1)) == length,
            f"portals.{key} is graded against another span",
        )
        # A carried record keeps its source-offset identity, and the grade is where this seam
        # states it: the entry itself restates neither position nor digest.
        _require(
            int(graded.get("sourceOffset", -1)) == int(member["span"]["offset"]),
            f"portals.{key}'s typedUnidentified row does not name where the lump sits",
        )
    unknown = set(portals) - set(PORTAL_KEYS.values())
    _require(not unknown, f"portals carries unknown key(s) {sorted(unknown)}")
    _require(
        typed_fields == {f"portals.{key}" for key in portals},
        "typedUnidentified names something other than the carried portal lumps",
    )
    _require(
        len(typed) == len(typed_fields),
        "a portal lump is graded typed-unidentified more than once",
    )
    return typed


def _check_rows(
    document: Mapping[str, Any], binary: bytes, root: Mapping[str, Any]
) -> tuple[int, int]:
    """The cluster table against the accessors: one accessor per recorded row, right length,
    right set-bit counts, and every shared entry pointing back at a row already recorded."""

    num_clusters = int(root["numClusters"])
    row_bytes = int(root["rowByteLength"])
    clusters = list(root.get("clusters") or [])
    _require(len(clusters) == num_clusters, "clusters[] does not hold one row per cluster")
    _require(
        row_bytes == _row_width(num_clusters),
        "rowByteLength is not ceil(numClusters / 8)",
    )
    accessors = list(document.get("accessors") or [])
    anomalies = list(root.get("anomalies") or [])
    short = {
        (int(row.get("cluster", -1)), str(row.get("set")))
        for row in anomalies
        if str(row.get("role", "")).endswith("-row-short")
    }
    out_of_range = {
        (int(row.get("cluster", -1)), str(row.get("set")))
        for row in anomalies
        if row.get("role") == "byteofs-out-of-range"
    }

    owners: dict[int, tuple[int, str]] = {}
    recorded: dict[int, tuple[int, str]] = {}          # source offset -> the entry that owns it
    for index, cluster in enumerate(clusters):
        _require(int(cluster["index"]) == index, f"clusters[{index}] is out of source order")
        _require(
            int(cluster["sourceOffset"])
            == VIS_HEADER_BYTES + index * BYTEOFS_ENTRY_BYTES,
            f"clusters[{index}].sourceOffset does not name its byteofs entry",
        )
        for slot, kind in enumerate(("pvs", "pas")):
            where = f"clusters[{index}].{kind}"
            entry = cluster[kind]
            _require(isinstance(entry, Mapping), f"{where} is not a record")
            count = cluster["visibleCount" if kind == "pvs" else "audibleCount"]
            if entry.get("outOfRange"):
                _require(
                    (index, kind) in out_of_range,
                    f"{where} names no row and no byteofs-out-of-range anomaly says why",
                )
                _require(count is None, f"{where} names no row and still states a count")
                continue
            if "sharedWith" in entry:
                target = int(entry["sharedWith"])
                shared_set = str(entry["sharedSet"])
                _require(
                    shared_set in ("pvs", "pas"),
                    f"{where} shares a set that is neither pvs nor pas",
                )
                order = 2 * index + slot
                target_order = 2 * target + (0 if shared_set == "pvs" else 1)
                _require(
                    0 <= target < len(clusters) and target_order < order,
                    f"{where} shares a row that is not recorded before it",
                )
                source = clusters[target][shared_set]
                _require(
                    "compressedLength" in source
                    and int(source["offset"]) == int(entry["offset"]),
                    f"{where} shares an entry that records no row at that offset",
                )
                _require(
                    count
                    == clusters[target]["visibleCount" if shared_set == "pvs" else "audibleCount"],
                    f"{where} disagrees with the counts of the row it shares",
                )
                continue
            offset = int(entry["offset"])
            _require(
                offset not in recorded,
                f"{where} records a row another entry already records",
            )
            recorded[offset] = (index, kind)
            _require(
                int(entry["compressedLength"]) > 0,
                f"{where} claims a zero-length compressed span",
            )
            if entry.get("emptyRow"):
                # A row that decoded to no byte gets no accessor -- `accessor.count` has a
                # minimum of one in glTF -- and it is short by definition, so the anomaly that
                # says so has to be there and no count may be stated.
                _require(
                    "accessor" not in entry,
                    f"{where} decodes to no byte and still names an accessor",
                )
                _require(
                    (index, kind) in short,
                    f"{where} decodes to no byte and no {kind}-row-short anomaly says why",
                )
                _require(count is None, f"{where} decodes to no byte and still states a count")
                continue
            accessor = entry.get("accessor")
            _require(
                isinstance(accessor, int) and 0 <= accessor < len(accessors),
                f"{where} names no accessor of this unit",
            )
            _require(accessor not in owners, f"accessor {accessor} is claimed by two rows")
            owners[int(accessor)] = (index, kind)
            row = accessors[int(accessor)]
            _require(
                row.get("componentType") == _UNSIGNED_BYTE and row.get("type") == "SCALAR",
                f"{where} is not an UNSIGNED_BYTE bitset accessor",
            )
            length = int(row.get("count", -1))
            _require(length > 0, f"{where} publishes an accessor of {length} elements")
            if (index, kind) in short:
                _require(
                    length < row_bytes,
                    f"{where} is graded short and holds a full row",
                )
            else:
                _require(
                    length == row_bytes,
                    f"{where} holds {length} bytes against a {row_bytes}-byte row",
                )
            _require(
                count == _count_bits(_accessor_bytes(document, binary, int(accessor)), num_clusters),
                f"{where} disagrees with the set bits of its accessor",
            )
    _require(
        len(owners) == len(accessors),
        f"{len(accessors)} accessor(s) against {len(owners)} recorded row(s)",
    )
    return num_clusters, row_bytes


def _check_leaves(root: Mapping[str, Any]) -> None:
    """The derived leaf lists partition the root's leaf table, minus the leaves an anomaly names."""

    clusters = list(root.get("clusters") or [])
    unclustered = list(root.get("unclusteredLeaves") or [])
    _require(unclustered == sorted(unclustered), "unclusteredLeaves is not in leaf order")
    seen: set[int] = set()
    for cluster in clusters:
        leaves = list(cluster.get("leaves") or [])
        _require(
            leaves == sorted(leaves),
            f"clusters[{cluster['index']}].leaves is not in leaf order",
        )
        for leaf in leaves:
            _require(leaf not in seen, f"leaf {leaf} is derived to two clusters")
            seen.add(int(leaf))
    for leaf in unclustered:
        _require(leaf not in seen, f"leaf {leaf} is both clustered and unclustered")
        seen.add(int(leaf))
    stray = {
        int(row["leaf"])
        for row in root.get("anomalies") or []
        if row.get("role") == "leaf-cluster-out-of-range"
    }
    _require(not (stray & seen), "a leaf is both derived and graded out of range")
    count = int(((root.get("map") or {}).get("leafTable") or {}).get("count", 0))
    _require(
        seen | stray == set(range(count)),
        "the derived leaf lists do not partition the root's leaf table",
    )


def _check_ledger_owners(root: Mapping[str, Any], by_lump: dict[int, dict[str, Any]]) -> None:
    """The ledger's owners are the ones the seam names, and lump 4's rows agree with clusters[]."""

    ledgers = {str(row["sourcePath"]): row for row in (root["coverage"])["byteLedger"]}
    for lump, member in by_lump.items():
        row = ledgers[str(member["path"])]
        if lump == VISIBILITY_LUMP:
            continue
        ranges = list(row["ranges"])
        _require(
            len(ranges) == 1
            and ranges[0]["state"] == "mapped"
            and ranges[0]["owner"] == f"portals.{PORTAL_KEYS[lump]}",
            f"lump {lump}'s ledger is not one mapped range under its portal owner",
        )

    row = ledgers[str(by_lump[VISIBILITY_LUMP]["path"])]
    clusters = list(root.get("clusters") or [])
    declared = {
        (index, kind): (int(entry["offset"]), int(entry["compressedLength"]))
        for index, cluster in enumerate(clusters)
        for kind in ("pvs", "pas")
        for entry in [cluster[kind]]
        # Every entry that recorded a row pays for its compressed span, whether the row it
        # decoded to was published as an accessor or was empty.
        if "compressedLength" in entry
    }
    overlapped = {
        (int(entry.get("cluster", -1)), str(entry.get("set")))
        for entry in root.get("anomalies") or []
        if entry.get("role") == "row-span-overlap"
    }
    claimed: dict[tuple[int, str], list[tuple[int, int]]] = {}
    header = 0
    byteofs: set[int] = set()
    unreferenced: dict[str, tuple[int, int]] = {}
    for entry in row["ranges"]:
        owner, state = str(entry["owner"]), str(entry["state"])
        offset, length = int(entry["offset"]), int(entry["length"])
        if owner == _HEADER_OWNER:
            _require(state == "mapped" and offset == 0, "vis.header is not the mapped header")
            header += length
            continue
        match = _BYTEOFS_OWNER.match(owner)
        if match:
            index = int(match[1])
            _require(state == "mapped", f"{owner} is not mapped")
            _require(
                offset == VIS_HEADER_BYTES + index * BYTEOFS_ENTRY_BYTES
                and length == BYTEOFS_ENTRY_BYTES,
                f"{owner} does not cover cluster {index}'s two offsets",
            )
            byteofs.add(index)
            continue
        match = _ROW_OWNER.match(owner)
        if match:
            key = (int(match[2]), match[1])
            _require(state == "derived", f"{owner} is not a derived compressed span")
            _require(key in declared, f"{owner} pays for a row no cluster entry records")
            start, size = declared[key]
            _require(
                start <= offset and offset + length <= start + size,
                f"{owner} claims {offset}+{length}, outside the span its entry declares",
            )
            claimed.setdefault(key, []).append((offset, length))
            continue
        match = _UNREFERENCED_OWNER.match(owner)
        _require(match is not None, f"byte ledger owner {owner!r} names no record of this seam")
        _require(state == "omitted-proven", f"{owner} is not an evidence-backed omission")
        _require(owner not in unreferenced, f"{owner} pays for two ranges")
        unreferenced[owner] = (offset, length)
    # An omitted range is only evidence-backed if the evidence is about *these* bytes: every
    # `vis.unreferenced[n]` range is the omissions row filed under the same owner, extent for
    # extent, and every such row has a range. The digest is weighed at export time.
    omitted = {
        str(entry["owner"]): (int(entry["sourceOffset"]), int(entry["byteLength"]))
        for entry in root.get("omissions") or []
        if entry.get("role") == "unreferenced-vis-bytes"
    }
    _require(
        omitted == unreferenced,
        "the omitted-proven ranges of lump 4 are not the unreferenced-vis-bytes omissions",
    )
    for key, (start, size) in declared.items():
        rows = claimed.get(key, [])
        if key in overlapped:
            # Another owner already pays for part of this span; the row is not claimed twice and
            # the anomaly is what records why the ledger falls short of the declared extent.
            _require(
                sum(length for _, length in rows) < size,
                f"vis.{key[1]}[{key[0]}] is graded overlapping and still claims its whole span",
            )
        else:
            _require(
                rows == [(start, size)],
                f"vis.{key[1]}[{key[0]}] does not pay for exactly its declared span",
            )
    num_clusters = int(root["numClusters"])
    if int(row["byteLength"]) >= VIS_HEADER_BYTES:
        _require(header == VIS_HEADER_BYTES, "the ledger does not pay for the lump's header")
    _require(
        byteofs == set(range(num_clusters)),
        "the ledger does not pay for every cluster's byteofs entry",
    )


def _check_vocabularies(root: Mapping[str, Any]) -> None:
    for row in root.get("anomalies") or []:
        _require(
            isinstance(row, Mapping) and row.get("role") in ANOMALY_ROLES,
            f"unknown source anomaly {row!r}",
        )
    omissions = list(root.get("omissions") or [])
    for row in omissions:
        _require(
            isinstance(row, Mapping) and row.get("role") in OMISSION_ROLES,
            f"unknown source omission {row!r}",
        )
    # `coverage.omittedProven` summarises: one row per omission role, naming how many ranges
    # that role covers and how many bytes they come to, with `omissions[]` holding the ranges
    # themselves. The same range is never stated twice.
    proven = list((root["coverage"]).get("omittedProven") or [])
    roles: dict[str, list[Mapping[str, Any]]] = {}
    for row in omissions:
        roles.setdefault(str(row["role"]), []).append(row)
    _require(
        len(proven) == len(roles)
        and {str(row.get("role")) for row in proven} == set(roles),
        "omittedProven does not summarise omissions[] once per role",
    )
    for row in proven:
        covered = roles[str(row["role"])]
        _require(
            int(row.get("runs", -1)) == len(covered)
            and int(row.get("byteLength", -1))
            == sum(int(entry["byteLength"]) for entry in covered),
            f"the omittedProven row for {row.get('role')} disagrees with omissions[]",
        )
        _require(
            "sourceOffset" not in row,
            f"the omittedProven row for {row.get('role')} restates a range of its own",
        )
    for row in (root["coverage"]).get("mapped") or []:
        _require(
            isinstance(row, Mapping) and row.get("state") in MAPPED_STATES,
            f"coverage.mapped row {row!r} carries no semantic state",
        )


def _check_dependencies(root: Mapping[str, Any], stem: str) -> None:
    rows = list(root["dependencies"])
    _require(len(rows) == 1, "a map-visibility unit depends on exactly the map root")
    row = rows[0]
    _require(row["role"] == "map", "the one dependency is not the map root")
    _require(row["asset"] == f"vtmb:map:{stem}", "the map dependency names another map")
    _require(row["sourcePath"] == f"maps/{stem}.bsp", "the map dependency names another member")
    _require(bool(row["resolved"]), "the map dependency did not resolve")


def _check_against_decode(
    root: Mapping[str, Any],
    document: Mapping[str, Any],
    binary: bytes,
    source_members: Sequence[SourceMember],
    leaf_table: bytes | None,
) -> None:
    """Re-read the selected spans: every row decompressed again, every leaf list re-derived."""

    stem = str(root["identity"]["asset"])[len(ASSET_PREFIX):]
    vis = next(
        (
            member.data
            for member in source_members
            if member.path == f"maps/{stem}.bsp#lump{VISIBILITY_LUMP}"
        ),
        None,
    )
    _require(vis is not None, "prepublication source members omit the VISIBILITY span")
    assert vis is not None

    num_clusters = int(root["numClusters"])
    row_bytes = int(root["rowByteLength"])
    clusters = list(root.get("clusters") or [])

    # `numClusters` is what the whole unit is shaped by, so it is read out of the lump again
    # rather than taken on the writer's word. It may fall short of the declared count only when
    # a byteofs-table-truncated anomaly says how far the table actually reached.
    truncated = [
        entry
        for entry in root.get("anomalies") or []
        if entry.get("role") == "byteofs-table-truncated"
    ]
    if len(vis) >= VIS_HEADER_BYTES:
        declared_clusters = int.from_bytes(vis[:VIS_HEADER_BYTES], "little", signed=True)
        _require(
            num_clusters == declared_clusters
            or any(
                entry.get("declaredClusters") == declared_clusters
                and int(entry.get("clustersRead", -1)) == num_clusters
                for entry in truncated
            ),
            f"numClusters is {num_clusters} where the lump's header declares "
            f"{declared_clusters}, and no byteofs-table-truncated anomaly says why",
        )
    else:
        _require(
            num_clusters == 0,
            "the VISIBILITY span is too short for a header and the unit still declares clusters",
        )

    # The omitted runs are re-hashed against the bytes they claim to be, so a row cannot state
    # an extent or a digest the source does not have.
    for entry in root.get("omissions") or []:
        role = str(entry.get("role"))
        if role == "empty-member":
            _require(
                vis == b"",
                "an empty-member omission is published for a span that holds bytes",
            )
            continue
        start, length = int(entry["sourceOffset"]), int(entry["byteLength"])
        chunk = vis[start:start + length]
        _require(
            len(chunk) == length
            and hashlib.sha256(chunk).hexdigest() == str(entry.get("sha256")),
            f"the {role} omission at {start}+{length} does not describe the bytes it names",
        )

    for index, cluster in enumerate(clusters):
        base = VIS_HEADER_BYTES + index * BYTEOFS_ENTRY_BYTES
        for slot, kind in enumerate(("pvs", "pas")):
            entry = cluster[kind]
            at = base + slot * 4
            declared = int.from_bytes(vis[at:at + 4], "little", signed=True)
            _require(
                int(entry["offset"]) == declared,
                f"clusters[{index}].{kind}.offset disagrees with the source byteofs entry",
            )
            if "compressedLength" not in entry:
                continue
            expected, compressed = _decompress(vis, declared, row_bytes)
            _require(
                compressed == int(entry["compressedLength"]),
                f"clusters[{index}].{kind} declares a compressed length an independent "
                f"decompression does not reach",
            )
            count = cluster["visibleCount" if kind == "pvs" else "audibleCount"]
            if entry.get("emptyRow"):
                _require(
                    expected == b"" and count is None,
                    f"clusters[{index}].{kind} is published empty and an independent "
                    f"decompression produces a row",
                )
                continue
            _require(
                _accessor_bytes(document, binary, int(entry["accessor"])) == expected,
                f"clusters[{index}].{kind}'s accessor disagrees with an independent "
                f"decompression of its row",
            )
            _require(
                count == _count_bits(expected, num_clusters),
                f"clusters[{index}].{kind} count disagrees with an independent decompression",
            )

    if leaf_table is None:
        return
    leaf_block = (root.get("map") or {}).get("leafTable") or {}
    _require(
        len(leaf_table) == int(leaf_block.get("byteLength", -1)),
        "map.leafTable disagrees with the root's leaf lump",
    )
    expected_lists: list[list[int]] = [[] for _ in range(num_clusters)]
    expected_unclustered: list[int] = []
    for leaf in range(len(leaf_table) // LEAF_RECORD_BYTES):
        at = leaf * LEAF_RECORD_BYTES + LEAF_CLUSTER_OFFSET
        cluster = int.from_bytes(leaf_table[at:at + 2], "little", signed=True)
        if cluster < 0:
            expected_unclustered.append(leaf)
        elif cluster < num_clusters:
            expected_lists[cluster].append(leaf)
    _require(
        list(root.get("unclusteredLeaves") or []) == expected_unclustered,
        "unclusteredLeaves disagrees with the root's leaf table",
    )
    for index, cluster in enumerate(clusters):
        _require(
            list(cluster.get("leaves") or []) == expected_lists[index],
            f"clusters[{index}].leaves disagrees with the root's leaf table",
        )


def validate_document(
    document: dict[str, Any],
    binary: bytes,
    *,
    source_members: Sequence[SourceMember] | None = None,
    leaf_table: bytes | None = None,
) -> dict[str, Any]:
    """Every check a published unit answers to; with `source_members`, the export-time ones too."""

    try:
        root = validate_extension_root(
            document,
            MAP_VISIBILITY_EXTENSION,
            asset_prefix=ASSET_PREFIX,
            schema_version=SCHEMA_VERSION,
        )
        validate_sceneless(document)
        validate_container(document, binary)
        validate_accessors(document, binary)
        validate_ledgers(root, source_members)
        reject_opaque_source(document, binary, source_members)
    except MapVisibilityGlbValidationError:
        raise
    except UnitValidationError as error:
        raise MapVisibilityGlbValidationError(str(error)) from error

    stem = _check_identity(root)
    by_lump = _check_members(root, stem)
    _check_map_block(root, by_lump)
    typed = _check_portals(root, by_lump)
    num_clusters, row_bytes = _check_rows(document, binary, root)
    _check_leaves(root)
    _check_ledger_owners(root, by_lump)
    _check_vocabularies(root)
    _check_dependencies(root, stem)

    incomplete = completeness(root)
    if incomplete["unresolved"] or incomplete["unsupported"]:
        raise MapVisibilityGlbValidationError(f"map-visibility unit is incomplete: {incomplete}")

    if source_members is not None:
        _check_against_decode(root, document, binary, source_members, leaf_table)

    ledgers = list((root["coverage"])["byteLedger"])
    return {
        "asset": str(root["identity"]["asset"]),
        "map": stem,
        "numClusters": num_clusters,
        "rowByteLength": row_bytes,
        "rows": len(document.get("accessors") or []),
        "sharedRows": sum(
            1
            for cluster in root.get("clusters") or []
            for kind in ("pvs", "pas")
            if "sharedWith" in cluster[kind]
        ),
        "unclusteredLeaves": len(root.get("unclusteredLeaves") or []),
        "portalLumps": sorted(str(key) for key in (root.get("portals") or {})),
        "typedUnidentified": len(typed),
        "anomalies": [str(row.get("role")) for row in root.get("anomalies") or []],
        "omissions": [str(row.get("role")) for row in root.get("omissions") or []],
        "unresolved": incomplete["unresolved"],
        "unsupported": incomplete["unsupported"],
        "sourceBytes": sum(int(row["byteLength"]) for row in ledgers),
        "accountedBytes": sum(int(row["accountedBytes"]) for row in ledgers),
        "ledgerCoveragePercent": [float(row["coveragePercent"]) for row in ledgers],
        "byteCoveragePercent": (
            min(float(row["coveragePercent"]) for row in ledgers) if ledgers else 100.0
        ),
        "warnings": _contract_warnings_for(root),
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: dict[str, Any]) -> list[str]:
    """What a published unit could not resolve, phrased for the operator."""

    return list(summary.get("warnings") or [])


__all__ = [
    "MapVisibilityGlbValidationError",
    "read_glb",
    "validate",
    "validate_document",
    "warnings_for",
]
