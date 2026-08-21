# -*- coding: utf-8 -*-
"""Turn a module's datamaps into Ghidra structures and hang them on their classes.

``ApplyDatamapTypes.java`` reads every `datamap_t` the census labelled, resolves the records
the builder assigns at static-init, derives each `fieldType`'s width from the corpus, and
writes one structure per class -- then makes the class a GhidraClass and sets ``__thiscall`` on
the methods it owns, so a decompiled member access reads ``this->m_flCycle`` instead of
``*(float *)(param_1 + 0x6f8)``.

Two phases:

    report     read-only: the derived width table and the per-class field counts
    apply      write the structures and the signatures

Usage:
    uv run elysium research datamap_types report vampire.dll
    uv run elysium research datamap_types apply vampire.dll client.dll

Run ``DumpDatamaps`` and ``DumpRtti`` on a program first -- their labels are what this reads.
Reports land under ``$ELYSIUM_WORK_ROOT/research/ghidra/types/``.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path

from elysium_pipeline.paths import repo_root, research_root

# Only these carry datamaps; the rest of the project's programs define no entity.
PROGRAMS = ("vampire.dll", "client.dll", "engine.dll")

LOCK_DELAY_SECONDS = 9.0


def _runner() -> Path:
    runner = repo_root() / "research" / "tooling" / "ghidra" / "driver" / "run.ps1"
    if not runner.is_file():
        raise FileNotFoundError(f"local Ghidra runner is absent: {runner}")
    return runner


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
    # analyzeHeadless.bat exits 0 even when the analyzer aborts or a script throws.
    with log.open("r", encoding="utf-8", errors="replace") as stream:
        stream.seek(marker)
        for line in stream:
            if "Abort due to Headless analyzer error" in line or "REPORT SCRIPT ERROR" in line:
                raise RuntimeError(f"headless run failed: {line.strip()}; see {log}")


def _echo_summary(report: Path) -> None:
    if not report.is_file():
        print(f"  no report written: {report}")
        return
    for line in reversed(report.read_text(encoding="utf-8", errors="replace").splitlines()):
        if line.startswith("// ") and "classes," in line:
            print("  " + line[3:])
            return
    print(f"  report holds no summary line: {report}")


def sweep(phase: str, programs: list[str], project_dir: Path | None, project_name: str) -> int:
    out_dir = research_root() / "ghidra" / "types"
    out_dir.mkdir(parents=True, exist_ok=True)
    log = out_dir / f"{phase}.log"
    project = project_dir or (research_root() / "ghidra" / "project")

    first = True
    for program in programs:
        if not first:
            time.sleep(LOCK_DELAY_SECONDS)
        first = False
        report = out_dir / f"{phase}-{program}.txt"
        script_args = [f"out={report.as_posix()}"]
        if phase == "apply":
            script_args.append("apply=1")
        print(f"[{program}] {phase}")
        _run(["-ProjDir", str(project), "-ProjName", project_name, "-NoFid",
              "-Program", program, "-Script", "ApplyDatamapTypes",
              "-ScriptArgs", " ".join(script_args)], log)
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
    parser = argparse.ArgumentParser(prog="datamap_types", description=__doc__)
    sub = parser.add_subparsers(dest="phase", required=True)

    report_parser = sub.add_parser("report", help="read-only: widths and per-class field counts")
    report_parser.add_argument("program", nargs="*", default=[])

    apply_parser = sub.add_parser("apply", help="write structures and typed signatures")
    apply_parser.add_argument("program", nargs="+")

    for one in (report_parser, apply_parser):
        one.add_argument("--project-dir", type=Path, default=None)
        one.add_argument("--project-name", default="vtmb")

    args = parser.parse_args()
    programs = list(args.program) or list(PROGRAMS)
    return sweep(args.phase, programs, args.project_dir, args.project_name)


if __name__ == "__main__":
    raise SystemExit(main())
