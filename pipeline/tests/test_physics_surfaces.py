"""Complete parser-input publication: duplicates, ordering and bytes survive deployment."""
import pytest
import json

from elysium_pipeline.exporters import physics_surfaces_glb as exporter
from elysium_pipeline.formats.unit_contract import read_glb, source_capsules, verify_ledger_row
from elysium_pipeline.importers import physics_surfaces as importer
from elysium_pipeline.importers.corpus_deploy import CorpusImportError
from elysium_pipeline.formats.physics_surfaces import decode_semantics


def test_complete_ordered_source_roundtrip(tmp_path, monkeypatch):
    body = (b'// comment\r\ndefault { friction 1D2 impact "Mixed.Name" }\r\n'
            b'x { friction 7 base default impact first impact last }\r\n'
            b'x { impact discarded }\r\n')
    monkeypatch.setattr(exporter.install, "build_index", lambda: {
        exporter.SOURCE: ("loose", "C:/game/Vampire/scripts/surfaceproperties.txt")})
    monkeypatch.setattr(exporter, "read_member", lambda index, key: body)
    path = exporter.export_table(tmp_path / "units")
    document, binary = read_glb(path)
    root = document["extensions"][exporter.EXTENSION]
    assert source_capsules(document, binary, root)[exporter.SOURCE] == body
    ledger = root["coverage"]["byteLedger"][0]
    verify_ledger_row(ledger, body)
    assert ledger["accountedBytes"] == len(body)
    assert [token["text"] for token in root["tokens"] if token["kind"] == "string"].count("x") == 2
    assert any(token["text"] == "1D2" for token in root["tokens"])
    assert any(token["text"] == "Mixed.Name" for token in root["tokens"])
    report = importer.import_physics_surfaces(tmp_path / "units", tmp_path / "corpus")
    assert not report.failures
    assert (tmp_path / "corpus/physics/surfaceproperties.txt").read_bytes() == body
    semantic = json.loads((tmp_path / "corpus/physics/surfaceproperties.semantic.json").read_text())
    assert [row["name"] for row in semantic["rows"]] == ["default", "x", "$MATERIAL_INDEX_SHADOW"]
    assert semantic["rows"][1]["physics"]["friction"] == 100.
    assert semantic["rows"][1]["sounds"]["impact"] == "last"
    assert importer.import_physics_surfaces(tmp_path / "units", tmp_path / "corpus").units_current == 1


def test_missing_source_refuses_publication(tmp_path, monkeypatch):
    monkeypatch.setattr(exporter.install, "build_index", lambda: {})
    monkeypatch.setattr(exporter, "read_member", lambda index, key: None)
    with pytest.raises(ValueError, match="missing scripts/surfaceproperties.txt"):
        exporter.export_table(tmp_path)


def test_deployment_rejects_unowned_members():
    with pytest.raises(CorpusImportError):
        importer.target_of("scripts/other.txt")


def test_authored_numeric_and_lexer_rules():
    data = (b'default { friction .8 density 2 } '
            b'Numbers { /* ignore */ friction 1D+2:ignored elasticity nan thickness inf '
            b'gamematerial 4294967363 climbable -257 '
            b'impact "UPPER:Sound" stepleft "Mix\\"Quote" claw_crit_impact HIT }')
    rows = decode_semantics(data)["rows"]
    assert rows[1]["name"] == "numbers"
    assert rows[1]["physics"]["friction"] == 100.
    assert rows[1]["physics"]["elasticity"] == 0.
    assert rows[1]["physics"]["thickness"] == 0.
    assert rows[1]["gamematerial"] == 67 and rows[1]["climbable"] == 255
    assert rows[1]["sounds"]["impact"] == "upper:sound"
    assert rows[1]["sounds"]["stepleft"] == 'mix"quote'
    assert rows[1]["sounds"]["claw_crit_impact"] == "hit"


def test_default_timing_and_incomplete_entry():
    rows = decode_semantics(b'before { base future density 3 } '
        b'default { density 7 } future { density 10 } '
        b'late { friction 9 base future } broken { density 99')["rows"]
    assert [row["name"] for row in rows] == ["before", "default", "future", "late", "$MATERIAL_INDEX_SHADOW"]
    assert rows[0]["physics"]["density"] == 3.
    assert rows[3]["physics"]["density"] == 10. and rows[3]["physics"]["friction"] == 0.
    assert rows[4]["physics"]["density"] == 7.
