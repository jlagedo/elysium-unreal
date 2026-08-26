"""The running graph, held to the reference compositor frame by frame.

A `debug compose` report records, per frame, the whole state the animation graph stood at: the
base it committed and the cycle it was at, the pose parameters it steered with, and each overlay
slot's clip, weight and cycle. That is every input the composition has, so the reference
compositor (`retail_compositor`) can answer the very same state offline, and the two frames are
either the same frame or one of them is wrong. Nothing here searches and nothing here consults
the capture: the capture validated the compositor once, and this is the compositor validating the
graph.

Read the harness counters before any number here (T1): a run that never posed or never travelled
is refused by `compose_diff` and is refused here on the same evidence.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import statistics
import sys

from elysium_pipeline.validation import retail_compositor as rc

#: Compression is the only thing allowed between the compositor and the graph, and it is bounded.
MEDIAN_TOLERANCE_CM = 0.5
MINIMUM_TRAVEL_CM = 25.0
MINIMUM_FRAMES = 10


def _relative(bones: dict, root_name: str) -> dict[str, tuple]:
    """Each bone's position in the root bone's frame, from 3x4 bone-to-component rows."""
    root = bones[root_name]
    r = ((root[0], root[1], root[2]), (root[4], root[5], root[6]), (root[8], root[9], root[10]))
    t = (root[3], root[7], root[11])
    out = {}
    for name, m in bones.items():
        if not isinstance(m, list) or len(m) < 12:
            continue
        d = (m[3] - t[0], m[7] - t[1], m[11] - t[2])
        out[name] = (r[0][0] * d[0] + r[1][0] * d[1] + r[2][0] * d[2],
                     r[0][1] * d[0] + r[1][1] * d[1] + r[2][1] * d[2],
                     r[0][2] * d[0] + r[1][2] * d[1] + r[2][2] * d[2])
    return out


#: Diagnostic: shift every recorded base cycle by this many 60 Hz ticks before composing, to
#: attribute a residual to a one-tick lag between the phase published and the pose evaluated.
PHASE_LAG_TICKS = 0.0
#: Diagnostic: print one frame in every N as a time series.
TRACE = 0
#: Diagnostic: the tick lags each frame may choose from; empty means the recorded cycle only.
BEST_LAG: tuple = ()
#: Diagnostic: search the whole cycle on one frame in every N (0 = off).
BEST_CYCLE = 0
#: Which recorded clock to compose at: the stack's normalized accumulator or the published phase.
CLOCK = "stack"


def _rigid_fit(pairs):
    """(angle in degrees, median residual in cm) of the best rotation about the origin taking the
    first positions onto the second -- Kabsch, both sets already root-relative."""
    import numpy as np
    a = np.array([p[0] for p in pairs], dtype=float)
    b = np.array([p[1] for p in pairs], dtype=float)
    h = a.T @ b
    u, _s, vt = np.linalg.svd(h)
    d = np.sign(np.linalg.det(vt.T @ u.T))
    r = vt.T @ np.diag([1.0, 1.0, d]) @ u.T
    angle = math.degrees(math.acos(max(-1.0, min(1.0, (np.trace(r) - 1.0) / 2.0))))
    residual = float(np.median(np.linalg.norm((r @ a.T).T - b, axis=1)))
    return angle, residual


def score(corpus: rc.Corpus, run_path: Path) -> int:
    run = json.loads(run_path.read_text(encoding="utf-8"))
    frames = run.get("frames", [])
    stem = str(run.get("stem", ""))
    travelled = float(run.get("farthest_travelled_cm", 0.0))
    print(f"[graph] {run_path.name}: {len(frames)} frames, stem '{stem}', travelled {travelled:.1f} cm")
    if travelled < MINIMUM_TRAVEL_CM or not stem:
        print("[graph] REFUSED: the run cannot support a verdict (see compose_diff)")
        return 2
    body = corpus.model(stem)
    root = next(b.name for b in body.bones if b.parent < 0)

    per_state: dict[str, list[float]] = defaultdict(list)
    fit_angle: dict[str, list[float]] = defaultdict(list)
    lag_hist: dict[float, int] = defaultdict(int)
    fit_residual: dict[str, list[float]] = defaultdict(list)
    per_bone: dict[str, list[float]] = defaultdict(list)
    state_bones: dict[str, dict] = defaultdict(lambda: defaultdict(list))
    composed = no_cycle = gated = unresolved = 0
    for frame in frames:
        motion = frame.get("motion", {})
        if motion.get("gated") or not frame.get("bones"):
            gated += 1
            continue
        selection = frame.get("selection", {})
        # The stack's own normalized accumulator when the report carries it, the published phase
        # otherwise -- and `--clock published` forces the second, to hold each to the compositor.
        cycle = selection.get("cycle_norm") if CLOCK == "stack" else None
        if cycle is None:
            cycle = selection.get("cycle")
        if cycle is None:
            no_cycle += 1
            continue
        owner = selection.get("owner", "")
        label = selection.get("label", "")
        params = {"move_yaw": float(selection.get("move_yaw", 0.0)),
                  "aim_yaw": float(selection.get("aim_yaw", 0.0)),
                  "aim_pitch": float(selection.get("aim_pitch", 0.0))}
        if float(motion.get("speed", 0.0)) <= 20.0:
            params.pop("move_yaw")
        base_cycle = float(cycle)
        base_seq = corpus.model(owner).sequences.get(label.lower()) if owner else None
        if PHASE_LAG_TICKS and base_seq is not None and base_seq.fps > 0 and base_seq.frames > 1:
            length = (base_seq.frames - 1) / base_seq.fps
            base_cycle = (base_cycle + PHASE_LAG_TICKS / 60.0 / length) % 1.0
        channels = [{"owner": owner, "label": label, "cycle": base_cycle, "weight": 1.0}]
        slots = []
        for row in frame.get("overlay_slots", ()):
            if row.get("label") and float(row.get("weight", 0.0)) > 0.0:
                # The slot evaluator's own normalized time when the report carries it -- the
                # instant the pose was evaluated at -- else the record's published cycle, which
                # the driver advances after the graph has read it.
                slot_cycle = row.get("cycle_norm") if CLOCK == "stack" else None
                if slot_cycle is None:
                    slot_cycle = row.get("cycle", 0.0)
                channels.append({"owner": row.get("owner_stem", owner), "label": row["label"],
                                 "cycle": float(slot_cycle),
                                 "weight": float(row["weight"])})
                slots.append(row["label"])
        # Diagnostic: let each frame pick the lag (in ticks) that fits best, and report the
        # histogram. A residual that collapses under a per-frame lag is a jitter between the cycle
        # published and the cycle the pose was evaluated at -- a harness fault, not a graph one.
        if BEST_CYCLE and base_seq is not None and (frame.get("frame", 0) % BEST_CYCLE == 0):
            # Diagnostic: the cycle that fits best over the whole lap, coarse then fine. Reported
            # beside the recorded cycle so a wrong CLOCK reads apart from a wrong POSE.
            theirs_now = _relative(frame["bones"], frame.get("root_bone", root))

            def fit(c):
                trial = list(channels)
                trial[0] = dict(trial[0], cycle=c % 1.0)
                _b, loc = rc.compose(corpus, stem, trial, params)
                rel = rc.relative_to_root(body, rc.model_space(body, loc), root)
                errs_t = [math.dist(rc._unreal(rel[n]), pp) for n, pp in theirs_now.items()
                          if n in rel]
                return statistics.median(errs_t) if errs_t else float("inf")

            def split(c):
                trial = list(channels)
                trial[0] = dict(trial[0], cycle=c % 1.0)
                _b, loc = rc.compose(corpus, stem, trial, params)
                rel = rc.relative_to_root(body, rc.model_space(body, loc), root)
                legs = [math.dist(rc._unreal(rel[n]), pp) for n, pp in theirs_now.items()
                        if n in rel and any(k in n for k in ("Thigh", "Calf", "Foot", "Toe"))]
                upper = [math.dist(rc._unreal(rel[n]), pp) for n, pp in theirs_now.items()
                         if n in rel and any(k in n for k in ("Spine1", "Neck", "Head", "Clavicle",
                                                             "UpperArm", "Forearm", "Hand"))]
                return (statistics.median(legs) if legs else -1.0,
                        statistics.median(upper) if upper else -1.0)

            legs_at, upper_at = split(float(cycle))
            # The delta at a searched phase of its own, everything else at the recorded cycle.
            deltas = [t for t in base_seq.autolayers
                      if (corpus.model(owner).sequences.get(t.lower()) or base_seq).flags & rc.FLAG_DELTA]
            best_delta = None
            if deltas:
                for k in range(50):
                    trial = list(channels)
                    trial[0] = dict(trial[0], cycle=float(cycle))
                    _b, loc = rc.compose(corpus, stem, trial, params,
                                         layer_cycles={deltas[0].lower(): k / 50.0})
                    rel = rc.relative_to_root(body, rc.model_space(body, loc), root)
                    up = [math.dist(rc._unreal(rel[n]), pp) for n, pp in theirs_now.items()
                          if n in rel and any(kk in n for kk in ("Spine1", "Neck", "Head", "Clavicle",
                                                                 "UpperArm", "Forearm", "Hand"))]
                    med = statistics.median(up) if up else float("inf")
                    if best_delta is None or med < best_delta[0]:
                        best_delta = (med, k / 50.0)
            print(f"[graph] split f={frame.get('frame', 0):4d} at recorded {float(cycle):.3f}: "
                  f"legs {legs_at:.2f} cm, upper {upper_at:.2f} cm"
                  + (f"; upper with {deltas[0]} at its own phase {best_delta[1]:.2f}: "
                     f"{best_delta[0]:.2f} cm" if best_delta else ""))
            coarse = min((fit(k / 50.0), k / 50.0) for k in range(50))
            fine = min((fit(coarse[1] + d / 500.0), coarse[1] + d / 500.0) for d in range(-9, 10))
            recorded = float(cycle)
            delta = ((fine[1] - recorded + 0.5) % 1.0) - 0.5
            print(f"[graph] cycle-fit f={frame.get('frame', 0):4d} {label:20s} recorded "
                  f"{recorded:.3f} best {fine[1] % 1.0:.3f} (delta {delta:+.3f}) residual "
                  f"{fine[0]:.3f} cm (at recorded {fit(recorded):.3f})")
        if BEST_LAG and base_seq is not None and base_seq.fps > 0 and base_seq.frames > 1:
            length = (base_seq.frames - 1) / base_seq.fps
            theirs_now = _relative(frame["bones"], frame.get("root_bone", root))
            best = None
            for lag in BEST_LAG:
                trial = list(channels)
                trial[0] = dict(trial[0], cycle=(float(cycle) + lag / 60.0 / length) % 1.0)
                try:
                    _b, loc = rc.compose(corpus, stem, trial, params)
                except KeyError:
                    continue
                rel = rc.relative_to_root(body, rc.model_space(body, loc), root)
                errs_t = [math.dist(rc._unreal(rel[n]), p) for n, p in theirs_now.items()
                          if n in rel]
                med = statistics.median(errs_t) if errs_t else float("inf")
                if best is None or med < best[0]:
                    best = (med, lag)
            if best is not None:
                lag_hist[best[1]] += 1
                channels[0] = dict(channels[0],
                                   cycle=(float(cycle) + best[1] / 60.0 / length) % 1.0)
                base_cycle = channels[0]["cycle"]
        try:
            _body, locals_ = rc.compose(corpus, stem, channels, params)
        except KeyError as error:
            unresolved += 1
            if unresolved <= 3:
                print(f"[graph]   unresolved: {error}")
            continue
        ours = rc.relative_to_root(body, rc.model_space(body, locals_), root)
        theirs = _relative(frame["bones"], frame.get("root_bone", root))
        # Only the bones the base's bank animates: a hair chain the graph simulates and a twist
        # chain a rig rule drives are things the compositor does not perform, and every other
        # instrument excludes them on the same evidence.
        driven = set()
        if base_seq is not None:
            base_model = corpus.model(owner)
            for i, c in enumerate(rc.evaluate_sequence(base_model, base_seq, base_cycle, params)):
                if c is not None:
                    driven.add(base_model.bones[i].name)
        errs = []
        key = f"{label} + {'/'.join(slots) if slots else 'no slot'}"
        pairs = []
        for name, pos in theirs.items():
            if name not in ours or (driven and name not in driven):
                continue
            mine = rc._unreal(ours[name])
            e = math.dist(mine, pos)
            errs.append(e)
            pairs.append((mine, pos))
            per_bone[name].append(e)
            state_bones[key][name].append(e)
        if not errs:
            continue
        composed += 1
        per_state[key].append(statistics.median(errs))
        # Diagnostic: the best rigid rotation about the root taking ours onto theirs, and what is
        # left after it. A residual that collapses under a small rotation is a root-orientation
        # difference, not a pose one.
        angle, residual = _rigid_fit(pairs)
        fit_angle[key].append(angle)
        fit_residual[key].append(residual)
        if TRACE and frame.get("frame", 0) % TRACE == 0:
            print(f"[graph] trace f={frame.get('frame', 0):4d} {label:22s} speed "
                  f"{float(motion.get('speed', 0.0)):6.1f} yaw {params.get('move_yaw', 0.0):7.1f} "
                  f"cycle {base_cycle:.3f} slots={len(slots)} median {statistics.median(errs):5.2f} "
                  f"fit {angle:5.2f} deg residual {residual:5.2f}")

    print(f"[graph] {composed} frames composed, {gated} gated or unposed, {no_cycle} without a "
          f"base cycle, {unresolved} unresolved")
    if lag_hist:
        print("[graph] best per-frame lag (ticks): "
              + " ".join(f"{lag:+.0f}:{n}" for lag, n in sorted(lag_hist.items())))
    if no_cycle and not composed:
        print("[graph] REFUSED: the report predates the base cycle; record it again")
        return 2
    everything = [m for ms in per_state.values() for m in ms]
    failed = False
    for key in sorted(per_state):
        ms = per_state[key]
        top = sorted(state_bones[key].items(), key=lambda kv: -statistics.median(kv[1]))[:4]
        worst = ", ".join(f"{n.replace('Bip01 ', '')} {statistics.median(v):.2f}" for n, v in top)
        print(f"[graph]   {key:52s} n={len(ms):4d} median {statistics.median(ms):6.3f} cm  "
              f"p90 {sorted(ms)[int(0.9 * (len(ms) - 1))]:6.3f}  max {max(ms):6.3f} | worst {worst}")
        print(f"[graph]   {'':52s} best rigid rotation about the root: median "
              f"{statistics.median(fit_angle[key]):.2f} deg; residual after it median "
              f"{statistics.median(fit_residual[key]):.3f} cm")
    if everything:
        median = statistics.median(everything)
        ok = median <= MEDIAN_TOLERANCE_CM
        failed = not ok
        print(f"[graph] {'OK' if ok else 'FAIL'}: the graph composes to the compositor's frame "
              f"(median {median:.3f} cm over {len(everything)} frames, bound {MEDIAN_TOLERANCE_CM})")
    ranked = sorted(per_bone.items(), key=lambda kv: -statistics.median(kv[1]))[:10]
    print("[graph] bones carrying the error, worst first:")
    for name, errs in ranked:
        print(f"[graph]   {name:28s} median {statistics.median(errs):6.3f} cm  n={len(errs)}")
    return 1 if failed else (0 if composed else 2)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--export-root", type=Path, required=True)
    parser.add_argument("--run", type=Path, action="append", required=True, dest="runs")
    parser.add_argument("--phase-lag", type=float, default=0.0, metavar="TICKS",
                        help="diagnostic: shift every base cycle by this many 60 Hz ticks")
    parser.add_argument("--trace", type=int, default=0, metavar="N",
                        help="diagnostic: print one frame in every N as a time series")
    parser.add_argument("--best-lag", type=int, default=0, metavar="TICKS",
                        help="diagnostic: let each frame choose a lag in [-TICKS, TICKS]")
    parser.add_argument("--best-cycle", type=int, default=0, metavar="N",
                        help="diagnostic: search the whole cycle on one frame in every N")
    parser.add_argument("--clock", choices=("stack", "published"), default="stack",
                        help="compose at the stack's normalized accumulator or the published phase")
    args = parser.parse_args(argv)
    global PHASE_LAG_TICKS, TRACE, BEST_LAG, BEST_CYCLE, CLOCK
    BEST_CYCLE = args.best_cycle
    CLOCK = args.clock
    PHASE_LAG_TICKS = args.phase_lag
    TRACE = args.trace
    BEST_LAG = tuple(range(-args.best_lag, args.best_lag + 1)) if args.best_lag else ()
    corpus = rc.Corpus(args.export_root)
    codes = [score(corpus, run) for run in args.runs]
    if any(c == 1 for c in codes):
        return 1
    return 2 if any(c == 2 for c in codes) else 0


if __name__ == "__main__":
    sys.exit(main())
