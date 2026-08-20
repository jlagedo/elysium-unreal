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


MALKAVIAN = "models/character/pc/female/malkavian/armor0/malkavian_female_armor_0.mdl"
JEANETTE = "models/character/npc/unique/santa_monica/jeanette/jeanette.mdl"
THERESE = "models/character/npc/unique/santa_monica/therese/therese.mdl"
LILY = "models/character/npc/unique/santa_monica/lily/lily.mdl"
TZIMISCE = "models/character/monster/tzimisce/creation1/creation1_full.mdl"
TEMPLE = "models/character/npc/unique/chinatown/temple_guard/temple_guard.mdl"
VV = "models/character/npc/unique/downtown/vv/vv.mdl"


class SecondaryMotionTests(unittest.TestCase):
    def test_selected_malkavian_chain_becomes_one_native_recipe(self):
        bones = _bones(
            ["Bip01 Head", "Bone05", "Bone06", "Bone07", "Bone08", "Bone09",
             "Bip01 Spine1", "Bone01"],
            [-1, 0, 1, 2, 3, 4, -1, 6])
        blob = _model([
            (7, -1, 0.0, 1.0, 0.8, 0.0, 20.0),  # breast: not a hair recipe
            (1, -1, 0.0, 0.9, 0.9, 0.3, 60.0),
        ])

        chains = motion.anim_dynamics_poc_chains(MALKAVIAN, blob, bones)

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

        selected = motion.anim_dynamics_poc_chains(JEANETTE, blob, bones)
        self.assertEqual([(item.first_bone, item.chain_end) for item in selected], [
            ("Bone01", "Bone07"), ("Bone09", "Bone13")])
        self.assertEqual(motion.anim_dynamics_poc_chains(THERESE, blob, bones), [])

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
            motion.anim_dynamics_poc_chains(MALKAVIAN, blob, bones)

    def test_malkavian_anonymous_breasts_are_bodies_and_not_hair(self):
        bones = _bones(
            ["Bip01 Head", "Bone05", "Bone06", "Bone07", "Bone08", "Bone09",
             "Bip01 Spine1", "Bone01", "Bone03"],
            [-1, 0, 1, 2, 3, 4, -1, 6, 6])
        blob = _model([
            (7, -1, 0.0, 0.0, 0.95, 0.3, 30.0),
            (8, -1, 0.0, 0.0, 0.95, 0.3, 30.0),
            (1, -1, 0.0, 0.9, 0.9, 0.3, 60.0),
        ])

        chains = motion.anim_dynamics_poc_chains(MALKAVIAN, blob, bones)
        bodies = motion.anim_dynamics_breast_bodies(MALKAVIAN, blob, bones)
        self.assertEqual(len(chains), 1)
        self.assertEqual([body.bound_bone for body in bodies], ["Bone01", "Bone03"])
        self.assertAlmostEqual(bodies[0].gravity_scale, 0.0)
        self.assertAlmostEqual(bodies[0].damping, 0.95)
        self.assertAlmostEqual(bodies[0].angular_spring, 4.0 * math.pow(10.0, -0.3), places=6)
        self.assertAlmostEqual(bodies[0].cone_angle_degrees, 30.0)

    def test_jeanette_token_breasts_are_bodies_beside_hair(self):
        bones = _bones(
            ["Bip01 Head", "Bone01", "Bone03", "Bone05", "Bone06", "Bone07",
             "Bone09", "Bone10", "Bone11", "Bone12", "Bone13",
             "Bip01 Spine1", "right breast", "left breast"],
            [-1, 0, 1, 2, 3, 4, 0, 6, 7, 8, 9, -1, 11, 11])
        blob = _model([
            (1, -1, 0.0, 1.1, 0.9, 0.0, 30.0),
            (6, -1, 0.0, 1.1, 0.9, 0.0, 30.0),
            (12, -1, 0.0, 0.0, 0.9, 2.0, 90.0),
            (13, -1, 0.0, 0.0, 0.9, 2.0, 90.0),
        ])

        chains = motion.anim_dynamics_poc_chains(JEANETTE, blob, bones)
        bodies = motion.anim_dynamics_breast_bodies(JEANETTE, blob, bones)
        self.assertEqual([(item.first_bone, item.chain_end) for item in chains], [
            ("Bone01", "Bone07"), ("Bone09", "Bone13")])
        self.assertEqual([body.bound_bone for body in bodies],
                         ["right breast", "left breast"])
        self.assertAlmostEqual(bodies[0].cone_angle_degrees, 90.0)
        self.assertAlmostEqual(bodies[0].angular_spring, 4.0 * math.pow(10.0, -2.0), places=6)

    def test_therese_has_breast_bodies_and_no_hair_recipe(self):
        bones = _bones(
            ["Bip01 Head", "Bone05", "Bip01 Spine1", "right breast", "left breast"],
            [-1, 0, -1, 2, 2])
        blob = _model([
            (1, -1, 0.0, 1.0, 0.9, 0.0, 15.0),
            (3, -1, 0.0, 0.0, 0.9, 2.0, 20.0),
            (4, -1, 0.0, 0.0, 0.9, 2.0, 20.0),
        ])
        self.assertEqual(motion.anim_dynamics_poc_chains(THERESE, blob, bones), [])
        bodies = motion.anim_dynamics_breast_bodies(THERESE, blob, bones)
        self.assertEqual([body.bound_bone for body in bodies],
                         ["right breast", "left breast"])

    def test_token_names_and_underscore_forms_are_admitted(self):
        bones = _bones(
            ["Bip01 Spine1", "BoobLeft03", "left_breast"],
            [-1, 0, 0])
        blob = _model([
            (1, -1, 0.0, 0.0, 0.95, 0.3, 30.0),
            (2, -1, 0.0, 3.0, 0.9, 0.3, 15.0),
        ])
        bodies = motion.anim_dynamics_breast_bodies(LILY, blob, bones)
        self.assertEqual([body.bound_bone for body in bodies], ["BoobLeft03", "left_breast"])
        self.assertAlmostEqual(bodies[1].gravity_scale, 3.0)
        self.assertAlmostEqual(bodies[1].cone_angle_degrees, 15.0)

    def test_authored_cone_above_90_is_capped_for_breasts(self):
        bones = _bones(["Bip01 Spine1", "left breast"], [-1, 0])
        blob = _model([(1, -1, 0.0, 0.0, 0.9, 2.0, 120.0)])
        bodies = motion.anim_dynamics_breast_bodies(LILY, blob, bones)
        self.assertAlmostEqual(bodies[0].cone_angle_degrees, 90.0)

    def test_damping_below_animdynamics_floor_is_clamped(self):
        bones = _bones(["Bip01 Spine1", "left breast"], [-1, 0])
        blob = _model([(1, -1, 0.0, 0.0, 0.15, 0.3, 30.0)])
        bodies = motion.anim_dynamics_breast_bodies(LILY, blob, bones)
        self.assertAlmostEqual(bodies[0].damping, 0.7)

    def test_head_parented_one_bone_curl_is_not_a_breast(self):
        bones = _bones(["Bip01 Head", "Bone01", "Bip01 Spine1", "left breast"],
                       [-1, 0, -1, 2])
        blob = _model([
            (1, -1, 0.0, 1.0, 0.9, 0.0, 25.0),
            (3, -1, 0.0, 0.0, 0.9, 2.0, 20.0),
        ])
        bodies = motion.anim_dynamics_breast_bodies(VV, blob, bones)
        self.assertEqual([body.bound_bone for body in bodies], ["left breast"])

    def test_three_bone_spine_chain_is_not_a_breast(self):
        bones = _bones(
            ["Bip01 Spine", "Bone01", "Bone02", "Bone03"],
            [-1, 0, 1, 2])
        blob = _model([(1, -1, 0.0, 3.0, 0.9, 0.3, 30.0)])
        self.assertEqual(motion.anim_dynamics_breast_bodies(TEMPLE, blob, bones), [])

    def test_tzimisce_rib_singletons_are_rejected(self):
        bones = _bones(
            ["Bip01 Spine1", "left rib bottom", "right rib bottom",
             "Bip01 Spine2", "left rib top", "right rib top",
             "Bip01 Spine3", "Bone05", "Bone07"],
            [-1, 0, 0, -1, 3, 3, -1, 6, 6])
        blob = _model([
            (1, -1, 0.0, 0.0, 0.8, 0.5, 30.0),
            (2, -1, 0.0, 0.0, 0.8, 0.5, 30.0),
            (4, -1, 0.0, 0.0, 0.8, 0.5, 30.0),
            (5, -1, 0.0, 0.0, 0.8, 0.5, 30.0),
            (7, -1, 0.0, 0.0, 0.8, 0.5, 30.0),
            (8, -1, 0.0, 0.0, 0.8, 0.5, 30.0),
        ])
        self.assertEqual(motion.anim_dynamics_breast_bodies(TZIMISCE, blob, bones), [])

    def test_unclassified_one_bone_spine_row_fails_loudly(self):
        bones = _bones(["Bip01 Spine1", "Bone99"], [-1, 0])
        blob = _model([(1, -1, 0.0, 0.0, 0.9, 0.3, 30.0)])
        with self.assertRaisesRegex(ValueError, "unclassified one-bone spine chain"):
            motion.anim_dynamics_breast_bodies(LILY, blob, bones)

    def test_vv_anonymous_pair(self):
        bones = _bones(
            ["Bip01 Spine1", "bone01", "bone03"],
            [-1, 0, 0])
        blob = _model([
            (1, -1, 0.0, 0.0, 0.9, 2.0, 25.0),
            (2, -1, 0.0, 0.0, 0.9, 2.0, 25.0),
        ])
        bodies = motion.anim_dynamics_breast_bodies(VV, blob, bones)
        self.assertEqual([body.bound_bone for body in bodies], ["bone01", "bone03"])


if __name__ == "__main__":
    unittest.main()
