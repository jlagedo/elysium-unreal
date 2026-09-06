"""The `.eskm` "SKEL" section's single-rooting of a forked skeleton, and its optional
reference-pose override.

Unreal's reference skeleton asserts on a second parent-less bone. `unreal_bones` resolves a
fork (seven bodies across the exported corpus, plus the wield corpus's `w_f_severed_arm` --
`regular_cop` and `prophet` fork on a single stray, `brian` on four, including a name that
recurs verbatim and a pair distinguished only by studiomdl's `[2]` duplicate-instance prefix)
onto one of its OWN bones -- the parent-less bone with the largest descendant subtree -- rather
than splicing a synthetic bone above it: a synthetic bone at index 0 is a bone no animation bank
names, and Unreal's skeleton remapper (`FSkeletonRemapping::GenerateMapping`) forces bone 0 onto
bone 0 by position regardless of name, so a shared bank's real `Bip01` track would land on the
synthetic bone and the body's own `Bip01` would keep its static bind. This covers that
resolution -- root choice, model-space-preserving reparenting, topological order, and
`bone_map`'s permutation contract, over both a single stray and a multi-stray/duplicate-name
skeleton -- plus `_ref_pose_rows`, the mechanism a caller (the wielded-weapon bake) uses to
substitute a different local transform per bone, including over a reparented row.

Every skeleton here is synthesised in-code; nothing depends on the user's game install.
"""

from __future__ import annotations

import struct

import numpy as np

import pytest

from elysium_pipeline.skeletal_stage import payload as UEK
from elysium_pipeline.skeletal_stage.cinematics import actor_rows
from elysium_pipeline.formats import mdl_skel
IDENTITY_Q = (0.0, 0.0, 0.0, 1.0)
#: A 90-degree rotation about Z, used wherever a test needs a root whose orientation actually
#: moves a reparented child's local transform -- an identity root would let a reparenting bug
#: that drops the rotation term entirely pass unnoticed.
QUAT_Z90 = (0.0, 0.0, 0.70710678, 0.70710678)


def bone(index, name, parent, pos=(0.0, 0.0, 0.0), quat=IDENTITY_Q):
    """One `mdl_skel.Bone` carrying only the fields `unreal_bones`/`_skel_section` read."""
    return mdl_skel.Bone(index=index, name=name, parent=parent, pos=pos, quat=quat)


def _parse_skel(blob):
    """(count, [(name, parent, transform_start, transform_end)]).

    `transform_start:transform_end` is the row's `f32 pos[3]` + `f32 quat[4]` -- the seven
    floats a reference-pose override replaces -- so a test can slice exactly that range out of
    the blob and diff the rest byte for byte."""
    (count,) = struct.unpack_from("<I", blob, 0)
    offset = 4
    rows = []
    for _ in range(count):
        (length,) = struct.unpack_from("<I", blob, offset)
        name = blob[offset + 4:offset + 4 + length].decode("utf-8")
        offset += 4 + length
        (parent,) = struct.unpack_from("<i", blob, offset)
        offset += 4
        start = offset
        offset += 12 + 16
        rows.append((name, parent, start, offset))
    return count, rows


def _world_transform(root_pos, root_quat, local_pos, local_quat):
    """`(local_pos, local_quat)` composed under `(root_pos, root_quat)` by ordinary FK, through
    `mdl_skel.rot_matrix` -- deliberately NOT the quaternion-sandwich helpers
    `UE_mdl_skeletal._reparent_local` is built from, so this verifies the RESULT against an
    independent implementation rather than re-deriving it the same way."""
    r = mdl_skel.rot_matrix(root_quat)
    world_pos = np.asarray(root_pos, dtype=np.float64) + r @ np.asarray(local_pos, dtype=np.float64)
    world_r = r @ mdl_skel.rot_matrix(local_quat)
    return world_pos, world_r


