"""Launch a hook-first ``sp_theatre`` run and retain one SQLite capture.

This tool installs one inert cfg into the selected Unofficial Patch only when
``--install-config`` is explicit. The native launcher creates ``Vampire.exe``
suspended, bootstraps the exact-build probe, preloads the skeletal hook, and
only then resumes retail. The cfg waits before issuing ``map sp_theatre`` so
the client and StudioRender hooks are armed before map resources begin loading.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

import psutil

from elysium_pipeline.paths import research_root, vtmb_root
from research.tooling.capture.finalize_capture_database import (
    finalize,
    read_key_values,
)
from research.tooling.capture.generated_binary_profiles import (
    PROFILES,
    match_profile,
    target,
)
from research.tooling.capture.retail_capture_native import (
    native_output_dir,
    run as run_native,
)


CONFIG_NAME = "elysium_cap11_theatre.cfg"
CONFIG_SIGNATURE = "// Generated retained CAP1.1 sp_theatre capture recipe."
PRE_MAP_WAITS = 180


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(8 * 1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def build_config(pre_map_waits: int = PRE_MAP_WAITS) -> str:
    if pre_map_waits < 1:
        raise ValueError("pre-map wait count must be positive")
    lines = [
        CONFIG_SIGNATURE,
        "// Uninstall: delete this file.",
        "echo ELYSIUM_CAP11_BOOT",
        "developer 1",
        "sv_cheats 1",
        "fps_max 30",
        "pausable 0",
        "cl_mouselook 0",
        "cl_mouseenable 0",
        *(["wait"] * pre_map_waits),
        "echo ELYSIUM_CAP11_MAP_SP_THEATRE",
        "map sp_theatre",
        "",
    ]
    return "\n".join(lines)


def _module_paths(game_root: Path) -> dict[str, Path]:
    return {
        "Vampire.exe": game_root / "Vampire.exe",
        "client.dll": game_root / "Vampire" / "cl_dlls" / "client.dll",
        "engine.dll": game_root / "Bin" / "engine.dll",
        "StudioRender.dll": game_root / "Bin" / "StudioRender.dll",
    }


def exact_modules(game_root: Path) -> list[dict[str, object]]:
    modules = []
    paths = _module_paths(game_root)
    expected_names = {str(profile["module"]) for profile in PROFILES}
    if set(paths) != expected_names:
        raise RuntimeError("capture module path list does not match binary profiles")
    for name, path in paths.items():
        if not path.is_file():
            raise FileNotFoundError(path)
        digest = sha256(path)
        profile = match_profile(name, path.stat().st_size, digest)
        modules.append(
            {
                "name": name,
                "path": os.fspath(path.resolve()),
                "file_size": path.stat().st_size,
                "sha256": digest,
                "binary_profile": profile["id"],
            }
        )
    return modules


def _profile(name: str) -> dict[str, object]:
    matches = [profile for profile in PROFILES if profile["module"] == name]
    if len(matches) != 1:
        raise LookupError(f"no unique profile for {name}")
    return matches[0]


def write_hook_ini(path: Path, session: Path, duration_seconds: int) -> None:
    studio = _profile("StudioRender.dll")
    client = _profile("client.dll")
    draw = target(studio, "studiorender.draw_model")
    resolve = target(client, "client.resolve_virtual_model_pose")
    build = target(client, "client.build_transformations")
    get_header = target(client, "client.get_studio_hdr")
    values = {
        "output": session / "scene.elpose",
        "animation_output": session / "animation.elanim",
        "ready": session / "ready.txt",
        "stop": session / "stop.txt",
        "done": session / "done.txt",
        "studiorender_sha256": studio["sha256"],
        "client_sha256": client["sha256"],
        "studio_object_rva": f"0x{draw['vtable']['object_rva']:x}",
        "studio_vtable_rva": f"0x{draw['vtable']['expected_vtable_rva']:x}",
        "draw_model_rva": f"0x{draw['rva']:x}",
        "draw_model_slot": draw["vtable"]["slot"],
        "resolve_virtual_model_pose_rva": f"0x{resolve['rva']:x}",
        "resolve_virtual_model_pose_expected": resolve["expected_bytes"],
        "build_transformations_rva": f"0x{build['rva']:x}",
        "build_transformations_expected": build["expected_bytes"],
        "get_studio_hdr_rva": f"0x{get_header['rva']:x}",
        "target_checksum": "0x00000000",
        "duration_seconds": duration_seconds + 60,
    }
    path.write_text(
        "[capture]\n"
        + "\n".join(f"{key}={value}" for key, value in values.items())
        + "\n",
        encoding="utf-16",
    )


def git_identity(repo_root: Path) -> dict[str, object]:
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


def running_vampire_pids() -> list[int]:
    matches = []
    for process in psutil.process_iter(("pid", "name")):
        try:
            if (process.info["name"] or "").casefold() == "vampire.exe":
                matches.append(int(process.info["pid"]))
        except (psutil.AccessDenied, psutil.NoSuchProcess):
            continue
    return sorted(matches)


def _run_and_log(command: list[str], log_path: Path, cwd: Path) -> int:
    with log_path.open("w", encoding="utf-8", buffering=1) as log:
        process = subprocess.Popen(
            command,
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        assert process.stdout is not None
        for line in process.stdout:
            print(line, end="", flush=True)
            log.write(line)
        return process.wait()


def run(args: argparse.Namespace) -> int:
    repo_root = Path(__file__).resolve().parents[3]
    game_root = vtmb_root().resolve()
    executable = game_root / "Vampire.exe"
    modules = exact_modules(game_root)
    config_root = game_root / "Unofficial_Patch" / "cfg"
    config_path = config_root / CONFIG_NAME
    config_text = build_config(args.pre_map_waits)
    if args.dry_run:
        print(config_text, end="")
        print(f"install_path={config_path}")
        print(f"uninstall_path={config_path}")
        return 0
    if not args.install_config:
        raise ValueError(
            "the theatre launch writes one inert cfg into the VtMB install; "
            "rerun with --install-config (the exact uninstall path is printed)"
        )
    if config_path.exists() and not config_path.read_text(
        encoding="utf-8-sig"
    ).startswith(CONFIG_SIGNATURE):
        raise FileExistsError(f"refusing to overwrite non-Elysium cfg: {config_path}")
    running = running_vampire_pids()
    if running:
        raise RuntimeError(f"vampire.exe is already running: {running}")

    run_native("build", args.config)
    native_output = native_output_dir(args.config)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    session = (
        args.output.resolve()
        if args.output
        else research_root() / "retail-capture" / "theatre" / f"cap11_{stamp}"
    )
    session.mkdir(parents=True, exist_ok=False)
    recipe_path = session / "recipe.cfg"
    recipe_path.write_text(config_text, encoding="ascii")
    config_path.write_text(config_text, encoding="ascii")
    hook = session / "live_pose_hook.dll"
    shutil.copy2(native_output / "live_pose_hook.dll", hook)
    hook_ini = hook.with_suffix(".ini")
    write_hook_ini(hook_ini, session, args.duration_seconds)

    launch_arguments = [
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
        CONFIG_NAME,
    ]
    supervision = session / "supervision.txt"
    launch = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "tool_git": git_identity(repo_root),
        "map": "sp_theatre",
        "capture_duration_seconds": args.duration_seconds,
        "pre_map_waits": args.pre_map_waits,
        "executable": os.fspath(executable),
        "modules": modules,
        "launch_arguments": launch_arguments,
        "installed_config": os.fspath(config_path),
        "uninstall": [os.fspath(config_path)],
        "retail_exit_code": None,
    }
    launch_path = session / "launch.json"
    launch_path.write_text(json.dumps(launch, indent=2) + "\n", encoding="utf-8")

    command = [
        os.fspath(native_output / "retail_launcher.exe"),
        "--executable",
        os.fspath(executable),
        "--working-directory",
        os.fspath(game_root),
        "--distribution",
        "owner-configured",
        "--startup-profile",
        "unofficial-patch",
        "--probe-host",
        os.fspath(native_output / "retail_probe_host.dll"),
        "--capture-hook",
        os.fspath(hook),
        "--capture-stop",
        os.fspath(session / "stop.txt"),
        "--capture-done",
        os.fspath(session / "done.txt"),
        "--finalization",
        os.fspath(supervision),
        "--timeout-ms",
        str(args.duration_seconds * 1000),
        "--normal-exit-code",
        "1",
        "--supervise",
        "--",
        *launch_arguments[2:],
    ]
    print(f"install={config_path}", flush=True)
    print(f"uninstall={config_path}", flush=True)
    print(f"session={session}", flush=True)
    result = _run_and_log(command, session / "launcher.log", game_root)
    launch["retail_exit_code"] = result
    launch_path.write_text(json.dumps(launch, indent=2) + "\n", encoding="utf-8")

    finalization = read_key_values(supervision)
    hook_done = read_key_values(session / "done.txt")
    if finalization.get("capture_done") != "1" or hook_done.get("complete") != "1":
        raise RuntimeError(
            "capture hook did not flush cleanly: "
            f"supervision={finalization.get('reason')} hook={hook_done}"
        )
    database_report = finalize(
        session,
        retain_temporary_streams=args.retain_temporary_streams,
    )
    clean = (
        int(hook_done.get("dropped", "0")) == 0
        and not any(database_report["incomplete_tail_bytes"].values())
    )
    result_path = session / "result.json"
    result_document = {
        "complete": clean,
        "supervision": finalization,
        "hook": hook_done,
        **database_report,
    }
    result_path.write_text(
        json.dumps(result_document, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(result_document, indent=2), flush=True)
    return 0 if clean else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-config", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--duration-seconds", type=int, default=240)
    parser.add_argument("--pre-map-waits", type=int, default=PRE_MAP_WAITS)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--retain-temporary-streams", action="store_true")
    args = parser.parse_args()
    if args.duration_seconds < 15 or args.duration_seconds > 600:
        parser.error("--duration-seconds must be between 15 and 600")
    if args.pre_map_waits < 1 or args.pre_map_waits > 1800:
        parser.error("--pre-map-waits must be between 1 and 1800")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
