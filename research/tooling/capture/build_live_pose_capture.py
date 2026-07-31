"""Build the 32-bit retail whole-scene pose hook and its injector."""

from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess
import sys
from elysium_pipeline.paths import research_root


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT
OUTPUT = research_root() / "live-pose" / "bin"
VSWHERE = (
    Path(r"C:\Program Files (x86)")
    / "Microsoft Visual Studio"
    / "Installer"
    / "vswhere.exe"
)


def visual_studio_environment() -> dict[str, str]:
    query = subprocess.run(
        [
            str(VSWHERE),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-format",
            "json",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    installs = json.loads(query.stdout)
    if not installs:
        raise RuntimeError("Visual Studio C++ x86 tools are not installed")
    vcvars = (
        Path(installs[0]["installationPath"])
        / "VC"
        / "Auxiliary"
        / "Build"
        / "vcvars32.bat"
    )
    result = subprocess.run(
        f'call "{vcvars}" >nul && set',
        check=True,
        capture_output=True,
        text=True,
        shell=True,
    )
    environment: dict[str, str] = {}
    for line in result.stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            environment[key] = value
    return environment


def compile_one(
    compiler: str,
    environment: dict[str, str],
    source: Path,
    output: Path,
    *,
    dll: bool,
) -> None:
    object_path = OUTPUT / f"{source.stem}.obj"
    command = [
        compiler,
        "/nologo",
        "/std:c++17",
        "/O2",
        "/EHsc",
        "/MT",
        "/W4",
        "/WX",
        f"/Fo{object_path}",
        str(source),
        "/link",
        "/INCREMENTAL:NO",
        f"/OUT:{output}",
    ]
    if dll:
        command.insert(6, "/LD")
    subprocess.run(command, check=True, env=environment)


def main() -> int:
    if not VSWHERE.exists():
        raise FileNotFoundError(VSWHERE)
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "contracts" / "generate_record_schemas.py"),
            "--check",
        ],
        check=True,
    )
    OUTPUT.mkdir(parents=True, exist_ok=True)
    environment = visual_studio_environment()
    search_path = next(
        (value for key, value in environment.items() if key.lower() == "path"),
        None,
    )
    compiler = shutil.which("cl.exe", path=search_path)
    if not compiler:
        raise RuntimeError("cl.exe was not exposed by vcvars32.bat")
    compile_one(
        compiler,
        environment,
        SOURCE / "live_pose_hook.cpp",
        OUTPUT / "live_pose_hook.dll",
        dll=True,
    )
    compile_one(
        compiler,
        environment,
        SOURCE / "live_pose_injector.cpp",
        OUTPUT / "live_pose_injector.exe",
        dll=False,
    )
    print(OUTPUT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
