"""The reference compositor: retail's pose arithmetic, run offline from the user's own `.mdl`.

One frame of a body is a base sequence at a cycle, its declared autolayers at that same cycle,
and whatever overlay slots stood, each accumulated onto the running local pose in a fixed order
with a fixed rule -- and every one of those rules is a fact `docs/vtmb/animation_and_movers.md`
recovered from the binary with an address beside it. This module states them once, in Source
space, on the raw model, and answers any state: any cell, any phase, any weight. That is what
makes it dense where the retail capture is sparse, and it is the oracle everything downstream is
held to -- the exporter's intermediates, the baked assets and the running graph each equal it or
they are wrong.

The capture validates THIS module, once per rule, through `validate_session`; nothing else ever
compares against the capture directly, because the capture records no aim value and no layer
phase, and a comparison that has to search for them measures the search.

Rules carried, each with its owner:

- frame selection `floor((numframes - 1) * cycle)`, remainder mixed by a normalized component
  lerp on rotation and a plain lerp on position (A.4, `FUN_10089b20`, `FUN_1010a0b0`);
- a pose parameter resolves to a cell and a fraction by wrapping into its loop range, normalizing
  over the descriptor's range, remapping through the sequence's own, clamping, and scaling
  against `groupsize` (A.3); a fan blends its two neighbours, a 3x3 grid its four corners;
- a bone whose animation record carries weight zero contributes nothing (A.4, `read_anim`);
- an ordinary layer accumulates `nlerp(out, layer, s)`, `pos = lerp`; a `0x14` delta accumulates
  `normalize(out * scale(delta, s))`, `pos += delta.pos * s` -- the delta on the RIGHT
  (`vampire.dll 0x100c12b0`); an autolayer's `s` is the literal `1.0` (`0x1008a0ce`) times the
  target's per-bone mask;
- a bone carrying `Flags & 0x2` takes its animated rotation as its MODEL-SPACE rotation, parent
  skipped, translation still composed through the parent (A.4a);
- correspondence between a bank's bones and a body's is by identical bone name
  (`animation_rig_resolution.md`).

Every quantity is in Source units and Source coordinates; nothing here converts, because the
capture is in the same space and the conversion is the exporter's, tested at its own seam.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import statistics
import sys
from typing import Any, Iterable

from elysium_pipeline.formats import mdl_skel as S

#: `StudioSeqDesc.flags` bits the accumulator reads (A.3).
FLAG_DELTA = 0x4
FLAG_POST = 0x10
#: `StudioBone.flags` bit for the split-rotation storage rule (A.4a).
SPLIT_ROTATION = 0x2
#: A channel below this weight is a base still ramping out (A.4c), not a candidate frame.
FULL_WEIGHT = 0.999
SOURCE_UNIT_TO_CM = 2.54


# ---------------------------------------------------------------------------- quaternion kit
# (x, y, z, w) throughout, the storage order of the file and of `read_anim`.

def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qnorm(q):
    n = math.sqrt(sum(c * c for c in q))
    if n < 1e-12:
        return None
    return tuple(c / n for c in q)


def qconj(q):
    return (-q[0], -q[1], -q[2], q[3])


def qrotate(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx))


def qnlerp(a, b, t):
    """`FUN_1010a0b0`: flip `b` to the nearer hemisphere, mix the components, normalize."""
    if sum(x * y for x, y in zip(a, b)) < 0.0:
        b = tuple(-c for c in b)
    return qnorm(tuple(x + (y - x) * t for x, y in zip(a, b))) or a


def qscale(q, s):
    """`QuaternionScale` (`0x1013add0`): shortest-arc slerp from identity by `s`, keeping the
    sign of `w`."""
    if s >= 1.0:
        return q
    if s <= 0.0:
        return (0.0, 0.0, 0.0, 1.0)
    x, y, z, w = q
    sinom = min(math.sqrt(x * x + y * y + z * z), 1.0)
    sinsom = math.sin(math.asin(sinom) * s)
    k = sinsom / (sinom + 1.1920929e-07)
    nw = math.sqrt(max(0.0, 1.0 - sinsom * sinsom))
    return (x * k, y * k, z * k, nw if w >= 0.0 else -nw)


def vlerp(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def mat_from_quat(q):
    x, y, z, w = q
    return ((1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)),
            (2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)),
            (2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)))


# ---------------------------------------------------------------------------- the models

class Model:
    """One `.mdl` as the compositor reads it: bones, sequences by label, pose parameters."""

    def __init__(self, key: str, d: bytes):
        self.key = key
        self.d = d
        self.bones = S.read_bones(d)
        self.by_name = {b.name.lower(): b for b in self.bones}
        self.sequences = {s.label.lower(): s for s in S.local_sequences(d)}
        self.pose_params = {p.name.lower(): p for p in S.pose_parameters(d)}
        self._abase = S._i32(d, 268)
        self._frames: dict[int, list] = {}

    def anim(self, index: int):
        """(animdesc base, frame count) for a local animation index."""
        ab = self._abase + index * 72
        return ab, S._i32(self.d, ab + 12)

    def frames(self, index: int):
        """Every decoded frame of one animation, `read_anim`'s own shape, memoized."""
        got = self._frames.get(index)
        if got is None:
            ab, n = self.anim(index)
            got = self._frames[index] = S.read_anim(self.d, self.bones, ab, max(n, 1))
        return got


