"""Synthetic contract tests for the isolated Nav-graph GLB exporter.

The `.ain` grammar is text and whitespace-tokenized, so every fixture here is built by hand as a
CRLF byte string -- never the real install -- with offsets worked out by inspection so the byte
ledger assertions are exact.
"""

from __future__ import annotations

import struct
from pathlib import Path, PurePosixPath

import pytest

from elysium_pipeline.exporters import nav_graph_glb as exporter
from elysium_pipeline.formats.nav_graph_glb import model as nav_model
from elysium_pipeline.formats.nav_graph_glb.decode import NavGraphDecodeError, decode_nav_graph
from elysium_pipeline.formats.nav_graph_glb.source import (
    NavGraphSourceClosure,
    NavGraphSourceError,
    ain_path,
    bsp_path,
    load_source_closure,
    loc_path,
)
from elysium_pipeline.formats.unit_contract import (
    ByteLedgerError,
    Origin,
    SourceMember,
    UnitValidationError,
)
from elysium_pipeline.validation import nav_graph_glb as validation

# Two 2-hull nodes (a fixed origin/yaw/hullOffsets pair, then a variable "tail" plus the fixed
# 2-token "lead"), one 25-token link, and a 2-entry WCLookup -- the smallest instance of every
# stream the grammar declares.
AIN_LINES = [
    "Version\t30",
    "NumHulls:         2",
    "UsedHullBits:     3",
    "ZoneCount:        2",
    "5 6",
    "NumNodes:         2",
    "",
    "Nodes:            1.0,2.0,3.0 90.000000 0.10 0.20 7 8 9",
    "4.0,5.0,6.0 180.000000 0.30 0.40 12 13 14",
    "TotalNumLinks      1",
    "0 1 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0",
    "WCLookup:          100 200",
]
AIN_BYTES = "\r\n".join(AIN_LINES).encode("ascii")
LOC_BYTES = b"424242\r\n"

AIN_KEY = ain_path("fixture")
LOC_KEY = loc_path("fixture")
BSP_KEY = bsp_path("fixture")

LOOSE = Origin(kind="loose", root="Unofficial_Patch")


def _index(*, with_loc: bool = True, with_bsp: bool = True) -> dict:
    index = {AIN_KEY: ("loose", "C:/game/Unofficial_Patch/" + AIN_KEY)}
    if with_loc:
        index[LOC_KEY] = ("loose", "C:/game/Unofficial_Patch/" + LOC_KEY)
    if with_bsp:
        index[BSP_KEY] = ("loose", "C:/game/Unofficial_Patch/" + BSP_KEY)
    return index


def _read_bytes(ain: bytes = AIN_BYTES, loc: bytes | None = LOC_BYTES):
    payloads = {AIN_KEY: ain}
    if loc is not None:
        payloads[LOC_KEY] = loc

    def read(index: dict, key: str) -> bytes | None:
        return payloads.get(key)

    return read


def _closure(
    *,
    with_loc: bool = True,
    with_bsp: bool = True,
    ain: bytes = AIN_BYTES,
    loc: bytes | None = LOC_BYTES,
) -> NavGraphSourceClosure:
    index = _index(with_loc=with_loc, with_bsp=with_bsp)
    return load_source_closure(index, "fixture", read_bytes=_read_bytes(ain=ain, loc=loc))


def _model(**kwargs):
    return decode_nav_graph(_closure(**kwargs))


# --- identity / key rule -----------------------------------------------------------------------


def test_the_asset_id_is_folded_and_namespaced():
    assert nav_model.asset_id("Fixture") == "vtmb:nav-graph:fixture"
    assert nav_model.asset_id("maps/graphs/Fixture.ain") == "vtmb:nav-graph:fixture"


def test_the_key_normalizer_tolerates_the_root_prefix_and_source_extension():
    for spelling in ("fixture", "FIXTURE", "maps/graphs/fixture.ain", "maps\\graphs\\fixture.ain"):
        assert nav_model.normalize_key(spelling) == "fixture"


def test_the_output_path_is_the_bare_key_plus_glb():
    assert nav_model.output_relative_path("maps/graphs/fixture.ain") == PurePosixPath("fixture.glb")


