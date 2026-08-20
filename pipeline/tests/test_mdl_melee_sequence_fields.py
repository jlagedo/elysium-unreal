"""The melee half of `StudioSeqDesc`'s custom block, from the descriptor to the clip sidecar.

Every image here is synthesised in-code, so nothing depends on the user's game install.
`reach`@720 (`+0x2D0`) is the swing's own target-acquisition distance and
`szblockedreactionindex`@740 (`+0x2E4`) names the activity the attacker plays when that swing is
blocked; the enum slot the runtime reads at `+0x2E0` is `-1` on disk everywhere, filled by the
DLL at model load exactly as `activity`@12 is filled from `szactivitynameindex`@4
(`docs/vtmb/combat-and-damage.md`).

`numswingcentres`@708 (`+0x2C4`) and `swingcentreindex`@712 (`+0x2C8`) are the contact half of
the same block: the 188-byte records saying where on the limb the swing sweeps, over which slice
of the clip cycle, and which knockback the victim answers it with.
"""

from __future__ import annotations

import json
import os
import struct
import tempfile
import unittest
from unittest import mock

from elysium_pipeline.exporters import npc_export
from elysium_pipeline.formats import mdl_skel

SEQ_STRIDE = 764
ANIM_STRIDE = 72
BONE_STRIDE = 160
SWING_STRIDE = 188
FLT_MAX = struct.unpack("<f", struct.pack("<I", 0x7F7FFFFF))[0]

_ANIM_BASE = 512
#: Where an `swingcentreindex` read as a file offset instead of a descriptor-relative one would
#: land, kept clear of every other block so a decoy record can be planted there.
_DECOY_BASE = 764
_BONE_BASE = 1300
_SEQ_BASE = 2048
_SWING_BASE = _SEQ_BASE + SEQ_STRIDE
_SWING_CAPACITY = 4
_STRINGS = _SWING_BASE + _SWING_CAPACITY * SWING_STRIDE

BONES = ("Bip01", "Bip01 R Forearm", "Bip01 R Hand")

#: The four direction buckets of a shipped fists swing, in the record's own bucket order.
FISTS_KNOCKBACK = ("ACT_KNOCKBACK_SMALL_HIGH_RIGHT", "ACT_KNOCKBACK_SMALL_HIGH_BACK",
                   "ACT_KNOCKBACK_SMALL_HIGH_LEFT", "ACT_KNOCKBACK_SMALL_HIGH_FORWARD")


def _swing(*, window=(0.26, 0.47), bone=1, a=(25.0, 0.0, 0.0), b=(0.0, 0.0, 0.0),
           knockback=FISTS_KNOCKBACK, b8=3, ba=0):
    """One swing-record spec. A `knockback` entry is a name, `None` for an unset bucket, or a
    tuple of names for a bucket that lists several candidates."""
    return {"window": window, "bone": bone, "a": a, "b": b,
            "knockback": knockback, "b8": b8, "ba": ba}


