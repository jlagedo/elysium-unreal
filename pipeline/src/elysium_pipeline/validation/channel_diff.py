"""Compare harness channel recordings (CCC0).

`uv run elysium debug move` records one run per course through
`FElysiumChannelRecorder`: a CSV of the frame channels it declared, and a
`.channels.json` manifest beside it carrying those declarations, the constants
the geometry was derived from, and the run channels. This turns "the step still
climbs" into a number and a non-zero exit code, and it is chained into `debug
move` the way `profile_report` is chained into `debug profile`.

**The comparison rule comes from the manifest, never from here.** The
comparator this replaces carried its own table of which columns it knew about,
so a column in neither of its two tolerance classes was written to disk and
silently never checked. Registering a channel in `ElysiumChannels::Defs()` is
now registering its comparison, and this module *refuses* a recording it cannot
fully account for rather than passing the part it understands.

Two baseline roots, because the two hosts differ in kind:

* **Gym runs** are made in an empty stage world on geometry derived from this
  repository's own constants, so nothing game-derived is in them and they are
  committed (`--gym-baseline`). What is compared there is the **run** channels —
  how far the body got, how high it stood or reached — which are written to
  saturate and are therefore the same at any gait. A per-frame trace is not:
  *when* a body reaches a feature moves with its speed even when *whether* it
  does not, so committing one would make every gym course turn red the day
  `CCC7` settles the speed authority, which is exactly the "expectation that
  moves with the thing it measures" the gym exists to avoid.
* **Sited runs** are made on a real map, so they are game-derived and their
  baseline stays under the gitignored `$ELYSIUM_EXPORT_ROOT/_move/baseline/`.
  Those are compared in full, frame rows included — they are re-promoted freely,
  so there is nothing to protect from a deliberate change.

The cross-rate mode is the one that answers the timestep question empirically:
run the same course at three frame rates and compare. Under the faithful
variable step the three disagree -- that IS retail's frame-rate dependence, and
it is expected -- and under a non-zero `elysium.move.FixedStep` they agree to
tolerance.
"""

from __future__ import annotations

import argparse
import csv
import json
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path

from elysium_pipeline.paths import export_root

MANIFEST_SUFFIX = ".channels.json"


@dataclass(frozen=True)
class Channel:
    name: str
    producer: str
    scope: str
    kind: str
    tolerance: float
    precision: int
    unit: str
    speed_dependent: bool
    value: float | None


def stem_of(manifest_path: Path) -> str:
    return manifest_path.name[: -len(MANIFEST_SUFFIX)]


def read_manifest(path: Path) -> tuple[dict, dict[str, Channel], list[str]]:
    """Return (run metadata, channels by name, the recorder's own reported errors)."""
    data = json.loads(path.read_text(encoding="utf-8"))
    channels: dict[str, Channel] = {}
    for row in data.get("channels", []):
        name = row.get("name", "")
        channels[name] = Channel(
            name=name,
            producer=row.get("producer", ""),
            scope=row.get("scope", ""),
            kind=row.get("kind", ""),
            # `None` rather than a default: a numeric channel with no tolerance is a channel with
            # no rule, and substituting one here would recreate the silent pass this replaces.
            tolerance=row.get("tolerance", None),
            precision=row.get("precision", 0),
            unit=row.get("unit", ""),
            speed_dependent=bool(row.get("speedDependent", False)),
            value=row.get("value", None),
        )
    return data.get("run", {}), channels, list(data.get("errors", []))


