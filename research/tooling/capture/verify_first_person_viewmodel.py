"""Verify raw ELGVM1 retail viewmodel streams without screenshot evidence."""

from __future__ import annotations

import argparse
import ctypes
from dataclasses import dataclass
import json
import math
from pathlib import Path
from typing import Iterable

from elysium_pipeline.paths import research_root
from research.tooling.capture.capture_first_person_viewmodel import (
    ACTION_SCENARIOS,
    MODULES,
    PROJECTION_SCENARIOS,
    SCENARIOS,
    capture_root,
)


INVALID_HANDLE = 0xFFFFFFFF
EXPECTED_TARGET_RVAS = (
    0x2532A0,
    0x238160,
    0x2387B0,
    0x238230,
    0x239570,
    0x255050,
    0x2552C0,
    0x198FA0,
    0x0919C0,
    0x0AB530,
    0x07A3E0,
)


class FileHeader(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("magic", ctypes.c_char * 8),
        ("format_version", ctypes.c_uint32),
        ("header_bytes", ctypes.c_uint32),
        ("record_bytes", ctypes.c_uint32),
        ("qpc_frequency", ctypes.c_uint64),
        ("start_qpc", ctypes.c_int64),
        ("process_id", ctypes.c_uint32),
        ("vampire_base", ctypes.c_uint32),
        ("client_base", ctypes.c_uint32),
        ("engine_base", ctypes.c_uint32),
        ("target_rvas", ctypes.c_uint32 * 11),
        ("vampire_sha256", ctypes.c_char * 65),
        ("client_sha256", ctypes.c_char * 65),
        ("engine_sha256", ctypes.c_char * 65),
        ("scenario", ctypes.c_char * 32),
        ("reserved", ctypes.c_char * 5),
    ]


class Record(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("magic", ctypes.c_char * 4),
        ("record_bytes", ctypes.c_uint32),
        ("ordinal", ctypes.c_uint64),
        ("qpc", ctypes.c_int64),
        ("thread_id", ctypes.c_uint32),
        ("frame", ctypes.c_uint32),
        ("boundary_id", ctypes.c_uint32),
        ("call_phase", ctypes.c_uint32),
        ("module_base", ctypes.c_uint32),
        ("function_rva", ctypes.c_uint32),
        ("caller_address", ctypes.c_uint32),
        ("server_entity", ctypes.c_uint32),
        ("server_handle", ctypes.c_uint32),
        ("server_owner_handle", ctypes.c_uint32),
        ("client_entity", ctypes.c_uint32),
        ("client_handle", ctypes.c_uint32),
        ("client_weapon_handle", ctypes.c_uint32),
        ("viewmodel_index", ctypes.c_int32),
        ("server_sequence", ctypes.c_int32),
        ("client_sequence", ctypes.c_int32),
        ("event_id", ctypes.c_int32),
        ("ammo_before", ctypes.c_int32),
        ("ammo_after", ctypes.c_int32),
        ("cycle", ctypes.c_float),
        ("playback_rate", ctypes.c_float),
        ("player_fov", ctypes.c_float),
        ("viewmodel_fov", ctypes.c_float),
        ("near_plane", ctypes.c_float),
        ("far_plane", ctypes.c_float),
        ("aspect", ctypes.c_float),
        ("tan_half_fov", ctypes.c_float),
        ("viewport_width", ctypes.c_int32),
        ("viewport_height", ctypes.c_int32),
        ("draw_policy", ctypes.c_uint32),
        ("projection_flags", ctypes.c_uint32),
        ("bone_count", ctypes.c_uint32),
        ("root_bone_index", ctypes.c_uint32),
        ("alignment_bone_index", ctypes.c_uint32),
        ("faults", ctypes.c_uint32),
        ("root_transform", ctypes.c_float * 12),
        ("alignment_transform", ctypes.c_float * 12),
        ("hands_model", ctypes.c_char * 96),
        ("weapon_model", ctypes.c_char * 96),
        ("scenario", ctypes.c_char * 32),
    ]


assert ctypes.sizeof(FileHeader) == 328
assert ctypes.sizeof(Record) == 484


BOUNDARY = {
    "server_update": 1,
    "server_event": 2,
    "server_shot": 3,
    "server_dry": 4,
    "reload_request": 5,
    "reload_frame": 6,
    "reload_finish": 7,
    "draw": 20,
    "setup_bones": 21,
    "client_event": 22,
    "projection": 30,
}
ENTRY = 1
EXIT = 2


class VerificationError(RuntimeError):
    pass


@dataclass(frozen=True)
class Stream:
    path: Path
    header: FileHeader
    records: tuple[Record, ...]

    @property
    def scenario(self) -> str:
        return cstring(self.header.scenario)


def cstring(value: object) -> str:
    return bytes(value).split(b"\0", 1)[0].decode("ascii", "strict")


