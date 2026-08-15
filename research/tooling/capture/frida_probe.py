"""Run opt-in Frida discovery probes against controlled IA-32 targets.

The retained native capture remains the acceptance instrument. This tool is a
read-only discovery collector that reuses the same binary profiles and retail
launcher, and writes all evidence below ELYSIUM_WORK_ROOT/research.

Usage:
    uv run elysium research frida_probe smoke
    uv run elysium research frida_probe attach --pid 1234 --recipe smoke
    uv run elysium research frida_probe launch --recipe cap2_8_callers
"""

from __future__ import annotations

import argparse
import copy
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time
from typing import Any

from elysium_pipeline.paths import research_root, vtmb_root
from research.tooling.capture.generated_binary_profiles import REGISTRY
from research.tooling.capture.retail_capture_native import (
    native_output_dir,
    run as run_native,
)


ROOT = Path(__file__).resolve().parent
FRIDA_ROOT = ROOT / "frida"
AGENT_PATH = FRIDA_ROOT / "agent.js"
RECIPE_ROOT = FRIDA_ROOT / "recipes"
RECIPE_SCHEMA = "elysium.research.frida.recipe.v1"
MANIFEST_SCHEMA = "elysium.research.frida.session.v1"
EVENT_SCHEMA = "elysium.research.frida.events.v1"
SAFE_RECIPE = re.compile(r"^[a-z0-9_]+$")

SYNCHRONIZE = 0x00100000
EVENT_MODIFY_STATE = 0x0002
WAIT_OBJECT_0 = 0
WAIT_TIMEOUT = 258


