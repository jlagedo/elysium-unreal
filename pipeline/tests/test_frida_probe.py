from __future__ import annotations

from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from research.tooling.capture.frida_probe import (
    RECIPE_SCHEMA,
    agent_config,
    launch,
    load_recipe,
    parser,
)
from research.tooling.capture.generated_binary_profiles import REGISTRY


class FridaProbeContractTest(unittest.TestCase):
    def test_smoke_recipe_uses_fixture_exports_without_retail_profiles(self) -> None:
        recipe = load_recipe("smoke")
        self.assertEqual(recipe["schema"], RECIPE_SCHEMA)
        self.assertEqual(recipe["targets"], [])
        self.assertEqual(
            {name.casefold() for name in recipe["expected_modules"]},
            {"client.dll", "engine.dll", "studiorender.dll"},
        )
        config = agent_config(recipe, validate_retail=False)
        self.assertTrue(config["require_ia32"])
        self.assertEqual(config["profiles"], [])
        self.assertEqual(
            {item["label"] for item in recipe["export_hooks"]},
            set(recipe["expected_callers"]),
        )

    def test_hook_recipe_names_existing_profile_targets(self) -> None:
        recipe = load_recipe("cap2_8_callers")
        known = {
            target["semantic_label"]
            for profile in REGISTRY["profiles"]
            for target in profile["targets"]
        }
        self.assertLessEqual(set(recipe["targets"]), known)
        with self.assertRaisesRegex(
            ValueError,
            "requires exact retail module validation",
        ):
            agent_config(recipe, validate_retail=False)

    def test_recipe_name_cannot_escape_recipe_root(self) -> None:
        with self.assertRaisesRegex(ValueError, "invalid Frida recipe name"):
            load_recipe("../smoke")

    def test_supervised_collector_arguments_parse_after_prefix(self) -> None:
        args = parser().parse_args(
            [
                "collect",
                "--recipe",
                "smoke",
                "--output",
                "session",
                "--stop-event",
                "stop",
                "--target-pid",
                "123",
                "--ready-event",
                "ready",
            ]
        )
        self.assertEqual(args.target_pid, 123)
        self.assertEqual(args.ready_event, "ready")

    def test_retail_launch_quotes_option_looking_collector_arguments(self) -> None:
        args = parser().parse_args(
            [
                "launch",
                "--recipe",
                "cap2_8_callers",
                "--startup-profile",
                "unofficial-patch-save",
            ]
        )
        with (
            patch(
                "research.tooling.capture.frida_probe._session_root",
                return_value=Path("session"),
            ),
            patch(
                "research.tooling.capture.frida_probe.subprocess.run",
                return_value=SimpleNamespace(returncode=0),
            ) as run,
        ):
            self.assertEqual(launch(args), 0)

        command = run.call_args.args[0]
        self.assertNotIn("--collector-argument", command)
        self.assertIn("--collector-argument=--recipe", command)
        self.assertIn("--collector-argument=--output", command)
        self.assertIn("--collector-argument=--validate-retail", command)


if __name__ == "__main__":
    unittest.main()
