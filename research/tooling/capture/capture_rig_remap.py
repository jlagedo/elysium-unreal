"""Read the runtime bone remap every include-model group resolved, out of process.

A rig-resolution capture names which bank owns a clip. It does not say how that
bank's bones reach the body's, and that correspondence is what a remake's rig
has to agree with. `FUN_10089c40` reads it from a table at group `+0x10`, 56
bytes per bone of the *including* model, and the shipped files carry that table
allocated and empty -- every record is `-1` on disk, beside the `0x7fffffff`
sequence-base sentinel at group `+8`. The engine fills both at model load, so
the correspondence exists only in a running process and no offline decode can
recover it.

This reads it with ReadProcessMemory against a live game, the same non-invasive
method as `capture_live_pose`. It never attaches a debugger, suspends a thread,
or writes to the game process. The studio-header addresses come from a
`life_rig_resolution` session: `client.resolve_sequence_owner` carries the
header pointer at every level of its recursion, so a session that reached a
bank six includes deep names the loaded address of all six.

Per record, read as shorts: `src`@0 is the bone index in the included bank, and
`-1` means the bank does not drive this bone, in which case the engine takes the
including model's own reference pose. `mode`@2 selects the copy path over the
chain-rebase path, and `sub`@3 whether the position is copied or transformed.
`chain_start`@4 and `chain_end`@6 bound the rebase walk. `+8` is a row-major
`matrix3x4` -- 12 floats, which is the whole rest of the record -- applied to the
bone position as an affine point transform by `FUN_10107f80`.

Usage:
    uv run elysium research capture_rig_remap --pid 1234
    uv run elysium research capture_rig_remap --pid 1234 --session <directory>
"""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import struct
import sys
from typing import Any

from elysium_pipeline.formats import install, mdl_skel
from elysium_pipeline.paths import research_root
from research.tooling.capture.capture_live_pose import ProcessReader


RECIPES = ("life_rig_resolution", "life_rig_chaos")
RECIPE = RECIPES[0]

#: `StudioModelGroup` (`docs/vtmb/mdl_v2531.md`) and the two header fields that
#: address the array.
GROUP_STRIDE = 116
GROUP_FILENAME = 0x00
#: Filled at load beside the remap: the global sequence range this group owns.
#: On disk `seq_base` is the `0x7fffffff` unresolved sentinel and `seq_count` 0.
GROUP_SEQ_BASE = 0x08
GROUP_SEQ_COUNT = 0x0C
GROUP_BONE_REMAP = 0x10
GROUP_POSE_REMAP = 0x44
POSE_REMAP_SLOTS = 24

#: One remap record per bone of the including model: four shorts of indexing
#: followed by a row-major `matrix3x4`, which accounts for the stride exactly.
REMAP_STRIDE = 56
REMAP_MATRIX = 8
REMAP_MATRIX_FLOATS = 12

HEADER_NUM_BONES = 240
HEADER_NUM_INCLUDES = 404
HEADER_INCLUDE_INDEX = 408
HEADER_NAME = 12
HEADER_NAME_BYTES = 128
HEADER_LENGTH = 140

#: A header this large is a misread rather than a model; the largest installed
#: file is under 7 MB.
MAXIMUM_IMAGE = 8 * 1024 * 1024


def _i16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<h", data, offset)[0]


def _i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def _f32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<f", data, offset)[0]


