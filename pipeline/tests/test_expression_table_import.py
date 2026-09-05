"""Expression stage mutation gates; writes stay in pytest's explicitly chosen temp root."""
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import re

import pytest

from elysium_pipeline.exporters.expression_table_glb import build_document
from elysium_pipeline.formats.expression_table_glb import decode_expression_table
from elysium_pipeline.formats.expression_table_glb.source import ExpressionTableSourceClosure
from elysium_pipeline.formats.unit_contract import encode_glb
from elysium_pipeline.importers import expression_tables as lane
# Reuse the format lane's independently constructed binary fixture, not its assertions.
from test_expression_table_glb import _build_vfe, _member, TXT


def document(stem="test_phonemes", *, vfe=True, txt=TXT, keys=None, settings=None):
    options = {"path_name": f"expressions/{stem}.vfe"}
    if keys is not None:
        options["keys"] = keys
    if settings is not None:
        options["settings"] = settings
    binary = _build_vfe(**options) if vfe is True else vfe
    closure = ExpressionTableSourceClosure(stem, lane.ASSET_PREFIX + stem,
        _member("vfe", f"expressions/{stem}.vfe", binary) if binary else None,
        _member("txt", f"expressions/{stem}.txt", txt) if txt is not None else None)
    return build_document(decode_expression_table(closure))[0]


def root(doc):
    return doc["extensions"][lane.EXPRESSION_TABLE_EXTENSION]


def project(doc):
    return lane.expression_projection(doc, hashlib.sha256(encode_glb(doc)).hexdigest())


def publish(base, doc):
    path = base / "expression-tables" / (root(doc)["identity"]["stem"] + ".glb")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encode_glb(doc))
    return path


def test_full_document_vfe_values_and_authoring_remain_distinct():
    doc = document()
    root(doc)["futureUnused"] = {"opaque": "00ff", "numbers": [1, 2, 3]}
    root(doc)["table"]["rows"][0]["futureRow"] = "keep"
    projection = project(doc)
    saved = json.loads(projection["sourceDocumentJson"])
    assert saved == doc
    assert root(saved)["table"]["rows"][0]["values"][0] == 0.3499999940395355
    assert root(saved)["authoring"]["rows"][0]["values"][0] == .35
    assert projection["runtimeStatus"] == "ready"
    assert projection["counts"]["runtime"] == {"rows": 2, "controllers": 2, "values": 4, "weights": 4}


def test_vfe_only_and_valid_empty_controller_table():
    doc = document("demal_expressions", txt=None)
    assert project(doc)["runtimeStatus"] == "ready"
    assert root(json.loads(project(doc)["sourceDocumentJson"]))["omissions"] == [{"role": "no-twin", "missingMember": "txt"}]
    empty = document("crooked_cop_expressions", keys=[], settings=[
        {"name": "Neutral", "class_index": 95, "values": []}], txt=None)
    assert project(empty)["counts"]["runtime"] == {"rows": 1, "controllers": 0, "values": 0, "weights": 0}
    assert project(empty)["runtimeStatus"] == "ready"


def test_txt_only_never_becomes_runtime_and_unweighted_data_stays_empty():
    doc = document("scrubs_female_phonemes", vfe=None,
                   txt=b'$keys lip\n"sil" "_" 0.123456789 "desc"\n')
    projection = project(doc)
    assert projection["runtimeStatus"] == "authoring-only"
    assert root(json.loads(projection["sourceDocumentJson"]))["table"] is None
    assert root(doc)["authoring"]["rows"][0]["weights"] == []


def test_markov_settings_and_opaque_bytes_survive_without_a_partial_runtime_table():
    doc = document(txt=None, settings=[
        {"name": "unknown", "class_index": 98, "kind": 1, "count": 2, "raw_values": b"\xab" * 16},
        {"name": "ordinary", "class_index": 109, "values": [(0, .25, .5)]}])
    projection = project(doc)
    assert projection["runtimeStatus"] == "unsupported-vfe"
    saved = root(json.loads(projection["sourceDocumentJson"]))
    assert saved == root(doc)
    assert saved["table"]["rows"][0]["index"] == 1
    assert any(r.get("hex") == "ab" * 16 for r in saved["coverage"]["typedUnidentified"])


