"""The melee half of `StudioSeqDesc`'s custom block, from the descriptor to the clip sidecar.

Every image here is synthesised in-code, so nothing depends on the user's game install.
`reach`@720 (`+0x2D0`) is the swing's own target-acquisition distance and
`szblockedreactionindex`@740 (`+0x2E4`) names the activity the attacker plays when that swing is
blocked; the enum slot the runtime reads at `+0x2E0` is `-1` on disk everywhere, filled by the
DLL at model load exactly as `activity`@12 is filled from `szactivitynameindex`@4
(`docs/vtmb/combat-and-damage.md`).
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
FLT_MAX = struct.unpack("<f", struct.pack("<I", 0x7F7FFFFF))[0]

_ANIM_BASE = 512
_SEQ_BASE = 1024
_STRINGS = _SEQ_BASE + SEQ_STRIDE


def _image(*, label="fists_attack_JabLeft", activity="ACT_MELEE_ATTACK",
           reach=FLT_MAX, blocked="ACT_BLOCKED_REACTION_LEFT", blocked_index=None):
    """A one-sequence, one-animation v2531 image `local_sequences` can walk.

    `blocked_index` overrides the stored `szblockedreactionindex` outright, which is how the
    unset marker and an index off the end of the image are exercised.
    """
    image = bytearray(_STRINGS)
    struct.pack_into("<4sI", image, 0, b"IDST", 2531)
    struct.pack_into("<ii", image, 264, 1, _ANIM_BASE)     # NumLocalAnims / LocalAnimIndex
    struct.pack_into("<ii", image, 272, 1, _SEQ_BASE)      # NumLocalSeq / LocalSeqIndex

    strings = bytearray()

    def intern(text):
        offset = _STRINGS + len(strings)
        strings.extend(text.encode("ascii") + b"\0")
        return offset

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
                          "reach_cm", "blocked_reaction"])
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


if __name__ == "__main__":
    unittest.main()
