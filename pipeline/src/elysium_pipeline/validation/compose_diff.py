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

**The score is taken over the WHOLE BODY, never over the bones a mask owns.**
An earlier form of this comparator scored the masked set alone. That is a real
quantity and it passed -- but every overlay mask roots at `Bip01 Spine1` and
does not own its parent, so the owned bones form ONE RIGID SUBTREE, and a set
of distances taken inside a rigid group is invariant to every isometry of that
group. Measured on this corpus: rotating the whole upper body 47 degrees and
translating it 250 cm changed the score by 5.7e-15 cm. The defect class that
reads as "the weapon swings while the arm's own pose looks right" is precisely
a boundary orientation error, and a masked-set comparison scores it at zero.
Scored over both halves, the pairwise set measures how they sit against each
other, which is the quantity the masked form was blind to.

**The comparison never converts a matrix.** Retail's frame is Source-space and
ours is Unreal-native, and the basis change between them contains a reflection,
so a rotation compared across it is a sign bug waiting to read as a finding.
What is compared is the set of distances between bones -- a quantity every
isometry preserves, and one a pose determines once the set spans both halves of
the body. A bone *length* would not do: it is invariant to rotation, so a length
signature measures the skeleton rather than the pose.

**A bone no clip in the playing bank addresses is not a composed-pose answer.**
The shared banks carry sixty bones; a body carries seventy-nine or eighty-eight,
and the difference is hair, garment and the axis-interpolated twist chain --
things retail drives by rig rules and secondary motion that no composition
reproduces. The set is read from the bank's own `ANIM` track headers for the
clip the frame committed, so it is the export's own answer rather than a list of
names this file would have to be edited to keep true.

**The retail frame is chosen by search, not by a clock.** The two runs share no
time base -- one is a recorded VtMB session and the other is a scripted stream
-- so for each of our frames the score is the closest captured frame that stood
the *same body*, the *same base clip*, the *same overlay set* and the same
moving or at-rest state. That is a lower bound on the error, which is the honest
direction: it cannot manufacture a divergence, only miss one.

**The arm scalar is the number a whole-body median dilutes.** The right hand's
distance to `Bip01 Spine1` through one cycle is one figure a boundary
orientation error MOVES, where a median over sixty bones spreads it thin. It is
read inside ONE state and never over a pooled cohort -- aiming, walking and
firing each hold the hand at a different distance, so a pooled excursion
measures the difference BETWEEN states -- and its bound is the capture's own
excursion for that same state wherever that is wider than the plan's 2.5 cm,
because a threshold that reads red where retail reads red measures itself
rather than the code.

**A body is a run, and `--run` is repeatable.** The harness seats one body per
launch, so the two bodies the capture recorded -- the male and the female
Malkavian -- are two reports. They are scored in one call rather than two so the
closing summary can put their arm scalars beside each other: a proportion defect
that the female's build happens to absorb is only visible as a difference
between the two, and two separate invocations print two verdicts and no
comparison.

Exit code is the verdict, so the comparator can be chained into the command
that recorded the run: 0 green, 1 the composed pose is wrong, 2 the run or the
corpus cannot support a verdict at all. Over several runs it is the worst of
them, with `1` outranking `2`: a run that cannot be judged does not excuse one
that failed.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

import numpy as np

from elysium_pipeline import paths
from elysium_pipeline.formats import eskm

#: Retail states its world in Source units; our run states centimetres. The
#: factor is the one `npc_export` converts every stated distance by.
SOURCE_UNIT_TO_CM = 2.54

#: A matrix3x4_t is three rows of four: a basis row, then that row's translation.
MATRIX_FLOATS = 12

#: The families a producer can push onto the overlay stack.
OVERLAY_SUFFIXES = ("_attack_layer", "_reload_layer", "_dryfire_layer")

#: Read off the measured distribution rather than guessed: `RigCompose` holds a
#: composed pose it accepts under a centimetre across the whole body. A composed
#: pose that cannot reach it is composing something the assets do not say.
MEDIAN_TOLERANCE_CM = 1.0

