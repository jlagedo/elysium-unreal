import json
import tempfile
import unittest
from pathlib import Path

from research.tooling.probes.npc_translation_survey import (
    build_grapple_families,
    resolve_entity_classname,
    survey_current_classes,
)


def _classes():
    common = {
        "pre_translate": "0x10",
        "class_translate": "0x20",
        "cover_activity": "0x30",
        "reload_activity": "0x40",
    }
    return [
        {
            "cpp_class": "CNPC_VHuman",
            "bases": ["CNPC_VHuman", "CAI_BaseNPC"],
            "entity_classnames": ["npc_VHuman"],
            "translation_functions": common,
        },
        {
            "cpp_class": "CNPC_VPedestrian",
            "bases": ["CNPC_VPedestrian", "CNPC_VHuman", "CAI_BaseNPC"],
            "entity_classnames": ["npc_VHuman", "npc_VDialogPedestrian"],
            "translation_functions": common,
        },
        {
            "cpp_class": "CNPC_VCop",
            "bases": ["CNPC_VCop", "CNPC_VHuman", "CAI_BaseNPC"],
            "entity_classnames": [],
            "translation_functions": common,
        },
    ]


class NpcTranslationSurveyTests(unittest.TestCase):
    def test_grapple_base_expands_to_eight_ordered_roles(self):
        registrations = [{"id": 100, "name": "ACT_PAIR", "kind": "special"}]
        registrations.extend({
            "id": 100 + offset,
            "name": "ACT_PAIR_%d" % offset,
            "kind": "ordinary",
        } for offset in range(1, 9))
        family = build_grapple_families(registrations)[0]
        self.assertEqual(family["base_activity"], "ACT_PAIR")
        self.assertEqual(len(family["variants"]), 8)
        self.assertEqual(family["variants"][0]["role"],
                         "attacker_short_victim_front")
        self.assertEqual(family["variants"][-1]["role"],
                         "victim_tall_attacker_back")
        self.assertEqual(family["variants"][-1]["activity"], "ACT_PAIR_8")

    def test_alias_resolution_prefers_most_derived_and_uses_canonical_rtti(self):
        classes = _classes()
        self.assertEqual(
            resolve_entity_classname("npc_VHuman", classes),
            (["CNPC_VPedestrian"], "resolved", "constructor_alias"),
        )
        self.assertEqual(
            resolve_entity_classname("npc_VCop", classes),
            (["CNPC_VCop"], "resolved", "canonical_rtti_name"),
        )
        self.assertEqual(
            resolve_entity_classname("npc_Missing", classes),
            ([], "entity_class_not_recovered", "none"),
        )

    def test_current_join_counts_direct_and_maker_spawn_demands(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            map_dir = root / "test_map"
            map_dir.mkdir()
            (map_dir / "test_map.ents").write_text(json.dumps({
                "map": "test_map",
                "entities": [
                    {"classname": "npc_VDialogPedestrian", "targetname": "talker"},
                    {"classname": "npc_maker", "targetname": "cop_maker",
                     "keys": {"NPCTypE": "npc_VCop"}},
                    {"classname": "npc_Missing", "targetname": "bad"},
                ],
            }), encoding="utf-8")
            rows = survey_current_classes(root, _classes())

        self.assertEqual(len(rows), 3)
        self.assertEqual(rows[0]["cpp_classes"], ["CNPC_VPedestrian"])
        self.assertEqual(rows[1]["demand_kind"], "maker_spawn")
        self.assertEqual(rows[1]["cpp_classes"], ["CNPC_VCop"])
        self.assertEqual(rows[2]["status"], "entity_class_not_recovered")


if __name__ == "__main__":
    unittest.main()