def test_a_key_with_a_path_separator_is_refused():
    with pytest.raises(ValueError):
        nav_model.normalize_key("a/b")


# --- source resolution --------------------------------------------------------------------------


def test_the_source_closure_resolves_both_members_and_the_map_bsp():
    closure = _closure()
    assert closure.ain.path == AIN_KEY and closure.ain.data == AIN_BYTES
    assert closure.loc is not None and closure.loc.data == LOC_BYTES
    assert closure.map_resolved is True
    assert closure.asset_id == "vtmb:nav-graph:fixture"
    assert closure.members() == (closure.ain, closure.loc)


def test_a_missing_ain_is_refused():
    with pytest.raises(NavGraphSourceError):
        load_source_closure({}, "fixture", read_bytes=_read_bytes())


def test_a_missing_loc_is_tolerated_as_an_optional_companion():
    closure = _closure(with_loc=False, loc=None)
    assert closure.loc is None
    assert closure.members() == (closure.ain,)


def test_an_unresolved_map_bsp_is_recorded_without_failing_resolution():
    closure = _closure(with_bsp=False)
    assert closure.map_resolved is False


# --- byte ledger: claimed exactly once, gapless, no zero states ---------------------------------


def test_every_ain_byte_is_claimed_exactly_once():
    model = _model()
    row = model.byte_ledger[0]
    assert row["sourcePath"] == AIN_KEY
    assert row["byteLength"] == len(AIN_BYTES)
    assert row["accountedBytes"] == len(AIN_BYTES)
    assert row["coveragePercent"] == 100.0
    ranges = row["ranges"]
    offsets = [entry["offset"] for entry in ranges]
    assert offsets == sorted(offsets)
    cursor = 0
    for entry in ranges:
        assert entry["offset"] == cursor
        cursor += entry["length"]
    assert cursor == len(AIN_BYTES)


def test_the_ain_ledger_owners_match_the_seams_byte_ledger_table():
    model = _model()
    row = model.byte_ledger[0]
    owners = {entry["owner"] for entry in row["ranges"]}
    expected = {
        "header.version",
        "header.numHulls",
        "header.usedHullBits",
        "header.zoneCount",
        "header.numNodes",
        "header.totalNumLinks",
        "zones",
        "nodes[0]",
        "nodes[1]",
        "nodes.label[0]",
        "links[0]",
        "wcLookup",
        "whitespace",
    }
    assert owners == expected


def test_every_loc_byte_is_claimed_exactly_once():
    model = _model()
    row = model.byte_ledger[1]
    assert row["sourcePath"] == LOC_KEY
    assert row["byteLength"] == len(LOC_BYTES)
    assert row["coveragePercent"] == 100.0
    assert [entry["offset"] for entry in row["ranges"]] == [0, 6]
    assert [entry["owner"] for entry in row["ranges"]] == ["stamp", "stamp.lineEnd"]


def test_the_ledger_carries_no_zero_states():
    """Every owner in the seam's byte-ledger table is `mapped-text`; the grammar is pure text and
    never declares a reserved-zero or padding-zero field."""

    model = _model()
    for row in model.byte_ledger:
        assert set(row["stateBytes"]) <= {"mapped-text"}


# --- dependencies ------------------------------------------------------------------------------


def test_each_dependency_role_is_published_with_its_resolution():
    model = _model()
    roles = {row["role"]: row for row in model.dependencies}
    assert set(roles) == {"map", "map-entities"}
    assert roles["map"]["asset"] == "vtmb:map:fixture"
    assert roles["map"]["sourcePath"] == BSP_KEY
    assert roles["map"]["resolved"] is True
    assert roles["map-entities"]["asset"] == "vtmb:map-entities:fixture"
    assert roles["map-entities"]["resolved"] is True


