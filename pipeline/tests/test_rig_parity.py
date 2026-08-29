"""The rig-parity joins, without a VtMB install or a capture.

`rig_parity` reads the user's `.mdl` bytes and a Frida session, neither of which
can be fixtured. What it *decides* is pure: which offline ordering explains a
live number space, which witnesses a body's own tree can reach, where the
export's clip map names a different bank than retail did, and which labels a
flat space repeats. Those are exercised here on hand-built rows.
"""

from __future__ import annotations

from collections import Counter
import os
import tempfile

# `analyze_rig_resolution` imports `install`, which resolves the VtMB root as it
# loads; an existing directory is all the import needs, and nothing here reads it.
os.environ.setdefault("ELYSIUM_VTMB_ROOT", tempfile.gettempdir())

from research.tooling.capture import analyze_rig_resolution as analyzer  # noqa: E402
from research.tooling.capture.rig_parity import (  # noqa: E402
    UNREAL_SKIP_CM,
    admissible_witnesses,
    compare_retarget,
    derive_transform,
    read_retail_transform,
    retail_matrix,
    best_numbering,
    compare_numbering,
    compare_ownership,
    compare_sequence_numbers,
    first_occurrence_owners,
    label_collisions,
    model_key,
    stems_by_model,
    witnessed_owners,
)
import pytest


def _flat(*rows: tuple[str, int, str, str]) -> list[dict]:
    return [
        {
            "global_index": index,
            "owner_model": model_key(owner),
            "owner_index": local,
            "label": label,
            "activity_name": activity,
        }
        for index, (owner, local, label, activity) in enumerate(rows)
    ]


def _live(*rows: tuple[str, int, str | None, bool]) -> list[dict]:
    return [
        {
            "body_model": "character/pc/body.mdl",
            "global_index": str(index),
            "owner_model": owner,
            "owner_index": str(local),
            "label": label or "",
            "resolved": "True" if resolved else "False",
        }
        for index, (owner, local, label, resolved) in enumerate(rows)
    ]


def test_captured_and_exported_names_meet_on_one_key() -> None:
    assert model_key("character/shared/male/Katana.mdl") == model_key("/character/shared/male/katana.mdl")
    assert model_key("models\\character\\shared\\male\\katana.mdl") == "models/character/shared/male/katana.mdl"


def test_an_unlabelled_live_row_still_checks_its_owner() -> None:
    """A bank the session never loaded carries no label but a real owner and index."""
    flat = _flat(("body.mdl", 0, "ragdoll", ""), ("bank.mdl", 0, "walk", "ACT_WALK"))
    agreeing = _live(("body.mdl", 0, None, False), ("bank.mdl", 0, None, False))
    assert compare_numbering(agreeing, flat)["owner_mismatches"] == 0
    disagreeing = _live(("body.mdl", 0, None, False), ("other.mdl", 0, None, False))
    report = compare_numbering(disagreeing, flat)
    assert report["owner_mismatches"] == 1
    assert report["examples"][0]["global_index"] == 1


def test_a_live_number_past_the_offline_space_is_a_mismatch() -> None:
    flat = _flat(("body.mdl", 0, "ragdoll", ""))
    live = _live(("body.mdl", 0, "ragdoll", True), ("bank.mdl", 0, "walk", True))
    report = compare_numbering(live, flat)
    assert report["owner_mismatches"] == 1
    assert report["compared"] == 1


def test_the_ordering_with_fewer_mismatches_wins() -> None:
    # A bank reached twice contributes twice under `nodedup`; the live space says it did.
    dedup = _flat(("body.mdl", 0, "a", ""), ("bank.mdl", 0, "w", ""), ("last.mdl", 0, "z", ""))
    nodedup = _flat(
        ("body.mdl", 0, "a", ""), ("bank.mdl", 0, "w", ""), ("bank.mdl", 0, "w", ""),
        ("last.mdl", 0, "z", ""),
    )
    live = _live(
        ("body.mdl", 0, "a", True), ("bank.mdl", 0, "w", True), ("bank.mdl", 0, "w", True),
        ("last.mdl", 0, "z", True),
    )
    ordering, report = best_numbering(live, {"dedup": dedup, "nodedup": nodedup})
    assert ordering == "nodedup"
    assert report["owner_mismatches"] == 0


