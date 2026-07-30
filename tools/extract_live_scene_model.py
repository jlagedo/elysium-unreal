"""Extract one model's consecutive pose changes from an ELPOSE2 scene trace."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from capture_live_scene import FILE_HEADER, POSE_HEADER


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(8 * 1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def parse_u32(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("session", type=Path)
    parser.add_argument("--checksum", type=parse_u32, required=True)
    parser.add_argument("--entity", type=parse_u32)
    parser.add_argument("--model-key", required=True)
    parser.add_argument("--label", required=True)
    args = parser.parse_args()

    source_session = args.session.resolve()
    trace = source_session / "scene.elpose"
    output = source_session / args.label
    output.mkdir(parents=False, exist_ok=False)

    frames: list[dict[str, object]] = []
    payloads: list[tuple[bytes, bytes]] = []
    raw_draws = 0
    entities: set[int] = set()
    last_hash: str | None = None
    model_name: str | None = None
    bone_count: int | None = None

    with trace.open("rb") as stream:
        file_values = FILE_HEADER.unpack(stream.read(FILE_HEADER.size))
        frequency = file_values[3]
        start_qpc = file_values[4]
        while True:
            raw_header = stream.read(POSE_HEADER.size)
            if not raw_header:
                break
            if len(raw_header) != POSE_HEADER.size:
                raise ValueError("trace ends in a partial pose header")
            values = POSE_HEADER.unpack(raw_header)
            record_bytes = values[1]
            qpc = values[3]
            client_entity = values[6]
            checksum = values[7]
            record_bones = values[8]
            payload_bytes = record_bytes - POSE_HEADER.size
            payload = stream.read(payload_bytes)
            if len(payload) != payload_bytes:
                raise ValueError("trace ends in a partial pose payload")
            if checksum != args.checksum:
                continue
            entities.add(client_entity)
            if args.entity is not None and client_entity != args.entity:
                continue

            raw_draws += 1
            if bone_count is None:
                bone_count = record_bones
                model_name = values[-1].split(b"\0", 1)[0].decode(
                    "ascii", "replace"
                )
            elif bone_count != record_bones:
                raise ValueError("one checksum produced multiple bone counts")
            matrix_bytes = record_bones * 12 * 4
            bones = payload[:matrix_bytes]
            skin = payload[matrix_bytes:]
            combined_hash = hashlib.sha256(bones + skin).hexdigest()
            if combined_hash == last_hash:
                continue

            index = len(frames)
            stem = f"frame_{index:04d}"
            bone_file = f"{stem}_bone_to_world.bin"
            skin_file = f"{stem}_skin_palette.bin"
            frames.append(
                {
                    "index": index,
                    "qpc": qpc,
                    "seconds_after_arm": (qpc - start_qpc) / frequency,
                    "client_entity": f"0x{client_entity:08x}",
                    "bone_to_world": bone_file,
                    "skin_palette": skin_file,
                    "bone_to_world_sha256": hashlib.sha256(bones).hexdigest(),
                    "skin_palette_sha256": hashlib.sha256(skin).hexdigest(),
                    "combined_sha256": combined_hash,
                }
            )
            payloads.append((bones, skin))
            last_hash = combined_hash

    if not frames or bone_count is None or model_name is None:
        raise ValueError("no matching pose records")
    if args.entity is None and len(entities) != 1:
        rendered = ", ".join(f"0x{entity:08x}" for entity in sorted(entities))
        raise ValueError(
            f"checksum has {len(entities)} client entities ({rendered}); "
            "select one with --entity"
        )

    for frame, (bones, skin) in zip(frames, payloads):
        (output / frame["bone_to_world"]).write_bytes(bones)
        (output / frame["skin_palette"]).write_bytes(skin)
    manifest = {
        "version": 1,
        "method": "ELPOSE2 consecutive pose-change extraction",
        "source_trace": str(trace),
        "source_trace_sha256": file_sha256(trace),
        "model": model_name,
        "model_key": args.model_key,
        "model_checksum": f"0x{args.checksum:08x}",
        "bone_count": bone_count,
        "matrix_layout": "row-major matrix3x4, 12 little-endian float32",
        "matrix_bytes_per_palette": bone_count * 12 * 4,
        "raw_draw_records": raw_draws,
        "requested_frames": len(frames),
        "captured_frames": len(frames),
        "complete": True,
        "frames": frames,
    }
    manifest_path = output / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(output)
    print(
        f"raw_draws={raw_draws} consecutive_pose_changes={len(frames)} "
        f"span={frames[-1]['seconds_after_arm'] - frames[0]['seconds_after_arm']:.6f}s"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