def _assert_reproduces_model_space(root_pos, root_quat, local_pos, local_quat,
                                   expected_pos, expected_quat, tol=1e-6):
    """The recomposed (root, local) pair lands back on `(expected_pos, expected_quat)`, checked
    as a rotation MATRIX (not raw quaternion components, which carry a sign ambiguity a
    numerically-equal rotation can legitimately differ on)."""
    world_pos, world_r = _world_transform(root_pos, root_quat, local_pos, local_quat)
    expected_r = mdl_skel.rot_matrix(expected_quat)
    assert np.allclose(world_pos, np.asarray(expected_pos, dtype=np.float64), atol=tol), (
        f"position {world_pos} != expected {expected_pos}")
    assert np.allclose(world_r, expected_r, atol=tol), (
        f"rotation {world_r} != expected {expected_r}")


def test_ref_pose_none_matches_no_override_at_all() -> None:
    bones = [
        bone(0, "root", -1, (0.0, 0.0, 0.0), IDENTITY_Q),
        bone(1, "child", 0, (1.0, 2.0, 3.0), (0.1, 0.2, 0.3, 0.9)),
    ]
    rows, bone_map, _reparented = UEK.unreal_bones(bones)

    plain = UEK._skel_section(rows)
    explicit_none = UEK._skel_section(UEK._ref_pose_rows(rows, bone_map, None, "ctx"))

    assert plain == explicit_none


def test_only_the_overridden_bones_seven_floats_change() -> None:
    bones = [
        bone(0, "root", -1, (0.0, 0.0, 0.0), IDENTITY_Q),
        bone(1, "mid", 0, (1.0, 2.0, 3.0), (0.1, 0.2, 0.3, 0.9)),
        bone(2, "tip", 1, (4.0, 5.0, 6.0), (0.0, 0.0, 0.0, 1.0)),
    ]
    rows, bone_map, _reparented = UEK.unreal_bones(bones)
    new_pos, new_quat = (7.0, 8.0, 9.0), (0.0, 0.7071, 0.0, 0.7071)
    ref_pose = [
        (bones[0].pos, bones[0].quat),
        (bones[1].pos, bones[1].quat),
        (new_pos, new_quat),
    ]

    without = UEK._skel_section(rows)
    overridden = UEK._skel_section(UEK._ref_pose_rows(rows, bone_map, ref_pose, "ctx"))

    assert len(without) == len(overridden)
    count_a, rows_a = _parse_skel(without)
    count_b, rows_b = _parse_skel(overridden)
    assert count_a == count_b
    # Names and parents, and therefore every row's byte offsets, are identical -- only the
    # transform range at the overridden row's offset may differ.
    assert [(n, p) for n, p, _s, _e in rows_a] == [(n, p) for n, p, _s, _e in rows_b]

    target = 2  # "tip" -- index 2 in both `bones` and the single-root `rows`.
    for i in range(len(rows_a)):
        s_a, e_a = rows_a[i][2], rows_a[i][3]
        s_b, e_b = rows_b[i][2], rows_b[i][3]
        assert (s_a, e_a) == (s_b, e_b)
        if i == target:
            assert without[s_a:e_a] != overridden[s_b:e_b]
            expected = (struct.pack("<3f", *UEK._conv_pos(new_pos))
                       + struct.pack("<4f", *UEK._conv_quat(new_quat)))
            assert overridden[s_b:e_b] == expected
        else:
            assert without[s_a:e_a] == overridden[s_b:e_b]

    # Masking the one differing range out of both blobs leaves them byte-identical --
    # "differs ONLY in that bone's seven transform floats" stated as a whole-file check
    # rather than only a per-row one.
    s, e = rows_a[target][2], rows_a[target][3]
    assert without[:s] + without[e:] == overridden[:s] + overridden[e:]


# SingleRootedRegressionTests
# A skeleton that was never forked takes the identity path exactly as before.

def test_single_rooted_rows_and_bone_map_are_untouched() -> None:
    bones = [
        bone(0, "root", -1, (0.0, 0.0, 0.0), IDENTITY_Q),
        bone(1, "mid", 0, (1.0, 2.0, 3.0), (0.1, 0.2, 0.3, 0.9)),
        bone(2, "tip", 1, (4.0, 5.0, 6.0), (0.0, 0.0, 0.0, 1.0)),
    ]
    rows, bone_map, reparented = UEK.unreal_bones(bones)

    assert rows == [(b.name, b.parent, b.pos, b.quat) for b in bones]
    assert bone_map == [0, 1, 2]
    assert reparented == {}