def _load_frida():
    try:
        import frida  # type: ignore[import-not-found]
    except ImportError as exc:
        raise RuntimeError(
            "Frida is not installed; run `uv sync --extra research-frida`"
        ) from exc
    return frida


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _atomic_json(path: Path, value: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def load_recipe(name: str) -> dict[str, Any]:
    if SAFE_RECIPE.fullmatch(name) is None:
        raise ValueError(f"invalid Frida recipe name: {name!r}")
    path = RECIPE_ROOT / f"{name}.json"
    if not path.is_file():
        raise FileNotFoundError(f"Frida recipe does not exist: {path}")
    recipe = json.loads(path.read_text(encoding="utf-8"))
    if recipe.get("schema") != RECIPE_SCHEMA or recipe.get("name") != name:
        raise ValueError(f"invalid Frida recipe identity: {path}")
    targets = recipe.get("targets")
    expected = recipe.get("expected_modules")
    if not isinstance(targets, list) or not all(
        isinstance(value, str) and value for value in targets
    ):
        raise ValueError(f"invalid Frida recipe targets: {path}")
    if not isinstance(expected, list) or not all(
        isinstance(value, str) and value for value in expected
    ):
        raise ValueError(f"invalid Frida expected modules: {path}")
    exports = recipe.get("export_hooks", [])
    if not isinstance(exports, list) or not all(
        isinstance(value, dict)
        and all(
            isinstance(value.get(field), str) and value[field]
            for field in ("module", "export", "label")
        )
        for value in exports
    ):
        raise ValueError(f"invalid Frida export hooks: {path}")
    expected_callers = recipe.get("expected_callers", [])
    if not isinstance(expected_callers, list) or not all(
        isinstance(value, str) and value for value in expected_callers
    ):
        raise ValueError(f"invalid Frida expected callers: {path}")
    if len(set(targets)) != len(targets):
        raise ValueError(f"duplicate Frida recipe target: {path}")
    if int(recipe.get("max_unique_callers", 256)) not in range(1, 4097):
        raise ValueError(f"invalid max_unique_callers: {path}")
    if int(recipe.get("stack_words", 8)) not in range(0, 65):
        raise ValueError(f"invalid stack_words: {path}")
    return recipe


def _target_profiles(
    recipe: dict[str, Any],
    *,
    validate_retail: bool,
) -> list[dict[str, Any]]:
    requested = set(recipe["targets"])
    if not requested:
        return []
    if not validate_retail:
        raise ValueError("a hook recipe requires exact retail module validation")

    from research.tooling.capture.capture_theatre import exact_modules

    approved = {
        str(item["binary_profile"]): item
        for item in exact_modules(vtmb_root())
    }
    profiles: list[dict[str, Any]] = []
    found: set[str] = set()
    for source in REGISTRY["profiles"]:
        targets = [
            target
            for target in source["targets"]
            if target["semantic_label"] in requested
        ]
        if not targets:
            continue
        approval = approved.get(source["id"])
        if approval is None:
            raise RuntimeError(
                f"binary profile was not approved from disk: {source['id']}"
            )
        profile = copy.deepcopy(source)
        profile["targets"] = targets
        profile["approved_path"] = approval["path"]
        profiles.append(profile)
        found.update(target["semantic_label"] for target in targets)
    missing = sorted(requested - found)
    if missing:
        raise ValueError(f"unknown Frida recipe targets: {', '.join(missing)}")
    return profiles


def agent_config(
    recipe: dict[str, Any],
    *,
    validate_retail: bool,
) -> dict[str, Any]:
    return {
        "schema": RECIPE_SCHEMA,
        "require_ia32": True,
        "recipe": recipe,
        "profiles": _target_profiles(
            recipe,
            validate_retail=validate_retail,
        ),
    }


def _session_root(mode: str, recipe: str) -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    path = research_root() / "frida" / f"{stamp}-{mode}-{recipe}"
    path.mkdir(parents=True, exist_ok=False)
    return path


class Recorder:
    def __init__(self, output: Path) -> None:
        output.mkdir(parents=True, exist_ok=True)
        self.output = output
        self._stream = (output / "events.jsonl").open(
            "w", encoding="utf-8", buffering=1
        )
        self._lock = threading.Lock()
        self.observed_modules: set[str] = set()
        self.observed_callers: set[str] = set()
        self.warnings: list[str] = []
        self.failures: list[str] = []
        self.detached = threading.Event()
        self.detach_reason: str | None = None

    def record(self, value: dict[str, Any]) -> None:
        with self._lock:
            self._stream.write(json.dumps(value, sort_keys=True) + "\n")
            if value.get("kind") == "module_added":
                name = value.get("name")
                if isinstance(name, str):
                    self.observed_modules.add(name.casefold())
            if value.get("kind") == "caller":
                target = value.get("target")
                if isinstance(target, str):
                    self.observed_callers.add(target)
            if value.get("kind") == "warning":
                warning = json.dumps(value, sort_keys=True)
                self.warnings.append(warning)
                print(f"WARNING - Frida agent: {warning}", file=sys.stderr)
            if value.get("kind") == "failure":
                failure = json.dumps(value, sort_keys=True)
                self.failures.append(failure)
                print(f"WARNING - Frida agent failure: {failure}", file=sys.stderr)

    def on_message(self, message: dict[str, Any], data: bytes | None) -> None:
        if message.get("type") == "send" and isinstance(message.get("payload"), dict):
            payload = dict(message["payload"])
            if data is not None:
                payload["binary_bytes"] = len(data)
            self.record(payload)
            return
        failure = {
            "schema": EVENT_SCHEMA,
            "kind": "warning",
            "operation": "agent_message",
            "message": message,
            "binary_bytes": len(data) if data is not None else 0,
        }
        self.record(failure)

    def on_detached(self, reason: str, crash: Any = None) -> None:
        self.detach_reason = str(reason)
        self.record(
            {
                "schema": EVENT_SCHEMA,
                "kind": "detached",
                "reason": self.detach_reason,
                "crash": str(crash) if crash is not None else None,
            }
        )
        self.detached.set()

    def wait_for_modules(self, expected: list[str], timeout: float) -> bool:
        wanted = {name.casefold() for name in expected}
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self._lock:
                if wanted <= self.observed_modules:
                    return True
            if self.detached.wait(0.025):
                break
        with self._lock:
            return wanted <= self.observed_modules

    def close(self) -> None:
        with self._lock:
            self._stream.close()


def _start_agent(
    frida: Any,
    pid: int,
    config: dict[str, Any],
    recorder: Recorder,
) -> tuple[Any, Any, dict[str, Any]]:
    session = frida.get_local_device().attach(pid)
    session.on("detached", recorder.on_detached)
    script = session.create_script(AGENT_PATH.read_text(encoding="utf-8"))
    script.on("message", recorder.on_message)
    script.load()
    ready = script.exports_sync.initialize(config)
    if ready.get("arch") != "ia32" or ready.get("pointer_size") != 4:
        raise RuntimeError(f"Frida attached with an unexpected target ABI: {ready}")
    return session, script, ready


def _stop_agent(session: Any, script: Any) -> dict[str, Any] | None:
    summary = None
    try:
        summary = script.exports_sync.stop()
    except Exception as exc:
        if "script has been destroyed" not in str(exc).casefold():
            print(f"WARNING - Frida agent stop failed: {exc}", file=sys.stderr)
    try:
        session.detach()
    except Exception as exc:
        if "session is detached" not in str(exc).casefold():
            print(f"WARNING - Frida detach failed: {exc}", file=sys.stderr)
    return summary


def _manifest_base(
    mode: str,
    recipe_name: str,
    recipe: dict[str, Any],
    frida: Any,
) -> dict[str, Any]:
    return {
        "schema": MANIFEST_SCHEMA,
        "complete": False,
        "mode": mode,
        "recipe": recipe_name,
        "recipe_sha256": _sha256(RECIPE_ROOT / f"{recipe_name}.json"),
        "agent_sha256": _sha256(AGENT_PATH),
        "frida_version": getattr(frida, "__version__", "unknown"),
        "started_at": datetime.now(timezone.utc).isoformat(),
        "targets": recipe["targets"],
        "expected_modules": recipe["expected_modules"],
    }


def smoke(args: argparse.Namespace) -> int:
    if args.supervised:
        return supervised_smoke(args)
    frida = _load_frida()
    recipe = load_recipe("smoke")
    output = _session_root("smoke", "smoke")
    recorder = Recorder(output)
    manifest = _manifest_base("smoke", "smoke", recipe, frida)
    device = frida.get_local_device()
    pid: int | None = None
    session = None
    try:
        run_native("build", args.config)
        executable = native_output_dir(args.config) / "vampire.exe"
        if not executable.is_file():
            raise FileNotFoundError(executable)
        argv = [
            os.fspath(executable),
            "--initial-delay-ms",
            "0",
            "--module-delay-ms",
            "50",
            "--lifetime-ms",
            "750",
        ]
        pid = device.spawn(argv)
        manifest.update({"pid": pid, "executable": os.fspath(executable)})
        session, script, ready = _start_agent(
            frida,
            pid,
            agent_config(recipe, validate_retail=False),
            recorder,
        )
        manifest["ready"] = ready
        device.resume(pid)
        if not recorder.wait_for_modules(recipe["expected_modules"], 10.0):
            missing = sorted(
                {name.casefold() for name in recipe["expected_modules"]}
                - recorder.observed_modules
            )
            raise RuntimeError(
                f"synthetic Frida smoke missed modules: {', '.join(missing)}"
            )
        if not recorder.detached.wait(10.0):
            raise TimeoutError("synthetic Frida target did not exit")
        missing_callers = sorted(
            set(recipe.get("expected_callers", [])) - recorder.observed_callers
        )
        if missing_callers:
            raise RuntimeError(
                f"synthetic Frida smoke missed callers: {', '.join(missing_callers)}"
            )
        manifest["complete"] = True
        manifest["detach_reason"] = recorder.detach_reason
        print(f"Frida IA-32 smoke passed: {output}")
        return 0
    finally:
        if pid is not None and not recorder.detached.is_set():
            if session is not None:
                try:
                    session.detach()
                except Exception as exc:
                    print(f"WARNING - Frida smoke detach failed: {exc}", file=sys.stderr)
            try:
                device.kill(pid)
            except Exception as exc:
                print(f"WARNING - Frida smoke cleanup failed: {exc}", file=sys.stderr)
        manifest["finished_at"] = datetime.now(timezone.utc).isoformat()
        manifest["observed_modules"] = sorted(recorder.observed_modules)
        manifest["observed_callers"] = sorted(recorder.observed_callers)
        manifest["warnings"] = recorder.warnings
        manifest["failures"] = recorder.failures
        _atomic_json(output / "manifest.json", manifest)
        recorder.close()


def supervised_smoke(args: argparse.Namespace) -> int:
    _load_frida()
    recipe = load_recipe("smoke")
    output = _session_root("supervised-smoke", "smoke")
    run_native("build", args.config)
    native_output = native_output_dir(args.config)
    executable = native_output / "vampire.exe"
    launcher = native_output / "retail_launcher.exe"
    probe_host = native_output / "retail_probe_host.dll"
    finalization = output / "supervision.txt"
    command = [
        os.fspath(launcher),
        "--executable",
        os.fspath(executable),
        "--working-directory",
        os.fspath(native_output),
        "--distribution",
        "synthetic-test",
        "--startup-profile",
        "direct",
        "--probe-host",
        os.fspath(probe_host),
        "--collector",
        sys.executable,
        "--collector-argument",
        os.fspath(Path(__file__).resolve()),
        "--collector-argument",
        "collect",
        "--collector-argument",
        "--recipe",
        "--collector-argument",
        "smoke",
        "--collector-argument",
        "--output",
        "--collector-argument",
        os.fspath(output),
        "--finalization",
        os.fspath(finalization),
        "--timeout-ms",
        "5000",
        "--normal-exit-code",
        "0",
        "--supervise",
        "--",
        "--initial-delay-ms",
        "0",
        "--module-delay-ms",
        "50",
        "--lifetime-ms",
        "750",
    ]
    completed = subprocess.run(command, check=False)
    if completed.returncode != 0:
        raise RuntimeError(
            f"supervised Frida smoke launcher failed: {completed.returncode}"
        )
    manifest_path = output / "manifest.json"
    if not manifest_path.is_file() or not finalization.is_file():
        raise RuntimeError("supervised Frida smoke did not finalize its reports")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    observed = {str(name).casefold() for name in manifest.get("observed_modules", [])}
    missing = {
        name.casefold() for name in recipe["expected_modules"]
    } - observed
    missing_callers = set(recipe.get("expected_callers", [])) - set(
        manifest.get("observed_callers", [])
    )
    finalization_text = finalization.read_text(encoding="utf-8")
    if not manifest.get("complete") or missing or missing_callers:
        raise RuntimeError(
            "supervised Frida smoke capture is incomplete"
            + (f"; missing {', '.join(sorted(missing))}" if missing else "")
            + (
                f"; missing callers {', '.join(sorted(missing_callers))}"
                if missing_callers
                else ""
            )
        )
    if "collector_ready=1" not in finalization_text:
        raise RuntimeError("supervised Frida smoke never reached collector-ready")
    print(f"Supervised Frida IA-32 smoke passed: {output}")
    return 0


class NamedEvents:
    def __init__(self, stop_name: str, ready_name: str) -> None:
        if os.name != "nt":
            raise RuntimeError("the supervised Frida collector requires Windows")
        self.kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel32.OpenEventW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
        self.kernel32.OpenEventW.restype = wintypes.HANDLE
        self.kernel32.SetEvent.argtypes = [wintypes.HANDLE]
        self.kernel32.SetEvent.restype = wintypes.BOOL
        self.kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.kernel32.WaitForSingleObject.restype = wintypes.DWORD
        self.kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel32.CloseHandle.restype = wintypes.BOOL
        self.stop = self.kernel32.OpenEventW(SYNCHRONIZE, False, stop_name)
        self.ready = self.kernel32.OpenEventW(EVENT_MODIFY_STATE, False, ready_name)
        if not self.stop or not self.ready:
            error = ctypes.get_last_error()
            self.close()
            raise OSError(error, "cannot open Frida collector supervision events")

    def signal_ready(self) -> None:
        if not self.kernel32.SetEvent(self.ready):
            raise ctypes.WinError(ctypes.get_last_error())

    def stop_requested(self, timeout_ms: int) -> bool:
        result = self.kernel32.WaitForSingleObject(self.stop, timeout_ms)
        if result == WAIT_OBJECT_0:
            return True
        if result == WAIT_TIMEOUT:
            return False
        raise OSError(result, "Frida collector stop-event wait failed")

    def close(self) -> None:
        if getattr(self, "stop", None):
            self.kernel32.CloseHandle(self.stop)
            self.stop = None
        if getattr(self, "ready", None):
            self.kernel32.CloseHandle(self.ready)
            self.ready = None


def collect(args: argparse.Namespace) -> int:
    frida = _load_frida()
    recipe = load_recipe(args.recipe)
    output = args.output.resolve()
    recorder = Recorder(output)
    manifest = _manifest_base("collector", args.recipe, recipe, frida)
    manifest["pid"] = args.target_pid
    events = NamedEvents(args.stop_event, args.ready_event)
    session = None
    script = None
    try:
        config = agent_config(recipe, validate_retail=args.validate_retail)
        session, script, ready = _start_agent(
            frida,
            args.target_pid,
            config,
            recorder,
        )
        manifest["ready"] = ready
        events.signal_ready()
        print(
            f"frida-collector-v1 event=ready target_pid={args.target_pid}",
            flush=True,
        )
        while not recorder.detached.is_set():
            if events.stop_requested(100):
                manifest["summary"] = _stop_agent(session, script)
                session = None
                script = None
                break
        manifest["complete"] = not recorder.failures
        manifest["detach_reason"] = recorder.detach_reason
        return 0 if manifest["complete"] else 2
    finally:
        if session is not None and script is not None and not recorder.detached.is_set():
            manifest["summary"] = _stop_agent(session, script)
        events.close()
        manifest["finished_at"] = datetime.now(timezone.utc).isoformat()
        manifest["observed_modules"] = sorted(recorder.observed_modules)
        manifest["observed_callers"] = sorted(recorder.observed_callers)
        manifest["warnings"] = recorder.warnings
        manifest["failures"] = recorder.failures
        _atomic_json(output / "manifest.json", manifest)
        recorder.close()


def attach(args: argparse.Namespace) -> int:
    frida = _load_frida()
    recipe = load_recipe(args.recipe)
    output = _session_root("attach", args.recipe)
    recorder = Recorder(output)
    manifest = _manifest_base("attach", args.recipe, recipe, frida)
    manifest["pid"] = args.pid
    session = None
    script = None
    try:
        session, script, ready = _start_agent(
            frida,
            args.pid,
            agent_config(recipe, validate_retail=bool(recipe["targets"])),
            recorder,
        )
        manifest["ready"] = ready
        recorder.detached.wait(args.duration_seconds)
        if not recorder.detached.is_set():
            manifest["summary"] = _stop_agent(session, script)
            session = None
            script = None
        manifest["complete"] = not recorder.failures
        manifest["detach_reason"] = recorder.detach_reason
        print(f"Frida attach session: {output}")
        return 0 if manifest["complete"] else 2
    finally:
        if session is not None and script is not None and not recorder.detached.is_set():
            manifest["summary"] = _stop_agent(session, script)
        manifest["finished_at"] = datetime.now(timezone.utc).isoformat()
        manifest["observed_modules"] = sorted(recorder.observed_modules)
        manifest["observed_callers"] = sorted(recorder.observed_callers)
        manifest["warnings"] = recorder.warnings
        manifest["failures"] = recorder.failures
        _atomic_json(output / "manifest.json", manifest)
        recorder.close()


def launch(args: argparse.Namespace) -> int:
    recipe = load_recipe(args.recipe)
    if not recipe["targets"]:
        raise ValueError("retail launch requires a recipe with at least one target")
    output = _session_root("launch", args.recipe)
    finalization = output / "supervision.txt"
    launcher = ROOT / "retail_capture_launch.py"
    command = [
        sys.executable,
        os.fspath(launcher),
        "--distribution",
        "owner-configured",
        "--startup-profile",
        args.startup_profile,
        "--config",
        args.config,
        "--run",
        "--timeout-seconds",
        str(args.timeout_seconds),
        "--collector",
        sys.executable,
        "--collector-argument",
        os.fspath(Path(__file__).resolve()),
        "--collector-argument",
        "collect",
        "--collector-argument",
        "--recipe",
        "--collector-argument",
        args.recipe,
        "--collector-argument",
        "--output",
        "--collector-argument",
        os.fspath(output),
        "--collector-argument",
        "--validate-retail",
        "--finalization",
        os.fspath(finalization),
    ]
    for value in args.target_argument:
        command.extend(("--target-argument", value))
    print(f"Frida retail session: {output}", flush=True)
    return subprocess.run(command, check=False).returncode


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)

    smoke_parser = subparsers.add_parser("smoke")
    smoke_parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    smoke_parser.add_argument(
        "--supervised",
        action="store_true",
        help="Run through the retained launcher and collector readiness seam.",
    )
    smoke_parser.set_defaults(function=smoke)

    attach_parser = subparsers.add_parser("attach")
    attach_parser.add_argument("--pid", type=int, required=True)
    attach_parser.add_argument("--recipe", default="smoke")
    attach_parser.add_argument("--duration-seconds", type=float, default=30.0)
    attach_parser.set_defaults(function=attach)

    collect_parser = subparsers.add_parser("collect")
    collect_parser.add_argument("--recipe", required=True)
    collect_parser.add_argument("--output", type=Path, required=True)
    collect_parser.add_argument("--validate-retail", action="store_true")
    collect_parser.add_argument("--stop-event", required=True)
    collect_parser.add_argument("--ready-event", required=True)
    collect_parser.add_argument("--target-pid", type=int, required=True)
    collect_parser.set_defaults(function=collect)

    launch_parser = subparsers.add_parser("launch")
    launch_parser.add_argument("--recipe", required=True)
    launch_parser.add_argument(
        "--startup-profile",
        choices=("direct", "unofficial-patch", "unofficial-patch-save"),
        default="unofficial-patch-save",
    )
    launch_parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    launch_parser.add_argument("--timeout-seconds", type=int, default=300)
    launch_parser.add_argument("--target-argument", action="append", default=[])
    launch_parser.set_defaults(function=launch)
    return result


def main() -> int:
    args = parser().parse_args()
    if getattr(args, "pid", 1) <= 0 or getattr(args, "target_pid", 1) <= 0:
        raise ValueError("target PID must be positive")
    if not 0 < getattr(args, "duration_seconds", 1.0) <= 600:
        raise ValueError("duration must be in (0, 600]")
    if not 0 < getattr(args, "timeout_seconds", 1) <= 600:
        raise ValueError("timeout must be in (0, 600]")
    return int(args.function(args))


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:
        print(f"WARNING - Frida probe failed: {exc}", file=sys.stderr, flush=True)
        raise SystemExit(1)
