"""The reference compositor's arithmetic, on synthetic bones.

Game-independent by construction: three hand-built bones and hand-built quaternions, so what is
asserted is the RULE -- which side a delta lands on, which bone a split rotation skips, how a
cell fraction resolves -- and never a model's data.
"""

from __future__ import annotations

import math
import unittest

from elysium_pipeline.formats.mdl_skel import Bone
from elysium_pipeline.validation import retail_compositor as rc


def _axis_angle(axis, degrees):
    x, y, z = axis
    n = math.sqrt(x * x + y * y + z * z)
    h = math.radians(degrees) / 2.0
    s = math.sin(h) / n
    return (x * s, y * s, z * s, math.cos(h))


def _bone(index, name, parent, pos=(0.0, 0.0, 0.0), quat=(0.0, 0.0, 0.0, 1.0), flags=0):
    return Bone(index=index, name=name, parent=parent, pos=pos, quat=quat,
                posscale=(1, 1, 1), rotscale=(1, 1, 1, 1), pose_to_bone=(0,) * 12, flags=flags)


class _FakeModel:
    def __init__(self, bones):
        self.bones = bones
        self.by_name = {b.name.lower(): b for b in bones}


class QuaternionKitTests(unittest.TestCase):
    def test_scale_is_a_slerp_from_identity(self):
        q = _axis_angle((0, 0, 1), 60.0)
        half = rc.qscale(q, 0.5)
        expected = _axis_angle((0, 0, 1), 30.0)
        for a, b in zip(half, expected):
            self.assertAlmostEqual(a, b, places=6)
        self.assertEqual(rc.qscale(q, 1.0), q)
        self.assertEqual(rc.qscale(q, 0.0), (0.0, 0.0, 0.0, 1.0))

    def test_nlerp_flips_to_the_nearer_hemisphere(self):
        q = _axis_angle((1, 0, 0), 20.0)
        flipped = tuple(-c for c in q)
        mixed = rc.qnlerp(q, flipped, 0.5)
        for a, b in zip(mixed, q):
            self.assertAlmostEqual(a, b, places=6)


class AccumulateTests(unittest.TestCase):
    def test_a_post_delta_lands_on_the_right(self):
        base = _axis_angle((0, 0, 1), 90.0)
        delta = _axis_angle((1, 0, 0), 30.0)
        out = [((1.0, 2.0, 3.0), base)]
        rc.accumulate(out, [((0.5, 0.0, 0.0), delta)], 1.0, rc.FLAG_DELTA | rc.FLAG_POST)
        expected = rc.qnorm(rc.qmul(base, delta))
        for a, b in zip(out[0][1], expected):
            self.assertAlmostEqual(a, b, places=6)
        self.assertEqual(out[0][0], (1.5, 2.0, 3.0))

    def test_a_pre_delta_lands_on_the_left_and_differs(self):
        base = _axis_angle((0, 0, 1), 90.0)
        delta = _axis_angle((1, 0, 0), 30.0)
        out = [((0.0, 0.0, 0.0), base)]
        rc.accumulate(out, [((0.0, 0.0, 0.0), delta)], 1.0, rc.FLAG_DELTA)
        expected = rc.qnorm(rc.qmul(delta, base))
        for a, b in zip(out[0][1], expected):
            self.assertAlmostEqual(a, b, places=6)
        post = rc.qnorm(rc.qmul(base, delta))
        self.assertGreater(max(abs(a - b) for a, b in zip(expected, post)), 0.1)

    def test_an_ordinary_layer_replaces_at_full_weight_and_skips_a_masked_bone(self):
        out = [((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)), ((9.0, 9.0, 9.0), (0.0, 0.0, 0.0, 1.0))]
        layer_rot = _axis_angle((0, 1, 0), 45.0)
        rc.accumulate(out, [((1.0, 1.0, 1.0), layer_rot), None], 1.0, 0)
        self.assertEqual(out[0][0], (1.0, 1.0, 1.0))
        self.assertEqual(out[0][1], layer_rot)
        self.assertEqual(out[1][0], (9.0, 9.0, 9.0))


class SlotGateTests(unittest.TestCase):
    def test_a_slot_gate_drops_every_bone_the_slot_does_not_own(self):
        layer = [((1.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))] * 4
        gated = rc.gate_to(layer, {1, 2})
        self.assertEqual([c is not None for c in gated], [False, True, True, False])
        self.assertIs(rc.gate_to(layer, None), layer)


class SplitRotationTests(unittest.TestCase):
    def test_a_split_bone_takes_its_rotation_as_model_space(self):
        turn = _axis_angle((0, 0, 1), 90.0)
        bones = [_bone(0, "root", -1), _bone(1, "spine", 0, pos=(0.0, 0.0, 10.0)),
                 _bone(2, "split", 1, pos=(0.0, 0.0, 10.0), flags=rc.SPLIT_ROTATION)]
        model = _FakeModel(bones)
        locals_ = [((0.0, 0.0, 0.0), turn), ((0.0, 0.0, 10.0), turn), ((0.0, 0.0, 10.0), turn)]
        world = rc.model_space(model, locals_)
        # Conventional inheritance would give the split bone 270 deg; the rule gives it its own 90.
        self.assertAlmostEqual(world[1][1][2], math.sin(math.radians(90.0)), places=6)
        for a, b in zip(world[2][1], turn):
            self.assertAlmostEqual(a, b, places=6)
        # Translation still composes through the (rotated) parent.
        self.assertAlmostEqual(world[2][0][2], 20.0, places=6)


class AxisTests(unittest.TestCase):
    def test_a_fan_resolves_a_cell_and_a_fraction(self):
        from elysium_pipeline.formats.mdl_skel import Grid, PoseParam
        model = _FakeModel([])
        model.pose_params = {"move_yaw": PoseParam(index=0, name="move_yaw", flags=1,
                                                   start=-180.0, end=180.0, loop=360.0)}
        grid = Grid(numblends=9, groupsize=(9, 1), paramindex=(0, -1),
                    paramstart=(-180.0, 0.0), paramend=(180.0, 0.0), cells=())
        self.assertEqual(rc._axis(model, grid, 0, {"move_yaw": 0.0}), (4, 0.0))
        i, s = rc._axis(model, grid, 0, {"move_yaw": 22.5})
        self.assertEqual(i, 4)
        self.assertAlmostEqual(s, 0.5, places=6)
        # Wrap through the loop: 190 is -170.
        i, s = rc._axis(model, grid, 0, {"move_yaw": 190.0})
        self.assertEqual(i, 0)
        self.assertAlmostEqual(s, 10.0 / 45.0, places=6)
        self.assertEqual(rc._axis(model, grid, 1, {"move_yaw": 0.0}), (0, 0.0))


if __name__ == "__main__":
    unittest.main()