def test_selections_and_chains_witness_the_same_body_label_owner() -> None:
    resolution = [
        {
            "target": "vampire.select_heaviest_sequence", "body_model": "pc/body.mdl",
            "resolved_label": "walk", "resolved_owner": "shared/bank.mdl",
            "resolved_activity_name": "ACT_WALK", "result_kind": "sequence",
            "requested": "7", "result": "12",
        },
        # An activity answer names no clip and witnesses nothing.
        {
            "target": "vampire.weapon_translate_activity", "body_model": "pc/body.mdl",
            "resolved_label": "", "resolved_owner": "", "resolved_activity_name": "",
            "result_kind": "activity", "requested": "7", "result": "9",
        },
    ]
    ownership = [
        {
            "requested_model": "pc/body.mdl", "requested_index": "12",
            "owner_model": "/shared/bank.mdl", "label": "Walk", "activity_name": "ACT_WALK",
        }
    ]
    seen = witnessed_owners(resolution, ownership)
    body = seen[model_key("pc/body.mdl")]
    assert list(body) == ["walk"]
    assert body["walk"]["owners"] == Counter({model_key("shared/bank.mdl"): 2})
    assert body["walk"]["activity_ids"] == Counter({"7": 1})
    assert body["walk"]["sequences"] == Counter({"12": 2})


def test_a_witness_outside_the_tree_is_rejected_not_diffed() -> None:
    witnessed = {
        "walk": {"label": "walk", "owners": Counter({"models/male/bank.mdl": 3}),
                 "activity_names": Counter(), "targets": Counter(),
                 "activity_ids": Counter(), "sequences": Counter()},
        "idle": {"label": "idle", "owners": Counter({"models/female/bank.mdl": 1}),
                 "activity_names": Counter(), "targets": Counter(),
                 "activity_ids": Counter(), "sequences": Counter()},
        "run": {"label": "run", "owners": Counter(
                    {"models/male/bank.mdl": 2, "models/female/bank.mdl": 1}),
                "activity_names": Counter(), "targets": Counter(),
                "activity_ids": Counter(), "sequences": Counter()},
    }
    admitted, rejected = admissible_witnesses(witnessed, {"models/male/bank.mdl"})
    assert set(admitted) == {"walk", "run"}
    assert admitted["run"]["owners"] == Counter({"models/male/bank.mdl": 2})
    assert set(rejected) == {"idle", "run"}
    assert rejected["run"]["owners"] == Counter({"models/female/bank.mdl": 1})


SEQUENCE_NUMBER_STEMS = {
    "models/shared/misc.mdl": "shared_misc",
    "models/shared/fists.mdl": "shared_fists",
}

SEQUENCE_NUMBER_LIVE = _live(
    ("body.mdl", 0, "ragdoll", True),
    ("models/shared/misc.mdl", 0, "idle01", True),
    ("models/shared/fists.mdl", 0, "kick", True),
    ("models/shared/fists.mdl", 1, None, False),
)


@staticmethod
def _exported(**labels):
    return {label.lower(): rows for label, rows in labels.items()}


def test_a_number_naming_another_bank_is_an_owner_mismatch() -> None:
    exported = _exported(
        kick=[{"label": "kick", "owner_stem": "shared_misc", "seq": 2}])
    report = compare_sequence_numbers(exported, SEQUENCE_NUMBER_LIVE, SEQUENCE_NUMBER_STEMS)
    assert report["owner_mismatches"] == 1
    assert report["examples"][0] == {"label": "kick", "seq": 2, "live": "shared_fists",
        "export": "shared_misc"}


def test_a_number_naming_another_clip_is_a_label_mismatch() -> None:
    exported = _exported(
        kick=[{"label": "kick", "owner_stem": "shared_misc", "seq": 1}])
    report = compare_sequence_numbers(exported, SEQUENCE_NUMBER_LIVE, SEQUENCE_NUMBER_STEMS)
    assert report["label_mismatches"] == 1
    assert report["owner_mismatches"] == 0


