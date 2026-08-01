"""Launch a hook-first ``sp_theatre`` run and retain one SQLite capture.

This tool installs one inert cfg into the selected Unofficial Patch only when
``--install-config`` is explicit. The native launcher creates ``Vampire.exe``
suspended, bootstraps the exact-build probe, preloads the skeletal hook, and
only then resumes retail. The cfg waits before issuing ``map sp_theatre`` so
the client and StudioRender hooks are armed before map resources begin loading.

``sp_theatre`` is a cutscene from end to end. A console ``map`` load spawns the
player short of the unnamed arrival trigger that starts it, so the operator
walks onto that trigger once; every stage after it is authored. The run stops
when the cutscene ends by loading ``sp_tutorial_1``.
"""

from __future__ import annotations

import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
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
BOOT_MARKER = "ELYSIUM_CAP11_BOOT"
MAP_MARKER = "ELYSIUM_CAP11_MAP_SP_THEATRE"
MARKERS = (BOOT_MARKER, MAP_MARKER)
TRANSITION_MAP = "sp_tutorial_1"
# One of the arrival trigger's own OnStartTouch outputs, logged once. The
# console map load places the player on that trigger, so this is when the
# cutscene starts and is the only sound zero for comparing two runs.
ARM_MARKER = "Setting Variable: G.Story_State = -4"
CUTSCENE_SECONDS = 370
PROBE_SECONDS = 90
# host_framerate pins the simulation step, not the wall clock, so a run that
# renders below 30 fps stretches the same cutscene over more wall-clock time.
# An unfocused window halves the rate and VtMB has no cvar or launch switch to
# stop that, so the backstop covers the walk plus the cutscene at the slowest
# observed rate with margin, not the authored length.
BACKSTOP_SECONDS = 720
MAXIMUM_DURATION_SECONDS = 900
CONSOLE_LOG_RELATIVE = Path("logs") / "console.log"
CONSOLE_LOG_ROOTS = (Path("."), Path("Unofficial_Patch"), Path("Vampire"))
# psutil terminates with SIGTERM semantics on Windows, so the watcher's own
# shutdown surfaces as this exit code and is a normal end of run.
WATCHER_EXIT_CODE = 15


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
        f"echo {BOOT_MARKER}",
        "developer 1",
        "sv_cheats 1",
        "fps_max 30",
        # A fixed simulation step makes every actor sample exactly 30 times per
        # game-second, which is what makes the captured rates comparable.
        "host_framerate 0.033333333",
        "host_timescale 1",
        # The operator alt-tabs to this terminal mid-cutscene; pausing there
        # would stall the capture against the backstop.
        "pausable 0",
        *(["wait"] * pre_map_waits),
        f"echo {MAP_MARKER}",
        "map sp_theatre",
        "",
    ]
    return "\n".join(lines)


def console_log_candidates(game_root: Path) -> list[Path]:
    return [game_root / root / CONSOLE_LOG_RELATIVE for root in CONSOLE_LOG_ROOTS]


def read_console_log(game_root: Path) -> tuple[Path | None, str]:
    for candidate in console_log_candidates(game_root):
        if not candidate.is_file():
            continue
        try:
            return candidate, candidate.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return candidate, ""
    return None, ""


def performance_counter() -> tuple[int, int]:
    """Read the same system counter the hook stamps on every record.

    ``QueryPerformanceCounter`` is machine-wide, so a value read here is
    directly comparable to ``records.qpc`` without correlating clocks.
    """
    counter = ctypes.c_int64()
    frequency = ctypes.c_int64()
    kernel32 = ctypes.windll.kernel32
    kernel32.QueryPerformanceCounter(ctypes.byref(counter))
    kernel32.QueryPerformanceFrequency(ctypes.byref(frequency))
    return counter.value, frequency.value


def transition_state_files(game_root: Path) -> set[str]:
    save_root = game_root / "Unofficial_Patch" / "Save"
    if not save_root.is_dir():
        return set()
    return {path.name for path in save_root.glob("sp_theatre.HL*")}