#: The plan's bound on the arm's excursion: retail's 1.89 cm through a moving
#: carry, with room for the 9-bit pose-parameter quantisation between the cell
#: the capture held and the cell this run resolved.
ARM_PEAK_TO_PEAK_TOLERANCE_CM = 2.5

#: The two ends of the arm scalar.
ARM_HAND_BONE = "Bip01 R Hand"
ARM_SPINE_BONE = "Bip01 Spine1"

#: A body under this is standing, and a standing body has no cycle for the hand
#: to swing over. Centimetres per second, on the ground plane.
MOVING_SPEED_CM_S = 20.0

#: A state group too small to contain a cycle would let two adjacent frames
#: stand for the whole excursion.
MINIMUM_STATE_FRAMES = 15

#: Below this a pairwise set is too small to determine a pose.
MINIMUM_SHARED_BONES = 16

#: A family the run barely stood would let one lucky frame stand for it.
MINIMUM_FRAMES_PER_LAYER = 5

#: T1: a quarter of a metre over a whole stream is not a strafe, and every gait
#: frame in such a report is a standing body under a gait selection.
MINIMUM_TRAVEL_CM = 25.0


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


def _bone_positions(bones: dict[str, Any], scale: float) -> dict[str, np.ndarray]:
    """The translation column only. The basis rows would need the reflection this avoids."""
    out = {}
    for name, row in bones.items():
        if not isinstance(row, list) or len(row) < MATRIX_FLOATS:
            continue
        out[name] = np.array((row[3] * scale, row[7] * scale, row[11] * scale))
    return out


def _sequence_labels(stem: str) -> dict[int, str]:
    """{retail's own global sequence number: the label the export packaged it under}.

    The capture records what the body committed as a number and every other side
    of this comparison speaks labels. `clips/<stem>.json` carries that same
    number per row under `seq`, which is the one place the two vocabularies meet.
    The sidecar's `activities` array is an intern table (T5) and is never read
    here.
    """
    path = paths.export_root() / "npc" / "clips" / f"{stem}.json"
    if not path.is_file():
        return {}
    sidecar = json.loads(path.read_text(encoding="utf-8"))
    out: dict[int, str] = {}
    for label, rows in sidecar.get("seq", {}).items():
        for raw in rows:
            out.setdefault(int(raw), label)
    return out


class _Banks:
    """The bones each clip of each owner states a track for, read once per owner.

    Track headers only: which bones a clip addresses is a property of its track
    headers, so the answer costs the walk that stepping over the payload does and
    never the price of decoding a pose.
    """

    def __init__(self) -> None:
        self._cache: dict[str, tuple[list[str], dict[str, set[int]]] | None] = {}

    def _load(self, owner: str) -> tuple[list[str], dict[str, set[int]]] | None:
        if owner in self._cache:
            return self._cache[owner]
        root = paths.export_root() / "npc"
        loaded = None
        for candidate in (root / "banks" / f"{owner}.eskm", root / f"{owner}.eskm"):
            if candidate.is_file():
                blob = eskm.read(candidate)
                loaded = ([name for name, _parent in eskm.bones(blob)],
                          eskm.clip_track_bones(blob))
                break
        self._cache[owner] = loaded
        return loaded

    def tracked(self, owner: str, label: str) -> set[str] | None:
        """The bone NAMES `label` carries a track for, or None when the bank does not state it."""
        loaded = self._load(owner)
        if loaded is None:
            return None
        names, by_clip = loaded
        indices = by_clip.get(label)
        if indices is None:
            return None
        return {names[index] for index in indices if 0 <= index < len(names)}


def _matrix(positions: dict[str, np.ndarray], order: list[str]) -> np.ndarray:
    """The full pairwise distance matrix over `order`."""
    points = np.array([positions[name] for name in order])
    delta = points[:, None, :] - points[None, :, :]
    return np.sqrt((delta * delta).sum(-1))


