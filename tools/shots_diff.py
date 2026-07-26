"""Compare a `shots.bat` capture against a kept baseline — the pixel half of the screenshot
regression harness (roadmap P2.9, sky-ambience B6).

`shots.bat` renders each configured vantage to `out/_shots/<map>/`. That answers "what does it
look like now"; this answers "what changed", which is the question a regression asks. Promote a
run you trust to `out/_shots/_baseline/<map>/`, and every later run is a diff against it: a look
regression becomes a number and a heat map instead of an eyeball and a memory.

Baselines live under `out/`, so they are game-derived and gitignored like every capture. They
are a local instrument, not a committed fixture.

    python tools/shots_diff.py --save                 # promote the current run(s) to baseline
    python tools/shots_diff.py                        # diff every map that has a baseline
    python tools/shots_diff.py sm_hub_1               # one map
    python tools/shots_diff.py --tol 4 --max-changed 1.0

Exit status is 1 when any vantage moves more than `--max-changed` percent of its pixels by more
than `--tol` levels, so it can gate a change rather than merely report on one.
"""
import argparse
import json
import shutil
from pathlib import Path

import numpy as np
from PIL import Image

OUT = Path(__file__).resolve().parent / "out" / "_shots"
BASELINE = OUT / "_baseline"
DIFFDIR = OUT / "_diff"


def runs(map_filter=None):
    """Map names with a current capture (a directory holding a manifest)."""
    if not OUT.is_dir():
        return []
    return sorted(d.name for d in OUT.iterdir()
                  if d.is_dir() and not d.name.startswith("_")
                  and (d / "manifest.json").is_file()
                  and (map_filter is None or d.name == map_filter))


def shots_of(mapdir):
    """{vantage: Path} from a capture's manifest (only the ones that captured)."""
    man = json.loads((mapdir / "manifest.json").read_text(encoding="utf-8"))
    return {s["cam"]: mapdir / s["file"] for s in man.get("shots", [])
            if s.get("ok") and (mapdir / s["file"]).is_file()}


def compare(a_path, b_path, tol):
    """(mean abs diff, p99 abs diff, % pixels changed, diff image) between two shots."""
    a = np.asarray(Image.open(a_path).convert("RGB")).astype(np.int16)
    b = np.asarray(Image.open(b_path).convert("RGB")).astype(np.int16)
    if a.shape != b.shape:
        return None
    d = np.abs(a - b)
    worst = d.max(axis=2)                       # per-pixel, worst channel
    changed = float((worst > tol).mean() * 100.0)
    # Heat map: red where it moved, dimmed baseline underneath, so the diff is readable as a
    # picture of the scene rather than as noise on black.
    heat = (a // 3).astype(np.uint8)
    heat[..., 0] = np.maximum(heat[..., 0], np.minimum(worst * 4, 255).astype(np.uint8))
    return float(d.mean()), float(np.percentile(worst, 99)), changed, Image.fromarray(heat)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map", nargs="?", help="only this map (default: every map with a capture)")
    ap.add_argument("--save", action="store_true",
                    help="promote the current capture(s) to the baseline instead of diffing")
    ap.add_argument("--tol", type=int, default=2,
                    help="a pixel counts as changed when any channel moves more than this (0..255)")
    ap.add_argument("--max-changed", type=float, default=0.5,
                    help="fail when more than this percent of a vantage's pixels changed")
    args = ap.parse_args()

    names = runs(args.map)
    if not names:
        print(f"no captures under {OUT} — run shots.bat first")
        raise SystemExit(2)

    if args.save:
        for name in names:
            dst = BASELINE / name
            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(OUT / name, dst)
            print(f"baseline: {name} <- {len(shots_of(dst))} shot(s)")
        return

    DIFFDIR.mkdir(parents=True, exist_ok=True)
    failed, compared, missing = 0, 0, 0
    for name in names:
        base = BASELINE / name
        if not (base / "manifest.json").is_file():
            print(f"{name}: no baseline (run --save on a capture you trust)")
            continue
        cur, old = shots_of(OUT / name), shots_of(base)
        print(f"{name}:")
        for cam in sorted(set(cur) | set(old)):
            if cam not in cur or cam not in old:
                print(f"   {cam:<8} only in {'current' if cam in cur else 'baseline'}")
                missing += 1
                continue
            res = compare(old[cam], cur[cam], args.tol)
            if res is None:
                print(f"   {cam:<8} resolution differs — not comparable")
                missing += 1
                continue
            mean, p99, changed, heat = res
            bad = changed > args.max_changed
            compared += 1
            failed += bad
            if bad:
                heat.save(DIFFDIR / f"{name}_{cam}.png")
            print("   %-8s mean %5.2f  p99 %3.0f  changed %6.2f%%  %s"
                  % (cam, mean, p99, changed, "FAIL -> _diff/%s_%s.png" % (name, cam)
                     if bad else "ok"))

    print(f"\n{compared} vantage(s) compared, {failed} over {args.max_changed}%"
          + (f", {missing} unpaired" if missing else ""))
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
