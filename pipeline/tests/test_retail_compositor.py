"""The reference compositor's arithmetic, on synthetic bones.

Game-independent by construction: three hand-built bones and hand-built quaternions, so what is
asserted is the RULE -- which side a delta lands on, which bone a split rotation skips, how a
cell fraction resolves -- and never a model's data.
"""

from __future__ import annotations

import math
import unittest

import pytest

from elysium_pipeline.formats import mdl_skel
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
    def __init__(self, bones, key=None):
        self.bones = bones
        self.by_name = {b.name.lower(): b for b in bones}
        self.key = key if key is not None else str(id(self))


def test_scale_is_a_slerp_from_identity():
    q = _axis_angle((0, 0, 1), 60.0)
    half = rc.qscale(q, 0.5)
    expected = _axis_angle((0, 0, 1), 30.0)
    for a, b in zip(half, expected):
        assert a == pytest.approx(b, abs=1e-6)
    assert rc.qscale(q, 1.0) == q
    assert rc.qscale(q, 0.0) == (0.0, 0.0, 0.0, 1.0)


def test_nlerp_flips_to_the_nearer_hemisphere():
    q = _axis_angle((1, 0, 0), 20.0)
    flipped = tuple(-c for c in q)
    mixed = rc.qnlerp(q, flipped, 0.5)
    for a, b in zip(mixed, q):
        assert a == pytest.approx(b, abs=1e-6)


def test_a_post_delta_lands_on_the_right():
    base = _axis_angle((0, 0, 1), 90.0)
    delta = _axis_angle((1, 0, 0), 30.0)
    out = [((1.0, 2.0, 3.0), base)]
    rc.accumulate(out, [((0.5, 0.0, 0.0), delta)], 1.0, rc.FLAG_DELTA | rc.FLAG_POST)
    expected = rc.qnorm(rc.qmul(base, delta))
    for a, b in zip(out[0][1], expected):
        assert a == pytest.approx(b, abs=1e-6)
    assert out[0][0] == (1.5, 2.0, 3.0)


def test_a_pre_delta_lands_on_the_left_and_differs():
    base = _axis_angle((0, 0, 1), 90.0)
    delta = _axis_angle((1, 0, 0), 30.0)
    out = [((0.0, 0.0, 0.0), base)]
    rc.accumulate(out, [((0.0, 0.0, 0.0), delta)], 1.0, rc.FLAG_DELTA)
    expected = rc.qnorm(rc.qmul(delta, base))
    for a, b in zip(out[0][1], expected):
        assert a == pytest.approx(b, abs=1e-6)
    post = rc.qnorm(rc.qmul(base, delta))
    assert max(abs(a - b) for a, b in zip(expected, post)) > 0.1


def test_an_ordinary_layer_replaces_at_full_weight_and_skips_a_masked_bone():
    out = [((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)), ((9.0, 9.0, 9.0), (0.0, 0.0, 0.0, 1.0))]
    layer_rot = _axis_angle((0, 1, 0), 45.0)
    rc.accumulate(out, [((1.0, 1.0, 1.0), layer_rot), None], 1.0, 0)
    assert out[0][0] == (1.0, 1.0, 1.0)
    assert out[0][1] == layer_rot
    assert out[1][0] == (9.0, 9.0, 9.0)


def test_a_slot_gate_drops_every_bone_the_slot_does_not_own():
    layer = [((1.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))] * 4
    gated = rc.gate_to(layer, {1, 2})
    assert [c is not None for c in gated] == [False, True, True, False]
    assert rc.gate_to(layer, None) is layer


def test_a_split_bone_takes_its_rotation_as_model_space():
    turn = _axis_angle((0, 0, 1), 90.0)
    bones = [_bone(0, "root", -1), _bone(1, "spine", 0, pos=(0.0, 0.0, 10.0)),
             _bone(2, "split", 1, pos=(0.0, 0.0, 10.0), flags=rc.SPLIT_ROTATION)]
    model = _FakeModel(bones)
    locals_ = [((0.0, 0.0, 0.0), turn), ((0.0, 0.0, 10.0), turn), ((0.0, 0.0, 10.0), turn)]
    world = rc.model_space(model, locals_)
    # Conventional inheritance would give the split bone 270 deg; the rule gives it its own 90.
    assert world[1][1][2] == pytest.approx(math.sin(math.radians(90.0)), abs=1e-6)
    for a, b in zip(world[2][1], turn):
        assert a == pytest.approx(b, abs=1e-6)
    # Translation still composes through the (rotated) parent.
    assert world[2][0][2] == pytest.approx(20.0, abs=1e-6)