def test_no_synthetic_root_is_emitted() -> None:
    bones = [
        bone(0, "root_a", -1, (1.0, 1.0, 1.0)),
        bone(1, "root_b", -1, (2.0, 2.0, 2.0)),
    ]
    rows, _bone_map, _reparented = UEK.unreal_bones(bones)

    names = [name for name, _p, _pos, _q in rows]
    assert "__elysium_skeleton_root" not in names
    assert len(rows) == len(bones)
    assert not hasattr(UEK, "SYNTHETIC_ROOT")


def test_root_is_the_largest_subtree_not_the_first_bone() -> None:
    # Mirrors `regular_cop`: bone 0 is a childless leaf root, bone 1 is the real root with
    # descendants. "First parent-less bone" picks wrong here.
    bones = [
        bone(0, "tongue", -1, (5.0, 5.0, 5.0)),
        bone(1, "Bip01", -1, (0.0, 0.0, 0.0), QUAT_Z90),
        bone(2, "Bip01 Pelvis", 1, (0.0, 1.0, 0.0)),
        bone(3, "Bip01 Spine", 2, (0.0, 1.0, 0.0)),
    ]
    rows, bone_map, reparented = UEK.unreal_bones(bones)

    assert rows[0][0] == "Bip01"
    assert rows[0][1] == (-1)
    assert bone_map[1] == 0
    assert reparented == {0: 1}  # tongue (original 0) folded onto Bip01 (original 1)


def test_root_stays_put_when_it_is_already_the_largest_subtree() -> None:
    # Mirrors `prophet`: bone 0 is the real root, the fork (a childless leaf) comes last.
    bones = [
        bone(0, "Bip01", -1, (0.0, 0.0, 0.0), QUAT_Z90),
        bone(1, "Bip01 Spine", 0, (0.0, 1.0, 0.0)),
        bone(2, "Tube01", -1, (3.0, 3.0, 3.0)),
    ]
    rows, bone_map, reparented = UEK.unreal_bones(bones)

    assert rows[0][0] == "Bip01"
    assert bone_map[0] == 0
    assert reparented == {2: 0}


def test_ties_break_on_lowest_original_index() -> None:
    # Two childless roots -- equal (zero) subtree size -- must resolve deterministically.
    bones = [
        bone(0, "root_a", -1, (1.0, 1.0, 1.0)),
        bone(1, "root_b", -1, (2.0, 2.0, 2.0)),
    ]
    _rows, bone_map, reparented = UEK.unreal_bones(bones)

    assert bone_map[0] == 0
    assert reparented == {1: 0}


def test_reparented_stray_reproduces_its_original_model_space_transform() -> None:
    bones = [
        bone(0, "Bip01", -1, (10.0, -5.0, 2.0), QUAT_Z90),
        bone(1, "Bip01 Spine", 0, (0.0, 1.0, 0.0)),
        bone(2, "Bip01 Spine1", 1, (0.0, 1.0, 0.0)),
        bone(3, "prop_root", -1, (-3.0, 4.0, 7.0), (0.0, 0.38268343, 0.0, 0.92387953)),
    ]
    rows, bone_map, reparented = UEK.unreal_bones(bones)

    assert reparented == {3: 0}
    root_slot, stray_slot = bone_map[0], bone_map[3]
    root_pos, root_quat = rows[root_slot][2], rows[root_slot][3]
    local_pos, local_quat = rows[stray_slot][2], rows[stray_slot][3]
    assert rows[stray_slot][1] == root_slot  # parent now points at the chosen root

    _assert_reproduces_model_space(root_pos, root_quat, local_pos, local_quat,
                                   expected_pos=bones[3].pos, expected_quat=bones[3].quat)


