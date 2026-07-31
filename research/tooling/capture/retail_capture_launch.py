"""Create the selected retail process suspended and bootstrap the probe host.

Until CAP1.5 owns complete process supervision, this command verifies the
versioned ready/error handshake and terminates before resuming retail:

    uv run elysium research retail_capture_launch \
        --distribution steam \
        --startup-profile unofficial-patch
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess

from elysium_pipeline.paths import vtmb_root
from research.tooling.capture.retail_capture_native import (
    native_output_dir,
    run as run_native,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--executable",
        type=Path,
        help="Vampire.exe to launch; defaults to ELYSIUM_VTMB_ROOT/Vampire.exe.",
    )
    parser.add_argument(
        "--working-directory",
        type=Path,
        help="Retail working directory; defaults to the executable directory.",
    )
    parser.add_argument("--distribution", required=True)
    parser.add_argument(
        "--startup-profile",
        choices=("direct", "unofficial-patch"),
        default="direct",
    )
    parser.add_argument(
        "--environment",
        action="append",
        default=[],
        metavar="NAME=VALUE",
        help="Override one inherited environment value; repeat as needed.",
    )
    parser.add_argument("--verify-suspended-ms", type=int, default=25)
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument(
        "target_arguments",
        nargs=argparse.REMAINDER,
        help="Arguments after -- are reproduced on the retail command line.",
    )
    args = parser.parse_args()
    target_arguments = args.target_arguments
    if target_arguments[:1] == ["--"]:
        target_arguments = target_arguments[1:]

    executable = (
        args.executable.resolve()
        if args.executable is not None
        else (vtmb_root() / "Vampire.exe").resolve()
    )
    working_directory = (
        args.working_directory.resolve()
        if args.working_directory is not None
        else executable.parent
    )
    if not executable.is_file():
        raise FileNotFoundError(executable)
    if not working_directory.is_dir():
        raise NotADirectoryError(working_directory)
    if args.verify_suspended_ms < 0 or args.verify_suspended_ms > 600000:
        parser.error("--verify-suspended-ms must be between 0 and 600000")
    for value in args.environment:
        name, separator, _ = value.partition("=")
        if not separator or not name:
            parser.error(f"--environment must be NAME=VALUE: {value!r}")

    run_native("build", args.config)
    native_output = native_output_dir(args.config)
    launcher = native_output / "retail_launcher.exe"
    probe_host = native_output / "retail_probe_host.dll"
    command = [
        os.fspath(launcher),
        "--executable",
        os.fspath(executable),
        "--working-directory",
        os.fspath(working_directory),
        "--distribution",
        args.distribution,
        "--startup-profile",
        args.startup_profile,
        "--verify-suspended-ms",
        str(args.verify_suspended_ms),
        "--probe-host",
        os.fspath(probe_host),
    ]
    for value in args.environment:
        command.extend(("--environment", value))
    command.append("--inject-and-terminate")
    if target_arguments:
        command.append("--")
        command.extend(target_arguments)
    subprocess.run(command, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
