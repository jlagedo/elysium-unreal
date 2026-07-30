"""Test how a VtMB cinematic skeleton is composed onto a live actor skeleton.

The courtroom cinematic MDLs contain multiple BipNN skeletons while the actors
render with their ordinary Bip01 models.  This analyzer joins a time-stamped
ELPOSE2 model extraction to an authored NPZ archive and compares plausible
local-space composition laws.  It is deliberately separate from the general
live-pose validator: the question here is not frame alignment, but whether the
cinematic channels are absolute or deltas from the cinematic bind pose.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import numpy as np

import install
import mdl_skel


ROOT = Path(__file__).resolve().parent
DEFAULT_SESSION = (
    ROOT
    / "out"
    / "_live_pose"
    / "courtroom_all_actors_retry_20260729_235845"
    / "vampire4_pose_changes"
)
DEFAULT_ARCHIVE = (
    ROOT / "out" / "_live_pose" / "courtroom_authored" / "courtroom_bip5.npz"
)
DEFAULT_MODEL = (
    "models/character/pc/female/ventrue/armor1/"
    "ventrue_female_Armor_1.mdl"
)


def rotation_from_quaternion(quaternions: np.ndarray) -> np.ndarray:
    quaternion = np.asarray(quaternions, dtype=np.float64)
    x, y, z, w = np.moveaxis(quaternion, -1, 0)
    matrix = np.empty(quaternion.shape[:-1] + (3, 3), dtype=np.float64)
    matrix[..., 0, 0] = 1 - 2 * (y * y + z * z)
    matrix[..., 0, 1] = 2 * (x * y - z * w)
    matrix[..., 0, 2] = 2 * (x * z + y * w)
    matrix[..., 1, 0] = 2 * (x * y + z * w)
    matrix[..., 1, 1] = 1 - 2 * (x * x + z * z)
    matrix[..., 1, 2] = 2 * (y * z - x * w)
    matrix[..., 2, 0] = 2 * (x * z - y * w)
    matrix[..., 2, 1] = 2 * (y * z + x * w)
    matrix[..., 2, 2] = 1 - 2 * (x * x + y * y)
    return matrix


def load_matrix(path: Path, bone_count: int) -> np.ndarray:
    compact = np.fromfile(path, dtype="<f4")
    if compact.size != bone_count * 12:
        raise ValueError(f"{path}: unexpected matrix payload size")
    compact = compact.reshape(bone_count, 3, 4)
    matrix = np.repeat(np.eye(4)[None, :, :], bone_count, axis=0)
    matrix[:, :3, :] = compact
    return matrix


def local_transforms(world: np.ndarray, parents: np.ndarray) -> np.ndarray:
    local = np.empty_like(world)
    for index, parent in enumerate(parents):
        if parent < 0:
            local[index] = world[index]
        else:
            local[index] = np.linalg.inv(world[int(parent)]) @ world[index]
    return local


def rms_by_frame(difference: np.ndarray) -> np.ndarray:
    axes = tuple(range(1, difference.ndim))
    return np.sqrt(np.mean(difference * difference, axis=axes))


def stats(values: np.ndarray) -> dict[str, float]:
    values = np.asarray(values, dtype=np.float64)
    return {
        "min": float(np.min(values)),
        "median": float(np.median(values)),
        "p90": float(np.percentile(values, 90)),
        "max": float(np.max(values)),
    }


def authored_frame_bins(
    authored_frames: np.ndarray, errors: np.ndarray, width: int = 300
) -> list[dict[str, object]]:
    bins = []
    first = int(authored_frames[0] // width * width)
    last = int(authored_frames[-1] // width * width)
    for begin in range(first, last + 1, width):
        selected = (authored_frames >= begin) & (authored_frames < begin + width)
        if np.any(selected):
            bins.append(
                {
                    "authored_frames": [begin, begin + width - 1],
                    "samples": int(np.count_nonzero(selected)),
                    "rms": stats(errors[selected]),
                }
            )
    return bins


def threshold_ranges(
    authored_frames: np.ndarray, errors: np.ndarray, threshold: float
) -> list[list[int]]:
    accepted = errors <= threshold
    ranges: list[list[int]] = []
    begin = None
    previous = None
    for frame, is_accepted in zip(authored_frames, accepted):
        frame = int(frame)
        if is_accepted:
            if begin is None:
                begin = frame
            previous = frame
        elif begin is not None:
            ranges.append([begin, int(previous)])
            begin = None
            previous = None
    if begin is not None:
        ranges.append([begin, int(previous)])
    return ranges


def nearest_rotation_frames(
    live: np.ndarray,
    predicted: np.ndarray,
    expected_frames: np.ndarray,
    radius: int,
) -> tuple[np.ndarray, np.ndarray]:
    nearest = np.empty(len(live), dtype=np.int32)
    errors = np.empty(len(live), dtype=np.float64)
    for live_index, expected in enumerate(expected_frames):
        begin = max(0, int(expected) - radius)
        end = min(len(predicted), int(expected) + radius + 1)
        difference = predicted[begin:end] - live[live_index]
        candidate_error = np.sqrt(
            np.mean(difference * difference, axis=(1, 2, 3))
        )
        candidate = int(np.argmin(candidate_error))
        nearest[live_index] = begin + candidate
        errors[live_index] = candidate_error[candidate]
    return nearest, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--session", type=Path, default=DEFAULT_SESSION)
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--source-root", default="Bip01")
    parser.add_argument(
        "--time-anchor",
        type=float,
        default=-20.879971,
        help="Capture seconds corresponding to authored frame zero.",
    )
    parser.add_argument("--fps", type=float, default=30.0)
    parser.add_argument(
        "--authored-end",
        type=int,
        default=4500,
        help="Exclusive frame bound used to omit the scene-end transition.",
    )
    parser.add_argument(
        "--nearest-radius",
        type=int,
        default=30,
        help="Search this many authored frames around the clock mapping.",
    )
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    session = args.session.resolve()
    manifest = json.loads((session / "manifest.json").read_text(encoding="utf-8"))
    index = install.build_index(verbose=False)
    model_data = install.read(index, args.model)
    if model_data is None:
        raise FileNotFoundError(args.model)
    target_bones = mdl_skel.read_bones(model_data)
    target_names = np.asarray([bone.name for bone in target_bones])
    target_parents = np.asarray([bone.parent for bone in target_bones])
    target_flags = np.asarray([bone.flags for bone in target_bones])

    with np.load(args.archive.resolve()) as authored:
        source_names = authored["bone_name"]
        source_parents = authored["parent"]
        source_flags = authored["flags"]
        source_position = authored["local_position"]
        source_quaternion = authored["local_quaternion"]
        source_bind_position = authored["bind_position"]
        source_bind_quaternion = authored["bind_quaternion"]

        roots = np.where(np.char.lower(source_names) == args.source_root.lower())[0]
        if len(roots) != 1:
            raise ValueError(f"expected one authored root named {args.source_root}")
        source_root = int(roots[0])
        source_descendants = {source_root}
        for bone_index in range(len(source_names)):
            parent = int(source_parents[bone_index])
            while parent >= 0:
                if parent == source_root:
                    source_descendants.add(bone_index)
                    break
                parent = int(source_parents[parent])

        source_by_folded_name = {}
        for bone_index in sorted(source_descendants):
            name = str(source_names[bone_index])
            folded = "bip01" + name[len(args.source_root) :]
            source_by_folded_name[folded.lower()] = bone_index

        shared_target = []
        shared_source = []
        for target_index, target_name in enumerate(target_names):
            source_index = source_by_folded_name.get(str(target_name).lower())
            if source_index is not None:
                shared_target.append(target_index)
                shared_source.append(source_index)
        shared_target = np.asarray(shared_target, dtype=np.int32)
        shared_source = np.asarray(shared_source, dtype=np.int32)

        ordinary = (
            (target_parents[shared_target] >= 0)
            & ((target_flags[shared_target] & 0x2) == 0)
            & ((source_flags[shared_source] & 0x2) == 0)
        )
        rotation_target = shared_target[ordinary]
        rotation_source = shared_source[ordinary]
        positioned = target_parents[shared_target] >= 0
        position_target = shared_target[positioned]
        position_source = shared_source[positioned]

        capture_seconds = np.asarray(
            [frame["seconds_after_arm"] for frame in manifest["frames"]]
        )
        authored_frame = np.rint(
            (capture_seconds - args.time_anchor) * args.fps
        ).astype(np.int32)
        selected = np.where(
            (np.arange(len(authored_frame)) > 0)
            & (authored_frame >= 0)
            & (authored_frame < min(args.authored_end, len(source_position)))
        )[0]
        if not len(selected):
            raise ValueError("time mapping selected no live frames")

        live_local = []
        for frame_index in selected:
            frame = manifest["frames"][int(frame_index)]
            world = load_matrix(
                session / frame["bone_to_world"], len(target_bones)
            )
            live_local.append(local_transforms(world, target_parents))
        live_local = np.stack(live_local)
        base_world = load_matrix(
            session / manifest["frames"][0]["bone_to_world"], len(target_bones)
        )
        base_local = local_transforms(base_world, target_parents)

        authored_selected = authored_frame[selected]
        live_rotation = live_local[:, rotation_target][:, :, :3, :3]
        live_position = live_local[:, position_target][:, :, :3, 3]
        animation_rotation = rotation_from_quaternion(
            source_quaternion[authored_selected[:, None], rotation_source[None, :]]
        )
        cinematic_bind_rotation = rotation_from_quaternion(
            source_bind_quaternion[rotation_source]
        )[None, ...]
        live_base_rotation = base_local[rotation_target, :3, :3][None, ...]
        target_bind_rotation = rotation_from_quaternion(
            np.asarray([target_bones[index].quat for index in rotation_target])
        )[None, ...]
        all_animation_rotation = rotation_from_quaternion(
            source_quaternion[: args.authored_end, rotation_source]
        )

        transpose_cinematic_bind = np.swapaxes(
            cinematic_bind_rotation, -1, -2
        )
        rotation_predictions = {
            "absolute_animation": animation_rotation,
            "live_base_times_inverse_cinematic_bind_times_animation": (
                live_base_rotation
                @ transpose_cinematic_bind
                @ animation_rotation
            ),
            "animation_times_inverse_cinematic_bind_times_live_base": (
                animation_rotation
                @ transpose_cinematic_bind
                @ live_base_rotation
            ),
            "live_base_times_animation_times_inverse_cinematic_bind": (
                live_base_rotation
                @ animation_rotation
                @ transpose_cinematic_bind
            ),
            "target_bind_times_inverse_cinematic_bind_times_animation": (
                target_bind_rotation
                @ transpose_cinematic_bind
                @ animation_rotation
            ),
            "animation_times_inverse_cinematic_bind_times_target_bind": (
                animation_rotation
                @ transpose_cinematic_bind
                @ target_bind_rotation
            ),
        }
        all_live_base_delta_rotation = (
            live_base_rotation
            @ transpose_cinematic_bind
            @ all_animation_rotation
        )
        rotation_results = {
            name: stats(rms_by_frame(live_rotation - predicted))
            for name, predicted in rotation_predictions.items()
        }

        animation_position = source_position[
            authored_selected[:, None], position_source[None, :]
        ].astype(np.float64)
        cinematic_bind_position = source_bind_position[position_source][None, ...]
        live_base_position = base_local[position_target, :3, 3][None, ...]
        target_bind_position = np.asarray(
            [target_bones[index].pos for index in position_target],
            dtype=np.float64,
        )[None, ...]
        position_predictions = {
            "absolute_animation": animation_position,
            "live_base_plus_animation_minus_cinematic_bind": (
                live_base_position + animation_position - cinematic_bind_position
            ),
            "target_bind_plus_animation_minus_cinematic_bind": (
                target_bind_position + animation_position - cinematic_bind_position
            ),
        }
        position_results = {
            name: stats(rms_by_frame(live_position - predicted))
            for name, predicted in position_predictions.items()
        }

        best_rotation_name = min(
            rotation_results,
            key=lambda name: rotation_results[name]["median"],
        )
        best_position_name = min(
            position_results,
            key=lambda name: position_results[name]["median"],
        )
        best_rotation_frame_error = rms_by_frame(
            live_rotation - rotation_predictions[best_rotation_name]
        )
        best_position_frame_error = rms_by_frame(
            live_position - position_predictions[best_position_name]
        )
        nearest_authored, nearest_rotation_error = nearest_rotation_frames(
            live_rotation,
            all_live_base_delta_rotation,
            authored_selected,
            args.nearest_radius,
        )
        nearest_offsets = nearest_authored - authored_selected
        unique_offsets, unique_offset_counts = np.unique(
            nearest_offsets, return_counts=True
        )
        best_rotation_error = np.sqrt(
            np.mean(
                (
                    live_rotation
                    - rotation_predictions[best_rotation_name]
                )
                ** 2,
                axis=(0, 2, 3),
            )
        )
        worst_rotation_order = np.argsort(best_rotation_error)[::-1][:12]

        report = {
            "version": 1,
            "capture": str(session),
            "archive": str(args.archive.resolve()),
            "target_model": args.model,
            "source_root": args.source_root,
            "time_mapping": {
                "anchor_seconds": args.time_anchor,
                "fps": args.fps,
                "selected_live_frames": int(len(selected)),
                "first_live_index": int(selected[0]),
                "last_live_index": int(selected[-1]),
                "first_authored_frame": int(authored_selected[0]),
                "last_authored_frame": int(authored_selected[-1]),
                "authored_end_exclusive": args.authored_end,
            },
            "bones": {
                "target": len(target_bones),
                "authored_skeleton": int(len(source_descendants)),
                "shared": int(len(shared_target)),
                "ordinary_rotation_compared": int(len(rotation_target)),
                "position_compared": int(len(position_target)),
            },
            "rotation_matrix_rms_by_live_frame": rotation_results,
            "position_rms_source_inches_by_live_frame": position_results,
            "best_rotation_law": best_rotation_name,
            "best_position_law": best_position_name,
            "best_rotation_diagnostics": {
                "authored_frame_bins": authored_frame_bins(
                    authored_selected, best_rotation_frame_error
                ),
                "sample_count_at_or_below_rms": {
                    f"{threshold:g}": int(
                        np.count_nonzero(best_rotation_frame_error <= threshold)
                    )
                    for threshold in (0.001, 0.005, 0.01, 0.05, 0.1)
                },
                "authored_frame_ranges_at_or_below_rms_0.01": threshold_ranges(
                    authored_selected, best_rotation_frame_error, 0.01
                ),
                "authored_frame_ranges_at_or_below_rms_0.05": threshold_ranges(
                    authored_selected, best_rotation_frame_error, 0.05
                ),
                "nearest_delta_pose_within_clock_window": {
                    "radius_frames": args.nearest_radius,
                    "rotation_matrix_rms": stats(nearest_rotation_error),
                    "authored_minus_clock_frame_offset": stats(nearest_offsets),
                    "offset_histogram": {
                        str(int(offset)): int(count)
                        for offset, count in zip(
                            unique_offsets, unique_offset_counts
                        )
                    },
                },
            },
            "best_position_diagnostics": {
                "authored_frame_bins": authored_frame_bins(
                    authored_selected, best_position_frame_error
                ),
                "sample_count_at_or_below_rms": {
                    f"{threshold:g}": int(
                        np.count_nonzero(best_position_frame_error <= threshold)
                    )
                    for threshold in (1e-5, 1e-4, 0.001, 0.01, 0.05)
                },
            },
            "worst_bones_under_best_rotation_law": [
                {
                    "bone": str(target_names[rotation_target[index]]),
                    "rms": float(best_rotation_error[index]),
                }
                for index in worst_rotation_order
            ],
        }

    rendered = json.dumps(report, indent=2) + "\n"
    if args.report:
        report_path = args.report.resolve()
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(rendered, encoding="utf-8")
        print(report_path)
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
