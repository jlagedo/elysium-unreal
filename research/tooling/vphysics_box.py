"""Read-only x86 retail Collision007#19 capture; outputs stay under ELYSIUM_WORK_ROOT."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import os
from pathlib import Path
import subprocess

RESEARCH_NOT_A_QUERY = "Compiles an authored x86 instrument and captures a retail collision surface."


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--coords", nargs=6, metavar=("MIN_X", "MIN_Y", "MIN_Z", "MAX_X", "MAX_Y", "MAX_Z"))
    args = parser.parse_args()
    work = Path(os.environ["ELYSIUM_WORK_ROOT"]).resolve()
    game_bin = Path(os.environ["ELYSIUM_VTMB_ROOT"]).resolve() / "Bin"
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    scratch = work / "research" / "vphysics-box" / stamp
    output = (args.out or scratch / "box.bin").resolve()
    if not output.is_relative_to(work):
        raise ValueError("--out must stay under ELYSIUM_WORK_ROOT")
    if output.exists():
        raise FileExistsError(output)
    scratch.mkdir(parents=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    install = subprocess.check_output([
        str(vswhere), "-latest", "-products", "*", "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath",
    ], text=True).strip()
    if not install:
        raise RuntimeError("Visual Studio x86 C++ tools were not found")
    environment = Path(install) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
    source = Path(__file__).with_name("vphysics_box_probe.cpp")
    executable = scratch / "box_probe.exe"
    # A generated batch keeps Windows quoting intact; all compiler outputs are scratch-owned.
    batch = scratch / "compile.cmd"
    batch.write_text(
        f'@call "{environment}" x86\n'
        'if errorlevel 1 exit /b %errorlevel%\n'
        f'@cl /nologo /EHsc /W4 "{source}" /Fo"{scratch / "box_probe.obj"}" /Fe"{executable}"\n',
        encoding="utf-8",
    )
    subprocess.run(["cmd.exe", "/d", "/c", str(batch)], check=True, cwd=scratch)
    subprocess.run([str(executable), str(game_bin), str(output), *(args.coords or [])], check=True, cwd=scratch)
    for name, path in (("DLL", game_bin / "vphysics.dll"), ("probe", source), ("capture", output)):
        print(f"{name} {path} SHA256 {hashlib.sha256(path.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
