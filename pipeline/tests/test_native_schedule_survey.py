import importlib.util
import json
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "research" / "tooling" / "probes" / "native_schedule_survey.py"
SPEC = importlib.util.spec_from_file_location("native_schedule_survey", MODULE_PATH)
SURVEY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SURVEY)


class NativeScheduleSurveyTests(unittest.TestCase):
    def test_decodes_ordered_tasks_arguments_interrupts_and_flags(self):
        text = (
            "\n Schedule SCHED_TEST Tasks "
            "TASK_SET_ACTIVITY ACTIVITY:ACT_IDLE "
            "TASK_WAIT 3 Interrupts COND_NEW_ENEMY COND_LIGHT_DAMAGE "
            "Flags DELAY_INTERRUPTS\n"
        )
        schedules = SURVEY.decode_schedules(b"noise\x00" + text.encode("ascii") + b"\x00tail")
        self.assertEqual(len(schedules), 1)
        schedule = schedules[0]
        self.assertEqual(schedule["name"], "SCHED_TEST")
        self.assertEqual(
            [(row["task"], row["argument"]) for row in schedule["tasks"]],
            [("TASK_SET_ACTIVITY", "ACTIVITY:ACT_IDLE"), ("TASK_WAIT", "3")],
        )
        self.assertEqual(schedule["tasks"][0]["activities"], ["ACT_IDLE"])
        self.assertEqual(schedule["interrupts"], ["COND_NEW_ENEMY", "COND_LIGHT_DAMAGE"])
        self.assertEqual(schedule["flags"], ["DELAY_INTERRUPTS"])

    def test_manifest_join_marks_direct_and_translation_required_requests(self):
        schedules = [{
            "name": "SCHED_TEST",
            "tasks": [
                {"task": "TASK_SET_ACTIVITY", "argument": "ACTIVITY:ACT_IDLE",
                 "activities": ["ACT_IDLE"]},
                {"task": "TASK_PLAY_SEQUENCE", "argument": "ACTIVITY:ACT_UNKNOWN",
                 "activities": ["ACT_UNKNOWN"]},
            ],
        }]
        manifest = {
            "manifest_version": 6,
            "npcs": {
                "npc": {
                    "model": "models/npc.mdl",
                    "clips": {"idle": "shared"},
                    "own_clips": {},
                },
            },
            "banks": {
                "shared": {
                    "clips": {
                        "idle": {"activity": "ACT_IDLE", "weight": 1, "flags": 1,
                                 "frames": 10, "fps": 30.0},
                    },
                },
            },
        }
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "npc_manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            joined = SURVEY.join_explicit_activities(schedules, path)
        rows = {row["activity"]: row for row in joined["requests"]}
        self.assertEqual(rows["ACT_IDLE"]["direct_status"], "available")
        self.assertEqual(rows["ACT_IDLE"]["direct_model_count"], 1)
        self.assertEqual(
            rows["ACT_UNKNOWN"]["direct_status"], "translation_or_fallback_required")


if __name__ == "__main__":
    unittest.main()