def transition_signal(game_root: Path, baseline: set[str]) -> str | None:
    """Name the signal proving retail left ``sp_theatre``, or ``None``.

    Retail writes ``<map>.HL*`` transition state when a ``trigger_changelevel``
    hands off, so a file the run did not start with is independent evidence of
    the same event the console log reports.
    """
    _, text = read_console_log(game_root)
    if TRANSITION_MAP in text:
        return "console-log"
    if transition_state_files(game_root) - baseline:
        return "map-transition-state"
    return None


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
    setup_bones = target(client, "client.setup_bones")
    engine = _profile("engine.dll")
    model_render = target(engine, "engine.model_render_draw_model")
    model_render_shadow = target(
        engine, "engine.model_render_draw_model_shadow")
    construct = target(client, "client.base_entity_construct")
    destruct = target(client, "client.base_entity_destruct")
    evaluate_sequence = target(client, "client.evaluate_sequence_pose")
    decode_bones = target(client, "client.decode_selected_bones")
    blend_axis = target(client, "client.resolve_blend_axis_weight")
    values = {
        "output": session / "scene.elpose",
        "animation_output": session / "animation.elanim",
        "census_output": session / "model.elmdl",
        "actor_output": session / "actor.elact",
        "contribution_output": session / "contribution.elcon",
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
        "setup_bones_rva": f"0x{setup_bones['rva']:x}",
        "setup_bones_expected": setup_bones["expected_bytes"],
        "model_render_draw_model_rva": f"0x{model_render['rva']:x}",
        "model_render_draw_model_expected": model_render["expected_bytes"],
        "model_render_draw_model_shadow_rva": (
            f"0x{model_render_shadow['rva']:x}"
        ),
        "model_render_draw_model_shadow_expected": (
            model_render_shadow["expected_bytes"]
        ),
        "base_entity_construct_rva": f"0x{construct['rva']:x}",
        "base_entity_construct_expected": construct["expected_bytes"],
        "base_entity_destruct_rva": f"0x{destruct['rva']:x}",
        "base_entity_destruct_expected": destruct["expected_bytes"],
        "evaluate_sequence_pose_rva": f"0x{evaluate_sequence['rva']:x}",
        "evaluate_sequence_pose_expected": evaluate_sequence["expected_bytes"],
        "decode_selected_bones_rva": f"0x{decode_bones['rva']:x}",
        "decode_selected_bones_expected": decode_bones["expected_bytes"],
        "resolve_blend_axis_weight_rva": f"0x{blend_axis['rva']:x}",
        "resolve_blend_axis_weight_expected": blend_axis["expected_bytes"],
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


class TransitionWatcher:
    """Bracket the cutscene: stamp where it starts, stop where it ends.

    The arm stamp is a raw performance counter, so analysis aligns two runs on
    the trigger instant rather than on wall clock. Touching ``stop.txt`` is
    what makes the hook drain, flush and close its streams and write
    ``done.txt``; retail is only terminated afterwards, so ending the run at
    the boundary never costs a partial stream.
    """

    def __init__(self, game_root: Path, session: Path, probe: bool) -> None:
        self._game_root = game_root
        self._stop_path = session / "stop.txt"
        self._done_path = session / "done.txt"
        self._probe = probe
        self._baseline = transition_state_files(game_root)
        self._finished = threading.Event()
        self._started = time.monotonic()
        self._thread = threading.Thread(target=self._poll, daemon=True)
        self.signal: str | None = None
        self.seconds: float | None = None
        self.arm_qpc: int | None = None
        self.arm_seconds: float | None = None
        self.qpc_frequency: int | None = None

    def __enter__(self) -> "TransitionWatcher":
        self._thread.start()
        return self

    def __exit__(self, *_: object) -> None:
        self._finished.set()

    def _poll(self) -> None:
        while not self._finished.wait(0.25):
            _, console_text = read_console_log(self._game_root)
            # The map marker is this run's own echo, so requiring it first
            # keeps any surviving earlier log from arming the run.
            if (
                self.arm_qpc is None
                and MAP_MARKER in console_text
                and ARM_MARKER in console_text.split(MAP_MARKER, 1)[1]
            ):
                self.arm_qpc, self.qpc_frequency = performance_counter()
                self.arm_seconds = round(time.monotonic() - self._started, 3)
                print(
                    f"cutscene armed at {self.arm_seconds}s (qpc={self.arm_qpc})",
                    flush=True,
                )
            if self._probe:
                continue
            signal = transition_signal(self._game_root, self._baseline)
            if signal is None:
                continue
            self.signal = signal
            self.seconds = round(time.monotonic() - self._started, 3)
            print(
                f"transition={signal} after {self.seconds}s; "
                "flushing the capture",
                flush=True,
            )
            self._stop_path.touch()
            while not self._finished.wait(0.5):
                if self._done_path.is_file():
                    break
            self._terminate_retail()
            return

    def _terminate_retail(self) -> None:
        for pid in running_vampire_pids():
            try:
                psutil.Process(pid).terminate()
            except (psutil.AccessDenied, psutil.NoSuchProcess):
                continue


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

    # Retail resolves `logs/console.log` against a root it does not create, so
    # every candidate directory has to exist before `-condebug` can write. A
    # log left by an earlier run has to go: the watcher arms on a line the
    # previous run also wrote, and would match it before retail reopens the
    # file.
    console_candidates = console_log_candidates(game_root)
    for candidate in console_candidates:
        candidate.parent.mkdir(parents=True, exist_ok=True)
        candidate.unlink(missing_ok=True)
    launch_arguments = [
        "-game",
        "Unofficial_Patch",
        "-dev",
        "-console",
        "-condebug",
        "-conclearlog",
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
        "probe": bool(args.probe),
        "capture_duration_seconds": args.duration_seconds,
        "pre_map_waits": args.pre_map_waits,
        "executable": os.fspath(executable),
        "modules": modules,
        "launch_arguments": launch_arguments,
        "installed_config": os.fspath(config_path),
        "uninstall": [
            os.fspath(config_path),
            *(os.fspath(candidate) for candidate in console_candidates),
        ],
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
        "--normal-exit-code",
        str(WATCHER_EXIT_CODE),
        "--supervise",
        "--",
        *launch_arguments[2:],
    ]
    print(f"install={config_path}", flush=True)
    print(f"uninstall={config_path}", flush=True)
    for candidate in console_candidates:
        print(f"uninstall={candidate}", flush=True)
    print(f"session={session}", flush=True)
    print(
        f"when '{MAP_MARKER}' appears, walk forward onto the arrival trigger. "
        "Take as long as you like: the run stamps the trigger instant, so a "
        f"slow walk only lengthens the idle prefix. The cutscene then runs "
        f"itself (~{CUTSCENE_SECONDS}s). Leave the game window focused for the "
        "whole run: an unfocused window halves the frame rate, which stretches "
        "the cutscene toward the backstop without changing what it plays. "
        + (
            f"This probe stops at its {args.duration_seconds}s backstop."
            if args.probe
            else f"Capture stops when {TRANSITION_MAP} loads."
        ),
        flush=True,
    )
    with TransitionWatcher(game_root, session, args.probe) as watcher:
        result = _run_and_log(command, session / "launcher.log", game_root)
    launch["retail_exit_code"] = result
    launch_path.write_text(json.dumps(launch, indent=2) + "\n", encoding="utf-8")

    console_source, console_text = read_console_log(game_root)
    if console_source is not None:
        (session / "console.log").write_text(console_text, encoding="utf-8")

    finalization = read_key_values(supervision)
    hook_done = read_key_values(session / "done.txt")
    if finalization.get("capture_done") != "1" or hook_done.get("complete") != "1":
        raise RuntimeError(
            "capture hook did not flush cleanly: "
            f"supervision={finalization.get('reason')} hook={hook_done}"
        )
    boundary = {
        "probe": bool(args.probe),
        "arm_marker": ARM_MARKER,
        "arm_qpc": watcher.arm_qpc,
        "arm_seconds": watcher.arm_seconds,
        "qpc_frequency": watcher.qpc_frequency,
        "signal": watcher.signal,
        "seconds": watcher.seconds,
        "console_log": (
            os.fspath(console_source) if console_source is not None else None
        ),
        "markers": [marker for marker in MARKERS if marker in console_text],
        "python_errors": [
            line
            for line in console_text.splitlines()
            if "Traceback" in line or "Error:" in line
        ],
    }
    # The finalizer archives this file, so the run zero travels inside the
    # evidence database instead of only in the result roll-up beside it.
    (session / "boundary.json").write_text(
        json.dumps(boundary, indent=2) + "\n",
        encoding="utf-8",
    )
    database_report = finalize(
        session,
        retain_temporary_streams=args.retain_temporary_streams,
    )
    clean = (
        int(hook_done.get("dropped", "0")) == 0
        and not any(database_report["incomplete_tail_bytes"].values())
        and list(boundary["markers"]) == list(MARKERS)
        and watcher.arm_qpc is not None
        and (args.probe or watcher.signal is not None)
    )
    result_path = session / "result.json"
    result_document = {
        "complete": clean,
        "boundary": boundary,
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
    parser.add_argument("--duration-seconds", type=int)
    parser.add_argument(
        "--probe",
        action="store_true",
        help=(
            "Short unattended run over the idle theatre. Measures rates and "
            "proves the hook flushes without reaching the cutscene."
        ),
    )
    parser.add_argument("--pre-map-waits", type=int, default=PRE_MAP_WAITS)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--retain-temporary-streams", action="store_true")
    args = parser.parse_args()
    if args.duration_seconds is None:
        args.duration_seconds = PROBE_SECONDS if args.probe else BACKSTOP_SECONDS
    if args.duration_seconds < 15 or args.duration_seconds > MAXIMUM_DURATION_SECONDS:
        parser.error(
            f"--duration-seconds must be between 15 and {MAXIMUM_DURATION_SECONDS}"
        )
    if args.pre_map_waits < 1 or args.pre_map_waits > 1800:
        parser.error("--pre-map-waits must be between 1 and 1800")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