def test_a_dependency_warns_rather_than_fails_when_the_map_bsp_is_absent():
    model = _model(with_bsp=False)
    assert all(row["resolved"] is False for row in model.dependencies)
    # Absence of the map warns rather than failing the unit: nothing here is `unresolved`.
    assert model.dependencies  # still published, just not resolved
    # An unresolved dependency has to actually surface as an operator-visible warning:
    # `unit_contract.warnings_for` never reads `dependencies[].resolved` on its own, so the
    # decoder names the departure as an anomaly, which it does read.
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    summary = validation.validate_document(document, binary)
    warnings = validation.warnings_for(summary)
    assert any("map/map-entities dependency" in warning for warning in warnings)
    assert any(a["role"] == "unresolved-map-dependency" for a in root["anomalies"])


# --- typedUnidentified rules ---------------------------------------------------------------------


def test_each_node_publishes_a_typed_unidentified_tail_and_lead_row():
    model = _model()
    by_field = {row["field"]: row for row in model.typed_unidentified}
    assert by_field["nodes[0].tail"]["count"] == 1
    assert by_field["nodes[0].lead"]["count"] == 2
    assert by_field["nodes[1].tail"]["count"] == 1
    assert by_field["nodes[1].lead"]["count"] == 2
    assert model.nodes[0].tail == (7,)
    assert model.nodes[0].lead == (8, 9)
    assert model.nodes[1].tail == (12,)
    assert model.nodes[1].lead == (13, 14)


def test_each_link_publishes_a_typed_unidentified_fields_row():
    model = _model()
    row = next(r for r in model.typed_unidentified if r["field"] == "links[0].fields")
    assert row["count"] == 23
    assert model.links[0].fields == (
        0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
    )


def test_the_stamp_is_typed_unidentified_when_the_loc_resolves():
    model = _model()
    row = next(r for r in model.typed_unidentified if r["field"] == "stamp")
    assert row["value"] == 424242
    assert model.stamp is not None and model.stamp.value == 424242


# --- omitted-proven: the missing-loc rule -------------------------------------------------------


def test_a_missing_loc_is_recorded_as_an_omission_not_a_failure():
    model = _model(with_loc=False, loc=None)
    assert model.stamp is None
    assert model.omissions == [
        {"role": "missing-loc", "reason": "no .loc companion resolved for this map"}
    ]
    assert not any(row["field"] == "stamp" for row in model.typed_unidentified)
    assert len(model.byte_ledger) == 1  # only the .ain member, never a mirrored .loc row


def test_a_zero_byte_loc_is_an_empty_member_omission_not_a_malformed_stamp():
    model = _model(loc=b"")
    assert model.stamp is None
    assert model.omissions == [{"role": "empty-member", "sourcePath": LOC_KEY, "byteLength": 0}]
    assert not any(a["role"] == "malformed-loc" for a in model.anomalies)
    row = next(r for r in model.byte_ledger if r["sourcePath"] == LOC_KEY)
    assert row["byteLength"] == 0
    assert row["coveragePercent"] == 100.0
    assert row["ranges"] == []
    document, binary = exporter.build_document(model)
    closure = _closure(loc=b"")
    summary = validation.validate_document(document, binary, source_members=closure.members())
    assert summary["asset"] == "vtmb:nav-graph:fixture"


def test_a_zero_byte_ain_publishes_empty_with_a_warning():
    # The `.ain` is the selecting member; `seam_map_unit_contract.md`'s "Source resolution":
    # "a unit whose selecting member is empty publishes with a warning" -- there is nothing to
    # tokenize, so this must not raise the way a genuinely malformed (non-empty) `.ain` does.
    closure = _closure(ain=b"")
    model = decode_nav_graph(closure)
    assert model.nodes == [] and model.links == []
    assert model.header.num_nodes.value == 0
    assert {"role": "empty-member", "sourcePath": AIN_KEY, "byteLength": 0} in model.omissions
    row = model.byte_ledger[0]
    assert row["sourcePath"] == AIN_KEY and row["byteLength"] == 0 and row["ranges"] == []
    # The `.loc` companion still resolves and decodes on its own, independently of the empty
    # `.ain` beside it.
    assert model.stamp is not None and model.stamp.value == 424242
    document, binary = exporter.build_document(model)
    assert "scenes" not in document and binary == b""
    summary = validation.validate_document(document, binary, source_members=closure.members())
    assert summary["numNodes"] == 0
    assert any("empty-member" in warning for warning in validation.warnings_for(summary))