@pytest.mark.parametrize("mutation", ["values", "weights", "row-order", "keys-order", "index", "nonfinite", "float-loss", "boolean", "code"])
def test_malformed_runtime_data_fails_without_dropping_rows(mutation):
    doc = document()
    table = root(doc)["table"]
    row = table["rows"][0]
    if mutation in ("values", "weights"):
        row[mutation].pop()
    elif mutation == "row-order":
        table["rows"].reverse()
    elif mutation == "keys-order":
        table["keys"].reverse()
    elif mutation == "index":
        row["index"] = 1
    elif mutation == "nonfinite":
        row["values"][0] = float("nan")
    elif mutation == "float-loss":
        row["values"][0] = .35
    elif mutation == "boolean":
        row["values"][0] = True
    else:
        row["phonemeCode"] = 1.2
    with pytest.raises((ValueError, OverflowError)):
        lane.expression_projection(doc, "a" * 64)


def test_whole_inventory_includes_unreferenced_and_rejects_path_collisions(tmp_path):
    exports, stage = tmp_path / "exports", tmp_path / "stage"
    for name in ("demal_expressions", "phonemes", "phonemes_male"):
        publish(exports, document(name, txt=None))
    manifest = lane.stage_expression_tables(exports, stage)
    assert len(manifest["assets"]) == 3 and len(manifest["keep"]) == 4
    assert lane.verify_expression_stage(stage / "manifest.json")[0] == manifest
    for name in ("a-b", "a_b"):
        publish(exports, document(name, txt=None))
    with pytest.raises(ValueError, match="collision"):
        lane.stage_expression_tables(exports, stage)
    failed = json.loads((stage / "manifest.json").read_bytes())
    assert failed["stageFailures"] and not failed["complete"]


@pytest.mark.parametrize("mutation", ["value", "weight", "authoring", "unknown", "missing-unit", "extra-unit", "path", "digest", "corpus", "keep", "status"])
def test_stage_rechecks_source_and_detects_even_rehashed_mutations(tmp_path, mutation):
    exports, stage = tmp_path / "exports", tmp_path / "stage"
    doc = document()
    root(doc)["unused"] = {"sentinel": 7}
    publish(exports, doc)
    manifest = lane.stage_expression_tables(exports, stage)
    entry = manifest["assets"][0]
    staged_path = stage / entry["file"]
    projection = json.loads(staged_path.read_bytes())
    source = json.loads(projection["sourceDocumentJson"])
    if mutation in ("value", "weight", "authoring", "unknown"):
        if mutation in ("value", "weight"):
            root(source)["table"]["rows"][0]["values" if mutation == "value" else "weights"][0] = .5
        elif mutation == "authoring":
            root(source)["authoring"]["rows"][0]["description"] = "changed"
        else:
            root(source)["unused"]["sentinel"] = 8
        projection["sourceDocumentJson"] = lane.json_text(source)
        staged_path.write_text(lane.json_text(projection), encoding="utf-8")
        entry["sha256"] = hashlib.sha256(staged_path.read_bytes()).hexdigest()
    elif mutation == "missing-unit":
        manifest["assets"] = []
    elif mutation == "extra-unit":
        publish(exports, document("unused_expressions", txt=None))
    elif mutation == "path":
        entry["file"] = "../outside.json"
    elif mutation == "digest":
        entry["sha256"] = "0" * 64
    elif mutation == "corpus":
        corpus_path = stage / manifest["corpus"]["file"]
        corpus = json.loads(corpus_path.read_bytes())
        corpus["tables"] = {}
        corpus_path.write_text(lane.json_text(corpus), encoding="utf-8")
        manifest["corpus"]["sha256"] = hashlib.sha256(corpus_path.read_bytes()).hexdigest()
    elif mutation == "keep":
        manifest["keep"].pop()
    else:
        entry["runtimeStatus"] = "authoring-only"
    (stage / "manifest.json").write_text(lane.json_text(manifest), encoding="utf-8")
    with pytest.raises(ValueError):
        lane.verify_expression_stage(stage / "manifest.json")


