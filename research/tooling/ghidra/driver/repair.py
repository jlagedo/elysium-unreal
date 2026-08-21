# -*- coding: utf-8 -*-
"""The repair passes: fix what the analyzer got wrong before the corpus photographs it.

Four defects that the corpus measured in itself, each with its own headless script:

    boundaries   a function whose body swallowed its neighbours -- vampire.dll's worst runs
                 68,194 bytes against a p99.9 of 3,096, and nothing inside it has a name or a
                 caller of its own                             (``SplitFuncs``)
    jumptables   a switch the decompiler could not follow, so its C has the WRONG control flow:
                 the switch reads as a call and cases vanish   (``RecoverJumpTables``)
    thiscall     a C++ method Ghidra never gave __thiscall, so `this` decompiles as `in_ECX`
                 and a virtual dispatch through it names no class -- 2,491 unresolved call
                 sites in vampire.dll                          (``RecoverThisCall``)
    signatures   a prototype the decompiler had to invent, which is where
                 ``CBaseCombatCharacter::MeleeSwingUpdate``'s 93 parameters come from
                                                               (``RecoverSignatures``)

Every phase reports before it writes:

    uv run elysium research repair boundaries report vampire.dll
    uv run elysium research repair boundaries apply vampire.dll

``all`` runs them in the order that matters -- boundaries first, because every later pass
reads function bodies, and signatures last, because a split function needs one too.

These passes MUTATE the shared Ghidra project, so they must land before ``corpus dump``: the
corpus preserves whatever names and types the project holds at the moment it is taken. Applying
stamps ``last-apply.json``, which is how an already-built corpus reports itself stale.

Reports land under ``$ELYSIUM_WORK_ROOT/research/ghidra/repair/``.
"""

from __future__ import annotations

import argparse
import subprocess
import time
from pathlib import Path

from elysium_pipeline.paths import repo_root, research_root

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

# Ghidra holds a global project lock and does not release it the instant the process exits.
LOCK_DELAY_SECONDS = 9.0

# Ordered: a split function needs a signature, and a recovered jump table changes the body a
# signature is read from, so signatures come last.
PASSES = {
    "boundaries": "SplitFuncs",
    "jumptables": "RecoverJumpTables",
    "thiscall": "RecoverThisCall",
    "signatures": "RecoverSignatures",
}
# `thiscall` precedes `signatures` because the convention decides whether the receiver is a
# parameter at all, and the parameter count is read against it.
ORDER = ("boundaries", "jumptables", "thiscall", "signatures")


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
        if line.startswith("// "):
            print("  " + line[3:])
            return
    print(f"  report holds no summary line: {report}")


def sweep(passes: list[str], phase: str, programs: list[str], project_dir: Path | None,
          project_name: str, extra: list[str]) -> int:
    out_dir = research_root() / "ghidra" / "repair"
    out_dir.mkdir(parents=True, exist_ok=True)
    project = project_dir or (research_root() / "ghidra" / "project")

    first = True
    for name in passes:
        script = PASSES[name]
        log = out_dir / f"{name}.log"
        for program in programs:
            if not first:
                time.sleep(LOCK_DELAY_SECONDS)
            first = False
            report = out_dir / f"{name}-{phase}-{program}.txt"
            script_args = [f"out={report.as_posix()}", *extra]
            if phase == "apply":
                script_args.append("apply=1")
            print(f"[{program}] {name} {phase}")
            _run(["-ProjDir", str(project), "-ProjName", project_name, "-NoFid",
                  "-Program", program, "-Script", script,
                  "-ScriptArgs", " ".join(script_args)], log)
            print(f"[{program}] {report}")
            _echo_summary(report)
    if phase == "apply":
        _stamp(programs)
    return 0


def _stamp(programs: list[str]) -> None:
    """Reuse the corpus driver's own stamp, so there is exactly one record of when a module's
    names and types last moved."""
    import json

    marker = research_root() / "ghidra" / "last-apply.json"
    marker.parent.mkdir(parents=True, exist_ok=True)
    stamps: dict[str, float] = {}
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
    parser = argparse.ArgumentParser(prog="repair", description=__doc__)
    parser.add_argument("pass_name", choices=[*ORDER, "all"])
    parser.add_argument("phase", choices=("report", "apply"))
    parser.add_argument("program", nargs="*", default=[])
    parser.add_argument("--project-dir", type=Path, default=None)
    parser.add_argument("--project-name", default="vtmb")
    parser.add_argument("--arg", action="append", default=[],
                        help="an extra key=value passed straight to the script")

    args = parser.parse_args()
    passes = list(ORDER) if args.pass_name == "all" else [args.pass_name]
    programs = list(args.program) or list(PROGRAMS)
    return sweep(passes, args.phase, programs, args.project_dir, args.project_name, args.arg)


if __name__ == "__main__":
    raise SystemExit(main())
