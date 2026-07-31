"""Inventory every raw animation identity resolved by installed player bodies.

The output is a disposable research index, not a public contract.  It preserves
the complete raw sequence and animation descriptors so unknown words remain
available when later retail captures expose why they matter.

Usage:
    uv run elysium research inventory_player_animations
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
from typing import Any

from elysium_pipeline.formats import kv
from elysium_pipeline.paths import research_root


CLANDOC_KEY = "vdata/system/clandoc000.txt"
BODY_SLOT = re.compile(r"^([mf])_body(\d+)$")
ANIMDESC_STRIDE = 72
SEQDESC_STRIDE = 764
BLEND_SIDE = 16


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _f32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def _vec3(data: bytes, offset: int) -> list[float]:
    return list(struct.unpack_from("<3f", data, offset))


def _cstr(data: bytes, offset: int) -> str:
    if not 0 <= offset < len(data):
        raise ValueError(f"string offset outside model: {offset}")
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError(f"unterminated model string at {offset}")
    return data[offset:end].decode("ascii", "replace")


def _cstr_rel(data: bytes, base: int, field_offset: int) -> str:
    relative = _i32(data, base + field_offset)
    return _cstr(data, base + relative) if relative else ""


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def normalize_model(value: str) -> str:
    model = value.strip().lower().replace("\\", "/")
    if not model.startswith("models/"):
        model = "models/" + model
    return model if model.endswith(".mdl") else model + ".mdl"


def player_slots(clandoc: bytes) -> list[dict[str, Any]]:
    """All indexed body references, including repeated templates and armour slots."""

    document = kv.parse(clandoc.decode("utf-8", errors="replace"))
    blocks = document.get("clandata", [])
    if isinstance(blocks, dict):
        blocks = [blocks]
    slots = []
    for block_index, block in enumerate(blocks):
        general = block.get("general", {})
        for key, value in general.items():
            match = BODY_SLOT.fullmatch(key)
            if not match or not isinstance(value, str) or not value.lower().endswith(".mdl"):
                continue
            slots.append(
                {
                    "clandoc_block": block_index,
                    "clan": str(general.get("clan", "")),
                    "body_key": key,
                    "sex": "male" if match.group(1) == "m" else "female",
                    "slot": int(match.group(2)),
                    "model": normalize_model(value),
                }
            )
    return slots


def _checked_array(
    data: bytes,
    label: str,
    count: int,
    offset: int,
    stride: int,
) -> None:
    if count < 0 or offset < 0 or offset + count * stride > len(data):
        raise ValueError(
            f"invalid {label} array: count={count} offset={offset} "
            f"stride={stride} size={len(data)}"
        )


def parse_owner(model: str, data: bytes) -> dict[str, Any]:
    """Decode only confirmed descriptor fields and preserve every descriptor byte."""

    if len(data) < 412 or data[:4] != b"IDST" or _i32(data, 4) != 2531:
        raise ValueError(f"{model}: not a VtMB v2531 MDL")
    bone_count = _i32(data, 240)
    animation_count, animation_base = _i32(data, 264), _i32(data, 268)
    sequence_count, sequence_base = _i32(data, 272), _i32(data, 276)
    _checked_array(
        data,
        "animation descriptor",
        animation_count,
        animation_base,
        ANIMDESC_STRIDE,
    )
    _checked_array(
        data,
        "sequence descriptor",
        sequence_count,
        sequence_base,
        SEQDESC_STRIDE,
    )

    animations = []
    for index in range(animation_count):
        base = animation_base + index * ANIMDESC_STRIDE
        raw = data[base : base + ANIMDESC_STRIDE]
        data_relative = _i32(data, base + 48)
        data_offset = base + data_relative
        bone_bytes = max(0, bone_count) * 32
        fixed_end = data_offset + bone_bytes
        fixed_records = (
            data[data_offset:fixed_end]
            if 0 <= data_offset <= fixed_end <= len(data)
            else b""
        )
        animations.append(
            {
                "identity": f"{model}#animation:{index}",
                "owner_model": model,
                "animation_index": index,
                "name": _cstr_rel(data, base, 0),
                "fps": _f32(data, base + 4),
                "flags": _i32(data, base + 8),
                "frames": _i32(data, base + 12),
                "movement_count": _i32(data, base + 16),
                "movement_offset_relative": _i32(data, base + 20),
                "bbox_min": _vec3(data, base + 24),
                "bbox_max": _vec3(data, base + 36),
                "animation_data_offset_relative": data_relative,
                "animation_data_offset": data_offset,
                "ik_rule_count": _i32(data, base + 52),
                "ik_rule_offset_relative": _i32(data, base + 56),
                "source_span": {"offset": base, "length": ANIMDESC_STRIDE},
                "descriptor_sha256": _sha256(raw),
                "descriptor_hex": raw.hex(),
                "unknown_trailing_hex": raw[60:].hex(),
                "fixed_bone_records": {
                    "offset": data_offset,
                    "length": bone_bytes,
                    "readable": len(fixed_records) == bone_bytes,
                    "sha256": _sha256(fixed_records) if fixed_records else None,
                },
            }
        )

    sequences = []
    for index in range(sequence_count):
        base = sequence_base + index * SEQDESC_STRIDE
        raw = data[base : base + SEQDESC_STRIDE]
        grid_flat = list(struct.unpack_from("<256h", data, base + 56))
        grid = [
            grid_flat[row * BLEND_SIDE : (row + 1) * BLEND_SIDE]
            for row in range(BLEND_SIDE)
        ]
        blend_count = _i32(data, base + 52)
        group_size = [_i32(data, base + 572), _i32(data, base + 576)]
        if (
            not all(1 <= value <= BLEND_SIDE for value in group_size)
            or group_size[0] * group_size[1] != blend_count
        ):
            raise ValueError(
                f"{model} sequence {index}: numblends={blend_count}, "
                f"groupsize={group_size}"
            )
        active_cells = []
        for row in range(group_size[0]):
            for column in range(group_size[1]):
                animation_index = grid[row][column]
                active_cells.append(
                    {
                        "row": row,
                        "column": column,
                        "animation_index": animation_index,
                        "animation_identity": (
                            f"{model}#animation:{animation_index}"
                            if 0 <= animation_index < animation_count
                            else None
                        ),
                    }
                )
        sequences.append(
            {
                "identity": f"{model}#sequence:{index}",
                "owner_model": model,
                "sequence_index": index,
                "label": _cstr_rel(data, base, 0),
                "activity_name": _cstr_rel(data, base, 4),
                "flags": _i32(data, base + 8),
                "activity_disk": _i32(data, base + 12),
                "activity_weight": _i32(data, base + 16),
                "event_count": _i32(data, base + 20),
                "event_offset_relative": _i32(data, base + 24),
                "bbox_min": _vec3(data, base + 28),
                "bbox_max": _vec3(data, base + 40),
                "blend_count": blend_count,
                "blend_group_size": group_size,
                "blend_parameter_index": [
                    _i32(data, base + 580),
                    _i32(data, base + 584),
                ],
                "blend_parameter_start": [
                    _f32(data, base + 588),
                    _f32(data, base + 592),
                ],
                "blend_parameter_end": [
                    _f32(data, base + 596),
                    _f32(data, base + 600),
                ],
                "blend_parameter_parent": _i32(data, base + 604),
                "blend_grid": grid,
                "active_blend_cells": active_cells,
                "source_span": {"offset": base, "length": SEQDESC_STRIDE},
                "descriptor_sha256": _sha256(raw),
                "descriptor_hex": raw.hex(),
                "unknown_tail_hex": raw[568:].hex(),
            }
        )

    return {
        "owner": {
            "owner_model": model,
            "model_sha256": _sha256(data),
            "model_size": len(data),
            "bone_count": bone_count,
            "sequence_count": sequence_count,
            "animation_count": animation_count,
        },
        "sequences": sequences,
        "animations": animations,
    }


def _write_jsonl(path: Path, records: list[dict[str, Any]]) -> dict[str, Any]:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        for record in records:
            stream.write(json.dumps(record, separators=(",", ":"), allow_nan=False))
            stream.write("\n")
    payload = path.read_bytes()
    return {
        "path": path.name,
        "records": len(records),
        "bytes": len(payload),
        "sha256": _sha256(payload),
    }


def _git_identity(repo_root: Path) -> dict[str, Any]:
    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    status = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=repo_root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    return {"commit": commit, "dirty": bool(status), "status": status}


def build_inventory(index: dict[str, Any]) -> dict[str, Any]:
    from elysium_pipeline.formats import install, mdl_skel

    clandoc = install.read(index, CLANDOC_KEY)
    if clandoc is None:
        raise FileNotFoundError(CLANDOC_KEY)
    slots = player_slots(clandoc)
    targets = sorted({slot["model"] for slot in slots})
    slot_by_target: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for slot in slots:
        slot_by_target[slot["model"]].append(slot)

    cache: dict[str, bytes | None] = {}

    def load(model: str) -> bytes | None:
        key = normalize_model(model)
        if key not in cache:
            cache[key] = install.read(index, key)
        return cache[key]

    owner_data: dict[str, bytes] = {}
    compatibility: dict[str, set[str]] = defaultdict(set)
    target_records = []
    missing = []
    for target in targets:
        order = []
        seen = set()

        def visit(model: str, parent: str | None) -> None:
            key = normalize_model(model)
            if key in seen:
                return
            seen.add(key)
            data = load(key)
            if data is None:
                missing.append(
                    {
                        "target_model": target,
                        "parent_model": parent,
                        "missing_model": key,
                    }
                )
                return
            owner_data[key] = data
            compatibility[key].add(target)
            order.append(key)
            for include in mdl_skel.read_includes(data):
                visit(include, key)

        visit(target, None)
        target_records.append(
            {
                "target_model": target,
                "clandoc_slots": slot_by_target[target],
                "resolved_owners": order,
                "missing_models": [
                    row for row in missing if row["target_model"] == target
                ],
            }
        )

    owners = []
    sequences = []
    animations = []
    for model in sorted(owner_data):
        parsed = parse_owner(model, owner_data[model])
        owner = parsed["owner"]
        owner["direct_includes"] = [
            normalize_model(value)
            for value in mdl_skel.read_includes(owner_data[model])
        ]
        owner["compatible_player_models"] = sorted(compatibility[model])
        owners.append(owner)
        sequences.extend(parsed["sequences"])
        animations.extend(parsed["animations"])

    labels = Counter(record["label"].casefold() for record in sequences)
    dimensions = Counter(tuple(record["blend_group_size"]) for record in sequences)
    referenced = {
        cell["animation_identity"]
        for record in sequences
        for cell in record["active_blend_cells"]
        if cell["animation_identity"] is not None
    }
    return {
        "clandoc": clandoc,
        "slots": slots,
        "targets": target_records,
        "owners": owners,
        "sequences": sequences,
        "animations": animations,
        "missing": missing,
        "summary": {
            "clandoc_body_references": len(slots),
            "clandoc_blocks_with_body_references": len(
                {slot["clandoc_block"] for slot in slots}
            ),
            "player_models": len(targets),
            "resolved_owner_models": len(owners),
            "sequence_descriptors": len(sequences),
            "animation_descriptors": len(animations),
            "expanded_target_sequence_reachability": sum(
                owner["sequence_count"]
                * len(owner["compatible_player_models"])
                for owner in owners
            ),
            "active_blend_cells": sum(
                len(record["active_blend_cells"]) for record in sequences
            ),
            "referenced_animation_descriptors": len(referenced),
            "unreferenced_animation_descriptors": (
                len(animations) - len(referenced)
            ),
            "distinct_casefolded_labels": len(labels),
            "duplicate_label_groups": sum(count > 1 for count in labels.values()),
            "multi_blend_sequences": sum(
                record["blend_count"] > 1 for record in sequences
            ),
            "blend_dimensions": {
                f"{rows}x{columns}": count
                for (rows, columns), count in sorted(dimensions.items())
            },
            "missing_models": len(missing),
        },
    }


def main() -> int:
    from elysium_pipeline.formats import install

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[3]
    stamp = time.strftime("%Y%m%d_%H%M%S")
    output = (
        args.output.resolve()
        if args.output
        else research_root()
        / "player-animation-inventory"
        / f"cap12_{stamp}_{os.getpid()}"
    )
    output.mkdir(parents=True, exist_ok=False)
    inventory = build_inventory(install.build_index(verbose=False))
    files = {}
    for name in ("targets", "owners", "sequences", "animations"):
        files[name] = _write_jsonl(output / f"{name}.jsonl", inventory[name])
    manifest = {
        "purpose": "disposable raw resolved player-animation research inventory",
        "tool": {
            "git": _git_identity(repo_root),
            "path": os.fspath(Path(__file__).resolve()),
            "sha256": _sha256(Path(__file__).read_bytes()),
        },
        "source": {
            "clandoc": CLANDOC_KEY,
            "clandoc_sha256": _sha256(inventory["clandoc"]),
        },
        "summary": inventory["summary"],
        "missing_models": inventory["missing"],
        "files": files,
    }
    manifest_path = output / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(json.dumps(inventory["summary"], indent=2))
    print(f"inventory={output}")
    print(f"manifest={manifest_path}")
    return 0 if not inventory["missing"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
