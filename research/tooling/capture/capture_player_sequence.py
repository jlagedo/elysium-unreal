"""Capture one player sequence through a retained unattended retail recipe.

The command writes one clearly owned cfg into the selected VtMB install only
when ``--install-config`` is explicit.  The file remains inert unless the
capture command calls it.  Its verified manual uninstall path is printed, and
the exact recipe is also retained with the evidence below ``ELYSIUM_WORK_ROOT``.

Usage:
    uv run elysium research capture_player_sequence --install-config
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from typing import Any

import psutil

from elysium_pipeline.paths import research_root, vtmb_root


DEFAULT_MODEL = (
    "models/character/pc/male/tremere/armor0/tremere_Male_Armor_0.mdl"
)
DEFAULT_LOAD_CONFIG = "elysium_load.cfg"
INSTALLED_CONFIG_NAME = "elysium_cap11_capture.cfg"
CONFIG_SIGNATURE = "// Generated retained CAP1.1 retail capture recipe."
LEGACY_CONFIG_SIGNATURE = "// Generated disposable CAP1.1 retail capture recipe."
PRE_THIRD_PERSON_WAITS = 30
TARGET_READY_WAITS = 30
MOVEMENT_SETTLE_WAITS = 45
REPEAT_GAP_WAITS = 15
POST_SEQUENCE_WAITS = 180
ALIGNMENT_TOLERANCE_FRAMES = 12
FIXED_FPS = 30.0
ARM_AFTER_TARGET_SECONDS = 0.0
MAX_PRE_SEQUENCE_FRAMES = TARGET_READY_WAITS + 1 + MOVEMENT_SETTLE_WAITS
SEQUENCE_REPETITIONS = 2


def _wait_lines(count: int) -> list[str]:
    if count < 0:
        raise ValueError("wait count cannot be negative")
    return ["wait"] * count


def _console_token(value: str, label: str) -> str:
    if not re.fullmatch(r"[A-Za-z0-9_.@-]+", value):
        raise ValueError(f"{label} is not a safe console token: {value!r}")
    return value


def read_load_command(path: Path) -> str:
    commands = [
        line.strip()
        for line in path.read_text(encoding="utf-8-sig").splitlines()
        if line.strip() and not line.lstrip().startswith("//")
    ]
    loads = [
        command
        for command in commands
        if command.partition(" ")[0].casefold() == "load"
    ]
    if len(loads) != 1:
        raise ValueError(f"{path} must contain exactly one active load command")
    return loads[0]


def build_config(load_command: str, clip: str, sequence_frames: int = 81) -> str:
    if "\n" in load_command or "\r" in load_command:
        raise ValueError("load command must occupy one line")
    if load_command.partition(" ")[0].casefold() != "load":
        raise ValueError(f"not a load command: {load_command!r}")
    if sequence_frames < 1:
        raise ValueError("sequence frame count must be positive")
    clip = _console_token(clip, "clip")
    lines = [
        CONFIG_SIGNATURE,
        "// Uninstall: delete this file.",
        "echo ELYSIUM_CAP11_BOOT",
        "sv_cheats 1",
        "fps_max 30",
        "host_framerate 0.033333333",
        "host_timescale 1",
        "pausable 0",
        "cl_mouselook 0",
        "cl_mouseenable 0",
        load_command,
        *_wait_lines(PRE_THIRD_PERSON_WAITS),
        "thirdperson",
        *_wait_lines(TARGET_READY_WAITS),
        "+forward",
        "wait",
        "-forward",
        *_wait_lines(MOVEMENT_SETTLE_WAITS),
        "echo ELYSIUM_CAP11_ARM",
        f"player_sequence {clip}",
        "echo ELYSIUM_CAP11_TRIGGERED",
        *_wait_lines(sequence_frames + REPEAT_GAP_WAITS),
        f"player_sequence {clip}",
        "echo ELYSIUM_CAP11_REPEATED",
        *_wait_lines(POST_SEQUENCE_WAITS),
        "quit",
        "",
    ]
    return "\n".join(lines)


def _contiguous_run(pairs: list[list[int]]) -> int:
    longest = 0
    current = 0
    previous: tuple[int, int] | None = None
    for live, authored in pairs:
        pair = (int(live), int(authored))
        if previous is not None and pair == (previous[0] + 1, previous[1] + 1):
            current += 1
        else:
            current = 1
        longest = max(longest, current)
        previous = pair
    return longest


def assess_seed(
    capture_manifest: dict[str, Any], validation: dict[str, Any]
) -> dict[str, Any]:
    clip_frames = int(validation["clip"]["frames"])
    alignment = validation["live_to_authored_alignment"]
    pairs = alignment["aligned_pairs"]
    required = max(1, clip_frames - ALIGNMENT_TOLERANCE_FRAMES)
    contiguous = _contiguous_run(pairs)
    authored = [int(pair[1]) for pair in pairs]
    records = capture_manifest.get("records", {})
    checks = {
        "capture_complete": bool(capture_manifest.get("complete")),
        "zero_drops": int(records.get("dropped", -1)) == 0,
        "zero_incomplete": int(records.get("incomplete", -1)) == 0,
        "enough_aligned_pairs": len(pairs) >= required,
        "contiguous_authored_run": contiguous >= required,
        "covers_clip_entry": bool(authored) and min(authored) <= 10,
        "covers_clip_exit": bool(authored) and max(authored) >= clip_frames - 4,
    }
    return {
        "passed": all(checks.values()),
        "checks": checks,
        "required_aligned_frames": required,
        "aligned_pair_count": len(pairs),
        "longest_contiguous_run": contiguous,
        "first_authored_frame": min(authored) if authored else None,
        "last_authored_frame": max(authored) if authored else None,
    }


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _git_identity(repo_root: Path) -> dict[str, Any]:
    head = subprocess.run(
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
    return {"commit": head, "dirty": bool(status), "status": status}


def _running_vampire_pids() -> list[int]:
    matches = []
    for process in psutil.process_iter(("pid", "name")):
        try:
            if (process.info["name"] or "").casefold() == "vampire.exe":
                matches.append(int(process.info["pid"]))
        except (psutil.AccessDenied, psutil.NoSuchProcess):
            continue
    return sorted(matches)


def _stop_process(process: subprocess.Popen[Any]) -> bool:
    if process.poll() is not None:
        return False
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)
    return True


def _write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def _latest_capture_session(root: Path) -> Path:
    manifests = sorted(
        root.glob("*/manifest.json"),
        key=lambda path: path.stat().st_mtime,
    )
    if not manifests:
        raise FileNotFoundError(f"capture produced no manifest below {root}")
    return manifests[-1].parent


def run(args: argparse.Namespace) -> int:
    from elysium_pipeline.formats import install, mdl_skel
    from research.tooling.capture import validate_live_pose_capture

    repo_root = Path(__file__).resolve().parents[3]
    game_root = vtmb_root().resolve()
    executable = game_root / "Vampire.exe"
    cfg_root = game_root / "Unofficial_Patch" / "cfg"
    load_config = cfg_root / args.load_config
    if not executable.is_file():
        raise FileNotFoundError(executable)
    if not load_config.is_file():
        raise FileNotFoundError(load_config)

    model_key = args.model.replace("\\", "/")
    if not model_key.casefold().startswith("models/"):
        model_key = "models/" + model_key
    if not model_key.casefold().endswith(".mdl"):
        model_key += ".mdl"
    index = install.build_index(verbose=False)
    model_data = install.read(index, model_key)
    if model_data is None:
        raise FileNotFoundError(f"{model_key} is absent from the merged install")
    owner_key, _, sequence = validate_live_pose_capture.resolve_clip(
        index, model_key, args.clip
    )
    target_bones = len(mdl_skel.read_bones(model_data))

    stamp = time.strftime("%Y%m%d_%H%M%S")
    token = f"{stamp}_{os.getpid()}"
    config_name = INSTALLED_CONFIG_NAME
    config_path = cfg_root / config_name
    load_command = read_load_command(load_config)
    config_text = build_config(load_command, args.clip, sequence.frames)

    if args.dry_run:
        print(config_text, end="")
        print(f"install_path={config_path}")
        print(f"uninstall_path={config_path}")
        return 0
    if not args.install_config:
        raise ValueError(
            "retail capture writes a retained, inert cfg into the VtMB install; "
            "rerun with --install-config (the command prints its exact uninstall path)"
        )
    if config_path.exists():
        installed_text = config_path.read_text(encoding="utf-8-sig")
        if not installed_text.startswith(
            (CONFIG_SIGNATURE, LEGACY_CONFIG_SIGNATURE)
        ):
            raise FileExistsError(
                f"refusing to overwrite non-Elysium cfg: {config_path}"
            )
    running = _running_vampire_pids()
    if running:
        raise RuntimeError(f"vampire.exe is already running: {running}")

    run_root = (
        args.output.resolve()
        if args.output
        else research_root() / "player-sequence" / f"cap11_howl_{token}"
    )
    run_root.mkdir(parents=True, exist_ok=False)
    capture_root = run_root / "capture"
    capture_root.mkdir()
    capture_log = run_root / "capture.log"
    recipe_path = run_root / "recipe.cfg"
    recipe_path.write_text(config_text, encoding="utf-8")
    run_manifest_path = run_root / "run.json"
    launch_command = [
        os.fspath(executable),
        "-game",
        "Unofficial_Patch",
        "-dev",
        "-console",
        "-sw",
        "-w",
        "1024",
        "-h",
        "768",
        "+exec",
        config_name,
    ]
    tool_files = [
        Path(__file__).resolve(),
        Path(validate_live_pose_capture.__file__).resolve(),
        Path(__file__).with_name("capture_live_pose.py").resolve(),
    ]
    run_manifest: dict[str, Any] = {
        "status": "running",
        "tool": {
            "git": _git_identity(repo_root),
            "files": {os.fspath(path): _sha256(path) for path in tool_files},
        },
        "recipe": {
            "launch_command": launch_command,
            "load_config": os.fspath(load_config),
            "load_command": load_command,
            "installed_config": os.fspath(config_path),
            "retained_config": os.fspath(recipe_path),
            "arm_after_target_seconds": args.arm_after_target_seconds,
            "waits": {
                "before_third_person": PRE_THIRD_PERSON_WAITS,
                "target_ready": TARGET_READY_WAITS,
                "movement_settle": MOVEMENT_SETTLE_WAITS,
                "between_repeated_sequences": sequence.frames
                + REPEAT_GAP_WAITS,
                "after_sequence": POST_SEQUENCE_WAITS,
            },
            "sequence_repetitions": SEQUENCE_REPETITIONS,
        },
        "target": {
            "model": model_key,
            "model_sha256": hashlib.sha256(model_data).hexdigest(),
            "bone_count": target_bones,
            "clip": args.clip,
            "owner": owner_key,
            "frames": sequence.frames,
            "fps": sequence.fps,
        },
        "process": {},
        "uninstall": [os.fspath(config_path)],
    }
    _write_json(run_manifest_path, run_manifest)

    game: subprocess.Popen[Any] | None = None
    collector: subprocess.Popen[Any] | None = None
    forced_cleanup = False
    failure: BaseException | None = None
    print(f"install={config_path}", flush=True)
    print(f"uninstall={config_path}", flush=True)
    try:
        config_path.write_text(config_text, encoding="ascii")
        game = subprocess.Popen(
            launch_command,
            cwd=game_root,
            creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0),
        )
        run_manifest["process"]["game_pid"] = game.pid
        collector_command = [
            sys.executable,
            "-m",
            "research.tooling.capture.capture_live_pose",
            "--pid",
            str(game.pid),
            "--model",
            model_key,
            "--label",
            f"{args.clip}_seed",
            "--frames",
            str(
                MAX_PRE_SEQUENCE_FRAMES
                + sequence.frames * SEQUENCE_REPETITIONS
            ),
            "--timeout",
            str(args.capture_timeout_seconds),
            "--output",
            os.fspath(capture_root),
            "--arm-after-target-seconds",
            str(args.arm_after_target_seconds),
        ]
        run_manifest["process"]["collector_command"] = collector_command
        _write_json(run_manifest_path, run_manifest)
        with capture_log.open("w", encoding="utf-8") as stream:
            collector = subprocess.Popen(
                collector_command,
                cwd=repo_root,
                stdout=stream,
                stderr=subprocess.STDOUT,
                text=True,
            )
            deadline = time.monotonic() + args.timeout_seconds
            while collector.poll() is None:
                if game.poll() is not None:
                    raise RuntimeError(
                        f"retail exited before capture completed: {game.returncode}"
                    )
                if time.monotonic() >= deadline:
                    raise TimeoutError("CAP1.1 capture exceeded the overall timeout")
                time.sleep(0.05)
        run_manifest["process"]["collector_exit_code"] = collector.returncode
        print(capture_log.read_text(encoding="utf-8"), end="", flush=True)
        if collector.returncode != 0:
            raise RuntimeError(f"pose collector exited {collector.returncode}")

        remaining = max(0.1, deadline - time.monotonic())
        try:
            game_exit = game.wait(timeout=remaining)
        except subprocess.TimeoutExpired as exc:
            raise TimeoutError("retail did not exit through the generated cfg") from exc
        run_manifest["process"]["game_exit_code"] = game_exit
        if game_exit not in (0, 1):
            raise RuntimeError(f"retail exited with unexpected code {game_exit}")

        session = _latest_capture_session(capture_root)
        capture_manifest_path = session / "manifest.json"
        capture_manifest = json.loads(capture_manifest_path.read_text(encoding="utf-8"))
        validation = validate_live_pose_capture.validate(
            session, model_key, args.clip
        )
        validation_path = session / "validation.json"
        _write_json(validation_path, validation)
        acceptance = assess_seed(capture_manifest, validation)
        run_manifest.update(
            {
                "status": "complete" if acceptance["passed"] else "rejected",
                "capture_manifest": os.fspath(capture_manifest_path),
                "validation": os.fspath(validation_path),
                "acceptance": acceptance,
            }
        )
        if not acceptance["passed"]:
            raise RuntimeError(f"CAP1.1 acceptance failed: {acceptance['checks']}")
        print(f"run={run_root}")
        print(f"capture={session}")
        print(f"validation={validation_path}")
        print(f"acceptance={json.dumps(acceptance, sort_keys=True)}")
        return 0
    except BaseException as exc:
        failure = exc
        if run_manifest["status"] == "running":
            run_manifest["status"] = "failed"
        run_manifest["error"] = f"{type(exc).__name__}: {exc}"
        raise
    finally:
        if collector is not None:
            forced_cleanup = _stop_process(collector) or forced_cleanup
            run_manifest["process"]["collector_exit_code"] = collector.returncode
        if game is not None:
            forced_cleanup = _stop_process(game) or forced_cleanup
            run_manifest["process"]["game_exit_code"] = game.returncode
        run_manifest["cleanup"] = {
            "config_present": config_path.is_file(),
            "forced_process_cleanup": forced_cleanup,
            "vampire_pids_after": _running_vampire_pids(),
        }
        if failure is None and run_manifest["status"] == "complete":
            run_manifest["cleanup"]["natural_game_exit"] = not forced_cleanup
        _write_json(run_manifest_path, run_manifest)
        print(
            "cleanup=" + json.dumps(run_manifest["cleanup"], sort_keys=True),
            flush=True,
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--install-config",
        action="store_true",
        help="Explicitly allow the disposable cfg installation for this run.",
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--load-config", default=DEFAULT_LOAD_CONFIG)
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--clip", default="howl")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--timeout-seconds", type=float, default=75.0)
    parser.add_argument("--capture-timeout-seconds", type=float, default=45.0)
    parser.add_argument(
        "--arm-after-target-seconds",
        type=float,
        default=ARM_AFTER_TARGET_SECONDS,
    )
    args = parser.parse_args()
    if args.timeout_seconds <= 0 or args.timeout_seconds > 300:
        parser.error("--timeout-seconds must be in (0, 300]")
    if args.capture_timeout_seconds <= 0 or args.capture_timeout_seconds > 120:
        parser.error("--capture-timeout-seconds must be in (0, 120]")
    if args.arm_after_target_seconds < 0 or args.arm_after_target_seconds > 60:
        parser.error("--arm-after-target-seconds must be in [0, 60]")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
