"""Placed MDL discovery and authored rest-pose policy.

This module is deliberately engine-neutral.  It inventories the model references already
exported from BSP entities and GAME_LUMP static props, resolves the sequence that
``CBaseAnimating`` stands on, and answers whether evaluating that sequence changes the stored
mesh.  The Unreal exporter consumes the answer; no Source/VtMB pose rule reaches runtime.
"""

from __future__ import annotations

import glob
import json
import math
import os
import struct
from dataclasses import dataclass

ACT_IDLE = "act_idle"
POSITION_TOLERANCE_SOURCE = 0.01 / 2.54  # 0.01 cm in Source inches
NORMAL_TOLERANCE_DOT = math.cos(math.radians(0.1))


def normalize_model_path(value: str) -> str:
    value = str(value or "").strip().lower().replace("\\", "/")
    if value and not value.startswith("models/"):
        value = "models/" + value
    return value


def model_stem(model_path: str) -> str:
    from elysium_pipeline.formats import mdl

    key = normalize_model_path(model_path)
    return mdl.sanitize(key[7:-4] if key.startswith("models/") and key.endswith(".mdl")
                        else key)


def fnv1a_32(model_path: str, placement_token: int) -> int:
    """Stable cross-language selection seed (UTF-8 path, then little-endian uint32 token)."""
    value = 2166136261
    payload = normalize_model_path(model_path).encode("utf-8")
    payload += struct.pack("<I", max(0, int(placement_token)) & 0xFFFFFFFF)
    for byte in payload:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def rest_candidates(sequences) -> list:
    sequences = list(sequences)
    idle = [seq for seq in sequences if str(seq.activity).lower() == ACT_IDLE]
    return sorted(idle, key=lambda seq: sequences.index(seq)) if idle else sequences[:1]


def select_rest_sequence(model_path: str, sequences, placement_token: int):
    candidates = rest_candidates(sequences)
    if not candidates:
        return None
    if len(candidates) == 1:
        return candidates[0]
    total = sum(max(1, int(seq.actweight)) for seq in candidates)
    pick = fnv1a_32(model_path, placement_token) % total
    for seq in candidates:
        weight = max(1, int(seq.actweight))
        if pick < weight:
            return seq
        pick -= weight
    return candidates[-1]


def select_rest_label(model_path: str, record: dict, placement_token: int) -> str:
    """Select from a serialized v7 row using the same weighted walk as the MDL form."""
    rows = {row.get("name", ""): row for row in record.get("clips", [])}
    names = [name for name in record.get("rest_candidates", []) if name in rows]
    if not names:
        return ""
    total = sum(max(1, int(rows[name].get("weight", 0))) for name in names)
    pick = fnv1a_32(model_path, placement_token) % total
    for name in names:
        weight = max(1, int(rows[name].get("weight", 0)))
        if pick < weight:
            return name
        pick -= weight
    return names[-1]


@dataclass(frozen=True)
class PlacedModelUse:
    model: str
    stem: str
    full_clips: bool = False


def discover(out_root: str, install_index=None) -> list[PlacedModelUse]:
    """Return every non-NPC MDL referenced by exported ``.ents`` or ``.props`` files.

    Current ``.props`` rows append the normalized source model.  For an older export, the
    sanitized OBJ stem is joined back through the patch-first install index when that join is
    unambiguous, keeping a stale workspace diagnosable while a fresh map export remains the
    authoritative producer.
    """
    from elysium_pipeline.formats import mdl

    models: dict[str, bool] = {}
    full = set()
    docs = []
    for path in glob.glob(os.path.join(out_root, "*", "*.ents")):
        try:
            with open(path, encoding="utf-8") as handle:
                doc = json.load(handle)
        except (OSError, ValueError):
            continue
        docs.append(doc)
        for ent in doc.get("entities", []):
            if str(ent.get("classname", "")).lower().startswith("npc_"):
                continue
            model = normalize_model_path(ent.get("keys", {}).get("model", ""))
            if model.endswith(".mdl"):
                models[model] = True

    animation_targets = set()
    for doc in docs:
        for ent in doc.get("entities", []):
            for wire in ent.get("outputs", []):
                if str(wire.get("input", "")).lower() == "setanimation":
                    target = str(wire.get("target", "")).strip().lower()
                    if target and not target.startswith("!"):
                        animation_targets.add(target)

    def targeted(name: str) -> bool:
        name = str(name or "").lower()
        return any(name.startswith(t[:-1]) if t.endswith("*") else name == t
                   for t in animation_targets)

    for doc in docs:
        for ent in doc.get("entities", []):
            keys = ent.get("keys", {})
            model = normalize_model_path(keys.get("model", ""))
            if model not in models:
                continue
            meaningful = lambda value: str(value or "").strip().lower() not in ("", "0", "none", "null")
            if (meaningful(keys.get("demo_sequence"))
                    or meaningful(keys.get("LoopSequence"))
                    or targeted(ent.get("targetname", ""))):
                full.add(model)

    by_stem: dict[str, list[str]] = {}
    if install_index is not None:
        for key in install_index:
            if key.startswith("models/") and key.endswith(".mdl"):
                # Current model stems omit the leading `models/`; legacy `.props` safenames did
                # not. Index both spellings so an old sidecar is diagnosable before map re-export.
                for stem in {model_stem(key), mdl.sanitize(key[:-4])}:
                    by_stem.setdefault(stem, []).append(key)

    for path in glob.glob(os.path.join(out_root, "*", "*.props")):
        try:
            lines = open(path, encoding="utf-8")
        except OSError:
            continue
        with lines:
            for line in lines:
                fields = line.split()
                if len(fields) < 11:
                    continue
                if len(fields) >= 12:
                    model = normalize_model_path(fields[11])
                else:
                    choices = by_stem.get(fields[0], [])
                    model = normalize_model_path(choices[0]) if len(choices) == 1 else ""
                if model.endswith(".mdl"):
                    models[model] = True

    uses = [PlacedModelUse(model, model_stem(model), model in full) for model in models]
    stems: dict[str, str] = {}
    for use in uses:
        previous = stems.setdefault(use.stem, use.model)
        if previous != use.model:
            raise ValueError(f"placed-model stem collision: {previous} and {use.model} -> {use.stem}")
    return sorted(uses, key=lambda use: use.model)