def test_an_unlabelled_exported_spare_row_still_checks_its_owner() -> None:
    agreeing = _exported(
        spare=[{"label": "spare", "owner_stem": "shared_fists", "seq": 3}])
    assert compare_sequence_numbers(agreeing, SEQUENCE_NUMBER_LIVE, SEQUENCE_NUMBER_STEMS)["owner_mismatches"] == 0
    disagreeing = _exported(
        spare=[{"label": "spare", "owner_stem": "shared_misc", "seq": 3}])
    assert compare_sequence_numbers(disagreeing, SEQUENCE_NUMBER_LIVE, SEQUENCE_NUMBER_STEMS)["owner_mismatches"] == 1


def test_an_unnumbered_row_is_counted_rather_than_compared() -> None:
    exported = _exported(
        kick=[{"label": "kick", "owner_stem": "shared_misc", "seq": None}])
    report = compare_sequence_numbers(exported, SEQUENCE_NUMBER_LIVE, SEQUENCE_NUMBER_STEMS)
    assert report["rows_unnumbered"] == 1
    assert report["rows_checked"] == 0
    assert report["owner_mismatches"] == 0


def test_a_number_past_the_live_table_is_counted_rather_than_compared() -> None:
    exported = _exported(
        kick=[{"label": "kick", "owner_stem": "shared_misc", "seq": 900}])
    report = compare_sequence_numbers(exported, SEQUENCE_NUMBER_LIVE, SEQUENCE_NUMBER_STEMS)
    assert report["numbers_absent_from_live_table"] == 1
    assert report["rows_checked"] == 0


def test_a_live_owner_with_no_export_stem_is_not_a_mismatch() -> None:
    # A weapon or viewmodel the character export never names cannot be compared against.
    live = _live(("models/weapons/w_null.mdl", 0, "idle", True))
    exported = _exported(
        idle=[{"label": "idle", "owner_stem": "shared_misc", "seq": 0}])
    report = compare_sequence_numbers(exported, live, SEQUENCE_NUMBER_STEMS)
    assert report["owner_mismatches"] == 0
    assert report["rows_checked"] == 1


STEMS = {
    "models/shared/misc.mdl": "shared_misc",
    "models/shared/pc_idles.mdl": "shared_pc_idles",
    "models/shared/fists.mdl": "shared_fists",
}


def _witness(label: str, owners: dict[str, int], activity: str = "") -> dict:
    return {
        "label": label, "owners": Counter(owners),
        "activity_names": Counter({activity: 1} if activity else {}),
        "targets": Counter(), "activity_ids": Counter(), "sequences": Counter(),
    }


def test_agreement_mismatch_absence_and_conflict_are_told_apart() -> None:
    witnessed = {
        "idle01": _witness("idle01", {"models/shared/misc.mdl": 5}, "ACT_IDLE"),
        "kick": _witness("kick", {"models/shared/fists.mdl": 1}, "ACT_KICK"),
        "gone": _witness("gone", {"models/shared/fists.mdl": 1}),
        "both": _witness(
            "both", {"models/shared/misc.mdl": 1, "models/shared/pc_idles.mdl": 1}
        ),
    }
    exported = {
        "idle01": [{"label": "idle01", "owner_stem": "shared_pc_idles",
                    "activity_name": "ACT_IDLE"}],
        "kick": [{"label": "kick", "owner_stem": "shared_fists",
                  "activity_name": "ACT_KICK"}],
    }
    report = compare_ownership(witnessed, exported, STEMS)
    assert report["agreed"] == 1
    assert report["owner_mismatches"] == 1
    assert report["examples"]["owner_mismatches"][0] == {"label": "idle01", "retail": "shared_misc", "export": ["shared_pc_idles"], "hits": 5}
    assert report["absent_from_export"] == 1
    assert report["retail_conflicts"] == 1


