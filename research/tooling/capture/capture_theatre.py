"""Launch a hook-first retail run and retain one SQLite capture.

This tool installs one inert cfg into the selected Unofficial Patch only when
``--install-config`` is explicit. The native launcher creates ``Vampire.exe``
suspended, bootstraps the exact-build probe, preloads the skeletal hook, and
only then resumes retail. The cfg waits before issuing its entry command so the
client and StudioRender hooks are armed before map resources begin loading.

``--map`` selects one of the recipes below. Everything a scene binds -- entry
command, console markers, stop signals, run-zero rule and durations -- lives in
its ``Recipe``; the launch, hook and finalization paths are scene-independent.

``sp_theatre`` is a cutscene from end to end. A console ``map`` load spawns the
player short of the unnamed arrival trigger that starts it, so the operator
walks onto that trigger once; every stage after it is authored. The run stops
when the cutscene ends by loading ``sp_tutorial_1``.

``sp_tutorial_1`` is entered from an owner save placed immediately before the
closing gunfight, so the operator plays the fight and the dialog after it and
the authored transition to ``sm_pawnshop_1`` stops the run. It is the corpus
that reaches the four-cell blend path: every 3x3 grid in the game is a weapon
aim layer autolayered from a ranged activity, which no cutscene enters.
"""

from __future__ import annotations

import argparse
import ctypes
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
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


PRE_MAP_WAITS = 180
# A property of retail_launcher.cpp's --timeout-ms ceiling, not of any scene.
MAXIMUM_DURATION_SECONDS = 900
CONSOLE_LOG_RELATIVE = Path("logs") / "console.log"
CONSOLE_LOG_ROOTS = (Path("."), Path("Unofficial_Patch"), Path("Vampire"))
# psutil terminates with SIGTERM semantics on Windows, so the watcher's own
# shutdown surfaces as this exit code and is a normal end of run.
WATCHER_EXIT_CODE = 15


@dataclass(frozen=True)
class Recipe:
    """Everything one captured scene binds that the instrument does not."""

    name: str
    config_name: str
    config_signature: str
    boot_marker: str
    map_marker: str
    # ``{save}`` is substituted from --save; a template without it takes none.
    entry_template: str
    # An authored console line proving the scene started. ``None`` means the
    # scene starts where it loads, so the map marker is the only sound zero.
    arm_marker: str | None
    # Console substrings proving retail left this scene, and transition-state
    # files named after it. Two independent signals for the same event.
    stop_console_tokens: tuple[str, ...]
    stop_state_globs: tuple[str, ...]
    # Opt-in only, via --allow-operator-stop, so a hand-ended run never claims
    # the authored ending.
    operator_stop_token: str | None
    # Bound to keys rather than typed, because a beat is marked mid-fight.
    beat_binds: tuple[tuple[str, str], ...] = ()
    run_zero_rule: str = "map_load_batch"
    session_root: str = "theatre"
    session_prefix: str = "cap11"
    scene_seconds: int = 370
    probe_seconds: int = 90
    backstop_seconds: int = 720
    operator_notes: tuple[str, ...] = ()

    @property
    def markers(self) -> tuple[str, ...]:
        return (self.boot_marker, self.map_marker)

    @property
    def requires_save(self) -> bool:
        return "{save}" in self.entry_template

    def entry_command(self, save: str | None) -> str:
        if not self.requires_save:
            if save:
                raise ValueError(f"recipe {self.name} takes no --save")
            return self.entry_template
        if not save:
            raise ValueError(f"recipe {self.name} requires --save <save-name>")
        return self.entry_template.format(save=console_token(save, "save"))

    def beat_markers(self) -> tuple[str, ...]:
        return tuple(marker for _, marker in self.beat_binds)

    def stop_tokens(self, allow_operator_stop: bool) -> tuple[str, ...]:
        if allow_operator_stop and self.operator_stop_token:
            return (*self.stop_console_tokens, self.operator_stop_token)
        return self.stop_console_tokens