def _quat_matrix(quat) -> np.ndarray:
    import numpy as np

    x, y, z, w = quat
    length = math.sqrt(x*x + y*y + z*z + w*w)
    if length <= 1e-12:
        return np.identity(3)
    x, y, z, w = x/length, y/length, z/length, w/length
    return np.array([
        [1 - 2*(y*y + z*z), 2*(x*y - z*w), 2*(x*z + y*w)],
        [2*(x*y + z*w), 1 - 2*(x*x + z*z), 2*(y*z - x*w)],
        [2*(x*z - y*w), 2*(y*z + x*w), 1 - 2*(x*x + y*y)],
    ], dtype=np.float64)


def _transform(position, quat) -> np.ndarray:
    import numpy as np

    out = np.identity(4)
    out[:3, :3] = _quat_matrix(quat)
    out[:3, 3] = position
    return out


def _inverse_bind(bone) -> np.ndarray:
    import numpy as np

    raw = np.asarray(bone.pose_to_bone, dtype=np.float64).reshape(3, 4)
    out = np.identity(4)
    out[:3, :] = raw
    return out


def _geometric_normals(surface) -> np.ndarray:
    import numpy as np

    positions = np.asarray(surface["pos"], dtype=np.float64)
    normals = np.asarray(surface["nrm"], dtype=np.float64)
    missing = np.linalg.norm(normals, axis=1) <= 1e-12
    if not missing.any():
        lengths = np.linalg.norm(normals, axis=1)
        return normals / np.maximum(lengths[:, None], 1e-12)
    generated = np.zeros_like(positions)
    for a, b, c in surface["tris"]:
        face = np.cross(positions[b] - positions[a], positions[c] - positions[a])
        generated[a] += face; generated[b] += face; generated[c] += face
    lengths = np.linalg.norm(generated, axis=1)
    generated /= np.maximum(lengths[:, None], 1e-12)
    normals[missing] = generated[missing]
    lengths = np.linalg.norm(normals, axis=1)
    return normals / np.maximum(lengths[:, None], 1e-12)


def rest_pose_static_equivalent(d: bytes, v: bytes, candidates) -> bool:
    """Whether every possible resting frame evaluates to the stored geometry.

    A malformed/unsupported model returns ``False``: skeletal is the conservative answer.
    """
    try:
        import numpy as np
        from elysium_pipeline.formats import mdl_skel as S
        from elysium_pipeline.exporters import UE_mdl_skeletal as UEK

        bones = S.read_bones(d)
        surfaces = S.decode_skinned(d, v)
        if not bones or not surfaces or not candidates:
            return False
        inverse = [_inverse_bind(bone) for bone in bones]
        for sequence in candidates:
            frames = S.read_anim(d, bones, sequence.base, sequence.frames)
            if not frames:
                return False
            split = UEK._split_rotation_tracks(bones, frames, sequence.frames)
            world = []
            for bone in bones:
                position, quat = frames[0][bone.index]
                if np.linalg.norm(quat) <= 1e-12:
                    position, quat = bone.pos, bone.quat
                if bone.index in split:
                    quat = split[bone.index][0]
                local = _transform(position, quat)
                world.append(local if bone.parent < 0 else world[bone.parent] @ local)
            skin = np.asarray(
                [world[i] @ inverse[i] for i in range(len(bones))], dtype=np.float64)

            for surface in surfaces.values():
                positions = np.asarray(surface["pos"], dtype=np.float64)
                normals = _geometric_normals(surface)
                joints = np.asarray(surface["joints"], dtype=np.int64)
                weights = np.asarray(surface["weights"], dtype=np.float64)
                if (joints.ndim != 2 or weights.shape != joints.shape
                        or positions.shape != normals.shape
                        or positions.shape[0] != joints.shape[0]
                        or np.any(joints < 0) or np.any(joints >= len(skin))):
                    return False

                transforms = skin[joints]
                points = np.concatenate(
                    (positions, np.ones((positions.shape[0], 1), dtype=np.float64)), axis=1)
                posed = np.einsum("nkij,nj,nk->ni", transforms, points, weights,
                                  optimize=True)[:, :3]
                posed_normals = np.einsum(
                    "nkij,nj,nk->ni", transforms[:, :, :3, :3], normals, weights,
                    optimize=True)
                if np.any(np.linalg.norm(posed - positions, axis=1) > POSITION_TOLERANCE_SOURCE):
                    return False
                lengths = np.linalg.norm(posed_normals, axis=1)
                if np.any(lengths <= 1e-12):
                    return False
                posed_normals /= lengths[:, None]
                if np.any(np.einsum("ni,ni->n", posed_normals, normals) < NORMAL_TOLERANCE_DOT):
                    return False
        return True
    except (IndexError, KeyError, TypeError, ValueError, struct.error, np.linalg.LinAlgError):
        return False
