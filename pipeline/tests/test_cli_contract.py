from __future__ import annotations

import unittest

from typer.testing import CliRunner

from elysium_pipeline.cli import app


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
            "test",
            "run",
            "debug",
            "research",
            "ide",
            "lane",
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

    def test_lane_help_exposes_candidate_lifecycle(self) -> None:
        result = self.runner.invoke(app, ["lane", "--help"])
        self.assertEqual(result.exit_code, 0, result.output)
        for command in ("create", "dispatch", "status", "mark"):
            self.assertIn(command, result.output)

    def test_targeted_model_export_rejects_clean(self) -> None:
        result = self.runner.invoke(
            app, ["export", "model", "models/character/test.mdl", "--clean"]
        )
        self.assertEqual(result.exit_code, 2, result.output)
        self.assertIn("No such option", result.output)


if __name__ == "__main__":
    unittest.main()
