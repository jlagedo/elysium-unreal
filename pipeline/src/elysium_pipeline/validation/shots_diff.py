"""Compare a `uv run elysium debug shots` capture against a kept baseline — the pixel half of the screenshot
regression harness (roadmap P2.9, sky-ambience B6).

`uv run elysium debug shots` renders each configured vantage to `$ELYSIUM_EXPORT_ROOT/_shots/<map>/`. That answers "what does it
look like now"; this answers "what changed", which is the question a regression asks. Promote a
run you trust to `$ELYSIUM_EXPORT_ROOT/_shots/_baseline/<map>/`, and every later run is a diff against it: a look
regression becomes a number and a heat map instead of an eyeball and a memory.

Baselines live under `$ELYSIUM_EXPORT_ROOT/`, so they are game-derived and gitignored like every capture. They
are a local instrument, not a committed fixture. `--save` writes a `baseline.json` beside each
promoted map naming the build commit, the map and the camera set it was captured against (R2.1,
MP-1.1), so a regression report can state what it is comparing against rather than "whatever HEAD
happened to be that day".

This comparison is an internal library module, not a public project-tooling
entrypoint. `uv run elysium debug shots` captures screenshots but does not promote
or compare baselines.

Exit status is 1 when any vantage moves more than `--max-changed` percent of its pixels by more
than `--tol` levels, so it can gate a change rather than merely report on one.
"""
import argparse
import json
import shutil
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image
from elysium_pipeline.paths import export_root, repo_root

OUT = export_root() / "_shots"
BASELINE = OUT / "_baseline"
DIFFDIR = OUT / "_diff"

#: R2.1 (MP-1.1): a baseline is only a useful regression witness if it names the build it was
#: captured against, so a later "did an untouched map change" question can be answered against the
#: right commit rather than "whatever HEAD happened to be that day".
BASELINE_MANIFEST = "baseline.json"


def git_commit(root: Path) -> str:
    """The working tree's HEAD commit, or `"unknown"` when git is unavailable (never raises --
    a baseline capture is a local, gitignored artifact and must not fail on a missing git binary).
    """
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=root,
            capture_output=True, text=True, check=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    commit = result.stdout.strip()
    return commit or "unknown"


def baseline_manifest(map_name: str, cameras, *, commit: str | None = None) -> dict:
    """The `baseline.json` shape: build commit + map + camera set, so a promoted baseline states
    what it was captured against without needing to cross-reference the copied capture manifest.
    """
    return {
        "commit": commit if commit is not None else git_commit(repo_root()),
        "map": map_name,
        "cameras": sorted(cameras),
    }


def write_baseline_manifest(dst: Path, map_name: str, cameras) -> dict:
    manifest = baseline_manifest(map_name, cameras)
    (dst / BASELINE_MANIFEST).write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


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
        print(f"no captures under {OUT} — run: uv run elysium debug shots")
        raise SystemExit(2)

    if args.save:
        for name in names:
            dst = BASELINE / name
            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(OUT / name, dst)
            cameras = sorted(shots_of(dst))
            manifest = write_baseline_manifest(dst, name, cameras)
            print(f"baseline: {name} <- {len(cameras)} shot(s) @ {manifest['commit'][:12]}")
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
