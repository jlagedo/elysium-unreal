# -*- coding: utf-8 -*-
"""Name a VtMB module's functions from the symbol-shaped strings compiled into it.

A Source build leaves its own identifiers in the image: a VProf scope pushed at a
function's entry is that function's name, an input handler opens by pushing
``C<Class>::Input<Name>``, and an assert carries the source file it was written in.
``NameFromStrings.java`` attributes each such string to the function that references it.

Three phases:

    survey     read-only: report what would be named, per module
    apply      rename the strong tier and record the evidence in each plate comment
    clear      revert this pass's own renames

Usage:
    uv run elysium research symbol_sweep survey
    uv run elysium research symbol_sweep survey vampire.dll client.dll
    uv run elysium research symbol_sweep apply vampire.dll

``survey`` writes nothing to the Ghidra project and defaults to every module in it.
``apply`` mutates the project database, so it takes an explicit module list. Reports land
under ``$ELYSIUM_WORK_ROOT/research/ghidra/names/``; nothing it produces enters the checkout.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path

from elysium_pipeline.paths import repo_root, research_root

# The programs the vtmb project holds. `research/tooling/ghidra/driver/README.md` records
# which of them carry RTTI and which need `MakeFuncs` before their code is owned at all.
PROGRAMS = (
    "vampire.dll",
    "client.dll",
    "engine.dll",
    "GameUI.dll",
    "vguimatsurface.dll",
    "MaterialSystem.dll",
    "StudioRender.dll",
    "stdshader_dx8.dll",
)

# The headless JVM releases the project lock a few seconds after it prints its result, so
# back-to-back runs race and the second one writes nothing.
LOCK_DELAY_SECONDS = 9.0

# Bytes from a function's entry point within which a referenced qualified name is taken to
# name that function rather than something it mentions.
ENTRY_SPAN = 128


def _runner() -> Path:
    runner = repo_root() / "research" / "tooling" / "ghidra" / "driver" / "run.ps1"
    if not runner.is_file():
        raise FileNotFoundError(f"local Ghidra runner is absent: {runner}")
    return runner


def _names_dir() -> Path:
    return research_root() / "ghidra" / "names"


def _run(arguments: list[str], log: Path) -> None:
    command = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
               "-File", str(_runner()), *arguments]
    log.parent.mkdir(parents=True, exist_ok=True)
    marker = log.stat().st_size if log.exists() else 0
    with log.open("a", encoding="utf-8") as stream:
        stream.write("\n$ " + " ".join(command) + "\n")
        stream.flush()
        result = subprocess.run(command, cwd=repo_root(), stdout=stream,
                                stderr=subprocess.STDOUT, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"headless run failed with exit code {result.returncode}; see {log}")
    # analyzeHeadless.bat exits 0 even when the analyzer aborts or a script throws, so the
    # log is the only honest signal that the run did anything.
    with log.open("r", encoding="utf-8", errors="replace") as stream:
        stream.seek(marker)
        for line in stream:
            if "Abort due to Headless analyzer error" in line or "REPORT SCRIPT ERROR" in line:
                raise RuntimeError(f"headless run failed: {line.strip()}; see {log}")


def _echo_summary(report: Path) -> None:
    """The script's own count line, so a batch reports per module without opening the file."""
    if not report.is_file():
        print(f"  no report written: {report}")
        return
    for line in reversed(report.read_text(encoding="utf-8", errors="replace").splitlines()):
        if line.startswith("// ") and "qualified-name strings" in line:
            print("  " + line[3:])
            return
    print(f"  report holds no summary line: {report}")


def sweep(phase: str, programs: list[str], project_dir: Path | None,
          project_name: str, entry: int) -> int:
    names = _names_dir()
    names.mkdir(parents=True, exist_ok=True)
    log = names / f"{phase}.log"
    project = project_dir or (research_root() / "ghidra" / "project")

    first = True
    for program in programs:
        if not first:
            time.sleep(LOCK_DELAY_SECONDS)
        first = False
        report = names / f"{phase}-{program}.txt"
        script_args = [f"out={report.as_posix()}", f"entry={entry}"]
        if phase == "apply":
            script_args.append("name=1")
        elif phase == "clear":
            script_args.append("clear=1")
        print(f"[{program}] {phase}")
        _run(["-ProjDir", str(project), "-ProjName", project_name, "-NoFid",
              "-Program", program, "-Script", "NameFromStrings",
              "-ScriptArgs", " ".join(script_args)], log)
        if phase != "clear":
            print(f"[{program}] {report}")
            _echo_summary(report)
    if phase == "apply":
        _record_apply(programs)
    return 0


def _record_apply(programs: list[str]) -> None:
    """Stamp when each module's names/types last changed.

    The corpus is a photograph of the project and cannot tell on its own that a newer set of
    names exists. Only these passes change them, so only these passes can date the change; a
    heuristic on the project directory's mtime fires on every read-only run and trains a reader
    to ignore it.
    """
    marker = research_root() / "ghidra" / "last-apply.json"
    marker.parent.mkdir(parents=True, exist_ok=True)
    stamps = {}
    if marker.is_file():
        try:
            stamps = json.loads(marker.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            print(f"last-apply stamp is unreadable ({error}); rewriting it")
    now = time.time()
    for program in programs:
        stamps[program] = now
    marker.write_text(json.dumps(stamps, indent=2, sort_keys=True), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(prog="symbol_sweep", description=__doc__)
    sub = parser.add_subparsers(dest="phase", required=True)

    survey_parser = sub.add_parser("survey", help="report only; writes nothing to the project")
    survey_parser.add_argument("program", nargs="*", default=[],
                               help="module name inside the Ghidra project (default: all)")

    apply_parser = sub.add_parser("apply", help="rename the strong tier")
    apply_parser.add_argument("program", nargs="+", help="module name inside the Ghidra project")

    clear_parser = sub.add_parser("clear", help="revert this pass's own renames")
    clear_parser.add_argument("program", nargs="+", help="module name inside the Ghidra project")

    for one in (survey_parser, apply_parser, clear_parser):
        one.add_argument("--project-dir", type=Path, default=None)
        one.add_argument("--project-name", default="vtmb")
        one.add_argument("--entry", type=int, default=ENTRY_SPAN,
                         help="bytes from the entry point that count as the strong tier")

    args = parser.parse_args()
    programs = list(args.program) or list(PROGRAMS)
    return sweep(args.phase, programs, args.project_dir, args.project_name, args.entry)


if __name__ == "__main__":
    raise SystemExit(main())