def test_parents_precede_children_and_bone_map_is_a_valid_permutation() -> None:
    bones = [
        bone(0, "leaf_a", -1, (9.0, 9.0, 9.0)),
        bone(1, "Bip01", -1, (0.0, 0.0, 0.0), QUAT_Z90),
        bone(2, "Bip01 Spine", 1, (0.0, 1.0, 0.0)),
        bone(3, "Bip01 Spine1", 2, (0.0, 1.0, 0.0)),
        bone(4, "stray_chain_root", -1, (5.0, 5.0, 5.0)),
        bone(5, "stray_chain_child", 4, (1.0, 0.0, 0.0)),
    ]
    rows, bone_map, reparented = UEK.unreal_bones(bones)

    # Every parent's emitted slot precedes its child's.
    for slot, (_name, parent, _pos, _quat) in enumerate(rows):
        if parent >= 0:
            assert parent < slot

    # `bone_map` is a bijection over every original StudioBone index.
    assert len(bone_map) == len(bones)
    assert sorted(bone_map) == list(range(len(bones)))

    # It round-trips: the row at `bone_map[i]` is bone `i`'s own row.
    for original, b in enumerate(bones):
        assert rows[bone_map[original]][0] == b.name

    assert reparented == {0: 1, 4: 1}
    # The chosen root (Bip01, 2 descendants) beats both leaf-adjacent roots (0 and 1
    # descendants respectively).
    assert bone_map[1] == 0


def test_four_strays_including_a_bracket_prefixed_near_duplicate_name() -> None:
    # `brian`: a biped root plus FOUR parent-less prop bones, one pair of which
    # ("lower_teeth" / "[2]lower_teeth") differ only by studiomdl's "[2]" duplicate-instance
    # prefix. The rig-name fold rewrites the bracket to an underscore, so the pair stays two
    # distinct names on the Unreal side rather than collapsing into one.
    bones = [
        bone(0, "Bip01", -1, (0.0, 0.0, 0.0), QUAT_Z90),
        bone(1, "Bip01 Spine", 0, (0.0, 1.0, 0.0)),
        bone(2, "Bip01 Spine1", 1, (0.0, 1.0, 0.0)),
        bone(3, "lower_teeth", -1, (1.0, 70.0, 3.0)),
        bone(4, "[2]upper_teeth", -1, (1.2, 70.0, 3.5)),
        bone(5, "[2]Sphere01", -1, (0.5, 68.0, 2.0)),
        bone(6, "[2]lower_teeth", -1, (1.1, 70.1, 3.1)),  # bone 3's name behind the prefix
    ]
    rows, bone_map, reparented = UEK.unreal_bones(bones)

    # The biped root (2 descendants) beats every childless prop root.
    assert bone_map[0] == 0
    assert reparented == {3: 0, 4: 0, 5: 0, 6: 0}

    # Every bone survives as its own row, INDEX-addressed -- the near-duplicate never
    # collapses bone 3 and bone 6 into one, and `bone_map` is still a full permutation.
    # Names come out FOLDED, because the fold is applied once at this boundary.
    assert len(rows) == len(bones)
    assert sorted(bone_map) == list(range(len(bones)))
    for original, b in enumerate(bones):
        assert rows[bone_map[original]][0] == UEK.rig_bone_name(b.name)
    emitted_names = {name.lower() for name, _p, _pos, _q in rows}
    assert len(emitted_names) == len(bones)

    # Every stray -- both "lower_teeth" bones included -- reproduces its OWN original
    # model-space transform under the chosen root, independently of the others.
    root_slot = bone_map[0]
    root_pos, root_quat = rows[root_slot][2], rows[root_slot][3]
    for original in (3, 4, 5, 6):
        slot = bone_map[original]
        assert rows[slot][1] == root_slot
        _assert_reproduces_model_space(
            root_pos, root_quat, rows[slot][2], rows[slot][3],
            expected_pos=bones[original].pos, expected_quat=bones[original].quat)

    for slot, (_name, parent, _pos, _quat) in enumerate(rows):
        if parent >= 0:
            assert parent < slot


