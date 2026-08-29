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

The chain half closes the descriptor: the button-state mask at `+0x2D4`, the DODGE activity name
at `+0x2DC` (load-resolved into `+0x2D8`, `-1` on disk like `+0x2E0`), the CHAIN and ALTERNATE
successor sequence labels at `+0x2E8`/`+0x2EC`, and the hand-off window at
`+0x2F0`/`+0x2F4`/`+0x2F8` — the descriptor's last twelve bytes.
"""

from __future__ import annotations

import json
import os
import struct
import tempfile
import unittest
from unittest import mock

from elysium_pipeline.exporters import UE_mdl_skeletal as UEK, npc_export
from elysium_pipeline.formats import mdl_skel
import pytest

SEQ_STRIDE = 764
ANIM_STRIDE = 72
BONE_STRIDE = 160
SWING_STRIDE = 188
FLT_MAX = struct.unpack("<f", struct.pack("<I", 0x7F7FFFFF))[0]
#: `low_reach`@716's own unset marker, and NOT `reach`@720's. The two fields are adjacent and the
#: markers are opposite ends of the float range, which is the trap this constant exists to pin.
FLT_MIN = struct.unpack("<f", struct.pack("<I", 0x00800000))[0]
ENVELOPE_STRIDE = 24

_ANIM_BASE = 512
_BONE_BASE = 1300
_SEQ_BASE = 2048
#: Room for the primary descriptor plus the sibling sequences a chain link resolves against.
_SEQ_CAPACITY = 4
_SWING_BASE = 8192
_SWING_CAPACITY = 4
#: The envelope array sits after the swing array and before the strings. It is a SEPARATE arena
#: because the two arrays are not parallel: `andrei`'s `JumpFromBlood_Attack` declares 459
#: envelopes against 17 contact records for the same clip.
_ENVELOPE_BASE = _SWING_BASE + _SWING_CAPACITY * SWING_STRIDE
_ENVELOPE_CAPACITY = 4
_STRINGS = _ENVELOPE_BASE + _ENVELOPE_CAPACITY * ENVELOPE_STRIDE

#: Where a `swingcentreindex` read as a file offset instead of a descriptor-relative one would
#: land. The gap between the descriptor array and the swing array is kept clear of every other
#: block so a decoy record can be planted exactly there.
_DECOY_BASE = _SWING_BASE - _SEQ_BASE
assert _SEQ_BASE + _SEQ_CAPACITY * SEQ_STRIDE <= _DECOY_BASE
assert _DECOY_BASE + SWING_STRIDE <= _SWING_BASE

#: What `+0x2F0`..`+0x2F8` read as on a sequence whose QC states no hand-off window: open at the
#: start of the cycle, close at the end, hold to the end. 13,841 shipped descriptors carry it.
WINDOW_DEFAULT = (0.0, 1.0, 1.0)

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
           swings=(), swing_count=None, swing_index=None, decoy_swing=False,
           mask=-1, dodge=None, dodge_index=None, chain=None, chain_index=None,
           chain_alt=None, window=None, siblings=(),
           low_reach=FLT_MIN, envelopes=(), envelope_count=None, envelope_index=None):
    """A one-animation v2531 image `local_sequences` can walk, carrying one authored sequence
    plus any `siblings` a chain link resolves against.

    `blocked_index` overrides the stored `szblockedreactionindex` outright, which is how the
    unset marker and an index off the end of the image are exercised; `dodge_index` and
    `chain_index` do the same for the chain half's own name indices. `low_reach` writes `+0x2CC`
    directly, so a case states the `FLT_MIN` marker or a value; `envelope_count`/`envelope_index`
    override the envelope array's declaration the way the swing pair's overrides do. `swing_count` and
    `swing_index` override the swing array's declaration the same way. `decoy_swing` plants a
    second, differently-windowed record at the offset an absolute reading of `swingcentreindex`
    would resolve to. A `siblings` entry is a bare label with the whole custom block unset, which
    is what every non-attack sequence of a weapon bank carries.
    """
    image = bytearray(_STRINGS)
    struct.pack_into("<4sI", image, 0, b"IDST", 2531)
    struct.pack_into("<ii", image, 240, len(BONES), _BONE_BASE)  # NumBones / BoneIndex
    struct.pack_into("<ii", image, 264, 1, _ANIM_BASE)     # NumLocalAnims / LocalAnimIndex
    assert 1 + len(siblings) <= _SEQ_CAPACITY, siblings
    struct.pack_into("<ii", image, 272, 1 + len(siblings), _SEQ_BASE)  # NumLocalSeq / index

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

    def write_seq(index, seq_label, *, seq_activity="", seq_reach=FLT_MAX, blocked_at=-1,
                  buttons=-1, dodge_at=-1, chain_at=-1, chain_alt_at=-1,
                  seq_window=WINDOW_DEFAULT, swings_declared=(0, 0),
                  seq_low_reach=FLT_MIN, envelopes_declared=(0, 0)):
        sb = _SEQ_BASE + index * SEQ_STRIDE
        struct.pack_into("<i", image, sb, intern(seq_label) - sb)
        if seq_activity:
            struct.pack_into("<i", image, sb + 4, intern(seq_activity) - sb)
        struct.pack_into("<i", image, sb + 16, 3)          # actweight
        struct.pack_into("<i", image, sb + 52, 1)          # numblends
        struct.pack_into("<h", image, sb + 56, 0)          # anim[0][0]
        struct.pack_into("<ii", image, sb + 572, 1, 1)     # groupsize
        struct.pack_into("<ii", image, sb + 580, -1, -1)
        struct.pack_into("<f", image, sb + 612, 0.2)       # fade
        struct.pack_into("<f", image, sb + 720, seq_reach)
        struct.pack_into("<i", image, sb + 740, blocked_at)
        struct.pack_into("<ii", image, sb + 708, *swings_declared)
        struct.pack_into("<f", image, sb + 716, seq_low_reach)      # +0x2CC
        struct.pack_into("<ii", image, sb + 700, *envelopes_declared)  # +0x2BC / +0x2C0
        struct.pack_into("<i", image, sb + 724, buttons)   # +0x2D4
        # +0x2D8, the resolved DODGE enum: -1 on all 14,012 shipped descriptors, filled by the
        # DLL from the name beside it, exactly as +0x2E0 is filled from +0x2E4.
        struct.pack_into("<i", image, sb + 728, -1)
        struct.pack_into("<i", image, sb + 732, dodge_at)      # +0x2DC
        struct.pack_into("<i", image, sb + 744, chain_at)      # +0x2E8
        struct.pack_into("<i", image, sb + 748, chain_alt_at)  # +0x2EC
        struct.pack_into("<3f", image, sb + 752, *seq_window)  # +0x2F0 / +0x2F4 / +0x2F8

    def write_envelope(record, corners):
        struct.pack_into("<6f", image, record, *corners[0], *corners[1])

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
    assert len(envelopes) <= _ENVELOPE_CAPACITY, envelopes
    if blocked_index is None:
        blocked_index = intern(blocked) - _SEQ_BASE if blocked else -1
    if dodge_index is None:
        dodge_index = intern(dodge) - _SEQ_BASE if dodge else -1
    if chain_index is None:
        chain_index = intern(chain) - _SEQ_BASE if chain else -1
    write_seq(0, label, seq_activity=activity, seq_reach=reach, blocked_at=blocked_index,
              buttons=mask, dodge_at=dodge_index, chain_at=chain_index,
              chain_alt_at=intern(chain_alt) - _SEQ_BASE if chain_alt else -1,
              seq_window=WINDOW_DEFAULT if window is None else window,
              swings_declared=(len(swings) if swing_count is None else swing_count,
                               (_SWING_BASE - _SEQ_BASE) if swing_index is None
                               else swing_index),
              seq_low_reach=low_reach,
              envelopes_declared=(len(envelopes) if envelope_count is None else envelope_count,
                                  (_ENVELOPE_BASE - _SEQ_BASE) if envelope_index is None
                                  else envelope_index))
    for position, sibling in enumerate(siblings, start=1):
        write_seq(position, sibling)
    for index, spec in enumerate(swings):
        write_swing(_SWING_BASE + index * SWING_STRIDE, spec)
    for index, corners in enumerate(envelopes):
        write_envelope(_ENVELOPE_BASE + index * ENVELOPE_STRIDE, corners)
    if decoy_swing:
        write_swing(_DECOY_BASE, _swing(window=(0.9, 0.95), bone=2, a=(-1.0, -1.0, -1.0),
                                        knockback=("ACT_DECOY", None, None, None)))

    image.extend(strings)
    return bytes(image)


def _sequences(**kwargs):
    """Every sequence of the synthesised image, in declaration order: the authored one first,
    then its `siblings`."""
    sequences = mdl_skel.local_sequences(_image(**kwargs))
    assert len(sequences) == 1 + len(kwargs.get("siblings", ())), sequences
    return sequences


def _sequence(**kwargs):
    return _sequences(**kwargs)[0]


def test_a_swing_carries_its_reach_and_blocked_reaction() -> None:
    seq = _sequence(reach=63.75)
    assert seq.reach == pytest.approx(63.75, abs=1e-4)
    assert seq.blocked_reaction == "ACT_BLOCKED_REACTION_LEFT"
    # The fields already read off the same descriptor are untouched by the two new ones.
    assert seq.activity == "ACT_MELEE_ATTACK"
    assert (seq.actweight, seq.frames, seq.fps) == (3, 21, 30.0)


def test_the_unset_markers_read_as_no_value() -> None:
    # FLT_MAX at @720 and -1 at @740 are what 13,431 and 13,865 shipped descriptors carry.
    seq = _sequence(reach=FLT_MAX, blocked=None)
    assert seq.reach is None
    assert seq.blocked_reaction is None


def test_a_zeroed_custom_block_states_no_reach() -> None:
    # Seven single-`idle` scenery and prop models zero the block instead of marking it unset,
    # and a zero query distance acquires nothing.
    assert _sequence(reach=0.0, blocked=None).reach is None


def test_a_blocked_reaction_index_off_the_image_is_refused() -> None:
    assert _sequence(blocked_index=1 << 20).blocked_reaction is None


def test_an_ordinary_sequence_states_neither() -> None:
    seq = _sequence(label="idle", activity="ACT_IDLE", reach=FLT_MAX, blocked=None)
    assert (seq.reach, seq.blocked_reaction) == (None, None)
    assert seq.swings == ()
    assert seq.combo is None


# LowReachTests
# `low_reach`@716 — the near edge of the band whose far edge is `reach`@720.

def test_the_unset_marker_is_flt_min_and_not_flt_max():
    # The whole reason this field needs its own reader. `FLT_MIN` is a finite POSITIVE float, so
    # a reader that copied `read_reach`'s `>= FLT_MAX` test would report 13,496 descriptors as
    # stating a near edge of 1.18e-38 — a band every distance clears, silently.
    assert _sequence(low_reach=FLT_MIN).low_reach is None
    assert _sequence(low_reach=FLT_MAX).low_reach is not None


def test_a_genuine_authored_zero_is_kept():
    # `werewolf`/`werewolf_damaged` `claw_attack_close` state exactly this against a reach of
    # 114.9. A near edge of zero is a band that starts at the body, which is a real claim —
    # unlike a zero FAR edge, which `read_reach` reads as unstated because it would be a swing
    # that can never reach.
    assert _sequence(low_reach=0.0).low_reach == 0.0


def test_the_two_edges_are_independent():
    # 72 descriptors state a reach with no low edge and 14 state a low edge with reach unset, so
    # neither field may be read as gating the other.
    reach_only = _sequence(reach=120.0, low_reach=FLT_MIN)
    assert reach_only.reach == pytest.approx(120.0, abs=1e-4)
    assert reach_only.low_reach is None
    low_only = _sequence(reach=FLT_MAX, low_reach=30.0)
    assert low_only.reach is None
    assert low_only.low_reach == pytest.approx(30.0, abs=1e-4)


class EnvelopeTests(unittest.TestCase):
    """The attack-envelope array at `numenvelopes`@700 / `envelopeindex`@704."""

    CORNERS = (((10.0, -4.0, -8.0), (40.0, 4.0, 8.0)),
               ((12.0, -5.0, -9.0), (44.0, 5.0, 9.0)))

    def test_records_decode_as_corner_pairs(self):
        assert _sequence(envelopes=self.CORNERS).envelopes == self.CORNERS

    def test_a_sequence_declaring_none_carries_none(self):
        assert _sequence().envelopes == ()

    def test_the_index_is_descriptor_relative(self):
        # Same bound `read_swing_records` requires. An absolute reading would resolve somewhere
        # inside the descriptor array and decode neighbouring fields as floats.
        absolute = _sequence(envelopes=self.CORNERS, envelope_index=_ENVELOPE_BASE)
        assert absolute.envelopes != self.CORNERS

    def test_an_implausible_count_yields_nothing(self):
        # The seven single-`idle` scenery models read count 768 at offset 768 — the descriptor tail
        # running into the string table. Refused rather than partly believed.
        assert _sequence(envelopes=self.CORNERS, envelope_count=768).envelopes == ()

    def test_an_array_off_the_end_of_the_image_yields_nothing(self):
        assert _sequence(envelopes=self.CORNERS, envelope_index=1 << 24).envelopes == ()

    def test_a_non_positive_index_yields_nothing(self):
        assert _sequence(envelopes=self.CORNERS, envelope_index=0).envelopes == ()

    def test_the_arrays_are_not_parallel(self):
        # The load-bearing fact about this array: it is a different structure with a different job,
        # and a consumer that walked them together would index one off the other's count.
        seq = _sequence(swings=(_swing(),), envelopes=self.CORNERS)
        assert len(seq.swings) == 1
        assert len(seq.envelopes) == 2


# ComboChainTests
# `read_combo_chain` -- the descriptor's last 44 bytes: which key selects this attack,
# which attack it hands off to, and when the hand-off may be asked for.

def test_the_first_fists_link_states_its_mask_chain_and_window() -> None:
    # Both player sexes' `fists.mdl` `Fists_attack_W1`, verbatim.
    combo = _sequence(label="Fists_attack_W1", mask=0x008, chain="Fists_attack_W2",
                      window=(0.5, 0.9, 0.91)).combo
    assert combo.mask == 0x008
    assert combo.chain == "Fists_attack_W2"
    assert combo.w_open == pytest.approx(0.5, abs=1e-6)
    assert combo.w_close == pytest.approx(0.9, abs=1e-6)
    assert combo.w_hold == pytest.approx(0.91, abs=1e-6)
    # The two names it does not state are absent, not missing keys.
    assert (combo.dodge, combo.chain_alt) == ("", "")


def test_the_mask_is_carried_raw() -> None:
    # The five stated masks are the neutral `0` and one `IN_*` direction bit each. They are
    # exported as the file's int: nothing here maps a bit to a direction.
    for mask in (0, mdl_skel.IN_FORWARD, mdl_skel.IN_BACK,
                 mdl_skel.IN_MOVELEFT, mdl_skel.IN_MOVERIGHT):
        assert _sequence(mask=mask).combo.mask == mask


def test_a_neutral_mask_is_a_stated_mask_not_an_absence() -> None:
    # `fists_attack_JabLeft` states `0` -- the attack a press with no direction held
    # selects -- and chains. 36 descriptors do; a reader treating a falsy mask as unset
    # would drop every one of them.
    combo = _sequence(label="fists_attack_JabLeft", mask=0, chain="fists_attack_longright",
                      window=(0.65, 0.9, 0.91)).combo
    assert combo is not None
    assert (combo.mask, combo.chain) == (0, "fists_attack_longright")


def test_an_unset_mask_alone_authors_nothing() -> None:
    # -1 at +0x2D4 with no name and the default window is what 13,804 descriptors carry.
    assert _sequence(mask=-1).combo is None


def test_the_default_window_is_what_unauthored_reads_as() -> None:
    # 13,841 descriptors state 0.0 / 1.0 / 1.0 -- open at the start, close at the end, hold
    # to the end. It gates nothing, and on its own it is not an authored block.
    assert _sequence(window=WINDOW_DEFAULT).combo is None


def test_a_wholly_zeroed_custom_block_authors_nothing() -> None:
    # The seven single-`idle` scenery and prop models zero the block rather than marking it
    # unset, so their window triple is all-zero rather than the default.
    assert _sequence(window=(0.0, 0.0, 0.0)).combo is None


def test_the_default_window_is_still_carried_beside_an_authored_field() -> None:
    # 20 `meleeshared_onehand` reaction links chain on the default window. Once the block is
    # authored every field is stated, because a consumer cannot re-derive a window.
    combo = _sequence(label="knockback_flying_into_back", chain="knockback_flying_idle",
                      chain_alt="knockback_flying_wall_hit").combo
    assert (combo.w_open, combo.w_close, combo.w_hold) == WINDOW_DEFAULT
    assert combo.mask == (-1)


def test_a_hold_below_the_close_is_carried_verbatim() -> None:
    # `katana_running_attack` authors 0.25 / 1.0 / 0.9 and `baseballbat_attack_jump`
    # 0.5 / 0.9 / 0.8. The three floats are per-sequence: nothing orders or repairs them.
    combo = _sequence(label="katana_running_attack", mask=0x008, chain="katana_combo_C2",
                      window=(0.25, 1.0, 0.9)).combo
    assert combo.w_open == pytest.approx(0.25, abs=1e-6)
    assert combo.w_close == pytest.approx(1.0, abs=1e-6)
    assert combo.w_hold == pytest.approx(0.9, abs=1e-6)
    assert combo.w_hold < combo.w_close


def test_the_dodge_activity_resolves_from_its_name() -> None:
    # `ACT_DODGE_DUCK` on all 12 that state one. The enum slot at +0x2D8 beside it is -1 on
    # disk everywhere, filled by the DLL at load, so the name is the durable key.
    combo = _sequence(label="katana_attack_Left_Right2", dodge="ACT_DODGE_DUCK").combo
    assert combo.dodge == "ACT_DODGE_DUCK"
    assert (combo.chain, combo.chain_alt) == ("", "")


def test_the_alternate_successor_is_read_from_its_own_offset() -> None:
    # The flying-knockback wall branch: 28 `meleeshared_onehand` descriptors name a second
    # successor at +0x2EC beside the ordinary one at +0x2E8.
    combo = _sequence(chain="knockback_flying_idle",
                      chain_alt="knockback_flying_wall_hit").combo
    assert combo.chain == "knockback_flying_idle"
    assert combo.chain_alt == "knockback_flying_wall_hit"


def test_a_name_index_off_the_image_is_refused() -> None:
    assert _sequence(chain_index=1 << 20).combo is None
    assert _sequence(dodge_index=1 << 20).combo is None


def test_the_names_are_addressed_from_the_descriptor() -> None:
    # Every index in this block is descriptor-relative, so a sibling descriptor reading the
    # same absolute string would need a different index. Resolving from the wrong base on the
    # second descriptor would name whatever bytes happen to sit there.
    seq, sibling = _sequences(mask=0x008, chain="fists_attack_Roundhouse",
                              siblings=("fists_attack_Roundhouse",))
    assert seq.combo.chain == "fists_attack_Roundhouse"
    assert sibling.combo is None


# ComboChainOrphanTests
# `combo_chain_orphans` -- the census of links naming a sequence the model does not have.

def test_a_link_resolving_to_a_local_sequence_is_no_orphan() -> None:
    clips = _sequences(mask=0x008, chain="fists_attack_Roundhouse",
                       siblings=("fists_attack_Roundhouse",))
    assert mdl_skel.combo_chain_orphans(clips) == ()


def test_the_match_is_case_insensitive() -> None:
    # Ten shipped links disagree with their target's case and resolve fine at runtime --
    # `tireiron_attack_slash` -> `tireiron_attack_Heavy` against the label
    # `tireiron_attack_heavy`.
    clips = _sequences(label="tireiron_attack_slash", chain="tireiron_attack_Heavy",
                       window=(0.5, 0.9, 0.91), siblings=("tireiron_attack_heavy",))
    assert mdl_skel.combo_chain_orphans(clips) == ()


def test_a_dangling_chain_keeps_its_string_and_is_named() -> None:
    # `Fists_attack_W2` chains to a `Fists_attack_W3` neither sex's `fists.mdl` defines.
    # The string is authored data and stays; the census is what says it goes nowhere.
    clips = _sequences(label="Fists_attack_W2", mask=0x008, chain="Fists_attack_W3",
                       window=(0.5, 0.9, 0.91), siblings=("Fists_attack_W1",))
    assert clips[0].combo.chain == "Fists_attack_W3"
    assert mdl_skel.combo_chain_orphans(clips) == (("Fists_attack_W2", "Fists_attack_W3"),)


def test_a_dangling_alternate_is_censused_on_the_same_terms() -> None:
    clips = _sequences(chain="knockback_flying_idle", chain_alt="knockback_flying_wall_hit",
                       siblings=("knockback_flying_idle",))
    assert mdl_skel.combo_chain_orphans(clips) == (("fists_attack_JabLeft", "knockback_flying_wall_hit"),)


def test_the_exporter_warns_naming_model_sequence_and_target() -> None:
    clips = _sequences(label="Fists_attack_W2", mask=0x008, chain="Fists_attack_W3",
                       window=(0.5, 0.9, 0.91), siblings=("Fists_attack_W1",))
    with mock.patch("builtins.print") as printed:
        count = npc_export.warn_combo_chain_orphans(
            "models/character/shared/male/fists.mdl", clips)
    assert count == 1
    line, = (call.args[0] for call in printed.call_args_list)
    for fragment in ("models/character/shared/male/fists.mdl", "Fists_attack_W2",
                     "Fists_attack_W3"):
        assert fragment in line


def test_a_model_with_no_dangling_link_warns_nothing() -> None:
    clips = _sequences(mask=0x008, chain="fists_attack_Roundhouse",
                       siblings=("fists_attack_Roundhouse",))
    with mock.patch("builtins.print") as printed:
        assert npc_export.warn_combo_chain_orphans("models/x.mdl", clips) == 0
    printed.assert_not_called()


# SwingContactRecordTests
# `read_swing_records` -- the 188-byte records `numswingcentres`@708 declares.

def test_a_swing_states_its_window_bone_and_segment() -> None:
    # The shape of male `fists.mdl`'s `Fists_attack_W1` record 0.
    record, = _sequence(swings=[_swing()]).swings
    assert record.start == pytest.approx(0.26, abs=1e-6)
    assert record.end == pytest.approx(0.47, abs=1e-6)
    assert (record.bone_index, record.bone) == (1, "Bip01 R Forearm")
    # Source units, unconverted: the parser leaves the file's units alone exactly as
    # `read_reach` does, and the export seam states the centimetres.
    assert record.a == (25.0, 0.0, 0.0)
    assert record.b == (0.0, 0.0, 0.0)
    assert not record.degenerate


def test_every_declared_record_is_read_in_array_order() -> None:
    seq = _sequence(swings=[_swing(bone=1, a=(25.0, 0.0, 0.0)),
                            _swing(bone=2, a=(15.0, 0.0, 0.0))])
    assert [(r.bone, r.a[0]) for r in seq.swings] == [("Bip01 R Forearm", 25.0), ("Bip01 R Hand", 15.0)]


def test_the_record_array_is_addressed_from_the_descriptor() -> None:
    # `swingcentreindex` is descriptor-relative. A decoy record sits at the offset an
    # absolute reading would resolve to, so reading it that way returns the wrong window
    # rather than failing a bounds check and looking correct.
    record, = _sequence(swings=[_swing()], decoy_swing=True).swings
    assert record.start == pytest.approx(0.26, abs=1e-6)
    assert record.knockback[0] == ("ACT_KNOCKBACK_SMALL_HIGH_RIGHT",)


def test_a_knockback_name_is_addressed_from_its_own_record() -> None:
    # Each name index is relative to the record that states it, not to the array or the
    # descriptor, so two records naming the same activity store two different indices.
    image = _image(swings=[_swing(), _swing()])
    first = struct.unpack_from("<i", image, _SWING_BASE + 0x78)[0]
    second = struct.unpack_from("<i", image, _SWING_BASE + SWING_STRIDE + 0x78)[0]
    assert first != second
    assert [r.knockback[0] for r in mdl_skel.local_sequences(image)[0].swings] == [("ACT_KNOCKBACK_SMALL_HIGH_RIGHT",)] * 2


def test_the_four_buckets_keep_the_order_the_record_states_them_in() -> None:
    record, = _sequence(swings=[_swing()]).swings
    assert record.knockback == tuple((name,) for name in FISTS_KNOCKBACK)


def test_a_bucket_carries_every_candidate_it_lists() -> None:
    # `gargoyle`'s ten two-record attacks put a second name in bucket 0; the slot array is
    # four wide, so a reader taking only the first slot drops them.
    record, = _sequence(swings=[_swing(knockback=(
        ("ACT_KNOCKBACK_BIGHIGHRIGHT_MELEESHARED_ONEHAND", "1"), None, None, None))]).swings
    assert record.knockback[0] == ("ACT_KNOCKBACK_BIGHIGHRIGHT_MELEESHARED_ONEHAND", "1")
    assert record.knockback[1:] == ((), (), ())


def test_an_unset_bucket_names_nothing() -> None:
    # A slot's unset marker is `0` -- Source's usual string-index convention, and not the
    # `-1` `szblockedreactionindex` uses.
    record, = _sequence(swings=[_swing(knockback=(None, None, None, None))]).swings
    assert record.knockback == ((), (), (), ())


def test_a_backwards_window_is_flagged_rather_than_repaired() -> None:
    # `fists_attack_heavy` and `fists_attack_heavy_old` on both player sexes state
    # start=0.302, end=0.0. Authored data is reproduced; the flag is what says so.
    record, = _sequence(swings=[_swing(window=(0.302, 0.0))]).swings
    assert record.degenerate
    assert record.start == pytest.approx(0.302, abs=1e-6)
    assert record.end == 0.0


def test_both_range_test_bytes_and_the_unidentified_region_are_carried_raw() -> None:
    record, = _sequence(swings=[_swing(b8=3, ba=2)]).swings
    assert (record.byte_b8, record.byte_ba) == (3, 2)
    assert record.unidentified == (-1,) * 21


def test_a_bone_index_outside_the_table_names_no_bone() -> None:
    record, = _sequence(swings=[_swing(bone=99)]).swings
    assert (record.bone_index, record.bone) == (99, "")


def test_a_count_past_the_runtime_clamp_is_refused() -> None:
    # Both runtime consumers clamp the walk; a descriptor claiming more than they would ever
    # read is a damaged descriptor, not a swing with more contacts.
    assert _sequence(swings=[_swing()], swing_count=21).swings == ()


def test_an_array_off_the_image_is_refused() -> None:
    assert _sequence(swings=[_swing()], swing_index=1 << 20).swings == ()
    assert _sequence(swings=[_swing()], swing_index=-4).swings == ()


def test_a_sequence_declaring_none_carries_none() -> None:
    assert _sequence().swings == ()


# ClipMetaTests
# `_clip_meta` states the reach in the centimetres every sidecar is written in.

def test_the_reach_crosses_the_seam_in_centimetres() -> None:
    meta = npc_export._clip_meta(_sequence(reach=64.0))
    assert meta["reach_cm"] == pytest.approx(162.56, abs=1e-4)
    assert meta["blocked_reaction"] == "ACT_BLOCKED_REACTION_LEFT"


def test_a_clip_stating_neither_carries_neither_key() -> None:
    meta = npc_export._clip_meta(_sequence(reach=FLT_MAX, blocked=None))
    assert "reach_cm" not in meta
    assert "blocked_reaction" not in meta
    assert "swings" not in meta
    assert "combo" not in meta


def test_the_combo_block_crosses_the_seam_whole_and_unconverted() -> None:
    # Nothing in it is a length or a direction: a button mask is a mask, an activity and a
    # sequence label are names, and a fraction of a clip cycle has no units.
    meta = npc_export._clip_meta(
        _sequence(label="Fists_attack_W1", mask=0x008, chain="Fists_attack_W2",
                  window=(0.5, 0.9, 0.91)))
    assert meta["combo"] == {"mask": 0x008, "dodge": "", "chain": "Fists_attack_W2",
                                     "chain_alt": "", "w_open": 0.5, "w_close": 0.9,
                                     "w_hold": 0.91}


def test_a_dangling_chain_label_still_crosses_the_seam() -> None:
    # The exporter warns; the data stays authored.
    combo = npc_export._clip_meta(
        _sequence(label="Fists_attack_W2", mask=0x008, chain="Fists_attack_W3",
                  window=(0.5, 0.9, 0.91)))["combo"]
    assert combo["chain"] == "Fists_attack_W3"


def test_a_swing_segment_crosses_the_seam_as_a_bone_local_point() -> None:
    # The same `source_to_unreal` an attachment's bone-local translation and every bone's
    # own bind translation go through: inches to centimetres, with the handedness flip on Y.
    swing, = npc_export._clip_meta(
        _sequence(swings=[_swing(a=(25.0, 4.0, 0.0), b=(0.0, -2.0, 1.0))]))["swings"]
    assert swing["a_cm"] == [63.5, -10.16, 0.0]
    assert swing["b_cm"] == [0.0, 5.08, 2.54]


def test_a_swing_states_its_window_bone_knockback_and_flags() -> None:
    swing, = npc_export._clip_meta(_sequence(swings=[_swing(ba=2)]))["swings"]
    # The window is a fraction of the clip cycle, so it crosses unitless and unconverted.
    assert (swing["start"], swing["end"]) == (0.26, 0.47)
    assert swing["bone"] == "Bip01 R Forearm"
    assert swing["kb_names"] == [[name] for name in FISTS_KNOCKBACK]
    assert (swing["b8"], swing["ba"]) == (3, 2)
    assert not swing["degenerate"]


def test_the_degenerate_flag_is_stated_on_every_swing_row() -> None:
    # A consumer forbidden to repair authored data reads the flag rather than re-deriving it.
    rows = npc_export._clip_meta(
        _sequence(swings=[_swing(), _swing(window=(0.302, 0.0))]))["swings"]
    assert [row["degenerate"] for row in rows] == [False, True]


# ClipSidecarRowTests
# The clip slice row, which is truncated at its last stated column.

def _slice(own_clips, clips=None, banks=None, clip_seq=None):
    # `clips` maps a label to the stems that declare it, in include-tree order; the fixture
    # defaults to the body owning every one of its own. `clip_seq` is parallel to it and
    # carries each row's global sequence number.
    resolved = (clips if clips is not None
                else {label: ["fighter"] for label in own_clips})
    manifest = {
        "manifest_version": npc_export.MANIFEST_VERSION,
        "npcs": {"fighter": {"model": "models/fighter.mdl",
                             "bones": 2,
                             "clips": resolved,
                             "clip_seq": (clip_seq if clip_seq is not None
                                          else {label: list(range(len(owners)))
                                                for label, owners in resolved.items()}),
                             "own_clips": own_clips}},
        "banks": banks or {},
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


def _row(written, label):
    """One label's first row. A label carries a LIST of rows, one per declaring owner."""
    rows = written["clips"][label]
    assert isinstance(rows, list) and rows and isinstance(rows[0], list), rows
    return rows[0]