# --- structural anomalies (still published, never block the unit) -------------------------------


def test_a_version_other_than_30_is_an_anomaly_not_a_failure():
    tampered = AIN_BYTES.replace(b"Version\t30", b"Version\t31")
    model = _model(ain=tampered)
    assert {"role": "version-not-30", "offset": 8, "value": "31"} in model.anomalies


def test_a_node_width_other_than_32_tokens_is_the_documented_anomaly():
    # This fixture's node width is 7 (2 hulls, 1 tail int, 2 lead), which is exactly why the
    # spec's `NumNodes x 32` formula does not generalise past `sp_tutorial_1` -- see the
    # exporter's `specDeviations`.
    model = _model()
    anomaly = next(a for a in model.anomalies if a["role"] == "node-count-mismatch")
    assert anomaly["derivedNodeWidth"] == 7


def test_an_out_of_range_link_index_is_flagged():
    tampered = AIN_BYTES.replace(
        b"0 1 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0",
        b"0 9 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0",
    )
    model = _model(ain=tampered)
    assert any(a["role"] == "link-index-out-of-range" for a in model.anomalies)


def test_an_out_of_range_link_index_still_exports_and_validates_with_a_clamped_geometry():
    # `link-index-out-of-range` is a published anomaly, not a failure: the BIN chunk cannot carry
    # a negative or overrunning unsigned index, so the exporter clamps it into range and the
    # extension keeps the raw (possibly out-of-range) declared value for the record.
    tampered = AIN_BYTES.replace(
        b"0 1 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0",
        b"0 -1 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0",
    )
    closure = _closure(ain=tampered)
    model = decode_nav_graph(closure)
    assert model.links[0].dst == -1
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    assert root["links"][0]["dst"] == -1  # the declared record states the raw decoded value
    summary = validation.validate_document(document, binary, source_members=closure.members())
    assert summary["asset"] == "vtmb:nav-graph:fixture"


def test_leading_garbage_before_version_is_an_unknown_line():
    tampered = b"garbage " + AIN_BYTES
    model = _model(ain=tampered)
    unknown = [a for a in model.anomalies if a["role"] == "unknown-line"]
    assert len(unknown) == 1 and unknown[0]["text"] == "garbage"


def test_unknown_line_owners_stay_unique_across_more_than_one_scan():
    # Two separate stray tokens, each skipped by a different `_expect_label` scan, must not both
    # claim `unknown-line[0]` -- the counter is shared file-wide, not reset per scan.
    tampered = AIN_BYTES.replace(b"NumHulls:         2", b"stray1 NumHulls:         2").replace(
        b"ZoneCount:        2", b"stray2 ZoneCount:        2"
    )
    model = _model(ain=tampered)
    row = model.byte_ledger[0]
    unknown_owners = [entry["owner"] for entry in row["ranges"] if entry["owner"].startswith("unknown-line[")]
    assert len(unknown_owners) == len(set(unknown_owners)) == 2
    assert set(unknown_owners) == {"unknown-line[0]", "unknown-line[1]"}


def test_a_node_region_not_divisible_by_num_nodes_is_an_anomaly_not_a_failure():
    # One stray token among the node data leaves 15 real tokens for 2 nodes: the floor width (7)
    # still matches the fixture's real node width, so both nodes decode correctly and the one
    # leftover token is claimed under its own owner instead of aborting the unit.
    tampered = AIN_BYTES.replace(
        b"4.0,5.0,6.0 180.000000 0.30 0.40 12 13 14",
        b"4.0,5.0,6.0 180.000000 0.30 0.40 12 13 14 99",
    )
    model = _model(ain=tampered)
    anomaly = next(
        a
        for a in model.anomalies
        if a["role"] == "node-count-mismatch" and a.get("reason", "").startswith("node region")
    )
    assert anomaly["remainder"] == 1
    assert anomaly["derivedNodeWidth"] == 7
    assert len(model.nodes) == 2
    assert model.nodes[0].tail == (7,) and model.nodes[1].tail == (12,)
    row = model.byte_ledger[0]
    assert any(entry["owner"] == "nodes.residual[0]" for entry in row["ranges"])
    assert row["coveragePercent"] == 100.0


