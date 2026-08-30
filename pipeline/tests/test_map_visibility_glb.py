"""Contract tests for the Map-visibility GLB seam.

Every fixture here is synthetic: a BSP built byte by byte in this module and handed to the seam
through a fake index and an injected reader, so nothing in this file touches an installed copy of
the game. What the tests pin is the seam's own promises -- the ledger over each owned span, the
run-length decode, the shared-row rule, the derived leaf lists, the portal lumps carried with
their meaning open, and the validator's refusal of a unit that has been tampered with.
"""

from __future__ import annotations

import hashlib
import struct

import pytest

import elysium_pipeline.formats.map_glb.partition as map_partition
from elysium_pipeline.exporters import map_visibility_glb as exporter
from elysium_pipeline.formats import map_visibility_glb as seam
from elysium_pipeline.formats.unit_contract import ROOT_KEYS, encode_glb, read_glb
from elysium_pipeline.validation import map_visibility_glb as validation

MAP_NAME = "vistestmap"
MAP_KEY = f"maps/{MAP_NAME}.bsp"
LOOSE_PATH = r"C:\game\Unofficial_Patch\maps\vistestmap.bsp"


# --------------------------------------------------------------------------------------------
# Synthetic bytes
# --------------------------------------------------------------------------------------------


def rle(row: bytes) -> bytes:
    """The Quake/Source run-length coding lump 4 stores a bitset row in."""

    out = bytearray()
    cursor = 0
    while cursor < len(row):
        if row[cursor]:
            out.append(row[cursor])
            cursor += 1
            continue
        stop = cursor
        while stop < len(row) and not row[stop]:
            stop += 1
        run = stop - cursor
        while run:
            take = min(run, 255)
            out.extend(b"\0" + bytes([take]))
            run -= take
        cursor = stop
    return bytes(out)


def build_vis(
    rows: list[tuple[bytes, bytes]],
    *,
    shares: dict[tuple[int, int], tuple[int, int]] | None = None,
    overrides: dict[tuple[int, int], int] | None = None,
    trailing: bytes = b"",
) -> bytes:
    """One VISIBILITY lump: the header, the offset table and the coded rows, in cluster order."""

    count = len(rows)
    header = 4 + count * 8
    body = bytearray()
    offsets = [[0, 0] for _ in range(count)]
    shares = shares or {}
    for index in range(count):
        for slot in (0, 1):
            source = shares.get((index, slot))
            if source is not None:
                offsets[index][slot] = offsets[source[0]][source[1]]
                continue
            offsets[index][slot] = header + len(body)
            body.extend(rle(rows[index][slot]))
    for (index, slot), value in (overrides or {}).items():
        offsets[index][slot] = value
    lump = bytearray(struct.pack("<i", count))
    for pair in offsets:
        lump.extend(struct.pack("<2i", pair[0], pair[1]))
    lump.extend(body)
    lump.extend(trailing)
    return bytes(lump)


def leaf_lump(clusters: list[int]) -> bytes:
    """One 32-byte `dleaf_t` per entry, carrying only the `cluster` short this seam reads."""

    out = bytearray()
    for cluster in clusters:
        record = bytearray(b"\0" * seam.LEAF_RECORD_BYTES)
        struct.pack_into("<h", record, 4, cluster)
        out.extend(record)
    return bytes(out)


class BspBuilder:
    """Lay lumps out after the header, remembering the directory rows they need."""

    def __init__(self) -> None:
        self.body = bytearray()
        self.rows: dict[int, tuple[int, int]] = {}

    @property
    def cursor(self) -> int:
        return map_partition.HEADER_BYTES + len(self.body)

    def add(self, index: int, payload: bytes) -> None:
        self.rows[index] = (self.cursor, len(payload))
        self.body.extend(payload)

    def finish(self, *, revision: int = 5) -> bytes:
        header = bytearray(struct.pack("<4si", b"VBSP", 17))
        for index in range(map_partition.LUMP_COUNT):
            offset, length = self.rows.get(index, (0, 0))
            header.extend(struct.pack("<iii4s", offset, length, 0, b"\0\0\0\0"))
        header.extend(struct.pack("<i", revision))
        data = bytes(header) + bytes(self.body)
        trailer = struct.pack("<I", self.rows.get(0, (0, 0))[0])
        return data + trailer + map_partition.TRAILER_SIGNATURE


ROWS_16 = [
    (bytes([0x01 << (index % 8), 0x00]), bytes([0x00, 0x01 << (index % 8)]))
    for index in range(16)
]