def _signature_error(ours: np.ndarray, theirs: np.ndarray) -> float:
    """RMS over the upper triangle, so every pair is counted once."""
    rows = np.triu_indices(ours.shape[0], 1)
    difference = ours[rows] - theirs[rows]
    return math.sqrt(float((difference * difference).mean()))


def _per_bone_error(ours: np.ndarray, theirs: np.ndarray) -> np.ndarray:
    """A bone's share: the mean absolute pair error over the pairs it is in."""
    count = ours.shape[0] - 1
    if count <= 0:
        return np.zeros(ours.shape[0])
    return np.abs(ours - theirs).sum(axis=1) / count


class _Distribution:
    """A described set, not an asserted one, until the numbers say what a bound should be."""

    def __init__(self) -> None:
        self.samples: list[float] = []

    def add(self, value: float) -> None:
        self.samples.append(float(value))

    def __len__(self) -> int:
        return len(self.samples)

    def at(self, fraction: float) -> float:
        ordered = sorted(self.samples)
        index = min(int(round(fraction * (len(ordered) - 1))), len(ordered) - 1)
        return ordered[max(index, 0)]

    def median(self) -> float:
        return self.at(0.5)

    def mean(self) -> float:
        return sum(self.samples) / len(self.samples)

    def std_dev(self) -> float:
        """The population deviation of the recorded set: the samples ARE the frames."""
        if len(self.samples) < 2:
            return 0.0
        average = self.mean()
        return math.sqrt(sum((s - average) ** 2 for s in self.samples) / len(self.samples))

    def peak_to_peak(self) -> float:
        """The full excursion. A defect that SWINGS is read here and never off a median, which
        an error constant across the cycle leaves untouched."""
        return max(self.samples) - min(self.samples)

    def describe(self) -> str:
        return (f"n={len(self.samples):<4} median {self.at(0.5):.3f} cm  "
                f"p90 {self.at(0.9):.3f} cm  max {self.at(1.0):.3f} cm")


class _Cohort:
    """One cohort's verdict material, and **every cohort carries all of it** (T2).

    Attribution printed for the layered cohort alone is attribution printed where
    the arm cannot show it: a pushed slot's mask replaces the whole upper body
    outright, so the arm is the LAYER's pose there whatever composed underneath
    it, and the legs dominate the ranking by construction.
    """

    def __init__(self, name: str, note: str) -> None:
        self.name = name
        self.note = note
        self.error = _Distribution()
        self.by_bone: dict[str, _Distribution] = {}
        self.by_state: dict[str, _Distribution] = {}
        self.arm: dict[str, _Distribution] = {}
        self.retail_arm: dict[str, _Distribution] = {}


class _Verdict:
    """What one run's score amounts to, kept so several runs can be summarised together.

    Only the figures the closing summary reads: the run's identity, its exit
    code, and per cohort the median it was judged on beside the arm scalar for
    every state it scored. The full report is printed as each run is scored; this
    is what survives the run to be placed beside another body's.
    """

    def __init__(self, path: Path, body: str, stem: str, weapon: str) -> None:
        self.path = path
        self.body = body
        self.stem = stem
        self.weapon = weapon
        self.code = 0
        self.note = ""
        #: `[(cohort, n, median, ok)]`
        self.cohorts: list[tuple[str, int, float, bool]] = []
        #: `[(cohort, state, n, peak_to_peak, bound_or_None, asserted)]`
        self.arms: list[tuple[str, str, int, float, float | None, bool]] = []


def _state_key(stem: str, base: str, moving: bool) -> str:
    return f"{stem} {base} {'moving' if moving else 'at rest'}"