def test_a_short_link_region_is_an_anomaly_not_a_failure():
    # `TotalNumLinks` declares 2 but only one 25-token link row is present: the decoder publishes
    # the one usable link and claims nothing is left over to claim (the declared count exceeds
    # what is actually there), rather than aborting the unit.
    tampered = AIN_BYTES.replace(b"TotalNumLinks      1", b"TotalNumLinks      2")
    closure = _closure(ain=tampered)
    model = decode_nav_graph(closure)
    anomaly = next(a for a in model.anomalies if a["role"] == "link-count-mismatch")
    assert anomaly["declared"] == 2
    assert len(model.links) == 1
    document, binary = exporter.build_document(model)
    summary = validation.validate_document(document, binary, source_members=closure.members())
    assert summary["asset"] == "vtmb:nav-graph:fixture"


def test_a_long_link_region_claims_its_residual_tokens():
    # `TotalNumLinks` declares 1, but a second, complete 25-token link row follows: the decoder
    # only publishes the declared count and claims the extra row's tokens as residual rather than
    # leaving them for the whitespace catch-all to reject.
    extra_row = b"0 1 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0"
    tampered = AIN_BYTES.replace(
        b"0 1 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0",
        b"0 1 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0\r\n" + extra_row,
    )
    model = _model(ain=tampered)
    anomaly = next(a for a in model.anomalies if a["role"] == "link-count-mismatch" and "declared" in a)
    assert anomaly["declared"] == 1
    assert len(model.links) == 1
    row = model.byte_ledger[0]
    assert any(entry["owner"].startswith("links.residual[") for entry in row["ranges"])
    assert row["coveragePercent"] == 100.0


def test_a_nodes_label_landing_inside_a_node_is_a_named_anomaly_not_a_ledger_crash():
    # A `Nodes:` label that recurs mid-node (rather than at a node boundary) used to make the
    # node's own contiguous-span ledger claim overlap the label's separate claim, aborting the
    # unit with a raw `ByteLedgerError`. Both are still claimed, apart from each other, and the
    # departure is a published anomaly instead.
    tampered = AIN_BYTES.replace(
        b"4.0,5.0,6.0 180.000000 0.30 0.40 12 13 14",
        b"4.0,5.0,6.0 180.000000 Nodes: 0.30 0.40 12 13 14",
    )
    model = _model(ain=tampered)
    assert len(model.nodes) == 2
    assert model.nodes[1].hull_offsets == (0.30, 0.40)
    anomaly = next(
        a
        for a in model.anomalies
        if a["role"] == "node-count-mismatch" and "falls inside" in a.get("reason", "")
    )
    assert anomaly["index"] == 1
    row = model.byte_ledger[0]
    offsets = [entry["offset"] for entry in row["ranges"]]
    assert offsets == sorted(offsets)
    assert row["coveragePercent"] == 100.0
    document, binary = exporter.build_document(model)
    summary = validation.validate_document(document, binary, source_members=_closure(ain=tampered).members())
    assert summary["asset"] == "vtmb:nav-graph:fixture"


def test_a_zone_count_mismatch_is_an_anomaly_not_a_failure():
    tampered = AIN_BYTES.replace(b"ZoneCount:        2", b"ZoneCount:        3")
    model = _model(ain=tampered)
    anomaly = next(a for a in model.anomalies if a["role"] == "zone-count-mismatch")
    assert anomaly["declared"] == 3 and anomaly["actual"] == 2


def test_a_wclookup_count_mismatch_is_an_anomaly_not_a_failure():
    tampered = AIN_BYTES.replace(b"WCLookup:          100 200", b"WCLookup:          100")
    model = _model(ain=tampered)
    anomaly = next(a for a in model.anomalies if a["role"] == "wclookup-count-mismatch")
    assert anomaly["declared"] == 2 and anomaly["actual"] == 1
    # `wcId: 0` has its own meaning ("no entity carries the id"); a node the short table never
    # reached publishes no fabricated value at all, not a fabricated 0.
    assert model.nodes[0].wc_id == 100
    assert model.nodes[1].wc_id is None