def _image(*, label="fists_attack_JabLeft", activity="ACT_MELEE_ATTACK",
           reach=FLT_MAX, blocked="ACT_BLOCKED_REACTION_LEFT", blocked_index=None,
           swings=(), swing_count=None, swing_index=None, decoy_swing=False):
    """A one-sequence, one-animation v2531 image `local_sequences` can walk.

    `blocked_index` overrides the stored `szblockedreactionindex` outright, which is how the
    unset marker and an index off the end of the image are exercised. `swing_count` and
    `swing_index` override the swing array's declaration the same way. `decoy_swing` plants a
    second, differently-windowed record at the offset an absolute reading of `swingcentreindex`
    would resolve to.
    """
    image = bytearray(_STRINGS)
    struct.pack_into("<4sI", image, 0, b"IDST", 2531)
    struct.pack_into("<ii", image, 240, len(BONES), _BONE_BASE)  # NumBones / BoneIndex
    struct.pack_into("<ii", image, 264, 1, _ANIM_BASE)     # NumLocalAnims / LocalAnimIndex
    struct.pack_into("<ii", image, 272, 1, _SEQ_BASE)      # NumLocalSeq / LocalSeqIndex

    strings = bytearray()

    def intern(text):
        offset = _STRINGS + len(strings)
        strings.extend(text.encode("ascii") + b"\0")
        return offset

    for index, name in enumerate(BONES):
        bone_at = _BONE_BASE + index * BONE_STRIDE
        struct.pack_into("<i", image, bone_at, intern(name) - bone_at)

    struct.pack_into("<i", image, _ANIM_BASE, intern("jab") - _ANIM_BASE)
    struct.pack_into("<f", image, _ANIM_BASE + 4, 30.0)    # fps
    struct.pack_into("<i", image, _ANIM_BASE + 12, 21)     # numframes

    struct.pack_into("<i", image, _SEQ_BASE, intern(label) - _SEQ_BASE)
    struct.pack_into("<i", image, _SEQ_BASE + 4, intern(activity) - _SEQ_BASE)
    struct.pack_into("<i", image, _SEQ_BASE + 16, 3)       # actweight
    struct.pack_into("<i", image, _SEQ_BASE + 52, 1)       # numblends
    struct.pack_into("<h", image, _SEQ_BASE + 56, 0)       # anim[0][0]
    struct.pack_into("<ii", image, _SEQ_BASE + 572, 1, 1)  # groupsize
    struct.pack_into("<ii", image, _SEQ_BASE + 580, -1, -1)
    struct.pack_into("<f", image, _SEQ_BASE + 612, 0.2)    # fade
    struct.pack_into("<f", image, _SEQ_BASE + 720, reach)

    if blocked_index is None:
        blocked_index = intern(blocked) - _SEQ_BASE if blocked else -1
    struct.pack_into("<i", image, _SEQ_BASE + 740, blocked_index)

    def write_swing(record, spec):
        struct.pack_into("<2f", image, record, *spec["window"])
        struct.pack_into("<i", image, record + 8, spec["bone"])
        struct.pack_into("<3f", image, record + 0x0C, *spec["a"])
        struct.pack_into("<3f", image, record + 0x18, *spec["b"])
        # The 21 dwords the shipped records leave `-1`-filled.
        struct.pack_into("<21i", image, record + 0x24, *([-1] * 21))
        for bucket, names in enumerate(spec["knockback"]):
            if names is None:
                continue
            for slot, name in enumerate((names,) if isinstance(names, str) else names):
                struct.pack_into("<i", image, record + 0x78 + (bucket * 4 + slot) * 4,
                                 intern(name) - record)
        image[record + 0xB8] = spec["b8"]
        image[record + 0xBA] = spec["ba"]

    assert len(swings) <= _SWING_CAPACITY, swings
    struct.pack_into("<i", image, _SEQ_BASE + 708,
                     len(swings) if swing_count is None else swing_count)
    struct.pack_into("<i", image, _SEQ_BASE + 712,
                     (_SWING_BASE - _SEQ_BASE) if swing_index is None else swing_index)
    for index, spec in enumerate(swings):
        write_swing(_SWING_BASE + index * SWING_STRIDE, spec)
    if decoy_swing:
        write_swing(_DECOY_BASE, _swing(window=(0.9, 0.95), bone=2, a=(-1.0, -1.0, -1.0),
                                        knockback=("ACT_DECOY", None, None, None)))

    image.extend(strings)
    return bytes(image)


def _sequence(**kwargs):
    sequences = mdl_skel.local_sequences(_image(**kwargs))
    assert len(sequences) == 1, sequences
    return sequences[0]