def _retail_frames(stem: str) -> tuple[list[dict[str, Any]], int]:
    """Every captured frame of `stem`, with what it stood and how fast it was going.

    The two oracles are joined on `(round(curtime, 5), stem)` -- the pose hook's
    own clock, which both analyzers read off the same `SetupBones` call, so a
    frame present in one is present in the other.

    The body is selected by exact equality and never by substring (T4):
    `"male" in "malkavian_female_armor_0"` is True, and a cohort scored against
    the wrong bank is a whole run of confident nonsense.
    """
    out: list[dict[str, Any]] = []
    unjoined = 0
    labels = _sequence_labels(stem)
    for session in _sessions():
        layers = json.loads((session / "layer_oracle.json").read_text(encoding="utf-8"))
        poses = json.loads((session / "pose_oracle.json").read_text(encoding="utf-8"))
        by_key = {(round(float(f.get("curtime", 0.0)), 5), f.get("stem")): f
                  for f in layers.get("frames", ())}
        for frame in poses.get("frames", ()):
            if frame.get("stem") != stem or not frame.get("bones"):
                continue
            state = frame.get("player_state")
            if not state:
                continue
            channels = by_key.get((round(float(frame.get("curtime", 0.0)), 5), stem))
            if channels is None:
                unjoined += 1
                continue
            rows = channels.get("channels", ())
            standing = sorted(str(c.get("label", "")) for c in rows
                              if float(c.get("weight", 0.0)) >= 0.999)
            # **A frame the capture took mid cross-fade is drawn from a base our run does not
            # stand.** Retail's client keeps every surviving previous sequence and ramps it out on
            # its own clock, so a plain channel below full weight is a base still fading; scoring
            # against such a frame charges T-C7's residual to whatever is under test here.
            cross_fading = any(not c.get("additive") and float(c.get("weight", 0.0)) < 0.999
                               for c in rows)
            velocity = state.get("velocity") or (0.0, 0.0, 0.0)
            speed = math.hypot(float(velocity[0]), float(velocity[1])) * SOURCE_UNIT_TO_CM
            out.append({
                "base": labels.get(int(state.get("sequence", -1)), ""),
                "channels": standing,
                "overlays": [c for c in standing if c.endswith(OVERLAY_SUFFIXES)],
                "moving": speed > MOVING_SPEED_CM_S,
                "cross_fading": cross_fading,
                "positions": _bone_positions(frame["bones"], SOURCE_UNIT_TO_CM),
            })
    return out, unjoined