def build_bsp(
    *,
    rows: list[tuple[bytes, bytes]] | None = None,
    shares: dict[tuple[int, int], tuple[int, int]] | None = None,
    overrides: dict[tuple[int, int], int] | None = None,
    trailing: bytes = b"",
    leaf_clusters: list[int] | None = None,
    portals: bool = False,
    vis_override: bytes | None = None,
    vis_present: bool = True,
) -> bytes:
    builder = BspBuilder()
    vis = (
        vis_override
        if vis_override is not None
        else build_vis(
            rows if rows is not None else ROWS_16,
            shares=shares,
            overrides=overrides,
            trailing=trailing,
        )
    )
    if vis_present:
        builder.add(4, vis)
    builder.add(
        10,
        leaf_lump(leaf_clusters if leaf_clusters is not None else [-1, 0, 0, 1, -1, 2]),
    )
    if portals:
        builder.add(22, bytes(range(30)))          # 30 bytes: divides by dportal_t's 10
        builder.add(23, bytes(range(20)))          # 20 bytes: does not divide by dcluster_t's 8
        builder.add(24, bytes(range(24)))          # 24 bytes: divides by dportalvert's 12
        builder.add(25, bytes(range(7)))           # 7 bytes: does not divide by 2
    return builder.finish()


def make_index() -> dict:
    return {MAP_KEY: ("loose", LOOSE_PATH)}


def reader(data: bytes):
    return lambda index, key: data if key == MAP_KEY else None


def decoded(data: bytes):
    closure = seam.load_source_closure(make_index(), MAP_NAME, read_bytes=reader(data))
    return closure, seam.decode_map_visibility(closure)


def export_unit(tmp_path, data: bytes):
    return exporter.export(make_index(), MAP_NAME, tmp_path, read_bytes=reader(data))


def extension_of(document) -> dict:
    return document["extensions"][seam.MAP_VISIBILITY_EXTENSION]


def built(data: bytes):
    _closure, model = decoded(data)
    return exporter.build_document(model)


# --------------------------------------------------------------------------------------------
# Identity and the family
# --------------------------------------------------------------------------------------------


def test_the_unit_is_named_by_the_map_stem_and_written_beside_the_root():
    assert seam.asset_id("MAPS/VisTestMap.bsp") == f"vtmb:map-visibility:{MAP_NAME}"
    assert seam.output_relative_path(MAP_NAME).as_posix() == f"maps/{MAP_NAME}.visibility.glb"


def test_the_singular_key_tolerates_the_root_prefix_and_the_source_extension():
    assert seam.normalize_key(MAP_KEY) == MAP_NAME
    assert seam.normalize_key(f"{MAP_NAME}.bsp") == MAP_NAME
    assert seam.normalize_key(MAP_NAME) == MAP_NAME


def test_one_file_carries_one_identity_and_is_written_where_its_key_says(tmp_path):
    destination = export_unit(tmp_path, build_bsp())
    assert destination == tmp_path / "maps" / f"{MAP_NAME}.visibility.glb"
    document, _binary = read_glb(destination)
    assert extension_of(document)["identity"]["asset"] == f"vtmb:map-visibility:{MAP_NAME}"


def test_the_seam_enumerates_every_map_the_index_resolves():
    assert exporter.source_keys(make_index()) == [MAP_NAME]


# --------------------------------------------------------------------------------------------
# The container
# --------------------------------------------------------------------------------------------


def test_the_unit_declares_its_extension_used_and_required_and_names_the_exporter():
    document, _binary = built(build_bsp())
    assert document["extensionsUsed"] == [seam.MAP_VISIBILITY_EXTENSION]
    assert document["extensionsRequired"] == [seam.MAP_VISIBILITY_EXTENSION]
    assert document["asset"]["generator"] == "Elysium Map-visibility GLB Exporter"


def test_the_extension_root_opens_with_the_contracts_keys_in_order():
    document, _binary = built(build_bsp())
    root = extension_of(document)
    assert tuple(list(root)[: len(ROOT_KEYS)]) == ROOT_KEYS
    assert list(root)[len(ROOT_KEYS):] == [
        "map", "numClusters", "rowByteLength", "clusters", "unclusteredLeaves", "portals",
        "anomalies", "omissions",
    ]


def test_the_unit_is_scene_less_and_still_carries_its_bitsets_in_core_gltf():
    document, binary = built(build_bsp())
    for name in ("scenes", "nodes", "meshes", "images", "textures", "samplers"):
        assert name not in document
    assert len(document["accessors"]) == 32
    assert document["buffers"] == [{"byteLength": len(binary)}]


def test_the_written_file_is_a_json_then_bin_container(tmp_path):
    data = export_unit(tmp_path, build_bsp()).read_bytes()
    assert data[:4] == b"glTF"
    assert struct.unpack_from("<I", data, 16)[0] == 0x4E4F534A
    json_length = struct.unpack_from("<I", data, 12)[0]
    assert struct.unpack_from("<I", data, 20 + json_length + 4)[0] == 0x004E4942