def read_stream(path: Path) -> Stream:
    data = path.read_bytes()
    if len(data) < ctypes.sizeof(FileHeader):
        raise VerificationError(f"{path}: truncated ELGVM1 header")
    header = FileHeader.from_buffer_copy(data)
    if bytes(header.magic)[:6] != b"ELGVM1":
        raise VerificationError(f"{path}: bad ELGVM1 magic")
    if header.format_version != 1:
        raise VerificationError(f"{path}: unsupported version {header.format_version}")
    if header.header_bytes != ctypes.sizeof(FileHeader):
        raise VerificationError(f"{path}: bad header size {header.header_bytes}")
    if header.record_bytes != ctypes.sizeof(Record):
        raise VerificationError(f"{path}: bad record size {header.record_bytes}")
    payload = data[header.header_bytes :]
    if len(payload) % header.record_bytes:
        raise VerificationError(f"{path}: truncated ELGVM1 record")
    records: list[Record] = []
    for offset in range(0, len(payload), header.record_bytes):
        record = Record.from_buffer_copy(payload, offset)
        if bytes(record.magic) != b"VMR1" or record.record_bytes != header.record_bytes:
            raise VerificationError(f"{path}: malformed record at payload offset {offset}")
        if cstring(record.scenario) != cstring(header.scenario):
            raise VerificationError(f"{path}: record scenario differs from header")
        records.append(record)
    return Stream(path, header, tuple(records))


def _finite(values: Iterable[float]) -> bool:
    return all(math.isfinite(value) and abs(value) < 1.0e7 for value in values)


def _matrix_delta(left: Record, right: Record, field: str) -> float:
    a = getattr(left, field)
    b = getattr(right, field)
    return max(abs(float(x) - float(y)) for x, y in zip(a, b))


def _pairs(stream: Stream) -> list[tuple[Record, Record]]:
    frames: dict[int, list[Record]] = {}
    for record in stream.records:
        if record.boundary_id == BOUNDARY["setup_bones"] and record.frame:
            frames.setdefault(record.frame, []).append(record)
    pairs: list[tuple[Record, Record]] = []
    for records in frames.values():
        hands: Record | None = None
        weapon: Record | None = None
        seen_hands = False
        seen_weapon = False
        for record in sorted(records, key=lambda item: item.ordinal):
            hands_name = cstring(record.hands_model).casefold()
            weapon_name = cstring(record.weapon_model).casefold()
            has_hands = hands_name.startswith("models/hands/") or (
                hands_name.startswith("v_") and "hands" in hands_name
            )
            has_weapon = weapon_name.startswith("models/weapons/") or (
                weapon_name.startswith("v_") and "hands" not in weapon_name
            )
            if has_hands and not seen_hands:
                hands = record
                seen_hands = True
                continue
            if has_weapon and not seen_weapon:
                weapon = record
                seen_weapon = True
        if hands is not None and weapon is not None:
            pairs.append((hands, weapon))
    return pairs


def _ordered_span(records: tuple[Record, ...], boundary: int) -> list[tuple[int, int]]:
    entries: dict[tuple[int, int], int] = {}
    spans: list[tuple[int, int]] = []
    for record in sorted(records, key=lambda item: item.ordinal):
        key = (record.thread_id, record.server_entity)
        if record.boundary_id != boundary:
            continue
        if record.call_phase == ENTRY:
            entries[key] = record.ordinal
        elif record.call_phase == EXIT and key in entries:
            spans.append((entries.pop(key), record.ordinal))
    return spans


def _verify_header(stream: Stream) -> list[str]:
    errors: list[str] = []
    header = stream.header
    expected_hashes = (
        MODULES["vampire.dll"],
        MODULES["client.dll"],
        MODULES["engine.dll"],
    )
    actual_hashes = (
        cstring(header.vampire_sha256),
        cstring(header.client_sha256),
        cstring(header.engine_sha256),
    )
    if actual_hashes != expected_hashes:
        errors.append("header binary hashes do not match the pinned registry")
    if tuple(header.target_rvas) != EXPECTED_TARGET_RVAS:
        errors.append("header target RVAs do not match the binary profiles")
    if not header.qpc_frequency or not header.process_id:
        errors.append("header lacks clock or process identity")
    if not all((header.vampire_base, header.client_base, header.engine_base)):
        errors.append("header lacks one or more module bases")
    if not stream.records:
        errors.append("stream has no records")
    if any(record.faults for record in stream.records):
        errors.append("one or more raw records reports a capture fault")
    ordinals = [record.ordinal for record in stream.records]
    if len(ordinals) != len(set(ordinals)):
        errors.append("record ordinals are not unique")
    return errors