def test_the_sequence_numbers_are_positionally_parallel_to_the_rows() -> None:
    # `seq[label][i]` is the global sequence number of `clips[label][i]`. The runtime orders
    # candidates by it and breaks a weight tie on it, so a list that fell out of step would
    # hand the tie-break another owner's number.
    fists = npc_export._clip_meta(
        _sequence(label="stealth", activity="ACT_SNEAK_FISTS", reach=FLT_MAX, blocked=None))
    bat = npc_export._clip_meta(
        _sequence(label="stealth", activity="ACT_SNEAK_BAT", reach=FLT_MAX, blocked=None))
    written = _slice(
        {},
        clips={"stealth": ["bat", "fists"]},
        clip_seq={"stealth": [317, 908]},
        banks={"bat": {"model": "models/bat.mdl", "clips": {"stealth": bat}},
               "fists": {"model": "models/fists.mdl", "clips": {"stealth": fists}}},
    )
    assert written["seq"]["stealth"] == [317, 908]
    assert [written["owners"][row[0]] for row in written["clips"]["stealth"]] == ["bat", "fists"]


def test_a_row_the_walk_could_not_number_holds_its_slot_open() -> None:
    # The two lists are read positionally, so an unnumbered row states a null rather than
    # shortening the list and shifting every number behind it onto the wrong owner.
    fists = npc_export._clip_meta(
        _sequence(label="stealth", activity="ACT_SNEAK_FISTS", reach=FLT_MAX, blocked=None))
    bat = npc_export._clip_meta(
        _sequence(label="stealth", activity="ACT_SNEAK_BAT", reach=FLT_MAX, blocked=None))
    written = _slice(
        {},
        clips={"stealth": ["bat", "fists"]},
        clip_seq={"stealth": [None, 908]},
        banks={"bat": {"model": "models/bat.mdl", "clips": {"stealth": bat}},
               "fists": {"model": "models/fists.mdl", "clips": {"stealth": fists}}},
    )
    assert written["seq"]["stealth"] == [None, 908]