def test_the_same_input_produces_the_same_bytes():
    data = build_bsp(portals=True)
    first = encode_glb(*built(data))
    second = encode_glb(*built(data))
    assert first == second


# --------------------------------------------------------------------------------------------
# The visibility decode
# --------------------------------------------------------------------------------------------


def test_every_row_is_one_unsigned_byte_accessor_of_the_declared_row_length():
    document, binary = built(build_bsp())
    root = extension_of(document)
    assert root["numClusters"] == 16 and root["rowByteLength"] == 2
    for accessor in document["accessors"]:
        assert accessor["componentType"] == 5121 and accessor["type"] == "SCALAR"
        assert accessor["count"] == 2
    first = root["clusters"][0]["pvs"]
    start = first["accessor"] * 4
    assert binary[start:start + 2] == ROWS_16[0][0]


def test_a_clusters_counts_are_the_set_bits_of_its_two_rows():
    root = extension_of(built(build_bsp())[0])
    assert root["clusters"][0]["visibleCount"] == 1
    assert root["clusters"][0]["audibleCount"] == 1


def test_the_row_entry_keeps_the_offset_the_source_declared_it_at():
    data = build_bsp()
    _closure, model = decoded(data)
    root = extension_of(exporter.build_document(model)[0])
    for index, cluster in enumerate(root["clusters"]):
        assert cluster["sourceOffset"] == 4 + index * 8
        assert cluster["pvs"]["offset"] >= 4 + 16 * 8


def test_two_clusters_that_name_one_offset_record_that_row_once(tmp_path):
    data = build_bsp(shares={(3, 0): (1, 0), (3, 1): (3, 0)})
    document, _binary = built(data)
    root = extension_of(document)
    assert "accessor" not in root["clusters"][3]["pvs"]
    assert root["clusters"][3]["pvs"]["sharedWith"] == 1
    assert root["clusters"][3]["pvs"]["sharedSet"] == "pvs"
    # Cluster 3's PAS names the same offset again, so it resolves to the entry that recorded the
    # row rather than to the entry that merely repeated it.
    assert root["clusters"][3]["pas"]["sharedWith"] == 1
    assert root["clusters"][3]["pas"]["sharedSet"] == "pvs"
    # Two entries fewer than the 32 an unshared map records, and the counts still answer.
    assert len(document["accessors"]) == 30
    assert root["clusters"][3]["visibleCount"] == root["clusters"][1]["visibleCount"]
    assert validation.validate(export_unit(tmp_path, data))["sharedRows"] == 2


def test_a_leaf_is_derived_to_the_cluster_the_roots_table_names():
    root = extension_of(built(build_bsp())[0])
    assert root["clusters"][0]["leaves"] == [1, 2]
    assert root["clusters"][1]["leaves"] == [3]
    assert root["clusters"][2]["leaves"] == [5]
    assert root["unclusteredLeaves"] == [0, 4]
    assert root["map"]["leafTable"]["count"] == 6


def test_the_leaf_lists_are_graded_derived_rather_than_read_from_this_units_own_spans():
    root = extension_of(built(build_bsp())[0])
    states = {row["field"]: row["state"] for row in root["coverage"]["mapped"]}
    assert states["clusters[].leaves"] == "derived"
    assert states["unclusteredLeaves"] == "derived"
    assert states["rowByteLength"] == "derived"
    # Lump 10 is the map root's; this unit reads it and never claims a byte of it.
    paths = {member["path"] for member in root["sourceResolution"]["members"]}
    assert paths == {f"{MAP_KEY}#lump4"}


# --------------------------------------------------------------------------------------------
# The byte ledger
# --------------------------------------------------------------------------------------------


def test_every_byte_of_every_span_is_claimed_exactly_once():
    root = extension_of(built(build_bsp(portals=True))[0])
    members = {member["path"]: member for member in root["sourceResolution"]["members"]}
    assert len(root["coverage"]["byteLedger"]) == len(members)
    for row in root["coverage"]["byteLedger"]:
        cursor = 0
        for entry in row["ranges"]:
            assert entry["offset"] == cursor
            cursor += entry["length"]
        assert cursor == row["byteLength"] == members[row["sourcePath"]]["byteLength"]
        assert row["accountedBytes"] == cursor and row["coveragePercent"] == 100.0


