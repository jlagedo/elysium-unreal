"""Compare movement-harness runs (roadmap 4.7).

`move.bat` writes one CSV per course under `tools/out/_move/`; this turns "the step still climbs"
into a number and a non-zero exit code. Modelled on `shots_diff.py`: a bare run diffs the current
output against the promoted baseline, `--save` promotes a run to be the new baseline.

    python tools/move_diff.py --save                 # promote the current run
    python tools/move_diff.py                        # diff against the baseline
    python tools/move_diff.py --hz 60 120 240        # cross-rate agreement check

The cross-rate mode is the one that answers the timestep question empirically: run the same course
at three frame rates and compare. Under the faithful variable step the three disagree -- that IS
retail's frame-rate dependence, and it is expected -- and under a non-zero `elysium.move.FixedStep`
they agree to tolerance.

Everything here is game-derived output, so `_move/` is gitignored like `_shots/`.
"""

from __future__ import annotations

import argparse
import csv
import json
import shutil
import sys
from pathlib import Path

OUT = Path(__file__).resolve().parent / "out" / "_move"
BASELINE = OUT / "baseline"

# Per-column tolerance, in Source units (or units/s). Positions are held tighter than velocities
# because a position drift is cumulative and a velocity one is not.
TOLERANCE = {
    "px": 0.5, "py": 0.5, "pz": 0.25,
    "vx": 2.0, "vy": 2.0, "vz": 2.0,
    "speed2d": 2.0,
}
# Columns that must match exactly -- a ground-state or duck-state flip is a behaviour change, not
# a numeric drift.
EXACT = ("onground", "ducked", "water")


def read_csv(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


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


def compare(a_path: Path, b_path: Path, by_time: bool = False) -> tuple[bool, list[str]]:
    """Align two runs and report the first divergence plus per-column worst deltas."""
    a, b = read_csv(a_path), read_csv(b_path)
    notes: list[str] = []

    if not a or not b:
        return False, ["  empty run"]

    if not by_time and len(a) != len(b):
        notes.append(f"  frame count differs: {len(a)} vs {len(b)}")

    worst: dict[str, float] = {}
    first_bad: str | None = None

    for i, (ra, rb) in enumerate(pair_rows(a, b, by_time)):
        for col in EXACT:
            if ra.get(col) != rb.get(col):
                if first_bad is None:
                    first_bad = f"  frame {i}: {col} {ra.get(col)} -> {rb.get(col)}"
        for col, tol in TOLERANCE.items():
            if col not in ra or col not in rb:
                continue
            d = abs(float(ra[col]) - float(rb[col]))
            if d > worst.get(col, 0.0):
                worst[col] = d
            if d > tol and first_bad is None:
                first_bad = f"  frame {i}: {col} {float(ra[col]):.3f} -> {float(rb[col]):.3f} (d={d:.3f} > {tol})"

    ok = first_bad is None and (by_time or len(a) == len(b))
    if first_bad:
        notes.append(first_bad)
    if worst:
        notes.append("  worst per column: " + ", ".join(
            f"{c}={v:.3f}" for c, v in sorted(worst.items(), key=lambda kv: -kv[1])))
    return ok, notes


def runs(directory: Path) -> dict[str, Path]:
    return {p.name: p for p in sorted(directory.glob("*.csv"))} if directory.is_dir() else {}


def cmd_save() -> int:
    current = runs(OUT)
    if not current:
        print(f"[move-diff] nothing to promote: no CSVs under {OUT}")
        return 1
    BASELINE.mkdir(parents=True, exist_ok=True)
    for name, path in current.items():
        shutil.copy2(path, BASELINE / name)
        summary = path.with_suffix(".json")
        if summary.exists():
            shutil.copy2(summary, BASELINE / summary.name)
    print(f"[move-diff] promoted {len(current)} run(s) to {BASELINE}")
    return 0


def cmd_diff() -> int:
    current, base = runs(OUT), runs(BASELINE)
    if not base:
        print(f"[move-diff] no baseline under {BASELINE} -- run with --save first")
        return 1
    if not current:
        print(f"[move-diff] no current run under {OUT}")
        return 1

    failures = 0
    for name, path in current.items():
        if name not in base:
            print(f"[move-diff] NEW      {name} (not in baseline)")
            continue
        ok, notes = compare(base[name], path)
        print(f"[move-diff] {'ok      ' if ok else 'REGRESS '}{name}")
        for n in notes:
            print(n)
        failures += 0 if ok else 1

    missing = set(base) - set(current)
    for name in sorted(missing):
        print(f"[move-diff] MISSING  {name} (in baseline, not in this run)")
        failures += 1

    print(f"[move-diff] {failures} regression(s)")
    return 1 if failures else 0


def cmd_hz(rates: list[int]) -> int:
    """Compare the same course across frame rates. Divergence here is a finding, not a failure."""
    by_course: dict[str, dict[int, Path]] = {}
    for p in sorted(OUT.glob("*.csv")):
        # <map>.<course>.<hz>hz.csv
        parts = p.stem.split(".")
        if len(parts) < 3 or not parts[-1].endswith("hz"):
            continue
        try:
            hz = int(parts[-1][:-2])
        except ValueError:
            continue
        by_course.setdefault(".".join(parts[:-1]), {})[hz] = p

    if not by_course:
        print(f"[move-diff] no rate-tagged runs under {OUT}")
        return 1

    for course, at in sorted(by_course.items()):
        have = [hz for hz in rates if hz in at]
        if len(have) < 2:
            print(f"[move-diff] {course}: need at least two of {rates}, have {sorted(at)}")
            continue
        ref = have[0]
        for hz in have[1:]:
            ok, notes = compare(at[ref], at[hz], by_time=True)
            verdict = "agrees" if ok else "DIVERGES"
            print(f"[move-diff] {course}: {ref}Hz vs {hz}Hz -- {verdict}")
            for n in notes:
                print(n)
            # The summary values are the headline: two runs can drift a little frame-to-frame and
            # still land the same jump apex and top speed, which is what "the feel is the same"
            # actually means.
            sa, sb = at[ref].with_suffix(".json"), at[hz].with_suffix(".json")
            if sa.exists() and sb.exists():
                ja, jb = json.loads(sa.read_text()), json.loads(sb.read_text())
                for key in ("peakSpeed2dUnits", "peakRiseUnits", "groundTransitions"):
                    if ja.get(key) != jb.get(key):
                        print(f"    {key}: {ja.get(key)} -> {jb.get(key)}")
    # Never a non-zero exit: whether the rates agree is the measurement, not the pass criterion.
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--save", action="store_true", help="promote the current run to the baseline")
    ap.add_argument("--hz", nargs="+", type=int, metavar="HZ",
                    help="compare the same course across these frame rates")
    args = ap.parse_args()

    if args.save:
        return cmd_save()
    if args.hz:
        return cmd_hz(args.hz)
    return cmd_diff()


if __name__ == "__main__":
    sys.exit(main())
