"""Contract tests for the dialogue capsule and `uv run elysium import dialogue`.

Every fixture is a synthetic `.dlg`/`.vcd` exported into a temporary `export_v2` root from
hand-built bytes and a fake install index -- never the real VtMB install, and never a real
deployed corpus. The one test that reads the machine's own trees is the legacy-parity check at
the bottom, and it skips itself when either tree is absent.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path

import pytest
from typer.testing import CliRunner

from elysium_pipeline import cli
from elysium_pipeline.config import ProjectConfig
from elysium_pipeline.exporters import dialogue_glb as dialogue_exporter
from elysium_pipeline.exporters import scene_glb as scene_exporter
from elysium_pipeline.formats.dialogue_glb.model import DIALOGUE_EXTENSION
from elysium_pipeline.formats.scene_glb.model import SCENE_EXTENSION
from elysium_pipeline.formats.unit_contract import encode_glb, read_glb
from elysium_pipeline.formats.unit_contract.validate import UnitValidationError
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers import dialogue as importer

RUNNER = CliRunner()


def _row(id_: str, male: str, link: str = "#") -> bytes:
    cells = [id_, male, "", link] + [""] * 9
    return b"".join(b"{\t" + cell.encode("latin-1") + b"\t}" for cell in cells) + b"\r\n"


#: Two conversations, spelled the way the shipped files are: latin-1 above 0x7f, CRLF rows.
DLG_BODIES = {
    "main characters/jack_tutorial": _row("1", "Welcome to the fold.") + _row("2", "Caf\xe9?"),
    "chinatown/barabus": _row("10", "Speak."),
}


def _scene(actor: str, expression: str) -> bytes:
    """One well-formed Faceposer `.vcd`: one actor, one channel, one expression event."""

    lines = [
        "// Choreo version 1",
        f'actor "{actor}"',
        "{",
        '  channel "face"',
        "  {",
        f'    event expression "{expression}"',
        "    {",
        "      time 0.000000 2.000000",
        '      param "smiling_jack_expressions"',
        f'      param2 "{expression}"',
        "    }",
        "  }",
        "}",
        "",
        "fps 60",
        "snap off",
        "",
    ]
    # The shipped corpus is CRLF throughout, and the ledger claims those two bytes per line.
    return "\r\n".join(lines).encode("latin-1")


#: Two scenes, in the `sound/`-rooted key space the seam publishes.
VCD_BODIES = {
    "character/dlg/jack/line1": _scene("Jack", "Snarl"),
    "character/boss/andrei/andrei": _scene("Andrei", "Angry"),
}


def _publish(export_v2_root: Path) -> Path:
    """Export every fixture into `<export_v2_root>/{dialogues,scenes}/**.glb`."""

    root = Path(export_v2_root)
    index: dict[str, tuple[str, str]] = {}
    lookup: dict[str, bytes] = {}
    for key, body in DLG_BODIES.items():
        index[f"dlg/{key}.dlg"] = ("loose", f"C:/game/Vampire/dlg/{key}.dlg")
        lookup[f"dlg/{key}.dlg"] = body
    for key, body in VCD_BODIES.items():
        index[f"sound/{key}.vcd"] = ("loose", f"C:/game/Vampire/sound/{key}.vcd")
        lookup[f"sound/{key}.vcd"] = body
    read = lambda idx, name: lookup.get(name)                       # noqa: E731
    for key in DLG_BODIES:
        dialogue_exporter.export(index, key, root / "dialogues", read_bytes=read)
    for key in VCD_BODIES:
        scene_exporter.export(index, key, root / "scenes", read_bytes=read)
    return root


def _deployed(corpus: Path, *directories: str) -> dict[str, bytes]:
    files: dict[str, bytes] = {}
    for directory in directories:
        root = Path(corpus) / directory
        for path in sorted(root.rglob("*")):
            if path.is_file():
                files[path.relative_to(corpus).as_posix()] = path.read_bytes()
    return files


EXPECTED = {
    **{f"dlg/{key}.dlg": body for key, body in DLG_BODIES.items()},
    **{f"scenes/{key}.vcd": body for key, body in VCD_BODIES.items()},
}


# --- the capsule upgrade -------------------------------------------------------------------------


def test_a_dialogue_unit_carries_its_dlg_bytes_and_declares_the_capsule(tmp_path):
    _publish(tmp_path)
    unit = tmp_path / "dialogues" / "chinatown" / "barabus.glb"
    document, binary = read_glb(unit)
    root = document["extensions"][DIALOGUE_EXTENSION]
    body = DLG_BODIES["chinatown/barabus"]

    assert root["schemaVersion"] == "1.1.0"
    assert root["sourceResolution"]["capsule"] == {"encoding": "raw"}
    member = root["sourceResolution"]["members"][0]
    assert member["path"] == "dlg/chinatown/barabus.dlg"
    assert member["byteLength"] == len(body)
    assert member["sha256"] == hashlib.sha256(body).hexdigest()
    assert member["capsule"] == {"bufferView": 0, "byteLength": len(body)}
    view = document["bufferViews"][0]
    assert binary[view["byteOffset"]:view["byteOffset"] + view["byteLength"]] == body
    assert "accessors" not in document


def test_a_scene_unit_carries_its_vcd_bytes_and_declares_the_capsule(tmp_path):
    _publish(tmp_path)
    unit = tmp_path / "scenes" / "character" / "dlg" / "jack" / "line1.glb"
    document, binary = read_glb(unit)
    root = document["extensions"][SCENE_EXTENSION]
    body = VCD_BODIES["character/dlg/jack/line1"]

    assert root["schemaVersion"] == "1.1.0"
    assert root["sourceResolution"]["capsule"] == {"encoding": "raw"}
    member = root["sourceResolution"]["members"][0]
    assert member["path"] == "sound/character/dlg/jack/line1.vcd"
    assert member["sha256"] == hashlib.sha256(body).hexdigest()
    view = document["bufferViews"][member["capsule"]["bufferView"]]
    assert binary[view["byteOffset"]:view["byteOffset"] + view["byteLength"]] == body


@pytest.mark.parametrize("family, extension, key", [
    ("dialogues", DIALOGUE_EXTENSION, "chinatown/barabus"),
    ("scenes", SCENE_EXTENSION, "character/dlg/jack/line1"),
])
def test_the_decode_is_unchanged_by_the_capsule(tmp_path, family, extension, key):
    """The capsule is added *beside* the decode; every semantic key it published still validates."""

    _publish(tmp_path)
    unit = tmp_path / family / Path(key + ".glb")
    document, binary = read_glb(unit)
    if family == "dialogues":
        from elysium_pipeline.validation import dialogue_glb as validation
    else:
        from elysium_pipeline.validation import scene_glb as validation
    validation.validate_document(document, binary)
    root = document["extensions"][extension]
    assert root["coverage"]["byteLedger"][0]["coveragePercent"] == 100.0


@pytest.mark.parametrize("family, extension", [
    ("dialogues", DIALOGUE_EXTENSION), ("scenes", SCENE_EXTENSION),
])
def test_a_unit_that_drops_its_capsule_declaration_is_refused(tmp_path, family, extension):
    _publish(tmp_path)
    unit = next((tmp_path / family).rglob("*.glb"))
    document, binary = read_glb(unit)
    root = document["extensions"][extension]
    root["sourceResolution"].pop("capsule")
    root["sourceResolution"]["members"][0].pop("capsule")
    if family == "dialogues":
        from elysium_pipeline.validation import dialogue_glb as validation
        error = validation.DialogueGlbValidationError
    else:
        from elysium_pipeline.validation import scene_glb as validation
        error = validation.SceneGlbValidationError
    with pytest.raises(error, match="capsule"):
        validation.validate_document(document, binary)


@pytest.mark.parametrize("family, extension", [
    ("dialogues", DIALOGUE_EXTENSION), ("scenes", SCENE_EXTENSION),
])
def test_a_capsule_whose_bytes_were_tampered_with_fails_the_hash(tmp_path, family, extension):
    _publish(tmp_path)
    unit = next((tmp_path / family).rglob("*.glb"))
    document, binary = read_glb(unit)
    if family == "dialogues":
        from elysium_pipeline.validation import dialogue_glb as validation
        error = validation.DialogueGlbValidationError
    else:
        from elysium_pipeline.validation import scene_glb as validation
        # The scene validator lets the contract's own error out unwrapped; the dialogue one
        # re-raises it as its seam's type. Either way the tampered capsule is refused.
        error = UnitValidationError
    with pytest.raises(error, match="capsule"):
        validation.validate_document(document, b"X" + binary[1:])


# --- the lane ------------------------------------------------------------------------------------


def test_every_unit_deploys_to_the_path_the_runtime_reads(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    result = importer.import_dialogue(tmp_path / "exports_v2", corpus)

    assert result.failures == []
    assert result.units == len(DLG_BODIES) + len(VCD_BODIES)
    assert result.written == len(EXPECTED)
    assert result.verified == len(EXPECTED)
    assert _deployed(corpus, "dlg", "scenes") == EXPECTED


def test_a_scene_loses_the_sound_prefix_the_runtime_strips(tmp_path):
    """`ScenesDir()` is `scenes/` and `NormalizeSceneRel` chops `sound/`, so the lane chops it."""

    assert importer.target_of("sound/character/dlg/jack/line1.vcd") == \
        ("scenes/character/dlg/jack/line1.vcd",)
    assert importer.target_of("dlg/chinatown/barabus.dlg") == ("dlg/chinatown/barabus.dlg",)
    with pytest.raises(corpus_deploy.CorpusImportError):
        importer.target_of("sound/character/dlg/jack/line1.wav")


def test_a_second_run_over_one_corpus_rewrites_nothing(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_dialogue(tmp_path / "exports_v2", corpus)
    stamps = {
        path: path.stat().st_mtime_ns
        for path in list(corpus.rglob("*.dlg")) + list(corpus.rglob("*.vcd"))
    }

    again = importer.import_dialogue(tmp_path / "exports_v2", corpus)
    assert again.written == 0
    assert again.units_current == len(DLG_BODIES) + len(VCD_BODIES)
    assert again.unchanged == len(EXPECTED)
    assert {
        path: path.stat().st_mtime_ns
        for path in list(corpus.rglob("*.dlg")) + list(corpus.rglob("*.vcd"))
    } == stamps


def test_a_recipe_version_bump_re_deploys_every_unit(tmp_path, monkeypatch):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_dialogue(tmp_path / "exports_v2", corpus)

    bumped = importer.LANE_SPEC.__class__(
        **{**{field: getattr(importer.LANE_SPEC, field)
              for field in importer.LANE_SPEC.__slots__},
           "recipe_version": "elysium-dialogue-corpus-vNEXT"}
    )
    again = corpus_deploy.deploy_lane(tmp_path / "exports_v2", corpus, bumped)
    # The stamp is discarded whole, so every unit is opened again -- and every file is still
    # byte-identical, so nothing is rewritten.
    assert again.units_current == 0
    assert again.written == 0 and again.unchanged == len(EXPECTED)


def test_a_stale_file_is_overwritten_with_what_the_unit_carries(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_dialogue(tmp_path / "exports_v2", corpus)
    target = corpus / "dlg" / "chinatown" / "barabus.dlg"
    target.write_bytes(b"stale")

    result = importer.import_dialogue(tmp_path / "exports_v2", corpus)
    assert result.written == 1
    assert target.read_bytes() == DLG_BODIES["chinatown/barabus"]


def test_a_stale_orphan_is_pruned(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    orphan = corpus / "scenes" / "character" / "retired.vcd"
    orphan.parent.mkdir(parents=True)
    orphan.write_bytes(b"no longer exported")

    result = importer.import_dialogue(tmp_path / "exports_v2", corpus)
    assert result.pruned == 1
    assert not orphan.exists()


def test_a_unit_from_before_the_capsule_is_one_failure_and_not_a_crash(tmp_path):
    root = _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"

    stale = root / "dialogues" / "chinatown" / "barabus.glb"
    document, _binary = read_glb(stale)
    dialogue_root = document["extensions"][DIALOGUE_EXTENSION]
    dialogue_root["schemaVersion"] = "1.0.0"
    dialogue_root["sourceResolution"].pop("capsule")
    dialogue_root["sourceResolution"]["members"][0].pop("capsule")
    document.pop("buffers")
    document.pop("bufferViews")
    stale.write_bytes(encode_glb(document, b""))

    result = importer.import_dialogue(tmp_path / "exports_v2", corpus)
    assert [key for key, _ in result.failures] == ["dialogues/chinatown/barabus"]
    assert "capsule" in result.failures[0][1]
    assert not (corpus / "dlg" / "chinatown" / "barabus.dlg").exists()
    assert result.written == len(EXPECTED) - 1                     # every other unit deployed


def test_a_failed_units_previously_deployed_file_is_kept_not_pruned(tmp_path):
    root = _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_dialogue(tmp_path / "exports_v2", corpus)

    (root / "dialogues" / "chinatown" / "barabus.glb").write_bytes(b"not a glb at all")
    result = importer.import_dialogue(tmp_path / "exports_v2", corpus)

    assert len(result.failures) == 1
    assert result.pruned == 0
    good = corpus / "dlg" / "chinatown" / "barabus.dlg"
    assert good.read_bytes() == DLG_BODIES["chinatown/barabus"]


def test_the_import_report_names_the_counts(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_dialogue(tmp_path / "exports_v2", corpus)

    report = json.loads(
        (corpus_deploy.bookkeeping_root(corpus, "dialogue") / "import_report.json")
        .read_text(encoding="utf-8")
    )
    assert report["lane"] == "dialogue"
    assert report["units"] == len(DLG_BODIES) + len(VCD_BODIES)
    assert report["filesWritten"] == len(EXPECTED)
    assert report["filesVerified"] == len(EXPECTED)
    assert report["failures"] == []


def test_an_export_root_with_no_units_says_what_to_run(tmp_path):
    with pytest.raises(corpus_deploy.CorpusImportError, match="dialogues-glb"):
        importer.import_dialogue(tmp_path / "exports_v2", tmp_path / "corpus")


def test_a_member_path_that_climbs_out_of_the_corpus_is_refused(tmp_path):
    for evil in ("dlg/../../evil.dlg", "dlg/C:/evil.dlg", "dlg\\evil.dlg", ""):
        with pytest.raises(corpus_deploy.CorpusImportError):
            corpus_deploy.safe_destination(tmp_path, evil)


# --- the command ---------------------------------------------------------------------------------


def _configured(monkeypatch, tmp_path) -> ProjectConfig:
    work = tmp_path / "work"
    (work / "logs").mkdir(parents=True)
    repo = tmp_path / "repo"
    repo.mkdir()
    config = ProjectConfig(
        repo_root=repo,
        project=repo / "ElysiumUE.uproject",
        game_root=None,
        work_root=work,
        export_root=work / "exports",
        export_v2_root=tmp_path / "exports_v2",
        ue_root=None,
        unreal_zen_data_path=None,
        unreal_local_data_cache_path=None,
        unreal_shader_work_root=None,
        temp_root=None,
    )
    monkeypatch.setattr(cli.CliState, "resolve", lambda self, **kwargs: config)
    return config


def test_the_command_deploys_both_trees_and_reports_one_line(monkeypatch, tmp_path):
    config = _configured(monkeypatch, tmp_path)
    _publish(tmp_path / "exports_v2")

    result = RUNNER.invoke(cli.app, ["import", "dialogue"])
    assert result.exit_code == 0, result.output
    assert "dialogue corpus import:" in result.output
    corpus = corpus_deploy.corpus_root(config.repo_root)
    assert _deployed(corpus, "dlg", "scenes") == EXPECTED


def test_the_command_fails_when_a_unit_cannot_be_imported(monkeypatch, tmp_path):
    _configured(monkeypatch, tmp_path)
    root = _publish(tmp_path / "exports_v2")
    (root / "scenes" / "character" / "dlg" / "jack" / "line1.glb").write_bytes(b"junk")

    result = RUNNER.invoke(cli.app, ["import", "dialogue"])
    assert result.exit_code == int(cli.ExitCode.OFFLINE_EXPORT), result.output
    assert "1 dialogue unit(s) could not be imported" in result.output


# --- parity with the legacy mirror ---------------------------------------------------------------


def _legacy_root() -> Path | None:
    raw = os.environ.get("ELYSIUM_EXPORT_ROOT", "").strip()
    if not raw:
        work = os.environ.get("ELYSIUM_WORK_ROOT", "").strip()
        raw = str(Path(work) / "exports") if work else ""
    root = Path(raw).expanduser() if raw else None
    return root if root and root.is_dir() else None


@pytest.mark.parametrize("directory, suffix", [("dlg", ".dlg"), ("scenes", ".vcd")])
def test_the_deployed_tree_matches_the_legacy_mirror_byte_for_byte(directory, suffix):
    """The corpus tree is the legacy tree: same relative paths, same bytes.

    Skipped unless both this machine's legacy export and its deployed corpus exist -- it reads
    what the operator's own `uv run elysium import dialogue` produced, not a fixture.
    """

    legacy_root = _legacy_root()
    corpus = Path(__file__).resolve().parents[2] / "Content" / "ElysiumCorpus" / directory
    if legacy_root is None or not (legacy_root / directory).is_dir() or not corpus.is_dir():
        pytest.skip("the legacy export or the deployed corpus is absent on this machine")

    legacy = {
        path.relative_to(legacy_root / directory).as_posix().lower(): path
        for path in (legacy_root / directory).rglob("*" + suffix)
    }
    deployed = {
        path.relative_to(corpus).as_posix().lower(): path for path in corpus.rglob("*" + suffix)
    }
    assert set(deployed) == set(legacy)
    for key, path in deployed.items():
        assert path.read_bytes() == legacy[key].read_bytes(), key