def test_the_ledger_names_the_owners_the_seam_publishes():
    root = extension_of(built(build_bsp(trailing=b"\xab\xcd"))[0])
    ledger = next(
        row for row in root["coverage"]["byteLedger"] if row["sourcePath"].endswith("#lump4")
    )
    owners = [entry["owner"] for entry in ledger["ranges"]]
    assert owners[0] == "vis.header"
    assert owners[1:3] == ["vis.byteofs[0]", "vis.byteofs[1]"]
    assert "vis.pvs[0]" in owners and "vis.pas[0]" in owners
    assert owners[-1] == "vis.unreferenced[0]"
    states = {entry["owner"]: entry["state"] for entry in ledger["ranges"]}
    assert states["vis.header"] == "mapped"
    assert states["vis.pvs[0]"] == "derived"
    assert states["vis.unreferenced[0]"] == "omitted-proven"


def test_the_seam_claims_no_zero_state_because_lump_four_declares_no_zero_storage():
    root = extension_of(built(build_bsp(portals=True, trailing=b"\0\0"))[0])
    for row in root["coverage"]["byteLedger"]:
        assert not {"reserved-zero", "padding-zero"} & set(row["stateBytes"])
    # Even a run of real zero bytes nobody references is an evidence-backed omission, never
    # padding: the format does not declare it as fill, so the unit does not claim it is.
    ledger = next(
        row for row in root["coverage"]["byteLedger"] if row["sourcePath"].endswith("#lump4")
    )
    assert ledger["ranges"][-1]["state"] == "omitted-proven"


def test_a_portal_lumps_span_is_one_mapped_range_under_its_own_owner():
    root = extension_of(built(build_bsp(portals=True))[0])
    ledger = next(
        row for row in root["coverage"]["byteLedger"] if row["sourcePath"].endswith("#lump23")
    )
    assert [entry["owner"] for entry in ledger["ranges"]] == ["portals.clusters"]
    assert ledger["ranges"][0]["state"] == "mapped"
    assert ledger["byteLength"] == 20


def test_a_run_no_row_reaches_is_omitted_proven_with_its_digest():
    root = extension_of(built(build_bsp(trailing=b"\xab\xcd"))[0])
    row = next(
        entry for entry in root["omissions"] if entry["role"] == "unreferenced-vis-bytes"
    )
    assert row["byteLength"] == 2 and row["owner"] == "vis.unreferenced[0]"
    assert len(row["sha256"]) == 64


def test_the_omitted_proven_grade_summarises_the_omissions_rather_than_copying_them():
    root = extension_of(built(build_bsp(trailing=b"\xab\xcd"))[0])
    # The range itself is stated once, in omissions[]; the coverage grade says how much of the
    # span that role accounts for and points back at the list that holds the ranges.
    assert root["coverage"]["omittedProven"] == [
        {
            "role": "unreferenced-vis-bytes",
            "sourcePath": f"{MAP_KEY}#lump4",
            "runs": 1,
            "byteLength": 2,
            "reason": "unreferenced-vis-bytes: each run is carried in omissions[] with its "
                      "offset, length and SHA-256",
        }
    ]


def test_an_omitted_proven_summary_that_disagrees_with_the_omissions_is_refused():
    document, binary = built(build_bsp(trailing=b"\xab\xcd"))
    extension_of(document)["coverage"]["omittedProven"][0]["runs"] = 2
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary)


def test_an_omission_whose_digest_is_not_the_bytes_it_names_is_refused():
    """The evidence has to be about *these* bytes, or an omission proves nothing."""

    data = build_bsp(trailing=b"\xab\xcd")
    closure, model = decoded(data)
    document, binary = exporter.build_document(model)
    extension_of(document)["omissions"][0]["sha256"] = hashlib.sha256(b"lies").hexdigest()
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(
            document, binary, source_members=closure.members(),
            leaf_table=closure.leaf_bytes(),
        )


def test_an_omitted_range_that_no_omission_row_accounts_for_is_refused():
    document, binary = built(build_bsp(trailing=b"\xab\xcd"))
    extension_of(document)["omissions"][0]["sourceOffset"] = 0
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary)


def test_the_unit_never_embeds_an_opaque_copy_of_a_span(tmp_path):
    data = build_bsp(portals=True)
    _document, binary = built(data)
    closure, _model = decoded(data)
    for member in closure.members():
        assert member.data not in binary
    assert validation.validate(export_unit(tmp_path, data))["byteCoveragePercent"] == 100.0


# --------------------------------------------------------------------------------------------
# The portal graph
# --------------------------------------------------------------------------------------------


def test_a_portal_lump_is_carried_typed_but_unidentified_with_its_divisibility_test():
    root = extension_of(built(build_bsp(portals=True))[0])
    assert sorted(root["portals"]) == ["clusterPortals", "clusters", "portals", "verts"]
    entry = root["portals"]["portals"]
    assert entry["candidate"] == {"struct": "dportal_t", "recordBytes": 10}
    assert entry["divides"] is True and entry["wholeRecords"] == 3 and entry["remainder"] == 0
    assert root["portals"]["clusters"]["divides"] is False
    fields = {row["field"] for row in root["coverage"]["typedUnidentified"]}
    assert fields == {
        "portals.portals", "portals.clusters", "portals.verts", "portals.clusterPortals"
    }


