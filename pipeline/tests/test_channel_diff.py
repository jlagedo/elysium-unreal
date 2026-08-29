"""The channel comparator's refusals (CCC0).

What is asserted here is the *closing* of a silent pass: the comparator this replaces carried its
own table of which columns it knew about, so a column in neither of its tolerance classes was
written to disk and never checked. Every case below is a recording the differ must refuse rather
than partly accept.

Recordings are synthesised in the test, so nothing game-derived is committed and the fixtures rule
in `pipeline/CLAUDE.md` is untouched.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from elysium_pipeline.validation import channel_diff


def channel(name, *, scope="frame", kind="numeric", tolerance=0.5, value=None,
            speed_dependent=None):
    row = {
        "name": name,
        "producer": "move",
        "scope": scope,
        "kind": kind,
        "tolerance": tolerance,
        "precision": 0 if kind == "exact" else 3,
        "unit": "u",
        "speedDependent": bool(scope == "frame") if speed_dependent is None else speed_dependent,
        "help": name,
    }
    if value is not None:
        row["value"] = value
    return row


def write_run(directory: Path, stem: str, *, host="gym", channels=None, rows=None,
              baseline="committed", errors=None):
    """Write one recording: a manifest, and a CSV only when frame rows are given."""
    directory.mkdir(parents=True, exist_ok=True)
    channels = channels if channels is not None else [
        channel("pz"),
        channel("top_stand", scope="run", tolerance=0.25, value=18.0, speed_dependent=False),
    ]
    manifest = {
        "schema": 1,
        "run": {"harness": "move", "host": host, "course": stem, "baseline": baseline},
        "channels": channels,
    }
    if errors:
        manifest["errors"] = errors
    (directory / f"{stem}{channel_diff.MANIFEST_SUFFIX}").write_text(
        json.dumps(manifest), encoding="utf-8")

    if rows is not None:
        header = [c["name"] for c in channels if c["scope"] == "frame"]
        lines = [",".join(header)]
        lines += [",".join(str(r[h]) for h in header) for r in rows]
        (directory / f"{stem}.csv").write_text("\n".join(lines) + "\n", encoding="utf-8")


@pytest.fixture
def out(tmp_path: Path) -> Path:
    return tmp_path / "_move"


@pytest.fixture
def gym(tmp_path: Path) -> Path:
    return tmp_path / "baselines"


def diff(out: Path, gym: Path) -> int:
    return channel_diff.cmd_diff(out, gym)


def promote(out: Path, gym: Path) -> int:
    return channel_diff.cmd_promote(out, gym)


def test_promote_then_diff_is_clean(out, gym):
    write_run(out, "gym.riser_0.60hz")
    assert promote(out, gym) == 0
    assert diff(out, gym) == 0


def test_a_moved_run_channel_regresses(out, gym):
    write_run(out, "gym.riser_0.60hz")
    assert promote(out, gym) == 0
    # The step no longer climbs: the bracket the constant owns turns red.
    write_run(out, "gym.riser_0.60hz", channels=[
        channel("pz"),
        channel("top_stand", scope="run", tolerance=0.25, value=0.0, speed_dependent=False),
    ])
    assert diff(out, gym) == 1


def test_a_change_inside_tolerance_passes(out, gym):
    write_run(out, "gym.riser_0.60hz")
    assert promote(out, gym) == 0
    write_run(out, "gym.riser_0.60hz", channels=[
        channel("pz"),
        channel("top_stand", scope="run", tolerance=0.25, value=18.1, speed_dependent=False),
    ])
    assert diff(out, gym) == 0


def test_a_column_the_manifest_does_not_declare_is_refused(out, gym):
    write_run(out, "gym.riser_0.60hz", rows=[{"pz": 1.0}])
    # Slip an extra column into the CSV without declaring it — the exact case that used to be
    # written and never compared.
    csv_path = out / "gym.riser_0.60hz.csv"
    csv_path.write_text("pz,mystery\n1.0,2.0\n", encoding="utf-8")
    assert diff(out, gym) == 1


def test_a_declared_channel_with_no_column_is_refused(out, gym):
    write_run(out, "gym.riser_0.60hz", rows=[{"pz": 1.0}])
    (out / "gym.riser_0.60hz.csv").write_text("\n", encoding="utf-8")
    assert diff(out, gym) == 1


def test_a_numeric_channel_with_no_tolerance_is_refused(out, gym):
    write_run(out, "gym.riser_0.60hz", channels=[
        channel("pz", tolerance=None),
        channel("top_stand", scope="run", tolerance=0.25, value=18.0, speed_dependent=False),
    ])
    assert diff(out, gym) == 1


def test_an_unknown_kind_is_refused(out, gym):
    write_run(out, "gym.riser_0.60hz", channels=[channel("pz", kind="approximately")])
    assert diff(out, gym) == 1


def test_the_recorders_own_reported_errors_are_refused(out, gym):
    write_run(out, "gym.riser_0.60hz", errors=["channel 'pz' was written but not declared"])
    assert diff(out, gym) == 1


def test_a_channel_missing_from_the_run_is_refused(out, gym):
    write_run(out, "gym.riser_0.60hz")
    assert promote(out, gym) == 0
    write_run(out, "gym.riser_0.60hz", channels=[channel("pz")])
    assert diff(out, gym) == 1


def test_a_new_unbaselined_channel_is_refused(out, gym):
    write_run(out, "gym.riser_0.60hz")
    assert promote(out, gym) == 0
    write_run(out, "gym.riser_0.60hz", channels=[
        channel("pz"),
        channel("top_stand", scope="run", tolerance=0.25, value=18.0, speed_dependent=False),
        channel("reach_max", scope="run", tolerance=0.25, value=1.0, speed_dependent=False),
    ])
    assert diff(out, gym) == 1


def test_widening_a_tolerance_is_refused_rather_than_obeyed(out, gym):
    write_run(out, "gym.riser_0.60hz")
    assert promote(out, gym) == 0
    # A run that grants itself a looser bar would otherwise pass on the strength of its own say-so.
    write_run(out, "gym.riser_0.60hz", channels=[
        channel("pz"),
        channel("top_stand", scope="run", tolerance=99.0, value=0.0, speed_dependent=False),
    ])
    assert diff(out, gym) == 1


def test_a_baselined_run_that_vanishes_is_refused(out, gym):
    write_run(out, "gym.riser_0.60hz")
    write_run(out, "gym.riser_p1.60hz")
    assert promote(out, gym) == 0
    (out / f"gym.riser_p1.60hz{channel_diff.MANIFEST_SUFFIX}").unlink()
    assert diff(out, gym) == 1


def test_promote_refuses_a_run_that_does_not_validate(out, gym):
    write_run(out, "gym.riser_0.60hz", channels=[channel("pz", tolerance=0)])
    assert promote(out, gym) == 1
    assert list(gym.glob("*")) == []


def test_a_committed_baseline_does_not_compare_what_ccc7_can_move(out, gym):
    speedy = [
        channel("pz"),
        channel("peak_speed2d", scope="run", kind="numeric", tolerance=2.0, value=225.0,
                speed_dependent=True),
    ]
    write_run(out, "gym.flat.60hz", channels=speedy)
    assert promote(out, gym) == 0
    # Halving the speed is what `CCC7` may legitimately do, and it must not turn the gym red.
    halved = [
        channel("pz"),
        channel("peak_speed2d", scope="run", kind="numeric", tolerance=2.0, value=112.5,
                speed_dependent=True),
    ]
    write_run(out, "gym.flat.60hz", channels=halved)
    assert diff(out, gym) == 0


def test_a_sited_baseline_compares_its_frame_trace(out, gym):
    rows = [{"pz": 36.0}, {"pz": 36.0}]
    write_run(out, "sp_tutorial_1.flat.60hz", host="sp_tutorial_1", rows=rows)
    assert promote(out, gym) == 0
    assert diff(out, gym) == 0

    moved = [{"pz": 36.0}, {"pz": 99.0}]
    write_run(out, "sp_tutorial_1.flat.60hz", host="sp_tutorial_1", rows=moved)
    assert diff(out, gym) == 1


def yaw(name="move_yaw_vel"):
    return channel(name, kind="angle", tolerance=1.0)


def test_an_angle_compares_across_the_wrap(out, gym):
    # A backpedalling body sits exactly on the +/-180 boundary, so plain subtraction reports
    # ~360 degrees for two readings a fifth of a degree apart.
    rows = [{"pz": 36.0, "move_yaw_vel": 179.9}]
    channels = [channel("pz"), yaw()]
    write_run(out, "sp_tutorial_1.back.60hz", host="sp_tutorial_1",
              channels=channels, rows=rows)
    assert promote(out, gym) == 0

    wrapped = [{"pz": 36.0, "move_yaw_vel": -179.9}]
    write_run(out, "sp_tutorial_1.back.60hz", host="sp_tutorial_1",
              channels=channels, rows=wrapped)
    assert diff(out, gym) == 0


def test_an_angle_still_catches_a_real_turn(out, gym):
    channels = [channel("pz"), yaw()]
    write_run(out, "sp_tutorial_1.back.60hz", host="sp_tutorial_1",
              channels=channels, rows=[{"pz": 36.0, "move_yaw_vel": 0.0}])
    assert promote(out, gym) == 0

    write_run(out, "sp_tutorial_1.back.60hz", host="sp_tutorial_1",
              channels=channels, rows=[{"pz": 36.0, "move_yaw_vel": 90.0}])
    assert diff(out, gym) == 1


def test_an_angle_with_no_tolerance_is_refused_like_a_numeric_one(out, gym):
    write_run(out, "gym.flat.60hz", channels=[
        channel("pz"),
        channel("move_yaw_vel", kind="angle", tolerance=None),
    ])
    assert diff(out, gym) == 1