class Corpus:
    """The install plus the export's manifest: which model owns a stem or a bank."""

    def __init__(self, export_root: Path):
        self.export_root = export_root
        with (export_root / "npc" / "npc_manifest.json").open(encoding="utf-8-sig") as handle:
            self.manifest = json.load(handle)
        # Imported here rather than at module scope: `install` resolves `ELYSIUM_VTMB_ROOT` at
        # import time, and the arithmetic above is game-independent and unit-tested without one.
        from elysium_pipeline.formats import install
        self._install = install
        self.index = install.build_index(verbose=False)
        self._models: dict[str, Model] = {}
        self._clips: dict[str, dict] = {}

    def model_key(self, owner: str) -> str:
        record = self.manifest["npcs"].get(owner) or self.manifest["banks"].get(owner)
        if record is None:
            raise KeyError(f"'{owner}' is neither a body nor a bank in npc_manifest.json")
        return record["model"].replace("\\", "/").lower()

    def model(self, owner: str) -> Model:
        key = self.model_key(owner)
        got = self._models.get(key)
        if got is None:
            got = self._models[key] = Model(key, self._install.read(self.index, key))
        return got

    def clips(self, stem: str) -> dict:
        got = self._clips.get(stem)
        if got is None:
            with (self.export_root / "npc" / "clips" / f"{stem}.json").open(
                    encoding="utf-8") as handle:
                got = self._clips[stem] = json.load(handle)
        return got

    def resolve_global(self, stem: str, sequence: int):
        """A body's global sequence number -> (owner stem, label), through its clips sidecar."""
        clips = self.clips(stem)
        owners = clips["owners"]
        for label, indices in clips["seq"].items():
            if sequence in indices:
                rows = clips["clips"].get(label) or []
                owner = owners[rows[0][0]] if rows else stem
                return owner, label
        return None, None


# ---------------------------------------------------------------------------- evaluation

def _axis(model: Model, grid, axis: int, params: dict[str, float]):
    """(cell index, fraction) for one grid axis at the given pose-parameter values (A.3)."""
    n = grid.groupsize[axis]
    if n <= 1 or grid.paramindex[axis] < 0:
        return 0, 0.0
    desc = None
    for p in model.pose_params.values():
        if p.index == grid.paramindex[axis]:
            desc = p
            break
    if desc is None or desc.name.lower() not in params:
        return 0, 0.0
    value = params[desc.name.lower()]
    if desc.loop:
        span = desc.loop
        value = ((value - desc.start) % span) + desc.start
    denominator = (desc.end - desc.start) or 1.0
    normalized = (value - desc.start) / denominator
    lo, hi = grid.paramstart[axis], grid.paramend[axis]
    # The sequence's own range is stated in the parameter's units; remap the normalized value
    # through it, which cancels the descriptor's range exactly when the two agree.
    pstart = (lo - desc.start) / denominator
    pend = (hi - desc.start) / denominator
    if pend != pstart:
        normalized = (normalized - pstart) / (pend - pstart)
    normalized = min(max(normalized, 0.0), 1.0)
    t = normalized * (n - 1)
    i = min(int(math.floor(t)), n - 2)
    return i, t - i


