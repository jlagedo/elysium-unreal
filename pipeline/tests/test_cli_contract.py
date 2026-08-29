from __future__ import annotations

import unittest

from typer.testing import CliRunner

from elysium_pipeline.cli import _CHILD_SIGNAL, _child_signal, app


class CliContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runner = CliRunner()

    def test_root_help_exposes_the_single_public_command_families(self) -> None:
        result = self.runner.invoke(app, ["--help"])
        assert result.exit_code == 0, result.output
        for command in (
            "reconstruct",
            "deps",
            "doctor",
            "build",
            "export",
            "export_v2",
            "test",
            "run",
            "debug",
            "research",
            "ide",
            "mcp",
        ):
            assert command in result.output

    def test_targeted_map_export_rejects_clean(self) -> None:
        result = self.runner.invoke(
            app, ["export", "map", "sp_tutorial_1", "--clean"]
        )
        assert result.exit_code == 2, result.output
        assert "No such option" in result.output

    def test_no_detached_checkout_family_is_exposed(self) -> None:
        for command in ("lane", "worktree"):
            result = self.runner.invoke(app, [command, "--help"])
            assert result.exit_code == 2, result.output
            assert "No such command" in result.output

    def test_export_help_exposes_wield(self) -> None:
        result = self.runner.invoke(app, ["export", "--help"])
        assert result.exit_code == 0, result.output
        assert "wield" in result.output

    def test_export_v2_exposes_the_isolated_glb_commands(self) -> None:
        result = self.runner.invoke(app, ["export_v2", "--help"])
        assert result.exit_code == 0, result.output
        assert "character-glb" in result.output
        assert "characters-glb" in result.output
        assert "texture-glb" in result.output
        assert "textures-glb" in result.output
        assert "material-glb" in result.output
        assert "materials-glb" in result.output
        assert "surface-property-glb" in result.output
        assert "surface-properties-glb" in result.output
        assert "export-all" in result.output

        old = self.runner.invoke(app, ["export", "--help"])
        assert old.exit_code == 0, old.output
        assert "character-glb" not in old.output


class ChildSignalTests(unittest.TestCase):
    def test_prefilter_agrees_with_the_regex_on_every_vocabulary_shape(self) -> None:
        samples = (
            "LogPython: baked hollywood in 12.3s",
            "Fatal error: rendering thread exception",
            "Assertion failed: Index < Num",
            "LogShaderCompiler: Warning: retrying job",
            "LogInit: Error: missing module",
            "Error: cook failed",
            "  Error: indented, so not anchored",
            "SomeError: not the anchored form",
            "Warning: at line start without the colon prefix",
            "LogStreaming: Display: loaded package",
            "plain engine chatter line",
            "",
        )
        for line in samples:
            assert _child_signal(line) == (_CHILD_SIGNAL.search(line) is not None), line