THEATRE = Recipe(
    name="sp_theatre",
    config_name="elysium_cap11_theatre.cfg",
    config_signature="// Generated retained CAP1.1 sp_theatre capture recipe.",
    boot_marker="ELYSIUM_CAP11_BOOT",
    map_marker="ELYSIUM_CAP11_MAP_SP_THEATRE",
    entry_template="map sp_theatre",
    # One of the arrival trigger's own OnStartTouch outputs, logged once. The
    # console map load places the player short of that trigger, so this is when
    # the cutscene starts and is the only sound zero for comparing two runs.
    arm_marker="Setting Variable: G.Story_State = -4",
    stop_console_tokens=("sp_tutorial_1",),
    stop_state_globs=("sp_theatre.HL*",),
    operator_stop_token=None,
    run_zero_rule="player_cast_batch",
    session_root="theatre",
    session_prefix="cap11",
    scene_seconds=370,
    probe_seconds=90,
    # host_framerate pins the simulation step, not the wall clock, so a run
    # that renders below 30 fps stretches the same cutscene over more
    # wall-clock time. An unfocused window halves the rate and VtMB has no cvar
    # or launch switch to stop that, so the backstop covers the walk plus the
    # cutscene at the slowest observed rate with margin, not the authored
    # length.
    backstop_seconds=720,
    operator_notes=(
        "when the map marker appears, walk forward onto the arrival trigger. "
        "Take as long as you like: the run stamps the trigger instant, so a "
        "slow walk only lengthens the idle prefix. The cutscene then runs "
        "itself.",
    ),
)


TUTORIAL = Recipe(
    name="sp_tutorial_1",
    config_name="elysium_cap27_tutorial.cfg",
    config_signature="// Generated retained CAP2.7 sp_tutorial_1 capture recipe.",
    boot_marker="ELYSIUM_CAP27_BOOT",
    map_marker="ELYSIUM_CAP27_LOAD_SP_TUTORIAL_1",
    entry_template="load {save}",
    # The save is placed inside the scene, so there is no authored line
    # separating arrival from start; the load is the zero.
    arm_marker=None,
    stop_console_tokens=("sm_pawnshop_1",),
    stop_state_globs=("sp_tutorial_1.HL*",),
    operator_stop_token="ELYSIUM_CAP27_STOP",
    beat_binds=(
        ("F6", "ELYSIUM_CAP27_BEAT_PLAYER_AIM"),
        ("F7", "ELYSIUM_CAP27_BEAT_PLAYER_FIRE"),
        ("F8", "ELYSIUM_CAP27_BEAT_FIGHT"),
        ("F9", "ELYSIUM_CAP27_BEAT_DIALOG"),
        ("F12", "ELYSIUM_CAP27_STOP"),
    ),
    run_zero_rule="map_load_batch",
    session_root="tutorial",
    session_prefix="cap27",
    scene_seconds=300,
    probe_seconds=120,
    backstop_seconds=420,
    operator_notes=(
        "F6 sweeps the player's own aim: thirdperson, ranged weapon drawn, "
        "mouse through its full yaw and pitch range before provoking anything. "
        "That is what reaches a 3x3 grid; the fight's one ranged NPC is "
        "corroboration.",
        "F7 fires while aiming off centre, F8 opens the fight, F9 opens the "
        "dialog. The transition to sm_pawnshop_1 stops the run.",
        "god on for the whole run: dying enters a path this recipe does not "
        "describe.",
    ),
)