def test_a_same_owner_different_activity_is_its_own_count() -> None:
    witnessed = {
        "stealth": _witness(
            "stealth", {"models/shared/fists.mdl": 1}, "ACT_SNEAK_FISTS"
        ),
    }
    exported = {
        "stealth": [{"label": "stealth", "owner_stem": "shared_fists",
                     "activity_name": "ACT_SNEAK_BAT"}],
    }
    report = compare_ownership(witnessed, exported, STEMS)
    assert report["activity_mismatches"] == 1
    assert report["agreed"] == 0


def test_the_right_owner_among_several_is_what_agrees() -> None:
    """A label several banks declare agrees when ANY row is retail's own owner."""
    witnessed = {
        "stealth": _witness(
            "stealth", {"models/shared/fists.mdl": 1}, "ACT_SNEAK_FISTS"),
    }
    exported = {
        "stealth": [
            {"label": "stealth", "owner_stem": "shared_misc",
             "activity_name": "ACT_SNEAK_BAT"},
            {"label": "stealth", "owner_stem": "shared_fists",
             "activity_name": "ACT_SNEAK_FISTS"},
        ],
    }
    report = compare_ownership(witnessed, exported, STEMS)
    assert report["agreed"] == 1
    assert report["owner_mismatches"] == 0


def test_a_retail_owner_the_export_never_named_is_reported_not_failed() -> None:
    witnessed = {"x": _witness("x", {"models/weapons/w_null.mdl": 1})}
    report = compare_ownership(witnessed, {}, STEMS)
    assert report["retail_owners_without_export_stem"] == {"models/weapons/w_null.mdl": 1}
    assert report["absent_from_export"] == 0


FLAT = _flat(
    ("body.mdl", 0, "ragdoll", "ACT_DIERAGDOLL"),
    ("pc_idles.mdl", 0, "idle01", "ACT_IDLE"),
    ("baseball.mdl", 0, "stealth", "ACT_SNEAK_BAT"),
    ("misc.mdl", 0, "Idle01", "ACT_IDLE"),
    ("fists.mdl", 0, "stealth", "ACT_SNEAK_FISTS"),
    ("fists.mdl", 1, "kick", "ACT_KICK"),
)


def test_first_occurrence_is_case_insensitive_and_keeps_the_first_spelling() -> None:
    firsts = first_occurrence_owners(FLAT)
    assert firsts["idle01"]["owner_model"] == model_key("pc_idles.mdl")
    assert firsts["idle01"]["label"] == "idle01"
    assert firsts["stealth"]["global_index"] == 2


def test_collisions_are_split_by_whether_the_copies_share_an_activity() -> None:
    report = label_collisions(FLAT)
    assert report["labels_repeated_across_banks"] == 2
    assert report["same_activity"] == 1
    assert report["different_activity"] == 1
    assert report["examples"]["same_activity"][0]["label"] == "idle01"
    assert report["examples"]["different_activity"][0]["activities"] == ["ACT_SNEAK_BAT", "ACT_SNEAK_FISTS"]


def test_a_label_repeated_inside_one_bank_is_not_a_cross_bank_collision() -> None:
    flat = _flat(("bank.mdl", 0, "x", "A"), ("bank.mdl", 1, "x", "B"))
    assert label_collisions(flat)["labels_repeated_across_banks"] == 0


def test_bodies_and_banks_map_by_normalised_model() -> None:
    index = {
        "npcs": {"body": {"model": "models/character/pc/Body.mdl"}},
        "banks": {"shared_bank": {"model": "character/shared/bank.mdl"}},
    }
    stems = stems_by_model(index)
    assert stems[model_key("character/pc/body.mdl")] == "body"
    assert stems[model_key("/character/shared/bank.mdl")] == "shared_bank"


class _FakeBank:
    def __init__(self, labels: list[str], activities: list[str]) -> None:
        self.present = True
        self._labels = labels
        self._activities = activities

    def label(self, index: int) -> str | None:
        return self._labels[index] if 0 <= index < len(self._labels) else None

    def activity(self, index: int) -> str | None:
        return self._activities[index] if 0 <= index < len(self._activities) else None