class MeleeSequenceDescriptorTests(unittest.TestCase):
    def test_a_swing_carries_its_reach_and_blocked_reaction(self) -> None:
        seq = _sequence(reach=63.75)
        self.assertAlmostEqual(seq.reach, 63.75, places=4)
        self.assertEqual(seq.blocked_reaction, "ACT_BLOCKED_REACTION_LEFT")
        # The fields already read off the same descriptor are untouched by the two new ones.
        self.assertEqual(seq.activity, "ACT_MELEE_ATTACK")
        self.assertEqual((seq.actweight, seq.frames, seq.fps), (3, 21, 30.0))

    def test_the_unset_markers_read_as_no_value(self) -> None:
        # FLT_MAX at @720 and -1 at @740 are what 13,431 and 13,865 shipped descriptors carry.
        seq = _sequence(reach=FLT_MAX, blocked=None)
        self.assertIsNone(seq.reach)
        self.assertIsNone(seq.blocked_reaction)

    def test_a_zeroed_custom_block_states_no_reach(self) -> None:
        # Seven single-`idle` scenery and prop models zero the block instead of marking it unset,
        # and a zero query distance acquires nothing.
        self.assertIsNone(_sequence(reach=0.0, blocked=None).reach)

    def test_a_blocked_reaction_index_off_the_image_is_refused(self) -> None:
        self.assertIsNone(_sequence(blocked_index=1 << 20).blocked_reaction)

    def test_an_ordinary_sequence_states_neither(self) -> None:
        seq = _sequence(label="idle", activity="ACT_IDLE", reach=FLT_MAX, blocked=None)
        self.assertEqual((seq.reach, seq.blocked_reaction), (None, None))
        self.assertEqual(seq.swings, ())