def test_a_malformed_node_origin_is_still_a_decode_error():
    # Unlike the departures above, a node origin that is not `x,y,z` at all is not a recoverable
    # departure the seam names -- the record itself cannot be parsed, so this remains fatal.
    tampered = AIN_BYTES.replace(b"1.0,2.0,3.0", b"1.0,2.0")
    with pytest.raises(NavGraphDecodeError):
        _model(ain=tampered)


def test_a_malformed_loc_companion_is_recorded_as_an_anomaly_not_a_failure():
    # `raw` is `None`, never the file's own bytes: a unit never embeds an opaque copy of its
    # source member, whatever the companion's size -- the member's own byteLength/sha256
    # (published in sourceResolution) is what identifies the malformed companion instead.
    model = _model(loc=b"not-a-number\r\n")
    assert model.stamp is not None
    assert model.stamp.value is None
    assert model.stamp.raw is None
    anomaly = next(a for a in model.anomalies if a["role"] == "malformed-loc")
    assert anomaly["reason"] == "not a decimal stamp plus CRLF"
    row = next(r for r in model.byte_ledger if r["sourcePath"] == LOC_KEY)
    assert row["coveragePercent"] == 100.0
    assert [entry["owner"] for entry in row["ranges"]] == ["stamp.raw"]
    assert any(t["field"] == "stamp.raw" for t in model.typed_unidentified)


def test_a_malformed_loc_companion_at_or_past_the_opaque_threshold_still_publishes():
    # The 64-byte `OPAQUE_JSON_MINIMUM` is exactly where a smaller fixture would fail to catch a
    # verbatim copy of the source; an 82-byte malformed companion must publish and validate clean
    # now that `stamp.raw` never restates the member's own bytes.
    malformed = b"not-a-number-but-much-longer-than-the-opaque-json-minimum-threshold!!\r\n"
    assert len(malformed) >= 64
    closure = _closure(loc=malformed)
    model = decode_nav_graph(closure)
    assert model.stamp is not None and model.stamp.raw is None and model.stamp.value is None
    document, binary = exporter.build_document(model)
    summary = validation.validate_document(document, binary, source_members=closure.members())
    assert summary["asset"] == "vtmb:nav-graph:fixture"


# --- zero-node graphs are scene-less --------------------------------------------------------------


EMPTY_AIN = "\r\n".join(
    [
        "Version\t30",
        "NumHulls:         2",
        "UsedHullBits:     1",
        "ZoneCount:        0",
        "",
        "NumNodes:         0",
        "",
        "TotalNumLinks      0",
        "WCLookup:          ",
    ]
).encode("ascii")


def test_a_zero_node_graph_decodes_with_no_nodes_or_links():
    model = _model(ain=EMPTY_AIN)
    assert model.nodes == [] and model.links == []
    assert model.wc_lookup.values == ()
    assert model.byte_ledger[0]["coveragePercent"] == 100.0


def test_a_zero_node_graph_with_a_declared_link_fails_validation_rather_than_publishing_silently():
    # `NumNodes: 0` but `TotalNumLinks: 1` plus one full 25-token link row: the link itself is
    # internally decodable (every endpoint is flagged `link-index-out-of-range` since there is no
    # node 0..N to address), but the scene-less core has no mesh, primitive or index accessor for
    # it to live in -- this must not publish a `links[]` the core cannot back.
    tampered = EMPTY_AIN.replace(
        b"TotalNumLinks      0",
        b"TotalNumLinks      1\r\n0 1 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0",
    )
    model = _model(ain=tampered)
    assert len(model.links) == 1
    document, binary = exporter.build_document(model)
    with pytest.raises(validation.NavGraphGlbValidationError, match="links"):
        validation.validate_document(document, binary)


def test_a_zero_node_graph_publishes_a_scene_less_document():
    model = _model(ain=EMPTY_AIN)
    document, binary = exporter.build_document(model)
    assert binary == b""
    assert "scenes" not in document and "nodes" not in document and "meshes" not in document
    summary = validation.validate_document(document, binary)
    assert summary["numNodes"] == 0