def read_csv(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def validate(stem: str, manifest_path: Path) -> list[str]:
    """Every reason this recording cannot be compared. Runs with or without a baseline."""
    problems: list[str] = []
    try:
        _run, channels, recorder_errors = read_manifest(manifest_path)
    except (OSError, ValueError) as exc:
        return [f"{stem}: unreadable manifest ({exc})"]

    for message in recorder_errors:
        problems.append(f"{stem}: the recorder reported '{message}'")

    if not channels:
        problems.append(f"{stem}: the manifest declares no channels")

    for name, ch in channels.items():
        if ch.kind not in ("numeric", "exact"):
            problems.append(f"{stem}: channel '{name}' has no comparison rule (kind={ch.kind!r})")
        elif ch.kind == "numeric" and not (isinstance(ch.tolerance, (int, float))
                                           and ch.tolerance > 0):
            problems.append(
                f"{stem}: numeric channel '{name}' declares no usable tolerance "
                f"({ch.tolerance!r}) — it would be written and never checked")
        if ch.scope not in ("frame", "run"):
            problems.append(f"{stem}: channel '{name}' has no scope (scope={ch.scope!r})")
        if ch.scope == "run" and ch.value is None:
            problems.append(f"{stem}: run channel '{name}' carries no value")

    # The CSV's columns and the manifest's frame channels have to be the same set, in both
    # directions: a column nothing declares is unchecked, and a declaration with no column is a
    # channel that quietly stopped being written.
    csv_path = manifest_path.with_name(stem + ".csv")
    if csv_path.exists():
        with csv_path.open(newline="", encoding="utf-8") as fh:
            header = next(csv.reader(fh), [])
        declared = {n for n, c in channels.items() if c.scope == "frame"}
        for column in header:
            if column not in declared:
                problems.append(f"{stem}: column '{column}' is not declared in the manifest")
        for name in sorted(declared - set(header)):
            problems.append(f"{stem}: frame channel '{name}' is declared but has no column")
    return problems


def pair_rows(a: list[dict], b: list[dict], by_time: bool):
    """Yield aligned row pairs.

    Frame-index alignment is right for two runs at the same rate. Across rates it is not: a 120 Hz
    run has twice the rows for the same wall-clock, so the runs must be aligned on **elapsed time**
    or every comparison degenerates into "they are at different points in the course".
    """
    if not by_time:
        yield from zip(a, b)
        return

    dt_b = float(b[0]["dt"]) if b else 0.0
    if dt_b <= 0.0:
        return
    for i, ra in enumerate(a):
        t = i * float(ra["dt"])
        j = int(round(t / dt_b))
        if 0 <= j < len(b):
            yield ra, b[j]


def compare_declarations(base: dict[str, Channel], cur: dict[str, Channel],
                         committed: bool) -> list[str]:
    """Both directions, plus rule drift.

    Reading the tolerance off the current run alone would let a run widen its own bar and pass;
    a channel present now and absent from the baseline is nobody's assertion at all.
    """
    problems: list[str] = []
    for name, ch in base.items():
        other = cur.get(name)
        if other is None:
            problems.append(f"  channel '{name}' is in the baseline and missing from this run")
            continue
        if other.kind != ch.kind or other.tolerance != ch.tolerance:
            problems.append(
                f"  channel '{name}' changed its rule: "
                f"{ch.kind}/{ch.tolerance} -> {other.kind}/{other.tolerance} — re-promote "
                f"deliberately rather than comparing against the old bar")
    for name in cur:
        if name not in base:
            problems.append(f"  channel '{name}' is new and not baselined — re-promote")
    # A committed baseline holds no frame rows, so a frame channel there would be a declaration
    # nothing ever compares.
    if committed:
        for name, ch in base.items():
            if ch.scope == "frame" and ch.value is not None:
                problems.append(f"  frame channel '{name}' carries a value in a committed baseline")
    return problems


def compare_run_channels(base: dict[str, Channel], cur: dict[str, Channel],
                         committed: bool) -> tuple[bool, list[str], int]:
    """Compare the per-run values. Returns (ok, notes, how many were deferred)."""
    notes: list[str] = []
    ok = True
    deferred = 0
    for name, ch in sorted(base.items()):
        if ch.scope != "run" or ch.value is None:
            continue
        other = cur.get(name)
        if other is None or other.value is None:
            continue          # already reported by compare_declarations
        # A committed baseline deliberately does not assert what the speed authority can move.
        if committed and ch.speed_dependent:
            deferred += 1
            continue
        if ch.kind == "exact":
            if float(other.value) != float(ch.value):
                notes.append(f"  {name}: {ch.value} -> {other.value}")
                ok = False
        else:
            delta = abs(float(other.value) - float(ch.value))
            if delta > float(ch.tolerance):
                notes.append(f"  {name}: {ch.value} -> {other.value} "
                             f"(d={delta:.3f} > {ch.tolerance}{ch.unit})")
                ok = False
    return ok, notes, deferred


def compare_frames(base_csv: Path, cur_csv: Path, channels: dict[str, Channel],
                   by_time: bool = False) -> tuple[bool, list[str]]:
    """Align two traces and report the first divergence plus per-channel worst deltas."""
    a, b = read_csv(base_csv), read_csv(cur_csv)
    notes: list[str] = []

    if not a or not b:
        return False, ["  empty run"]
    if not by_time and len(a) != len(b):
        notes.append(f"  frame count differs: {len(a)} vs {len(b)}")

    worst: dict[str, float] = {}
    first_bad: str | None = None

    for i, (ra, rb) in enumerate(pair_rows(a, b, by_time)):
        for name, ch in channels.items():
            if ch.scope != "frame" or name not in ra or name not in rb:
                continue
            if ch.kind == "exact":
                if ra[name] != rb[name] and first_bad is None:
                    first_bad = f"  frame {i}: {name} {ra[name]} -> {rb[name]}"
                continue
            d = abs(float(ra[name]) - float(rb[name]))
            if d > worst.get(name, 0.0):
                worst[name] = d
            if d > float(ch.tolerance) and first_bad is None:
                first_bad = (f"  frame {i}: {name} {float(ra[name]):.3f} -> "
                             f"{float(rb[name]):.3f} (d={d:.3f} > {ch.tolerance})")

    ok = first_bad is None and (by_time or len(a) == len(b))
    if first_bad:
        notes.append(first_bad)
    if worst:
        notes.append("  worst per channel: " + ", ".join(
            f"{c}={v:.3f}" for c, v in sorted(worst.items(), key=lambda kv: -kv[1])))
    return ok, notes


def manifests(directory: Path) -> dict[str, Path]:
    if not directory.is_dir():
        return {}
    return {stem_of(p): p for p in sorted(directory.glob("*" + MANIFEST_SUFFIX))}


def is_gym(manifest_path: Path) -> bool:
    try:
        run, _channels, _errors = read_manifest(manifest_path)
    except (OSError, ValueError):
        return False
    return run.get("host") == "gym"


def is_deferred(manifest_path: Path) -> bool:
    try:
        run, _channels, _errors = read_manifest(manifest_path)
    except (OSError, ValueError):
        return False
    return run.get("baseline") == "deferred"


def cmd_promote(out: Path, gym_baseline: Path) -> int:
    current = manifests(out)
    if not current:
        print(f"[channels] nothing to promote: no runs under {out}")
        return 1

    problems: list[str] = []
    for stem, path in current.items():
        problems.extend(validate(stem, path))
    if problems:
        print("[channels] refusing to promote a run that does not validate:")
        for p in problems:
            print(f"  {p}")
        return 1

    local_baseline = out / "baseline"
    promoted = 0
    for stem, path in current.items():
        if is_gym(path):
            # Only the manifest: the trace is not what a committed baseline asserts, and carrying
            # one would put a speed-dependent artefact in the repository.
            gym_baseline.mkdir(parents=True, exist_ok=True)
            target = gym_baseline / path.name
        else:
            local_baseline.mkdir(parents=True, exist_ok=True)
            target = local_baseline / path.name
            csv_path = path.with_name(stem + ".csv")
            if csv_path.exists():
                shutil.copy2(csv_path, local_baseline / csv_path.name)
        shutil.copy2(path, target)
        print(f"[channels] promoted {stem} -> {target}")
        promoted += 1

    print(f"[channels] promoted {promoted} run(s)")
    return 0


def cmd_diff(out: Path, gym_baseline: Path) -> int:
    current = manifests(out)
    if not current:
        print(f"[channels] no runs under {out}")
        return 1

    local_baseline = out / "baseline"
    failures = 0
    deferred_runs = 0
    checked = 0

    for stem, path in sorted(current.items()):
        problems = validate(stem, path)
        if problems:
            for p in problems:
                print(f"[channels] REFUSED  {p}")
            failures += 1
            continue

        committed = is_gym(path)
        base_dir = gym_baseline if committed else local_baseline
        base_path = base_dir / path.name
        if not base_path.exists():
            print(f"[channels] NEW      {stem} (no baseline under {base_dir})")
            continue

        _base_run, base_channels, _ = read_manifest(base_path)
        _cur_run, cur_channels, _ = read_manifest(path)

        notes = compare_declarations(base_channels, cur_channels, committed)
        ok = not notes

        values_ok, value_notes, deferred = compare_run_channels(
            base_channels, cur_channels, committed)
        ok = ok and values_ok
        notes.extend(value_notes)

        # A sited baseline carries its trace, and comparing it is the whole point there.
        base_csv = base_path.with_name(stem + ".csv")
        cur_csv = path.with_name(stem + ".csv")
        if not committed and base_csv.exists() and cur_csv.exists():
            frames_ok, frame_notes = compare_frames(base_csv, cur_csv, base_channels)
            ok = ok and frames_ok
            notes.extend(frame_notes)

        if is_deferred(path):
            deferred_runs += 1
        checked += 1
        print(f"[channels] {'ok      ' if ok else 'REGRESS '}{stem}"
              + (f"  ({deferred} channel(s) deferred to CCC7)" if deferred else ""))
        for n in notes:
            print(n)
        failures += 0 if ok else 1

    missing = set(manifests(gym_baseline)) | set(manifests(local_baseline))
    for stem in sorted(missing - set(current)):
        print(f"[channels] MISSING  {stem} (in a baseline, not in this run)")
        failures += 1

    print(f"[channels] {checked} run(s) checked, {deferred_runs} awaiting CCC7, "
          f"{failures} regression(s)")
    return 1 if failures else 0


def cmd_hz(out: Path, rates: list[int]) -> int:
    """Compare the same course across frame rates. Divergence here is a finding, not a failure."""
    by_course: dict[str, dict[int, Path]] = {}
    for stem, path in manifests(out).items():
        # <host>.<course>.<hz>hz
        parts = stem.split(".")
        if len(parts) < 3 or not parts[-1].endswith("hz"):
            continue
        try:
            hz = int(parts[-1][:-2])
        except ValueError:
            continue
        by_course.setdefault(".".join(parts[:-1]), {})[hz] = path

    if not by_course:
        print(f"[channels] no rate-tagged runs under {out}")
        return 1

    for course, at in sorted(by_course.items()):
        have = [hz for hz in rates if hz in at]
        if len(have) < 2:
            print(f"[channels] {course}: need at least two of {rates}, have {sorted(at)}")
            continue
        ref = have[0]
        for hz in have[1:]:
            _run_a, channels_a, _ = read_manifest(at[ref])
            _run_b, channels_b, _ = read_manifest(at[hz])
            csv_a = at[ref].with_name(stem_of(at[ref]) + ".csv")
            csv_b = at[hz].with_name(stem_of(at[hz]) + ".csv")
            if not (csv_a.exists() and csv_b.exists()):
                continue
            ok, notes = compare_frames(csv_a, csv_b, channels_a, by_time=True)
            print(f"[channels] {course}: {ref}Hz vs {hz}Hz -- "
                  f"{'agrees' if ok else 'DIVERGES'}")
            for n in notes:
                print(n)
            # The run channels are the headline: two runs can drift a little frame-to-frame and
            # still land the same jump apex and top speed, which is what "the feel is the same"
            # actually means.
            for name, ch in sorted(channels_a.items()):
                other = channels_b.get(name)
                if ch.scope != "run" or other is None or ch.value is None:
                    continue
                if ch.value != other.value:
                    print(f"    {name}: {ch.value} -> {other.value}")
    # Never a non-zero exit: whether the rates agree is the measurement, not the pass criterion.
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    # The tracked root is passed in and never constructed here: no pipeline module builds a
    # repository-relative path (`pipeline/CLAUDE.md`).
    ap.add_argument("--gym-baseline", type=Path, required=True,
                    help="the tracked directory holding the committed gym baselines")
    ap.add_argument("--out", type=Path, default=None,
                    help="the run directory (default $ELYSIUM_EXPORT_ROOT/_move)")
    ap.add_argument("--promote", action="store_true",
                    help="promote the current runs to their baselines")
    ap.add_argument("--hz", nargs="+", type=int, metavar="HZ",
                    help="compare the same course across these frame rates")
    args = ap.parse_args()

    out = args.out if args.out is not None else export_root() / "_move"

    if args.promote:
        return cmd_promote(out, args.gym_baseline)
    if args.hz:
        return cmd_hz(out, args.hz)
    return cmd_diff(out, args.gym_baseline)


if __name__ == "__main__":
    sys.exit(main())
