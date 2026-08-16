"""The `.eskm` "SKEL" section's optional reference-pose override.

`UE_mdl_skeletal.write_model` bakes every body's container bind pose into "SKEL" by default.
`_ref_pose_rows` is the mechanism a caller (the wielded-weapon bake) uses to substitute a
different local transform per bone -- the model's own clip at frame 0, computed elsewhere. This
covers only the mechanism: default byte-identity, a targeted override, the synthetic-root case a
forked skeleton takes through `unreal_bones`, and the loud failure on a length mismatch.

Every skeleton here is synthesised in-code; nothing depends on the user's game install.
"""

from __future__ import annotations

import struct
import unittest

from elysium_pipeline.exporters import UE_mdl_skeletal as UEK
from elysium_pipeline.formats import mdl_skel

IDENTITY_Q = (0.0, 0.0, 0.0, 1.0)


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


class DefaultByteIdentityTests(unittest.TestCase):
    def test_ref_pose_none_matches_no_override_at_all(self) -> None:
        bones = [
            bone(0, "root", -1, (0.0, 0.0, 0.0), IDENTITY_Q),
            bone(1, "child", 0, (1.0, 2.0, 3.0), (0.1, 0.2, 0.3, 0.9)),
        ]
        rows, bone_map = UEK.unreal_bones(bones)

        plain = UEK._skel_section(rows)
        explicit_none = UEK._skel_section(UEK._ref_pose_rows(rows, bone_map, None, "ctx"))

        self.assertEqual(plain, explicit_none)

    def test_ref_pose_rows_returns_the_same_rows_object_when_none(self) -> None:
        bones = [bone(0, "root", -1)]
        rows, bone_map = UEK.unreal_bones(bones)

        self.assertIs(UEK._ref_pose_rows(rows, bone_map, None, "ctx"), rows)


class OverrideAppliedTests(unittest.TestCase):
    def test_only_the_overridden_bones_seven_floats_change(self) -> None:
        bones = [
            bone(0, "root", -1, (0.0, 0.0, 0.0), IDENTITY_Q),
            bone(1, "mid", 0, (1.0, 2.0, 3.0), (0.1, 0.2, 0.3, 0.9)),
            bone(2, "tip", 1, (4.0, 5.0, 6.0), (0.0, 0.0, 0.0, 1.0)),
        ]
        rows, bone_map = UEK.unreal_bones(bones)
        new_pos, new_quat = (7.0, 8.0, 9.0), (0.0, 0.7071, 0.0, 0.7071)
        ref_pose = [
            (bones[0].pos, bones[0].quat),
            (bones[1].pos, bones[1].quat),
            (new_pos, new_quat),
        ]

        without = UEK._skel_section(rows)
        overridden = UEK._skel_section(UEK._ref_pose_rows(rows, bone_map, ref_pose, "ctx"))

        self.assertEqual(len(without), len(overridden))
        count_a, rows_a = _parse_skel(without)
        count_b, rows_b = _parse_skel(overridden)
        self.assertEqual(count_a, count_b)
        # Names and parents, and therefore every row's byte offsets, are identical -- only the
        # transform range at the overridden row's offset may differ.
        self.assertEqual([(n, p) for n, p, _s, _e in rows_a], [(n, p) for n, p, _s, _e in rows_b])

        target = 2  # "tip" -- index 2 in both `bones` and the single-root `rows`.
        for i in range(len(rows_a)):
            s_a, e_a = rows_a[i][2], rows_a[i][3]
            s_b, e_b = rows_b[i][2], rows_b[i][3]
            self.assertEqual((s_a, e_a), (s_b, e_b))
            if i == target:
                self.assertNotEqual(without[s_a:e_a], overridden[s_b:e_b])
                expected = (struct.pack("<3f", *UEK._conv_pos(new_pos))
                           + struct.pack("<4f", *UEK._conv_quat(new_quat)))
                self.assertEqual(overridden[s_b:e_b], expected)
            else:
                self.assertEqual(without[s_a:e_a], overridden[s_b:e_b])

        # Masking the one differing range out of both blobs leaves them byte-identical --
        # "differs ONLY in that bone's seven transform floats" stated as a whole-file check
        # rather than only a per-row one.
        s, e = rows_a[target][2], rows_a[target][3]
        self.assertEqual(without[:s] + without[e:], overridden[:s] + overridden[e:])