def _cell_frame(model: Model, anim_index: int, cycle: float):
    """One animation at a cycle: per bone `(pos, quat)` or None where the record's weight is
    zero. `floor((numframes - 1) * cycle)` and the remainder (A.4)."""
    frames = model.frames(anim_index)
    n = len(frames)
    t = (n - 1) * min(max(cycle, 0.0), 1.0)
    f0 = min(int(math.floor(t)), n - 1)
    f1 = min(f0 + 1, n - 1)
    frac = t - f0
    out = []
    for bone in range(len(model.bones)):
        p0, q0 = frames[f0][bone]
        if qnorm(q0) is None:
            out.append(None)          # weight-zero record: no contribution at all
            continue
        if f1 == f0 or frac <= 0.0:
            out.append((tuple(p0), qnorm(q0)))
            continue
        p1, q1 = frames[f1][bone]
        out.append((vlerp(p0, p1, frac), qnlerp(qnorm(q0), qnorm(q1) or qnorm(q0), frac)))
    return out


def evaluate_sequence(model: Model, seq, cycle: float, params: dict[str, float],
                      force: dict[str, tuple[int, int]] | None = None):
    """A sequence's own pose at a cycle: its cells resolved against the pose parameters and
    blended, per bone `(pos, quat)` or None. `force` pins a sequence's cell outright, which is
    how a grid whose parameter the capture never recorded is searched rather than guessed."""
    grid = seq.grid
    pinned = (force or {}).get(seq.label.lower())
    if pinned is not None:
        if len(pinned) == 4:
            i0, s0, i1, s1 = pinned
        else:
            i0, s0 = pinned[0], 0.0
            i1, s1 = pinned[1], 0.0
    else:
        i0, s0 = _axis(model, grid, 0, params)
        i1, s1 = _axis(model, grid, 1, params)
    n0, n1 = grid.groupsize
    cells = {(c.axis0, c.axis1): c.anim for c in grid.cells}

    def cell(a, b):
        return _cell_frame(model, cells[(min(a, n0 - 1), min(b, n1 - 1))], cycle)

    def blend(a, b, t):
        if t <= 0.0:
            return a
        if t >= 1.0:
            return b
        out = []
        for x, y in zip(a, b):
            if x is None or y is None:
                out.append(x if y is None else y)
            else:
                out.append((vlerp(x[0], y[0], t), qnlerp(x[1], y[1], t)))
        return out

    row0 = blend(cell(i0, i1), cell(i0 + 1, i1), s0) if n0 > 1 else cell(0, i1)
    if n1 <= 1:
        return row0
    row1 = blend(cell(i0, i1 + 1), cell(i0 + 1, i1 + 1), s0) if n0 > 1 else cell(0, i1 + 1)
    return blend(row0, row1, s1)


#: Diagnostic: skip every delta layer, to attribute a residual to the additive or not.
SKIP_DELTAS = False


def accumulate(out: list, layer: list, s: float, flags: int) -> None:
    """`FUN_10088e10`, one evaluated layer onto the running pose, in place (A.4)."""
    if s <= 0.0 or (SKIP_DELTAS and flags & FLAG_DELTA):
        return
    s = min(s, 1.0)
    additive = bool(flags & FLAG_DELTA)
    post = bool(flags & FLAG_POST)
    for bone, contribution in enumerate(layer):
        if contribution is None or out[bone] is None:
            continue
        pos, quat = out[bone]
        lpos, lquat = contribution
        if not additive:
            out[bone] = (vlerp(pos, lpos, s), qnlerp(quat, lquat, s))
            continue
        scaled = qscale(lquat, s)
        mixed = qmul(quat, scaled) if post else qmul(scaled, quat)
        out[bone] = (tuple(p + d * s for p, d in zip(pos, lpos)), qnorm(mixed) or quat)


def gate_to(layer: list, owned: set | None) -> list:
    """`layer` restricted to the bone indices in `owned`; unchanged when `owned` is None.

    An overlay slot's whole accumulation -- its sequence and that sequence's autolayers alike --
    reaches only the bones the slot's sequence owns. Measured on the capture rather than read off
    an address: over the female Malkavian's 36 full-weight `m37_attack_layer` frames the legs fit
    2.14 cm with the attack delta kept off them and 4.48 cm with it applied, the upper body
    unchanged either way; the delta's own records reach the legs, the slot does not.
    """
    if owned is None:
        return layer
    return [c if i in owned else None for i, c in enumerate(layer)]