def test_a_record_written_before_the_numbering_reads_as_unnumbered() -> None:
    # A manifest `reindex` re-derives sidecars from, written before `clip_seq` existed.
    written = _slice(
        {"idle": npc_export._clip_meta(
            _sequence(label="idle", activity="ACT_IDLE", reach=FLT_MAX, blocked=None))},
        clips={"idle": ["fighter"]},
        clip_seq={},
    )
    assert written["seq"]["idle"] == [None]


def test_a_label_two_banks_declare_writes_one_row_per_owner() -> None:
    # The shipped corpus repeats a label across banks with a different activity on each -- all
    # ten weapon banks declare `stealth_success_attacker_shortvictim`. Keeping only the first
    # would make the other nine activities unanswerable, so every owner gets its own row and
    # include-tree order decides which one a label-only lookup resolves to.
    fists = npc_export._clip_meta(
        _sequence(label="stealth", activity="ACT_SNEAK_FISTS", reach=FLT_MAX, blocked=None))
    bat = npc_export._clip_meta(
        _sequence(label="stealth", activity="ACT_SNEAK_BAT", reach=FLT_MAX, blocked=None))
    written = _slice(
        {},
        clips={"stealth": ["bat", "fists"]},
        banks={"bat": {"model": "models/bat.mdl", "clips": {"stealth": bat}},
               "fists": {"model": "models/fists.mdl", "clips": {"stealth": fists}}},
    )
    rows = written["clips"]["stealth"]
    assert len(rows) == 2
    owners = [written["owners"][row[0]] for row in rows]
    activities = [written["activities"][row[1]] for row in rows]
    assert owners == ["bat", "fists"]
    assert activities == ["ACT_SNEAK_BAT", "ACT_SNEAK_FISTS"]
    # The tree's first is first, which is the answer a label-only lookup keeps giving.
    assert _row(written, "stealth")[0] == written["owners"].index("bat")