def test_selection_preserves_both_fallbacks_and_missing_evidence():
    from elysium_pipeline.formats.model_glb.expressions import selected_tables
    rows = selected_tables("models/missing", {"expressions/phonemes.vfe": object(),
                                             "expressions/phonemes_male.vfe": object()})
    result = lane.expression_selection_projection(rows)
    assert result[1]["primaryAssetId"] == ""
    assert result[1]["fallbackAssetIds"] == [lane.ASSET_PREFIX + "phonemes", lane.ASSET_PREFIX + "phonemes_male"]
    assert json.loads(result[1]["sourceSelectionJson"]) == rows[1]
    bad = deepcopy(rows)
    bad[0]["fallbacks"][1]["asset"] = lane.ASSET_PREFIX + "phonemes"
    with pytest.raises(ValueError, match="identity"):
        lane.expression_selection_projection(bad)


def test_published_corpus_read_only(tmp_path):
    configured = os.environ.get("ELYSIUM_R8_EXPRESSION_CORPUS")
    if not configured:
        pytest.skip("set ELYSIUM_R8_EXPRESSION_CORPUS for read-only published-corpus acceptance")
    projections = lane.inventory_expression_tables(Path(configured))
    by_stem = {p["assetId"].removeprefix(lane.ASSET_PREFIX): p for p in projections}
    assert len(projections) == 250
    assert by_stem["demal_expressions"]["runtimeStatus"] == "ready"
    assert by_stem["scrubs_female_phonemes"]["runtimeStatus"] == "authoring-only"
    for stem in ("phonemes_strong", "phonemes_weak"):
        assert by_stem[stem]["runtimeStatus"] == "undecoded-vfe"
        assert any("hex" in r for r in root(json.loads(by_stem[stem]["sourceDocumentJson"]))["coverage"]["typedUnidentified"])
    assert by_stem["crooked_cop_expressions"]["counts"]["runtime"] == {"rows": 32, "controllers": 0, "values": 0, "weights": 0}
    summary = {"units": len(projections), "statuses": dict(lane.Counter(p["runtimeStatus"] for p in projections)),
               "counts": {field: sum(p["counts"][field] for p in projections)
                          for field in ("typedUnidentified", "unresolved", "unsupported", "omissions", "sourceBytes", "accountedBytes")},
               "runtime": {field: sum(p["counts"]["runtime"][field] for p in projections)
                           for field in ("rows", "controllers", "values", "weights")},
               "authoring": {field: sum(p["counts"]["authoring"][field] for p in projections)
                             for field in ("rows", "controllers", "values", "weights")}}
    inventory = []
    for projection in projections:
        evidence = root(json.loads(projection["sourceDocumentJson"]))
        inventory.append({key: projection[key] for key in
                          ("assetId", "assetPath", "sourceGlbSha256", "runtimeStatus", "counts")}
                         | {"comparison": evidence["comparison"],
                            "unresolved": evidence["coverage"]["unresolved"], "omissions": evidence["omissions"]})
    (tmp_path / "corpus_coverage.json").write_text(
        json.dumps({"summary": summary, "assets": inventory}, indent=2) + "\n", encoding="utf-8")
    print("expression corpus: " + json.dumps(summary, sort_keys=True))


def test_native_adam_fixture_matches_published_row():
    configured = os.environ.get("ELYSIUM_R8_EXPRESSION_CORPUS")
    if not configured:
        pytest.skip("requires the read-only expression corpus")
    native = Path(__file__).parents[2] / "Source/ElysiumUE/Private/Tests/ElysiumExpressionDataTests.cpp"
    text = native.read_text(encoding="utf-8").split("FElysiumExpressionAdamStageTest::RunTest", 1)[1]
    literal = re.search(r'const FString Document = TEXT\(R"JSON\((.*?)\)JSON"\);', text, re.S)
    assert literal, "native regression must retain its literal published-number fixture"
    fixture = root(json.loads(literal.group(1)))
    source, _ = lane.decode_glb((Path(configured) / "expression-tables/adam_expressions.glb").read_bytes())
    expected = root(source)
    assert fixture["identity"]["asset"] == expected["identity"]["asset"]
    assert fixture["table"]["keys"] == expected["table"]["keys"]
    assert fixture["table"]["rows"][0] == expected["table"]["rows"][1]
    assert fixture["authoring"]["rows"][0]["values"][0] == expected["authoring"]["rows"][1]["values"][2]
