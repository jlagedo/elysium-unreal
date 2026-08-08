import json
import tempfile
import unittest
from pathlib import Path

from research.tooling.probes.action_animation_survey import (
    build_report,
    scan_python_text,
)


class ActionAnimationSurveyTests(unittest.TestCase):
    def test_python_scan_ignores_inert_strings_and_recovers_scheduled_payload(self):
        source = '''
# npc.SetGesture("comment")
inert = 'npc.SetDisposition("string", 1)'
npc.SetGesture("wave")
actor.SetModel("models/character/npc/test.mdl")
actor.Transform()
ScheduleTask(1.0, 'FindEntityByName("prop").SetAnimation("open")')
def two_calls():
    fighter = Find("fighter_%i" % i)
    fighter.SetAnimation("attack_one")
    fighter.SetAnimation("attack_two")
'''
        rows = scan_python_text(source, "py", "scripts/test.py")

        self.assertEqual([row["call"] for row in rows],
                         ["SetGesture", "SetModel", "Transform", "SetAnimation",
                          "SetAnimation", "SetAnimation"])
        self.assertEqual(rows[0]["value"], "wave")
        self.assertFalse(rows[0]["scheduled"])
        self.assertEqual(rows[1]["value"], "models/character/npc/test.mdl")
        self.assertEqual(rows[1]["value_kind"], "model_selection")
        self.assertEqual(rows[2]["value_kind"], "model_transform")
        self.assertEqual(rows[3]["value"], "open")
        self.assertTrue(rows[3]["scheduled"])
        self.assertEqual(rows[3]["inline_target"], "prop")
        self.assertEqual(rows[4]["bound_target"], "fighter_%i")
        self.assertEqual(rows[5]["bound_target"], "fighter_%i")

    def test_report_joins_map_targets_and_all_python_surfaces(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            map_dir = root / "test_map"
            script_dir = root / "scripts" / "test"
            dialogue_dir = root / "dlg" / "test"
            map_dir.mkdir(parents=True)
            script_dir.mkdir(parents=True)
            dialogue_dir.mkdir(parents=True)

            entities = [
                {"classname": "worldspawn", "targetname": "", "levelscript": "test"},
                {
                    "classname": "prop_dynamic",
                    "targetname": "prop",
                    "model": "models/prop.mdl",
                    "LoopSequence": "idle",
                    "RandomAnimation": "0",
                    "demo_sequence": "NONE",
                },
                {
                    "classname": "scripted_sequence",
                    "targetname": "beat",
                    "m_iszEntity": "npc",
                    "m_iszIdle": "wait",
                    "m_iszPlay": "wave",
                    "m_iszPostIdle": "hold",
                    "m_iszCustomMove": "sneak",
                    "m_iszPreIdle": "ignored",
                    "m_fMoveTo": "3",
                    "m_iszNextScript": "beat2",
                },
                {"classname": "scripted_sequence", "targetname": "beat2"},
                {
                    "classname": "npc_VHumanCombatant",
                    "targetname": "npc",
                    "combat_start_activity": "ACT_RUN",
                    "default_disposition": "Combat",
                    "additionalequipment": "item_w_fists",
                    "alternateequipment": "0",
                    "MorphModel": "models/character/npc/morph.mdl",
                    "usescript": 'pc.SetGesture("use_wave")',
                },
                {
                    "classname": "aiscripted_schedule",
                    "targetname": "schedule",
                    "schedule": "3",
                    "forcestate": "2",
                    "goalent": "!player",
                },
                {
                    "classname": "logic_pythoncheck",
                    "targetname": "check",
                    "python_script": 'FindEntityByName("beat").BeginSequence()',
                },
                {
                    "classname": "logic_relay",
                    "targetname": "relay",
                    "outputs": [
                        {
                            "name": "OnTrigger", "target": "prop", "input": "SetAnimation",
                            "param": "open", "delay": "0", "times": "1", "python": "",
                        },
                        {
                            "name": "OnTrigger", "target": "missing", "input": "BeginSequence",
                            "param": "", "delay": "0", "times": "1",
                            "python": 'pc.SetDisposition("Fear", 3)',
                        },
                    ],
                },
            ]
            (map_dir / "test_map.ents").write_text(
                json.dumps({"map": "test_map", "entities": entities}), encoding="utf-8")
            (script_dir / "test.py").write_text(
                'FindEntityByName("prop").SetAnimation("close")\n', encoding="latin-1")
            (dialogue_dir / "test.dlg").write_text(
                '{0}{1}{2}{3}{pc.SetDisposition("Neutral", 1)}{pc.SeductiveFeed()}\n',
                encoding="latin-1")

            report = build_report(root)

        self.assertEqual(report["maps"]["maps"], 1)
        self.assertEqual(report["maps"]["levelscript_maps"], {"test": ["test_map"]})
        self.assertEqual(report["summary"]["io_inputs"], {
            "SetAnimation": 1,
            "BeginSequence": 1,
        })
        self.assertEqual(report["summary"]["io_resolution"], {
            "resolved": 1,
            "missing_target": 1,
        })
        set_animation = report["maps"]["io_wires"][0]
        self.assertEqual(set_animation["resolved_targets"][0]["classname"], "prop_dynamic")
        self.assertEqual(set_animation["resolved_targets"][0]["model"], "models/prop.mdl")
        self.assertTrue(report["maps"]["sequence_links"][0]["target_exists"])
        self.assertEqual(report["summary"]["producer_status"]["engine_ignored"], 1)
        self.assertEqual(report["summary"]["producer_status"]["engine_ignored_sentinel"], 1)
        self.assertEqual(report["summary"]["producer_counts"]["npc_morph_model"], 1)
        self.assertEqual(report["summary"]["python_calls"], {
            "SetDisposition": 2,
            "SetAnimation": 1,
            "SeductiveFeed": 1,
            "BeginSequence": 1,
            "SetGesture": 1,
        })


if __name__ == "__main__":
    unittest.main()