class SwingContactRecordTests(unittest.TestCase):
    """`read_swing_records` -- the 188-byte records `numswingcentres`@708 declares."""

    def test_a_swing_states_its_window_bone_and_segment(self) -> None:
        # The shape of male `fists.mdl`'s `Fists_attack_W1` record 0.
        record, = _sequence(swings=[_swing()]).swings
        self.assertAlmostEqual(record.start, 0.26, places=6)
        self.assertAlmostEqual(record.end, 0.47, places=6)
        self.assertEqual((record.bone_index, record.bone), (1, "Bip01 R Forearm"))
        # Source units, unconverted: the parser leaves the file's units alone exactly as
        # `read_reach` does, and the export seam states the centimetres.
        self.assertEqual(record.a, (25.0, 0.0, 0.0))
        self.assertEqual(record.b, (0.0, 0.0, 0.0))
        self.assertFalse(record.degenerate)

    def test_every_declared_record_is_read_in_array_order(self) -> None:
        seq = _sequence(swings=[_swing(bone=1, a=(25.0, 0.0, 0.0)),
                                _swing(bone=2, a=(15.0, 0.0, 0.0))])
        self.assertEqual([(r.bone, r.a[0]) for r in seq.swings],
                         [("Bip01 R Forearm", 25.0), ("Bip01 R Hand", 15.0)])

    def test_the_record_array_is_addressed_from_the_descriptor(self) -> None:
        # `swingcentreindex` is descriptor-relative. A decoy record sits at the offset an
        # absolute reading would resolve to, so reading it that way returns the wrong window
        # rather than failing a bounds check and looking correct.
        record, = _sequence(swings=[_swing()], decoy_swing=True).swings
        self.assertAlmostEqual(record.start, 0.26, places=6)
        self.assertEqual(record.knockback[0], ("ACT_KNOCKBACK_SMALL_HIGH_RIGHT",))

    def test_a_knockback_name_is_addressed_from_its_own_record(self) -> None:
        # Each name index is relative to the record that states it, not to the array or the
        # descriptor, so two records naming the same activity store two different indices.
        image = _image(swings=[_swing(), _swing()])
        first = struct.unpack_from("<i", image, _SWING_BASE + 0x78)[0]
        second = struct.unpack_from("<i", image, _SWING_BASE + SWING_STRIDE + 0x78)[0]
        self.assertNotEqual(first, second)
        self.assertEqual([r.knockback[0] for r in mdl_skel.local_sequences(image)[0].swings],
                         [("ACT_KNOCKBACK_SMALL_HIGH_RIGHT",)] * 2)

    def test_the_four_buckets_keep_the_order_the_record_states_them_in(self) -> None:
        record, = _sequence(swings=[_swing()]).swings
        self.assertEqual(record.knockback, tuple((name,) for name in FISTS_KNOCKBACK))

    def test_a_bucket_carries_every_candidate_it_lists(self) -> None:
        # `gargoyle`'s ten two-record attacks put a second name in bucket 0; the slot array is
        # four wide, so a reader taking only the first slot drops them.
        record, = _sequence(swings=[_swing(knockback=(
            ("ACT_KNOCKBACK_BIGHIGHRIGHT_MELEESHARED_ONEHAND", "1"), None, None, None))]).swings
        self.assertEqual(record.knockback[0],
                         ("ACT_KNOCKBACK_BIGHIGHRIGHT_MELEESHARED_ONEHAND", "1"))
        self.assertEqual(record.knockback[1:], ((), (), ()))

    def test_an_unset_bucket_names_nothing(self) -> None:
        # A slot's unset marker is `0` -- Source's usual string-index convention, and not the
        # `-1` `szblockedreactionindex` uses.
        record, = _sequence(swings=[_swing(knockback=(None, None, None, None))]).swings
        self.assertEqual(record.knockback, ((), (), (), ()))

    def test_a_backwards_window_is_flagged_rather_than_repaired(self) -> None:
        # `fists_attack_heavy` and `fists_attack_heavy_old` on both player sexes state
        # start=0.302, end=0.0. Authored data is reproduced; the flag is what says so.
        record, = _sequence(swings=[_swing(window=(0.302, 0.0))]).swings
        self.assertTrue(record.degenerate)
        self.assertAlmostEqual(record.start, 0.302, places=6)
        self.assertEqual(record.end, 0.0)

    def test_both_range_test_bytes_and_the_unidentified_region_are_carried_raw(self) -> None:
        record, = _sequence(swings=[_swing(b8=3, ba=2)]).swings
        self.assertEqual((record.byte_b8, record.byte_ba), (3, 2))
        self.assertEqual(record.unidentified, (-1,) * 21)

    def test_a_bone_index_outside_the_table_names_no_bone(self) -> None:
        record, = _sequence(swings=[_swing(bone=99)]).swings
        self.assertEqual((record.bone_index, record.bone), (99, ""))

    def test_a_count_past_the_runtime_clamp_is_refused(self) -> None:
        # Both runtime consumers clamp the walk; a descriptor claiming more than they would ever
        # read is a damaged descriptor, not a swing with more contacts.
        self.assertEqual(_sequence(swings=[_swing()], swing_count=21).swings, ())

    def test_an_array_off_the_image_is_refused(self) -> None:
        self.assertEqual(_sequence(swings=[_swing()], swing_index=1 << 20).swings, ())
        self.assertEqual(_sequence(swings=[_swing()], swing_index=-4).swings, ())

    def test_a_sequence_declaring_none_carries_none(self) -> None:
        self.assertEqual(_sequence().swings, ())


