"""Compare two captured live VtMB skeletal-palette sessions.

Raw bone-to-world matrices include the player's entity transform.  This tool
also compares every bone relative to bone 0, which removes spawn translation
and orientation and exposes repeatability of the evaluated skeletal pose.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
from typing import Iterable


DEFAULT_THRESHOLDS = (1.0e-5, 1.0e-4, 1.0e-3, 1.0e-2, 5.0e-2)


def load_manifest(session: Path) -> dict[str, object]:
    manifest_path = session / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("matrix_layout") != (
        "row-major matrix3x4, 12 little-endian float32"
    ):
        raise ValueError(f"{manifest_path}: unsupported matrix layout")
    if manifest.get("captured_frames") != len(manifest.get("frames", [])):
        raise ValueError(f"{manifest_path}: captured frame count is inconsistent")
    return manifest


def load_palette_frames(
    session: Path, manifest: dict[str, object], field: str
) -> list[list[tuple[float, ...]]]:
    bone_count = int(manifest["bone_count"])
    byte_count = bone_count * 12 * 4
    result: list[list[tuple[float, ...]]] = []
    for frame in manifest["frames"]:
        path = session / frame[field]
        payload = path.read_bytes()
        if len(payload) != byte_count:
            raise ValueError(
                f"{path}: expected {byte_count} bytes, got {len(payload)}"
            )
        values = struct.unpack(f"<{bone_count * 12}f", payload)
        result.append(
            [values[offset : offset + 12] for offset in range(0, len(values), 12)]
        )
    return result


def rotation(matrix: tuple[float, ...]) -> tuple[float, ...]:
    return (
        matrix[0],
        matrix[1],
        matrix[2],
        matrix[4],
        matrix[5],
        matrix[6],
        matrix[8],
        matrix[9],
        matrix[10],
    )


def translation(matrix: tuple[float, ...]) -> tuple[float, float, float]:
    return matrix[3], matrix[7], matrix[11]


def transpose_multiply(
    left: tuple[float, ...], right: tuple[float, ...]
) -> tuple[float, ...]:
    return tuple(
        sum(left[k * 3 + row] * right[k * 3 + column] for k in range(3))
        for row in range(3)
        for column in range(3)
    )


def root_relative_descriptor(
    frame: list[tuple[float, ...]],
) -> tuple[float, ...]:
    root_rotation = rotation(frame[0])
    root_translation = translation(frame[0])
    values: list[float] = []
    for matrix in frame:
        bone_rotation = rotation(matrix)
        bone_translation = translation(matrix)
        delta = tuple(
            bone_translation[axis] - root_translation[axis] for axis in range(3)
        )
        relative_translation = tuple(
            sum(root_rotation[k * 3 + axis] * delta[k] for k in range(3))
            for axis in range(3)
        )
        values.extend(transpose_multiply(root_rotation, bone_rotation))
        values.extend(relative_translation)
    return tuple(values)


def rms(left: Iterable[float], right: Iterable[float]) -> float:
    differences = [(a - b) ** 2 for a, b in zip(left, right)]
    return math.sqrt(sum(differences) / len(differences))


def median(values: list[float]) -> float:
    ordered = sorted(values)
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) * 0.5


def monotonic_matches(
    distances: list[list[float]], threshold: float
) -> list[tuple[int, int]]:
    rows = len(distances)
    columns = len(distances[0]) if rows else 0
    counts = [[0] * (columns + 1) for _ in range(rows + 1)]
    for row in range(1, rows + 1):
        for column in range(1, columns + 1):
            if distances[row - 1][column - 1] <= threshold:
                counts[row][column] = counts[row - 1][column - 1] + 1
            else:
                counts[row][column] = max(
                    counts[row - 1][column], counts[row][column - 1]
                )

    pairs: list[tuple[int, int]] = []
    row, column = rows, columns
    while row and column:
        if (
            distances[row - 1][column - 1] <= threshold
            and counts[row][column] == counts[row - 1][column - 1] + 1
        ):
            pairs.append((row - 1, column - 1))
            row -= 1
            column -= 1
        elif counts[row - 1][column] >= counts[row][column - 1]:
            row -= 1
        else:
            column -= 1
    pairs.reverse()
    return pairs


def raw_hash_summary(
    manifest_a: dict[str, object], manifest_b: dict[str, object], field: str
) -> dict[str, int]:
    hashes_a = [frame[field] for frame in manifest_a["frames"]]
    hashes_b = [frame[field] for frame in manifest_b["frames"]]
    return {
        "same_index": sum(a == b for a, b in zip(hashes_a, hashes_b)),
        "common_at_any_index": len(set(hashes_a) & set(hashes_b)),
    }


def compare(session_a: Path, session_b: Path) -> dict[str, object]:
    manifest_a = load_manifest(session_a)
    manifest_b = load_manifest(session_b)
    for field in ("model", "model_checksum", "bone_count", "matrix_layout"):
        if manifest_a[field] != manifest_b[field]:
            raise ValueError(
                f"manifest mismatch for {field}: "
                f"{manifest_a[field]!r} != {manifest_b[field]!r}"
            )

    matrices_a = load_palette_frames(session_a, manifest_a, "bone_to_world")
    matrices_b = load_palette_frames(session_b, manifest_b, "bone_to_world")
    descriptors_a = [root_relative_descriptor(frame) for frame in matrices_a]
    descriptors_b = [root_relative_descriptor(frame) for frame in matrices_b]
    distances = [
        [rms(descriptor_a, descriptor_b) for descriptor_b in descriptors_b]
        for descriptor_a in descriptors_a
    ]
    nearest_a = [min(row) for row in distances]

    threshold_results: list[dict[str, object]] = []
    for threshold in DEFAULT_THRESHOLDS:
        pairs = monotonic_matches(distances, threshold)
        threshold_results.append(
            {
                "rms_threshold": threshold,
                "monotonic_matches": len(pairs),
                "pairs": [[a, b] for a, b in pairs],
            }
        )

    root_a = translation(matrices_a[0][0])
    root_b = translation(matrices_b[0][0])
    return {
        "version": 1,
        "session_a": str(session_a.resolve()),
        "session_b": str(session_b.resolve()),
        "model": manifest_a["model"],
        "bone_count": manifest_a["bone_count"],
        "frames_a": len(matrices_a),
        "frames_b": len(matrices_b),
        "capture_span_seconds_a": manifest_a["frames"][-1]["seconds_after_arm"],
        "capture_span_seconds_b": manifest_b["frames"][-1]["seconds_after_arm"],
        "raw_hashes": {
            field: raw_hash_summary(manifest_a, manifest_b, field)
            for field in (
                "bone_to_world_sha256",
                "skin_palette_sha256",
                "combined_sha256",
            )
        },
        "first_root_translation": {
            "a": list(root_a),
            "b": list(root_b),
            "a_minus_b": [a - b for a, b in zip(root_a, root_b)],
        },
        "root_relative_rms": {
            "nearest_a_to_b_min": min(nearest_a),
            "nearest_a_to_b_median": median(nearest_a),
            "nearest_a_to_b_max": max(nearest_a),
            "thresholds": threshold_results,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("session_a", type=Path)
    parser.add_argument("session_b", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    report = compare(args.session_a.resolve(), args.session_b.resolve())
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
