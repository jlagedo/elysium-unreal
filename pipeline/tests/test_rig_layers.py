"""The layer-oracle joins, without a VtMB install or a capture.

`analyze_rig_layers` reads the user's `.mdl` bytes and a Frida session, neither
of which can be fixtured. What it *decides* is pure: which index space a
contribution belongs to, how a repeated contribution collapses into one channel,
which channels are additive, and how many of them the graph can hold. Those are
exercised here on hand-built rows.
"""

from __future__ import annotations

import json
import os
import tempfile
import unittest

# `analyze_rig_layers` defers its `install` import to `build`, so the module
# imports without a configured game root. The default keeps a sibling module on
# the same chain honest if the import order ever changes.
os.environ.setdefault("ELYSIUM_VTMB_ROOT", tempfile.gettempdir())

from research.tooling.capture.analyze_rig_layers import (  # noqa: E402
    ClipSpace,
    channels_for_frame,
    is_body_model,
    summarise,
)

BANK = "models/character/shared/female/move_and_ranged.mdl"
BODY = "models/character/pc/female/malkavian/armor0/malkavian_female_armor_0.mdl"

#: A clip slice in the shipped shape: interned owners and activities, rows per
#: label, and the parallel `seq` map T1 writes.
SLICE = {
    "stem": "body",
    "owners": ["body", "bank"],
    "activities": ["", "ACT_WALK"],
    "fields": ["owner", "activity", "weight", "flags", "frames", "fps", "fade"],
    "clips": {
        "walk": [[1, 1, 30, 0x1, 46, 30.0, 0.2]],
        "aim_layer": [[1, 0, 1, 0x0, 1, 30.0, 0.2]],
        "bobble_delta": [[1, 0, 1, 0x4, 1, 30.0, 0.2]],
        "attack_delta": [[1, 0, 1, 0x4, 1, 30.0, 0.2]],
    },
    "seq": {"walk": [410], "aim_layer": [401], "bobble_delta": [402], "attack_delta": [403]},
}

#: The bank's own positional label list — index 0 is not `walk`, so a body
#: number read as a bank index would answer the wrong clip and vice versa.
BANK_LABELS = ["aim_layer", "bobble_delta", "attack_delta", "walk"]


def _space() -> ClipSpace:
    directory = tempfile.mkdtemp()
    clips = os.path.join(directory, "npc", "clips")
    os.makedirs(clips)
    with open(os.path.join(clips, "body.json"), "w", encoding="utf-8") as stream:
        json.dump(SLICE, stream)
    from pathlib import Path

    return ClipSpace("body", Path(directory), lambda model: BANK_LABELS)


def _contrib(model: str, sequence: int, weight: float = 1.0) -> dict:
    return {"model": model, "sequence": sequence, "weight": weight}


def test_a_body_path_is_told_from_a_bank_path() -> None:
    assert is_body_model(BODY)
    assert is_body_model("models/character/npc/common/bum/male/bum_male.mdl")
    assert not is_body_model(BANK)


def test_the_separator_and_case_do_not_decide_it() -> None:
    assert is_body_model("MODELS\\CHARACTER\\PC\\male\\x.mdl")


def test_a_body_contribution_reads_as_a_global_number() -> None:
    space = _space()
    assert space.resolve(BODY, 401) == ("aim_layer", "bank", 0x0)
    assert space.resolve(BODY, 410) == ("walk", "bank", 0x1)


def test_a_bank_contribution_reads_as_a_local_index() -> None:
    space = _space()
    assert space.resolve(BANK, 0) == ("aim_layer", "bank", 0x0)
    assert space.resolve(BANK, 3) == ("walk", "bank", 0x1)


def test_one_number_answers_differently_in_each_space() -> None:
    # 401 is `aim_layer` globally and out of range locally; 0 is `aim_layer`
    # locally and absent globally. Reading either through the wrong space
    # would answer a clip that exists and is wrong.
    space = _space()
    assert space.resolve(BODY, 401)[0] == "aim_layer"
    assert space.resolve(BANK, 401) is None
    assert space.resolve(BODY, 0) is None


def test_a_repeated_contribution_is_one_channel_that_counts_its_repeats() -> None:
    frame = {"contributions": [
        _contrib(BANK, 0), _contrib(BANK, 0), _contrib(BANK, 1),
    ]}
    channels, unresolved = channels_for_frame(frame, _space())
    assert unresolved == 0
    assert len(channels) == 2
    aim = next(c for c in channels if c["label"] == "aim_layer")
    assert aim["times_accumulated"] == 2
    assert "weight_disagreement" not in aim


def test_a_repeat_that_disagrees_on_weight_says_so() -> None:
    frame = {"contributions": [_contrib(BANK, 0, 1.0), _contrib(BANK, 0, 0.25)]}
    channels, _ = channels_for_frame(frame, _space())
    assert len(channels) == 1
    assert channels[0]["weight"] == 1.0
    assert channels[0]["weight_disagreement"] == 0.25


def test_the_two_spaces_name_one_channel_rather_than_two() -> None:
    # The same clip reached as a bank-local index and as a body-global number
    # is one channel, because the dedup key is (owner, label).
    frame = {"contributions": [_contrib(BANK, 0), _contrib(BODY, 401)]}
    channels, _ = channels_for_frame(frame, _space())
    assert [c["label"] for c in channels] == ["aim_layer"]
    assert channels[0]["times_accumulated"] == 2


def test_the_delta_flag_is_what_marks_an_additive() -> None:
    frame = {"contributions": [_contrib(BANK, 0), _contrib(BANK, 1)]}
    channels, _ = channels_for_frame(frame, _space())
    by = {c["label"]: c["additive"] for c in channels}
    assert not by["aim_layer"]
    assert by["bobble_delta"]


def test_an_unreadable_contribution_is_counted_not_dropped_silently() -> None:
    frame = {"contributions": [_contrib(BANK, 0), _contrib(BANK, 99)]}
    channels, unresolved = channels_for_frame(frame, _space())
    assert len(channels) == 1
    assert unresolved == 1


class CapacityTests(unittest.TestCase):
    """One overlay node and one `_delta` node is what the graph carries."""

    @staticmethod
    def _row(plain: int, additive: int) -> dict:
        channels = [{"label": f"p{i}", "additive": False} for i in range(plain)]
        channels += [{"label": f"a{i}", "additive": True} for i in range(additive)]
        return {"channels": channels}

    def test_a_base_and_one_overlay_and_one_additive_fits(self) -> None:
        report = summarise([self._row(2, 1)])
        assert report["frames_beyond_the_graph"] == 0

    def test_a_second_additive_does_not_fit(self) -> None:
        report = summarise([self._row(2, 2)])
        assert report["frames_over_one_additive"] == 1
        assert report["frames_beyond_the_graph"] == 1

    def test_a_third_plain_channel_does_not_fit(self) -> None:
        report = summarise([self._row(3, 0)])
        assert report["frames_over_one_overlay"] == 1
        assert report["frames_beyond_the_graph"] == 1

    def test_a_frame_over_on_both_counts_is_one_frame_beyond_the_graph(self) -> None:
        report = summarise([self._row(4, 2)])
        assert report["frames_over_one_overlay"] == 1
        assert report["frames_over_one_additive"] == 1
        assert report["frames_beyond_the_graph"] == 1
