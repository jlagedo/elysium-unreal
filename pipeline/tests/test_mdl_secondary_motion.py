import math
import struct
from types import SimpleNamespace
import unittest

from elysium_pipeline.formats import mdl_secondary_motion as motion


def _model(records):
    blob = bytearray(512 + len(records) * motion.CHAIN_STRIDE)
    struct.pack_into("<i", blob, 4, motion.MDL_VERSION)
    struct.pack_into("<ii", blob, motion.H_NUM_CHAINS, len(records), 512)
    for index, record in enumerate(records):
        struct.pack_into("<ii5f", blob, 512 + index * motion.CHAIN_STRIDE, *record)
    return bytes(blob)


def _bones(names, parents):
    return [SimpleNamespace(index=index, name=name, parent=parents[index])
            for index, name in enumerate(names)]


class SecondaryMotionTests(unittest.TestCase):
    def test_selected_malkavian_chain_becomes_one_native_recipe(self):
        bones = _bones(
            ["Bip01 Head", "Bone05", "Bone06", "Bone07", "Bone08", "Bone09",
             "Bip01 Spine1", "Bone01"],
            [-1, 0, 1, 2, 3, 4, -1, 6])
        blob = _model([
            (7, -1, 0.0, 1.0, 0.8, 0.0, 20.0),  # breast: explicitly excluded
            (1, -1, 0.0, 0.9, 0.9, 0.3, 60.0),
        ])

        chains = motion.anim_dynamics_poc_chains(
            "models/character/pc/female/malkavian/armor0/malkavian_female_armor_0.mdl",
            blob, bones)

        self.assertEqual(len(chains), 1)
        chain = chains[0]
        self.assertEqual((chain.first_bone, chain.chain_end), ("Bone05", "Bone09"))
        self.assertAlmostEqual(chain.gravity_scale, 0.9)
        self.assertAlmostEqual(chain.damping, 0.9)
        self.assertAlmostEqual(chain.angular_spring, 4.0 * math.pow(10.0, -0.3), places=6)
        self.assertAlmostEqual(chain.cone_angle_degrees, 60.0)

    def test_selected_jeanette_routes_are_exact_and_other_models_are_empty(self):
        bones = _bones(
            ["Bip01 Head", "Bone01", "Bone03", "Bone05", "Bone06", "Bone07",
             "Bone09", "Bone10", "Bone11", "Bone12", "Bone13"],
            [-1, 0, 1, 2, 3, 4, 0, 6, 7, 8, 9])
        blob = _model([
            (1, -1, 0.0, 1.1, 0.9, 0.0, 30.0),
            (6, -1, 0.0, 1.1, 0.9, 0.0, 30.0),
        ])

        selected = motion.anim_dynamics_poc_chains(
            "models/character/npc/unique/santa_monica/jeanette/jeanette.mdl", blob, bones)
        self.assertEqual([(item.first_bone, item.chain_end) for item in selected], [
            ("Bone01", "Bone07"), ("Bone09", "Bone13")])
        self.assertEqual(motion.anim_dynamics_poc_chains(
            "models/character/npc/unique/santa_monica/therese/therese.mdl", blob, bones), [])

    def test_malformed_chain_table_and_non_finite_values_fail_loudly(self):
        truncated = bytearray(_model([(1, -1, 0.0, 1.0, 0.9, 0.0, 30.0)]))
        struct.pack_into("<i", truncated, motion.H_CHAIN_INDEX, len(truncated) - 4)
        with self.assertRaisesRegex(ValueError, "run past"):
            motion.read_chain_records(bytes(truncated))

        non_finite = _model([(1, -1, 0.0, float("nan"), 0.9, 0.0, 30.0)])
        with self.assertRaisesRegex(ValueError, "non-finite"):
            motion.read_chain_records(non_finite)

    def test_selected_chain_must_exist_and_be_head_parented(self):
        bones = _bones(["Bip01 Spine1", "Bone05", "Bone06"], [-1, 0, 1])
        blob = _model([(1, -1, 0.0, 0.9, 0.9, 0.3, 60.0)])
        with self.assertRaisesRegex(ValueError, "not 'Bip01 Head'"):
            motion.anim_dynamics_poc_chains(
                "models/character/pc/female/malkavian/armor0/malkavian_female_armor_0.mdl",
                blob, bones)


if __name__ == "__main__":
    unittest.main()