class _FakeLibrary:
    def __init__(self, banks: dict[str, _FakeBank]) -> None:
        self._banks = banks

    def bank(self, name: str | None):
        return self._banks.get(analyzer.BankLibrary.key_for(name)) if name else None


# AnalyzerLiveMapTests
# `analyze_rig_resolution.sequence_map` completes an unlabelled live row off the install.

def _write(directory: str, rows: list[str]) -> None:
    header = "body_model,global_index,owner_model,owner_index,depth,label,activity_name,resolved\n"
    with open(os.path.join(directory, "sequence_map.csv"), "w", encoding="utf-8") as stream:
        stream.write(header + "".join(row + "\n" for row in rows))


def test_an_unlabelled_row_is_completed_and_marked() -> None:
    from pathlib import Path

    with tempfile.TemporaryDirectory() as directory:
        _write(directory, [
            "pc/body.mdl,0,pc/body.mdl,0,0,ragdoll,ACT_DIERAGDOLL,True",
            "pc/body.mdl,1,shared/bank.mdl,3,2,,,False",
            "pc/body.mdl,2,shared/missing.mdl,0,2,,,False",
        ])
        library = _FakeLibrary({
            analyzer.BankLibrary.key_for("shared/bank.mdl"): _FakeBank(
                ["a", "b", "c", "walk"], ["", "", "", "ACT_WALK"]
            ),
        })
        table = analyzer.sequence_map(Path(directory), library)
    body = analyzer.BankLibrary.key_for("pc/body.mdl")
    assert table[(body, 0)]["label"] == "ragdoll"
    assert table[(body, 1)]["label"] == "walk"
    assert table[(body, 1)]["activity_name"] == "ACT_WALK"
    assert table[(body, 1)]["resolved"] == "install"
    assert (body, 2) not in table


def test_without_a_library_unlabelled_rows_are_still_dropped() -> None:
    from pathlib import Path

    with tempfile.TemporaryDirectory() as directory:
        _write(directory, ["pc/body.mdl,1,shared/bank.mdl,3,2,,,False"])
        assert analyzer.sequence_map(Path(directory)) == {}


# AnalyzerChainFallbackTests
# A selection whose body is known never borrows another body's chain.

def test_head_only_key_is_used_only_when_the_body_is_unknown() -> None:
    ownership = [
        {"requested_model": "pc/female.mdl", "requested_index": 769,
         "owner_model": "shared/female/katana.mdl", "label": "katana_combo_C2",
         "activity_name": "ACT_MELEE_ATTACK_KATANA"},
    ]
    calls = [
        {"target": "vampire.get_model_ptr", "sequence": 1, "ecx": "0x1",
         "stack_words": ["0", "ffffffff"]},
        {"target": "vampire.select_heaviest_sequence", "sequence": 2, "ecx": "0x1",
         "stack_words": ["0", "7"]},
        {"target": "vampire.select_heaviest_sequence", "sequence": 3, "ecx": "0x2",
         "stack_words": ["0", "7"]},
    ]
    returns = {
        ("vampire.get_model_ptr", 1): {"return_value": "0x10", "fields": {"model": "pc/male.mdl"}},
        ("vampire.select_heaviest_sequence", 2): {"return_value": "0x301"},
        ("vampire.select_heaviest_sequence", 3): {"return_value": "0x301"},
    }
    rows = analyzer.selections(calls, returns, ownership, live_map={})
    known, unknown = rows
    assert known["body_model"] == "pc/male.mdl"
    assert known["resolved_owner"] is None
    assert unknown["body_model"] is None
    assert unknown["resolved_owner"] == "shared/female/katana.mdl"
    assert unknown["resolved_from"] == "chain"


def _matrix(rows, translation=(0.0, 0.0, 0.0)) -> str:
    """A `matrix3x4` string the way `capture_rig_remap` writes one: row-major, translation in
    the fourth column."""
    out = []
    for index, row in enumerate(rows):
        out.extend(list(row) + [translation[index]])
    return ";".join("%.7g" % v for v in out)