def test_a_verbatim_duplicate_bone_name_is_a_fatal_export_error() -> None:
    # Two bones sharing one rig name cannot both bind: skeleton, data-model control and bank
    # binding all address a bone by name, so the second would silently hold its bind. No
    # installed model carries one (whole-corpus census: 0 of 1,130), so the export refuses.
    bones = [
        bone(0, "Bip01", -1, (0.0, 0.0, 0.0), QUAT_Z90),
        bone(1, "lower_teeth", -1, (1.0, 70.0, 3.0)),
        bone(2, "lower_teeth", -1, (1.1, 70.1, 3.1)),
    ]
    with pytest.raises(SystemExit):
        UEK.unreal_bones(bones)


def test_low_z_stray_resolves_the_same_as_a_head_height_one() -> None:
    # `ash`: the fork is a single prop root at ankle height (z=14.5 in this corpus's units),
    # not a head-height one like `brian`'s -- selection is subtree-size-only and must not care
    # about a stray's position at all.
    bones = [
        bone(0, "Bip01", -1, (0.0, 0.0, 0.0), QUAT_Z90),
        bone(1, "Bip01 Spine", 0, (0.0, 1.0, 0.0)),
        bone(2, "[2]GeoSphere02", -1, (2.0, -3.0, 14.5)),
    ]
    rows, bone_map, reparented = UEK.unreal_bones(bones)

    assert bone_map[0] == 0
    assert reparented == {2: 0}
    root_slot, stray_slot = bone_map[0], bone_map[2]
    root_pos, root_quat = rows[root_slot][2], rows[root_slot][3]
    _assert_reproduces_model_space(
        root_pos, root_quat, rows[stray_slot][2], rows[stray_slot][3],
        expected_pos=bones[2].pos, expected_quat=bones[2].quat)


# CinematicMultiRootTests
# `_cinematic_rows` threads the same resolution through its subset + rename remap.

def test_forked_actor_subset_resolves_without_a_synthetic_root() -> None:
    sub = [
        bone(10, "Bip02", -1, (1.0, 1.0, 1.0)),
        bone(11, "Bip02 Spine", 10, (0.0, 1.0, 0.0)),
        bone(12, "Bip02 Spine1", 11, (0.0, 1.0, 0.0)),
        bone(13, "Bip02 Prop", -1, (5.0, 5.0, 5.0)),  # a second, forked root
    ]
    rows, order, reparented = actor_rows(sub, "Bip02")
    # The forked prop resolved onto the actor's own root, keyed by ORIGINAL bone index.
    assert reparented == {13: 10}

    names = [name for name, _p, _pos, _q in rows]
    assert "__elysium_skeleton_root" not in names
    assert len(rows) == len(sub)
    assert rows[order[10]][0] == "Bip01"  # the prefix folds Bip02 -> Bip01
    assert rows[order[10]][1] == (-1)
    # order round-trips every original StudioBone index into a valid emitted slot.
    assert sorted(order.values()) == list(range(len(sub)))
    for slot, (_name, parent, _pos, _quat) in enumerate(rows):
        if parent >= 0:
            assert parent < slot


def test_override_on_a_reparented_stray_reproduces_the_overrides_own_model_space_pose() -> None:
    bones = [
        bone(0, "root_a", -1, (1.0, 1.0, 1.0), QUAT_Z90),
        bone(1, "root_b", -1, (2.0, 2.0, 2.0)),
    ]
    rows, bone_map, reparented = UEK.unreal_bones(bones)
    assert reparented == {1: 0}

    # A caller-supplied override (e.g. the wielded-weapon bake's clip frame 0) with values
    # that differ from the bind on BOTH bones -- so a bug that silently fell back to the
    # bind-derived local would be caught.
    override_root = ((-4.0, 3.0, 0.0), (0.0, 0.0, 0.38268343, 0.92387953))
    override_stray = ((6.0, -1.0, 2.0), (0.70710678, 0.0, 0.0, 0.70710678))
    ref_pose = [override_root, override_stray]

    out = UEK._ref_pose_rows(rows, bone_map, ref_pose, "ctx", reparented)

    root_slot, stray_slot = bone_map[0], bone_map[1]
    assert out[root_slot][2:] == override_root
    stray_row = out[stray_slot]
    assert stray_row[1] == root_slot

    _assert_reproduces_model_space(
        override_root[0], override_root[1], stray_row[2], stray_row[3],
        expected_pos=override_stray[0], expected_quat=override_stray[1])