def test_the_envelope_corners_are_not_positionally_converted() -> None:
    # **The negative that matters.** An envelope's corners are not a point in the model's frame:
    # the axes are reach distance, lateral tolerance and vertical offset. Putting them through
    # the positional projection would apply the Y reflection as well as the scale, mirroring an
    # axis that is symmetric about zero — so the mirrored record would compare equal against
    # every symmetric enemy box and disagree with nothing that could report it.
    #
    # The check is a scale and ONLY a scale, asserted on a corner whose Y is non-zero and
    # asymmetric so a sign flip cannot hide.
    corners = (((10.0, -3.0, -8.0), (40.0, 7.0, 8.0)),)
    meta = npc_export._clip_meta(_sequence(envelopes=corners))
    cm = npc_export.INCH_TO_CM
    assert meta["envelopes"] == [{
        "min": [round(10.0 * cm, 4), round(-3.0 * cm, 4), round(-8.0 * cm, 4)],
        "max": [round(40.0 * cm, 4), round(7.0 * cm, 4), round(8.0 * cm, 4)],
    }]
    # And the same corner through the positional path, to name what was avoided.
    assert meta["envelopes"][0]["min"][1] != round(UEK._conv_pos((10.0, -3.0, -8.0))[1], 4)


def test_a_low_reach_converts_to_centimetres_and_keeps_a_zero() -> None:
    assert npc_export._clip_meta(_sequence(low_reach=30.0))["low_reach_cm"] == pytest.approx(round(30.0 * npc_export.INCH_TO_CM, 4), abs=1e-4)
    # A stated zero is a band that starts at the body, and it has to survive the seam.
    assert npc_export._clip_meta(_sequence(low_reach=0.0))["low_reach_cm"] == 0.0
    assert "low_reach_cm" not in npc_export._clip_meta(_sequence(low_reach=FLT_MIN))