class ClipMetaTests(unittest.TestCase):
    """`_clip_meta` states the reach in the centimetres every sidecar is written in."""

    def test_the_reach_crosses_the_seam_in_centimetres(self) -> None:
        meta = npc_export._clip_meta(_sequence(reach=64.0))
        self.assertAlmostEqual(meta["reach_cm"], 162.56, places=4)
        self.assertEqual(meta["blocked_reaction"], "ACT_BLOCKED_REACTION_LEFT")

    def test_a_clip_stating_neither_carries_neither_key(self) -> None:
        meta = npc_export._clip_meta(_sequence(reach=FLT_MAX, blocked=None))
        self.assertNotIn("reach_cm", meta)
        self.assertNotIn("blocked_reaction", meta)
        self.assertNotIn("swings", meta)

    def test_a_swing_segment_crosses_the_seam_as_a_bone_local_point(self) -> None:
        # The same `source_to_unreal` an attachment's bone-local translation and every bone's
        # own bind translation go through: inches to centimetres, with the handedness flip on Y.
        swing, = npc_export._clip_meta(
            _sequence(swings=[_swing(a=(25.0, 4.0, 0.0), b=(0.0, -2.0, 1.0))]))["swings"]
        self.assertEqual(swing["a_cm"], [63.5, -10.16, 0.0])
        self.assertEqual(swing["b_cm"], [0.0, 5.08, 2.54])

    def test_a_swing_states_its_window_bone_knockback_and_flags(self) -> None:
        swing, = npc_export._clip_meta(_sequence(swings=[_swing(ba=2)]))["swings"]
        # The window is a fraction of the clip cycle, so it crosses unitless and unconverted.
        self.assertEqual((swing["start"], swing["end"]), (0.26, 0.47))
        self.assertEqual(swing["bone"], "Bip01 R Forearm")
        self.assertEqual(swing["kb_names"], [[name] for name in FISTS_KNOCKBACK])
        self.assertEqual((swing["b8"], swing["ba"]), (3, 2))
        self.assertFalse(swing["degenerate"])

    def test_the_degenerate_flag_is_stated_on_every_swing_row(self) -> None:
        # A consumer forbidden to repair authored data reads the flag rather than re-deriving it.
        rows = npc_export._clip_meta(
            _sequence(swings=[_swing(), _swing(window=(0.302, 0.0))]))["swings"]
        self.assertEqual([row["degenerate"] for row in rows], [False, True])


