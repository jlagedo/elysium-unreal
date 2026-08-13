"""Prepare and control the hash-pinned ELGVM1 retail capture.

The tool never changes retail data.  It writes the hook configuration beside
the generated DLL, injects that DLL into an explicitly selected retail
process, and controls capture with marker files below ELYSIUM_WORK_ROOT.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

from elysium_pipeline.paths import research_root, vtmb_root
from research.tooling.capture.generated_binary_profiles import match_profile
from research.tooling.capture.retail_capture_native import (
    native_output_dir,
    run as run_native,
)


MODULES = {
    "vampire.dll": "c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f",
    "client.dll": "e88beae0dd03af06493c71c5e8d87a6993b54e590cb6ad37cd3513c588582870",
    "engine.dll": "9d00b2c1e5edbad052fb0b514634ba54574666e12d1b408c46ce141d51ed2313",
}
MODULE_RELATIVE_PATHS = {
    "vampire.dll": Path("Vampire/dlls/vampire.dll"),
    "client.dll": Path("Vampire/cl_dlls/client.dll"),
    "engine.dll": Path("Bin/engine.dll"),
}

ACTION_SCENARIOS = (
    "tremere-normal-glock",
    "tremere-shield-glock",
    "tremere-normal-m37",
    "tremere-shield-m37",
)
PROJECTION_SCENARIOS = tuple(
    f"projection-p{player}-v{viewmodel}-{aspect}"
    for player in (75, 90)
    for viewmodel in (54, 68)
    for aspect in ("4x3", "16x9")
)
SCENARIOS = (*ACTION_SCENARIOS, *PROJECTION_SCENARIOS, "drawviewmodel-off")


def capture_root() -> Path:
    return research_root() / "first-person-viewmodel" / "capture"


def module_path(name: str) -> Path:
    return vtmb_root() / MODULE_RELATIVE_PATHS[name]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verified_modules() -> dict[str, dict[str, object]]:
    result: dict[str, dict[str, object]] = {}
    for name, expected in MODULES.items():
        path = module_path(name)
        if not path.is_file():
            raise FileNotFoundError(path)
        actual = sha256(path)
        if actual != expected:
            raise RuntimeError(
                f"{name} hash mismatch: expected {expected}, got {actual}"
            )
        profile = match_profile(name, path.stat().st_size, actual)
        result[name] = {
            "path": str(path),
            "size": path.stat().st_size,
            "sha256": actual,
            "profile": profile["id"],
        }
    return result


def paths_for(scenario: str) -> dict[str, Path]:
    root = capture_root()
    return {
        "output": root / f"{scenario}.elgvm",
        "ready": root / f"{scenario}.ready",
        "stop": root / f"{scenario}.stop",
        "done": root / f"{scenario}.done",
    }


def recipe(scenario: str) -> list[str]:
    if scenario in ACTION_SCENARIOS:
        weapon = "Glock" if scenario.endswith("glock") else "M37"
        hands = "shield" if "-shield-" in scenario else "normal"
        actions = [
            f"equip {weapon} with Tremere {hands} hands",
            "draw, then hold idle long enough to record multiple frames",
            "fire one live round, then empty the weapon and dry-fire once",
            "perform an ordinary reload",
        ]
        if weapon == "M37":
            actions.append(
                "perform the M37 start, per-shell loop, and finish reload"
            )
        return actions
    if scenario.startswith("projection-"):
        _, player, viewmodel, aspect = scenario.split("-")
        return [
            f"set player FOV to {player[1:]}",
            f"set viewmodel_fov to {viewmodel[1:]}",
            f"set the display to {aspect.replace('x', ':')}",
            "draw Glock and hold idle for at least two rendered frames",
        ]
    return [
        "draw Glock with DrawViewmodel 1 and hold for two frames",
        "set DrawViewmodel 0 and hold for two frames",
        "restore DrawViewmodel 1 and hold for two frames",
    ]


def prepare(scenario: str, configuration: str, overwrite: bool) -> dict[str, object]:
    modules = verified_modules()
    run_native("build", configuration)
    output_dir = native_output_dir(configuration)
    hook = output_dir / "first_person_viewmodel_hook.dll"
    injector = output_dir / "live_pose_injector.exe"
    if not hook.is_file() or not injector.is_file():
        raise FileNotFoundError("native ELGVM1 hook or injector was not built")
    paths = paths_for(scenario)
    capture_root().mkdir(parents=True, exist_ok=True)
    if paths["output"].exists() and not overwrite:
        raise FileExistsError(
            f"capture exists: {paths['output']} (pass --overwrite to replace it)"
        )
    if overwrite:
        for path in paths.values():
            path.unlink(missing_ok=True)
    else:
        for key in ("ready", "stop", "done"):
            paths[key].unlink(missing_ok=True)
    ini = hook.with_suffix(".ini")
    ini.write_text(
        "[capture]\n"
        f"output={paths['output']}\n"
        f"ready={paths['ready']}\n"
        f"stop={paths['stop']}\n"
        f"done={paths['done']}\n"
        f"scenario={scenario}\n"
        f"vampire_sha256={MODULES['vampire.dll']}\n"
        f"client_sha256={MODULES['client.dll']}\n"
        f"engine_sha256={MODULES['engine.dll']}\n"
        "duration_seconds=300\n",
        encoding="utf-8",
    )
    manifest = {
        "schema": "elysium.research.first-person-viewmodel.capture-preparation",
        "version": 1,
        "scenario": scenario,
        "modules": modules,
        "hook": str(hook),
        "injector": str(injector),
        "paths": {key: str(path) for key, path in paths.items()},
        "recipe": recipe(scenario),
    }
    (capture_root() / f"{scenario}.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return manifest


def attach(scenario: str, configuration: str, pid: int | None) -> None:
    output_dir = native_output_dir(configuration)
    command = [
        str(output_dir / "live_pose_injector.exe"),
        str(output_dir / "first_person_viewmodel_hook.dll"),
    ]
    if pid is not None:
        command.append(str(pid))
    subprocess.run(command, check=True)
    ready = paths_for(scenario)["ready"]
    deadline = time.monotonic() + 15.0
    while time.monotonic() < deadline and not ready.is_file():
        time.sleep(0.05)
    if not ready.is_file():
        raise RuntimeError(f"capture hook did not become ready: {ready}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=SCENARIOS)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--pid", type=int)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--attach", action="store_true")
    parser.add_argument("--stop", action="store_true")
    parser.add_argument("--status", action="store_true")
    parser.add_argument("--recipe", action="store_true")
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--list-scenarios", action="store_true")
    args = parser.parse_args()
    if args.list_scenarios:
        print("\n".join(SCENARIOS))
        return 0
    if not args.scenario:
        parser.error("--scenario is required unless --list-scenarios is used")
    if args.pid is not None and args.pid <= 0:
        parser.error("--pid must be positive")
    if not any((args.prepare, args.attach, args.stop, args.status, args.recipe)):
        parser.error("select --prepare, --attach, --stop, --status, or --recipe")
    if args.prepare:
        print(json.dumps(prepare(args.scenario, args.config, args.overwrite), indent=2))
    if args.attach:
        attach(args.scenario, args.config, args.pid)
        print(paths_for(args.scenario)["ready"])
    if args.stop:
        marker = paths_for(args.scenario)["stop"]
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.write_text("stop=1\n", encoding="ascii")
        print(marker)
    if args.status:
        print(
            json.dumps(
                {key: path.exists() for key, path in paths_for(args.scenario).items()},
                indent=2,
                sort_keys=True,
            )
        )
    if args.recipe:
        print("\n".join(f"{index}. {step}" for index, step in enumerate(recipe(args.scenario), 1)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
