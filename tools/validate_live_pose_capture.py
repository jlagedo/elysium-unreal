"""Validate a live retail VtMB pose capture against its patch-first MDL data.

The capture contains StudioRender's bone-to-world and final skin palettes.  This
tool verifies the inverse-bind multiplication, resolves a named included-model
clip, aligns the live pose to authored frames using shared bone names, and
compares split-inheritance evaluation with an ordinary hierarchy.
"""

from __future__ import annotations

import argparse
from bisect import bisect_left
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct

import numpy as np

import install
import mdl_skel


DEFAULT_MODEL = (
    "models/character/pc/male/tremere/armor0/tremere_Male_Armor_0.mdl"
)


def matrix_from_quaternion(position, quaternion) -> np.ndarray:
    x, y, z, w = quaternion
    matrix = np.eye(4, dtype=np.float64)
    matrix[:3, :3] = (
        (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
        (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
        (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)),
    )
    matrix[:3, 3] = position
    return matrix


def matrices_from_payload(payload: bytes, bone_count: int) -> np.ndarray:
    expected = bone_count * 12 * 4
    if len(payload) != expected:
        raise ValueError(f"expected {expected} matrix bytes, got {len(payload)}")
    compact = np.frombuffer(payload, dtype="<f4").reshape(bone_count, 3, 4)
    matrices = np.repeat(np.eye(4, dtype=np.float64)[None, :, :], bone_count, 0)
    matrices[:, :3, :] = compact
    return matrices


def root_relative(matrices: np.ndarray) -> np.ndarray:
    inverse_root = np.linalg.inv(matrices[0])
    return np.stack([(inverse_root @ matrix)[:3, :] for matrix in matrices])


def rms(values: np.ndarray) -> float:
    return float(math.sqrt(float(np.mean(values * values))))


def summary(values: list[float]) -> dict[str, float]:
    array = np.asarray(values, dtype=np.float64)
    return {
        "min": float(np.min(array)),
        "median": float(np.median(array)),
        "max": float(np.max(array)),
    }


def resolve_clip(index, model_key: str, label: str):
    def load(key: str):
        key = key if key.lower().endswith(".mdl") else key + ".mdl"
        return install.read(index, key)

    for owner_key, owner_data in mdl_skel.resolve_tree(load, model_key):
        for sequence in mdl_skel.local_sequences(owner_data):
            if sequence.label.lower() == label.lower():
                return owner_key, owner_data, sequence
    raise ValueError(f"{model_key}: no resolved clip named {label!r}")


def evaluated_target_pose(
    target_bones,
    owner_by_name,
    owner_frame,
    *,
    split_inheritance: bool,
) -> np.ndarray:
    world: list[np.ndarray] = []
    for target in target_bones:
        owner = owner_by_name.get(target.name.lower())
        position, quaternion = (
            owner_frame[owner.index] if owner else (target.pos, target.quat)
        )
        local = matrix_from_quaternion(position, quaternion)
        if target.parent < 0:
            matrix = local
        elif split_inheritance and target.flags & 0x2:
            matrix = np.eye(4, dtype=np.float64)
            matrix[:3, :3] = local[:3, :3]
            matrix[:3, 3] = (
                world[target.parent]
                @ np.asarray((*position, 1.0), dtype=np.float64)
            )[:3]
        else:
            matrix = world[target.parent] @ local
        world.append(matrix)
    return root_relative(np.stack(world))


def load_model(index, model_key: str) -> tuple[str, bytes]:
    key = model_key.replace("\\", "/")
    if not key.lower().startswith("models/"):
        key = "models/" + key
    if not key.lower().endswith(".mdl"):
        key += ".mdl"
    data = install.read(index, key)
    if data is None:
        raise FileNotFoundError(f"{key} is not present in the merged install")
    return key, data


def find_local_clip(model_key: str, model_data: bytes, label: str):
    for sequence in mdl_skel.local_sequences(model_data):
        if sequence.label.lower() == label.lower():
            return sequence
    raise ValueError(f"{model_key}: no local clip named {label!r}")


def descendant_indices(bones, root_name: str) -> set[int]:
    roots = [bone.index for bone in bones if bone.name.lower() == root_name.lower()]
    if len(roots) != 1:
        raise ValueError(
            f"animation model has {len(roots)} bones named {root_name!r}; expected one"
        )
    root = roots[0]
    descendants = {root}
    for bone in bones:
        parent = bone.parent
        while parent >= 0:
            if parent == root:
                descendants.add(bone.index)
                break
            parent = bones[parent].parent
    return descendants


def longest_increasing_pairs(
    authored_indices: np.ndarray, credible: np.ndarray
) -> list[tuple[int, int]]:
    """Longest strictly increasing subsequence of credible nearest-frame matches."""
    tails: list[int] = []
    tails_at: list[int] = []
    previous = [-1] * len(authored_indices)
    for live_index, authored_index in enumerate(authored_indices):
        if not credible[live_index]:
            continue
        value = int(authored_index)
        slot = bisect_left(tails, value)
        if slot == len(tails):
            tails.append(value)
            tails_at.append(live_index)
        else:
            tails[slot] = value
            tails_at[slot] = live_index
        if slot:
            previous[live_index] = tails_at[slot - 1]
    if not tails_at:
        return []
    live_index = tails_at[-1]
    pairs: list[tuple[int, int]] = []
    while live_index >= 0:
        pairs.append((live_index, int(authored_indices[live_index])))
        live_index = previous[live_index]
    pairs.reverse()
    return pairs


def nearest_frame_matches(
    live: np.ndarray, predicted: np.ndarray
) -> tuple[np.ndarray, np.ndarray]:
    """Find nearest authored frames without materializing the full LxF distance cube."""
    live_flat = live.reshape(len(live), -1).astype(np.float32, copy=False)
    predicted_flat = predicted.reshape(len(predicted), -1).astype(
        np.float32, copy=False
    )
    divisor = float(live_flat.shape[1])
    predicted_squared = np.sum(
        predicted_flat * predicted_flat, axis=1, dtype=np.float64
    )
    nearest = np.empty(len(live_flat), dtype=np.int32)
    nearest_rms = np.empty(len(live_flat), dtype=np.float64)
    for begin in range(0, len(live_flat), 256):
        batch = live_flat[begin : begin + 256]
        live_squared = np.sum(batch * batch, axis=1, dtype=np.float64)
        squared = (
            live_squared[:, None]
            + predicted_squared[None, :]
            - 2.0 * (batch @ predicted_flat.T)
        ) / divisor
        np.maximum(squared, 0.0, out=squared)
        batch_nearest = np.argmin(squared, axis=1)
        row = np.arange(len(batch_nearest))
        nearest[begin : begin + len(batch)] = batch_nearest
        nearest_rms[begin : begin + len(batch)] = np.sqrt(
            squared[row, batch_nearest]
        )
    return nearest, nearest_rms


def validate(
    session: Path,
    model_key: str,
    clip_label: str,
    *,
    animation_model: str | None = None,
    bone_root: str | None = None,
    alignment_threshold: float = 0.05,
) -> dict[str, object]:
    manifest = json.loads((session / "manifest.json").read_text(encoding="utf-8"))
    if manifest["captured_frames"] != len(manifest["frames"]):
        raise ValueError("manifest captured frame count is inconsistent")

    index = install.build_index(verbose=False)
    model_key, model_data = load_model(index, model_key)
    checksum = struct.unpack_from("<I", model_data, 8)[0]
    if manifest["model_checksum"].lower() != f"0x{checksum:08x}":
        raise ValueError("capture/model checksum mismatch")

    target_bones = mdl_skel.read_bones(model_data)
    if len(target_bones) != manifest["bone_count"]:
        raise ValueError("capture/model bone-count mismatch")

    live_bone_world: list[np.ndarray] = []
    live_skin: list[np.ndarray] = []
    for frame in manifest["frames"]:
        live_bone_world.append(
            matrices_from_payload(
                (session / frame["bone_to_world"]).read_bytes(), len(target_bones)
            )
        )
        live_skin.append(
            matrices_from_payload(
                (session / frame["skin_palette"]).read_bytes(), len(target_bones)
            )
        )

    pose_to_bone = []
    for bone in target_bones:
        matrix = np.eye(4, dtype=np.float64)
        matrix[:3, :] = np.asarray(bone.pose_to_bone).reshape(3, 4)
        pose_to_bone.append(matrix)
    pose_to_bone = np.stack(pose_to_bone)

    skin_frame_rms: list[float] = []
    skin_max_abs = 0.0
    skin_worst: dict[str, object] | None = None
    for frame_index, (bone_world, actual_skin) in enumerate(
        zip(live_bone_world, live_skin)
    ):
        expected_skin = bone_world @ pose_to_bone
        differences = expected_skin[:, :3, :] - actual_skin[:, :3, :]
        skin_frame_rms.append(rms(differences))
        flat_index = int(np.argmax(np.abs(differences)))
        absolute = float(np.abs(differences).flat[flat_index])
        if absolute > skin_max_abs:
            bone_index, row, column = np.unravel_index(
                flat_index, differences.shape
            )
            bone_index = int(bone_index)
            skin_max_abs = absolute
            skin_worst = {
                "frame": frame_index,
                "bone": bone_index,
                "bone_name": target_bones[bone_index].name,
                "row": int(row),
                "column": int(column),
            }

    if animation_model:
        owner_key, owner_data = load_model(index, animation_model)
        sequence = find_local_clip(owner_key, owner_data, clip_label)
    else:
        owner_key, owner_data, sequence = resolve_clip(index, model_key, clip_label)
    owner_bones = mdl_skel.read_bones(owner_data)
    if bone_root:
        owner_indices = descendant_indices(owner_bones, bone_root)
        owner_by_name = {
            bone.name.lower(): bone
            for bone in owner_bones
            if bone.index in owner_indices
        }
    else:
        owner_by_name = {bone.name.lower(): bone for bone in owner_bones}
    owner_frames = mdl_skel.read_anim(
        owner_data, owner_bones, sequence.base, sequence.frames
    )
    predicted_split = np.stack(
        [
            evaluated_target_pose(
                target_bones,
                owner_by_name,
                frame,
                split_inheritance=True,
            )
            for frame in owner_frames
        ]
    )
    predicted_conventional = np.stack(
        [
            evaluated_target_pose(
                target_bones,
                owner_by_name,
                frame,
                split_inheritance=False,
            )
            for frame in owner_frames
        ]
    )
    live_relative = np.stack([root_relative(frame) for frame in live_bone_world])

    shared = np.asarray(
        [
            bone.index
            for bone in target_bones
            if bone.name.lower() in owner_by_name
        ],
        dtype=np.int32,
    )
    target_only = np.asarray(
        [
            bone.index
            for bone in target_bones
            if bone.name.lower() not in owner_by_name
        ],
        dtype=np.int32,
    )
    if not len(shared):
        raise ValueError("target and animation model have no shared bone names")
    nearest_authored, nearest_rms = nearest_frame_matches(
        live_relative[:, shared, :, :],
        predicted_split[:, shared, :, :],
    )
    credible = nearest_rms <= alignment_threshold
    offsets = Counter(
        int(authored - live)
        for live, authored in enumerate(nearest_authored)
        if credible[live]
    )
    if not offsets:
        raise ValueError(
            "no live frame aligns to the authored clip within RMS "
            f"{alignment_threshold}"
        )
    dominant_offset, _ = offsets.most_common(1)[0]
    aligned_pairs = longest_increasing_pairs(nearest_authored, credible)

    shared_split_rms: list[float] = []
    shared_conventional_rms: list[float] = []
    target_only_bind_rms: list[float] = []
    split_bones: dict[str, list[float]] = {
        bone.name: [] for bone in target_bones if bone.flags & 0x2
    }
    for live_index, authored_index in aligned_pairs:
        live = live_relative[live_index]
        shared_split_rms.append(
            rms(live[shared] - predicted_split[authored_index, shared])
        )
        shared_conventional_rms.append(
            rms(live[shared] - predicted_conventional[authored_index, shared])
        )
        if len(target_only):
            target_only_bind_rms.append(
                rms(live[target_only] - predicted_split[authored_index, target_only])
            )
        for bone in target_bones:
            if bone.flags & 0x2:
                split_bones[bone.name].append(
                    rms(live[bone.index] - predicted_split[authored_index, bone.index])
                )

    return {
        "version": 1,
        "capture": str(session.resolve()),
        "capture_manifest_sha256": hashlib.sha256(
            (session / "manifest.json").read_bytes()
        ).hexdigest(),
        "model": model_key,
        "model_checksum": f"0x{checksum:08x}",
        "bone_count": len(target_bones),
        "captured_frames": len(live_relative),
        "capture_span_seconds": manifest["frames"][-1]["seconds_after_arm"],
        "skin_palette_relation": {
            "equation": "skinPalette[i] = boneToWorld[i] * poseToBone[i]",
            "frame_rms": summary(skin_frame_rms),
            "max_absolute_element_error": skin_max_abs,
            "worst_element": skin_worst,
        },
        "clip": {
            "label": sequence.label,
            "owner": owner_key,
            "external_animation_model": animation_model is not None,
            "bone_root": bone_root,
            "frames": sequence.frames,
            "fps": sequence.fps,
            "flags": sequence.flags,
        },
        "live_to_authored_alignment": {
            "method": "root-relative RMS over case-insensitive name-shared bones",
            "alignment_threshold": alignment_threshold,
            "shared_bones": int(len(shared)),
            "target_only_bones": int(len(target_only)),
            "dominant_authored_minus_live_frame": dominant_offset,
            "credible_nearest_pair_count": int(np.count_nonzero(credible)),
            "nearest_authored_frames": [int(value) for value in nearest_authored],
            "nearest_authored_rms": [float(value) for value in nearest_rms],
            "aligned_pairs": [[live, authored] for live, authored in aligned_pairs],
            "aligned_pair_count": len(aligned_pairs),
            "split_hierarchy_shared_bone_rms": summary(shared_split_rms),
            "conventional_hierarchy_shared_bone_rms": summary(
                shared_conventional_rms
            ),
            "target_bind_fallback_rms": (
                summary(target_only_bind_rms) if target_only_bind_rms else None
            ),
            "split_bones": {
                name: summary(values) for name, values in split_bones.items()
            },
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("session", type=Path)
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument(
        "--anim-model",
        help="External cinematic MDL that locally owns --clip.",
    )
    parser.add_argument(
        "--bone-root",
        help="Use only this named skeleton root and its descendants from --anim-model.",
    )
    parser.add_argument("--clip", default="howl")
    parser.add_argument("--alignment-threshold", type=float, default=0.05)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    report = validate(
        args.session.resolve(),
        args.model,
        args.clip,
        animation_model=args.anim_model,
        bone_root=args.bone_root,
        alignment_threshold=args.alignment_threshold,
    )
    rendered = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered, encoding="utf-8")
        print(args.report.resolve())
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