def test_a_portal_entry_names_its_member_and_restates_neither_its_position_nor_its_digest():
    root = extension_of(built(build_bsp(portals=True))[0])
    entry = root["portals"]["portals"]
    assert entry["member"] == f"{MAP_KEY}#lump22"
    assert "offset" not in entry and "sha256" not in entry
    # Both are stated once each, where they belong: in the member row and in the lump table.
    member = next(
        row for row in root["sourceResolution"]["members"] if row["path"] == entry["member"]
    )
    lump = next(row for row in root["map"]["lumps"] if row["lump"] == 22)
    assert lump["offset"] == member["span"]["offset"]
    graded = next(
        row for row in root["coverage"]["typedUnidentified"] if row["field"] == "portals.portals"
    )
    assert graded["sha256"] == member["sha256"]


def test_a_carried_portal_lump_keeps_its_source_offset_identity_in_its_grade():
    """A typedUnidentified value states where it was read from; the grade is where this seam
    says it, so `portals.<key>` never restates a position the member row already carries."""

    root = extension_of(built(build_bsp(portals=True))[0])
    for lump, key in seam.PORTAL_KEYS.items():
        member = next(
            row
            for row in root["sourceResolution"]["members"]
            if row["path"] == f"{MAP_KEY}#lump{lump}"
        )
        graded = next(
            row
            for row in root["coverage"]["typedUnidentified"]
            if row["field"] == f"portals.{key}"
        )
        assert graded["sourceOffset"] == member["span"]["offset"]
        assert graded["byteLength"] == member["byteLength"]


def test_a_portal_grade_that_names_another_position_is_refused():
    document, binary = built(build_bsp(portals=True))
    graded = next(
        row
        for row in extension_of(document)["coverage"]["typedUnidentified"]
        if row["field"] == "portals.portals"
    )
    graded["sourceOffset"] = 0
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary)


def test_the_lump_table_spells_the_directory_row_offset_the_way_the_map_root_does():
    root = extension_of(built(build_bsp(portals=True))[0])
    for row in root["map"]["lumps"]:
        assert "directoryOffset" not in row
        assert row["sourceOffset"] == (
            map_partition.LUMP_DIRECTORY_OFFSET + row["lump"] * map_partition.LUMP_ENTRY_SIZE
        )


def test_a_portal_anomaly_names_the_lump_it_is_about_in_the_operator_warning(tmp_path):
    summary = validation.validate(export_unit(tmp_path, build_bsp(portals=True)))
    spoken = [line for line in validation.warnings_for(summary) if line.startswith("anomaly: ")]
    assert len(spoken) == 2 and len(set(spoken)) == 2
    assert any("lump 23 (CLUSTERS)" in line for line in spoken)
    assert any("lump 25 (CLUSTERPORTALS)" in line for line in spoken)


def test_a_portal_lump_whose_length_does_not_divide_is_an_anomaly_not_a_failure(tmp_path):
    data = build_bsp(portals=True)
    root = extension_of(built(data)[0])
    roles = [row["role"] for row in root["anomalies"]]
    assert roles == ["portal-lump-not-divisible", "portal-lump-not-divisible"]
    assert {row["lump"] for row in root["anomalies"]} == {23, 25}
    summary = validation.validate(export_unit(tmp_path, data))
    assert summary["typedUnidentified"] == 4
    assert summary["portalLumps"] == ["clusterPortals", "clusters", "portals", "verts"]


def test_a_map_without_a_portal_graph_lists_the_four_rows_as_absent_and_is_complete(tmp_path):
    root = extension_of(built(build_bsp())[0])
    assert root["portals"] == {}
    absent = [row for row in root["map"]["lumps"] if not row["present"]]
    assert [row["lump"] for row in absent] == [22, 23, 24, 25]
    assert all(row["length"] == 0 for row in absent)
    summary = validation.validate(export_unit(tmp_path, build_bsp()))
    assert summary["typedUnidentified"] == 0 and summary["warnings"] == []


def test_the_portal_lumps_are_carried_and_are_not_unresolved(tmp_path):
    root = extension_of(built(build_bsp(portals=True))[0])
    assert root["coverage"]["unresolved"] == [] and root["coverage"]["unsupported"] == []
    assert validation.validate(export_unit(tmp_path, build_bsp(portals=True)))


# --------------------------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------------------------