def _verify_composition(stream: Stream) -> list[str]:
    errors: list[str] = []
    pairs = _pairs(stream)
    if not pairs:
        return ["no frame contains separate hands and weapon SetupBones records"]
    if not any(left.client_entity != right.client_entity for left, right in pairs):
        errors.append("paired submissions do not identify distinct client entities")
    if not any(
        left.client_handle not in (0, INVALID_HANDLE)
        and right.client_handle not in (0, INVALID_HANDLE)
        and left.client_handle != right.client_handle
        for left, right in pairs
    ):
        errors.append("paired submissions do not carry two distinct client handles")
    aligned = [
        pair
        for pair in pairs
        if pair[0].root_bone_index != INVALID_HANDLE
        and pair[1].root_bone_index != INVALID_HANDLE
        and pair[0].alignment_bone_index != INVALID_HANDLE
        and pair[1].alignment_bone_index != INVALID_HANDLE
        and _finite(pair[0].root_transform)
        and _finite(pair[1].root_transform)
        and _finite(pair[0].alignment_transform)
        and _finite(pair[1].alignment_transform)
        and _matrix_delta(pair[0], pair[1], "root_transform") <= 0.1
        and _matrix_delta(pair[0], pair[1], "alignment_transform") <= 0.1
    ]
    if not aligned:
        errors.append("no paired frame proves Camera01 and R-hand bone alignment")
    server_handles = {
        record.server_handle
        for record in stream.records
        if record.server_handle not in (0, INVALID_HANDLE)
    }
    client_weapon_handles = {
        record.client_weapon_handle
        for record in stream.records
        if record.client_weapon_handle not in (0, INVALID_HANDLE)
    }
    if not server_handles.intersection(client_weapon_handles):
        errors.append("server weapon handle does not join a client viewmodel weapon hook")
    return errors


def _verify_projection(stream: Stream) -> list[str]:
    errors: list[str] = []
    projections = [
        record
        for record in stream.records
        if record.boundary_id == BOUNDARY["projection"]
    ]
    draws = [
        record
        for record in stream.records
        if record.boundary_id == BOUNDARY["draw"] and record.call_phase == ENTRY
    ]
    matches: list[tuple[Record, Record]] = []
    for draw in draws:
        for projection in projections:
            if (
                projection.frame == draw.frame
                and math.isclose(projection.viewmodel_fov, draw.viewmodel_fov, abs_tol=1e-4)
                and math.isclose(projection.near_plane, draw.near_plane, abs_tol=1e-4)
                and math.isclose(projection.far_plane, draw.far_plane, abs_tol=1e-3)
            ):
                matches.append((draw, projection))
                break
    if not matches:
        return ["no client draw joins its separate engine viewmodel projection"]
    for _, projection in matches:
        expected_tan = math.tan(math.radians(projection.viewmodel_fov) / 2.0)
        if not math.isclose(projection.tan_half_fov, expected_tan, rel_tol=1e-5):
            errors.append("engine projection does not use tan(viewmodel_fov / 2)")
            break
        if projection.projection_flags != 0:
            errors.append("viewmodel projection unexpectedly used the square-aspect flag")
            break
    if stream.scenario.startswith("projection-"):
        _, player, viewmodel, aspect = stream.scenario.split("-")
        expected_player = float(player[1:])
        expected_viewmodel = float(viewmodel[1:])
        expected_aspect = 4.0 / 3.0 if aspect == "4x3" else 16.0 / 9.0
        if not any(
            math.isclose(draw.player_fov, expected_player, abs_tol=0.05)
            and math.isclose(draw.viewmodel_fov, expected_viewmodel, abs_tol=0.05)
            and math.isclose(projection.aspect, expected_aspect, rel_tol=1e-4)
            for draw, projection in matches
        ):
            errors.append("projection scenario values do not match its player/viewmodel FOV and aspect label")
    if not any(
        math.isclose(draw.near_plane, 1.0, abs_tol=1e-4)
        and math.isclose(draw.far_plane, 28400.0, abs_tol=0.5)
        for draw, _ in matches
    ):
        errors.append("viewmodel clipping range is not 1..28400")
    return errors


