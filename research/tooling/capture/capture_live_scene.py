"""Control and summarize the injected whole-scene retail skeletal-pose recorder."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

from elysium_pipeline.formats import install
from elysium_pipeline.paths import research_root
from research.tooling.capture.capture_contracts import (
    DEFAULT_EXPERIMENT,
    analyzer_versions,
    create_session_manifest,
    load_experiment,
    output_metadata,
    parse_assignments,
    process_launch_context,
    validate_session_manifest,
    write_session_manifest,
)
from research.tooling.capture.capture_live_pose import find_module, find_process
from research.tooling.capture.generated_record_schemas import (
    ANIMATION_FILE_HEADER,
    ANIMATION_RECORD_HEADER,
    POSE_FILE_HEADER as FILE_HEADER,
    POSE_RECORD_HEADER as POSE_HEADER,
)


ROOT = Path(__file__).resolve().parent
OUTPUT_ROOT = research_root() / "live-pose"
BUILD_ROOT = OUTPUT_ROOT / "bin"
EXPECTED_STUDIO_RENDER_SHA256 = (
    "13d56ce90de2c5faedc0df26d36b24e90f30eded5055625f616cd97e301e124b"
)
EXPECTED_CLIENT_SHA256 = (
    "e88beae0dd03af06493c71c5e8d87a6993b54e590cb6ad37cd3513c588582870"
)
def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(8 * 1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def safe_label(value: str) -> str:
    label = re.sub(r"[^a-zA-Z0-9_.-]+", "_", value).strip("_.-")
    if not label:
        raise ValueError("capture label contains no filesystem-safe characters")
    return label


def model_checksum(model_key: str) -> tuple[str, int]:
    key = model_key.replace("\\", "/")
    if not key.lower().startswith("models/"):
        key = "models/" + key
    if not key.lower().endswith(".mdl"):
        key += ".mdl"
    data = install.read(install.build_index(verbose=False), key)
    if data is None:
        raise FileNotFoundError(f"{key} is not present in the merged install")
    return key, struct.unpack_from("<I", data, 8)[0]


def build_if_needed() -> None:
    outputs = [
        BUILD_ROOT / "live_pose_hook.dll",
        BUILD_ROOT / "live_pose_injector.exe",
    ]
    sources = [
        Path(__file__).resolve(),
        ROOT / "build_live_pose_capture.py",
        ROOT / "generated_record_schemas.h",
        ROOT / "generated_record_schemas.py",
        ROOT / "contracts" / "record_schemas.json",
        ROOT / "contracts" / "generate_record_schemas.py",
        ROOT / "live_pose_hook.cpp",
        ROOT / "live_pose_injector.cpp",
    ]
    newest_source = max(path.stat().st_mtime for path in sources)
    if any(
        not output.exists() or output.stat().st_mtime < newest_source
        for output in outputs
    ):
        subprocess.run(
            [sys.executable, str(ROOT / "build_live_pose_capture.py")],
            check=True,
        )


def write_ini(path: Path, values: dict[str, object]) -> None:
    lines = ["[capture]"]
    lines.extend(f"{key}={value}" for key, value in values.items())
    path.write_text("\n".join(lines) + "\n", encoding="utf-16")


def read_done(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    if not path.exists():
        return result
    for line in path.read_text(encoding="ascii", errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
    return result


def summarize(session: Path) -> dict[str, object]:
    trace = session / "scene.elpose"
    with trace.open("rb") as stream:
        raw_header = stream.read(FILE_HEADER.size)
        if len(raw_header) != FILE_HEADER.size:
            raise ValueError("capture file has no complete header")
        (
            magic,
            version,
            header_bytes,
            frequency,
            start_qpc,
            pid,
            studio_base,
            studio_object,
            studio_vtable,
            draw_rva,
            studio_hash,
            _,
        ) = FILE_HEADER.unpack(raw_header)
        if magic.rstrip(b"\0") != b"ELPOSE2" or version != 2:
            raise ValueError("unrecognized live-pose capture format")
        if header_bytes != FILE_HEADER.size:
            raise ValueError("unexpected live-pose file-header size")

        records = 0
        incomplete_bytes = 0
        first_qpc: int | None = None
        last_qpc: int | None = None
        models: Counter[tuple[int, int, str]] = Counter()
        entities: dict[tuple[int, int, str], set[int]] = defaultdict(set)
        thread_ids: set[int] = set()
        while True:
            raw = stream.read(POSE_HEADER.size)
            if not raw:
                break
            if len(raw) != POSE_HEADER.size:
                incomplete_bytes = len(raw)
                break
            fields = POSE_HEADER.unpack(raw)
            (
                record_magic,
                record_bytes,
                _sequence,
                qpc,
                thread_id,
                _studio_hdr,
                client_entity,
                checksum,
                bone_count,
                _model_info,
                *_tail,
            ) = fields
            if record_magic != b"POSE":
                raise ValueError(f"bad pose-record magic at record {records}")
            expected = POSE_HEADER.size + bone_count * 12 * 4 * 2
            if record_bytes != expected:
                raise ValueError(
                    f"record {records} has {record_bytes} bytes, expected {expected}"
                )
            payload = stream.read(record_bytes - POSE_HEADER.size)
            if len(payload) != record_bytes - POSE_HEADER.size:
                incomplete_bytes = POSE_HEADER.size + len(payload)
                break
            model_name = fields[-1].split(b"\0", 1)[0].decode(
                "ascii", "replace"
            )
            key = (checksum, bone_count, model_name)
            models[key] += 1
            entities[key].add(client_entity)
            thread_ids.add(thread_id)
            first_qpc = qpc if first_qpc is None else min(first_qpc, qpc)
            last_qpc = qpc if last_qpc is None else max(last_qpc, qpc)
            records += 1

    model_rows = []
    for key, count in models.most_common():
        checksum, bone_count, model_name = key
        model_rows.append(
            {
                "model": model_name,
                "checksum": f"0x{checksum:08x}",
                "bone_count": bone_count,
                "draw_records": count,
                "client_entities": [
                    f"0x{entity:08x}" for entity in sorted(entities[key])
                ],
            }
        )
    span = (
        (last_qpc - first_qpc) / frequency
        if first_qpc is not None and last_qpc is not None
        else 0.0
    )
    report = {
        "version": 1,
        "session": str(session.resolve()),
        "trace": str(trace.resolve()),
        "trace_bytes": trace.stat().st_size,
        "trace_sha256": file_sha256(trace),
        "pid": pid,
        "studio_render_base": f"0x{studio_base:08x}",
        "studio_object": f"0x{studio_object:08x}",
        "studio_vtable": f"0x{studio_vtable:08x}",
        "draw_model_rva": f"0x{draw_rva:x}",
        "studio_render_sha256": studio_hash.split(b"\0", 1)[0].decode("ascii"),
        "qpc_frequency": frequency,
        "start_qpc": start_qpc,
        "record_count": records,
        "capture_span_seconds": span,
        "thread_ids": sorted(thread_ids),
        "incomplete_tail_bytes": incomplete_bytes,
        "done": read_done(session / "done.txt"),
        "models": model_rows,
    }
    index_path = session / "scene_index.json"
    index_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def summarize_animation(session: Path) -> dict[str, object] | None:
    trace = session / "animation.elanim"
    if not trace.exists():
        return None
    with trace.open("rb") as stream:
        raw_header = stream.read(ANIMATION_FILE_HEADER.size)
        if len(raw_header) != ANIMATION_FILE_HEADER.size:
            raise ValueError("animation capture file has no complete header")
        (
            magic,
            version,
            header_bytes,
            frequency,
            start_qpc,
            pid,
            client_base,
            resolve_rva,
            build_rva,
            target_checksum,
            client_hash,
            _,
        ) = ANIMATION_FILE_HEADER.unpack(raw_header)
        animation_format = magic.rstrip(b"\0")
        if animation_format not in {b"ELANIM1", b"ELANIM2"}:
            raise ValueError("unrecognized animation capture format")
        if version not in {1, 2}:
            raise ValueError("unrecognized animation capture version")
        if (animation_format, version) not in {
            (b"ELANIM1", 1),
            (b"ELANIM2", 2),
        }:
            raise ValueError("animation capture magic/version mismatch")
        if header_bytes != ANIMATION_FILE_HEADER.size:
            raise ValueError("unexpected animation file-header size")

        records = 0
        stages: Counter[str] = Counter()
        studio_sequences: Counter[int] = Counter()
        entities: set[int] = set()
        first_qpc: int | None = None
        last_qpc: int | None = None
        cycle_ranges: dict[int, list[float]] = {}
        incomplete_bytes = 0
        while True:
            raw = stream.read(ANIMATION_RECORD_HEADER.size)
            if not raw:
                break
            if len(raw) != ANIMATION_RECORD_HEADER.size:
                incomplete_bytes = len(raw)
                break
            fields = ANIMATION_RECORD_HEADER.unpack(raw)
            (
                record_magic,
                record_bytes,
                _ordinal,
                qpc,
                _thread_id,
                client_entity,
                _studio_hdr,
                checksum,
                bone_count,
                studio_sequence,
                sample_phase,
                entity_cycle,
                _result,
                _positions,
                _quaternions,
            ) = fields
            stage = record_magic.decode("ascii", "replace")
            if stage not in {"BASE", "FINL"}:
                raise ValueError(
                    f"bad animation-record magic at record {records}: {stage!r}"
                )
            selected_bytes = ((bone_count + 31) // 32) * 4 if version >= 2 else 0
            expected = (
                ANIMATION_RECORD_HEADER.size
                + bone_count * 7 * 4
                + selected_bytes
            )
            if record_bytes != expected:
                raise ValueError(
                    f"animation record {records} has {record_bytes} bytes, "
                    f"expected {expected}"
                )
            payload = stream.read(record_bytes - ANIMATION_RECORD_HEADER.size)
            if len(payload) != record_bytes - ANIMATION_RECORD_HEADER.size:
                incomplete_bytes = ANIMATION_RECORD_HEADER.size + len(payload)
                break
            if target_checksum and checksum != target_checksum:
                raise ValueError("animation record does not match target checksum")
            records += 1
            stages[stage] += 1
            studio_sequences[studio_sequence] += 1
            entities.add(client_entity)
            first_qpc = qpc if first_qpc is None else min(first_qpc, qpc)
            last_qpc = qpc if last_qpc is None else max(last_qpc, qpc)
            bounds = cycle_ranges.setdefault(
                studio_sequence,
                [entity_cycle, entity_cycle, float("inf"), float("-inf")],
            )
            bounds[0] = min(bounds[0], entity_cycle)
            bounds[1] = max(bounds[1], entity_cycle)
            if stage == "BASE":
                bounds[2] = min(bounds[2], sample_phase)
                bounds[3] = max(bounds[3], sample_phase)

    span = (
        (last_qpc - first_qpc) / frequency
        if first_qpc is not None and last_qpc is not None
        else 0.0
    )
    report = {
        "version": 1,
        "animation_format": animation_format.decode("ascii"),
        "session": str(session.resolve()),
        "trace": str(trace.resolve()),
        "trace_bytes": trace.stat().st_size,
        "trace_sha256": file_sha256(trace),
        "pid": pid,
        "client_base": f"0x{client_base:08x}",
        "resolve_virtual_model_pose_rva": f"0x{resolve_rva:x}",
        "build_transformations_rva": f"0x{build_rva:x}",
        "target_checksum": f"0x{target_checksum:08x}",
        "client_sha256": client_hash.split(b"\0", 1)[0].decode("ascii"),
        "record_count": records,
        "capture_span_seconds": span,
        "stages": dict(stages),
        "client_entities": [
            f"0x{entity:08x}" for entity in sorted(entities)
        ],
        "studio_sequences": {
            str(sequence): {
                "records": count,
                "cycle_min": cycle_ranges[sequence][0],
                "cycle_max": cycle_ranges[sequence][1],
                "sample_phase_min": (
                    cycle_ranges[sequence][2]
                    if math.isfinite(cycle_ranges[sequence][2])
                    else None
                ),
                "sample_phase_max": (
                    cycle_ranges[sequence][3]
                    if math.isfinite(cycle_ranges[sequence][3])
                    else None
                ),
            }
            for sequence, count in studio_sequences.most_common()
        },
        "incomplete_tail_bytes": incomplete_bytes,
    }
    (session / "animation_index.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    return report


def start_capture(args) -> int:
    load_experiment(args.experiment)
    build_if_needed()
    pid = args.pid or find_process("vampire.exe")
    _, _, studio_path = find_module(pid, "studiorender.dll")
    studio_hash = hashlib.sha256(studio_path.read_bytes()).hexdigest()
    if studio_hash != EXPECTED_STUDIO_RENDER_SHA256:
        raise RuntimeError(f"unexpected StudioRender.dll SHA-256: {studio_hash}")
    _, _, client_path = find_module(pid, "client.dll")
    client_hash = hashlib.sha256(client_path.read_bytes()).hexdigest()
    if client_hash != EXPECTED_CLIENT_SHA256:
        raise RuntimeError(f"unexpected client.dll SHA-256: {client_hash}")
    animation_model = None
    target_checksum = 0
    if args.animation_model:
        animation_model, target_checksum = model_checksum(args.animation_model)

    OUTPUT_ROOT.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    session = OUTPUT_ROOT / f"{safe_label(args.label)}_{stamp}"
    session.mkdir(parents=False, exist_ok=False)
    hook = session / "live_pose_hook.dll"
    shutil.copy2(BUILD_ROOT / "live_pose_hook.dll", hook)
    ini = hook.with_suffix(".ini")
    values = {
        "output": session / "scene.elpose",
        "ready": session / "ready.txt",
        "stop": session / "stop.txt",
        "done": session / "done.txt",
        "studiorender_sha256": studio_hash,
        "client_sha256": client_hash,
        "animation_output": (
            session / "animation.elanim" if animation_model else ""
        ),
        "target_checksum": f"0x{target_checksum:08x}",
        "duration_seconds": args.duration,
    }
    write_ini(ini, values)
    _, _, executable_path = find_module(pid, "vampire.exe")
    manifest = create_session_manifest(
        session=session,
        capture_command=[sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]],
        pid=pid,
        launch=process_launch_context(pid),
        environment=parse_assignments(args.environment, "environment"),
        distribution=args.distribution,
        patch=args.patch,
        modules={
            "vampire.exe": {
                "path": str(executable_path),
                "sha256": file_sha256(executable_path),
            },
            "StudioRender.dll": {
                "path": str(studio_path),
                "sha256": studio_hash,
            },
            "client.dll": {
                "path": str(client_path),
                "sha256": client_hash,
            },
        },
        probe_profile=args.probe_profile,
        map_name=args.map,
        experiment_path=args.experiment,
        clock_controls=parse_assignments(
            args.clock_control or ["mode=uncontrolled"],
            "clock-control",
        ),
        analyzers=analyzer_versions(
            [
                ROOT / "capture_live_scene.py",
                ROOT / "analyze_live_animation_stages.py",
                ROOT / "analyze_cinematic_pose_composition.py",
            ]
        ),
        capture={
            "method": "injected StudioRender DrawModel vtable hook",
            "label": args.label,
            "duration_seconds": args.duration,
            "animation_model": animation_model,
            "animation_model_checksum": (
                f"0x{target_checksum:08x}" if animation_model else None
            ),
            "session": str(session.resolve()),
        },
    )
    write_session_manifest(session / "manifest.json", manifest)
    try:
        subprocess.run(
            [
                str(BUILD_ROOT / "live_pose_injector.exe"),
                str(hook.resolve()),
                str(pid),
            ],
            check=True,
        )
        ready = session / "ready.txt"
        deadline = time.monotonic() + 15.0
        while time.monotonic() < deadline and not ready.exists():
            done = read_done(session / "done.txt")
            if "error" in done:
                raise RuntimeError(done["error"])
            time.sleep(0.05)
        if not ready.exists():
            raise TimeoutError("hook DLL did not report ready within 15 seconds")
    except Exception as exc:
        manifest["state"] = "failed"
        manifest["hook_health"] = {
            "state": "failed",
            "details": {"error": str(exc)},
        }
        write_session_manifest(session / "manifest.json", manifest)
        raise
    print(session.resolve())
    print(f"automatic_stop_seconds={args.duration}")
    return 0


def stop_capture(args) -> int:
    session = args.session.resolve()
    manifest_path = session / "manifest.json"
    manifest = validate_session_manifest(
        json.loads(manifest_path.read_text(encoding="utf-8"))
    )
    (session / "stop.txt").write_text("stop\n", encoding="ascii")
    deadline = time.monotonic() + args.wait
    while time.monotonic() < deadline and not (session / "done.txt").exists():
        time.sleep(0.05)
    if not (session / "done.txt").exists():
        raise TimeoutError("capture hook did not stop within the wait interval")
    report = summarize(session)
    animation = summarize_animation(session)
    done = report["done"]
    capture_records = report["record_count"] + (
        animation["record_count"] if animation else 0
    )
    incomplete = report["incomplete_tail_bytes"] + (
        animation["incomplete_tail_bytes"] if animation else 0
    )
    dropped = int(done.get("dropped", "0"))
    written = int(done.get("written", str(capture_records)))
    clean = (
        done.get("complete") == "1"
        and dropped == 0
        and incomplete == 0
        and written == capture_records
    )
    output_paths = [session / "scene.elpose", session / "scene_index.json"]
    if animation:
        output_paths.extend(
            [session / "animation.elanim", session / "animation_index.json"]
        )
    manifest["state"] = "complete" if clean else "failed"
    manifest["outputs"] = output_metadata(session, output_paths)
    manifest["records"] = {
        "captured": capture_records,
        "written": written,
        "dropped": dropped,
        "incomplete": incomplete,
    }
    manifest["hook_health"] = {
        "state": "healthy" if clean else "degraded",
        "details": done,
    }
    manifest["capture"]["summary"] = {
        "scene_index": "scene_index.json",
        "animation_index": "animation_index.json" if animation else None,
    }
    write_session_manifest(manifest_path, manifest)
    print(json.dumps(
        {
            "session": report["session"],
            "records": report["record_count"],
            "bytes": report["trace_bytes"],
            "span_seconds": report["capture_span_seconds"],
            "models": len(report["models"]),
            "animation": (
                {
                    "records": animation["record_count"],
                    "bytes": animation["trace_bytes"],
                    "stages": animation["stages"],
                }
                if animation
                else None
            ),
            "done": report["done"],
            "manifest_state": manifest["state"],
        },
        indent=2,
    ))
    return 0 if clean else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    start = subparsers.add_parser("start")
    start.add_argument("--pid", type=int)
    start.add_argument("--label", default="live_scene")
    start.add_argument("--duration", type=int, default=300)
    start.add_argument("--map", default="sp_theatre")
    start.add_argument("--distribution", default="user-owned-retail")
    start.add_argument("--patch", default="Unofficial Patch 11.4")
    start.add_argument("--probe-profile", default="retained-baseline-v1")
    start.add_argument(
        "--experiment",
        type=Path,
        default=DEFAULT_EXPERIMENT,
    )
    start.add_argument(
        "--environment",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="Declared launch environment value; repeat as needed.",
    )
    start.add_argument(
        "--clock-control",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="Declared retail clock control; repeat as needed.",
    )
    start.add_argument(
        "--animation-model",
        help=(
            "Also capture raw BASE and final pre-hierarchy local poses for "
            "this patch-first models/... MDL."
        ),
    )
    start.set_defaults(function=start_capture)
    stop = subparsers.add_parser("stop")
    stop.add_argument("session", type=Path)
    stop.add_argument("--wait", type=float, default=20.0)
    stop.set_defaults(function=stop_capture)
    summary = subparsers.add_parser("summary")
    summary.add_argument("session", type=Path)
    summary.set_defaults(
        function=lambda args: (
            print(json.dumps(summarize(args.session.resolve()), indent=2)) or 0
        )
    )
    return args.function(args) if (args := parser.parse_args()) else 2


if __name__ == "__main__":
    raise SystemExit(main())