def test_a_fan_resolves_a_cell_and_a_fraction():
    from elysium_pipeline.formats.mdl_skel import Grid, PoseParam
    model = _FakeModel([])
    model.pose_params = {"move_yaw": PoseParam(index=0, name="move_yaw", flags=1,
                                               start=-180.0, end=180.0, loop=360.0)}
    grid = Grid(numblends=9, groupsize=(9, 1), paramindex=(0, -1),
                paramstart=(-180.0, 0.0), paramend=(180.0, 0.0), cells=())
    assert rc._axis(model, grid, 0, {"move_yaw": 0.0}) == (4, 0.0)
    i, s = rc._axis(model, grid, 0, {"move_yaw": 22.5})
    assert i == 4
    assert s == pytest.approx(0.5, abs=1e-6)
    # Wrap through the loop: 190 is -170.
    i, s = rc._axis(model, grid, 0, {"move_yaw": 190.0})
    assert i == 0
    assert s == pytest.approx(10.0 / 45.0, abs=1e-6)
    assert rc._axis(model, grid, 1, {"move_yaw": 0.0}) == (0, 0.0)


# BindRemapBranchTests
# The three-branch bind-pose remap (`vampire.dll FUN_100c67b0` / `0x1008cfa0`), on
# hand-built bind pairs -- never a model's data.

def test_close_binds_copy_the_position_unchanged():
    owner = _bone(0, "root", -1, pos=(1.0, 2.0, 3.0))
    body = _bone(0, "root", -1, pos=(1.0, 2.0, 3.0 + 0.09))  # |a-b|^2 = 0.0081 < 0.01
    kind, payload = rc._bind_branch(owner.pos, body.pos)
    assert kind == "copy"
    assert payload is None


def test_close_binds_boundary_just_outside_is_not_copy():
    owner = _bone(0, "root", -1, pos=(0.0, 0.0, 0.0))
    body = _bone(0, "root", -1, pos=(0.0, 0.0, 0.11))  # |a-b|^2 = 0.0121 > 0.01
    kind, _payload = rc._bind_branch(owner.pos, body.pos)
    assert kind != "copy"


def test_both_binds_near_origin_is_still_a_copy_not_a_translation():
    # |a-b|^2 exceeds the threshold but BOTH |a|^2 and |b|^2 are within it: the disassembly
    # takes neither the translate nor the similarity path.
    a = (0.09, 0.0, 0.0)
    b = (-0.09, 0.0, 0.0)
    assert sum(c * c for c in (a[0] - b[0], a[1] - b[1], a[2] - b[2])) > 0.01
    assert sum(c * c for c in a) <= 0.01
    assert sum(c * c for c in b) <= 0.01
    kind, payload = rc._bind_branch(a, b)
    assert kind == "copy"
    assert payload is None


def test_one_bind_near_origin_is_a_pure_translation():
    a = (3.0, 0.0, 0.0)     # owner/bank bind, far from the origin
    b = (0.02, 0.0, 0.0)    # body bind, within the threshold
    kind, payload = rc._bind_branch(a, b)
    assert kind == "translate"
    for got, want in zip(payload, (b[0] - a[0], b[1] - a[1], b[2] - a[2])):
        assert got == pytest.approx(want, abs=1e-6)


def test_two_far_binds_are_a_similarity():
    a = (2.0, 0.0, 0.0)
    b = (0.0, 4.0, 0.0)
    kind, payload = rc._bind_branch(a, b)
    assert kind == "similarity"
    arc, scale = payload
    assert scale == pytest.approx(2.0, abs=1e-6)  # |b|/|a| = 4/2
    rotated = rc.qrotate(arc, a)
    for got, want in zip(rotated, (0.0, 2.0, 0.0)):  # a rotated onto b's direction, |a| kept
        assert got == pytest.approx(want, abs=1e-6)