def remap(body: Model, owner: Model, pose: list) -> list:
    """An owner-indexed pose carried onto the body's bones by identical name; None elsewhere."""
    out = [None] * len(body.bones)
    for index, contribution in enumerate(pose):
        if contribution is None:
            continue
        target = body.by_name.get(owner.bones[index].name.lower())
        if target is not None:
            out[target.index] = contribution
    return out


def compose(corpus: Corpus, stem: str, channels: Iterable[dict], params: dict[str, float],
            force: dict[str, tuple[int, int]] | None = None,
            layer_cycles: dict[str, float] | None = None):
    """The composed LOCAL pose of `stem` for an ordered channel list, per body bone.

    Each channel is `{owner, label, cycle, weight}`; the first is the base and its declared
    autolayers are composed after it at its own cycle with weight 1.0, then every further channel
    in order with its own weight and, likewise, its autolayers. A body bone no channel reaches
    holds its bind.
    """
    body = corpus.model(stem)
    out = [(tuple(b.pos), qnorm(b.quat)) for b in body.bones]

    def apply(owner: str, label: str, cycle: float, weight: float, depth: int = 0,
              gate: set | None = None):
        model = corpus.model(owner)
        seq = model.sequences.get(label.lower())
        if seq is None:
            raise KeyError(f"'{label}' is not a sequence of '{owner}'")
        layer = remap(body, model, evaluate_sequence(model, seq, cycle, params, force))
        if depth == 0 and gate is not None:
            # A slot channel: its own bones gate everything the channel accumulates (`gate_to`).
            gate = {i for i, c in enumerate(layer) if c is not None}
        accumulate(out, gate_to(layer, gate), weight, seq.flags)
        if depth == 0:
            for target in seq.autolayers:
                # Retail evaluates an autolayer at the HOST's cycle; `layer_cycles` is a
                # diagnostic override for holding a graph that does otherwise to account.
                apply(owner, target, (layer_cycles or {}).get(target.lower(), cycle), 1.0,
                      depth + 1, gate)

    for index, channel in enumerate(channels):
        apply(channel["owner"], channel["label"], channel["cycle"], channel.get("weight", 1.0),
              gate=set() if (index > 0 or channel.get("slot")) else None)
    return body, out


def model_space(body: Model, locals_: list):
    """Model-space `(pos, quat)` per bone with retail's split-rotation rule (A.4a)."""
    out = []
    for bone in body.bones:
        pos, quat = locals_[bone.index]
        if bone.parent < 0:
            out.append((tuple(pos), quat))
            continue
        ppos, pquat = out[bone.parent]
        world_pos = tuple(a + b for a, b in zip(ppos, qrotate(pquat, pos)))
        world_rot = quat if (bone.flags & SPLIT_ROTATION) else (qnorm(qmul(pquat, quat)) or pquat)
        out.append((world_pos, world_rot))
    return out


def relative_to_root(body: Model, world: list, root_name: str):
    """Each bone's position in the root bone's own frame, which is what a bone-to-world capture
    reduces to once the entity's placement is divided out."""
    root = body.by_name[root_name.lower()].index
    rpos, rquat = world[root]
    inv = qconj(rquat)
    return {body.bones[i].name: qrotate(inv, tuple(a - b for a, b in zip(pos, rpos)))
            for i, (pos, _q) in enumerate(world)}


# ---------------------------------------------------------------------------- the capture

def _capture_relative(frame: dict) -> dict[str, tuple]:
    """A `pose_oracle` frame's bones in its root bone's frame, from the 3x4 bone-to-world rows."""
    root = frame["bones"][frame["root_bone"]]
    r = ((root[0], root[1], root[2]), (root[4], root[5], root[6]), (root[8], root[9], root[10]))
    t = (root[3], root[7], root[11])
    out = {}
    for name, m in frame["bones"].items():
        d = (m[3] - t[0], m[7] - t[1], m[11] - t[2])
        # R^T d
        out[name] = (r[0][0] * d[0] + r[1][0] * d[1] + r[2][0] * d[2],
                     r[0][1] * d[0] + r[1][1] * d[1] + r[2][1] * d[2],
                     r[0][2] * d[0] + r[1][2] * d[1] + r[2][2] * d[2])
    return out