def compare(run_path: Path) -> _Verdict:
    """Score one run. `verdict.code` is this run's exit code."""
    if not run_path.is_file():
        print(f"[compose] no run at {run_path}; record one with `uv run elysium debug compose`")
        verdict = _Verdict(run_path, "", "", "")
        verdict.code, verdict.note = 2, "no such run"
        return verdict
    run = json.loads(run_path.read_text(encoding="utf-8"))
    frames = run.get("frames", [])
    stem = str(run.get("stem", ""))
    # The body the harness was ASKED for. Older reports predate the field and carry only what the
    # driver published, which is why it falls back to the stem rather than to a refusal.
    body = str(run.get("body", "")) or stem
    weapon = str(run.get("weapon", ""))
    verdict = _Verdict(run_path, body, stem, weapon)
    travelled = float(run.get("farthest_travelled_cm", 0.0))
    print(f"[compose] {run_path.name}: {len(frames)} frames, weapon {weapon}, "
          f"body '{body}', stem '{stem}', farthest travelled {travelled:.1f} cm")

    # **T1, read before a single pose number is.** A body that never strafed still records a gait
    # selection, four slot rows and a full skeleton on every frame, and every one of them is a
    # standing body -- which a comparator will happily score into a green median.
    if travelled < MINIMUM_TRAVEL_CM:
        print(f"[compose] REFUSED: the body travelled {travelled:.1f} cm, under "
              f"{MINIMUM_TRAVEL_CM:.0f}; every gait frame in this report is a standing body, so "
              f"no verdict is taken from it")
        verdict.code, verdict.note = 2, f"travelled {travelled:.1f} cm"
        return verdict
    if not stem:
        print("[compose] REFUSED: the run names no body stem, so nothing selects the captured "
              "frames to score it against")
        verdict.code, verdict.note = 2, "no body stem"
        return verdict
    # **The body asked for against the body stood.** The harness names one on its command line and
    # the driver publishes what it actually resolved; when they disagree every frame in the report
    # is a full skeleton under a gait selection belonging to the wrong body -- the same shape as a
    # body that never strafed, one field further out, and just as invisible in the pose data. The
    # capture is selected by the PUBLISHED stem, so scoring it would be a real comparison of the
    # wrong run.
    if body.lower() != stem.lower():
        print(f"[compose] REFUSED: the run was recorded for body '{body}' but the driver published "
              f"'{stem}'; the override did not take, so every frame in it is the wrong body")
        verdict.code, verdict.note = 2, f"asked '{body}', stood '{stem}'"
        return verdict

    retail, unjoined = _retail_frames(stem)
    if not retail:
        print(f"[compose] ABSTAIN: no life_rig_pose session under "
              f"$ELYSIUM_WORK_ROOT/research/frida carries both oracles for '{stem}'")
        verdict.note = "no captured frame of this body"
        return verdict
    fading = sum(1 for r in retail if r["cross_fading"])
    retail = [r for r in retail if not r["cross_fading"]]
    print(f"[compose] capture: {len(retail)} frames of '{stem}' joined across both oracles"
          + (f", {unjoined} unjoined" if unjoined else "")
          + (f", {fading} set aside as mid cross-fade" if fading else ""))
    if not retail:
        print(f"[compose] ABSTAIN: every captured frame of '{stem}' was taken mid cross-fade")
        verdict.note = "every captured frame is mid cross-fade"
        return verdict

    banks = _Banks()
    control = _Cohort("control", "no slot standing")
    layered = _Cohort("layered", "exactly one slot at full weight")
    cohorts = [control, layered]

    gated = 0
    multiple = 0
    unmatched: dict[str, int] = {}
    unknown_clip: dict[str, int] = {}
    maskless: dict[str, int] = {}
    too_few_bones = 0
    excluded_bones: set[str] = set()
    channel_gaps: dict[str, int] = {}
    by_layer: dict[str, _Distribution] = {}

    # The capture's own distances per candidate and bone set, kept between our frames: one retail
    # frame is a candidate for hundreds of ours and its distances do not change.
    retail_cache: dict[tuple[int, tuple[str, ...]], np.ndarray] = {}

    for frame in frames:
        motion = frame.get("motion", {})
        if motion.get("gated"):
            # A screen holding input turns every replayed command into a zero (T1). The pose is
            # real, but the body is not acting on the stream, so it is not the case under test.
            gated += 1
            continue
        selection = frame.get("selection", {})
        base = str(selection.get("label", ""))
        owner = str(selection.get("owner", ""))
        rows = [r for r in frame.get("overlay_slots", ()) if r.get("label")]
        live = [r for r in rows if float(r.get("weight", 0.0)) >= 0.999]
        if len(live) > 1:
            multiple += 1
            continue
        if live and not live[0].get("masked_bones"):
            # A layer that reached the frame carrying no mask composes at zero weight on every
            # bone in `BlendMask` mode -- it poses nothing. That is a finding, not a frame to skip.
            maskless[live[0]["label"]] = maskless.get(live[0]["label"], 0) + 1
            continue
        overlays = sorted(str(r["label"]) for r in live)
        moving = float(motion.get("speed", 0.0)) > MOVING_SPEED_CM_S

        tracked = banks.tracked(owner, base)
        if tracked is None:
            key = f"{owner}/{base}"
            unknown_clip[key] = unknown_clip.get(key, 0) + 1
            continue

        state = _state_key(stem, base, moving)
        candidates = [r for r in retail
                      if r["base"] == base and r["overlays"] == overlays
                      and r["moving"] == moving]
        if not candidates:
            key = f"{state}, overlays {overlays if overlays else '[]'}"
            unmatched[key] = unmatched.get(key, 0) + 1
            continue

        ours = _bone_positions(frame.get("bones", {}), 1.0)   # already centimetres
        excluded_bones.update(name for name in ours if name not in tracked)

        best = float("inf")
        best_order: list[str] = []
        best_ours = None
        best_theirs = None
        best_reference = None
        mine_cache: dict[tuple[str, ...], np.ndarray] = {}
        for index, reference in enumerate(candidates):
            order = sorted(name for name in tracked
                           if name in ours and name in reference["positions"])
            if len(order) < MINIMUM_SHARED_BONES:
                continue
            cache_key = (index, tuple(order))
            theirs = retail_cache.get(cache_key)
            if theirs is None:
                theirs = _matrix(reference["positions"], order)
                retail_cache[cache_key] = theirs
            mine = mine_cache.get(tuple(order))
            if mine is None:
                mine = _matrix(ours, order)
                mine_cache[tuple(order)] = mine
            score = _signature_error(mine, theirs)
            if score < best:
                best, best_order, best_ours, best_theirs = score, order, mine, theirs
                best_reference = reference
        if best_ours is None or best_theirs is None or best_reference is None:
            too_few_bones += 1
            continue

        cohort = layered if overlays else control
        cohort.error.add(best)
        cohort.by_state.setdefault(state, _Distribution()).add(best)
        if overlays:
            by_layer.setdefault(overlays[0], _Distribution()).add(best)

        shares = _per_bone_error(best_ours, best_theirs)
        for position, name in enumerate(best_order):
            cohort.by_bone.setdefault(name, _Distribution()).add(float(shares[position]))

        # **The channel sets the two sides recorded, compared but never gated on.** A capture
        # accumulating a channel our record does not name is a real difference and it is named
        # here; it is not a reason to refuse the frame, because our record enumerates what the
        # driver published and not necessarily every channel the graph composed.
        ours_channels = set(str(x) for x in selection.get("layers", ())) | set(overlays)
        missing = [c for c in best_reference["channels"] if c not in ours_channels]
        if missing:
            key = f"{state}: the capture also accumulated {', '.join(missing)}"
            channel_gaps[key] = channel_gaps.get(key, 0) + 1

        if moving and ARM_HAND_BONE in best_order and ARM_SPINE_BONE in best_order:
            cohort.arm.setdefault(state, _Distribution()).add(
                float(np.linalg.norm(ours[ARM_HAND_BONE] - ours[ARM_SPINE_BONE])))

    # **The capture's own excursion per state, over the capture's frames rather than over the ones
    # a search happened to pick.** The bound is read from it, so it may not depend on which of our
    # frames matched what.
    for cohort in cohorts:
        for state in cohort.arm:
            span = _Distribution()
            for reference in retail:
                if not reference["moving"]:
                    continue
                if _state_key(stem, reference["base"], True) != state:
                    continue
                if bool(reference["overlays"]) != (cohort is layered):
                    continue
                positions = reference["positions"]
                if ARM_HAND_BONE in positions and ARM_SPINE_BONE in positions:
                    span.add(float(np.linalg.norm(
                        positions[ARM_HAND_BONE] - positions[ARM_SPINE_BONE])))
            if len(span):
                cohort.retail_arm[state] = span

    # **Every counter is surfaced on the success path, not only when the run abstains.** A run that
    # scored eight frames of five hundred and passed is the failure mode a hidden counter produces.
    print(f"[compose] {gated} frames dropped with input gated, {multiple} with more than one slot "
          f"at full weight, {too_few_bones} with too few shared bones")
    for label, count in sorted(unmatched.items()):
        print(f"[compose]   no captured frame stands '{label}': {count} of ours unscored")
    for label, count in sorted(unknown_clip.items()):
        print(f"[compose]   '{label}' is not a clip of its stated bank, so the bones it addresses "
              f"are unknown: {count} frames unscored")
    if excluded_bones:
        names = sorted(excluded_bones)
        print(f"[compose] {len(names)} bones excluded as addressed by no clip of the playing bank "
              f"({', '.join(names)})")
    for label, count in sorted(channel_gaps.items()):
        print(f"[compose]   {label} ({count} frames)")

    failed = False
    for label, count in sorted(maskless.items()):
        print(f"[compose] FAIL: '{label}' stood on {count} frames carrying no bone mask, "
              f"so it composed at zero weight on every bone")
        failed = True

    if not any(len(c.error) for c in cohorts):
        # A run that composed nothing is the finding, not an abstention: the harness drove a
        # weapon and held the trigger, so an empty stack means the producers never reached it.
        print("[compose] FAIL: no frame could be scored -- the run stood no pose the capture "
              "also stands")
        verdict.code, verdict.note = 1, "no frame could be scored"
        return verdict

    for cohort in cohorts:
        if not len(cohort.error):
            print(f"[compose] {cohort.name:<8} ({cohort.note}): no frame")
            continue
        print(f"[compose] {cohort.name:<8} ({cohort.note}): {cohort.error.describe()}")
        for state in sorted(cohort.by_state):
            print(f"[compose]   {state:<48} {cohort.by_state[state].describe()}")

    # **Both cohorts are asserted.** In `RigCompose` the control cohort is the premise that makes
    # the layered figure readable, because that test REBUILDS the base; here both sides are the
    # running graph's own output, so a control that misses is the same defect reported one
    # composition earlier rather than a broken premise.
    for cohort in cohorts:
        if not len(cohort.error):
            continue
        median = cohort.error.median()
        if median > MEDIAN_TOLERANCE_CM:
            print(f"[compose] FAIL: the {cohort.name} cohort does not compose to the pose retail "
                  f"drew (median {median:.3f} cm > {MEDIAN_TOLERANCE_CM:.3f} cm over "
                  f"{len(cohort.error)} frames)")
            failed = True
        else:
            print(f"[compose] OK: the {cohort.name} cohort composes to the pose retail drew "
                  f"(median {median:.3f} cm over {len(cohort.error)} frames)")
        verdict.cohorts.append((cohort.name, len(cohort.error), median,
                                median <= MEDIAN_TOLERANCE_CM))

    # **Per layer, not on the aggregate**: one family is a minority of any run, so a global median
    # cannot fail on a one-family defect.
    for label in sorted(by_layer):
        distribution = by_layer[label]
        print(f"[compose]   {label:<34} {distribution.describe()}")
        if len(distribution) < MINIMUM_FRAMES_PER_LAYER:
            continue
        if distribution.median() > MEDIAN_TOLERANCE_CM:
            print(f"[compose] FAIL: '{label}' composes to a pose retail did not draw "
                  f"(median {distribution.median():.3f} cm over {len(distribution)} frames)")
            failed = True

    # **The arm scalar, asserted on the CONTROL cohort, one state at a time.** It is asserted there
    # and not on the layered one because that is the only cohort it can fail in: a pushed slot's
    # mask replaces the whole upper body, so in the layered cohort the hand is the LAYER's pose
    # whatever the composition did to the arm underneath it (T2). Both are printed.
    for cohort in cohorts:
        if not cohort.arm:
            if len(cohort.error):
                print(f"[compose] {cohort.name}: no moving frame carries both ends of the arm "
                      f"scalar")
            continue
        print(f"[compose] {cohort.name}: right hand <- `{ARM_SPINE_BONE}`, per state, ours beside "
              f"the capture's own:")
        for state in sorted(cohort.arm):
            ours_span = cohort.arm[state]
            theirs_span = cohort.retail_arm.get(state)
            note = "" if len(ours_span) >= MINIMUM_STATE_FRAMES else "  (too few to assert)"
            print(f"[compose]   {state:<48} n={len(ours_span):<4} "
                  f"ours mean {ours_span.mean():6.2f} sd {ours_span.std_dev():5.2f} "
                  f"p2p {ours_span.peak_to_peak():6.2f} cm"
                  + (f" | capture n={len(theirs_span):<4} mean {theirs_span.mean():6.2f} "
                     f"sd {theirs_span.std_dev():5.2f} p2p {theirs_span.peak_to_peak():6.2f} cm"
                     if theirs_span else " | capture: no frame")
                  + note)
            asserted = cohort is control and len(ours_span) >= MINIMUM_STATE_FRAMES
            bound: float | None = None
            if asserted:
                bound = ARM_PEAK_TO_PEAK_TOLERANCE_CM
                if theirs_span is not None and len(theirs_span) >= MINIMUM_STATE_FRAMES:
                    bound = max(bound, theirs_span.peak_to_peak())
            # **Recorded for every cohort, asserted on one.** The summary prints the layered
            # cohort's excursion too: it cannot fail there (a pushed slot's mask replaces the whole
            # upper body, so the hand is the LAYER's pose whatever composed underneath), but it is
            # the number that says whether the two cohorts disagree, which is what a body-to-body
            # comparison is read off.
            verdict.arms.append((cohort.name, state, len(ours_span),
                                 ours_span.peak_to_peak(), bound, asserted))
            if not asserted or bound is None:
                continue
            if ours_span.peak_to_peak() > bound:
                print(f"[compose] FAIL: {state}: the right hand does not hold its station against "
                      f"`{ARM_SPINE_BONE}` through the cycle (peak-to-peak "
                      f"{ours_span.peak_to_peak():.3f} cm > {bound:.3f})")
                failed = True

    # Where the error sits, worst first, for every cohort and labelled with which one. Reported
    # rather than asserted: the verdicts above are what fails, and this is what a reader opens the
    # report to find out next.
    for cohort in cohorts:
        if not cohort.by_bone:
            continue
        ranked = sorted(((name, d.median()) for name, d in cohort.by_bone.items()),
                        key=lambda pair: pair[1], reverse=True)
        print(f"[compose] {cohort.name}: the bones carrying its error, worst first:")
        for name, value in ranked[:12]:
            print(f"[compose]   {name:<28} {value:.3f} cm")

    verdict.code = 1 if failed else 0
    return verdict