def test_a_low_reach_lands_behind_the_four_melee_columns() -> None:
    # 14 shipped descriptors state a low edge with `reach` unset, so every column before this
    # one has to be held open — and each with its own placeholder, because the readers differ.
    written = _slice({"swing": npc_export._clip_meta(
        _sequence(label="swing", reach=FLT_MAX, blocked=None, low_reach=12.0))})
    row = _row(written, "swing")
    assert len(row) == 12
    assert row[7] is None            # reach_cm — read guarded against a null
    assert row[8] == ""         # blocked_reaction — read as a string
    assert row[9] == []         # swings — read as an array
    assert row[10] is None           # combo — read guarded against a null
    assert row[11] == pytest.approx(round(12.0 * npc_export.INCH_TO_CM, 4), abs=1e-4)


def test_envelopes_land_last_and_hold_every_column_open() -> None:
    written = _slice({"swing": npc_export._clip_meta(
        _sequence(label="swing", reach=FLT_MAX, blocked=None,
                  envelopes=(((1.0, -2.0, -3.0), (4.0, 2.0, 3.0)),)))})
    row = _row(written, "swing")
    assert len(row) == 13
    assert row[7] is None
    assert row[8] == ""
    assert row[9] == []
    assert row[10] is None
    assert row[11] is None           # low_reach_cm — the same null guard as reach_cm
    assert len(row[12]) == 1