def _move_yaw(state: dict) -> float | None:
    """`move_yaw = AngleDiff(facing, VecToYaw(velocity))`, facing the minuend (A.4)."""
    vx, vy, _vz = state.get("velocity", [0, 0, 0])
    if math.hypot(vx, vy) <= 20.0:
        return None
    vel_yaw = math.degrees(math.atan2(vy, vx)) % 360.0
    facing = float(state.get("aim_yaw", 0.0)) % 360.0
    diff = (facing - vel_yaw + 180.0) % 360.0 - 180.0
    return diff


def _dist(a, b):
    return math.dist(a, b)


def _describe(values):
    if not values:
        return "n=0"
    return "n=%d mean %.2f sd %.2f p2p %.2f" % (
        len(values), statistics.fmean(values), statistics.pstdev(values), max(values) - min(values))


def _grids_in_closure(model: Model, seq) -> list:
    """The multi-cell grids among a sequence's autolayers -- the aim layer, on a weapon host."""
    out = []
    for target in seq.autolayers:
        t = model.sequences.get(target.lower())
        if t is not None and t.grid.numblends > 1:
            out.append(t)
    return out


def validate_session(corpus: Corpus, session: Path, stems: Iterable[str] | None = None,
                     arm=("Bip01 R Hand", "Bip01 Spine1"), search_aim: bool = False) -> int:
    """Compose every joined, full-weight, unlayered frame of the session and hold this module
    to the capture per bone and per state. Returns a process exit code."""
    poses = json.loads((session / "pose_oracle.json").read_text(encoding="utf-8"))["frames"]
    layers = json.loads((session / "layer_oracle.json").read_text(encoding="utf-8"))["frames"]
    by_key = {(round(f["curtime"], 5), f["stem"]): f for f in layers}
    wanted = set(stems or ())
    per_bone: dict[str, list[float]] = defaultdict(list)
    per_state_err: dict[str, list[float]] = defaultdict(list)
    arm_ours: dict[str, list[float]] = defaultdict(list)
    arm_theirs: dict[str, list[float]] = defaultdict(list)
    cell_hist: dict[str, dict] = defaultdict(lambda: defaultdict(int))
    state_bones: dict[str, dict] = defaultdict(lambda: defaultdict(list))
    joined = set_aside = layered = composed = unresolved = 0
    for frame in poses:
        stem = frame["stem"]
        if wanted and stem not in wanted:
            continue
        rec = by_key.get((round(frame["curtime"], 5), stem))
        if rec is None:
            continue
        joined += 1
        channels = rec.get("channels", [])
        if any(not c["additive"] and c["weight"] < FULL_WEIGHT for c in channels):
            set_aside += 1
            continue
        owner, label = corpus.resolve_global(stem, int(rec["sequence"]))
        if label is None:
            unresolved += 1
            continue
        base_model = corpus.model(owner)
        base_seq = base_model.sequences.get(label.lower())
        closure = {t.lower() for t in (base_seq.autolayers if base_seq else ())} | {label.lower()}
        if any(c["label"].lower() not in closure for c in channels):
            layered += 1          # an overlay slot stood; its phase is unrecorded
            continue
        state = frame["player_state"]
        params: dict[str, float] = {}
        yaw = _move_yaw(state)
        moving = yaw is not None
        if moving:
            params["move_yaw"] = yaw
        params["aim_yaw"] = 0.0   # a literal on the player (T18); pitch is unrecorded
        theirs = _capture_relative(frame)
        channel = [{"owner": owner, "label": label, "cycle": float(rec["cycle"]), "weight": 1.0}]
        grids = _grids_in_closure(base_model, base_seq) if (search_aim and base_seq) else []
        candidates = [None]
        if grids:
            g = grids[0]
            n0, n1 = g.grid.groupsize
            # Yaw is a literal 0 on the player, which the first sweep showed is the CENTRE column;
            # pitch is the slewed view pitch and lands between cells, so it is searched as a
            # fraction across the whole axis in quarter-cell steps.
            mid = n0 // 2
            candidates = [{g.label.lower(): (mid, 0.0, min(int(t), n1 - 2), t - int(t))}
                          for t in [k * 0.25 for k in range(4 * (n1 - 1) + 1)]]
        best = None
        for force in candidates:
            body, locals_ = compose(corpus, stem, channel, params, force)
            got = relative_to_root(body, model_space(body, locals_), frame["root_bone"])
            score = statistics.median(
                _dist(got[n], p) for n, p in theirs.items() if n in got) if len(candidates) > 1 else 0.0
            if best is None or score < best[0]:
                best = (score, got, force)
        ours, force = best[1], best[2]
        if force:
            cell = next(iter(force.values()))
            pitch = round(cell[2] + cell[3], 2) if len(cell) == 4 else cell
            cell_hist[f"{stem} {label}{' moving' if yaw is not None else ' at rest'}"][pitch] += 1
        # Only the bones the base's owner animates: everything else is bind here and secondary
        # motion or a rig rule in retail, which this module does not perform.
        driven = {base_model.bones[i].name for i, c in enumerate(
            evaluate_sequence(base_model, base_seq, float(rec["cycle"]), params)) if c is not None}
        errs = []
        for name, pos in theirs.items():
            if name not in ours or name not in driven:
                continue
            e = _dist(ours[name], pos) * SOURCE_UNIT_TO_CM
            per_bone[name].append(e)
            errs.append(e)
            state_bones[f"{stem} {label}{' moving' if moving else ' at rest'}"][name].append(e)
        if not errs:
            continue
        composed += 1
        key = f"{stem} {label}{' moving' if moving else ' at rest'}"
        per_state_err[key].append(statistics.median(errs))
        if moving and arm[0] in ours and arm[1] in ours and arm[0] in theirs:
            arm_ours[key].append(_dist(ours[arm[0]], ours[arm[1]]) * SOURCE_UNIT_TO_CM)
            arm_theirs[key].append(_dist(theirs[arm[0]], theirs[arm[1]]) * SOURCE_UNIT_TO_CM)

    print(f"[oracle] {session.name}: {joined} joined, {set_aside} set aside mid cross-fade, "
          f"{layered} layered (unrecorded slot phase), {unresolved} unresolved, "
          f"{composed} composed")
    for key in sorted(per_state_err):
        errs = per_state_err[key]
        line = f"[oracle]   {key:60s} n={len(errs):3d} median {statistics.median(errs):6.3f} cm"
        if key in cell_hist:
            hist = " ".join(f"{c}:{n}" for c, n in sorted(cell_hist[key].items()))
            line += f" | pitch {{{hist}}}"
        if key in state_bones:
            top = sorted(state_bones[key].items(), key=lambda kv: -statistics.median(kv[1]))[:4]
            line += " | worst " + ", ".join(f"{n.replace('Bip01 ', '')} {statistics.median(v):.1f}"
                                            for n, v in top)
        if key in arm_ours:
            line += (f" | arm ours {_describe(arm_ours[key])} | capture "
                     f"{_describe(arm_theirs[key])}")
        print(line)
    worst = sorted(per_bone.items(), key=lambda kv: -statistics.median(kv[1]))[:12]
    print("[oracle] bones carrying the error, worst first:")
    for name, errs in worst:
        print(f"[oracle]   {name:28s} median {statistics.median(errs):6.3f} cm  n={len(errs)}")
    return 0 if composed else 2


