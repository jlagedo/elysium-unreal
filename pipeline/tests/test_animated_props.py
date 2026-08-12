"""Animated-prop export contract: the RLE clamp, the motion filter, the index projection.

Every buffer here is synthesised in-code, so nothing depends on the user's game install.
"""

from __future__ import annotations

import struct
import unittest
from unittest import mock

from elysium_pipeline.exporters import npc_export
from elysium_pipeline.formats import mdl_skel


def _run(valid_keys, total, stored=None):
    """One `mstudioanimvalue_t` run: byte valid, byte total, then the int16 keys.

    `stored` truncates how many keys are actually written, which is how a shipped model whose
    final run overshoots the end of the file is reproduced.
    """
    keys = valid_keys if stored is None else valid_keys[:stored]
    return bytes([len(valid_keys), total]) + struct.pack(f"<{len(keys)}h", *keys)


class RleChannelTests(unittest.TestCase):
    def test_explicit_keys_decode_exactly(self) -> None:
        self.assertEqual(mdl_skel._rle_channel(_run([10, 20, 30], 3), 0, 3), [10, 20, 30])

    def test_frames_past_valid_hold_the_last_key(self) -> None:
        # Frames [0,valid) take the explicit keys; [valid,total) clamp to the last one.
        self.assertEqual(mdl_skel._rle_channel(_run([7, 9], 5), 0, 5), [7, 9, 9, 9, 9])

    def test_two_runs_are_concatenated(self) -> None:
        buffer = _run([1, 2], 2) + _run([5], 2)
        self.assertEqual(mdl_skel._rle_channel(buffer, 0, 4), [1, 2, 5, 5])

    def test_a_run_claiming_more_keys_than_the_file_stores_is_clamped(self) -> None:
        # `stage_light.mdl` is 11,400 bytes and a literal walk asks for 11,419. Retail's
        # ExtractAnimValue stops at the frame it wants and never notices; decoding every frame
        # eagerly has to clamp rather than raise.
        buffer = _run([3, 4, 5, 6], 4, stored=2)
        self.assertEqual(mdl_skel._rle_channel(buffer, 0, 4), [3, 4, 4, 4])

    def test_a_channel_that_ends_early_pads_to_numframes(self) -> None:
        self.assertEqual(mdl_skel._rle_channel(_run([2], 1), 0, 4), [2, 2, 2, 2])

    def test_a_truncated_header_terminates(self) -> None:
        self.assertEqual(mdl_skel._rle_channel(b"\x02", 0, 3), [0, 0, 0])

    def test_zero_valid_does_not_index_an_empty_key_list(self) -> None:
        self.assertEqual(mdl_skel._rle_channel(_run([], 3), 0, 3), [0, 0, 0])

    def test_a_zero_length_run_cannot_loop_forever(self) -> None:
        self.assertEqual(mdl_skel._rle_channel(_run([1], 0), 0, 2), [0, 0])


class _Seq:
    def __init__(self, frames):
        self.frames = frames


class HasAnimationTests(unittest.TestCase):
    def test_a_single_frame_sequence_is_not_animation(self) -> None:
        # `stage_light`, `lampfloor`, `glassa`, `junkyardcraneb`, `bottleb` and `bottlec` each
        # declare exactly one 1-frame `idle` while authoring LoopSequence.
        with mock.patch.object(mdl_skel, "local_sequences", return_value=[_Seq(1)]):
            self.assertFalse(npc_export.has_animation(b""))

    def test_any_multi_frame_sequence_qualifies(self) -> None:
        # `clamp`'s `idle` is one frame beside its real 45-frame open/close, so the test is "any",
        # not "every".
        with mock.patch.object(mdl_skel, "local_sequences", return_value=[_Seq(1), _Seq(45)]):
            self.assertTrue(npc_export.has_animation(b""))

    def test_a_model_with_no_sequences_is_not_animation(self) -> None:
        with mock.patch.object(mdl_skel, "local_sequences", return_value=[]):
            self.assertFalse(npc_export.has_animation(b""))

    def test_an_undecodable_model_is_not_animation(self) -> None:
        with mock.patch.object(mdl_skel, "local_sequences", side_effect=struct.error("truncated")):
            self.assertFalse(npc_export.has_animation(b""))


class AnimatedPropIndexRowTests(unittest.TestCase):
    # `drknobantique`'s real shape: declaration order puts `idle` first, alphabetical order would
    # put `handle_locked` first, and retail's rest pose is sequence index 0.
    RECORD = {
        "glb": "animated_props/drknobantique.glb",
        "model": "models/scenery/doorknoba/drknobantique.mdl",
        "bones": 2,
        "clips": {
            "idle": {"activity": "", "weight": 0, "flags": 1, "frames": 16, "fps": 15.0},
            "handle_locked": {"activity": "", "weight": 0, "flags": 0, "frames": 16, "fps": 15.0},
            "handle_unlocked": {"activity": "", "weight": 0, "flags": 0, "frames": 16, "fps": 15.0},
        },
    }

    def test_declaration_order_is_preserved(self) -> None:
        row = npc_export.animated_prop_index_row(self.RECORD)
        self.assertEqual([c["name"] for c in row["clips"]],
                         ["idle", "handle_locked", "handle_unlocked"])

    def test_each_clip_carries_its_ordinal_and_selection_keys(self) -> None:
        row = npc_export.animated_prop_index_row(self.RECORD)
        self.assertEqual([c["index"] for c in row["clips"]], [0, 1, 2])
        first = row["clips"][0]
        self.assertEqual(first["flags"], 1)          # STUDIO_LOOPING
        self.assertEqual(first["activity"], "")
        self.assertEqual(first["frames"], 16)
        self.assertEqual(first["fps"], 15.0)

    def test_optional_sidecars_are_omitted_when_absent(self) -> None:
        row = npc_export.animated_prop_index_row(self.RECORD)
        self.assertNotIn("procedural", row)
        self.assertNotIn("blends", row)
        self.assertEqual(row["split_bones"], [])

    def test_a_clipless_record_projects_an_empty_list(self) -> None:
        row = npc_export.animated_prop_index_row({"glb": "g", "model": "m"})
        self.assertEqual(row["clips"], [])

    def test_a_bounds_radius_reaches_the_runtime_row(self) -> None:
        record = dict(self.RECORD, clips={
            "idle": dict(self.RECORD["clips"]["idle"], bounds_radius_m=22.388)})
        row = npc_export.animated_prop_index_row(record)
        self.assertEqual(row["clips"][0]["bounds_radius_m"], 22.388)