# --- build_document / round trip ------------------------------------------------------------------


def test_the_document_declares_the_extension_and_generator():
    model = _model()
    document, binary = exporter.build_document(model)
    assert document["asset"]["generator"] == "Elysium Nav-graph GLB Exporter"
    assert document["extensionsUsed"] == ["ELYSIUM_vtmb_nav_graph"]
    assert document["extensionsRequired"] == ["ELYSIUM_vtmb_nav_graph"]
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    assert list(root)[:5] == ["schemaVersion", "identity", "sourceResolution", "dependencies", "coverage"]


def test_offset_derived_records_publish_their_source_offset_beside_the_field():
    model = _model()
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    offsets = root["sourceOffsets"]
    assert offsets["header.numHulls"] == {
        "offset": model.header.num_hulls.offset,
        "length": model.header.num_hulls.length,
    }
    assert offsets["zones"] == {"offset": model.zones.offset, "length": model.zones.length}
    assert offsets["wcLookup"] == {"offset": model.wc_lookup.offset, "length": model.wc_lookup.length}
    assert offsets["stamp"] == {"offset": model.stamp.offset, "length": model.stamp.length}


def test_a_node_position_is_not_stated_twice_in_the_extension():
    # `nodes[].origin` carries only the source-space value; the glTF value is recoverable through
    # `coordinateTransform` and is already stated once by the core POSITION accessor and the core
    # node's own `translation` (`seam_map_unit_contract.md`: "the same datum is never stated
    # twice").
    model = _model()
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    for node in root["nodes"]:
        assert set(node["origin"]) == {"source"}


def test_the_scene_carries_one_node_per_graph_node_plus_the_link_mesh():
    model = _model()
    document, binary = exporter.build_document(model)
    assert len(document["nodes"]) == 3  # 2 graph nodes + 1 mesh node
    assert document["nodes"][-1] == {"mesh": 0}
    assert document["meshes"][0]["primitives"][0]["mode"] == 1
    assert len(binary) > 0


def test_export_time_validation_passes_before_the_file_is_written(tmp_path):
    closure = _closure()
    model = decode_nav_graph(closure)
    document, binary = exporter.build_document(model)
    summary = validation.validate_document(document, binary, source_members=closure.members())
    assert summary["asset"] == "vtmb:nav-graph:fixture"
    assert summary["numNodes"] == 2 and summary["totalNumLinks"] == 1


def test_the_exporter_writes_a_file_that_round_trips_through_validate(tmp_path):
    destination = exporter.export(_index(), "fixture", tmp_path, read_bytes=_read_bytes())
    assert destination == tmp_path / "fixture.glb"
    summary = validation.validate(destination)
    assert summary["asset"] == "vtmb:nav-graph:fixture"
    assert summary["numNodes"] == 2


def test_export_is_deterministic(tmp_path):
    one = exporter.export(_index(), "fixture", tmp_path / "a", read_bytes=_read_bytes())
    two = exporter.export(_index(), "fixture", tmp_path / "b", read_bytes=_read_bytes())
    assert one.read_bytes() == two.read_bytes()


def test_source_keys_enumerates_every_ain_under_the_family_directory():
    index = _index()
    index["maps/graphs/other.ain"] = ("loose", "C:/other.ain")
    index["maps/graphs/other.loc"] = ("loose", "C:/other.loc")
    assert exporter.source_keys(index) == ["fixture", "other"]


# --- validator rejection of a tampered ledger ----------------------------------------------------