def test_a_plain_clip_stops_at_fade() -> None:
    written = _slice({"idle": npc_export._clip_meta(
        _sequence(label="idle", activity="ACT_IDLE", reach=FLT_MAX, blocked=None))})
    # The column names, in the order a reader indexes them by. New columns APPEND: the index of
    # an existing one is a contract with every slice already on disk.
    assert written["fields"] == ["owner", "activity", "weight", "flags", "frames", "fps", "fade",
                      "reach_cm", "blocked_reaction", "swings", "combo",
                      "low_reach_cm", "envelopes"]
    assert _row(written, "idle") == [0, 1, 3, 0, 21, 30.0, 0.2]


def test_a_reach_without_a_reaction_adds_one_column() -> None:
    written = _slice({"swing": npc_export._clip_meta(
        _sequence(label="swing", reach=64.0, blocked=None))})
    assert _row(written, "swing") == [0, 1, 3, 0, 21, 30.0, 0.2, 162.56]


def test_a_blocked_reaction_is_inlined_not_interned() -> None:
    # `activities` is the stem's playable vocabulary -- runtime conformance unions it to
    # answer "can some model play this activity" -- so a reaction a clip only reacts to
    # (never performs) must not join it. The literal is written straight into the row.
    written = _slice({"jab": npc_export._clip_meta(_sequence(reach=64.0))})
    assert written["activities"] == ["", "ACT_MELEE_ATTACK"]
    assert _row(written, "jab") == [0, 1, 3, 0, 21, 30.0, 0.2, 162.56, "ACT_BLOCKED_REACTION_LEFT"]