def test_zero_quaternion_root_fails_loudly() -> None:
    bones = [
        bone(0, "root_a", -1, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 0.0)),
        bone(1, "root_b", -1, (1.0, 0.0, 0.0)),
    ]
    with pytest.raises(ValueError) as ctx:
        UEK.unreal_bones(bones)
    assert "root_b" in str(ctx.value)
    assert "root_a" in str(ctx.value)


def test_shorter_ref_pose_fails_loudly() -> None:
    bones = [bone(0, "root", -1), bone(1, "child", 0)]
    rows, bone_map, _reparented = UEK.unreal_bones(bones)

    with pytest.raises(ValueError) as ctx:
        UEK._ref_pose_rows(rows, bone_map, [((0.0, 0.0, 0.0), IDENTITY_Q)],
                           "models/weapons/w_test.mdl")

    message = str(ctx.value)
    assert "models/weapons/w_test.mdl" in message
    assert "1" in message   # entries given
    assert "2" in message   # entries expected


def _surface(pos, nrm, joints, weights, tris=()):
    """The minimal `decode_skinned`-shaped surface `_reskin_surfaces` reads."""
    return {"pos": list(pos), "nrm": list(nrm), "uv": [(0.0, 0.0)] * len(pos),
            "joints": [list(j) for j in joints], "weights": [list(w) for w in weights],
            "tris": list(tris)}


def _rigid_move(bind_ms, ref_ms, point):
    """`M_ref * M_bind^-1` applied to a model-space point, via the same independent numpy path
    `_world_transform` uses -- never the quaternion helpers under test."""
    bind_pos, bind_r = bind_ms
    ref_pos, ref_r = ref_ms
    local = bind_r.T @ (np.asarray(point, dtype=np.float64) - bind_pos)
    return ref_r @ local + ref_pos


BONES = [
    bone(0, "root", -1, (0.0, 0.0, 0.0), IDENTITY_Q),
    bone(1, "grip", 0, (1.0, 2.0, 3.0), QUAT_Z90),
]

REF_POSE = [
    ((0.0, 0.0, 0.0), IDENTITY_Q),
    ((4.0, -1.0, 0.5), (0.38268343, 0.0, 0.0, 0.92387953)),  # 45 deg about X, moved
]


# RefPoseGeometryTests
# `_reskin_surfaces`: the MESH geometry follows the SKEL section's reference-pose override.
#
# The vertices in the container are meaningful only against the reference pose they are stored
# with (`CalculateInvRefMatrices` derives the skinning inverses from the SKEL rows), so an
# override that moved the skeleton but not the geometry bakes a self-contradictory file -- the
# wield corpus's measured placement defect. Expectations here are composed through
# `_world_transform`'s numpy matrices, independent of the quaternion helpers under test.

def _model_space(locals_):
    out = []
    for index, b in enumerate(BONES):
        pos, quat = locals_[index]
        if b.parent < 0:
            out.append((np.asarray(pos, dtype=np.float64), mdl_skel.rot_matrix(quat)))
            continue
        parent_pos, parent_r = out[b.parent]
        out.append((parent_pos + parent_r @ np.asarray(pos, dtype=np.float64),
                    parent_r @ mdl_skel.rot_matrix(quat)))
    return out


def test_identity_override_leaves_geometry_unchanged() -> None:
    bind = [(b.pos, b.quat) for b in BONES]
    surface = _surface(pos=[(0.5, 0.25, -1.0)], nrm=[(0.0, 1.0, 0.0)],
                       joints=[(1, 0, 0)], weights=[(1.0, 0.0, 0.0)])
    surfaces = {"mat": surface}

    UEK._reskin_surfaces(surfaces, BONES, bind, "ctx")

    assert np.allclose(surface["pos"][0], (0.5, 0.25, -1.0), atol=1e-6)
    assert np.allclose(surface["nrm"][0], (0.0, 1.0, 0.0), atol=1e-6)


