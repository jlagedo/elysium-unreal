"""Configure, build, and test the 32-bit retail-capture native project.

Usage:
    uv run elysium research retail_capture_native build
    uv run elysium research retail_capture_native test --config Debug
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

from elysium_pipeline.paths import research_root


ROOT = Path(__file__).resolve().parent / "native"
VSWHERE = (
    Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    / "Microsoft Visual Studio"
    / "Installer"
    / "vswhere.exe"
)


def _visual_studio_installation() -> Path:
    if not VSWHERE.is_file():
        raise FileNotFoundError(f"Visual Studio locator is missing: {VSWHERE}")
    result = subprocess.run(
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
    installations = json.loads(result.stdout)
    if not installations:
        raise RuntimeError("Visual Studio C++ x86 tools are not installed")
    return Path(installations[0]["installationPath"])


def _msvc_x86_environment(installation: Path) -> dict[str, str]:
    vcvars = installation / "VC" / "Auxiliary" / "Build" / "vcvars32.bat"
    if not vcvars.is_file():
        raise FileNotFoundError(f"32-bit MSVC environment is missing: {vcvars}")
    result = subprocess.run(
        f'call "{vcvars}" >nul && set',
        shell=True,
        check=True,
        capture_output=True,
        text=True,
    )
    environment = os.environ.copy()
    for line in result.stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            environment[key] = value
    return environment


def _bundled_tool(installation: Path, filename: str) -> Path | None:
    roots = (
        installation
        / "Common7"
        / "IDE"
        / "CommonExtensions"
        / "Microsoft"
        / "CMake"
    )
    matches = sorted(roots.rglob(filename)) if roots.is_dir() else []
    return matches[0] if matches else None


def _tool(
    name: str,
    installation: Path,
    environment: dict[str, str],
) -> Path:
    search_path = next(
        (value for key, value in environment.items() if key.lower() == "path"),
        None,
    )
    found = shutil.which(name, path=search_path)
    if found:
        return Path(found)
    bundled = _bundled_tool(installation, f"{name}.exe")
    if bundled:
        return bundled
    raise RuntimeError(f"{name} is not installed or bundled with Visual Studio")


def native_build_root() -> Path:
    return research_root() / "retail-capture" / "native" / "build"


def native_output_dir(configuration: str) -> Path:
    return native_build_root() / f"win32-{configuration.lower()}" / "bin"


def run(action: str, configuration: str) -> Path:
    installation = _visual_studio_installation()
    environment = _msvc_x86_environment(installation)
    cmake = _tool("cmake", installation, environment)
    ctest = _tool("ctest", installation, environment)
    preset = f"win32-{configuration.lower()}"
    environment["ELYSIUM_NATIVE_BUILD_ROOT"] = str(native_build_root())

    subprocess.run(
        [str(cmake), "--preset", preset],
        cwd=ROOT,
        env=environment,
        check=True,
    )
    if action in {"build", "test"}:
        subprocess.run(
            [str(cmake), "--build", "--preset", preset],
            cwd=ROOT,
            env=environment,
            check=True,
        )
    if action == "test":
        subprocess.run(
            [str(ctest), "--preset", preset],
            cwd=ROOT,
            env=environment,
            check=True,
        )
    return native_output_dir(configuration)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action",
        choices=("configure", "build", "test"),
        nargs="?",
        default="build",
    )
    parser.add_argument(
        "--config",
        choices=("Debug", "Release"),
        default="Release",
    )
    args = parser.parse_args()
    print(run(args.action, args.config))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