def test_the_one_dependency_is_the_map_root_whose_leaf_table_the_derived_rows_restate():
    data = build_bsp()
    root = extension_of(built(data)[0])
    assert len(root["dependencies"]) == 1
    row = root["dependencies"][0]
    assert row["role"] == "map"
    assert row["asset"] == f"vtmb:map:{MAP_NAME}"
    assert row["sourcePath"] == MAP_KEY
    assert row["resolved"] is True
    assert row["byteLength"] == len(data)


# --------------------------------------------------------------------------------------------
# Non-canonical storage
# --------------------------------------------------------------------------------------------


def test_a_byteofs_entry_outside_the_lump_names_no_row_and_states_no_count(tmp_path):
    data = build_bsp(overrides={(2, 1): 10_000})
    root = extension_of(built(data)[0])
    assert root["clusters"][2]["pas"] == {"offset": 10_000, "outOfRange": True}
    assert root["clusters"][2]["audibleCount"] is None
    anomaly = next(
        row for row in root["anomalies"] if row["role"] == "byteofs-out-of-range"
    )
    assert anomaly["cluster"] == 2 and anomaly["set"] == "pas"
    # The row the entry used to name is now referenced by nobody, and is omitted with evidence.
    assert any(row["role"] == "unreferenced-vis-bytes" for row in root["omissions"])
    assert validation.validate(export_unit(tmp_path, data))["byteCoveragePercent"] == 100.0


def test_a_row_that_runs_past_the_lump_is_published_truncated_with_the_anomaly(tmp_path):
    data = build_bsp(trailing=b"\0")
    vis_length = len(build_vis(ROWS_16, trailing=b"\0"))
    data = build_bsp(trailing=b"\0", overrides={(1, 1): vis_length - 1})
    root = extension_of(built(data)[0])
    roles = {row["role"] for row in root["anomalies"]}
    assert {"pas-row-overrun", "pas-row-short"} <= roles
    entry = root["clusters"][1]["pas"]
    assert entry["compressedLength"] == 1
    # glTF gives `accessor.count` a minimum of one, so a row that decoded to nothing publishes
    # no accessor at all; the entry and its `-row-short` anomaly are what state what happened.
    assert entry == {"offset": vis_length - 1, "compressedLength": 1, "emptyRow": True}
    document, _binary = built(data)
    assert all(accessor["count"] > 0 for accessor in document["accessors"])
    assert len(document["accessors"]) == 31
    # A count of zero would claim that nothing is visible; the source said nothing at all.
    assert root["clusters"][1]["audibleCount"] is None
    assert validation.validate(export_unit(tmp_path, data))["byteCoveragePercent"] == 100.0


def test_a_map_whose_every_row_decodes_to_nothing_still_publishes(tmp_path):
    """The BIN chunk is keyed on the accessors, not the other way round."""

    vis = build_vis([(b"\xff\xff", b"\xff\xff")], overrides={(0, 0): 12, (0, 1): 12})
    vis = vis[:12] + b"\0"                       # one trailing 0x00 both offsets name
    data = build_bsp(vis_override=vis, leaf_clusters=[0, -1])
    document, binary = built(data)
    assert "accessors" not in document and binary == b""
    root = extension_of(document)
    assert root["clusters"][0]["pvs"]["emptyRow"] is True
    assert root["clusters"][0]["visibleCount"] is None
    summary = validation.validate(export_unit(tmp_path, data))
    assert summary["rows"] == 0 and summary["byteCoveragePercent"] == 100.0


def test_an_empty_row_that_claims_an_accessor_anyway_is_refused():
    vis = build_vis([(b"\xff\xff", b"\xff\xff")], overrides={(0, 0): 12, (0, 1): 12})
    data = build_bsp(vis_override=vis[:12] + b"\0", leaf_clusters=[0, -1])
    document, binary = built(data)
    extension_of(document)["clusters"][0]["pvs"]["accessor"] = 0
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary)


def test_two_rows_whose_spans_overlap_pay_once_and_name_the_overlap(tmp_path):
    rows = [(b"\xff\xff", b"\xff\xff") for _ in range(16)]
    header = 4 + 16 * 8
    data = build_bsp(rows=rows, overrides={(1, 0): header + 1})
    root = extension_of(built(data)[0])
    overlap = next(row for row in root["anomalies"] if row["role"] == "row-span-overlap")
    assert overlap["cluster"] == 1 and overlap["set"] == "pvs"
    ledger = next(
        row for row in root["coverage"]["byteLedger"] if row["sourcePath"].endswith("#lump4")
    )
    assert "vis.pvs[1]" not in {entry["owner"] for entry in ledger["ranges"]}
    assert validation.validate(export_unit(tmp_path, data))["byteCoveragePercent"] == 100.0


