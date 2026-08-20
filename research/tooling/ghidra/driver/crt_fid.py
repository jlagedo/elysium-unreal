# -*- coding: utf-8 -*-
"""Build the VC6 SP5 C-runtime FID database and apply it to the VtMB binaries.

VtMB's modules statically link the CRT, so a large share of every module's functions are
Microsoft's, not Troika's. Ghidra's Function ID analyzer names them from a database of
hashed library functions; the databases it ships are merged approximations of a Visual
Studio release, and none is VC6 SP5. This builds one from the archives the game actually
links, so a matched name is an exact-archive match rather than a near miss.

Three phases, each independently runnable:

    stage      unpack LIBC.LIB and LIBCMT.LIB from the reference archive, hash-verified
    build      import every COFF member as its own program, analyze it, hash it into the db
    apply      name an already-analyzed VtMB program from the finished database, then
               census its global constructors (the CRT names are what locate them)

Usage:
    uv run elysium research crt_fid stage
    uv run elysium research crt_fid build
    uv run elysium research crt_fid apply vampire.dll client.dll

Everything it reads and writes lives under ``$ELYSIUM_WORK_ROOT/research/ghidra/``. The
Microsoft archives are third-party reference material and never enter the checkout.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import time
import zipfile
from pathlib import Path

from elysium_pipeline.paths import cache_root, repo_root, research_root

GHIDRA_DIR_NAME = "ghidra_12.1.2_PUBLIC"
SOURCE_ARCHIVE = "vc6sp5-lib.zip"
LOCK_DELAY_SECONDS = 9.0

# sha256 as published in the reference archive's own README; a mismatch means the staged
# archive is not the release the Rich header dates the game to, and the database would
# describe a runtime the game does not link.
LIBRARIES = (
    ("libc", "LIBC.LIB",
     "9bfe3af117de0d676f74448666b3b906cafaba15f21dbc04b88a359426a111e0"),
    ("libcmt", "LIBCMT.LIB",
     "28b9f04962378ec4668072f37d7fd2835cd6cacc17b40cf22002c57bd8e76714"),
)

LIBRARY_FAMILY = "vc6"
LIBRARY_VERSION = "sp5"
LANGUAGE_ID = "x86:LE:32:default"
COMPILER_SPEC = "windows"


def _ghidra_root() -> Path:
    root = cache_root() / GHIDRA_DIR_NAME
    if not root.is_dir():
        raise FileNotFoundError(f"Ghidra installation is absent: {root}")
    return root


def _crt_dir() -> Path:
    return research_root() / "ghidra" / "crt" / "vc6sp5"


def _fidb_path() -> Path:
    return research_root() / "ghidra" / "fid" / "vc6sp5.fidb"


def _project_dir(variant: str) -> Path:
    return research_root() / "ghidra" / "crtfid" / variant


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
    # analyzeHeadless.bat exits 0 even when the analyzer aborts, so the log is the only
    # honest signal that the run did anything.
    with log.open("r", encoding="utf-8", errors="replace") as stream:
        stream.seek(marker)
        for line in stream:
            if "Abort due to Headless analyzer error" in line or "REPORT SCRIPT ERROR" in line:
                raise RuntimeError(f"headless run failed: {line.strip()}; see {log}")


def _any_program(project: Path, name: str) -> str:
    """Any program name in a Ghidra project, read from its own on-disk name index.

    A headless script needs a program to process even when its work is project-wide, so
    the populate phase processes one arbitrary object and never looks at it. The index is
    plain text: folder paths at column 0, ``<id>:<name>:<version>`` rows beneath each.
    """
    index = project / f"{name}.rep" / "idata" / "~index.dat"
    if not index.is_file():
        raise FileNotFoundError(f"project has no name index: {index}")
    for line in index.read_text(encoding="latin-1").splitlines():
        if not line.startswith(" "):
            continue
        fields = line.strip().split(":")
        if len(fields) >= 2 and fields[1]:
            return fields[1]
    raise ValueError(f"project holds no programs: {index}")


def stage() -> int:
    archive = research_root() / SOURCE_ARCHIVE
    if not archive.is_file():
        raise FileNotFoundError(f"reference archive is absent: {archive}")
    destination = _crt_dir()
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as bundle:
        for _, member, expected in LIBRARIES:
            payload = bundle.read(f"vc6sp5-lib/lib/{member}")
            digest = hashlib.sha256(payload).hexdigest()
            if digest != expected:
                raise ValueError(f"{member}: sha256 is {digest}, expected {expected}")
            (destination / member).write_bytes(payload)
            print(f"staged {member} ({len(payload)} bytes, sha256 verified)")
    print(f"staged into {destination}")
    return 0


def build(variants: list[str], reimport: bool = True) -> int:
    ghidra = _ghidra_root()
    common = ghidra / "Ghidra" / "Features" / "FunctionID" / "data" / "common_symbols_win32.txt"
    if not common.is_file():
        raise FileNotFoundError(f"Ghidra common-symbols list is absent: {common}")
    fidb = _fidb_path()
    log = research_root() / "ghidra" / "fid" / "build.log"

    for variant, member, _ in LIBRARIES:
        if variants and variant not in variants:
            continue
        archive = _crt_dir() / member
        if not archive.is_file():
            raise FileNotFoundError(f"{member} is not staged; run `crt_fid stage` first")
        project = _project_dir(variant)

        # Import and analyze every member. The FID prescript turns off the analyzers that
        # would rename a library's own functions from inside itself, and turns on the
        # scalar operand analyzer so an object's relocated addresses are recognised as
        # references instead of being hashed as constants.
        if reimport:
            print(f"[{variant}] importing {member} — the slow phase, one program per object")
            _run(["-ProjDir", str(project), "-ProjName", variant, "-NoFid", "-Recursive",
                  "-Processor", LANGUAGE_ID, "-Cspec", COMPILER_SPEC,
                  "-PreScript", "FunctionIDHeadlessPrescript", "-Import", str(archive)], log)
            time.sleep(LOCK_DELAY_SECONDS)

        report = research_root() / "ghidra" / "fid" / f"{variant}.txt"
        script_args = " ".join([
            f"fidb={fidb.as_posix()}",
            "folder=/",
            f"name={LIBRARY_FAMILY}",
            f"version={LIBRARY_VERSION}",
            f"variant={variant}",
            f"lang={LANGUAGE_ID}",
            f"common={common.as_posix()}",
            f"out={report.as_posix()}",
        ])
        print(f"[{variant}] populating {fidb.name}")
        _run(["-ProjDir", str(project), "-ProjName", variant, "-NoFid", "-Recursive",
              "-Program", _any_program(project, variant),
              "-Script", "BuildCrtFid", "-ScriptArgs", script_args], log)
        print(f"[{variant}] {report}")
        time.sleep(LOCK_DELAY_SECONDS)

    print(f"database: {fidb}")
    return 0


def apply(programs: list[str], project_dir: Path | None, project_name: str,
          census: bool = True) -> int:
    fidb = _fidb_path()
    if not fidb.is_file():
        raise FileNotFoundError(f"FID database is absent: {fidb}; run `crt_fid build` first")
    project = project_dir or (research_root() / "ghidra" / "project")
    log = research_root() / "ghidra" / "fid" / "apply.log"
    first = True
    for program in programs:
        if not first:
            time.sleep(LOCK_DELAY_SECONDS)
        first = False
        report = research_root() / "ghidra" / "fid" / f"applied-{program}.txt"
        script_args = f"fidb={fidb.as_posix()} out={report.as_posix()}"
        print(f"[{program}] applying {fidb.name}")
        _run(["-ProjDir", str(project), "-ProjName", project_name, "-NoFid",
              "-Program", program, "-Script", "ApplyFid", "-ScriptArgs", script_args], log)
        print(f"[{program}] {report}")

        # The census depends on the names the apply just wrote: __cinit and __initterm are
        # what locate the global-constructor tables.
        if not census:
            continue
        time.sleep(LOCK_DELAY_SECONDS)
        init_report = research_root() / "ghidra" / "fid" / f"init-{program}.txt"
        print(f"[{program}] censusing global constructors")
        _run(["-ProjDir", str(project), "-ProjName", project_name, "-NoFid",
              "-Program", program, "-Script", "DumpInitTable",
              "-ScriptArgs", f"out={init_report.as_posix()} top=40"], log)
        print(f"[{program}] {init_report}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(prog="crt_fid", description=__doc__)
    sub = parser.add_subparsers(dest="phase", required=True)

    sub.add_parser("stage", help="unpack the hash-verified CRT archives")

    build_parser = sub.add_parser("build", help="import, analyze and hash the CRT archives")
    build_parser.add_argument("variant", nargs="*", choices=["libc", "libcmt"],
                              help="restrict to one archive (default: both)")
    build_parser.add_argument("--no-import", dest="reimport", action="store_false",
                              help="reuse the objects already imported and only hash them")

    apply_parser = sub.add_parser("apply", help="name an analyzed program from the database")
    apply_parser.add_argument("program", nargs="+", help="program name inside the Ghidra project")
    apply_parser.add_argument("--no-census", dest="census", action="store_false",
                              help="skip the global-constructor census after labelling")
    apply_parser.add_argument("--project-dir", type=Path, default=None)
    apply_parser.add_argument("--project-name", default="vtmb")

    args = parser.parse_args()
    if args.phase == "stage":
        return stage()
    if args.phase == "build":
        return build(args.variant, args.reimport)
    return apply(args.program, args.project_dir, args.project_name, args.census)


if __name__ == "__main__":
    raise SystemExit(main())