# ---------------------------------------------------------------------------- the dense oracle

#: The `move_yaw` values a fan is sampled at: every spoke of a 9x1 fan plus four between spokes.
MOVE_YAW_SAMPLES = [-180.0, -160.0, -135.0, -90.0, -45.0, -20.0, 0.0, 20.0, 45.0, 90.0, 100.0,
                    135.0, 180.0]
#: The aim pitch values, in the parameter's own degrees: the centre row and one either side.
AIM_PITCH_SAMPLES = [-30.0, 0.0, 30.0]
#: Phases through a cycle. Eight is enough to catch a phase error and small enough to read.
PHASES = [k / 8.0 for k in range(8)]


def _unreal(pos):
    from elysium_pipeline.formats.bsp import source_to_unreal
    return [round(c, 4) for c in source_to_unreal(*pos)]


def oracle_states(corpus: Corpus, stem: str, hosts: Iterable[str]):
    """Every state the oracle answers for `stem` over the named base hosts."""
    for label in hosts:
        owner, seq = _find_sequence(corpus, stem, label)
        # A masked host (an attack layer) only ever stands in an overlay slot, so its state is
        # composed the way a slot composes it: gated to the bones it owns (`gate_to`).
        masked = any(c is None for c in evaluate_sequence(corpus.model(owner), seq, 0.0,
                                                          {"aim_yaw": 0.0, "aim_pitch": 0.0}))
        yaws = MOVE_YAW_SAMPLES if seq.grid.numblends > 1 else [None]
        grids = _grids_in_closure(corpus.model(owner), seq)
        pitches = AIM_PITCH_SAMPLES if grids else [None]
        for yaw in yaws:
            for pitch in pitches:
                for cycle in PHASES:
                    params = {"aim_yaw": 0.0}
                    if yaw is not None:
                        params["move_yaw"] = yaw
                    if pitch is not None:
                        params["aim_pitch"] = pitch
                    yield {"owner": owner, "label": seq.label, "cycle": cycle,
                           "params": params, "slot": masked}


