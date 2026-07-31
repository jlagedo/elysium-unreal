"""Compare retail base and final local poses captured from client.dll.

`capture_live_scene.py start --animation-model ...` records two checkpoints for
the selected actor model:

* BASE: `C_BaseAnimating::ResolveVirtualModelPose` has decoded/remapped one
  sequence into local position/quaternion arrays.
* FINL: the layered pose is complete and is about to enter
  `BuildTransformations`.

This analyzer joins those records to an external cinematic MDL.  The BASE
record carries retail's normalized sequence phase, so the authored sample is
`phase * (frames - 1)` and no wall-clock alignment or rendered-frame inference
is involved.  ELANIM2 also records the selected-bone bit list; array slots
outside that list are stale scratch data and must never enter a comparison.
With `--held-pose`, the same exact-phase records are compared against the
actor's first rendered local pose under the donor-bind composition candidates.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path

import numpy as np

from elysium_pipeline.formats import install, mdl_skel
from research.tooling.capture.generated_record_schemas import (
    ANIMATION_FILE_HEADER,
    ANIMATION_RECORD_HEADER,
)
from research.tooling.capture.analyze_cinematic_pose_composition import load_matrix, local_transforms


DEFAULT_MODEL = (
    "models/character/pc/female/ventrue/armor1/"
    "ventrue_female_Armor_1.mdl"
)
DEFAULT_ANIMATION_MODEL = (
    "models/cinematic/Santa_Monica/Courtroom/Courtroom_bip5.mdl"
)


@dataclass
class Record:
    stage: str
    ordinal: int
    qpc: int
    entity: int
    sequence: int
    sample_phase: float
    entity_cycle: float
    result: int
    positions_pointer: int
    quaternions_pointer: int
    selected: np.ndarray
    positions: np.ndarray
    quaternions: np.ndarray


def normalize_model_key(value: str) -> str:
    key = value.replace("\\", "/")
    if not key.lower().startswith("models/"):
        key = "models/" + key
    if not key.lower().endswith(".mdl"):
        key += ".mdl"
    return key


def rotation_from_quaternion(quaternions: np.ndarray) -> np.ndarray:
    quaternion = np.asarray(quaternions, dtype=np.float64)
    quaternion /= np.maximum(
        np.linalg.norm(quaternion, axis=-1, keepdims=True), 1.0e-30
    )
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


def slerp(left: np.ndarray, right: np.ndarray, alpha: float) -> np.ndarray:
    a = np.asarray(left, dtype=np.float64)
    b = np.asarray(right, dtype=np.float64).copy()
    dot = np.sum(a * b, axis=-1, keepdims=True)
    b = np.where(dot < 0.0, -b, b)
    dot = np.abs(dot)
    linear = dot > 0.9995
    theta = np.arccos(np.clip(dot, -1.0, 1.0))
    sine = np.sin(theta)
    safe_sine = np.where(np.abs(sine) > 1.0e-12, sine, 1.0)
    mixed = (
        np.sin((1.0 - alpha) * theta) / safe_sine * a
        + np.sin(alpha * theta) / safe_sine * b
    )
    mixed = np.where(linear, (1.0 - alpha) * a + alpha * b, mixed)
    mixed /= np.maximum(
        np.linalg.norm(mixed, axis=-1, keepdims=True), 1.0e-30
    )
    return mixed


def sample_authored(
    positions: np.ndarray, quaternions: np.ndarray, frame: float
) -> tuple[np.ndarray, np.ndarray]:
    frame = min(max(float(frame), 0.0), len(positions) - 1.0)
    lower = int(math.floor(frame))
    upper = min(lower + 1, len(positions) - 1)
    alpha = frame - lower
    position = (
        positions[lower].astype(np.float64) * (1.0 - alpha)
        + positions[upper].astype(np.float64) * alpha
    )
    quaternion = slerp(quaternions[lower], quaternions[upper], alpha)
    return position, quaternion


def rms(values: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.asarray(values, dtype=np.float64) ** 2)))


def stats(values: list[float]) -> dict[str, float] | None:
    if not values:
        return None
    array = np.asarray(values, dtype=np.float64)
    return {
        "min": float(np.min(array)),
        "median": float(np.median(array)),
        "p90": float(np.percentile(array, 90)),
        "max": float(np.max(array)),
    }


def read_records(
    trace: Path, checksum: int, bone_count: int
) -> tuple[dict[str, object], list[Record]]:
    records: list[Record] = []
    with trace.open("rb") as stream:
        raw = stream.read(ANIMATION_FILE_HEADER.size)
        if len(raw) != ANIMATION_FILE_HEADER.size:
            raise ValueError("animation trace has no complete header")
        fields = ANIMATION_FILE_HEADER.unpack(raw)
        animation_format = fields[0].rstrip(b"\0")
        version = fields[1]
        if (animation_format, version) not in {
            (b"ELANIM1", 1),
            (b"ELANIM2", 2),
        }:
            raise ValueError("unrecognized animation trace")
        if fields[2] != ANIMATION_FILE_HEADER.size:
            raise ValueError("unexpected animation trace header size")
        if fields[9] != checksum:
            raise ValueError(
                f"trace targets 0x{fields[9]:08x}, model is 0x{checksum:08x}"
            )
        header = {
            "qpc_frequency": fields[3],
            "start_qpc": fields[4],
            "pid": fields[5],
            "client_base": f"0x{fields[6]:08x}",
            "resolve_virtual_model_pose_rva": f"0x{fields[7]:x}",
            "build_transformations_rva": f"0x{fields[8]:x}",
            "target_checksum": f"0x{fields[9]:08x}",
            "format": animation_format.decode("ascii"),
            "version": version,
            "client_sha256": fields[10]
            .split(b"\0", 1)[0]
            .decode("ascii", "replace"),
        }
        while True:
            raw = stream.read(ANIMATION_RECORD_HEADER.size)
            if not raw:
                break
            if len(raw) != ANIMATION_RECORD_HEADER.size:
                raise ValueError("animation trace ends in a partial record header")
            values = ANIMATION_RECORD_HEADER.unpack(raw)
            (
                magic,
                record_bytes,
                ordinal,
                qpc,
                _thread,
                entity,
                _studio_hdr,
                record_checksum,
                record_bones,
                sequence,
                sample_phase,
                entity_cycle,
                result,
                positions_pointer,
                quaternions_pointer,
            ) = values
            selected_words = (record_bones + 31) // 32 if version >= 2 else 0
            expected = (
                ANIMATION_RECORD_HEADER.size
                + record_bones * 7 * 4
                + selected_words * 4
            )
            if record_bytes != expected:
                raise ValueError(
                    f"record {ordinal}: {record_bytes} bytes, expected {expected}"
                )
            payload = stream.read(record_bytes - ANIMATION_RECORD_HEADER.size)
            if len(payload) != record_bytes - ANIMATION_RECORD_HEADER.size:
                raise ValueError("animation trace ends in a partial record payload")
            if record_checksum != checksum or record_bones != bone_count:
                raise ValueError(f"record {ordinal}: target model changed")
            pose_bytes = record_bones * 7 * 4
            values_f32 = np.frombuffer(payload[:pose_bytes], dtype="<f4")
            split = record_bones * 3
            positions = values_f32[:split].reshape(record_bones, 3).copy()
            quaternions = values_f32[split:].reshape(record_bones, 4).copy()
            if version >= 2:
                words = np.frombuffer(
                    payload[pose_bytes:], dtype="<u4", count=selected_words
                )
                indices = np.arange(record_bones, dtype=np.uint32)
                selected = (
                    (words[indices >> 5] >> (indices & 31)) & 1
                ).astype(bool)
            else:
                selected = np.ones(record_bones, dtype=bool)
            records.append(
                Record(
                    stage=magic.decode("ascii"),
                    ordinal=ordinal,
                    qpc=qpc,
                    entity=entity,
                    sequence=sequence,
                    sample_phase=sample_phase,
                    entity_cycle=entity_cycle,
                    result=result,
                    positions_pointer=positions_pointer,
                    quaternions_pointer=quaternions_pointer,
                    selected=selected,
                    positions=positions,
                    quaternions=quaternions,
                )
            )
    return header, records


def descendants(bones, root_name: str) -> set[int]:
    roots = [
        bone.index
        for bone in bones
        if bone.name.lower() == root_name.lower()
    ]
    if len(roots) != 1:
        raise ValueError(
            f"animation model has {len(roots)} roots named {root_name!r}"
        )
    root = roots[0]
    result = {root}
    for bone in bones:
        parent = bone.parent
        while parent >= 0:
            if parent == root:
                result.add(bone.index)
                break
            parent = bones[parent].parent
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("session", type=Path)
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--anim-model", default=DEFAULT_ANIMATION_MODEL)
    parser.add_argument(
        "--archive",
        type=Path,
        help=(
            "Optional archive_courtroom_poses.py NPZ for the animation model; "
            "avoids decoding all authored frames again."
        ),
    )
    parser.add_argument("--bone-root", default="Bip01")
    parser.add_argument("--clip", default="entire_scene")
    parser.add_argument(
        "--held-pose",
        type=Path,
        help=(
            "Optional extract_live_scene_model.py pose-change directory. "
            "Its first rendered pose is the actor local held at scene entry."
        ),
    )
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    session = args.session.resolve()
    trace = session / "animation.elanim"
    index = install.build_index(verbose=False)
    model_key = normalize_model_key(args.model)
    model_data = install.read(index, model_key)
    if model_data is None:
        raise FileNotFoundError(model_key)
    checksum = int.from_bytes(model_data[8:12], "little")
    target_bones = mdl_skel.read_bones(model_data)
    header, records = read_records(trace, checksum, len(target_bones))

    animation_key = normalize_model_key(args.anim_model)
    animation_data = install.read(index, animation_key)
    if animation_data is None:
        raise FileNotFoundError(animation_key)
    source_bones = mdl_skel.read_bones(animation_data)
    sequence = next(
        (
            value
            for value in mdl_skel.local_sequences(animation_data)
            if value.label.lower() == args.clip.lower()
        ),
        None,
    )
    if sequence is None:
        raise ValueError(f"{animation_key}: no local clip {args.clip!r}")
    if args.archive:
        archive_path = args.archive.resolve()
        with np.load(archive_path) as archived:
            source_position = archived["local_position"].astype(
                np.float64
            )
            source_quaternion = archived["local_quaternion"].astype(
                np.float64
            )
            source_bind_position = archived["bind_position"].astype(
                np.float64
            )
            source_bind_quaternion = archived["bind_quaternion"].astype(
                np.float64
            )
            archived_names = archived["bone_name"]
        if source_position.shape != (
            sequence.frames,
            len(source_bones),
            3,
        ):
            raise ValueError(
                f"{archive_path}: position array does not match source model"
            )
        if source_quaternion.shape != (
            sequence.frames,
            len(source_bones),
            4,
        ):
            raise ValueError(
                f"{archive_path}: quaternion array does not match source model"
            )
        if list(archived_names) != [bone.name for bone in source_bones]:
            raise ValueError(
                f"{archive_path}: bone names do not match source model"
            )
    else:
        archive_path = None
        source_frames = mdl_skel.read_anim(
            animation_data, source_bones, sequence.base, sequence.frames
        )
        source_position = np.asarray(
            [[pose[0] for pose in frame] for frame in source_frames],
            dtype=np.float64,
        )
        source_quaternion = np.asarray(
            [[pose[1] for pose in frame] for frame in source_frames],
            dtype=np.float64,
        )
        source_bind_position = np.asarray(
            [bone.pos for bone in source_bones], dtype=np.float64
        )
        source_bind_quaternion = np.asarray(
            [bone.quat for bone in source_bones], dtype=np.float64
        )

    source_descendants = descendants(source_bones, args.bone_root)
    source_by_target_name: dict[str, int] = {}
    for source_index in sorted(source_descendants):
        name = source_bones[source_index].name
        folded = "Bip01" + name[len(args.bone_root) :]
        source_by_target_name[folded.lower()] = source_index
    shared_target = np.asarray(
        [
            bone.index
            for bone in target_bones
            if bone.name.lower() in source_by_target_name
        ],
        dtype=np.int32,
    )
    shared_source = np.asarray(
        [
            source_by_target_name[target_bones[index].name.lower()]
            for index in shared_target
        ],
        dtype=np.int32,
    )
    if not len(shared_target):
        raise ValueError("target and cinematic root share no bone names")

    held_pose_path = args.held_pose.resolve() if args.held_pose else None
    held_local = None
    if held_pose_path:
        held_manifest = json.loads(
            (held_pose_path / "manifest.json").read_text(encoding="utf-8")
        )
        if held_manifest["bone_count"] != len(target_bones):
            raise ValueError(f"{held_pose_path}: held pose bone count changed")
        held_frame = held_manifest["frames"][0]
        held_world = load_matrix(
            held_pose_path / held_frame["bone_to_world"], len(target_bones)
        )
        held_local = local_transforms(
            held_world,
            np.asarray([bone.parent for bone in target_bones], dtype=np.int32),
        )

    by_sequence: dict[int, dict[str, list[float]]] = defaultdict(
        lambda: defaultdict(list)
    )
    held_sums: dict[int, dict[str, list[float]]] = defaultdict(
        lambda: defaultdict(lambda: [0.0, 0.0])
    )
    samples: dict[int, list[list[float]]] = defaultdict(list)
    selected_counts: dict[int, list[int]] = defaultdict(list)
    last_base: dict[tuple[int, int], Record] = {}
    final_pairs = 0

    def pose_key(record: Record) -> tuple[int, int]:
        if record.positions_pointer:
            return record.entity, record.positions_pointer
        return record.entity, record.sequence

    for record in records:
        key = pose_key(record)
        if record.stage == "BASE":
            last_base[key] = record
            if not (0.0 <= record.sample_phase <= 1.0):
                continue
            selected_shared = record.selected[shared_target]
            selected_counts[record.sequence].append(
                int(np.count_nonzero(selected_shared))
            )
            if not np.any(selected_shared):
                continue
            selected_target = shared_target[selected_shared]
            selected_source = shared_source[selected_shared]
            source_frame = record.sample_phase * (sequence.frames - 1)
            authored_position, authored_quaternion = sample_authored(
                source_position[:, selected_source],
                source_quaternion[:, selected_source],
                source_frame,
            )
            live_rotation = rotation_from_quaternion(
                record.quaternions[selected_target]
            )
            authored_rotation = rotation_from_quaternion(authored_quaternion)
            by_sequence[record.sequence]["base_source_rotation"].append(
                rms(live_rotation - authored_rotation)
            )
            by_sequence[record.sequence]["base_source_position"].append(
                rms(
                    record.positions[selected_target].astype(np.float64)
                    - authored_position
                )
            )
            if held_local is not None:
                ordinary = np.asarray(
                    [
                        target_bones[target].parent >= 0
                        and not (target_bones[target].flags & 0x2)
                        and not (source_bones[source].flags & 0x2)
                        for target, source in zip(selected_target, selected_source)
                    ],
                    dtype=bool,
                )
                if np.any(ordinary):
                    ordinary_target = selected_target[ordinary]
                    ordinary_source = selected_source[ordinary]
                    authored_position_ordinary = authored_position[ordinary]
                    authored_rotation_ordinary = authored_rotation[ordinary]
                    live_position_ordinary = record.positions[
                        ordinary_target
                    ].astype(np.float64)
                    live_rotation_ordinary = live_rotation[ordinary]
                    held_position = held_local[
                        ordinary_target, :3, 3
                    ]
                    held_rotation = held_local[
                        ordinary_target, :3, :3
                    ]
                    bind_position = source_bind_position[ordinary_source]
                    bind_rotation = rotation_from_quaternion(
                        source_bind_quaternion[ordinary_source]
                    )
                    inverse_bind_rotation = np.swapaxes(
                        bind_rotation, -1, -2
                    )
                    predictions = {
                        "held_times_inverse_bind_times_authored": (
                            held_rotation
                            @ inverse_bind_rotation
                            @ authored_rotation_ordinary
                        ),
                        "authored_times_inverse_bind_times_held": (
                            authored_rotation_ordinary
                            @ inverse_bind_rotation
                            @ held_rotation
                        ),
                        "held_times_authored_times_inverse_bind": (
                            held_rotation
                            @ authored_rotation_ordinary
                            @ inverse_bind_rotation
                        ),
                    }
                    for law, predicted in predictions.items():
                        difference = live_rotation_ordinary - predicted
                        held_sums[record.sequence][law][0] += float(
                            np.sum(difference * difference)
                        )
                        held_sums[record.sequence][law][1] += difference.size
                    position_difference = live_position_ordinary - (
                        held_position
                        + authored_position_ordinary
                        - bind_position
                    )
                    held_sums[record.sequence][
                        "held_plus_authored_minus_bind_position"
                    ][0] += float(
                        np.sum(position_difference * position_difference)
                    )
                    held_sums[record.sequence][
                        "held_plus_authored_minus_bind_position"
                    ][1] += position_difference.size
            samples[record.sequence].append(
                [record.entity_cycle, record.sample_phase]
            )
            continue

        base = last_base.get(key)
        if base is None or base.sequence != record.sequence:
            continue
        selected_shared = (
            base.selected[shared_target] & record.selected[shared_target]
        )
        if not np.any(selected_shared):
            continue
        selected_target = shared_target[selected_shared]
        final_pairs += 1
        base_rotation = rotation_from_quaternion(
            base.quaternions[selected_target]
        )
        final_rotation = rotation_from_quaternion(
            record.quaternions[selected_target]
        )
        by_sequence[record.sequence]["final_base_rotation"].append(
            rms(final_rotation - base_rotation)
        )
        by_sequence[record.sequence]["final_base_position"].append(
            rms(
                record.positions[selected_target].astype(np.float64)
                - base.positions[selected_target].astype(np.float64)
            )
        )

    sequence_rows = []
    for studio_sequence, metrics in by_sequence.items():
        sample_array = np.asarray(samples.get(studio_sequence, []))
        cycle_fit = None
        if len(sample_array) >= 2 and np.ptp(sample_array[:, 0]) > 1.0e-8:
            slope, intercept = np.polyfit(
                sample_array[:, 0], sample_array[:, 1], 1
            )
            residual = sample_array[:, 1] - (
                sample_array[:, 0] * slope + intercept
            )
            cycle_fit = {
                "resolver_phase_equals_entity_cycle_times": float(slope),
                "plus": float(intercept),
                "rms_phase": rms(residual),
            }
        sequence_rows.append(
            {
                "studio_sequence": studio_sequence,
                "base_records_in_clip_phase_range": len(
                    metrics["base_source_rotation"]
                ),
                "selected_shared_bones_per_base_record": stats(
                    selected_counts.get(studio_sequence, [])
                ),
                "base_source_rotation_matrix_rms": stats(
                    metrics["base_source_rotation"]
                ),
                "base_source_position_rms_source_inches": stats(
                    metrics["base_source_position"]
                ),
                "final_base_rotation_matrix_rms": stats(
                    metrics["final_base_rotation"]
                ),
                "final_base_position_rms_source_inches": stats(
                    metrics["final_base_position"]
                ),
                "entity_cycle_to_resolver_phase_fit": cycle_fit,
                "held_scene_entry_composition": {
                    law: math.sqrt(total / count)
                    for law, (total, count) in held_sums.get(
                        studio_sequence, {}
                    ).items()
                    if count
                } or None,
            }
        )
    sequence_rows.sort(
        key=lambda row: (
            (
                row["base_source_rotation_matrix_rms"]["median"]
                if row["base_source_rotation_matrix_rms"]
                else float("inf")
            ),
            -row["base_records_in_clip_phase_range"],
        )
    )

    report = {
        "version": 2,
        "capture": str(trace),
        "capture_sha256": hashlib.sha256(trace.read_bytes()).hexdigest(),
        "capture_header": header,
        "target_model": model_key,
        "target_checksum": f"0x{checksum:08x}",
        "animation_model": animation_key,
        "animation_archive": str(archive_path) if archive_path else None,
        "held_pose": str(held_pose_path) if held_pose_path else None,
        "clip": {
            "label": sequence.label,
            "frames": sequence.frames,
            "fps": sequence.fps,
            "bone_root": args.bone_root,
            "source_frame_from_phase": "phase * (frames - 1)",
        },
        "bones": {
            "target": len(target_bones),
            "source": len(source_bones),
            "shared": int(len(shared_target)),
        },
        "records": {
            "total": len(records),
            "base": sum(record.stage == "BASE" for record in records),
            "final": sum(record.stage == "FINL" for record in records),
            "entities": [
                f"0x{entity:08x}"
                for entity in sorted({record.entity for record in records})
            ],
            "final_base_pairs": final_pairs,
            "selected_bone_masks_recorded": header["version"] >= 2,
        },
        "limitations": (
            []
            if header["version"] >= 2
            else [
                "ELANIM1 does not record selected-bone masks or pose-buffer "
                "identities; resolver calls cannot be paired conclusively "
                "with the final pose."
            ]
        ),
        "best_cinematic_sequence_candidate": (
            sequence_rows[0]["studio_sequence"] if sequence_rows else None
        ),
        "studio_sequences": sequence_rows,
    }
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
