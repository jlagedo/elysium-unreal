"""The shared debug-oracle CLI forwards source-free argv, with emission output intact.

Run the real Typer parser and command action with execution/reporting intercepted:
no process, install read, workspace lease or generated-state write is permitted.
"""

import os
import sys
from types import SimpleNamespace
from unittest.mock import Mock

import pytest
from typer.testing import CliRunner

from elysium_pipeline import cli
from elysium_pipeline.process import ProcessRunner


@pytest.fixture
def oracle_cli(tmp_path, monkeypatch):
    runner = Mock(spec=ProcessRunner)
    config = SimpleNamespace(export_root=tmp_path / "output directory with spaces")

    def execute(state, command, category, action, **kwargs):
        assert command == "debug oracle"
        assert kwargs["require_game"] is True
        action(config, runner)

    monkeypatch.setattr(cli, "_execute", execute)
    return CliRunner(), runner, config


def invoke(fixture, *arguments):
    app_runner, process_runner, config = fixture
    result = app_runner.invoke(cli.app, ["debug", "oracle", *arguments])
    assert result.exit_code == 0, result.output or str(result.exception)
    process_runner.run.assert_called_once()
    args, kwargs = process_runner.run.call_args
    assert kwargs == {"check": True}
    assert "--export-root" not in args[0]
    return args[0], config


def test_run_passes_repeated_reports_without_export_root(oracle_cli):
    argv, _ = invoke(oracle_cli, "--run", "first report.json", "--run", "second.json")
    assert argv == [sys.executable, "-m", "elysium_pipeline.validation.graph_identity",
                    "--run", "first report.json", "--run", "second.json"]


def test_validate_keeps_session_stems_and_aim_search(oracle_cli):
    argv, _ = invoke(oracle_cli, "--validate", "retail capture", "--stem", "body_a",
                     "--stem", "body_b", "--search-aim")
    assert argv == [sys.executable, "-m", "elysium_pipeline.validation.retail_compositor",
                    "--stem", "body_a", "--stem", "body_b",
                    "--validate", "retail capture", "--search-aim"]


def test_validate_preserves_default_capture_bodies(oracle_cli):
    argv, _ = invoke(oracle_cli, "--validate", "retail capture")
    assert argv == [sys.executable, "-m", "elysium_pipeline.validation.retail_compositor",
                    "--stem", "malkavian_female_armor_0", "--stem", "malkavian_male_armor_0",
                    "--validate", "retail capture"]


def test_emit_keeps_output_destination_and_explicit_hosts(oracle_cli):
    argv, config = invoke(oracle_cli, "--emit", "--stem", "body_a",
                          "--host", "walk", "--host", "idle01")
    assert argv == [sys.executable, "-m", "elysium_pipeline.validation.retail_compositor",
                    "--stem", "body_a", "--emit", os.fspath(config.export_root / "_oracle"),
                    "--host", "walk", "--host", "idle01"]
    assert not config.export_root.exists()


def test_emit_preserves_default_bodies_and_hosts(oracle_cli):
    argv, config = invoke(oracle_cli, "--emit")
    assert argv == [sys.executable, "-m", "elysium_pipeline.validation.retail_compositor",
                    "--stem", "malkavian_female_armor_0", "--stem", "malkavian_male_armor_0",
                    "--emit", os.fspath(config.export_root / "_oracle"),
                    "--host", "m37_aggressive_run", "--host", "m37_ready",
                    "--host", "m37_relaxed_run", "--host", "supershotgun_aggressive_run",
                    "--host", "steyr_aggressive_run"]
    assert not config.export_root.exists()


def test_command_rejects_obsolete_export_root_without_starting_a_process(oracle_cli):
    app_runner, runner, _ = oracle_cli
    result = app_runner.invoke(cli.app, ["debug", "oracle", "--run", "capture.json",
                                          "--export-root", "forbidden"])
    assert result.exit_code == 2
    runner.run.assert_not_called()
