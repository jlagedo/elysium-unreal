"""Score a headless composed-pose run against the captured retail poses.

`uv run elysium debug compose` drives a body on a real map through the real
input router and writes, per frame, the pose the animation graph produced --
the player body's own component-space bone transforms -- beside the four
overlay slot rows and the selection record that produced them.

This is the half no automation test reaches. `Elysium.Content.RigPose` scores
one clip and only on a frame nothing layered onto; `Elysium.Content.RigCompose`
scores the baked assets by evaluating a sequence directly and never stands a
graph up. A slot published at the wrong weight, a bone mask resolved against
the wrong skeleton, or a chain whose branches compose in the wrong order is
invisible to both, and only exists in a built world.

**The comparison never converts a matrix.** Retail's frame is Source-space and
ours is Unreal-native, and the basis change between them contains a reflection,
so a rotation compared across it is a sign bug waiting to read as a finding.
What is compared is the set of distances between the bones an overlay's mask
owns -- a quantity every isometry preserves, and one a pose determines. A bone
*length* would not do: it is invariant to rotation, so a length signature
measures the skeleton rather than the pose.

**The retail frame is chosen by search, not by a clock.** The two runs share no
time base -- one is a recorded VtMB session and the other is a scripted stream
-- so for each of our frames carrying an overlay, the score is the closest
retail frame that stood the *same layer label*. That is a lower bound on the
error, which is the honest direction: it cannot manufacture a divergence, only
miss one.

Exit code is the verdict, so the comparator can be chained into the command
that recorded the run.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

from elysium_pipeline import paths

#: Retail states its world in Source units; our run states centimetres. The
#: factor is the one `npc_export` converts every stated distance by.
SOURCE_UNIT_TO_CM = 2.54

#: A matrix3x4_t is three rows of four: a basis row, then that row's translation.
MATRIX_FLOATS = 12

#: The families a producer can push onto the overlay stack.
OVERLAY_SUFFIXES = ("_attack_layer", "_reload_layer", "_dryfire_layer")

#: Read off the measured distribution rather than guessed: `RigCompose` puts the
#: attack families at a median of 0.18-0.55 cm across the 49 masked bones, so a
#: centimetre is a ceiling the assets clear with room. A composed pose that
#: cannot reach it is composing something the assets do not say.
MEDIAN_TOLERANCE_CM = 1.0


def _sessions() -> list[Path]:
    """Every captured `life_rig_pose` session that has been through both analyzers."""
    frida = paths.research_root() / "frida"
    if not frida.is_dir():
        return []
    out = []
    for session in sorted(frida.glob("*-life_rig_pose")):
        if (session / "pose_oracle.json").is_file() and (session / "layer_oracle.json").is_file():
            out.append(session)
    return out


def _bone_positions(bones: dict[str, Any], scale: float) -> dict[str, tuple[float, float, float]]:
    """The translation column only. The basis rows would need the reflection this avoids."""
    out = {}
    for name, row in bones.items():
        if not isinstance(row, list) or len(row) < MATRIX_FLOATS:
            continue
        out[name] = (row[3] * scale, row[7] * scale, row[11] * scale)
    return out


def _dist(a, b) -> float:
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def _signature(positions, order) -> list[float] | None:
    """All-pairs distances over `order`, or None when a bone is missing.

    Partial pair sets are refused rather than compared: two signatures built
    from different bone sets are two different quantities, and scoring them
    against each other is how a comparator reports a number that means nothing.
    """
    try:
        points = [positions[name] for name in order]
    except KeyError:
        return None
    return [_dist(points[a], points[b])
            for a in range(len(points))
            for b in range(a + 1, len(points))]


def _rms(ours: list[float], theirs: list[float]) -> float:
    if len(ours) != len(theirs) or not ours:
        return float("inf")
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(ours, theirs)) / len(ours))


def _retail_frames() -> dict[str, list[dict[str, Any]]]:
    """Captured poses that stood exactly one overlay at full weight, keyed by its label.

    Full weight only, and for the reason `RigCompose` states: retail composes a
    masked overlay with `nlerp(out, layer, s)`, so at `s == 1` the drawn pose on
    the bones the mask owns is the layer's own and nothing of the base survives
    there. Below it the pose is a genuine blend against a base neither side of
    this comparison rebuilds.
    """
    by_label: dict[str, list[dict[str, Any]]] = {}
    for session in _sessions():
        layers = json.loads((session / "layer_oracle.json").read_text(encoding="utf-8"))
        poses = json.loads((session / "pose_oracle.json").read_text(encoding="utf-8"))
        pose_by_key = {(f.get("curtime"), f.get("stem")): f for f in poses.get("frames", ())}
        for frame in layers.get("frames", ()):
            overlays = [c for c in frame.get("channels", ())
                        if str(c.get("label", "")).endswith(OVERLAY_SUFFIXES)]
            if len(overlays) != 1 or float(overlays[0].get("weight", 0.0)) < 0.999:
                continue
            pose = pose_by_key.get((frame.get("curtime"), frame.get("stem")))
            if not pose or not pose.get("bones"):
                continue
            by_label.setdefault(overlays[0]["label"], []).append({
                "stem": pose.get("stem"),
                "positions": _bone_positions(pose["bones"], SOURCE_UNIT_TO_CM),
            })
    return by_label


def _masked_bones(row: dict[str, Any], run_frame: dict[str, Any],
                  reference: dict[str, Any]) -> list[str]:
    """The bones the layer's own mask owns, that both sides carry, in one stable order.

    The mask is what makes the comparison mean anything. A masked overlay at full
    weight replaces exactly these bones and leaves every other one to the base --
    so scoring a wider set scores a lower body whose gait the two runs were never
    driven to share, and reports the legs as a composition defect. The set is
    recorded per frame by the harness because the blend resolves it by name
    against the playing skeleton, and only that process knows what it resolved to.
    """
    masked = [str(name) for name in row.get("masked_bones", ())]
    ours = set(run_frame.get("bones", {}))
    theirs = set(reference["positions"])
    return [name for name in masked if name in ours and name in theirs]


def compare(run_path: Path) -> int:
    if not run_path.is_file():
        print(f"[compose] no run at {run_path}; record one with `uv run elysium debug compose`")
        return 2
    run = json.loads(run_path.read_text(encoding="utf-8"))
    frames = run.get("frames", [])
    print(f"[compose] {run_path.name}: {len(frames)} frames, weapon {run.get('weapon')}, "
          f"stem {run.get('stem')}")

    retail = _retail_frames()
    if not retail:
        print("[compose] ABSTAIN: no life_rig_pose session carries both oracles under "
              "$ELYSIUM_WORK_ROOT/research/frida")
        return 0

    scored: list[float] = []
    by_label: dict[str, list[float]] = {}
    layered = 0
    unmatched: dict[str, int] = {}
    maskless: dict[str, int] = {}
    slots_seen = 0

    for frame in frames:
        rows = [r for r in frame.get("overlay_slots", ()) if r.get("label")]
        slots_seen += len(rows)
        live = [r for r in rows if float(r.get("weight", 0.0)) >= 0.999]
        if len(live) != 1:
            continue
        label = live[0]["label"]
        layered += 1
        if not live[0].get("masked_bones"):
            # A layer that reached the frame carrying no mask composes at zero weight on every bone
            # in `BlendMask` mode -- it poses nothing. That is a finding, not a frame to skip.
            maskless[label] = maskless.get(label, 0) + 1
            continue
        references = retail.get(label)
        if not references:
            unmatched[label] = unmatched.get(label, 0) + 1
            continue

        ours = _bone_positions(frame.get("bones", {}), 1.0)   # already centimetres
        best = float("inf")
        for reference in references:
            order = _masked_bones(live[0], frame, reference)
            if len(order) < 8:
                continue
            mine = _signature(ours, order)
            theirs = _signature(reference["positions"], order)
            if mine is None or theirs is None:
                continue
            best = min(best, _rms(mine, theirs))
        if best < float("inf"):
            scored.append(best)
            by_label.setdefault(label, []).append(best)

    print(f"[compose] {slots_seen} slot rows over {len(frames)} frames; "
          f"{layered} frames stood exactly one at full weight")
    if unmatched:
        for label, count in sorted(unmatched.items()):
            print(f"[compose]   '{label}': {count} frames, no captured retail frame stands it")
    if maskless:
        for label, count in sorted(maskless.items()):
            print(f"[compose] FAIL: '{label}' stood on {count} frames carrying no bone mask, "
                  f"so it composed at zero weight on every bone")
        return 1
    if not scored:
        # A run that composed nothing is the finding, not an abstention: the
        # harness drove a weapon and held the trigger, so an empty stack means
        # the producers never reached it.
        print("[compose] FAIL: no frame could be scored -- the run composed no overlay "
              "the capture also stands")
        return 1

    scored.sort()
    def at(fraction: float) -> float:
        return scored[min(int(fraction * (len(scored) - 1)), len(scored) - 1)]

    print(f"[compose] composed pose vs retail: n={len(scored)}  median {at(0.5):.3f} cm  "
          f"p90 {at(0.9):.3f} cm  max {at(1.0):.3f} cm")
    for label in sorted(by_label):
        values = sorted(by_label[label])
        median = values[len(values) // 2]
        print(f"[compose]   {label:<34} n={len(values):<4} median {median:.3f} cm  "
              f"max {values[-1]:.3f} cm")

    median = at(0.5)
    if median > MEDIAN_TOLERANCE_CM:
        print(f"[compose] FAIL: the composed pose does not reach retail's "
              f"(median {median:.3f} cm > {MEDIAN_TOLERANCE_CM:.3f} cm)")
        return 1
    print(f"[compose] OK: median {median:.3f} cm within {MEDIAN_TOLERANCE_CM:.3f} cm")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", required=True, type=Path,
                        help="the composed-pose report written by `debug compose`")
    args = parser.parse_args(argv)
    return compare(args.run)


if __name__ == "__main__":
    sys.exit(main())
