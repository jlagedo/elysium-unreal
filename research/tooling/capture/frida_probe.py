"""Run opt-in Frida discovery probes against controlled IA-32 targets.

The retained native capture remains the acceptance instrument. This tool is an
observational discovery collector that reuses the same binary profiles and
retail launcher, and writes all evidence below ELYSIUM_WORK_ROOT/research.

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
    if recipe.get("mode", "callers") not in ("callers", "trace"):
        raise ValueError(f"invalid Frida recipe mode: {path}")
    if int(recipe.get("max_trace_events", 4096)) not in range(1, 1048577):
        raise ValueError(f"invalid max_trace_events: {path}")
    if not isinstance(recipe.get("capture_returns", False), bool):
        raise ValueError(f"invalid capture_returns: {path}")
    _validate_field_reads(recipe, path)
    _validate_on_change(recipe, path)
    _validate_sample(recipe, path)
    _validate_max_depth(recipe, path)
    return recipe


FIELD_TYPES = (
    "u8", "i32", "u32", "f32", "ptr", "vec3", "cstr", "f32array", "u32array",
)
ARRAY_TYPES = ("f32array", "u32array")
FIELD_BASES = ("module", "register", "argument", "return")


def _integer_field(value: object, label: str) -> int:
    if isinstance(value, str):
        return int(value, 0)
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    raise ValueError(f"{label} must be an integer")


def _validate_field_reads(recipe: dict[str, Any], path: Path) -> None:
    reads = recipe.get("field_reads", {})
    if not isinstance(reads, dict):
        raise ValueError(f"invalid Frida field reads: {path}")
    known = set(recipe["targets"]) | {
        declaration["label"] for declaration in recipe.get("export_hooks", [])
    }
    stack_words = int(recipe.get("stack_words", 8))
    for target, declarations in reads.items():
        if target not in known:
            raise ValueError(
                f"field reads name an undeclared target {target!r}: {path}"
            )
        if not isinstance(declarations, list) or not declarations:
            raise ValueError(f"invalid field-read list for {target}: {path}")
        labels: set[str] = set()
        for field in declarations:
            if not isinstance(field, dict):
                raise ValueError(f"invalid field read for {target}: {path}")
            label = field.get("label")
            if not isinstance(label, str) or not label:
                raise ValueError(f"invalid field-read label for {target}: {path}")
            if label in labels:
                raise ValueError(f"duplicate field-read label {label}: {path}")
            labels.add(label)
            if field.get("type") not in FIELD_TYPES:
                raise ValueError(f"invalid field-read type for {label}: {path}")
            if field.get("when", "enter") not in ("enter", "leave"):
                raise ValueError(f"invalid field-read phase for {label}: {path}")
            base = field.get("base")
            if base not in FIELD_BASES:
                raise ValueError(f"invalid field-read base for {label}: {path}")
            if field["type"] == "cstr":
                field["max_length"] = _integer_field(
                    field.get("max_length", 128), f"field read {label} max_length"
                )
                if not 1 <= field["max_length"] <= 1024:
                    raise ValueError(
                        f"field read {label} names a string bound outside "
                        f"1..1024: {path}"
                    )
            elif "max_length" in field:
                raise ValueError(
                    f"field read {label} bounds a value that is not a string: {path}"
                )
            if field["type"] in ARRAY_TYPES:
                # An array read walks memory, so its ceiling is mandatory and its
                # length must be stated exactly once -- a constant, or the label of
                # a field decoded earlier in the same phase. A declaration carrying
                # both would leave the reader to choose, and one carrying neither
                # would read whatever the ceiling allows on every call.
                field["max_count"] = _integer_field(
                    field.get("max_count"), f"field read {label} max_count"
                )
                if not 1 <= field["max_count"] <= 8192:
                    raise ValueError(
                        f"field read {label} names an array ceiling outside "
                        f"1..8192: {path}"
                    )
                has_count = "count" in field
                has_source = "count_from" in field
                if has_count == has_source:
                    raise ValueError(
                        f"field read {label} needs exactly one of `count` and "
                        f"`count_from`: {path}"
                    )
                if has_count:
                    field["count"] = _integer_field(
                        field["count"], f"field read {label} count"
                    )
                    if not 0 <= field["count"] <= field["max_count"]:
                        raise ValueError(
                            f"field read {label} names a count outside its own "
                            f"ceiling: {path}"
                        )
                else:
                    source = field["count_from"]
                    if not isinstance(source, str):
                        raise ValueError(
                            f"field read {label} names an invalid count source: {path}"
                        )
                    # The reader decodes a phase in declaration order and resolves the
                    # count out of what it has already decoded, so a source declared
                    # after this field -- or in the other phase -- reads as absent and
                    # loses the whole array to an error value rather than to a refusal.
                    phase = field.get("when", "enter")
                    earlier = {
                        other["label"]
                        for other in declarations[:declarations.index(field)]
                        if other.get("when", "enter") == phase
                    }
                    if source not in earlier:
                        raise ValueError(
                            f"field read {label} counts from {source!r}, which is not "
                            f"declared before it in the {phase} phase: {path}"
                        )
                if "count_scale" in field:
                    field["count_scale"] = _integer_field(
                        field["count_scale"], f"field read {label} count_scale"
                    )
                    if not 1 <= field["count_scale"] <= 64:
                        raise ValueError(
                            f"field read {label} scales its count outside 1..64: {path}"
                        )
                if "decimals" in field:
                    field["decimals"] = _integer_field(
                        field["decimals"], f"field read {label} decimals"
                    )
                    if not 0 <= field["decimals"] <= 9:
                        raise ValueError(
                            f"field read {label} rounds outside 0..9 decimals: {path}"
                        )
            elif any(key in field for key in ("count", "count_from", "max_count")):
                raise ValueError(
                    f"field read {label} counts a value that is not an array: {path}"
                )
            if base == "return":
                # The return value only exists on the way out, so a declaration
                # that reads it anywhere else would decode a stale register.
                if field.get("when") != "leave":
                    raise ValueError(
                        f"field read {label} reads the return value outside "
                        f"the leave phase: {path}"
                    )
            elif base == "module":
                if not isinstance(field.get("module"), str) or not field["module"]:
                    raise ValueError(f"field read {label} needs a module: {path}")
                field["rva"] = _integer_field(
                    field.get("rva"), f"field read {label} rva"
                )
            elif base == "register":
                if field.get("register") not in (
                    "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp"
                ):
                    raise ValueError(f"invalid field-read register for {label}: {path}")
            else:
                index = _integer_field(field.get("index"), f"field read {label} index")
                if not 0 <= index < stack_words:
                    raise ValueError(
                        f"field read {label} names argument {index} outside the "
                        f"{stack_words} captured stack words: {path}"
                    )
                field["index"] = index
            # The agent adds these to a pointer, so they reach it as numbers
            # rather than the hexadecimal strings a recipe is written in.
            field["offset"] = _integer_field(
                field.get("offset", 0), f"field read {label} offset"
            )
            field["deref"] = [
                _integer_field(step, f"field read {label} deref step")
                for step in field.get("deref", [])
            ]


_CHANGE_WORD = re.compile(r"^word(\d+)$")


def _validate_on_change(recipe: dict[str, Any], path: Path) -> None:
    """A change key names the values whose tuple is the target's answer.

    Every name must resolve to something the record actually carries: a
    declared field-read label, `ecx`, `return_value`, or `word<N>` inside the
    captured stack window. A key that names nothing decodable would silently
    dedupe on a constant and swallow the capture.
    """
    declared = dict(recipe.get("on_change", {}))
    census = recipe.get("on_first", {})
    if not isinstance(recipe.get("on_change", {}), dict) or not isinstance(census, dict):
        raise ValueError(f"invalid Frida on-change declaration: {path}")
    overlap = sorted(set(declared) & set(census))
    if overlap:
        raise ValueError(
            f"a target declares both on_change and on_first "
            f"({', '.join(overlap)}): {path}"
        )
    declared.update(census)
    if declared and recipe.get("mode") != "trace":
        raise ValueError(f"on-change keys require trace mode: {path}")
    known = set(recipe["targets"]) | {
        declaration["label"] for declaration in recipe.get("export_hooks", [])
    }
    stack_words = int(recipe.get("stack_words", 8))
    reads = recipe.get("field_reads", {})
    for target, names in declared.items():
        if target not in known:
            raise ValueError(
                f"on-change names an undeclared target {target!r}: {path}"
            )
        if not isinstance(names, list) or not names:
            raise ValueError(f"invalid on-change key for {target}: {path}")
        if len(set(names)) != len(names):
            raise ValueError(f"duplicate on-change key part for {target}: {path}")
        labels = {field["label"] for field in reads.get(target, [])}
        leave = {
            field["label"]
            for field in reads.get(target, [])
            if field.get("when", "enter") == "leave"
        }
        # A key part read at leave is only decidable once the call returns, and the agent defers
        # the whole record when it sees one. Both halves have to agree, so a target that reaches
        # here with a leave-phase key part but no `capture_returns` would key on a value that is
        # not there yet -- which reads as a constant and suppresses every distinct answer.
        if leave & set(names) and not recipe.get("capture_returns"):
            raise ValueError(
                f"on-change key for {target} names the leave-phase field(s) "
                f"{sorted(leave & set(names))!r}, which cannot be read without "
                f"capture_returns: {path}"
            )
        for name in names:
            if not isinstance(name, str) or not name:
                raise ValueError(f"invalid on-change key part for {target}: {path}")
            if name in ("ecx", "return_value") or name in labels:
                continue
            word = _CHANGE_WORD.fullmatch(name)
            if word is None:
                raise ValueError(
                    f"on-change key {name!r} for {target} names neither a "
                    f"declared field, ecx, return_value, nor a stack word: {path}"
                )
            if not 0 <= int(word.group(1)) < stack_words:
                raise ValueError(
                    f"on-change key {name!r} for {target} falls outside the "
                    f"{stack_words} captured stack words: {path}"
                )


def _validate_sample(recipe: dict[str, Any], path: Path) -> None:
    """A sampled target must be a census, never an ordered one.

    Sampling drops calls outright, so a target whose ORDER is the answer would be recorded with
    holes that read as absences. Requiring a census key is what makes that a refusal rather
    than a silently thinned trace.
    """
    declared = recipe.get("sample", {})
    if not isinstance(declared, dict):
        raise ValueError(f"invalid Frida sample declaration: {path}")
    known = set(recipe["targets"]) | {
        declaration["label"] for declaration in recipe.get("export_hooks", [])
    }
    census = set(recipe.get("on_first", {}))
    for target, rate in declared.items():
        if target not in known:
            raise ValueError(f"sample names an undeclared target {target!r}: {path}")
        if not isinstance(rate, int) or isinstance(rate, bool) or not 1 <= rate <= 4096:
            raise ValueError(f"invalid sample rate for {target}: {path}")
        if rate != 1 and target not in census:
            raise ValueError(
                f"sampled target {target!r} carries no on_first key, so its order is its "
                f"answer and sampling would punch holes in it: {path}"
            )


def _validate_max_depth(recipe: dict[str, Any], path: Path) -> None:
    """Keep recursive calls out without thinning the retained top-level order."""
    declared = recipe.get("max_depth", {})
    if not isinstance(declared, dict):
        raise ValueError(f"invalid Frida max-depth declaration: {path}")
    known = set(recipe["targets"]) | {
        declaration["label"] for declaration in recipe.get("export_hooks", [])
    }
    if declared and recipe.get("mode") != "trace":
        raise ValueError(f"max-depth filters require trace mode: {path}")
    for target, depth in declared.items():
        if target not in known:
            raise ValueError(
                f"max-depth filter names an undeclared target {target!r}: {path}"
            )
        value = _integer_field(depth, f"max depth for {target}")
        if not 0 <= value <= 64:
            raise ValueError(f"max depth for {target} lies outside 0..64: {path}")
        declared[target] = value


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
            if value.get("kind") in ("caller", "call"):
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
        if args.recipe != "smoke":
            raise ValueError(
                "the supervised smoke launcher passes the fixture recipe to its "
                f"collector and cannot run {args.recipe!r}"
            )
        return supervised_smoke(args)
    frida = _load_frida()
    recipe = load_recipe(args.recipe)
    output = _session_root("smoke", args.recipe)
    recorder = Recorder(output)
    manifest = _manifest_base("smoke", args.recipe, recipe, frida)
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


def _wait_for_attach_stop(
    detached: threading.Event,
    duration_seconds: float,
    stop_file: Path | None,
) -> str:
    deadline = time.monotonic() + duration_seconds
    while True:
        if stop_file is not None and stop_file.is_file():
            return "stop-file"
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return "duration"
        if detached.wait(min(0.1, remaining)):
            return "detached"


def attach(args: argparse.Namespace) -> int:
    frida = _load_frida()
    recipe = load_recipe(args.recipe)
    stop_file = args.stop_file.resolve() if args.stop_file is not None else None
    if stop_file is not None and stop_file.exists():
        raise FileExistsError(f"Frida attach stop file already exists: {stop_file}")
    output = _session_root("attach", args.recipe)
    recorder = Recorder(output)
    manifest = _manifest_base("attach", args.recipe, recipe, frida)
    manifest["pid"] = args.pid
    manifest["stop_file"] = os.fspath(stop_file) if stop_file is not None else None
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
        manifest["stop_reason"] = _wait_for_attach_stop(
            recorder.detached,
            args.duration_seconds,
            stop_file,
        )
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
    collector_arguments = [
        os.fspath(Path(__file__).resolve()),
        "collect",
        "--recipe",
        args.recipe,
        "--output",
        os.fspath(output),
        "--validate-retail",
    ]
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
        "--finalization",
        os.fspath(finalization),
    ]
    command.extend(
        f"--collector-argument={value}" for value in collector_arguments
    )
    for value in args.target_argument:
        command.extend(("--target-argument", value))
    print(f"Frida retail session: {output}", flush=True)
    return subprocess.run(command, check=False).returncode


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)

    smoke_parser = subparsers.add_parser("smoke")
    smoke_parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    smoke_parser.add_argument("--recipe", default="smoke")
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
    attach_parser.add_argument(
        "--stop-file",
        type=Path,
        help="Detach cleanly when this externally-created file appears.",
    )
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