def _seq(label, bbmin, bbmax):
    return mdl_skel.Seq(label=label, base=0, frames=16, fps=30.0, activity="",
                        actweight=0, flags=0, bbmin=bbmin, bbmax=bbmax)


class ClipBoundsRadiusTests(unittest.TestCase):
    """What a clip declares about its own reach, reconciled with what baked.

    `cin_sheriff_sword`'s `scene` is the shape these guard: a 2 m mesh whose sequence bbox
    spans 881 Source units because the rig carries the sword across the courtroom.
    """

    SWORD = _seq("scene", (-881.4, -162.6, -6.7), (0.0, 89.3, 166.2))

    def test_the_authored_box_reduces_to_its_largest_coordinate(self) -> None:
        self.assertAlmostEqual(npc_export.authored_radius_m(self.SWORD), 881.4 * 0.0254, places=6)

    def test_the_authored_radius_wins_when_it_covers_the_bake(self) -> None:
        # The real case: studiomdl's box sits a little outside the extent it was computed from.
        self.assertAlmostEqual(npc_export.clip_bounds_radius_m(self.SWORD, 21.833),
                               881.4 * 0.0254, places=6)

    def test_a_zeroed_descriptor_falls_back_to_the_bake(self) -> None:
        # Every model's header ViewBBMin/ViewBBMax is (0,0,0) in this corpus, so a sequence box
        # that was never filled in is a shape the export has to survive rather than trust.
        blank = _seq("scene", (0.0, 0.0, 0.0), (0.0, 0.0, 0.0))
        self.assertEqual(npc_export.authored_radius_m(blank), 0.0)
        self.assertAlmostEqual(npc_export.clip_bounds_radius_m(blank, 9.99), 9.99, places=6)

    def test_an_authored_radius_short_of_the_bake_loses(self) -> None:
        short = _seq("scene", (-1.0, 0.0, 0.0), (1.0, 0.0, 0.0))
        self.assertAlmostEqual(npc_export.clip_bounds_radius_m(short, 4.0), 4.0, places=6)

    def test_the_key_is_absent_until_it_has_been_reconciled(self) -> None:
        # Presence is the promise that the number covers the geometry; a bank or NPC clip, which
        # nothing measures, must not look like it carries one.
        self.assertNotIn("bounds_radius_m", npc_export._clip_meta(self.SWORD))
        self.assertEqual(npc_export._clip_meta(self.SWORD, 22.38812)["bounds_radius_m"], 22.3881)


class ClipExtentTests(unittest.TestCase):
    """The measured half of the same question: how far a bone chain actually reaches."""

    def test_a_rotated_parent_carries_its_child_out(self) -> None:
        bones = [mdl_skel.Bone(index=0, parent=-1), mdl_skel.Bone(index=1, parent=0)]
        # Frame 0 rests; frame 1 turns the root a quarter turn about Z, which swings the child's
        # 100-unit local offset onto +Y. Either way the reach is 100 units = 2.54 m.
        rest = [((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)), ((100.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))]
        turn = [((0.0, 0.0, 0.0), (0.0, 0.0, 0.7071067811865476, 0.7071067811865476)),
                ((100.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))]
        with mock.patch.object(mdl_skel, "read_anim", return_value=[rest, turn]):
            self.assertAlmostEqual(mdl_skel.clip_extent(b"", bones, 0, 2), 100.0 * 0.0254, places=6)

    def test_a_clip_with_no_bones_or_no_frames_claims_nothing(self) -> None:
        self.assertEqual(mdl_skel.clip_extent(b"", [], 0, 16), 0.0)
        self.assertEqual(mdl_skel.clip_extent(b"", [mdl_skel.Bone(index=0, parent=-1)], 0, 0), 0.0)

    def test_the_vectorised_rotation_matches_the_scalar_one(self) -> None:
        import numpy as np
        quats = np.array([[0.0, 0.0, 0.0, 1.0],
                          [0.5, -0.5, 0.5, 0.5],
                          [0.0, 0.0, 0.7071067811865476, 0.7071067811865476]])
        stacked = mdl_skel.rot_matrices(quats)
        for i, q in enumerate(quats):
            np.testing.assert_allclose(stacked[i], mdl_skel.rot_matrix(q), atol=1e-12)


if __name__ == "__main__":
    unittest.main()
