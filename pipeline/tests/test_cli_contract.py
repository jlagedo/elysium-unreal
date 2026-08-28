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

    def test_build_modes_are_named_flags_not_positional_modes(self) -> None:
        result = self.runner.invoke(app, ["build", "--help"])
        self.assertEqual(result.exit_code, 0, result.output)
        self.assertIn("--rebuild", result.output)
        self.assertIn("--clean", result.output)
        self.assertIn("--analyze", result.output)
        self.assertNotIn("[mode]", result.output)

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

    def test_worktree_create_assigns_a_build_slot(self) -> None:
        result = self.runner.invoke(app, ["worktree", "create", "--help"])
        self.assertEqual(result.exit_code, 0, result.output)
        for option in ("--ue-root", "--build-jobs"):
            self.assertIn(option, result.output)

    def test_targeted_model_export_rejects_clean(self) -> None:
        result = self.runner.invoke(
            app, ["export", "model", "models/character/test.mdl", "--clean"]
        )
        self.assertEqual(result.exit_code, 2, result.output)
        self.assertIn("No such option", result.output)

    def test_placed_model_export_is_explicitly_map_scoped(self) -> None:
        result = self.runner.invoke(app, ["export", "placed-model", "--help"])
        self.assertEqual(result.exit_code, 0, result.output)
        self.assertIn("map_name", result.output)
        self.assertIn("model", result.output)
        self.assertIn("--force", result.output)

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

    def test_export_v2_single_and_corpus_argument_contracts(self) -> None:
        single = self.runner.invoke(app, ["export_v2", "chacter-glb", "--help"])
        self.assertEqual(single.exit_code, 0, single.output)
        self.assertIn("model", single.output)

        corpus = self.runner.invoke(app, ["export_v2", "chacters-glb", "--help"])
        self.assertEqual(corpus.exit_code, 0, corpus.output)
        self.assertNotIn("model", corpus.output)

        texture = self.runner.invoke(app, ["export_v2", "texture-glb", "--help"])
        self.assertEqual(texture.exit_code, 0, texture.output)
        self.assertIn("texture", texture.output)

        textures = self.runner.invoke(app, ["export_v2", "textures-glb", "--help"])
        self.assertEqual(textures.exit_code, 0, textures.output)
        self.assertNotIn("texture path", textures.output.lower())

    def test_wield_export_stems_are_optional_positional_arguments(self) -> None:
        result = self.runner.invoke(app, ["export", "wield", "--help"])
        self.assertEqual(result.exit_code, 0, result.output)
        self.assertIn("stems", result.output)
        self.assertIn("whole corpus", result.output)


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
