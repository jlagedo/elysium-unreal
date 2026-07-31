"""Run repeated synthetic retail launch and capture lifecycle acceptance.

Usage:
    uv run elysium research retail_capture_soak
    uv run elysium research retail_capture_soak --config Debug --cycles 100
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import time

from elysium_pipeline.paths import research_root
from research.tooling.capture.retail_capture_native import (
    native_output_dir,
    run as run_native,
)


ROOT = Path(__file__).resolve().parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        choices=("Debug", "Release"),
        default="Release",
    )
    parser.add_argument("--cycles", type=int, default=100)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.cycles < 1 or args.cycles > 1000:
        parser.error("--cycles must be between 1 and 1000")

    run_native("build", args.config)
    native_output = native_output_dir(args.config)
    output = (
        args.output.resolve()
        if args.output is not None
        else (
            research_root()
            / "retail-capture"
            / "lifecycle-soak"
            / f"{time.strftime('%Y%m%d_%H%M%S')}-{args.config.lower()}"
        ).resolve()
    )
    command = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        os.fspath(ROOT / "native" / "verify_lifecycle_soak.ps1"),
        "-Launcher",
        os.fspath(native_output / "retail_launcher.exe"),
        "-Target",
        os.fspath(native_output / "vampire.exe"),
        "-BinDir",
        os.fspath(native_output),
        "-Probe",
        os.fspath(native_output / "retail_probe_host.dll"),
        "-Collector",
        os.fspath(native_output / "synthetic_collector.exe"),
        "-OutputDir",
        os.fspath(output),
        "-Cycles",
        str(args.cycles),
    ]
    print(f"soak artifacts: {output}", flush=True)
    return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