def _find_sequence(corpus: Corpus, stem: str, label: str):
    """(owner, Seq) for a label the body plays, through the sidecar's owner list."""
    clips = corpus.clips(stem)
    rows = clips["clips"].get(label)
    if not rows:
        raise KeyError(f"'{stem}' does not play '{label}'")
    owner = clips["owners"][rows[0][0]]
    seq = corpus.model(owner).sequences.get(label.lower())
    if seq is None:
        raise KeyError(f"'{label}' is not a sequence of '{owner}'")
    return owner, seq


def emit_oracle(corpus: Corpus, stem: str, hosts: Iterable[str], out_dir: Path) -> Path:
    """Write `<out_dir>/<stem>.json`: the composed pose for every state, root-relative,
    Unreal-native centimetres through the exporter's own conversion.

    Positions only, per bone: they are what every downstream instrument can read off a
    skeletal mesh component without a second convention, and a wrong local rotation anywhere
    in a chain shows up as a wrong position at its children.
    """
    body = corpus.model(stem)
    root = next(b.name for b in body.bones if b.parent < 0)
    frames = []
    for state in oracle_states(corpus, stem, hosts):
        _body, locals_ = compose(corpus, stem, [{"owner": state["owner"], "label": state["label"], "slot": state["slot"],
                                                 "cycle": state["cycle"], "weight": 1.0}],
                                 state["params"])
        rel = relative_to_root(body, model_space(body, locals_), root)
        frames.append({**state, "root_bone": root,
                       "bones": {name: _unreal(pos) for name, pos in rel.items()}})
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{stem}.json"
    path.write_text(json.dumps({"schema": "elysium-oracle-1", "stem": stem,
                                "space": "unreal-cm-root-relative",
                                "frames": frames}, indent=None), encoding="utf-8")
    return path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--export-root", type=Path, required=True)
    parser.add_argument("--validate", type=Path, metavar="SESSION",
                        help="a life_rig_pose session directory carrying both oracles")
    parser.add_argument("--stem", action="append", default=[])
    parser.add_argument("--no-delta", action="store_true",
                        help="diagnostic: compose without any delta layer")
    parser.add_argument("--emit", type=Path, metavar="OUT_DIR",
                        help="write the dense oracle for every --stem over --host labels")
    parser.add_argument("--host", action="append", default=[],
                        help="a base sequence label to answer states for (repeatable)")
    parser.add_argument("--search-aim", action="store_true",
                        help="try every cell of the aim grid per frame and report which one "
                             "retail drew, per state")
    args = parser.parse_args(argv)
    corpus = Corpus(args.export_root)
    global SKIP_DELTAS
    SKIP_DELTAS = bool(args.no_delta)
    if args.validate:
        return validate_session(corpus, args.validate, args.stem, search_aim=args.search_aim)
    if args.emit:
        if not args.stem or not args.host:
            parser.error("--emit needs at least one --stem and one --host")
        for stem in args.stem:
            path = emit_oracle(corpus, stem, args.host, args.emit)
            print(f"[oracle] wrote {path}")
        return 0
    parser.error("nothing to do")
    return 2


if __name__ == "__main__":
    sys.exit(main())
