"""The pose-oracle joins, without a VtMB install or a capture.

`analyze_rig_pose` reads a Frida session and the user's `.mdl` bytes, neither of
which can be fixtured. What it *decides* is pure: which entity a pose belongs
to, which contributions share its frame, which player state was read closest to
its clock, and how a flat float run becomes one named matrix a bone. Those are
exercised here on hand-built events.
"""

from __future__ import annotations

import os
import struct
import tempfile
import unittest

# `rig_parity` imports the format modules, which resolve the VtMB root as they
# load; an existing directory is all the import needs, and nothing here reads it.
os.environ.setdefault("ELYSIUM_VTMB_ROOT", tempfile.gettempdir())

from research.tooling.capture.analyze_rig_pose import (  # noqa: E402
    resolve_body,
    CLOCK_TOLERANCE_SECONDS,
    ENTITY_DELTA,
    contributions,
    contributions_at,
    entity_delta_agreement,
    entity_models,
    group_frames,
    name_matrices,
    nearest_state,
    player_states,
    shape_matrices,
)
import pytest


def _bits(value: float) -> str:
    """A float as the hexadecimal stack word the agent records it as."""
    return hex(struct.unpack("<I", struct.pack("<f", value))[0])


def _pose_call(sequence: int, entity: str, curtime: float, thread: int = 1) -> dict:
    return {
        "kind": "call", "target": "client.setup_bones", "sequence": sequence,
        "ecx": entity, "thread_id": thread, "depth": 0,
        "stack_words": ["0x0", "0x1000", "0x15e", "0x10", _bits(curtime)],
    }


def _pose_return(sequence: int, bone_count: int, matrices: list) -> dict:
    return {
        "kind": "return", "target": "client.setup_bones", "sequence": sequence,
        "fields": {"bone_count": bone_count, "bone_matrices": matrices},
    }


def _contribution(sequence: int, model: str, index: int, weight: float,
                  curtime: float, thread: int = 1) -> dict:
    words = ["0x0"] * 8
    words[4] = hex(index)
    words[7] = _bits(weight)
    return {
        "kind": "call", "target": "client.accumulate_sequence_pose",
        "sequence": sequence, "ecx": "0x1", "thread_id": thread, "depth": 1,
        "stack_words": words, "fields": {"model": model, "curtime": curtime},
    }


def _player(sequence: int, curtime: float, **fields) -> dict:
    return {
        "kind": "call", "target": "vampire.player_item_post_frame",
        "sequence": sequence, "ecx": "0xp", "thread_id": 2,
        "fields": {"curtime": curtime, **fields},
    }


def test_a_model_is_taken_from_the_call_that_asked_for_it() -> None:
    events = [
        {"kind": "call", "target": "client.get_studio_hdr", "sequence": 1,
         "ecx": "0x100"},
        {"kind": "return", "target": "client.get_studio_hdr", "sequence": 1,
         "fields": {"model": "character/pc/body.mdl", "numbones": 77}},
    ]
    models = entity_models(events)
    assert models["0x100"][0]["model"] == "character/pc/body.mdl"
    assert models["0x100"][0]["numbones"] == 77


def test_an_entity_keeps_every_model_it_answered_with_in_order() -> None:
    """An entity outlives the model it draws, so a swap is a second answer on one pointer."""
    events = [
        {"kind": "call", "target": "client.get_studio_hdr", "sequence": 1, "ecx": "0x100"},
        {"kind": "return", "target": "client.get_studio_hdr", "sequence": 1,
         "fields": {"model": "first.mdl"}},
        {"kind": "call", "target": "client.get_studio_hdr", "sequence": 2, "ecx": "0x100"},
        {"kind": "return", "target": "client.get_studio_hdr", "sequence": 2,
         "fields": {"model": "second.mdl"}},
    ]
    assert [entry["model"] for entry in entity_models(events)["0x100"]] == ["first.mdl", "second.mdl"]


def test_one_model_answered_twice_is_recorded_once() -> None:
    events = [
        {"kind": "call", "target": "client.get_studio_hdr", "sequence": 1, "ecx": "0x100"},
        {"kind": "return", "target": "client.get_studio_hdr", "sequence": 1,
         "fields": {"model": "only.mdl"}},
        {"kind": "call", "target": "client.get_studio_hdr", "sequence": 2, "ecx": "0x100"},
        {"kind": "return", "target": "client.get_studio_hdr", "sequence": 2,
         "fields": {"model": "only.mdl"}},
    ]
    assert len(entity_models(events)["0x100"]) == 1


