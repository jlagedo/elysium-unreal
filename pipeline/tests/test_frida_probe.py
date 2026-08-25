from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
from threading import Event
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from research.tooling.capture.frida_probe import (
    RECIPE_SCHEMA,
    _wait_for_attach_stop,
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

    def test_life7_theatre_recipe_carries_the_ordered_oracle_boundaries(self) -> None:
        recipe = load_recipe("life7_theatre_oracle")
        known = {
            target["semantic_label"]
            for profile in REGISTRY["profiles"]
            for target in profile["targets"]
        }
        self.assertLessEqual(set(recipe["targets"]), known)
        self.assertEqual(recipe["expected_modules"], ["vampire.dll"])
        self.assertEqual(
            recipe["max_depth"],
            {"vampire.camera_track_sample": 0},
        )

        required = {
            "vampire.base_entity_output_fire_output",
            "vampire.event_queue_add",
            "vampire.event_queue_service_events",
            "vampire.base_entity_accept_input",
            "vampire.execute_output_python",
            "vampire.execute_queued_python_source",
            "vampire.logic_python_check_evaluate",
            "vampire.python_data_manager_setattr",
            "vampire.python_entity_input_function",
            "vampire.scene_start_playback",
            "vampire.scene_on_finished",
            "vampire.scene_cancel_playback",
            "vampire.scene_dispatch_start_event",
            "vampire.scene_find_named_entity",
            "vampire.scene_apply_anim_set",
            "vampire.camera_play_position",
            "vampire.camera_play_target",
            "vampire.camera_track_sample",
            "vampire.camera_player_adopt_position",
            "vampire.camera_player_adopt_target",
            "vampire.scripted_sequence_begin",
            "vampire.scripted_sequence_finish",
        }
        self.assertEqual(set(recipe["targets"]), required)

        reads = recipe["field_reads"]
        self.assertIn(
            "source",
            {field["label"] for field in reads["vampire.execute_output_python"]},
        )
        self.assertIn(
            "value_word",
            {
                field["label"]
                for field in reads["vampire.python_data_manager_setattr"]
            },
        )
        self.assertTrue(
            {
                "scene_time",
                "event_type",
                "event_name",
                "event_param",
                "event_start",
                "event_end",
            }
            <= {
                field["label"]
                for field in reads["vampire.scene_dispatch_start_event"]
            }
        )

    def test_attach_stop_file_is_an_explicit_terminal_boundary(self) -> None:
        with TemporaryDirectory() as directory:
            stop_file = Path(directory) / "stop-life7"
            stop_file.write_text("stop\n", encoding="ascii")
            self.assertEqual(
                _wait_for_attach_stop(Event(), 10.0, stop_file),
                "stop-file",
            )

        detached = Event()
        detached.set()
        self.assertEqual(_wait_for_attach_stop(detached, 10.0, None), "detached")

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

        attach_args = parser().parse_args(
            [
                "attach",
                "--pid",
                "123",
                "--recipe",
                "life7_theatre_oracle",
                "--stop-file",
                "stop-life7",
            ]
        )
        self.assertEqual(attach_args.stop_file, Path("stop-life7"))

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