IDENTITY = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))


def test_the_fourth_column_is_the_translation() -> None:
    rows, translation = retail_matrix(_matrix(IDENTITY, (1.0, 2.0, 3.0)))
    assert rows == [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
    assert translation == (1.0, 2.0, 3.0)


def test_a_pure_translation_reads_as_unit_scale_and_no_angle() -> None:
    # Retail's origin branch: identity rotation, `b - a` in the fourth column, stated in
    # inches, which is why the invariant comes back in centimetres.
    read = read_retail_transform(_matrix(IDENTITY, (1.22551, 0.0, 0.0)))
    assert read["scale"] == pytest.approx(1.0, abs=1e-6)
    assert read["angle_degrees"] == pytest.approx(0.0, abs=1e-4)
    assert read["translation_cm"] == pytest.approx(1.22551 * 2.54, abs=1e-5)


def test_a_uniform_scale_reads_as_that_scale() -> None:
    scaled = tuple(tuple(0.7 * v for v in row) for row in IDENTITY)
    read = read_retail_transform(_matrix(scaled))
    assert read["scale"] == pytest.approx(0.7, abs=1e-6)
    assert read["row_norm_spread"] == pytest.approx(0.0, abs=1e-9)


def test_a_length_ratio_with_no_rotation() -> None:
    out = derive_transform((0.0, 0.0, 2.0), (0.0, 0.0, 6.0))
    assert out["branch"] == "axis_angle"
    assert out["scale"] == pytest.approx(3.0, abs=1e-9)
    assert out["angle_degrees"] == pytest.approx(0.0, abs=1e-9)


def test_the_angle_is_between_the_two_bind_directions() -> None:
    out = derive_transform((1.0, 0.0, 0.0), (0.0, 1.0, 0.0))
    assert out["angle_degrees"] == pytest.approx(90.0, abs=1e-6)
    assert out["scale"] == pytest.approx(1.0, abs=1e-9)


def test_a_bind_at_the_origin_takes_the_translation_branch() -> None:
    # Retail writes a pure `b - a` there, and no stock Unreal translation mode carries it.
    out = derive_transform((0.0, 0.0, 0.0), (0.0, 0.0, 4.0))
    assert out["branch"] == "translation"
    assert out["translation_cm"] == pytest.approx(4.0, abs=1e-9)


def test_the_reflection_is_not_a_difference() -> None:
    """`source_to_unreal` negates Y, which preserves an angle and flips the axis, so the
    invariants this compares survive the basis change."""
    a, b = (1.0, 2.0, 3.0), (2.0, 1.0, 5.0)
    straight = derive_transform(a, b)
    mirrored = derive_transform((a[0], -a[1], a[2]), (b[0], -b[1], b[2]))
    assert straight["scale"] == pytest.approx(mirrored["scale"], abs=1e-9)
    assert straight["angle_degrees"] == pytest.approx(mirrored["angle_degrees"], abs=1e-9)


class _StubBinds:
    """`ContainerBinds` over hand-built rows, so the join is exercised without an export."""

    def __init__(self, containers):
        self._containers = containers

    def get(self, stem):
        rows = self._containers.get(stem)
        if rows is None:
            return None
        return {
            "rows": rows,
            "fold": {row[0].lower(): i for i, row in enumerate(rows)},
            "driven": {i for i in range(len(rows))},
        }


def _remap_row(**overrides):
    row = {
        "including_model": "models/pc/body.mdl",
        "bank": "models/shared/bank.mdl",
        "body_bone": "Bip01",
        "bank_bone": "Bip01",
        "bank_bone_index": "0",
        "mode": "0",
        "sub": "0",
        "chain_start": "-1",
        "chain_end": "-1",
        "matrix3x4": _matrix(IDENTITY),
    }
    row.update(overrides)
    return row


COMPARE_RETARGET_STEMS = {"models/pc/body.mdl": "body", "models/shared/bank.mdl": "bank"}


def _binds(body_translation, bank_translation):
    return _StubBinds({
        "body": [("Bip01", -1, body_translation, (0, 0, 0, 1))],
        "bank": [("Bip01", -1, bank_translation, (0, 0, 0, 1))],
    })


def test_a_pair_both_engines_copy_agrees() -> None:
    binds = _binds((0.0, 0.0, 10.0), (0.0, 0.0, 10.0))
    out = compare_retarget([_remap_row()], binds, COMPARE_RETARGET_STEMS)
    assert out["copy"]["agree"] == 1
    assert out["copy"]["diverges"] == 0


def test_retail_copying_a_pair_we_retarget_is_a_divergence() -> None:
    # Inside retail's own epsilon and outside Unreal's, which is the whole finding.
    binds = _binds((0.0, 0.0, 10.2), (0.0, 0.0, 10.0))
    out = compare_retarget([_remap_row()], binds, COMPARE_RETARGET_STEMS)
    assert out["copy"]["diverges"] == 1
    assert out["copy"]["examples"][0]["separation_cm"] == pytest.approx(0.2, abs=1e-5)


def test_retails_origin_branch_against_our_length_ratio() -> None:
    binds = _binds((0.14496, 0.0, 0.0), (2.96802, 0.0, 0.0))
    row = _remap_row(sub="1", matrix3x4=_matrix(IDENTITY, (3.11281 / 2.54, 0.0, 0.0)))
    out = compare_retarget([row], binds, COMPARE_RETARGET_STEMS)
    assert out["transform"]["diverges"] == 1
    example = out["transform"]["examples"][0]
    assert example["kind"] == "branch"
    assert example["retail"] == "translation"
    assert example["ours"] == "axis_angle"


def test_a_matching_length_ratio_agrees() -> None:
    scaled = tuple(tuple(3.0 * v for v in r) for r in IDENTITY)
    binds = _binds((0.0, 0.0, 6.0), (0.0, 0.0, 2.0))
    out = compare_retarget([_remap_row(sub="1", matrix3x4=_matrix(scaled))],
                           binds, COMPARE_RETARGET_STEMS)
    assert out["transform"]["agree"] == 1
    assert out["transform"]["diverges"] == 0


def test_an_undriven_bone_the_bank_does_not_carry_agrees() -> None:
    binds = _StubBinds({
        "body": [("Bip01", -1, (0.0, 0.0, 1.0), (0, 0, 0, 1)),
                 ("Bat", -1, (0.0, 0.0, 2.0), (0, 0, 0, 1))],
        "bank": [("Bip01", -1, (0.0, 0.0, 1.0), (0, 0, 0, 1))],
    })
    row = _remap_row(body_bone="Bat", bank_bone="", bank_bone_index="-1")
    out = compare_retarget([row], binds, COMPARE_RETARGET_STEMS)
    assert out["driven"]["undriven_agree"] == 1
    assert out["driven"]["undriven_we_carry"] == 0


def test_an_undriven_bone_our_bank_does_carry_is_reported() -> None:
    binds = _StubBinds({
        "body": [("Bat", -1, (0.0, 0.0, 2.0), (0, 0, 0, 1))],
        "bank": [("Bat", -1, (0.0, 0.0, 2.0), (0, 0, 0, 1))],
    })
    row = _remap_row(body_bone="Bat", bank_bone="", bank_bone_index="-1")
    out = compare_retarget([row], binds, COMPARE_RETARGET_STEMS)
    assert out["driven"]["undriven_we_carry"] == 1


def test_a_hub_with_no_container_is_not_applicable_rather_than_a_miss() -> None:
    """Retail composes its chain hop by hop; this repository retargets a bank straight onto
    the playing mesh, so an intermediate hop's remap describes a stage that does not exist."""
    binds = _binds((0.0, 0.0, 1.0), (0.0, 0.0, 1.0))
    row = _remap_row(bank="models/shared/hub.mdl")
    out = compare_retarget([row], binds, COMPARE_RETARGET_STEMS)
    assert out["rows_applicable"] == 0
    assert out["rows_not_applicable"] == 1