def test_a_return_whose_read_failed_names_nothing() -> None:
    events = [
        {"kind": "call", "target": "client.get_studio_hdr", "sequence": 1, "ecx": "0x100"},
        {"kind": "return", "target": "client.get_studio_hdr", "sequence": 1,
         "fields": {"model": {"error": "unreadable"}}},
    ]
    assert entity_models(events) == {}


def test_a_pose_resolves_to_its_model_across_the_subobject_delta() -> None:
    """`setup_bones` runs on the entity plus four; the model target on the entity."""
    entity = 0x15942828
    events = [
        _pose_call(1, hex(entity + ENTITY_DELTA), 10.0),
        {"kind": "call", "target": "client.get_studio_hdr", "sequence": 2,
         "ecx": hex(entity)},
        {"kind": "return", "target": "client.get_studio_hdr", "sequence": 2,
         "fields": {"model": "character/pc/body.mdl"}},
    ]
    agreement = entity_delta_agreement(events, entity_models(events))
    assert agreement == {"posed_entities": 1, "resolved_to_a_model": 1}


def test_a_pose_no_model_target_named_is_counted_not_hidden() -> None:
    events = [_pose_call(1, "0x2000", 10.0)]
    agreement = entity_delta_agreement(events, {})
    assert agreement == {"posed_entities": 1, "resolved_to_a_model": 0}


STATES = [_player(1, 10.000, cycle=0.1), _player(2, 10.016, cycle=0.2),
          _player(3, 10.032, cycle=0.3)]


def test_states_come_back_in_clock_order() -> None:
    shuffled = [STATES[2], STATES[0], STATES[1]]
    assert [s["curtime"] for s in player_states(shuffled)] == [10.000, 10.016, 10.032]


def test_a_state_with_no_clock_cannot_be_placed() -> None:
    assert player_states([{
        "kind": "call", "target": "vampire.player_item_post_frame",
        "sequence": 1, "ecx": "0xp", "fields": {"cycle": 0.5}}]) == []


def test_the_nearest_frame_wins_on_either_side() -> None:
    states = player_states(STATES)
    assert nearest_state(states, 10.017)["cycle"] == pytest.approx(0.2, abs=1e-7)
    assert nearest_state(states, 10.015)["cycle"] == pytest.approx(0.2, abs=1e-7)


def test_a_clock_outside_tolerance_matches_nothing() -> None:
    # Half a second past the last frame read: a pose that far from any state
    # belongs to a frame the player target never reported.
    states = player_states(STATES)
    assert nearest_state(states, 10.532) is None
    # And the boundary itself is inclusive, so a frame exactly one tolerance
    # away is still that frame rather than a miss.
    assert nearest_state(states, 10.032 + CLOCK_TOLERANCE_SECONDS) is not None


def test_an_empty_stream_answers_nothing_rather_than_raising() -> None:
    assert nearest_state([], 1.0) is None


def test_a_contribution_carries_its_model_index_weight_and_clock() -> None:
    rows, unplaceable = contributions([
        _contribution(1, "shared/frenzy.mdl", 42, 0.75, 10.0)])
    assert unplaceable == 0
    assert rows[0]["model"] == "shared/frenzy.mdl"
    assert rows[0]["sequence"] == 42
    assert rows[0]["weight"] == pytest.approx(0.75, abs=1e-5)


def test_a_contribution_with_no_clock_is_counted_not_attached() -> None:
    row = _contribution(1, "m.mdl", 1, 1.0, 10.0)
    row["fields"].pop("curtime")
    rows, unplaceable = contributions([row])
    assert (rows, unplaceable) == ([], 1)


def test_only_the_contributions_of_that_frame_attach_to_it() -> None:
    rows, _ = contributions([
        _contribution(1, "a.mdl", 1, 1.0, 10.000),
        _contribution(2, "b.mdl", 2, 0.5, 10.002),
        _contribution(3, "c.mdl", 3, 0.25, 10.500),
    ])
    window = contributions_at(rows, 10.001)
    assert [row["model"] for row in window] == ["a.mdl", "b.mdl"]


def test_a_frame_carries_its_entity_clock_and_matrices() -> None:
    matrices = [float(v) for v in range(24)]
    frames, nested = group_frames([
        _pose_call(1, "0x100", 10.0), _pose_return(1, 2, matrices)])
    assert len(frames) == 1
    assert frames[0]["entity"] == "0x100"
    assert frames[0]["curtime"] == pytest.approx(10.0, abs=1e-4)
    assert frames[0]["bone_count"] == 2
    assert nested == 0


def test_a_call_the_sampler_declined_yields_no_frame() -> None:
    """Only a call that was recorded can close into a frame."""
    frames, _ = group_frames([_pose_return(7, 2, [0.0] * 24)])
    assert frames == []