def _verify_authority(stream: Stream) -> list[str]:
    errors: list[str] = []
    events = _ordered_span(stream.records, BOUNDARY["server_event"])
    shots = _ordered_span(stream.records, BOUNDARY["server_shot"])
    if not shots:
        errors.append("no server weapon shot transaction was captured")
    elif not any(
        event_entry < shot_entry < shot_exit < event_exit
        for event_entry, event_exit in events
        for shot_entry, shot_exit in shots
    ):
        errors.append("server shot is not nested inside the server animation-event route")
    shot_exits = [
        record
        for record in stream.records
        if record.boundary_id == BOUNDARY["server_shot"]
        and record.call_phase == EXIT
    ]
    if not any(
        record.ammo_before >= 0
        and record.ammo_after >= 0
        and record.ammo_after < record.ammo_before
        for record in shot_exits
    ):
        errors.append("server shot exit does not prove ammunition consumption")
    if not any(
        record.boundary_id == BOUNDARY["client_event"]
        for record in stream.records
    ):
        errors.append("no client visual animation event was captured")
    dry_spans = _ordered_span(stream.records, BOUNDARY["server_dry"])
    if not dry_spans:
        errors.append("no dry-fire boundary was captured")
    reload_order = [
        next(
            (
                record.ordinal
                for record in stream.records
                if record.boundary_id == boundary and record.call_phase == ENTRY
            ),
            None,
        )
        for boundary in (
            BOUNDARY["reload_request"],
            BOUNDARY["reload_frame"],
            BOUNDARY["reload_finish"],
        )
    ]
    if any(value is None for value in reload_order) or reload_order != sorted(reload_order):
        errors.append("reload request, frame, and finish boundaries are absent or unordered")
    if stream.scenario.endswith("m37"):
        reload_frames = sum(
            record.boundary_id == BOUNDARY["reload_frame"]
            and record.call_phase == ENTRY
            for record in stream.records
        )
        if reload_frames < 3:
            errors.append("M37 capture lacks three reload-frame observations")
    return errors


def _verify_draw_off(stream: Stream) -> list[str]:
    draw_frames = {
        record.frame: record
        for record in stream.records
        if record.boundary_id == BOUNDARY["draw"] and record.call_phase == ENTRY
    }
    setup_frames = {
        record.frame
        for record in stream.records
        if record.boundary_id == BOUNDARY["setup_bones"]
    }
    if not any(frame not in setup_frames for frame in draw_frames):
        return ["DrawViewmodel 0 transition did not suppress both visual submissions"]
    if not any(frame in setup_frames for frame in draw_frames):
        return ["DrawViewmodel transition lacks a visible comparison frame"]
    return []


def verify_streams(streams: list[Stream], allow_partial: bool = False) -> dict[str, object]:
    scenarios = [stream.scenario for stream in streams]
    failures: list[dict[str, object]] = []
    checks: list[dict[str, object]] = []

    def record_check(scenario: str, name: str, errors: list[str]) -> None:
        item = {"scenario": scenario, "check": name, "passed": not errors, "errors": errors}
        checks.append(item)
        if errors:
            failures.append(item)

    if not allow_partial:
        missing = sorted(set(SCENARIOS).difference(scenarios))
        record_check("aggregate", "complete-scenario-matrix", [f"missing {value}" for value in missing])
    for stream in streams:
        record_check(stream.scenario, "format-and-pins", _verify_header(stream))
        record_check(stream.scenario, "two-component-composition", _verify_composition(stream))
        record_check(stream.scenario, "projection", _verify_projection(stream))
        if stream.scenario in ACTION_SCENARIOS:
            record_check(stream.scenario, "shot-reload-authority", _verify_authority(stream))
        if stream.scenario == "drawviewmodel-off":
            record_check(stream.scenario, "drawviewmodel-lifetime", _verify_draw_off(stream))
    if not allow_partial:
        projection_values = {
            (
                round(record.player_fov, 3),
                round(record.viewmodel_fov, 3),
                round(record.aspect, 6),
            )
            for stream in streams
            for record in stream.records
            if stream.scenario in PROJECTION_SCENARIOS
            and record.boundary_id == BOUNDARY["projection"]
            and math.isclose(record.near_plane, 1.0, abs_tol=1e-4)
            and math.isclose(record.far_plane, 28400.0, abs_tol=0.5)
        }
        record_check(
            "aggregate",
            "independent-fov-aspect-matrix",
            [] if len(projection_values) >= 8 else [f"only {len(projection_values)} projection combinations observed"],
        )
    report = {
        "schema": "elysium.research.first-person-viewmodel.verification",
        "version": 1,
        "state": "complete" if not failures else "failed",
        "allow_partial": allow_partial,
        "streams": [str(stream.path) for stream in streams],
        "scenarios": sorted(scenarios),
        "record_count": sum(len(stream.records) for stream in streams),
        "checks": checks,
    }
    if failures:
        raise VerificationError(json.dumps(report, indent=2, sort_keys=True))
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path)
    parser.add_argument("--allow-partial", action="store_true")
    parser.add_argument(
        "--report",
        type=Path,
        default=research_root() / "first-person-viewmodel" / "viewmodel-capture-verification.json",
    )
    args = parser.parse_args()
    inputs = args.paths or [capture_root()]
    files: list[Path] = []
    for path in inputs:
        files.extend(sorted(path.glob("*.elgvm")) if path.is_dir() else [path])
    if not files:
        parser.error("no ELGVM1 streams found")
    try:
        report = verify_streams([read_stream(path) for path in files], args.allow_partial)
    except VerificationError as error:
        print(error)
        return 1
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(args.report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