def test_a_leaf_naming_a_cluster_the_lump_does_not_declare_is_an_anomaly(tmp_path):
    data = build_bsp(leaf_clusters=[-1, 0, 99])
    root = extension_of(built(data)[0])
    anomaly = next(
        row for row in root["anomalies"] if row["role"] == "leaf-cluster-out-of-range"
    )
    assert anomaly["leaf"] == 2 and anomaly["cluster"] == 99
    assert root["unclusteredLeaves"] == [0]
    assert validation.validate(export_unit(tmp_path, data))


def test_a_map_that_stores_no_visibility_lump_publishes_with_an_empty_member(tmp_path):
    """The contract's empty-member path: the unit publishes and warns, it does not disappear."""

    data = build_bsp(vis_present=True, vis_override=b"", leaf_clusters=[-1, -1])
    document, binary = built(data)
    root = extension_of(document)
    assert binary == b"" and "accessors" not in document
    member = root["sourceResolution"]["members"][0]
    assert member["path"] == f"{MAP_KEY}#lump4" and member["byteLength"] == 0
    assert member["sha256"] == hashlib.sha256(b"").hexdigest()
    assert root["numClusters"] == 0 and root["clusters"] == []
    lump4 = root["map"]["lumps"][0]
    assert lump4["present"] is False and lump4["member"] == f"{MAP_KEY}#lump4"
    omission = next(row for row in root["omissions"] if row["role"] == "empty-member")
    assert omission["byteLength"] == 0
    ledger = root["coverage"]["byteLedger"][0]
    assert ledger["byteLength"] == 0 and ledger["coveragePercent"] == 100.0
    summary = validation.validate(export_unit(tmp_path, data))
    assert summary["byteCoveragePercent"] == 100.0 and summary["rows"] == 0
    assert "omitted: empty-member: the map's VISIBILITY lump is unpopulated, so the unit's " \
           "selecting member holds no bytes" in validation.warnings_for(summary)


def test_an_absent_lump_publishes_the_offset_its_directory_row_names(tmp_path):
    """One directory row is stated one way: the lump table and the member's span agree."""

    data = build_bsp(vis_override=b"", leaf_clusters=[-1, -1])
    root = extension_of(built(data)[0])
    lump4 = root["map"]["lumps"][0]
    member = root["sourceResolution"]["members"][0]
    assert lump4["present"] is False and lump4["length"] == 0
    assert lump4["offset"] == member["span"]["offset"] > 0
    assert validation.validate(export_unit(tmp_path, data))["byteCoveragePercent"] == 100.0


def test_an_absent_lump_whose_offset_disagrees_with_its_span_is_refused():
    document, binary = built(build_bsp(vis_override=b"", leaf_clusters=[-1, -1]))
    extension_of(document)["map"]["lumps"][0]["offset"] = 0
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary)


def test_an_empty_member_published_without_its_omission_row_is_refused():
    document, binary = built(build_bsp(vis_override=b"", leaf_clusters=[-1, -1]))
    extension_of(document)["omissions"] = []
    extension_of(document)["coverage"]["omittedProven"] = []
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary)


def test_a_visibility_lump_that_declares_no_offset_table_publishes_what_it_holds(tmp_path):
    data = build_bsp(vis_override=struct.pack("<i", 4) + b"\x01\x02\x03")
    root = extension_of(built(data)[0])
    assert root["numClusters"] == 0 and root["clusters"] == []
    assert any(row["role"] == "byteofs-table-truncated" for row in root["anomalies"])
    assert any(row["role"] == "unreferenced-vis-bytes" for row in root["omissions"])
    assert validation.validate(export_unit(tmp_path, data))["byteCoveragePercent"] == 100.0


# --------------------------------------------------------------------------------------------
# Validation
# --------------------------------------------------------------------------------------------


def test_a_closure_whose_span_is_not_the_extent_its_directory_row_names_is_refused():
    """Every departure the bytes can produce is carried; a self-contradicting closure is not."""

    import dataclasses

    closure, _model = decoded(build_bsp())
    offset, length = closure.visibility.span
    for span, data in (
        ((offset + 1, length), closure.visibility.data),
        ((offset, length - 1), closure.visibility.data[:-1]),
    ):
        contradicted = dataclasses.replace(
            closure,
            visibility=dataclasses.replace(closure.visibility, span=span, data=data),
        )
        with pytest.raises(seam.MapVisibilityDecodeError):
            seam.decode_map_visibility(contradicted)