def _cstring(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    end = len(data) if end < 0 else end
    return data[offset:end].decode("ascii", "replace")


def install_key(model_name: str) -> str:
    key = model_name.replace("\\", "/").lower().lstrip("/")
    return key if key.startswith("models/") else "models/" + key


def latest_session() -> Path:
    root = research_root() / "frida"
    candidates = sorted(
        (
            path
            for recipe in RECIPES
            for path in root.glob(f"*-{recipe}")
            if (path / "events.jsonl").is_file()
        ),
        key=lambda path: path.name,
    )
    if not candidates:
        raise FileNotFoundError(
            f"no {RECIPE} capture exists below {root}; run "
            f"`uv run elysium research frida_probe attach --recipe {RECIPE}` first"
        )
    return candidates[-1]


def observed_headers(session: Path) -> dict[int, str]:
    """Every loaded studio-header address the session named, with its model.

    `resolve_sequence_owner` carries the header as its first argument at every
    recursion level, which is what reaches a nested bank; the two model-pointer
    hooks answer only the entity's own top-level model.
    """
    headers: dict[int, str] = {}
    events_path = session / "events.jsonl"
    returns: dict[tuple[str, int], dict[str, Any]] = {}
    calls: list[dict[str, Any]] = []
    for line in events_path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        event = json.loads(line)
        if event.get("kind") == "call":
            calls.append(event)
        elif event.get("kind") == "return":
            returns[(event["target"], event["sequence"])] = event
    for call in calls:
        target = call["target"]
        fields = call.get("fields") or {}
        if target in ("client.resolve_sequence_owner", "client.accumulate_sequence_pose"):
            words = call.get("stack_words") or []
            model = fields.get("model")
            if len(words) > 1 and words[1] and isinstance(model, str) and model:
                headers.setdefault(int(str(words[1]), 16), model)
        elif target in ("vampire.get_model_ptr", "client.get_studio_hdr"):
            answer = returns.get((target, call["sequence"]))
            if answer is None:
                continue
            model = ((answer.get("fields") or {}).get("model"))
            address = int(str(answer["return_value"]), 16)
            if address and isinstance(model, str) and model:
                headers.setdefault(address, model)
    return headers


class BoneNames:
    """Bone names per installed model, read once each."""

    def __init__(self) -> None:
        self._index = install.build_index(dirs=("models",), verbose=False)
        self._cache: dict[str, list[str]] = {}
        self.missing: set[str] = set()

    def names(self, model: str) -> list[str]:
        key = install_key(model)
        cached = self._cache.get(key)
        if cached is not None:
            return cached
        data = install.read(self._index, key)
        if data is None:
            self.missing.add(key)
            names: list[str] = []
        else:
            names = [bone.name for bone in mdl_skel.read_bones(data)]
        self._cache[key] = names
        return names


def read_image(reader: ProcessReader, address: int, model: str) -> bytes | None:
    """The loaded model image, verified to be the header the capture named.

    A stale address from an earlier session would otherwise be decoded as a
    model and produce an invented remap.
    """
    try:
        head = reader.read(address, HEADER_LENGTH + 4)
    except OSError:
        return None
    if head[0:4] != b"IDST" or _i32(head, 4) != 2531:
        return None
    if _cstring(head, HEADER_NAME).casefold() != model.casefold():
        return None
    length = _i32(head, HEADER_LENGTH)
    if not 0 < length <= MAXIMUM_IMAGE:
        return None
    try:
        return reader.read(address, length)
    except OSError:
        return None


def remap_rows(image: bytes, model: str, bones: BoneNames):
    """One row per (group, bone of the including model)."""
    own = bones.names(model)
    bone_count = _i32(image, HEADER_NUM_BONES)
    groups = _i32(image, HEADER_NUM_INCLUDES)
    group_base = _i32(image, HEADER_INCLUDE_INDEX)
    rows = []
    summaries = []
    for group in range(groups):
        base = group_base + group * GROUP_STRIDE
        bank = _cstring(image, base + _i32(image, base + GROUP_FILENAME))
        bank_bones = bones.names(bank)
        # The OFFSET is what has to be positive, not the address it produces. `base` is always
        # positive, so `table <= 0` can never fail for a group whose remap offset reads 0 -- and a
        # zero offset points the walk at the group record itself, which decodes as 56-byte remap
        # records whose first field is the low half of the filename index: small, positive, and
        # counted as a mapped bone. The group then reports `readable` with fabricated rows.
        remap_offset = _i32(image, base + GROUP_BONE_REMAP)
        table = base + remap_offset
        if remap_offset <= 0 or table + REMAP_STRIDE * bone_count > len(image):
            print(
                f"WARNING - {model} group {group} ({bank}) states a bone remap offset of "
                f"{remap_offset}, which is unset or points outside the loaded image; the group "
                f"is reported unread rather than decoded",
                file=sys.stderr,
            )
            summaries.append(
                {
                    "including_model": model,
                    "group": group,
                    "bank": bank,
                    "readable": False,
                }
            )
            continue
        mapped = 0
        name_agreements = 0
        name_conflicts = []
        modes: dict[str, int] = {}
        for bone in range(bone_count):
            record = table + bone * REMAP_STRIDE
            source = _i16(image, record + 0)
            mode = image[record + 2]
            sub = image[record + 3]
            source_name = (
                bank_bones[source] if 0 <= source < len(bank_bones) else None
            )
            own_name = own[bone] if bone < len(own) else None
            if source >= 0:
                mapped += 1
                if source_name is not None and own_name is not None:
                    if source_name.casefold() == own_name.casefold():
                        name_agreements += 1
                    else:
                        name_conflicts.append(
                            {
                                "bone": bone,
                                "body_bone": own_name,
                                "bank_bone": source_name,
                            }
                        )
            modes[f"{mode}/{sub}"] = modes.get(f"{mode}/{sub}", 0) + 1
            rows.append(
                {
                    "including_model": model,
                    "group": group,
                    "bank": bank,
                    "body_bone_index": bone,
                    "body_bone": own_name,
                    "bank_bone_index": source,
                    "bank_bone": source_name,
                    "mode": mode,
                    "sub": sub,
                    "chain_start": _i16(image, record + 4),
                    "chain_end": _i16(image, record + 6),
                    "matrix3x4": ";".join(
                        f"{_f32(image, record + REMAP_MATRIX + step * 4):.7g}"
                        for step in range(REMAP_MATRIX_FLOATS)
                    ),
                }
            )
        pose = [
            _i16(image, base + GROUP_POSE_REMAP + slot * 2)
            for slot in range(POSE_REMAP_SLOTS)
        ]
        summaries.append(
            {
                "including_model": model,
                "group": group,
                "bank": bank,
                "readable": True,
                "body_bones": bone_count,
                "bank_bones": len(bank_bones),
                "bones_mapped": mapped,
                "bones_unmapped": bone_count - mapped,
                "resolved": mapped > 0,
                "name_agreements": name_agreements,
                "name_conflicts": name_conflicts,
                "modes": modes,
                "pose_parameter_remap": pose,
            }
        )
    return rows, summaries


def sequence_map(images: dict[str, bytes], model: str, limit: int = 65536):
    """Every global sequence number a body answers, resolved to its owning bank.

    This is the same walk `FUN_10089c40` performs -- an index below the model's
    own `NumLocalSeq`@272 is its own, otherwise the group whose runtime range
    contains it owns it and the walk recurses with the index rebased by that
    group's `seq_base`. Because the ranges only exist in a loaded model, the
    whole numbering is a property of the running process; a level whose header
    the session never named is reported as a miss rather than guessed.
    """
    def total(image: bytes) -> int:
        groups = _i32(image, HEADER_NUM_INCLUDES)
        if groups <= 0:
            return _i32(image, 272)
        last = _i32(image, HEADER_INCLUDE_INDEX) + (groups - 1) * GROUP_STRIDE
        return _i32(image, last + GROUP_SEQ_BASE) + _i32(image, last + GROUP_SEQ_COUNT)

    def walk(name: str, index: int, depth: int):
        image = images.get(install_key(name))
        if image is None or depth > 16:
            return None, name, index, depth
        if index < _i32(image, 272):
            return image, name, index, depth
        groups = _i32(image, HEADER_NUM_INCLUDES)
        group_base = _i32(image, HEADER_INCLUDE_INDEX)
        for group in range(groups):
            base = group_base + group * GROUP_STRIDE
            start = _i32(image, base + GROUP_SEQ_BASE)
            count = _i32(image, base + GROUP_SEQ_COUNT)
            if start <= index < start + count:
                bank = _cstring(image, base + _i32(image, base + GROUP_FILENAME))
                return walk(bank, index - start, depth + 1)
        return None, name, index, depth

    root = images.get(install_key(model))
    if root is None:
        return []
    rows = []
    for index in range(min(total(root), limit)):
        image, owner, local, depth = walk(model, index, 0)
        label = None
        activity = None
        if image is not None:
            labels = mdl_skel.local_sequence_labels(image)
            activities = mdl_skel.local_sequence_activities(image)
            if 0 <= local < len(labels):
                label = labels[local]
                activity = activities[local] or None
        rows.append(
            {
                "body_model": model,
                "global_index": index,
                "owner_model": owner,
                "owner_index": local,
                "depth": depth,
                "label": label,
                "activity_name": activity,
                "resolved": image is not None and label is not None,
            }
        )
    return rows


def _write_csv(path: Path, rows: list[dict[str, Any]], columns: list[str]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument(
        "--session",
        type=Path,
        help="A rig-resolution capture; the newest one is used when omitted.",
    )
    arguments = parser.parse_args()
    if arguments.pid <= 0:
        raise ValueError("target PID must be positive")
    session = (
        arguments.session.resolve() if arguments.session else latest_session()
    )
    headers = observed_headers(session)
    if not headers:
        print(
            f"WARNING - {session} names no loaded studio header; nothing to read",
            file=sys.stderr,
        )
        return 2

    bones = BoneNames()
    reader = ProcessReader(arguments.pid)
    rows: list[dict[str, Any]] = []
    summaries: list[dict[str, Any]] = []
    unreadable: list[str] = []
    images: dict[str, bytes] = {}
    try:
        for address, model in sorted(headers.items(), key=lambda item: item[1]):
            image = read_image(reader, address, model)
            if image is None:
                unreadable.append(f"{model} @ 0x{address:08x}")
                continue
            images.setdefault(install_key(model), image)
            model_rows, model_summaries = remap_rows(image, model, bones)
            rows.extend(model_rows)
            summaries.extend(model_summaries)
    finally:
        reader.close()

    # A body is a model whose own sequences are a small prefix of a much larger
    # answered range; mapping every one of those numbers is what lets a capture
    # that recorded only a sequence id name the clip behind it.
    sequences: list[dict[str, Any]] = []
    for key, image in sorted(images.items()):
        if _i32(image, HEADER_NUM_INCLUDES) <= 0:
            continue
        sequences.extend(sequence_map(images, _cstring(image, HEADER_NAME)))

    _write_csv(
        session / "rig_remap.csv",
        rows,
        [
            "including_model", "group", "bank", "body_bone_index", "body_bone",
            "bank_bone_index", "bank_bone", "mode", "sub", "chain_start",
            "chain_end", "matrix3x4",
        ],
    )
    _write_csv(
        session / "sequence_map.csv",
        sequences,
        [
            "body_model", "global_index", "owner_model", "owner_index", "depth",
            "label", "activity_name", "resolved",
        ],
    )
    resolved = [row for row in summaries if row.get("resolved")]
    conflicts = [row for row in resolved if row["name_conflicts"]]
    report = {
        "session": str(session),
        "pid": arguments.pid,
        "method": "ReadProcessMemory",
        "headers_named": len(headers),
        "headers_unreadable": unreadable,
        "groups": len(summaries),
        "groups_resolved": len(resolved),
        "groups_with_name_conflicts": len(conflicts),
        "sequence_numbers_mapped": len(sequences),
        "sequence_numbers_unresolved": sum(
            1 for row in sequences if not row["resolved"]
        ),
        "banks_missing_from_install": sorted(bones.missing),
        "group_summaries": summaries,
    }
    (session / "rig_remap.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    print(f"Runtime rig remap: {session}")
    print(f"  headers named        {len(headers)} ({len(unreadable)} unreadable)")
    print(f"  include groups       {len(summaries)} ({len(resolved)} carry a remap)")
    print(
        f"  sequence numbers     {len(sequences)} mapped, "
        f"{sum(1 for row in sequences if not row['resolved'])} unresolved"
    )
    for row in resolved:
        print(
            f"    {row['including_model']} -> {row['bank']}: "
            f"{row['bones_mapped']}/{row['body_bones']} bones mapped, "
            f"{row['name_agreements']} by identical name, "
            f"{len(row['name_conflicts'])} disagree, modes {row['modes']}"
        )
    if conflicts:
        print(
            f"WARNING - {len(conflicts)} groups map a bone to a differently named "
            "bone; a name-matching rig cannot reproduce those rows",
            file=sys.stderr,
        )
    if unreadable:
        print(
            "WARNING - no live header at: " + ", ".join(unreadable), file=sys.stderr
        )
    if bones.missing:
        print(
            "WARNING - the install has no such model: "
            + ", ".join(sorted(bones.missing)),
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:  # noqa: BLE001 - the CLI reports its own failure
        print(f"WARNING - rig remap capture failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
