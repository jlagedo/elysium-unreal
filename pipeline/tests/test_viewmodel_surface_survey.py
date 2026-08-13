import json
import unittest

from research.tooling.probes import viewmodel_surface_survey as survey


class ViewmodelSurfaceSurveyTests(unittest.TestCase):
    def test_clandoc_preserves_repeated_hand_values(self):
        source = b'''ClanData
        {
            General
            {
                "Clan" "Tremere"
                "M_Hands" "models/hands/male/tremere/first.mdl"
                "M_Hands" "models/hands/male/tremere/second.mdl"
                "F_Hands" "models/hands/female/tremere/female.mdl"
            }
        }'''

        rows = survey.clandoc_hand_references(source)

        self.assertEqual(
            [row["model"] for row in rows],
            [
                "models/hands/male/tremere/first.mdl",
                "models/hands/male/tremere/second.mdl",
                "models/hands/female/tremere/female.mdl",
            ],
        )
        self.assertEqual([row["repeated"] for row in rows], [True, True, False])
        self.assertEqual([row["value_index"] for row in rows], [0, 1, 0])

    def test_script_only_shield_models_keep_lines_and_normalize_paths(self):
        source = b'''HANDSMODEL.SetModel("models/hands/male/tremere/normal.mdl")
if shield:
    HANDSMODEL.SetModel('models\\hands\\male\\tremere\\shield.mdl')
'''

        rows = survey.script_hand_references("python/vamputil.py", source)

        self.assertEqual(
            rows,
            [
                {
                    "line": 1,
                    "model": "models/hands/male/tremere/normal.mdl",
                    "source": "python/vamputil.py",
                },
                {
                    "line": 3,
                    "model": "models/hands/male/tremere/shield.mdl",
                    "source": "python/vamputil.py",
                },
            ],
        )

    def test_item_join_carries_viewmodel_family_camera_and_reload(self):
        source = b'''WeaponData
        {
            "viewmodel" "Weapons\\M37\\view\\v_m37"
            "anim_prefix" "m37"
            "camera_class" "ranged"
            "reload_single" "1"
        }'''

        row = survey.item_record("vdata/items/item_w_m37.txt", source)

        self.assertEqual(row["classname"], "item_w_m37")
        self.assertEqual(row["viewmodel"], "models/weapons/m37/view/v_m37.mdl")
        self.assertEqual(row["anim_prefix"], "m37")
        self.assertEqual(row["camera_class"], "ranged")
        self.assertEqual(row["reload_single"], "1")

    def test_missing_reference_diagnostics_are_stable_and_attributed(self):
        clandoc = [{"model": "models/hands/missing.mdl", "key": "m_hands"}]
        scripts = [{"model": "models/hands/present.mdl", "source": "python/a.py"}]
        items = [
            {
                "viewmodel": "models/weapons/missing.mdl",
                "source": "vdata/items/a.txt",
            }
        ]

        rows = survey.reference_diagnostics(
            {"models/hands/present.mdl"}, clandoc, scripts, items
        )

        self.assertEqual(
            [(row["source"], row["model"]) for row in rows],
            [
                ("clandoc", "models/hands/missing.mdl"),
                ("item", "models/weapons/missing.mdl"),
            ],
        )

    def test_serialization_is_independent_of_mapping_insertion_order(self):
        left = {"z": 1, "nested": {"b": 2, "a": 1}}
        right = {"nested": {"a": 1, "b": 2}, "z": 1}

        self.assertEqual(survey.serialize_report(left), survey.serialize_report(right))
        self.assertEqual(json.loads(survey.serialize_report(left)), left)

    def test_declared_closed_domains_have_expected_sizes(self):
        self.assertEqual(len(survey.EXPECTED_PACKED_VIEWMODELS), 17)
        self.assertEqual(len(survey.EXPECTED_FIREARM_FAMILIES), 12)
        self.assertEqual(len(survey.EXPECTED_SHIELD_MODELS), 2)


if __name__ == "__main__":
    unittest.main()
