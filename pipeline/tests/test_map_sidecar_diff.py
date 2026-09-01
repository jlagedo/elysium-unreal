"""The R3.3 differ's pure functions (`pipeline/src/elysium_pipeline/validation/map_sidecar_diff.py`).

`docs/project/seam_migration.md` -> "Roadmap -- one pipeline" R3.3 asks for a byte diff of the
producer's sidecars against the legacy exporter's, plus a structural `.ents` diff, with the two
pre-declared divergences (`la_hub_1`'s corrupted legacy hulls, `.dispcol`'s printed 4th decimal)
recognized by name rather than reported as defects. These tests build small file trees under
`tmp_path` rather than reading the real corpus, so they pin the classification rules themselves.
"""

from __future__ import annotations

import json

from elysium_pipeline.validation.map_sidecar_diff import (
    classify_dispcol,
    diff_map,
    entity_field_diff,
    ents_structural_diff,
    file_diff,
)


def test_file_diff_covers_equal_unequal_and_one_sided_presence(tmp_path):
    a, b = tmp_path / "a.hulls", tmp_path / "b.hulls"
    a.write_bytes(b"1 2 3\n4 5 6\n")
    b.write_bytes(b"1 2 3\n4 5 6\n")
    equal = file_diff(a, b)
    assert equal == {
        "legacyPath": str(a), "producerPath": str(b),
        "legacyPresent": True, "producerPresent": True,
        "legacyBytes": 12, "producerBytes": 12, "equal": True,
    }

    b.write_bytes(b"1 2 3\n9 5 6\n")
    unequal = file_diff(a, b)
    assert unequal["equal"] is False
    assert unequal["firstDiffOffset"] == 6                 # the '4' -> '9' byte
    assert unequal["legacySha256"] != unequal["producerSha256"]

    c = tmp_path / "c.hulls"                                # producer never wrote this one
    one_sided = file_diff(a, c)
    assert one_sided == {
        "legacyPath": str(a), "producerPath": str(c),
        "legacyPresent": True, "producerPresent": False, "equal": False,
    }

    # Neither side writes a `.dispcol` for a map with no displacements -- that is equality, not
    # a missing file worth reporting.
    d, e = tmp_path / "d.dispcol", tmp_path / "e.dispcol"
    assert file_diff(d, e)["equal"] is True


def test_classify_dispcol_accepts_small_deltas_and_rejects_shape_or_large_deltas(tmp_path):
    legacy = tmp_path / "m.dispcol"
    legacy.write_text(
        "0 0 0 1 0 0 0 1 0\n1 1 1 2 1 1 1 2 1\n", encoding="ascii"
    )

    small = tmp_path / "small.dispcol"
    small.write_text(
        "0 0 0 1 0 0 0 1.0019 0\n1 1 1 2 1 1 1 2 1\n", encoding="ascii"
    )
    assert classify_dispcol(file_diff(legacy, small), legacy, small) == "named_divergence"

    large = tmp_path / "large.dispcol"
    large.write_text(
        "0 0 0 1 0 0 0 5.0 0\n1 1 1 2 1 1 1 2 1\n", encoding="ascii"
    )
    assert classify_dispcol(file_diff(legacy, large), legacy, large) == "unexpected"

    shorter = tmp_path / "shorter.dispcol"
    shorter.write_text("0 0 0 1 0 0 0 1 0\n", encoding="ascii")   # one row dropped
    assert classify_dispcol(file_diff(legacy, shorter), legacy, shorter) == "unexpected"


def test_entity_field_diff_reports_added_removed_changed_outputs_and_hull_summaries():
    legacy = {
        "classname": "func_door_rotating", "targetname": "havenrm",
        "origin": [1.0, 2.0, 3.0], "model": 18,
        "hulls": [[0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0]],
        "contents": 1, "outputs": [{"target": "a"}],
    }
    producer = {
        "classname": "func_door_rotating", "targetname": "havenrm",
        "origin": [1.0, 2.0, 3.5], "model": 18,          # origin.z moved
        "hulls": [[0, 0, 0, 1, 0, 0, 0, 0, -5]],           # one vertex dropped and moved
        "outputs": [{"target": "b"}],                     # outputs differ
        # "contents" removed, "blocks_player" added
        "blocks_player": True,
    }

    diff = entity_field_diff(legacy, producer)

    assert diff["added"] == ["blocks_player"]
    assert diff["removed"] == ["contents"]
    assert diff["changed"] == {"origin": {"legacy": [1.0, 2.0, 3.0], "producer": [1.0, 2.0, 3.5]}}
    assert diff["outputs"] == {"legacy": [{"target": "a"}], "producer": [{"target": "b"}]}
    assert diff["hullVertexCounts"] == {"legacy": [4], "producer": [3]}
    assert diff["hullAabb"] == {
        "legacy": [0.0, 0.0, 0.0, 1.0, 1.0, 0.0], "producer": [0.0, 0.0, -5.0, 1.0, 0.0, 0.0],
    }
    # An untouched row -- same fields, same hulls -- reports nothing.
    assert entity_field_diff(legacy, dict(legacy)) == {"added": [], "removed": [], "changed": {}}


