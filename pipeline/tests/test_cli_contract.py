from __future__ import annotations

import unittest

from typer.testing import CliRunner

from elysium_pipeline.cli import _CHILD_SIGNAL, _child_signal, app


class CliContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runner = CliRunner()

    def test_root_help_exposes_the_single_public_command_families(self) -> None:
        result = self.runner.invoke(app, ["--help"])
        self.assertEqual(result.exit_code, 0, result.output)
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
            "worktree",
            "mcp",
        ):
            self.assertIn(command, result.output)

    def test_targeted_map_export_rejects_clean(self) -> None:
        result = self.runner.invoke(
            app, ["export", "map", "sp_tutorial_1", "--clean"]
        )
        self.assertEqual(result.exit_code, 2, result.output)
        self.assertIn("No such option", result.output)

    def test_the_task_worktree_is_the_only_detached_checkout_family(self) -> None:
        result = self.runner.invoke(app, ["lane", "--help"])
        self.assertEqual(result.exit_code, 2, result.output)
        self.assertIn("No such command", result.output)

    def test_worktree_help_exposes_task_lifecycle(self) -> None:
        result = self.runner.invoke(app, ["worktree", "--help"])
        self.assertEqual(result.exit_code, 0, result.output)
        for command in ("create", "status", "close"):
            self.assertIn(command, result.output)

    def test_export_help_exposes_wield(self) -> None:
        result = self.runner.invoke(app, ["export", "--help"])
        self.assertEqual(result.exit_code, 0, result.output)
        self.assertIn("wield", result.output)

    def test_export_v2_exposes_the_isolated_glb_commands(self) -> None:
        result = self.runner.invoke(app, ["export_v2", "--help"])
        self.assertEqual(result.exit_code, 0, result.output)
        self.assertIn("chacter-glb", result.output)
        self.assertIn("chacters-glb", result.output)
        self.assertIn("texture-glb", result.output)
        self.assertIn("textures-glb", result.output)

        old = self.runner.invoke(app, ["export", "--help"])
        self.assertEqual(old.exit_code, 0, old.output)
        self.assertNotIn("character-glb", old.output)


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
            self.assertEqual(
                _child_signal(line), _CHILD_SIGNAL.search(line) is not None, line
            )


if __name__ == "__main__":
    unittest.main()