class ClipSidecarRowTests(unittest.TestCase):
    """The clip slice row, which is truncated at its last stated column."""

    def _slice(self, own_clips):
        manifest = {
            "manifest_version": npc_export.MANIFEST_VERSION,
            "npcs": {"fighter": {"glb": "fighter.glb", "model": "models/fighter.mdl",
                                 "bones": 2,
                                 "clips": {label: "fighter" for label in own_clips},
                                 "own_clips": own_clips}},
            "banks": {},
        }
        with tempfile.TemporaryDirectory() as temporary:
            with (mock.patch.object(npc_export, "CLIPS_DIR",
                                    os.path.join(temporary, "clips")),
                  mock.patch.object(npc_export, "INDEX",
                                    os.path.join(temporary, "npc_index.json"))):
                npc_export.write_sidecars(manifest)
                with open(os.path.join(temporary, "clips", "fighter.json"),
                          encoding="utf-8") as f:
                    return json.load(f)

    def test_a_plain_clip_stops_at_fade(self) -> None:
        written = self._slice({"idle": npc_export._clip_meta(
            _sequence(label="idle", activity="ACT_IDLE", reach=FLT_MAX, blocked=None))})
        self.assertEqual(written["fields"],
                         ["owner", "activity", "weight", "flags", "frames", "fps", "fade",
                          "reach_cm", "blocked_reaction", "swings"])
        self.assertEqual(written["clips"]["idle"], [0, 1, 3, 0, 21, 30.0, 0.2])

    def test_a_reach_without_a_reaction_adds_one_column(self) -> None:
        written = self._slice({"swing": npc_export._clip_meta(
            _sequence(label="swing", reach=64.0, blocked=None))})
        self.assertEqual(written["clips"]["swing"], [0, 1, 3, 0, 21, 30.0, 0.2, 162.56])

    def test_a_blocked_reaction_is_inlined_not_interned(self) -> None:
        # `activities` is the stem's playable vocabulary -- runtime conformance unions it to
        # answer "can some model play this activity" -- so a reaction a clip only reacts to
        # (never performs) must not join it. The literal is written straight into the row.
        written = self._slice({"jab": npc_export._clip_meta(_sequence(reach=64.0))})
        self.assertEqual(written["activities"], ["", "ACT_MELEE_ATTACK"])
        self.assertEqual(written["clips"]["jab"],
                         [0, 1, 3, 0, 21, 30.0, 0.2, 162.56, "ACT_BLOCKED_REACTION_LEFT"])

    def test_a_reaction_without_a_reach_still_lands_in_its_own_column(self) -> None:
        # No shipped descriptor states a reaction without a reach -- all 147 carriers state both --
        # but the row is positional, so the reach column is held open rather than closed up. A
        # reader reaching column 8 must find the reaction literal there, not a reach.
        written = self._slice({"phantom": npc_export._clip_meta(
            _sequence(label="phantom", reach=FLT_MAX,
                      blocked="ACT_BLOCKED_REACTION_RIGHT"))})
        row = written["clips"]["phantom"]
        self.assertEqual(len(row), 9)
        self.assertIsNone(row[7])
        self.assertEqual(row[8], "ACT_BLOCKED_REACTION_RIGHT")

    def test_a_blocked_reaction_literal_never_joins_the_activities_table(self) -> None:
        # Even when a different clip's own activity happens to equal a reaction literal (so
        # that literal already sits in `activities`), the reaction column still carries a
        # plain inline string -- never an index into a table it does not depend on.
        written = self._slice({
            "jab": npc_export._clip_meta(
                _sequence(reach=64.0, blocked="ACT_BLOCKED_REACTION_RIGHT")),
            "recoil": npc_export._clip_meta(_sequence(
                label="recoil", activity="ACT_BLOCKED_REACTION_LEFT",
                reach=FLT_MAX, blocked=None)),
        })
        self.assertEqual(written["activities"],
                         ["", "ACT_MELEE_ATTACK", "ACT_BLOCKED_REACTION_LEFT"])
        self.assertEqual(written["clips"]["jab"][8], "ACT_BLOCKED_REACTION_RIGHT")
        self.assertEqual(written["clips"]["recoil"], [0, 2, 3, 0, 21, 30.0, 0.2])

    def test_a_swing_lands_behind_the_melee_pair(self) -> None:
        written = self._slice({"jab": npc_export._clip_meta(
            _sequence(reach=64.0, swings=[_swing()]))})
        row = written["clips"]["jab"]
        self.assertEqual(len(row), 10)
        self.assertEqual(row[7:9], [162.56, "ACT_BLOCKED_REACTION_LEFT"])
        self.assertEqual(row[9][0]["bone"], "Bip01 R Forearm")

    def test_a_swing_without_the_melee_pair_holds_both_columns_open(self) -> None:
        # 427 of the 574 swing carriers name no blocked reaction (all 574 state a reach), so the
        # row must hold the columns a swing sits behind rather than close them up and put a list
        # where a literal belongs. The reaction's placeholder is the empty literal, because that
        # column is read as a string unconditionally where `reach_cm`'s null is read guarded.
        written = self._slice({"claw": npc_export._clip_meta(
            _sequence(label="claw", reach=FLT_MAX, blocked=None, swings=[_swing()]))})
        row = written["clips"]["claw"]
        self.assertEqual(len(row), 10)
        self.assertIsNone(row[7])
        self.assertEqual(row[8], "")
        self.assertEqual([bucket[0] for bucket in row[9][0]["kb_names"]], list(FISTS_KNOCKBACK))

    def test_a_knockback_name_never_joins_the_activities_table(self) -> None:
        # Same rule as the blocked reaction, for the same reason: a knockback is what the
        # *victim* plays, so it must not enter this stem's playable vocabulary.
        written = self._slice({"claw": npc_export._clip_meta(
            _sequence(label="claw", reach=FLT_MAX, blocked=None, swings=[_swing()]))})
        self.assertEqual(written["activities"], ["", "ACT_MELEE_ATTACK"])


if __name__ == "__main__":
    unittest.main()
