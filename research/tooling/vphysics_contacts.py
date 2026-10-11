"""Read-only public retail contact44 comparison against the authored native fixture."""
from __future__ import annotations
from datetime import datetime, timezone
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

RESEARCH_NOT_A_QUERY = "Builds and runs a read-only public retail contact observer."


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--variants", nargs="+", choices=("baseline", "swapped", "mirror", "swizzle"), default=["baseline"])
    parser.add_argument("--clock", choices=("raw", "admitted"), default="raw")
    parser.add_argument("--arrival", choices=("frame", "zero"), default="frame")
    args = parser.parse_args()
    work = Path(os.environ["ELYSIUM_WORK_ROOT"]).resolve()
    game = Path(os.environ["ELYSIUM_VTMB_ROOT"]).resolve()
    scratch = work / "research/vphysics-contacts" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    scratch.mkdir(parents=True)
    source = Path(__file__).with_name("vphysics_contacts_probe.cpp")
    surfaces = Path("Content/ElysiumCorpus/physics/surfaceproperties.txt").resolve()
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    install = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
    if not install:
        raise RuntimeError("Visual Studio x86 tools not found")
    exe = scratch / "contacts.exe"
    batch = scratch / "compile.cmd"
    batch.write_text(f'@call "{Path(install) / "VC/Auxiliary/Build/vcvarsall.bat"}" x86\nif errorlevel 1 exit /b %errorlevel%\n@cl /nologo /EHsc /W4 "{source}" /Fo"{scratch / "probe.obj"}" /Fe"{exe}"\n', encoding="utf-8")
    subprocess.run(["cmd.exe", "/d", "/c", str(batch)], cwd=scratch, check=True)
    runs = []
    for variant in args.variants:
        output = scratch / f"contacts-{variant}.csv"
        try:
            result = subprocess.run([str(exe), str(game / "Bin"), str(surfaces), str(output), variant, args.clock, args.arrival], cwd=scratch, capture_output=True, text=True, timeout=30)
        except subprocess.TimeoutExpired as error:
            result = subprocess.CompletedProcess(error.cmd, -1, (error.stdout or b"").decode() if isinstance(error.stdout, bytes) else error.stdout or "", "bounded public probe timeout")
        (scratch / f"stdout-{variant}.txt").write_text(result.stdout)
        (scratch / f"stderr-{variant}.txt").write_text(result.stderr)
        runs.append({"variant": variant, "clock": args.clock, "arrival": args.arrival, "exitCode": result.returncode, "csv": str(output), "stdout": result.stdout})
        if result.returncode:
            break
    summary = {"exitCode": runs[-1]["exitCode"], "runs": runs, "provenance": [{"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in (Path(__file__), source, game / "Bin/vphysics.dll", surfaces)]}
    report = scratch / "index.json"; report.write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps(summary,indent=2)); print(f"Report {report}")
    if summary["exitCode"]:
        raise RuntimeError(f"Contact instrument failed; retained report {report}")


if __name__ == "__main__":
    main()