def test_a_return_whose_matrix_read_failed_is_not_a_frame() -> None:
    frames, _ = group_frames([
        _pose_call(1, "0x100", 10.0),
        {"kind": "return", "target": "client.setup_bones", "sequence": 1,
         "fields": {"bone_count": 2, "bone_matrices": {"error": "unreadable"}}},
    ])
    assert frames == []


def test_a_contribution_inside_a_recorded_call_is_counted_as_nested() -> None:
    frames, nested = group_frames([
        _pose_call(1, "0x100", 10.0),
        _contribution(2, "m.mdl", 1, 1.0, 10.0),
        _pose_return(1, 2, [0.0] * 24),
    ])
    assert (len(frames), nested) == (1, 1)


def test_a_contribution_outside_every_recorded_call_is_not_nested() -> None:
    _, nested = group_frames([_contribution(1, "m.mdl", 1, 1.0, 10.0)])
    assert nested == 0


def test_a_flat_run_becomes_one_matrix_a_bone() -> None:
    shaped = shape_matrices([float(v) for v in range(36)], 3)
    assert len(shaped) == 3
    assert shaped[1] == [float(v) for v in range(12, 24)]


def test_the_stated_count_bounds_the_run_rather_than_its_length() -> None:
    # The capture reads a generous window; the engine's own count is the authority.
    assert len(shape_matrices([0.0] * 120, 4)) == 4


def test_a_run_shorter_than_the_count_yields_what_it_carries() -> None:
    assert len(shape_matrices([0.0] * 24, 9)) == 2


def test_matrices_are_keyed_by_the_models_own_bone_names() -> None:
    named, surplus = name_matrices([[1.0] * 12, [2.0] * 12], ["Bip01", "Bip01 Pelvis"])
    assert list(named) == ["Bip01", "Bip01 Pelvis"]
    assert named["Bip01 Pelvis"][0] == 2.0
    assert surplus == 0


def test_a_matrix_the_bone_list_cannot_name_is_reported() -> None:
    """The two disagreeing means the model resolved wrongly, so it is counted."""
    named, surplus = name_matrices([[1.0] * 12, [2.0] * 12], ["Bip01"])
    assert list(named) == ["Bip01"]
    assert surplus == 1


COUNTS = {"male.mdl": 79, "female.mdl": 88, "twin.mdl": 79, "prop.mdl": 2}


# BodyResolutionTests
# Which model a frame's matrices belong to.
#
# The bone count the engine filled is the hard measurement: a model may only name a frame
# whose array is exactly as long as that model's own bone list, because naming an 88-bone
# array with a 79-name list does not fail -- it answers 79 matrices under the wrong names
# and drops nine.

# BodyResolutionTests
# Which model a frame's matrices belong to.
#
# The bone count the engine filled is the hard measurement: a model may only name a frame
# whose array is exactly as long as that model's own bone list, because naming an 88-bone
# array with a 79-name list does not fail -- it answers 79 matrices under the wrong names
# and drops nine.

def test_a_pose_that_names_its_own_model_answers_directly() -> None:
    assert resolve_body(88, "female.mdl", ["male.mdl"], [], [], COUNTS) == ("female.mdl", "stated by the pose record")


def test_a_stated_model_that_cannot_carry_the_array_is_refused() -> None:
    """A decode fault in the read chain, and guessing past it would bury it."""
    model, reason = resolve_body(88, "male.mdl", [], [], [], COUNTS)
    assert model is None
    assert "does not carry this bone count" in reason


def test_the_census_answers_when_it_fits_the_count() -> None:
    assert resolve_body(79, None, ["male.mdl"], ["male.mdl", "female.mdl"], [], COUNTS) == ("male.mdl", "census")


def test_a_census_the_count_contradicts_is_re_attributed() -> None:
    """The entity redrew as another body; the count says which one."""
    assert resolve_body(88, None, ["male.mdl"], ["male.mdl", "female.mdl"], [], COUNTS) == ("female.mdl", "re-attributed by bone count")


def test_a_contribution_breaks_a_tie_the_count_cannot() -> None:
    model, reason = resolve_body(
        79, None, ["female.mdl"], ["male.mdl", "twin.mdl"], ["twin.mdl"], COUNTS)
    assert model == "twin.mdl"
    assert "contribution" in reason


def test_two_models_of_one_count_and_no_contribution_are_refused() -> None:
    model, reason = resolve_body(
        79, None, ["female.mdl"], ["male.mdl", "twin.mdl"], [], COUNTS)
    assert model is None
    assert "several models" in reason


def test_a_count_no_session_model_carries_is_refused() -> None:
    model, reason = resolve_body(31, None, ["male.mdl"], ["male.mdl"], [], COUNTS)
    assert model is None
    assert "no model" in reason