def summarise(verdicts: list[_Verdict]) -> None:
    """The closing block: one line per cohort per run, and the arm scalar under it.

    Printed even for a single run, because it is the shape a reader takes the
    verdict from -- and printed with the body's name on every line, because with
    two runs in one call an unlabelled figure is a figure that belongs to
    whichever run the reader last remembers.
    """
    print(f"[compose] ==== summary: {len(verdicts)} run{'' if len(verdicts) == 1 else 's'} ====")
    for verdict in verdicts:
        mark = {0: "OK", 1: "FAIL", 2: "NO VERDICT"}[verdict.code]
        print(f"[compose] {verdict.body} + {verdict.weapon}: {mark}"
              + (f" ({verdict.note})" if verdict.note else ""))
        for name, count, median, ok in verdict.cohorts:
            print(f"[compose]   {name:<8} n={count:<5} median {median:6.3f} cm  "
                  f"{'OK' if ok else 'FAIL'} (bound {MEDIAN_TOLERANCE_CM:.3f})")
            for cohort, state, arm_n, span, bound, asserted in verdict.arms:
                if cohort != name:
                    continue
                if bound is None:
                    note = "reported, not asserted"
                else:
                    note = f"{'OK' if span <= bound else 'FAIL'} (bound {bound:.3f})"
                print(f"[compose]     arm p2p {span:6.3f} cm  n={arm_n:<4} {note}   {state}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    # Repeatable: the harness stands one body per launch, so two bodies are two reports, and they
    # are scored in one call so the summary can put them beside each other.
    parser.add_argument("--run", required=True, type=Path, action="append", dest="runs",
                        help="a composed-pose report written by `debug compose`; repeatable")
    args = parser.parse_args(argv)
    verdicts = [compare(run) for run in args.runs]
    summarise(verdicts)
    # The worst of them, with 1 outranking 2: a run that could not be judged does not excuse one
    # that failed.
    if any(v.code == 1 for v in verdicts):
        return 1
    return 2 if any(v.code == 2 for v in verdicts) else 0


if __name__ == "__main__":
    sys.exit(main())