def test_the_validator_rejects_a_tampered_range_length():
    closure = _closure()
    model = decode_nav_graph(closure)
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    root["coverage"]["byteLedger"][0]["ranges"][0]["length"] += 1
    with pytest.raises(Exception):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_rejects_a_ledger_whose_digest_disagrees_with_its_member():
    closure = _closure()
    model = decode_nav_graph(closure)
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    root["coverage"]["byteLedger"][0]["sourceSha256"] = "0" * 64
    with pytest.raises(Exception):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_rejects_an_index_accessor_value_outside_the_position_range():
    closure = _closure()
    model = decode_nav_graph(closure)
    document, binary = exporter.build_document(model)
    # Tamper the BIN chunk directly -- bypassing the exporter's own clamping entirely -- to prove
    # the standalone validator bounds the line-list primitive's indices on its own, per
    # `seam_map_nav_graph.md`'s "the standalone validator checks ... the line-list primitive's
    # index range".
    tampered_binary = bytearray(binary)
    position_bytes = 2 * 3 * 4  # two nodes' POSITION floats, 3 floats * 4 bytes each
    struct.pack_into("<I", tampered_binary, position_bytes, 99)
    with pytest.raises(validation.NavGraphGlbValidationError, match="POSITION"):
        validation.validate_document(
            document, bytes(tampered_binary), source_members=closure.members()
        )


def test_the_validator_notices_a_declared_field_that_disagrees_with_an_independent_redecode():
    closure = _closure()
    model = decode_nav_graph(closure)
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    root["wcLookup"] = [999, 999]
    with pytest.raises(validation.NavGraphGlbValidationError, match="wcLookup"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_notices_a_tampered_node_origin():
    # Caught by the existing core-geometry cross-check (the BIN chunk still carries the real
    # position), which is the pre-existing protection this pass leaves in place; the new
    # independent re-decode comparison covers the fields geometry does not (hullOffsets, header).
    closure = _closure()
    model = decode_nav_graph(closure)
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    root["nodes"][0]["origin"]["source"] = [999.0, 999.0, 999.0]
    with pytest.raises(validation.NavGraphGlbValidationError, match="disagrees"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_notices_a_tampered_hull_offsets():
    closure = _closure()
    model = decode_nav_graph(closure)
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    root["nodes"][0]["hullOffsets"] = [999.0, 999.0]
    with pytest.raises(validation.NavGraphGlbValidationError, match="hullOffsets"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_notices_a_tampered_header_field():
    closure = _closure()
    model = decode_nav_graph(closure)
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    root["header"]["numHulls"] = 77
    with pytest.raises(validation.NavGraphGlbValidationError, match="numHulls"):
        validation.validate_document(document, binary, source_members=closure.members())
    root["header"]["numHulls"] = model.header.num_hulls.value
    root["header"]["usedHullBits"] = {"value": 123, "bits": []}
    with pytest.raises(validation.NavGraphGlbValidationError, match="usedHullBits"):
        validation.validate_document(document, binary, source_members=closure.members())
    root["header"]["usedHullBits"] = {
        "value": model.header.used_hull_bits.value,
        "bits": [bool((model.header.used_hull_bits.value >> bit) & 1) for bit in range(model.header.num_hulls.value)],
    }
    root["header"]["version"] = 999
    with pytest.raises(validation.NavGraphGlbValidationError, match="version"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_notices_a_tampered_source_offsets_table():
    closure = _closure()
    model = decode_nav_graph(closure)
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    root["sourceOffsets"]["zones"] = {"offset": 12345, "length": 9}
    with pytest.raises(validation.NavGraphGlbValidationError, match="sourceOffsets"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_a_zone_line_with_no_tokens_omits_the_zones_source_offset():
    model = _model(ain=EMPTY_AIN)  # ZoneCount: 0, no zone tokens on the line
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    assert "zones" not in root["sourceOffsets"]
    summary = validation.validate_document(document, binary)
    assert summary["numNodes"] == 0


def test_the_validator_notices_a_tampered_node_source_offset():
    closure = _closure()
    model = decode_nav_graph(closure)
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    root["nodes"][0]["sourceOffset"] = 99999
    with pytest.raises(validation.NavGraphGlbValidationError, match="sourceOffset"):
        validation.validate_document(document, binary, source_members=closure.members())


def test_the_validator_rejects_a_member_capsule_the_unit_never_declared():
    closure = _closure()
    model = decode_nav_graph(closure)
    document, binary = exporter.build_document(model)
    root = document["extensions"]["ELYSIUM_vtmb_nav_graph"]
    root["sourceResolution"]["members"][0]["capsule"] = {"byteLength": 0}
    with pytest.raises(UnitValidationError, match="does not declare"):
        validation.validate_document(document, binary, source_members=closure.members())
