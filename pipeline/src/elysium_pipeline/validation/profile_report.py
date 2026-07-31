#!/usr/bin/env python3
"""Turn the headless profiler's output into the roadmap 0.1 baseline table.

The C++ harness (-ElysiumProfile, Source/ElysiumUE/Private/ElysiumProfiler.cpp) writes,
per map, under $ELYSIUM_EXPORT_ROOT/_profile/:
  <map>_summary.json          — vantages, averaged stat-unit ms, SM6/adapter, light count
  elysium_<map>_<cam>*.csv    — the CSV profiler capture (per-pass GPU stats when the
                                engine was launched with -csvGpuStats)

This reads both and emits, per vantage, the appendix table
(Lumen GI / reflections / MegaLights / ShadowDepths+VSM / Total GPU) plus a raw dump of
the heaviest GPU passes (so the real stat names are always visible, whatever the engine
version calls them). Output goes to stdout and to <map>_report.md. Stdlib only.
"""
from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import sys
from elysium_pipeline.paths import export_root

PROFILE_DIR = os.path.join(os.fspath(export_root()), "_profile")

# The CSV profiler prefixes true per-pass GPU timings (milliseconds) with "GPU/". Other
# "GPU"-ish columns (DrawCall/* are call *counts*, RayTracingGeometry/* are megabytes,
# Exclusive/* are CPU-thread ms) must be ignored, or counts/MB get averaged in as if ms.
GPU_PREFIX = "GPU/"

# Roadmap appendix rows -> the exact GPU/ pass suffixes that sum into them. VtMB has no
# distinct VirtualShadowMaps pass; VSM depth cost lands in ShadowDepths.
BUCKETS = [
    ("Lumen GI",           ("LumenScreenProbeGather", "UpdateLumenSceneBuffers")),
    ("Lumen reflections",  ("LumenReflections",)),
    ("MegaLights",         ("MegaLights", "MegaLightsLightPowerDelta")),
    ("ShadowDepths / VSM", ("ShadowDepths", "ShadowProjection")),
]


def newest_csv(map_name: str, cam: str) -> str | None:
    pat = os.path.join(PROFILE_DIR, f"elysium_{map_name}_{cam}*.csv")
    hits = sorted(glob.glob(pat), key=os.path.getmtime, reverse=True)
    return hits[0] if hits else None


def column_means(csv_path: str) -> dict[str, float]:
    """Mean of each numeric column across captured frames (non-numeric cells ignored)."""
    sums: dict[str, float] = {}
    counts: dict[str, int] = {}
    with open(csv_path, newline="", encoding="utf-8", errors="replace") as fh:
        reader = csv.reader(fh)
        try:
            header = next(reader)
        except StopIteration:
            return {}
        header = [h.strip() for h in header]
        for row in reader:
            for name, cell in zip(header, row):
                try:
                    v = float(cell)
                except (ValueError, TypeError):
                    continue
                sums[name] = sums.get(name, 0.0) + v
                counts[name] = counts.get(name, 0) + 1
    return {n: sums[n] / counts[n] for n in sums if counts[n] > 0}


def gpu_passes(means: dict[str, float]) -> dict[str, float]:
    """Just the GPU/ per-pass timings, keyed by suffix (prefix stripped)."""
    return {n[len(GPU_PREFIX):]: v for n, v in means.items() if n.startswith(GPU_PREFIX)}


def bucket_ms(passes: dict[str, float]) -> dict[str, float]:
    out: dict[str, float] = {}
    for label, suffixes in BUCKETS:
        out[label] = sum(passes.get(s, 0.0) for s in suffixes)
    return out


def heaviest_gpu(passes: dict[str, float], top: int = 20) -> list[tuple[str, float]]:
    hot = [(n, v) for n, v in passes.items() if v > 0.0]
    hot.sort(key=lambda kv: kv[1], reverse=True)
    return hot[:top]


def report(map_name: str) -> int:
    summary_path = os.path.join(PROFILE_DIR, f"{map_name}_summary.json")
    if not os.path.exists(summary_path):
        print(f"[profile] no summary at {summary_path} — run: uv run elysium debug profile {map_name}",
              file=sys.stderr)
        return 1
    with open(summary_path, encoding="utf-8") as fh:
        summ = json.load(fh)

    lines: list[str] = []
    def emit(s: str = "") -> None:
        print(s)
        lines.append(s)

    sm6 = summ.get("sm6")
    emit(f"# Profiling baseline — {map_name}")
    emit()
    emit(f"- RHI: {summ.get('rhi')}  ·  adapter: {summ.get('adapter')}")
    emit(f"- SM6: {'yes' if sm6 else 'NO — Lumen/MegaLights/VSM OFF, baseline invalid'}")
    emit(f"- world lights: {summ.get('world_lights')}  ·  "
         f"warmup {summ.get('warmup_frames')} / capture {summ.get('capture_frames')} frames")
    emit()

    for v in summ.get("vantages", []):
        cam = v["cam"]
        loc = v.get("loc", [0, 0, 0])
        rot = v.get("rot", [0, 0, 0])
        emit(f"## vantage `{cam}`  loc {loc}  rot {rot}")
        emit()
        emit(f"- stat unit (avg): game {v['game_ms']:.2f} ms · GPU {v['gpu_ms']:.2f} ms "
             f"(render thread {v['render_ms']:.2f} ms — ~0 = idle-waiting on the GPU, i.e. GPU-bound)")
        emit()

        csv_path = newest_csv(map_name, cam)
        if not csv_path:
            emit(f"_(no CSV found for `elysium_{map_name}_{cam}*.csv` — was -csvGpuStats set?)_")
            emit()
            continue

        means = column_means(csv_path)
        passes = gpu_passes(means)
        buckets = bucket_ms(passes)
        total_gpu = v["gpu_ms"]
        sum_passes = sum(passes.values())

        emit("| Pass | ms @1440p (dev GPU) | Notes |")
        emit("|---|---|---|")
        emit(f"| Lumen GI | {buckets['Lumen GI']:.2f} | LumenScreenProbeGather + scene update |")
        emit(f"| Lumen reflections | {buckets['Lumen reflections']:.2f} | |")
        emit(f"| MegaLights | {buckets['MegaLights']:.2f} | 0.2: should dominate ShadowDepths |")
        emit(f"| ShadowDepths / VSM | {buckets['ShadowDepths / VSM']:.2f} | 0.2: small ⇒ MegaLights carrying local lights |")
        emit(f"| Total GPU | {total_gpu:.2f} | RHIGetGPUFrameCycles (whole frame) |")
        emit(f"| _(sum of GPU passes)_ | {sum_passes:.2f} | _sanity: ≈ Total GPU_ |")
        emit()

        heavy = heaviest_gpu(passes)
        if heavy:
            emit("<details><summary>heaviest GPU passes (GPU/ ms)</summary>")
            emit()
            emit("| pass | ms |")
            emit("|---|---|")
            for name, ms in heavy:
                emit(f"| {name} | {ms:.3f} |")
            emit()
            emit("</details>")
            emit()
        else:
            emit("_(no GPU-pass columns in CSV — launch with -csvGpuStats to get per-pass ms)_")
            emit()

    out_md = os.path.join(PROFILE_DIR, f"{map_name}_report.md")
    with open(out_md, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"\n[profile] wrote {out_md}", file=sys.stderr)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--map", default="sp_tutorial_1", help="map name under $ELYSIUM_EXPORT_ROOT")
    args = ap.parse_args()
    return report(args.map)


if __name__ == "__main__":
    raise SystemExit(main())
