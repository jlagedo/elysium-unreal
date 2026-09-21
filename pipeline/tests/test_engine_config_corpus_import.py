"""Contract tests for `uv run elysium import engine-config`.

Every fixture is a synthetic engine-config unit exported into a temporary `export_v2` root from
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
from elysium_pipeline.exporters import engine_config_glb as exporter
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers import engine_config as importer

RUNNER = CliRunner()

USER_CFG = b'alias patchtype "setPlus()"\r\nbind "w" "+forward"\r\n'
DEFAULT_CFG = b'bind "ESCAPE" "cancelselect"\r\nsetinfo "name" "player"\r\n'
LIGHTS_RAD = b"lights/white 255 255 255 200\r\n"
LOADORDER = b"// the retail launcher's order\r\nsp_tutorial_1\r\n"

#: One console script per subtree the lane keeps, plus two members it declines.
MEMBERS = {
    "cfg/user.cfg": USER_CFG,
    "cfg/default.cfg": DEFAULT_CFG,
    "lights.rad": LIGHTS_RAD,
    "maps/loadorder.txt": LOADORDER,
}

#: What the lane is expected to put in the corpus: the `cfg/` members alone, path unchanged.
DEPLOYED = {
    "cfg/user.cfg": USER_CFG,
    "cfg/default.cfg": DEFAULT_CFG,
}


def _publish(export_v2_root: Path) -> Path:
    """Export every member into `<export_v2_root>/engine-config/**.glb`."""

    root = Path(export_v2_root)
    index = {path: ("loose", "C:/game/Unofficial_Patch/" + path) for path in MEMBERS}
    index["lights/white"] = ("vpk", ("C:/game/Vampire/pack001.vpk", 0, 10))
    index["materials/lights/white.vmt"] = ("vpk", ("C:/game/Vampire/pack001.vpk", 0, 10))
    read = lambda idx, name: MEMBERS.get(name)                       # noqa: E731
    for path in MEMBERS:
        exporter.export(index, path, root / "engine-config", read_bytes=read)
    return root


def _deployed(corpus: Path) -> dict[str, bytes]:
    return {
        path.relative_to(corpus).as_posix(): path.read_bytes()
        for path in sorted(Path(corpus).rglob("*"))
        if path.is_file() and corpus_deploy.BOOKKEEPING_DIRECTORY not in path.parts
    }


# --- the mapping ---------------------------------------------------------------------------------


def test_a_cfg_member_keeps_its_install_relative_path():
    assert importer.target_of("cfg/user.cfg") == ("cfg/user.cfg",)
    assert importer.target_of("cfg/valve.rc") == ("cfg/valve.rc",)
    assert importer.target_of("cfg/dummy.txt") == ("cfg/dummy.txt",)


def test_the_members_with_no_runtime_reader_deploy_nowhere():
    """Compiler tables, packer configuration and the binary state files stay offline."""

    for declined in (
        "lights.rad", "detail.vbsp", "maps/loadorder.txt", "pack_values.txt",
        "localized_list.txt", "vidcfg.bin", "voice_ban.dt", "hl2.tmp",
    ):
        assert importer.target_of(declined) == ()


def test_a_member_that_names_no_file_is_a_defect_in_the_unit():
    for refused in ("", "cfg/", "cfg/sub/"):
        with pytest.raises(corpus_deploy.CorpusImportError):
            importer.target_of(refused)


# --- the lane ------------------------------------------------------------------------------------


def test_only_the_console_scripts_reach_the_corpus(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    result = importer.import_engine_config(tmp_path / "exports_v2", corpus)

    assert result.failures == []
    # Every unit is read; only the two with a runtime reader produce a file.
    assert result.units == 4 and result.written == 2 and result.verified == 2
    assert _deployed(corpus) == DEPLOYED


def test_a_second_run_over_one_corpus_rewrites_nothing(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_engine_config(tmp_path / "exports_v2", corpus)
    stamps = {path: path.stat().st_mtime_ns for path in corpus.rglob("*") if path.is_file()}

    again = importer.import_engine_config(tmp_path / "exports_v2", corpus)
    assert again.written == 0 and again.units_current == 4 and again.verified == 0
    for path, stamp in stamps.items():
        if corpus_deploy.BOOKKEEPING_DIRECTORY not in path.parts:
            assert path.stat().st_mtime_ns == stamp


def test_a_stale_file_is_overwritten_and_an_orphan_is_pruned(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_engine_config(tmp_path / "exports_v2", corpus)
    target = corpus / "cfg" / "user.cfg"
    target.write_bytes(b"stale")
    orphan = corpus / "cfg" / "retired.cfg"
    orphan.write_bytes(b"no longer exported")

    result = importer.import_engine_config(tmp_path / "exports_v2", corpus)
    assert result.written == 1 and target.read_bytes() == USER_CFG
    assert result.pruned == 1 and not orphan.exists()


def test_an_export_root_with_no_units_says_what_to_run(tmp_path):
    with pytest.raises(corpus_deploy.CorpusImportError, match="engine-configs-glb"):
        importer.import_engine_config(tmp_path / "exports_v2", tmp_path / "corpus")


def test_the_import_report_names_the_counts(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_engine_config(tmp_path / "exports_v2", corpus)

    report = json.loads(
        (corpus_deploy.bookkeeping_root(corpus, "engine-config") / "import_report.json")
        .read_text(encoding="utf-8")
    )
    assert report["lane"] == "engine-config"
    assert report["units"] == 4 and report["filesWritten"] == 2
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


def test_the_command_deploys_the_cfg_tree_and_reports_one_line(monkeypatch, tmp_path):
    config = _configured(monkeypatch, tmp_path)
    _publish(tmp_path / "exports_v2")

    result = RUNNER.invoke(cli.app, ["import", "engine-config"])
    assert result.exit_code == 0, result.output
    assert "engine-config corpus import:" in result.output
    assert _deployed(corpus_deploy.corpus_root(config.repo_root)) == DEPLOYED


def test_the_command_fails_when_a_unit_cannot_be_imported(monkeypatch, tmp_path):
    _configured(monkeypatch, tmp_path)
    root = _publish(tmp_path / "exports_v2")
    (root / "engine-config" / "cfg" / "user.cfg.glb").write_bytes(b"junk")

    result = RUNNER.invoke(cli.app, ["import", "engine-config"])
    assert result.exit_code == int(cli.ExitCode.OFFLINE_EXPORT), result.output
    assert "1 engine-config unit(s) could not be imported" in result.output