def test_ents_structural_diff_aligns_by_index_and_reports_only_in_one_side(tmp_path):
    legacy_path = tmp_path / "m.ents"
    producer_path = tmp_path / "p.ents"
    legacy_path.write_text(json.dumps({"map": "m", "entities": [
        {"classname": "worldspawn"},
        {"classname": "func_door_rotating", "origin": [0.0, 0.0, 0.0]},
    ]}), encoding="ascii")
    producer_path.write_text(json.dumps({"map": "m", "entities": [
        {"classname": "worldspawn"},
        {"classname": "func_door_rotating", "origin": [0.0, 0.0, 1.0]},   # moved
        {"classname": "info_player_start"},                                # producer-only extra row
    ]}), encoding="ascii")

    result = ents_structural_diff(legacy_path, producer_path)

    assert result["legacyEntityCount"] == 2
    assert result["producerEntityCount"] == 3
    assert result["entityDiffCount"] == 2
    assert result["entityDiffs"][0]["index"] == 1
    assert result["entityDiffs"][0]["changed"]["origin"]["producer"] == [0.0, 0.0, 1.0]
    assert result["entityDiffs"][1] == {"index": 2, "onlyIn": "producer"}


def test_diff_map_classifies_byte_equal_named_divergence_and_unexpected(tmp_path):
    legacy_root, producer_root = tmp_path / "legacy", tmp_path / "producer"
    legacy_dir, producer_dir = legacy_root / "clean", producer_root / "_sidecars" / "clean"
    legacy_dir.mkdir(parents=True)
    producer_dir.mkdir(parents=True)
    ents_doc = json.dumps({"map": "clean", "entities": [{"classname": "worldspawn"}]})
    for suffix, text in ((".ents", ents_doc), (".hulls", "1 2 3\n"), (".lights", ""),
                          (".env", "skybox 0\n"), (".sky", ""), (".spawn", ""), (".ropes", "")):
        (legacy_dir / f"clean{suffix}").write_text(text, encoding="ascii")
        (producer_dir / f"clean{suffix}").write_text(text, encoding="ascii")

    clean = diff_map("clean", legacy_root=legacy_root, producer_root=producer_root)
    assert clean["classification"] == "byte_equal"

    # Only `.dispcol` differs, and only in its 4th decimal -- the pre-declared divergence.
    legacy_disp, producer_disp = legacy_root / "disp", producer_root / "_sidecars" / "disp"
    legacy_disp.mkdir(parents=True)
    producer_disp.mkdir(parents=True)
    for suffix, text in ((".ents", json.dumps({"map": "disp", "entities": []})), (".hulls", ""),
                          (".lights", ""), (".env", ""), (".sky", ""), (".spawn", ""), (".ropes", "")):
        (legacy_disp / f"disp{suffix}").write_text(text, encoding="ascii")
        (producer_disp / f"disp{suffix}").write_text(text, encoding="ascii")
    (legacy_disp / "disp.dispcol").write_text("0 0 0 1 0 0 0 1 0\n", encoding="ascii")
    (producer_disp / "disp.dispcol").write_text("0 0 0 1 0 0 0 1.0012 0\n", encoding="ascii")

    named = diff_map("disp", legacy_root=legacy_root, producer_root=producer_root)
    assert named["classification"] == "named_divergence_only"
    assert named["files"][".dispcol"]["classification"] == "named_divergence"

    # `.hulls` differs for a reason not on the pre-declared list -> unexpected.
    legacy_bad, producer_bad = legacy_root / "bad", producer_root / "_sidecars" / "bad"
    legacy_bad.mkdir(parents=True)
    producer_bad.mkdir(parents=True)
    for suffix, text in ((".ents", json.dumps({"map": "bad", "entities": []})), (".lights", ""),
                          (".env", ""), (".sky", ""), (".spawn", ""), (".ropes", "")):
        (legacy_bad / f"bad{suffix}").write_text(text, encoding="ascii")
        (producer_bad / f"bad{suffix}").write_text(text, encoding="ascii")
    (legacy_bad / "bad.hulls").write_text("1 2 3\n", encoding="ascii")
    (producer_bad / "bad.hulls").write_text("9 9 9\n", encoding="ascii")

    unexpected = diff_map("bad", legacy_root=legacy_root, producer_root=producer_root)
    assert unexpected["classification"] == "unexpected_divergence"
    assert unexpected["files"][".hulls"]["classification"] == "unexpected"

    # A pre-declared *map* divergence short-circuits without touching disk at all.
    named_map = diff_map("la_hub_1", legacy_root=tmp_path / "does-not-exist",
                          producer_root=tmp_path / "does-not-exist-either")
    assert named_map["classification"] == "named_divergence_only"
    assert "planenum" in named_map["reason"]