def test_the_measured_ash_tremere_pelvis_case_is_the_origin_translate_branch():
    # The measured defect case, converted from its own cm reading to the Source units this
    # module works in: `Bip01 Pelvis` bank bind 2.968 cm / body bind 0.145 cm, 177.2 degrees
    # apart. The body's own bind is within the threshold in Source units too (0.145 cm /
    # 2.54 = 0.0571 in, under the 0.1 in threshold), so this is the "one bind near the
    # origin" branch -- not a similarity -- which is exactly the defect: Unreal's
    # `OrientAndScale` has no such branch at all, and rotate-and-scale on a bone whose body
    # bind sits this close to the origin annihilates the animated deviation.
    a = (2.968 / rc.SOURCE_UNIT_TO_CM, 0.0, 0.0)
    b = (-0.145 / rc.SOURCE_UNIT_TO_CM, 0.0, 0.0)
    kind, payload = rc._bind_branch(a, b)
    assert kind == "translate"
    for got, want in zip(payload, (b[0] - a[0], b[1] - a[1], b[2] - a[2])):
        assert got == pytest.approx(want, abs=1e-6)


def test_antiparallel_far_binds_resolve_a_half_turn():
    # A synthetic (not model-measured) antiparallel pair where NEITHER bind is near the
    # origin, to exercise `_shortest_arc`'s degenerate 180-degree branch under the
    # similarity path specifically.
    a = (2.0, 0.0, 0.0)
    b = (-1.0, 0.0, 0.0)
    kind, payload = rc._bind_branch(a, b)
    assert kind == "similarity"
    arc, scale = payload
    assert scale == pytest.approx(0.5, abs=1e-6)
    rotated = rc.qrotate(arc, a)
    assert rotated[0] == pytest.approx(-a[0], abs=1e-4)
    assert rotated[1] == pytest.approx(0.0, abs=1e-4)
    assert rotated[2] == pytest.approx(0.0, abs=1e-4)


class RemapTests(unittest.TestCase):
    """`remap()` on a two-bone owner/body pair covering all three branches at once, plus the
    rotation-untouched guarantee."""

    def setUp(self):
        self.rot = _axis_angle((0, 1, 0), 37.0)
        # copy_bone: binds coincide.  translate_bone: owner far, body near the origin.
        # similarity_bone: both far and not parallel.
        self.owner = _FakeModel([
            _bone(0, "copy_bone", -1, pos=(1.0, 1.0, 1.0)),
            _bone(1, "translate_bone", -1, pos=(3.0, 0.0, 0.0)),
            _bone(2, "similarity_bone", -1, pos=(2.0, 0.0, 0.0)),
        ])
        self.body = _FakeModel([
            _bone(0, "copy_bone", -1, pos=(1.0, 1.0, 1.0)),
            _bone(1, "translate_bone", -1, pos=(0.0, 0.0, 0.0)),
            _bone(2, "similarity_bone", -1, pos=(0.0, 2.0, 0.0)),
        ])

    def test_copy_branch_passes_the_animated_position_through(self):
        pose = [((5.0, 6.0, 7.0), self.rot), None, None]
        out = rc.remap(self.body, self.owner, pose)
        assert out[0][0] == (5.0, 6.0, 7.0)

    def test_translate_branch_adds_the_raw_bind_offset(self):
        pose = [None, ((3.5, 0.0, 0.0), self.rot), None]
        out = rc.remap(self.body, self.owner, pose)
        # p + (b - a) = 3.5 + (0 - 3) = 0.5
        for got, want in zip(out[1][0], (0.5, 0.0, 0.0)):
            assert got == pytest.approx(want, abs=1e-6)

    def test_similarity_branch_rotates_and_rescales(self):
        pose = [None, None, ((2.0, 0.0, 0.0), self.rot)]  # animated pos == owner bind exactly
        out = rc.remap(self.body, self.owner, pose)
        # Rotating the owner's own bind direction onto the body's and rescaling to |b| must
        # reproduce the body's own bind exactly (both binds' lengths are 2.0 here).
        for got, want in zip(out[2][0], (0.0, 2.0, 0.0)):
            assert got == pytest.approx(want, abs=1e-6)

    def test_rotation_is_never_touched_in_any_branch(self):
        pose = [((5.0, 6.0, 7.0), self.rot), ((3.5, 0.0, 0.0), self.rot),
                ((2.0, 0.0, 0.0), self.rot)]
        out = rc.remap(self.body, self.owner, pose)
        for entry in out:
            assert entry is not None
            assert entry[1] == self.rot


