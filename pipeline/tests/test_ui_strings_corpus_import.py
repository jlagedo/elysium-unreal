"""Contract tests for `uv run elysium import ui-strings`.

Every fixture is a synthetic ui-resource unit exported into a temporary `export_v2` root from
hand-built bytes and a fake install index -- never the real VtMB install, and never a real
deployed corpus.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest
from typer.testing import CliRunner

from elysium_pipeline import cli
from elysium_pipeline.config import ProjectConfig
from elysium_pipeline.exporters import ui_resource_glb as exporter
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers import ui_strings as importer

RUNNER = CliRunner()

#: The authored string table as the install ships it: UTF-16 LE with a byte-order mark.
STRINGS_TEXT = (
    '"lang"\r\n{\r\n"Language" "English"\r\n"Tokens"\r\n{\r\n'
    '"VMainMenu_BTN_NewGame" "New Game"\r\n'
    '"GameUI_OK" "OK"\r\n}\r\n}\r\n'
)
STRINGS_BODY = b"\xff\xfe" + STRINGS_TEXT.encode("utf-16-le")

#: A layout the port never opens, so the lane must leave it in the units and out of the corpus.
MENU_BODY = (
    b'"GameMenu"\r\n{\r\n\t"1"\r\n\t{\r\n'
    b'\t\t"label" "#GameUI_GameMenu_ResumeGame"\r\n\t\t"command" "ResumeGame"\r\n'
    b"\t}\r\n}\r\n"
)

MEMBERS = {
    "resource/gameui_english.txt": STRINGS_BODY,
    "resource/gamemenu.res": MENU_BODY,
}

DEPLOYED = {"ui/resource/gameui_english.txt": STRINGS_BODY}


def _publish(export_v2_root: Path) -> Path:
    """Export both members into `<export_v2_root>/ui-resources/**.glb`.

    Unlike the script and engine-config seams, this one names its own family directory
    (`formats/ui_resource_glb/model.py::FAMILY_DIR`) inside `output_relative_path`, so `export`
    takes the bare `export_v2` root.
    """

    root = Path(export_v2_root)
    index = {path: ("loose", "C:/game/Vampire/" + path) for path in MEMBERS}
    read = lambda idx, name: MEMBERS.get(name)                       # noqa: E731
    for path in MEMBERS:
        exporter.export(index, path, root, read_bytes=read)
    return root


def _deployed(corpus: Path) -> dict[str, bytes]:
    return {
        path.relative_to(corpus).as_posix(): path.read_bytes()
        for path in sorted(Path(corpus).rglob("*"))
        if path.is_file() and corpus_deploy.BOOKKEEPING_DIRECTORY not in path.parts
    }


# --- the mapping ---------------------------------------------------------------------------------


def test_the_string_table_is_the_one_member_that_lands():
    assert importer.target_of("resource/gameui_english.txt") == (
        "ui/resource/gameui_english.txt",
    )
    # The other 51 units of the family are design intent the port does not open. Declining them
    # is a no-op, not a failure: "the port does not read this" is not a defect in the unit.
    for declined in (
        "resource/gamemenu.res", "resource/vampirescheme.res", "scripts/kb_trans.lst",
        "scripts/dialog_main",
    ):
        assert importer.target_of(declined) == ()


def test_a_member_that_names_no_file_is_a_defect_in_the_unit():
    for refused in ("", "resource/"):
        with pytest.raises(corpus_deploy.CorpusImportError):
            importer.target_of(refused)


# --- the lane ------------------------------------------------------------------------------------


def test_the_table_deploys_verbatim_with_its_bom(tmp_path):
    """The runtime parses the file, so what lands must be the bytes the install encoded."""

    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    result = importer.import_ui_strings(tmp_path / "exports_v2", corpus)

    assert result.failures == []
    assert result.units == 2 and result.written == 1 and result.verified == 1
    assert _deployed(corpus) == DEPLOYED
    landed = (corpus / "ui" / "resource" / "gameui_english.txt").read_bytes()
    assert landed[:2] == b"\xff\xfe"
    assert landed.decode("utf-16-le")[1:] == STRINGS_TEXT


def test_a_second_run_over_one_corpus_rewrites_nothing(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_ui_strings(tmp_path / "exports_v2", corpus)
    stamps = {path: path.stat().st_mtime_ns for path in corpus.rglob("*") if path.is_file()}

    again = importer.import_ui_strings(tmp_path / "exports_v2", corpus)
    assert again.written == 0 and again.units_current == 2 and again.verified == 0
    for path, stamp in stamps.items():
        if corpus_deploy.BOOKKEEPING_DIRECTORY not in path.parts:
            assert path.stat().st_mtime_ns == stamp


def test_a_stale_file_is_overwritten_and_an_orphan_is_pruned(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_ui_strings(tmp_path / "exports_v2", corpus)
    target = corpus / "ui" / "resource" / "gameui_english.txt"
    target.write_bytes(b"stale")
    orphan = corpus / "ui" / "strings.json"
    orphan.write_bytes(b"{}")

    result = importer.import_ui_strings(tmp_path / "exports_v2", corpus)
    assert result.written == 1 and target.read_bytes() == STRINGS_BODY
    # `ui/strings.json` is exactly the derived file the legacy mirror wrote, and the first run of
    # this lane over an old deployment is what sweeps it away.
    assert result.pruned == 1 and not orphan.exists()


def test_an_export_root_with_no_units_says_what_to_run(tmp_path):
    with pytest.raises(corpus_deploy.CorpusImportError, match="ui-resources-glb"):
        importer.import_ui_strings(tmp_path / "exports_v2", tmp_path / "corpus")


def test_the_import_report_names_the_counts(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_ui_strings(tmp_path / "exports_v2", corpus)

    report = json.loads(
        (corpus_deploy.bookkeeping_root(corpus, "ui-strings") / "import_report.json")
        .read_text(encoding="utf-8")
    )
    assert report["lane"] == "ui-strings"
    assert report["units"] == 2 and report["filesWritten"] == 1
    assert report["failures"] == []


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


def test_the_command_deploys_the_table_and_reports_one_line(monkeypatch, tmp_path):
    config = _configured(monkeypatch, tmp_path)
    _publish(tmp_path / "exports_v2")

    result = RUNNER.invoke(cli.app, ["import", "ui-strings"])
    assert result.exit_code == 0, result.output
    assert "ui-strings corpus import:" in result.output
    assert _deployed(corpus_deploy.corpus_root(config.repo_root)) == DEPLOYED


def test_the_command_fails_when_a_unit_cannot_be_imported(monkeypatch, tmp_path):
    _configured(monkeypatch, tmp_path)
    root = _publish(tmp_path / "exports_v2")
    (root / "ui-resources" / "resource" / "gameui_english.txt.glb").write_bytes(b"junk")

    result = RUNNER.invoke(cli.app, ["import", "ui-strings"])
    assert result.exit_code == int(cli.ExitCode.OFFLINE_EXPORT), result.output
    assert "1 ui-strings unit(s) could not be imported" in result.output
