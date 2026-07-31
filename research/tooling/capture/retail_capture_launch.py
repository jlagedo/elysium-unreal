"""Create the selected retail process suspended and bootstrap the probe host.

By default this command verifies the handshake and terminates before resuming
retail. ``--run`` enables supervised execution; press Ctrl-C once to perform
the required human cancellation acceptance:

    uv run elysium research retail_capture_launch \
        --distribution owner-configured \
        --startup-profile unofficial-patch-save \
        --run --timeout-seconds 300
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import time

from elysium_pipeline.paths import research_root, vtmb_root
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
        choices=("direct", "unofficial-patch", "unofficial-patch-save"),
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
        "--run",
        action="store_true",
        help="Resume after bootstrap and supervise through a terminal condition.",
    )
    parser.add_argument(
        "--timeout-seconds",
        type=int,
        default=300,
        help="Supervised runtime limit; zero waits indefinitely.",
    )
    parser.add_argument(
        "--normal-exit-code",
        action="append",
        type=int,
        help=(
            "Process exit code accepted as normal; repeat as needed. "
            "Retail defaults to 0 and 1."
        ),
    )
    parser.add_argument(
        "--collector",
        type=Path,
        help="Optional external collector owned by the supervision job.",
    )
    parser.add_argument(
        "--collector-argument",
        action="append",
        default=[],
        help="Collector argument; repeat once for every argument.",
    )
    parser.add_argument(
        "--finalization",
        type=Path,
        help="Versioned finalization report path for supervised execution.",
    )
    parser.add_argument(
        "--target-argument",
        action="append",
        default=[],
        help="Retail argument; repeat once for every argument.",
    )
    parser.add_argument(
        "target_arguments",
        nargs=argparse.REMAINDER,
        help="Arguments after -- are reproduced on the retail command line.",
    )
    args = parser.parse_args()
    target_arguments = [*args.target_argument, *args.target_arguments]
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
    if args.startup_profile == "unofficial-patch-save":
        load_config = (
            working_directory
            / "Unofficial_Patch"
            / "cfg"
            / "elysium_load.cfg"
        )
        if not load_config.is_file():
            raise FileNotFoundError(load_config)
        active_commands = [
            line.strip()
            for line in load_config.read_text(
                encoding="utf-8-sig",
            ).splitlines()
            if line.strip() and not line.lstrip().startswith("//")
        ]
        load_commands = [
            command
            for command in active_commands
            if command.partition(" ")[0].casefold() == "load"
        ]
        if not load_commands:
            raise ValueError(
                f"{load_config} contains no active load command"
            )
        print(
            f"direct-save config: {load_config} "
            f"command={load_commands[0]!r}",
            flush=True,
        )
    if args.verify_suspended_ms < 0 or args.verify_suspended_ms > 600000:
        parser.error("--verify-suspended-ms must be between 0 and 600000")
    if args.timeout_seconds < 0 or args.timeout_seconds > 600:
        parser.error("--timeout-seconds must be between 0 and 600")
    normal_exit_codes = (
        args.normal_exit_code
        if args.normal_exit_code is not None
        else [0, 1]
    )
    if any(code < 0 or code > 0xFFFFFFFF for code in normal_exit_codes):
        parser.error("--normal-exit-code must be between 0 and 4294967295")
    for value in args.environment:
        name, separator, _ = value.partition("=")
        if not separator or not name:
            parser.error(f"--environment must be NAME=VALUE: {value!r}")

    run_native("build", args.config)
    native_output = native_output_dir(args.config)
    launcher = native_output / "retail_launcher.exe"
    probe_host = native_output / "retail_probe_host.dll"
    collector = args.collector.resolve() if args.collector else None
    if collector is not None and not collector.is_file():
        raise FileNotFoundError(collector)
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
    finalization = None
    if args.run:
        finalization = (
            args.finalization.resolve()
            if args.finalization is not None
            else (
                research_root()
                / "retail-capture"
                / "supervision"
                / f"{time.strftime('%Y%m%d_%H%M%S')}-finalization.txt"
            ).resolve()
        )
        finalization.parent.mkdir(parents=True, exist_ok=True)
        command.extend(
            (
                "--finalization",
                os.fspath(finalization),
                "--timeout-ms",
                str(args.timeout_seconds * 1000),
            )
        )
        for exit_code in normal_exit_codes:
            command.extend(("--normal-exit-code", str(exit_code)))
        if collector is not None:
            command.extend(("--collector", os.fspath(collector)))
            for value in args.collector_argument:
                command.extend(("--collector-argument", value))
        command.append("--supervise")
    else:
        command.append("--inject-and-terminate")
    if target_arguments:
        command.append("--")
        command.extend(target_arguments)
    if not args.run:
        subprocess.run(command, check=True)
        return 0

    print(f"finalization report: {finalization}", flush=True)
    process = subprocess.Popen(command)
    try:
        return process.wait()
    except KeyboardInterrupt:
        print("Ctrl-C received; waiting for supervised finalization...", flush=True)
        try:
            return process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.terminate()
            return process.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