# ClosureRemapTests
# `compose()` remaps a channel's WHOLE closure -- its sequence plus its autolayers,
# composed in the owner's own bind space -- exactly ONCE, never per layer.
#
# A translate-branch bone makes the distinction observable: remapping each layer on its own
# would add the bind offset twice (once to the base, once to the raw additive delta), which is
# exactly the wrong-shaped defect this module exists to catch.

def _corpus():
    identity = (0.0, 0.0, 0.0, 1.0)
    owner_bones = [_bone(0, "root", -1, pos=(0.0, 0.0, 0.0)),
                   _bone(1, "target", 0, pos=(1.0, 0.0, 0.0))]
    body_bones = [_bone(0, "root", -1, pos=(0.0, 0.0, 0.0)),
                  _bone(1, "target", 0, pos=(0.0, 0.0, 0.0))]  # |b|^2 <= eps: translate branch

    owner = _FakeModel(owner_bones)
    owner.pose_params = {}
    base_grid = mdl_skel.Grid(numblends=1, groupsize=(1, 1), paramindex=(-1, -1),
                              paramstart=(0.0, 0.0), paramend=(0.0, 0.0),
                              cells=(mdl_skel.Cell(axis0=0, axis1=0, anim=0),))
    delta_grid = mdl_skel.Grid(numblends=1, groupsize=(1, 1), paramindex=(-1, -1),
                               paramstart=(0.0, 0.0), paramend=(0.0, 0.0),
                               cells=(mdl_skel.Cell(axis0=0, axis1=0, anim=1),))
    base_seq = mdl_skel.Seq(label="base_seq", base=0, frames=1, fps=30.0, activity="",
                            actweight=0.0, flags=0, grid=base_grid, autolayers=("delta_seq",))
    delta_seq = mdl_skel.Seq(label="delta_seq", base=0, frames=1, fps=30.0, activity="",
                             actweight=0.0, flags=rc.FLAG_DELTA, grid=delta_grid,
                             autolayers=())
    owner.sequences = {"base_seq": base_seq, "delta_seq": delta_seq}
    # anim 0 (base): both bones exactly at the owner's own bind. anim 1 (delta): an additive
    # +0.5 on X for the target bone only.
    frame_table = {
        0: [[((0.0, 0.0, 0.0), identity), ((1.0, 0.0, 0.0), identity)]],
        1: [[((0.0, 0.0, 0.0), identity), ((0.5, 0.0, 0.0), identity)]],
    }
    owner.frames = lambda index: frame_table[index]

    body = _FakeModel(body_bones)

    class _StubCorpus:
        def model(self, name):
            return {"owner_stem": owner, "body_stem": body}[name]

    return _StubCorpus()


def test_the_additive_delta_is_remapped_once_with_the_base_not_twice():
    corpus = _corpus()
    channels = [{"owner": "owner_stem", "label": "base_seq", "cycle": 0.0, "weight": 1.0}]
    _body, locals_ = rc.compose(corpus, "body_stem", channels, {})
    # Owner-space closure: bind(1.0) -[base, replace]-> 1.0 -[delta, additive]-> 1.5.
    # Remapped ONCE (translate: b - a = 0.0 - 1.0 = -1.0): 1.5 + (-1.0) = 0.5.
    assert locals_[1][0][0] == pytest.approx(0.5, abs=1e-6)
    # The wrong, per-layer answer (offset applied to the base AND to the raw delta) would
    # read -0.5 here; guard against silently regressing back to it.
    assert locals_[1][0][0] != pytest.approx(-0.5, abs=1e-6)