def test_a_reaction_without_a_reach_still_lands_in_its_own_column() -> None:
    # No shipped descriptor states a reaction without a reach -- all 147 carriers state both --
    # but the row is positional, so the reach column is held open rather than closed up. A
    # reader reaching column 8 must find the reaction literal there, not a reach.
    written = _slice({"phantom": npc_export._clip_meta(
        _sequence(label="phantom", reach=FLT_MAX,
                  blocked="ACT_BLOCKED_REACTION_RIGHT"))})
    row = _row(written, "phantom")
    assert len(row) == 9
    assert row[7] is None
    assert row[8] == "ACT_BLOCKED_REACTION_RIGHT"


def test_a_blocked_reaction_literal_never_joins_the_activities_table() -> None:
    # Even when a different clip's own activity happens to equal a reaction literal (so
    # that literal already sits in `activities`), the reaction column still carries a
    # plain inline string -- never an index into a table it does not depend on.
    written = _slice({
        "jab": npc_export._clip_meta(
            _sequence(reach=64.0, blocked="ACT_BLOCKED_REACTION_RIGHT")),
        "recoil": npc_export._clip_meta(_sequence(
            label="recoil", activity="ACT_BLOCKED_REACTION_LEFT",
            reach=FLT_MAX, blocked=None)),
    })
    assert written["activities"] == ["", "ACT_MELEE_ATTACK", "ACT_BLOCKED_REACTION_LEFT"]
    assert _row(written, "jab")[8] == "ACT_BLOCKED_REACTION_RIGHT"
    assert _row(written, "recoil") == [0, 2, 3, 0, 21, 30.0, 0.2]