class MultiRootOverrideTests(unittest.TestCase):
    def test_override_lands_on_the_right_row_and_synthetic_root_is_untouched(self) -> None:
        # Two parent-less bones -- `unreal_bones` forks in the SYNTHETIC_ROOT branch.
        bones = [
            bone(0, "root_a", -1, (1.0, 1.0, 1.0), (0.0, 0.0, 0.0, 1.0)),
            bone(1, "root_b", -1, (2.0, 2.0, 2.0), (0.0, 0.0, 0.0, 1.0)),
        ]
        rows, bone_map = UEK.unreal_bones(bones)
        self.assertEqual(rows[0][0], UEK.SYNTHETIC_ROOT)
        self.assertEqual(bone_map, [1, 2])  # original index -> emitted index, root shifted by one

        new_pos, new_quat = (9.0, 9.0, 9.0), (0.5, 0.5, 0.5, 0.5)
        ref_pose = [(bones[0].pos, bones[0].quat), (new_pos, new_quat)]  # override original bone 1

        blob = UEK._skel_section(UEK._ref_pose_rows(rows, bone_map, ref_pose, "ctx"))
        count, parsed = _parse_skel(blob)
        self.assertEqual(count, 3)
        self.assertEqual([name for name, _p, _s, _e in parsed],
                         [UEK.SYNTHETIC_ROOT, "root_a", "root_b"])

        synthetic_start, synthetic_end = parsed[0][2], parsed[0][3]
        expected_identity = (struct.pack("<3f", *UEK._conv_pos((0.0, 0.0, 0.0)))
                             + struct.pack("<4f", *UEK._conv_quat((0.0, 0.0, 0.0, 1.0))))
        self.assertEqual(blob[synthetic_start:synthetic_end], expected_identity)

        root_a_start, root_a_end = parsed[1][2], parsed[1][3]
        expected_root_a = (struct.pack("<3f", *UEK._conv_pos(bones[0].pos))
                           + struct.pack("<4f", *UEK._conv_quat(bones[0].quat)))
        self.assertEqual(blob[root_a_start:root_a_end], expected_root_a)

        root_b_start, root_b_end = parsed[2][2], parsed[2][3]
        expected_root_b = (struct.pack("<3f", *UEK._conv_pos(new_pos))
                           + struct.pack("<4f", *UEK._conv_quat(new_quat)))
        self.assertEqual(blob[root_b_start:root_b_end], expected_root_b)


class LengthMismatchTests(unittest.TestCase):
    def test_shorter_ref_pose_fails_loudly(self) -> None:
        bones = [bone(0, "root", -1), bone(1, "child", 0)]
        rows, bone_map = UEK.unreal_bones(bones)

        with self.assertRaises(ValueError) as ctx:
            UEK._ref_pose_rows(rows, bone_map, [((0.0, 0.0, 0.0), IDENTITY_Q)],
                               "models/weapons/w_test.mdl")

        message = str(ctx.exception)
        self.assertIn("models/weapons/w_test.mdl", message)
        self.assertIn("1", message)   # entries given
        self.assertIn("2", message)   # entries expected

    def test_longer_ref_pose_fails_loudly(self) -> None:
        bones = [bone(0, "root", -1)]
        rows, bone_map = UEK.unreal_bones(bones)
        too_long = [((0.0, 0.0, 0.0), IDENTITY_Q), ((1.0, 1.0, 1.0), IDENTITY_Q)]

        with self.assertRaises(ValueError) as ctx:
            UEK._ref_pose_rows(rows, bone_map, too_long, "models/weapons/w_over.mdl")

        self.assertIn("models/weapons/w_over.mdl", str(ctx.exception))

    def test_multi_root_length_mismatch_also_fails_loudly(self) -> None:
        bones = [bone(0, "root_a", -1), bone(1, "root_b", -1)]
        rows, bone_map = UEK.unreal_bones(bones)

        with self.assertRaises(ValueError):
            UEK._ref_pose_rows(rows, bone_map, [((0.0, 0.0, 0.0), IDENTITY_Q)], "ctx")


if __name__ == "__main__":
    unittest.main()
