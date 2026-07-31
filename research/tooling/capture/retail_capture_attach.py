"""Inject the retail probe host into an already-running 32-bit process.

This is a non-owning debugger-led discovery fallback. Reproducible capture
acceptance uses ``retail_capture_launch`` instead.

Usage:
    uv run elysium research retail_capture_attach \
        --pid 1234 --distribution owner-configured
"""

from __future__ import annotations

import argparse
import os
import subprocess

from research.tooling.capture.retail_capture_native import (
    native_output_dir,
    run as run_native,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--distribution", required=True)
    parser.add_argument(
        "--config",
        choices=("Debug", "Release"),
        default="Release",
    )
    args = parser.parse_args()
    if args.pid <= 0:
        parser.error("--pid must be positive")

    run_native("build", args.config)
    native_output = native_output_dir(args.config)
    command = [
        os.fspath(native_output / "retail_launcher.exe"),
        "--attach-pid",
        str(args.pid),
        "--distribution",
        args.distribution,
        "--probe-host",
        os.fspath(native_output / "retail_probe_host.dll"),
    ]
    return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