def test_a_swing_lands_behind_the_melee_pair() -> None:
    written = _slice({"jab": npc_export._clip_meta(
        _sequence(reach=64.0, swings=[_swing()]))})
    row = _row(written, "jab")
    assert len(row) == 10
    assert row[7:9] == [162.56, "ACT_BLOCKED_REACTION_LEFT"]
    assert row[9][0]["bone"] == "Bip01 R Forearm"


def test_a_swing_without_the_melee_pair_holds_both_columns_open() -> None:
    # 427 of the 574 swing carriers name no blocked reaction (all 574 state a reach), so the
    # row must hold the columns a swing sits behind rather than close them up and put a list
    # where a literal belongs. The reaction's placeholder is the empty literal, because that
    # column is read as a string unconditionally where `reach_cm`'s null is read guarded.
    written = _slice({"claw": npc_export._clip_meta(
        _sequence(label="claw", reach=FLT_MAX, blocked=None, swings=[_swing()]))})
    row = _row(written, "claw")
    assert len(row) == 10
    assert row[7] is None
    assert row[8] == ""
    assert [bucket[0] for bucket in row[9][0]["kb_names"]] == list(FISTS_KNOCKBACK)


def test_a_knockback_name_never_joins_the_activities_table() -> None:
    # Same rule as the blocked reaction, for the same reason: a knockback is what the
    # *victim* plays, so it must not enter this stem's playable vocabulary.
    written = _slice({"claw": npc_export._clip_meta(
        _sequence(label="claw", reach=FLT_MAX, blocked=None, swings=[_swing()]))})
    assert written["activities"] == ["", "ACT_MELEE_ATTACK"]


def test_a_combo_lands_behind_the_swing() -> None:
    written = _slice({"jab": npc_export._clip_meta(
        _sequence(reach=64.0, swings=[_swing()], mask=0x008, chain="Fists_attack_W2",
                  window=(0.5, 0.9, 0.91)))})
    row = _row(written, "jab")
    assert len(row) == 11
    assert row[7:9] == [162.56, "ACT_BLOCKED_REACTION_LEFT"]
    assert row[9][0]["bone"] == "Bip01 R Forearm"
    assert row[10]["chain"] == "Fists_attack_W2"


def test_a_combo_without_the_melee_trio_holds_all_three_columns_open() -> None:
    # 28 of the 208 carriers -- the `meleeshared_onehand` flying-knockback reaction chain --
    # state no reach, no reaction and no swing. The row holds those columns rather than
    # closing them up and putting a dict where a list belongs.
    written = _slice({"flung": npc_export._clip_meta(
        _sequence(label="flung", reach=FLT_MAX, blocked=None,
                  chain="knockback_flying_idle",
                  chain_alt="knockback_flying_wall_hit"))})
    row = _row(written, "flung")
    assert len(row) == 11
    assert row[7] is None
    assert row[8] == ""
    assert row[9] == []
    assert row[10]["chain_alt"] == "knockback_flying_wall_hit"


def test_a_chain_label_never_joins_the_activities_table() -> None:
    # A successor is a sequence label, not an activity, so `activities` is not a table it
    # indexes -- and the runtime conformance union must not learn it as playable.
    written = _slice({"jab": npc_export._clip_meta(
        _sequence(mask=0x008, chain="Fists_attack_W2", dodge="ACT_DODGE_DUCK",
                  window=(0.5, 0.9, 0.91)))})
    assert written["activities"] == ["", "ACT_MELEE_ATTACK"]
    assert _row(written, "jab")[10]["dodge"] == "ACT_DODGE_DUCK"