def test_a_cluster_count_short_of_the_lumps_own_header_needs_its_anomaly(tmp_path):
    """`numClusters` shapes the whole unit, so export-time validation reads it out of the lump
    again: a count below the header's own is only admitted when an anomaly says how far the
    offset table really reached."""

    data = build_bsp(vis_override=struct.pack("<i", 4) + b"\x01\x02\x03")
    closure, model = decoded(data)
    document, binary = exporter.build_document(model)
    root = extension_of(document)
    assert root["numClusters"] == 0
    truncated = next(row for row in root["anomalies"] if row["role"] == "byteofs-table-truncated")
    assert truncated["declaredClusters"] == 4 and truncated["clustersRead"] == 0
    validation.validate_document(
        document, binary, source_members=closure.members(), leaf_table=closure.leaf_bytes()
    )
    root["anomalies"] = [
        row for row in root["anomalies"] if row["role"] != "byteofs-table-truncated"
    ]
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(
            document, binary, source_members=closure.members(),
            leaf_table=closure.leaf_bytes(),
        )


def test_a_key_that_is_not_a_name_below_maps_is_refused(tmp_path):
    with pytest.raises(seam.MapVisibilityKeyError):
        exporter.export(make_index(), "not a map key!", tmp_path, read_bytes=reader(b""))


def test_a_published_unit_round_trips_through_standalone_validation(tmp_path):
    summary = validation.validate(export_unit(tmp_path, build_bsp(portals=True)))
    assert summary["asset"] == f"vtmb:map-visibility:{MAP_NAME}"
    assert summary["numClusters"] == 16 and summary["rows"] == 32
    assert summary["byteCoveragePercent"] == 100.0
    assert summary["sourceBytes"] == summary["accountedBytes"]
    assert validation.warnings_for(summary) == summary["warnings"]


def test_export_time_validation_weighs_the_ledger_against_the_selected_spans(tmp_path):
    data = build_bsp()
    closure, model = decoded(data)
    document, binary = exporter.build_document(model)
    validation.validate_document(
        document, binary, source_members=closure.members(), leaf_table=closure.leaf_bytes()
    )
    other = seam.load_source_closure(
        make_index(), MAP_NAME, read_bytes=reader(build_bsp(trailing=b"\xab\xcd"))
    )
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary, source_members=other.members())


def test_a_tampered_ledger_is_refused_on_read_back(tmp_path):
    destination = export_unit(tmp_path, build_bsp())
    document, binary = read_glb(destination)
    ledger = extension_of(document)["coverage"]["byteLedger"][0]
    ledger["ranges"][2]["state"] = "padding-zero"
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary)


def test_a_ledger_that_drops_a_range_is_refused(tmp_path):
    destination = export_unit(tmp_path, build_bsp())
    document, binary = read_glb(destination)
    ledger = extension_of(document)["coverage"]["byteLedger"][0]
    del ledger["ranges"][3]
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary)


def test_a_false_zero_claim_is_refused_against_the_selected_member(tmp_path):
    data = build_bsp()
    closure, model = decoded(data)
    document, binary = exporter.build_document(model)
    root = extension_of(document)
    ledger = root["coverage"]["byteLedger"][0]
    ledger["ranges"][0]["state"] = "reserved-zero"
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_row_whose_accessor_disagrees_with_the_source_is_refused(tmp_path):
    data = build_bsp()
    closure, model = decoded(data)
    document, binary = exporter.build_document(model)
    tampered = bytearray(binary)
    tampered[0] ^= 0xFF
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(
            document, bytes(tampered), source_members=closure.members(),
            leaf_table=closure.leaf_bytes(),
        )


def test_a_derived_leaf_list_that_disagrees_with_the_root_is_refused(tmp_path):
    data = build_bsp()
    closure, model = decoded(data)
    document, binary = exporter.build_document(model)
    extension_of(document)["clusters"][0]["leaves"] = [1]
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(
            document, binary, source_members=closure.members(),
            leaf_table=closure.leaf_bytes(),
        )


def test_a_shared_row_recorded_twice_is_refused(tmp_path):
    data = build_bsp(shares={(3, 0): (1, 0)})
    document, binary = built(data)
    root = extension_of(document)
    root["clusters"][3]["pvs"] = dict(root["clusters"][1]["pvs"])
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary)


def test_a_unit_that_declares_a_scene_is_refused(tmp_path):
    document, binary = built(build_bsp())
    document["scenes"] = [{"nodes": []}]
    document["nodes"] = [{}]
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary)


def test_a_portal_entry_that_restates_its_divisibility_wrongly_is_refused(tmp_path):
    document, binary = built(build_bsp(portals=True))
    extension_of(document)["portals"]["clusters"]["divides"] = True
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary)


def test_an_anomaly_role_the_seam_never_agreed_to_is_refused(tmp_path):
    document, binary = built(build_bsp())
    extension_of(document)["anomalies"].append({"role": "invented-tolerance"})
    with pytest.raises(validation.MapVisibilityGlbValidationError):
        validation.validate_document(document, binary)