def test_single_influence_vertex_moves_rigidly_with_its_bone() -> None:
    vertex = (0.5, 0.25, -1.0)
    surface = _surface(pos=[vertex], nrm=[(0.0, 1.0, 0.0)],
                       joints=[(1, 0, 0)], weights=[(1.0, 0.0, 0.0)])
    bind_ms = _model_space([(b.pos, b.quat) for b in BONES])
    ref_ms = _model_space(REF_POSE)

    UEK._reskin_surfaces({"mat": surface}, BONES, REF_POSE, "ctx")

    expected = _rigid_move(bind_ms[1], ref_ms[1], vertex)
    assert np.allclose(surface["pos"][0], expected, atol=1e-6), f"{surface['pos'][0]} != {expected}"
    # The normal takes the rotation alone: same move, zero translation.
    expected_nrm = _rigid_move(bind_ms[1], ref_ms[1], (0.0, 1.0, 0.0)) \
        - _rigid_move(bind_ms[1], ref_ms[1], (0.0, 0.0, 0.0))
    assert np.allclose(surface["nrm"][0], expected_nrm, atol=1e-6)
    assert float(np.linalg.norm(surface["nrm"][0])) == pytest.approx(1.0, abs=1e-6)


def test_blended_vertex_interpolates_its_influences() -> None:
    vertex = (2.0, 0.0, 1.0)
    surface = _surface(pos=[vertex], nrm=[(0.0, 0.0, 1.0)],
                       joints=[(0, 1, 0)], weights=[(0.25, 0.75, 0.0)])
    bind_ms = _model_space([(b.pos, b.quat) for b in BONES])
    ref_ms = _model_space(REF_POSE)

    UEK._reskin_surfaces({"mat": surface}, BONES, REF_POSE, "ctx")

    expected = (0.25 * _rigid_move(bind_ms[0], ref_ms[0], vertex)
                + 0.75 * _rigid_move(bind_ms[1], ref_ms[1], vertex))
    assert np.allclose(surface["pos"][0], expected, atol=1e-6), f"{surface['pos'][0]} != {expected}"


def test_weightless_vertex_binds_wholly_to_bone_zero() -> None:
    # Mirrors `ElysiumSkeletalBuild`'s own fallback (`Influences.Emplace(0, 1.0f)`), so the
    # restated geometry and the built mesh keep agreeing on what such a vertex rides.
    vertex = (1.0, 1.0, 1.0)
    surface = _surface(pos=[vertex], nrm=[(1.0, 0.0, 0.0)],
                       joints=[(1, 0, 0)], weights=[(0.0, 0.0, 0.0)])
    ref_pose = [((5.0, 0.0, 0.0), IDENTITY_Q), (BONES[1].pos, BONES[1].quat)]

    UEK._reskin_surfaces({"mat": surface}, BONES, ref_pose, "ctx")

    assert np.allclose(surface["pos"][0], (6.0, 1.0, 1.0), atol=1e-6)


def test_zero_authored_normal_stays_zero_for_the_geometric_fallback() -> None:
    surface = _surface(pos=[(0.5, 0.25, -1.0)], nrm=[(0.0, 0.0, 0.0)],
                       joints=[(1, 0, 0)], weights=[(1.0, 0.0, 0.0)])

    UEK._reskin_surfaces({"mat": surface}, BONES, REF_POSE, "ctx")

    assert surface["nrm"][0] == (0.0, 0.0, 0.0)


def test_length_mismatch_fails_loudly() -> None:
    with pytest.raises(ValueError) as ctx:
        UEK._reskin_surfaces({}, BONES, [((0.0, 0.0, 0.0), IDENTITY_Q)],
                             "models/weapons/w_short.mdl")

    assert "models/weapons/w_short.mdl" in str(ctx.value)


def test_zero_quaternion_in_the_override_fails_loudly() -> None:
    ref_pose = [((0.0, 0.0, 0.0), IDENTITY_Q), ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 0.0))]

    with pytest.raises(ValueError) as ctx:
        UEK._reskin_surfaces({}, BONES, ref_pose, "models/weapons/w_zero.mdl")

    message = str(ctx.value)
    assert "models/weapons/w_zero.mdl" in message
    assert "grip" in message
