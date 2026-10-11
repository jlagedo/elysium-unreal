"""Read-only public retail contact44 comparison against the authored native fixture."""
from __future__ import annotations
from datetime import datetime, timezone
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import re
import csv

RESEARCH_NOT_A_QUERY = "Builds and runs a read-only public retail contact observer."


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--variants", nargs="+", choices=("baseline", "swapped", "mirror", "swizzle"), default=["baseline"])
    parser.add_argument("--clock", choices=("raw", "admitted"), default="raw")
    parser.add_argument("--arrival", choices=("frame", "zero"), default="frame")
    parser.add_argument("--moving-log", type=Path, help="Replay NativeMovingFirstContact's ordinary creation/submission rows; comparison uses actual47, not37.")
    parser.add_argument("--observe-event-dt", action="store_true", help="Pair unchanged moving replay with public zero-force MotionEvent dt observation.")
    args = parser.parse_args()
    if args.moving_log and args.variants != ["baseline"]:
        parser.error("moving replay requires the recorded baseline geometry")
    if args.observe_event_dt and not args.moving_log:
        parser.error("event observation requires --moving-log")
    work = Path(os.environ["ELYSIUM_WORK_ROOT"]).resolve()
    game = Path(os.environ["ELYSIUM_VTMB_ROOT"]).resolve()
    scratch = work / "research/vphysics-contacts" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    scratch.mkdir(parents=True)
    moving_input = None
    native_blocked_frame = None
    if args.moving_log:
        native_log = args.moving_log.read_text(encoding="utf-8", errors="replace")
        blocked_frames = [int(frame) for frame, count in re.findall(r"MOVING_FEEDBACK,(\d+),(\d+),", native_log) if int(count) > 0]
        if not blocked_frames:
            raise RuntimeError("Moving log did not prove actual native178")
        native_blocked_frame = blocked_frames[0]
        rows = re.findall(r"MOVING_(?:CREATE|INPUT),[^\r\n]+", native_log)
        rows = [row for row in rows if row.startswith("MOVING_CREATE,") or int(row.split(",")[1]) <= native_blocked_frame]
        if not rows:
            raise RuntimeError("No actual normal moving input rows in supplied log")
        moving_input = scratch / "moving-input.csv"
        moving_input.write_text("\n".join(rows)+"\n", encoding="utf-8")
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
    variants = [(variant, False) for variant in args.variants]
    if args.observe_event_dt:
        variants.append(("baseline", True))
    for variant, observe_events in variants:
        label = f"{variant}-events" if observe_events else variant
        output = scratch / f"contacts-{label}.csv"
        try:
            command = [str(exe), str(game / "Bin"), str(surfaces), str(output), variant, args.clock, args.arrival]
            if moving_input:
                command.append(str(moving_input))
            if observe_events:
                command.append("events")
            result = subprocess.run(command, cwd=scratch, capture_output=True, text=True, timeout=30)
        except subprocess.TimeoutExpired as error:
            result = subprocess.CompletedProcess(error.cmd, -1, (error.stdout or b"").decode() if isinstance(error.stdout, bytes) else error.stdout or "", "bounded public probe timeout")
        (scratch / f"stdout-{label}.txt").write_text(result.stdout)
        (scratch / f"stderr-{label}.txt").write_text(result.stderr)
        runs.append({"variant": variant, "clock": "recorded admitted host/native frames" if moving_input else args.clock,
                     "arrival": "recorded normal object46 submissions" if moving_input else args.arrival,
                     "comparisonThroughNativeFirst178Frame": native_blocked_frame, "eventObserver": observe_events,
                     "exitCode": result.returncode, "csv": str(output), "stdout": result.stdout})
        if result.returncode:
            break
    provenance_paths = [Path(__file__), source, game / "Bin/vphysics.dll", surfaces]
    if args.moving_log:
        provenance_paths.extend([args.moving_log, moving_input])
    summary = {"exitCode": runs[-1]["exitCode"], "runs": runs, "provenance": [{"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()} for path in provenance_paths]}
    if args.observe_event_dt and summary["exitCode"] == 0:
        with Path(runs[0]["csv"]).open(newline="") as handle:
            control_rows = list(csv.DictReader(handle))
        with Path(runs[1]["csv"]).open(newline="") as handle:
            event_rows = list(csv.DictReader(handle))
        excluded = {"event_count", "event_min", "event_max", "event_sum"}
        summary["eventObserverPreservedAllOtherColumns"] = len(control_rows) == len(event_rows) and all(
            {key: value for key, value in control.items() if key not in excluded} ==
            {key: value for key, value in observed.items() if key not in excluded}
            for control, observed in zip(control_rows, event_rows, strict=True))
        if not summary["eventObserverPreservedAllOtherColumns"]:
            summary["exitCode"] = 18
    report = scratch / "index.json"; report.write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps(summary,indent=2)); print(f"Report {report}")
    if summary["exitCode"]:
        raise RuntimeError(f"Contact instrument failed; retained report {report}")


if __name__ == "__main__":
    main()