RECIPES = {recipe.name: recipe for recipe in (THEATRE, TUTORIAL)}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(8 * 1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def console_token(value: str, label: str) -> str:
    """Reject anything that would not survive one console line intact."""
    if not re.fullmatch(r"[A-Za-z0-9_.@-]+", value):
        raise ValueError(f"{label} is not a safe console token: {value!r}")
    return value


def build_config(
    recipe: Recipe,
    pre_map_waits: int = PRE_MAP_WAITS,
    save: str | None = None,
) -> str:
    if pre_map_waits < 1:
        raise ValueError("pre-map wait count must be positive")
    entry = recipe.entry_command(save)
    lines = [
        recipe.config_signature,
        "// Uninstall: delete this file.",
        f"echo {recipe.boot_marker}",
        "developer 1",
        "sv_cheats 1",
        "fps_max 30",
        # A fixed simulation step makes every actor sample exactly 30 times per
        # game-second, which is what makes the captured rates comparable.
        "host_framerate 0.033333333",
        "host_timescale 1",
        # The operator alt-tabs to this terminal mid-run; pausing there would
        # stall the capture against the backstop.
        "pausable 0",
        # A beat is marked while the operator is fighting, so it is a keypress
        # rather than a typed line. These never persist: the watcher terminates
        # retail instead of issuing `quit`, so host_writeconfig never runs.
        *(
            f'bind "{key}" "echo {marker}"'
            for key, marker in recipe.beat_binds
        ),
        *(["wait"] * pre_map_waits),
        f"echo {recipe.map_marker}",
        entry,
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


def transition_state_files(game_root: Path, recipe: Recipe) -> set[str]:
    save_root = game_root / "Unofficial_Patch" / "Save"
    if not save_root.is_dir():
        return set()
    return {
        path.name
        for pattern in recipe.stop_state_globs
        for path in save_root.glob(pattern)
    }


def arm_signal(recipe: Recipe, console_text: str) -> bool:
    """Whether the console proves this run's scene has started.

    The map marker is this run's own echo, so requiring it first keeps any
    surviving earlier log from arming the run, and requiring the arm marker
    *after* it keeps a stale trigger line from doing the same.
    """
    if recipe.map_marker not in console_text:
        return False
    if recipe.arm_marker is None:
        return True
    return recipe.arm_marker in console_text.split(recipe.map_marker, 1)[1]


def beat_marks(recipe: Recipe, console_text: str) -> list[str]:
    """The recipe's beat markers the console shows, in first-sighting order."""
    if recipe.map_marker not in console_text:
        return []
    tail = console_text.split(recipe.map_marker, 1)[1]
    seen = [
        (tail.index(marker), marker)
        for marker in recipe.beat_markers()
        if marker in tail
    ]
    return [marker for _, marker in sorted(seen)]


def transition_signal(
    game_root: Path,
    recipe: Recipe,
    baseline: set[str] | None,
    allow_operator_stop: bool = False,
) -> str | None:
    """Name the signal proving retail left this scene, or ``None``.

    Retail writes ``<map>.HL*`` transition state when a ``trigger_changelevel``
    hands off, so a file the run did not start with is independent evidence of
    the same event the console log reports.

    An operator-issued stop is named apart from the authored two, so a run
    ended by hand never claims an ending the scene did not reach.

    ``baseline`` is ``None`` until the scene is up. A ``load`` recipe restores
    transition state as part of loading its save, so a set snapshotted before
    launch would read those files as a transition and stop the capture at load.
    """
    _, text = read_console_log(game_root)
    if any(token in text for token in recipe.stop_console_tokens):
        return "console-log"
    if baseline is not None and transition_state_files(game_root, recipe) - baseline:
        return "map-transition-state"
    if (
        allow_operator_stop
        and recipe.operator_stop_token
        and recipe.operator_stop_token in text
    ):
        return "operator-console-token"
    return None


def _game_dll(game_root: Path) -> Path:
    """Resolve the server game DLL the process maps, patch-first.

    The patch supplies the launcher rather than a game DLL, so this normally
    falls through to the base install; writing the search order out is what
    keeps that a choice rather than an accident. The profile hash-gates
    whichever file it returns, and the wrapped ``vampire.dll.12`` sibling the
    installer wrote is never a candidate because it is a different filename.
    """
    patched = game_root / "Unofficial_Patch" / "dlls" / "vampire.dll"
    if patched.is_file():
        return patched
    return game_root / "Vampire" / "dlls" / "vampire.dll"


def save_identity(game_root: Path, save: str | None) -> dict[str, object] | None:
    """Resolve and hash the save a ``load`` recipe names.

    A save that is not there makes the run load nothing and capture an idle
    main menu, which reads as a clean but empty capture. Failing here, with the
    names that do exist, costs the operator one line instead of one run.
    """
    if not save:
        return None
    save_root = game_root / "Unofficial_Patch" / "Save"
    matches = sorted(save_root.glob(f"{save}.*")) if save_root.is_dir() else []
    if not matches:
        present = sorted(path.name for path in save_root.glob("*")) if (
            save_root.is_dir()
        ) else []
        raise FileNotFoundError(
            f"no save named {save!r} under {save_root}; present: {present}"
        )
    return {
        "name": save,
        "files": [
            {
                "path": os.fspath(path),
                "file_size": path.stat().st_size,
                "sha256": sha256(path),
            }
            for path in matches
        ],
    }


def _module_paths(game_root: Path) -> dict[str, Path]:
    return {
        "Vampire.exe": game_root / "Vampire.exe",
        "vampire.dll": _game_dll(game_root),
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
    decode_quaternion = target(client, "client.decode_bone_quaternion")
    decode_position = target(client, "client.decode_bone_position")
    vampire = _profile("vampire.dll")
    scene_targets = {
        name: target(vampire, f"vampire.{name}")
        for name in (
            "scene_start_playback",
            "scene_on_finished",
            "scene_cancel_playback",
            "scene_dispatch_start_event",
            "scene_find_named_entity",
            "scene_apply_anim_set",
        )
    }
    scene_targets["maintain_sequence_transitions"] = target(
        client, "client.maintain_sequence_transitions")
    values = {
        "output": session / "scene.elpose",
        "animation_output": session / "animation.elanim",
        "census_output": session / "model.elmdl",
        "actor_output": session / "actor.elact",
        "contribution_output": session / "contribution.elcon",
        "scene_output": session / "scene.elscn",
        "ready": session / "ready.txt",
        "stop": session / "stop.txt",
        "done": session / "done.txt",
        "studiorender_sha256": studio["sha256"],
        "client_sha256": client["sha256"],
        "vampire_sha256": vampire["sha256"],
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
        "decode_bone_quaternion_rva": f"0x{decode_quaternion['rva']:x}",
        "decode_bone_quaternion_expected": decode_quaternion["expected_bytes"],
        "decode_bone_position_rva": f"0x{decode_position['rva']:x}",
        "decode_bone_position_expected": decode_position["expected_bytes"],
        "target_checksum": "0x00000000",
        "duration_seconds": duration_seconds + 60,
    }
    for name, entry in scene_targets.items():
        values[f"{name}_rva"] = f"0x{entry['rva']:x}"
        values[f"{name}_expected"] = entry["expected_bytes"]
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

    def __init__(
        self,
        game_root: Path,
        session: Path,
        probe: bool,
        recipe: Recipe,
        allow_operator_stop: bool = False,
    ) -> None:
        self._game_root = game_root
        self._stop_path = session / "stop.txt"
        self._done_path = session / "done.txt"
        self._probe = probe
        self._recipe = recipe
        self._allow_operator_stop = allow_operator_stop
        # Taken when the scene comes up, not here: see transition_signal.
        self._baseline: set[str] | None = None
        self._finished = threading.Event()
        self._started = time.monotonic()
        self._thread = threading.Thread(target=self._poll, daemon=True)
        self.signal: str | None = None
        self.seconds: float | None = None
        self.arm_qpc: int | None = None
        self.arm_seconds: float | None = None
        self.qpc_frequency: int | None = None
        self.beats: list[dict[str, object]] = []

    def __enter__(self) -> "TransitionWatcher":
        self._thread.start()
        return self

    def __exit__(self, *_: object) -> None:
        self._finished.set()

    def _poll(self) -> None:
        while not self._finished.wait(0.25):
            _, console_text = read_console_log(self._game_root)
            if (
                self._baseline is None
                and self._recipe.map_marker in console_text
            ):
                self._baseline = transition_state_files(
                    self._game_root, self._recipe)
            if self.arm_qpc is None and arm_signal(self._recipe, console_text):
                self.arm_qpc, self.qpc_frequency = performance_counter()
                self.arm_seconds = round(time.monotonic() - self._started, 3)
                print(
                    f"scene armed at {self.arm_seconds}s (qpc={self.arm_qpc})",
                    flush=True,
                )
            self._stamp_beats(console_text)
            if self._probe:
                continue
            signal = transition_signal(
                self._game_root,
                self._recipe,
                self._baseline,
                self._allow_operator_stop,
            )
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

    def _stamp_beats(self, console_text: str) -> None:
        """Stamp each beat marker once, the first time the console shows it.

        A beat is the operator naming a phase of the run. It is kept apart from
        ``arm_qpc``, which means the scene's own start instant: feeding a
        keystroke into that field would make every alignment downstream a
        measurement of the operator rather than of the scene.
        """
        stamped = {beat["marker"] for beat in self.beats}
        for marker in beat_marks(self._recipe, console_text):
            if marker in stamped:
                continue
            counter, frequency = performance_counter()
            self.beats.append(
                {
                    "marker": marker,
                    "qpc": counter,
                    "qpc_frequency": frequency,
                    "seconds": round(time.monotonic() - self._started, 3),
                }
            )
            print(f"beat {marker} at {self.beats[-1]['seconds']}s", flush=True)

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
    recipe = RECIPES[args.map]
    game_root = vtmb_root().resolve()
    executable = game_root / "Vampire.exe"
    modules = exact_modules(game_root)
    config_root = game_root / "Unofficial_Patch" / "cfg"
    config_path = config_root / recipe.config_name
    config_text = build_config(recipe, args.pre_map_waits, args.save)
    if args.dry_run:
        print(config_text, end="")
        print(f"install_path={config_path}")
        print(f"uninstall_path={config_path}")
        return 0
    if not args.install_config:
        raise ValueError(
            "the capture launch writes one inert cfg into the VtMB install; "
            "rerun with --install-config (the exact uninstall path is printed)"
        )
    if config_path.exists() and not config_path.read_text(
        encoding="utf-8-sig"
    ).startswith(recipe.config_signature):
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
        else (
            research_root()
            / "retail-capture"
            / recipe.session_root
            / f"{recipe.session_prefix}_{stamp}"
        )
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
        recipe.config_name,
    ]
    supervision = session / "supervision.txt"
    launch = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "tool_git": git_identity(repo_root),
        "map": recipe.name,
        "recipe": recipe.name,
        # The offline analyzers pick their run-zero rule from this rather than
        # from the map name, so a recipe states its own rule.
        "run_zero_rule": recipe.run_zero_rule,
        "entry_command": recipe.entry_command(args.save),
        "save": save_identity(game_root, args.save),
        "probe": bool(args.probe),
        "allow_operator_stop": bool(args.allow_operator_stop),
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
        f"when '{recipe.map_marker}' appears, the scene is up (~"
        f"{recipe.scene_seconds}s). "
        + " ".join(recipe.operator_notes)
        + " Leave the game window focused for the whole run: an unfocused "
        "window halves the frame rate, which stretches the run toward the "
        "backstop without changing what it plays. "
        + (
            f"This probe stops at its {args.duration_seconds}s backstop."
            if args.probe
            else "Capture stops when "
            + " or ".join(recipe.stop_tokens(args.allow_operator_stop))
            + " appears."
        ),
        flush=True,
    )
    with TransitionWatcher(
        game_root,
        session,
        args.probe,
        recipe,
        args.allow_operator_stop,
    ) as watcher:
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
        "recipe": recipe.name,
        "run_zero_rule": recipe.run_zero_rule,
        "arm_marker": recipe.arm_marker,
        "arm_qpc": watcher.arm_qpc,
        "arm_seconds": watcher.arm_seconds,
        "qpc_frequency": watcher.qpc_frequency,
        "signal": watcher.signal,
        "seconds": watcher.seconds,
        # Operator beats name phases of the run. They are kept apart from the
        # arm stamp, which means the scene's own start.
        "beats": watcher.beats,
        "console_log": (
            os.fspath(console_source) if console_source is not None else None
        ),
        "markers": [
            marker for marker in recipe.markers if marker in console_text
        ],
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
        and list(boundary["markers"]) == list(recipe.markers)
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
    parser.add_argument(
        "--map",
        choices=sorted(RECIPES),
        default=THEATRE.name,
        help="which capture recipe to run",
    )
    parser.add_argument(
        "--save",
        help="save name a load-based recipe enters its scene through",
    )
    parser.add_argument("--install-config", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--duration-seconds", type=int)
    parser.add_argument(
        "--probe",
        action="store_true",
        help=(
            "Short run over the scene. Measures rates and proves the hook "
            "flushes without reaching the authored ending."
        ),
    )
    parser.add_argument(
        "--allow-operator-stop",
        action="store_true",
        help=(
            "Accept this recipe's operator stop token as an ending. Recorded "
            "as its own signal, so the run never claims the authored one."
        ),
    )
    parser.add_argument("--pre-map-waits", type=int, default=PRE_MAP_WAITS)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--retain-temporary-streams", action="store_true")
    args = parser.parse_args()
    recipe = RECIPES[args.map]
    if recipe.requires_save and not args.save:
        parser.error(f"--map {recipe.name} requires --save <save-name>")
    if args.save and not recipe.requires_save:
        parser.error(f"--map {recipe.name} takes no --save")
    if args.allow_operator_stop and not recipe.operator_stop_token:
        parser.error(f"--map {recipe.name} declares no operator stop token")
    if args.duration_seconds is None:
        args.duration_seconds = (
            recipe.probe_seconds if args.probe else recipe.backstop_seconds
        )
    if args.duration_seconds < 15 or args.duration_seconds > MAXIMUM_DURATION_SECONDS:
        parser.error(
            f"--duration-seconds must be between 15 and {MAXIMUM_DURATION_SECONDS}"
        )
    if args.pre_map_waits < 1 or args.pre_map_waits > 1800:
        parser.error("--pre-map-waits must be between 1 and 1800")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
