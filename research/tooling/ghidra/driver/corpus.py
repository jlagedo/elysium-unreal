# -*- coding: utf-8 -*-
"""The whole-body corpus: materialize every module once, then answer questions locally.

``DumpCorpus.java`` writes one module's functions, strings and class structures as JSONL;
``build`` loads those into a single SQLite database and derives the field ledger from the
decompiled expressions. Every later question -- who calls this, what else reads this offset,
which functions mention this string, what does this class look like -- is a query against that
database, with no headless run and no project lock.

Phases and queries:

    vtables [module...]      run the vtable pass (cheap; ground truth for virtual edges)
    dump [module...]         run the Ghidra pass (slow; this is the overnight step)
    listing [module...]      run the disassembly pass into its own database
    build [module...]        load the JSONL into corpus.sqlite, derive the ledger and the
                             virtual call graph
    stat                     what the database holds

    func <name|addr>         one function: signature, class, callers, callees
    code <name|addr>         its decompilation, with the decompiler's warnings about it
    asm <name|addr>          its disassembly, when the C is damaged or folds a constant away
    callers|callees <ref>    one hop of the call graph, direct and virtual
    vtable <Class>           a class's slots, in order
    slot <N>                 every class that fills a slot -- one virtual across the hierarchy
    grep <regex>             search the decompiled corpus
    str <text>               strings, and the functions that reference them
    globals <text>           a global and every function that touches it
    fields <Class>           a class's fields, by offset
    readers <offset>         THE LEDGER: every function that touches an offset or field
    closure <Class>          one class, whole: fields, methods, callers, strings
    outliers                 functions whose body swallowed its neighbours
    suggest                  unnamed functions only one class ever calls

Usage:
    uv run elysium research corpus dump
    uv run elysium research corpus build
    uv run elysium research corpus readers 0x2f8
    uv run elysium research corpus closure CBasePlayer

Everything lives under ``$ELYSIUM_WORK_ROOT/research/ghidra/corpus/``; nothing enters the
checkout. Run the naming and typing passes BEFORE ``dump`` -- the corpus preserves whatever
names and types the project holds at the moment it is taken.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sqlite3
import subprocess
import time
from pathlib import Path

from elysium_pipeline.paths import repo_root, research_root, vtmb_root

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

LOCK_DELAY_SECONDS = 9.0

SCHEMA = """
CREATE TABLE IF NOT EXISTS functions (
    module TEXT, addr TEXT, name TEXT, ns TEXT, size INTEGER, cc TEXT,
    thunk INTEGER, code TEXT, warn TEXT DEFAULT '', PRIMARY KEY (module, addr));
CREATE TABLE IF NOT EXISTS edges (
    module TEXT, caller TEXT, callee TEXT, kind TEXT DEFAULT 'direct');
CREATE TABLE IF NOT EXISTS vtables (
    module TEXT, table_addr TEXT, cls TEXT, sub INTEGER, slot INTEGER,
    func TEXT, thunk INTEGER);
CREATE TABLE IF NOT EXISTS vcalls (
    module TEXT, caller TEXT, recv TEXT, off INTEGER, slot INTEGER, cls TEXT);
CREATE TABLE IF NOT EXISTS globals (
    module TEXT, addr TEXT, name TEXT, type TEXT, size INTEGER);
CREATE TABLE IF NOT EXISTS global_refs (module TEXT, addr TEXT, func_addr TEXT);
CREATE TABLE IF NOT EXISTS strings (module TEXT, addr TEXT, text TEXT);
CREATE TABLE IF NOT EXISTS string_refs (module TEXT, str_addr TEXT, func_addr TEXT);
CREATE TABLE IF NOT EXISTS fields (
    module TEXT, cls TEXT, off INTEGER, name TEXT, len INTEGER, type TEXT, note TEXT);
CREATE TABLE IF NOT EXISTS accesses (
    module TEXT, func_addr TEXT, cls TEXT, off INTEGER, field TEXT, kind TEXT);
CREATE TABLE IF NOT EXISTS externals (
    module TEXT, addr TEXT, lib TEXT, name TEXT, imported TEXT, PRIMARY KEY (module, addr));
CREATE TABLE IF NOT EXISTS meta (
    module TEXT PRIMARY KEY, binary TEXT, sha256 TEXT, dumped_at REAL, project_at REAL);"""

# The indices, declared apart from the schema because they have to be RECONCILED rather than
# created. `CREATE INDEX IF NOT EXISTS` matches on the index's NAME, not its definition, so
# widening one in place is a silent no-op: the old single-column index survives, every query
# still plans against it, and nothing anywhere says so. `_ensure_indices` compares each stored
# definition and rebuilds the ones that differ.
#
# `functions_addr` is the one that matters most. The table's PRIMARY KEY is (module, addr) and
# cannot serve a lookup that knows only the address -- which is how every reference query starts
# -- so without it SQLite scans all 71,235 rows, each carrying its own decompilation, and drags
# 162 MiB of TEXT past the page cache to answer `corpus func <addr>`.
INDICES = {
    "functions_addr": "functions (addr)",
    "functions_name": "functions (name)",
    "functions_ns": "functions (ns)",
    "edges_caller": "edges (module, caller, kind)",
    "edges_callee": "edges (module, callee, kind)",
    "vtables_cls": "vtables (cls, slot)",
    "vtables_func": "vtables (module, func)",
    "vtables_slot": "vtables (module, slot, sub)",
    "vcalls_caller": "vcalls (module, caller)",
    "vcalls_slot": "vcalls (module, slot, cls)",
    "vcalls_cls": "vcalls (module, cls, slot)",
    "strings_addr": "strings (module, addr)",
    "string_refs_func": "string_refs (func_addr)",
    "string_refs_str": "string_refs (module, str_addr)",
    "globals_name": "globals (name)",
    "externals_name": "externals (name)",
    "global_refs_addr": "global_refs (module, addr)",
    "global_refs_func": "global_refs (func_addr)",
    "fields_cls": "fields (cls, off)",
    "fields_off": "fields (off)",
    "accesses_off": "accesses (off, cls)",
    "accesses_field": "accesses (field)",
    "accesses_cls": "accesses (cls)",
}


def _ensure_indices(connection: sqlite3.Connection) -> None:
    """Create each index, and rebuild any whose stored definition no longer matches."""
    existing = {name: sql for name, sql in connection.execute(
        "SELECT name, sql FROM sqlite_master WHERE type = 'index' AND sql IS NOT NULL")}
    rebuilt = 0
    for name, definition in INDICES.items():
        wanted = f"CREATE INDEX {name} ON {definition}"
        if existing.get(name) == wanted:
            continue
        if name in existing:
            connection.execute(f"DROP INDEX {name}")
            rebuilt += 1
        connection.execute(wanted)
    if rebuilt:
        print(f"rebuilt {rebuilt} index(es) whose definition had changed")
    connection.commit()


# Where each module's own binary lives, relative to the VtMB install. A corpus is only as
# current as the project it was taken from, and neither fact is visible in the data itself.
BINARIES = {
    "vampire.dll": "Vampire/dlls/vampire.dll",
    "client.dll": "Vampire/cl_dlls/client.dll",
    "GameUI.dll": "Vampire/cl_dlls/GameUI.dll",
    "engine.dll": "Bin/engine.dll",
    "vguimatsurface.dll": "Bin/vguimatsurface.dll",
    "MaterialSystem.dll": "Bin/MaterialSystem.dll",
    "StudioRender.dll": "Bin/StudioRender.dll",
    "stdshader_dx8.dll": "Bin/stdshader_dx8.dll",
}

# What a member access looks like once the datamap pass has typed `this`, and what it looks like
# when it has not. The second and third forms carry no class, so they are recorded with the
# offset alone -- which is still what makes "who else touches 0x2f8" answerable module-wide.
NAMED_ACCESS = re.compile(r"\bthis->([A-Za-z_][A-Za-z0-9_]*)")
THIS_OFFSET = re.compile(r"\bthis\s*\+\s*(0x[0-9a-fA-F]+|\d+)")
THIS_INDEX = re.compile(r"\bthis\[(0x[0-9a-fA-F]+|\d+)\]")
EXPR_OFFSET = re.compile(r"\b(?:param_\d+|[a-zA-Z_]\w*Var\d+|puVar\d+|piVar\d+|iVar\d+)"
                         r"\s*\+\s*(0x[0-9a-fA-F]+)")

# What a VIRTUAL call looks like in the decompiler's output. It names a slot, never a function:
# `(**(code **)(*(int *)this + 0x30))(this)`. That is the whole reason a direct-call graph is
# blind to VtMB's game logic -- 79.6% of vampire.dll's class methods have no direct caller at
# all -- and pairing the slot back to `vtables` is what makes those edges exist.
# Matched loosely on purpose: the receiver expression is whatever the decompiler wrote, and it
# writes several shapes for the same dispatch -- `*(int *)this`, a hoisted `iVar1` already
# holding the vtable pointer, an indexed `param_1[2]`. The SLOT is stated in every one of them,
# and the slot alone answers "what could reach this virtual"; the class is read out separately
# and only where the expression actually states it.
VCALL = re.compile(r"\(\*\*\(code \*\*\)\s*\(?"
                   r"((?:[^()]|\([^()]*\)){0,60}?)"        # the cast parens have to be allowed
                   r"\s*\+\s*(0x[0-9a-fA-F]+|\d+)\s*\)\s*\)")
# Slot zero carries no displacement, so it is written without the addition.
VCALL_ZERO = re.compile(r"\(\*\*\(code \*\*\)\s*\*"
                        r"(?:\([A-Za-z_][\w ]*\*+\))?\s*"
                        r"([A-Za-z_]\w*)\s*\)\s*\)")
# The last identifier of a receiver expression, and whether the expression indexes.
RECEIVER = re.compile(r"([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*$")
# Ghidra declares its locals at the top of the body, so a receiver that is not `this` still
# states its own class when the datamap pass typed it: `CBasePlayer *pCVar3;`.
DECLARATION = re.compile(r"^\s{2}([A-Za-z_]\w*)\s*\*\s*(\w+)\s*;\s*$", re.MULTILINE)


def _corpus_dir() -> Path:
    return research_root() / "ghidra" / "corpus"


def _database() -> Path:
    return _corpus_dir() / "corpus.sqlite"


def _listing_database() -> Path:
    """The disassembly lives apart from the corpus: it is comparable in size to every decompiled
    function put together, and every MCP query opens the corpus."""
    return _corpus_dir() / "listing.sqlite"


def _migrate(connection: sqlite3.Connection) -> None:
    """Add columns a corpus built by an earlier schema does not have.

    ``CREATE TABLE IF NOT EXISTS`` mints the new tables but never widens an existing one, so a
    database carried forward would keep answering queries against a column that is not there.
    Each addition is checked and reported rather than attempted and swallowed.
    """
    for table, column, definition in (("edges", "kind", "TEXT DEFAULT 'direct'"),
                                      ("functions", "warn", "TEXT DEFAULT ''")):
        present = {row[1] for row in connection.execute(f"PRAGMA table_info({table})")}
        if not present or column in present:
            continue
        print(f"migrating: {table} gains {column}")
        connection.execute(f"ALTER TABLE {table} ADD COLUMN {column} {definition}")
    connection.commit()


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
    with log.open("r", encoding="utf-8", errors="replace") as stream:
        stream.seek(marker)
        for line in stream:
            if "Abort due to Headless analyzer error" in line or "REPORT SCRIPT ERROR" in line:
                raise RuntimeError(f"headless run failed: {line.strip()}; see {log}")


def dump(programs: list[str], project_dir: Path | None, project_name: str, limit: int) -> int:
    out = _corpus_dir()
    out.mkdir(parents=True, exist_ok=True)
    log = out / "dump.log"
    project = project_dir or (research_root() / "ghidra" / "project")
    first = True
    for program in programs:
        if not first:
            time.sleep(LOCK_DELAY_SECONDS)
        first = False
        args = [f"out={out.as_posix()}"]
        if limit:
            args.append(f"limit={limit}")
        print(f"[{program}] dump")
        _run(["-ProjDir", str(project), "-ProjName", project_name, "-NoFid",
              "-Program", program, "-Script", "DumpCorpus", "-ScriptArgs", " ".join(args)], log)
        for kind in ("functions", "strings", "globals", "fields"):
            path = out / f"{kind}-{program}.jsonl"
            print(f"  {kind}: {path.stat().st_size if path.is_file() else 0} bytes")
    return 0


def vtables(programs: list[str], project_dir: Path | None, project_name: str,
            name: bool, create: bool) -> int:
    """The class-vtable pass: cheap, and the ground truth every virtual edge is joined against.

    Kept apart from ``dump`` so it can be re-run in seconds. Folding it into the decompilation
    pass would mean paying for 63,000 functions again to change one walk.
    """
    out = _corpus_dir()
    out.mkdir(parents=True, exist_ok=True)
    log = out / "vtables.log"
    project = project_dir or (research_root() / "ghidra" / "project")
    first = True
    for program in programs:
        if not first:
            time.sleep(LOCK_DELAY_SECONDS)
        first = False
        target = out / f"vtables-{program}.jsonl"
        args = [f"out={target.as_posix()}",
                f"report={(out / f'vtables-{program}.txt').as_posix()}"]
        if name:
            args.append("name=1")
        if create:
            args.append("create=1")
        print(f"[{program}] vtables{' (naming)' if name else ''}"
              f"{' (creating)' if create else ''}")
        _run(["-ProjDir", str(project), "-ProjName", project_name, "-NoFid",
              "-Program", program, "-Script", "DumpVtables", "-ScriptArgs", " ".join(args)], log)
        print(f"  {target.stat().st_size if target.is_file() else 0} bytes")
    if name or create:
        _record_apply(programs)
    return 0


def pyapi(programs: list[str], project_dir: Path | None, project_name: str,
           apply: bool) -> int:
    """Apply CPython 2.1.2's declared C API to the modules that embed its interpreter.

    A write pass, so it runs BEFORE a dump: the corpus photographs the project, and a signature
    applied after one is a signature the corpus does not have.
    """
    out = _corpus_dir()
    api = out / "python21-api.txt"
    if not api.is_file():
        print(f"no prototype file at {api}; run `research pyapi` first")
        return 1
    log = out / "pyapi.log"
    project = project_dir or (research_root() / "ghidra" / "project")
    first = True
    for program in programs:
        if not first:
            time.sleep(LOCK_DELAY_SECONDS)
        first = False
        args = [f"api={api.as_posix()}",
                f"report={(out / f'pyapi-{program}.txt').as_posix()}"]
        if apply:
            args.append("apply=1")
        print(f"[{program}] pyapi{' (applying)' if apply else ' (report only)'}")
        _run(["-ProjDir", str(project), "-ProjName", project_name, "-NoFid",
              "-Program", program, "-Script", "ApplyPythonApi",
              "-ScriptArgs", " ".join(args)], log)
    if apply:
        _record_apply(programs)
    return 0


def externals(programs: list[str], project_dir: Path | None, project_name: str) -> int:
    """The imported-function pass: seconds, and it is what makes `callees` complete.

    A call into another DLL is recorded against a synthetic `EXTERNAL:` address that no other
    table holds a row for, so the join dropped it. This reads the external function list only,
    so recovering those names never costs a re-decompilation.
    """
    out = _corpus_dir()
    out.mkdir(parents=True, exist_ok=True)
    log = out / "externals.log"
    project = project_dir or (research_root() / "ghidra" / "project")
    first = True
    for program in programs:
        if not first:
            time.sleep(LOCK_DELAY_SECONDS)
        first = False
        target = out / f"externals-{program}.jsonl"
        print(f"[{program}] externals")
        _run(["-ProjDir", str(project), "-ProjName", project_name, "-NoFid",
              "-Program", program, "-Script", "DumpExternals",
              "-ScriptArgs", f"out={target.as_posix()}"], log)
        print(f"  {target.stat().st_size if target.is_file() else 0} bytes")
    return 0


def listing(programs: list[str], project_dir: Path | None, project_name: str,
            limit: int) -> int:
    """The disassembly pass. Slow like ``dump``, and the fallback for every damaged C."""
    out = _corpus_dir()
    out.mkdir(parents=True, exist_ok=True)
    log = out / "listing.log"
    project = project_dir or (research_root() / "ghidra" / "project")
    first = True
    for program in programs:
        if not first:
            time.sleep(LOCK_DELAY_SECONDS)
        first = False
        args = [f"out={out.as_posix()}"]
        if limit:
            args.append(f"limit={limit}")
        print(f"[{program}] listing")
        _run(["-ProjDir", str(project), "-ProjName", project_name, "-NoFid",
              "-Program", program, "-Script", "DumpListing", "-ScriptArgs", " ".join(args)], log)
        path = out / f"listing-{program}.jsonl"
        print(f"  {path.stat().st_size if path.is_file() else 0} bytes")
    return 0


def _record_apply(programs: list[str]) -> None:
    """Stamp when each module's names last changed, so the corpus can report itself stale.

    Only a pass that writes can date the change; a heuristic on the project directory's mtime
    fires on every read-only run and trains a reader to ignore it.
    """
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


def _load_module(connection: sqlite3.Connection, module: str, corpus: Path) -> tuple[int, int, int]:
    functions = corpus / f"functions-{module}.jsonl"
    if not functions.is_file():
        raise FileNotFoundError(f"{module}: no dump at {functions}; run `corpus dump {module}`")
    for table in ("functions", "edges", "strings", "string_refs", "fields", "accesses",
                  "vtables", "vcalls", "globals", "global_refs"):
        connection.execute(f"DELETE FROM {table} WHERE module = ?", (module,))

    # The vtable classes are read before the functions, because a dispatch site's receiver type
    # is only believable when it is one of them.
    vtables_path = corpus / f"vtables-{module}.jsonl"
    classes: set[str] = set()
    if vtables_path.is_file():
        with vtables_path.open(encoding="utf-8") as stream:
            for line in stream:
                record = json.loads(line)
                classes.add(record["cls"])
                connection.execute("INSERT INTO vtables VALUES (?,?,?,?,?,?,?)",
                                   (module, record["t"], record["cls"], record.get("sub") or 0,
                                    record["slot"], record["f"],
                                    1 if record.get("thunk") else 0))
    else:
        print(f"  {module}: no vtable dump; virtual edges cannot be resolved for it")

    rows = edges = accesses = 0
    with functions.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            code = record.get("c") or ""
            connection.execute(
                "INSERT OR REPLACE INTO functions VALUES (?,?,?,?,?,?,?,?,?)",
                (module, record["a"], record["n"], record.get("ns") or "", record.get("sz") or 0,
                 record.get("cc") or "", 1 if record.get("thunk") else 0, code,
                 "; ".join(record.get("w") or [])))
            rows += 1
            for callee in record.get("callees") or []:
                connection.execute("INSERT INTO edges VALUES (?,?,?,'direct')",
                                   (module, record["a"], callee))
                edges += 1
            accesses += _derive_accesses(connection, module, record, code)
            _derive_vcalls(connection, module, record, code, classes)

    _load_externals(connection, module, corpus)

    globals_path = corpus / f"globals-{module}.jsonl"
    if globals_path.is_file():
        with globals_path.open(encoding="utf-8") as stream:
            for line in stream:
                record = json.loads(line)
                connection.execute("INSERT INTO globals VALUES (?,?,?,?,?)",
                                   (module, record["a"], record["n"], record.get("t") or "",
                                    record.get("sz") or 0))
                for reference in record.get("refs") or []:
                    connection.execute("INSERT INTO global_refs VALUES (?,?,?)",
                                       (module, record["a"], reference))

    strings_path = corpus / f"strings-{module}.jsonl"
    if strings_path.is_file():
        with strings_path.open(encoding="utf-8") as stream:
            for line in stream:
                record = json.loads(line)
                connection.execute("INSERT INTO strings VALUES (?,?,?)",
                                   (module, record["a"], record["t"]))
                for reference in record.get("refs") or []:
                    connection.execute("INSERT INTO string_refs VALUES (?,?,?)",
                                       (module, record["a"], reference))

    fields_path = corpus / f"fields-{module}.jsonl"
    if fields_path.is_file():
        with fields_path.open(encoding="utf-8") as stream:
            for line in stream:
                record = json.loads(line)
                connection.execute("INSERT INTO fields VALUES (?,?,?,?,?,?,?)",
                                   (module, record["cls"], record["off"], record["name"],
                                    record.get("len") or 0, record.get("type") or "",
                                    record.get("note") or ""))
    return rows, edges, accesses


def _load_externals(connection: sqlite3.Connection, module: str, corpus: Path) -> int:
    """The imported-function names, from their own cheap sidecar.

    Separate from the function loader so `reindex` can pick up an externals pass without a
    rebuild: the pass costs seconds, and re-reading 63,000 decompilations to absorb 153 names
    would be the whole point of the split thrown away.
    """
    path = corpus / f"externals-{module}.jsonl"
    if not path.is_file():
        return 0
    rows = 0
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            connection.execute("INSERT OR REPLACE INTO externals VALUES (?,?,?,?,?)",
                               (module, record["a"], record.get("lib") or "",
                                record.get("name") or "", record.get("import") or ""))
            rows += 1
    return rows


def _derive_accesses(connection: sqlite3.Connection, module: str,
                     record: dict, code: str) -> int:
    """The field ledger, read off the decompiled expressions.

    A typed method states its members by name, so those rows carry a class and a field. An
    untyped one states an offset against a pointer the decompiler named itself; that row carries
    the offset alone. Keeping both is what lets a question about `+0x2f8` be answered across the
    module instead of only across the part that happens to be typed.
    """
    if not code:
        return 0
    owner = record.get("ns") or ""
    if owner in ("Global", "<global>"):
        owner = ""
    seen: set[tuple[str, int | None, str, str]] = set()
    for name in NAMED_ACCESS.findall(code):
        seen.add((owner, None, name, "named"))
    for pattern, kind in ((THIS_OFFSET, "this"), (THIS_INDEX, "this")):
        for raw in pattern.findall(code):
            seen.add((owner, int(raw, 0), "", kind))
    for raw in EXPR_OFFSET.findall(code):
        value = int(raw, 0)
        if value < 0x10000:                      # a struct offset, not an absolute address
            seen.add(("", value, "", "expr"))
    for cls, offset, field, kind in seen:
        connection.execute("INSERT INTO accesses VALUES (?,?,?,?,?,?)",
                           (module, record["a"], cls, offset, field, kind))
    return len(seen)


def _derive_vcalls(connection: sqlite3.Connection, module: str, record: dict,
                   code: str, classes: set[str]) -> int:
    """Every virtual dispatch the function performs, as (receiver, slot).

    The decompiler prints these as a slot against a pointer, which is why they are absent from a
    call graph built from static references. The receiver's class is resolved where the code
    states it -- `this` in a method the datamap pass typed, or a local whose declaration names a
    class -- and left null otherwise. An unresolved site is kept, not dropped: it is what lets
    "who could reach this virtual" be answered by slot even when no class is known.
    """
    if not code:
        return 0
    owner = record.get("ns") or ""
    if owner in ("Global", "<global>", "<EXTERNAL>"):
        owner = ""
    # Only a type that actually owns a vtable counts. Ghidra declares `int *piVar4;` in the same
    # shape as `CBasePlayer *pCVar1;`, and taking the first at face value attributes the site to
    # a class called `int`.
    declared = {variable: cls for cls, variable in DECLARATION.findall(code) if cls in classes}

    def classify(expression: str) -> str:
        """The receiver's class, only where the expression itself states it.

        Two disqualifiers, both of which would otherwise attribute a site to the wrong object:
        an expression with no dereference is a hoisted vtable pointer (`iVar1 + 0xa0`) and names
        no object at all, and an indexed one (`param_1[2]`) names a DIFFERENT object from the
        variable it indexes.
        """
        if "*" not in expression:
            return ""
        match = RECEIVER.search(expression)
        if match is None or match.group(2):
            return ""
        name = match.group(1)
        return owner if name == "this" else declared.get(name, "")

    seen: set[tuple[str, int]] = set()
    sites: list[tuple[str, int, int, str]] = []
    for expression, raw in VCALL.findall(code):
        offset = int(raw, 0)
        expression = expression.strip()
        if (expression, offset) in seen:
            continue
        seen.add((expression, offset))
        sites.append((expression[:60], offset, offset // 4, classify(expression)))
    for receiver in VCALL_ZERO.findall(code):
        if (receiver, 0) in seen:
            continue
        seen.add((receiver, 0))
        sites.append((receiver, 0, 0,
                      owner if receiver == "this" else declared.get(receiver, "")))

    for receiver, offset, slot, cls in sites:
        connection.execute("INSERT INTO vcalls VALUES (?,?,?,?,?,?)",
                           (module, record["a"], receiver, offset, slot, cls))
    return len(sites)


def _resolve_virtual_edges(connection: sqlite3.Connection) -> tuple[int, int]:
    """Join each resolved call site's (class, slot) to the implementation the vtable holds.

    Only the receiver's OWN class is joined. A call through a base pointer can land on any
    derived override, and inventing those edges would turn one silent wrongness into another --
    `slot <N>` reports the whole override set, and `callers` reports slot matches separately as
    possible rather than as fact.
    """
    written = ambiguous = 0
    # One caller can reach one implementation through two different (class, slot) pairs -- a
    # base and its subobject both name it -- and grouping by the pair alone wrote the edge
    # twice, so `callers` listed the same caller twice and `func` counted it twice.
    seen: set[tuple[str, str, str]] = set()
    rows = connection.execute(
        """SELECT v.module, v.caller, v.cls, v.slot, count(DISTINCT t.func) AS candidates,
                  min(t.func) AS target
           FROM vcalls v JOIN vtables t
             ON t.module = v.module AND t.cls = v.cls AND t.slot = v.slot AND t.sub = 0
           WHERE v.cls != '' GROUP BY v.module, v.caller, v.cls, v.slot""").fetchall()
    for row in rows:
        if row["candidates"] != 1:
            ambiguous += 1
            continue
        edge = (row["module"], row["caller"], row["target"])
        if edge in seen:
            continue
        seen.add(edge)
        connection.execute("INSERT INTO edges VALUES (?,?,?,'virtual')", edge)
        written += 1
    return written, ambiguous


def build(programs: list[str]) -> int:
    corpus = _corpus_dir()
    connection = sqlite3.connect(_database())
    connection.row_factory = sqlite3.Row
    connection.executescript(SCHEMA)
    _migrate(connection)
    _ensure_indices(connection)
    total = 0
    for program in programs:
        if not (corpus / f"functions-{program}.jsonl").is_file():
            print(f"[{program}] no dump on disk - skipped")
            continue
        rows, edges, accesses = _load_module(connection, program, corpus)
        _stamp(connection, program, corpus)
        connection.commit()
        print(f"[{program}] {rows} functions, {edges} call edges, {accesses} access rows")
        total += rows
    # Resolve each field row against the ledger's named accesses, so a named access also
    # answers an offset query.
    connection.execute("""
        UPDATE accesses SET off = (
            SELECT f.off FROM fields f
            WHERE f.module = accesses.module AND f.cls = accesses.cls
              AND f.name = accesses.field)
        WHERE off IS NULL AND field != '' AND cls != ''""")
    connection.commit()
    resolved = connection.execute(
        "SELECT count(*) FROM accesses WHERE kind = 'named' AND off IS NOT NULL").fetchone()[0]
    unresolved = connection.execute(
        "SELECT count(*) FROM accesses WHERE kind = 'named' AND off IS NULL").fetchone()[0]
    print(f"ledger: {resolved} named accesses carry an offset, {unresolved} name a field that "
          f"no class structure states (an inherited or non-datamap member)")

    # The call graph's missing half. Rebuilt wholesale rather than per module, because a virtual
    # edge is a join across two tables and a module reloaded alone would leave the old join
    # behind.
    connection.execute("DELETE FROM edges WHERE kind = 'virtual'")
    virtual, ambiguous = _resolve_virtual_edges(connection)
    sites = connection.execute("SELECT count(*) FROM vcalls").fetchone()[0]
    unknown = connection.execute("SELECT count(*) FROM vcalls WHERE cls = ''").fetchone()[0]
    connection.commit()
    print(f"virtual call graph: {sites} dispatch sites, {sites - unknown} name their receiver's "
          f"class, {virtual} resolved to an edge, {ambiguous} left unresolved because the class "
          f"holds more than one function at that slot, {unknown} carry no class (queryable by "
          f"slot with `corpus slot <N>`)")

    _build_index(connection)
    # The planner picks between these indices by guesswork until it has seen their shape, and
    # guesses badly on a table whose rows carry a decompilation each.
    connection.execute("ANALYZE")
    connection.commit()
    print(f"database: {_database()}  ({total} functions)")
    connection.close()
    _build_listing(programs)
    return 0


def reindex() -> int:
    """Everything `build` derives from rows already in the database, without re-reading a dump.

    The two derived structures -- the search index and the virtual half of the call graph -- are
    the ones whose *rules* change while the decompilation underneath does not. Re-running the
    Ghidra pass to pick up a tokenizer change would cost an overnight run for nothing.
    """
    if not _database().is_file():
        print(f"no corpus at {_database()}; run `corpus build` first")
        return 1
    connection = sqlite3.connect(_database())
    connection.row_factory = sqlite3.Row
    # The schema first: `_ensure_indices` indexes tables, and a table the running schema declares
    # but this database predates does not exist yet.
    connection.executescript(SCHEMA)
    _migrate(connection)
    _ensure_indices(connection)
    loaded = sum(_load_externals(connection, one, _corpus_dir()) for one in PROGRAMS)
    if loaded:
        print(f"imported functions: {loaded} names loaded from the externals sidecars")
    connection.execute("DELETE FROM edges WHERE kind = 'virtual'")
    virtual, ambiguous = _resolve_virtual_edges(connection)
    connection.commit()
    print(f"virtual call graph: {virtual} edges, {ambiguous} sites left unresolved because the "
          f"class holds more than one function at that slot")
    _build_index(connection)
    connection.execute("ANALYZE")
    connection.commit()
    connection.close()
    print(f"database: {_database()}  {_database().stat().st_size // (1 << 20)} MiB")
    return 0


def _build_listing(programs: list[str]) -> None:
    """Load whatever disassembly dumps exist into their own database."""
    corpus = _corpus_dir()
    present = [one for one in programs if (corpus / f"listing-{one}.jsonl").is_file()]
    if not present:
        return
    connection = sqlite3.connect(_listing_database())
    connection.execute("CREATE TABLE IF NOT EXISTS listing ("
                       "module TEXT, addr TEXT, asm TEXT, PRIMARY KEY (module, addr))")
    for module in present:
        connection.execute("DELETE FROM listing WHERE module = ?", (module,))
        rows = 0
        with (corpus / f"listing-{module}.jsonl").open(encoding="utf-8") as stream:
            for line in stream:
                record = json.loads(line)
                connection.execute("INSERT OR REPLACE INTO listing VALUES (?,?,?)",
                                   (module, record["a"], record["asm"]))
                rows += 1
        connection.commit()
        print(f"[{module}] {rows} functions disassembled")
    connection.close()
    print(f"listing: {_listing_database()}")


def _build_index(connection: sqlite3.Connection) -> None:
    """A full-text index over the decompiled C, used only to PREFILTER `grep`.

    60 MB of TEXT is ~100 ms to scan per query, paid on every question. The index narrows the
    scan and the regex still decides -- but only if the index and the regex agree on what a
    match is, and a word tokenizer does NOT. `grep` searches for substrings: `/Melee/` must find
    `CBaseCombatCharacter::MeleeSwingUpdate`. A word tokenizer indexes that identifier as one
    token, so `MATCH "Melee"` returns nothing for it, and the prefilter turns 1,258 real hits
    into 152. Measured, before this was a trigram index: /Disciplin/ 451 -> 6, /NextAttack/
    52 -> 0, /haracter/ 1,634 -> 0. Every one of those answered "N function(s) match" with a
    number that was simply wrong.

    `tokenize='trigram'` indexes every 3-character window instead, which is exactly substring
    semantics, and is case-insensitive by default -- the same as `grep`'s own `re.IGNORECASE`.
    It costs roughly 3.5x the code size on disk. That is the price of an answer that is not
    silently short.
    """
    try:
        connection.execute("DROP TABLE IF EXISTS code_fts")
        connection.execute("CREATE VIRTUAL TABLE code_fts USING fts5("
                           "code, content='functions', content_rowid='rowid', "
                           "tokenize='trigram')")
        connection.execute("INSERT INTO code_fts(rowid, code) "
                           "SELECT rowid, code FROM functions WHERE code != ''")
        connection.commit()
        count = connection.execute("SELECT count(*) FROM code_fts").fetchone()[0]
        print(f"search index: {count} functions indexed (trigram)")
    except sqlite3.OperationalError as error:
        # FTS5 is compiled into every stock CPython, but a stripped SQLite would fail here and
        # `grep` has to know the index is absent rather than query a table that is not there.
        print(f"search index NOT built ({error}); `grep` will scan the whole corpus")


def _stamp(connection: sqlite3.Connection, module: str, corpus: Path) -> None:
    """Record what this module's rows were taken from, so a later reader can tell it is stale."""
    relative = BINARIES.get(module)
    binary = (vtmb_root() / relative) if relative else None
    if binary is None or not binary.is_file():
        print(f"  {module}: binary not found; this module's rows carry no staleness stamp")
        digest, path = "", ""
    else:
        digest, path = _sha256(binary), str(binary)
    dumped = (corpus / f"functions-{module}.jsonl").stat().st_mtime
    connection.execute("INSERT OR REPLACE INTO meta VALUES (?,?,?,?,?)",
                       (module, path, digest, dumped, dumped))


# The MCP server is long-lived and answers many queries against an unchanging database, so the
# per-query setup is the cost that matters there, not the query. Both of these are held open:
# the connection (whose page cache is the point) and the staleness verdict (which reads every
# module's binary end to end).
_CONNECTION: sqlite3.Connection | None = None
_LISTING: sqlite3.Connection | None = None
_STALENESS: tuple[tuple, list[str]] | None = None


def _listing_connection() -> sqlite3.Connection | None:
    """The disassembly database, opened once. None when it has not been built."""
    global _LISTING
    if _LISTING is not None:
        return _LISTING
    if not _listing_database().is_file():
        return None
    _LISTING = sqlite3.connect(_listing_database())
    _LISTING.row_factory = sqlite3.Row
    for pragma in ("mmap_size = 1073741824", "cache_size = -65536", "temp_store = MEMORY"):
        _LISTING.execute(f"PRAGMA {pragma}")
    return _LISTING


def _connect() -> sqlite3.Connection:
    global _CONNECTION
    if _CONNECTION is not None:
        for warning in _staleness(_CONNECTION):
            print(warning)
        return _CONNECTION
    if not _database().is_file():
        raise FileNotFoundError(f"no corpus at {_database()}; run `corpus build` first")
    connection = sqlite3.connect(_database())
    connection.row_factory = sqlite3.Row
    # Read-only work over a 162 MiB database: map it rather than copying pages through the
    # cache, and keep enough of it resident that a second query does not re-read the index.
    for pragma in ("mmap_size = 1073741824", "cache_size = -262144",
                   "temp_store = MEMORY", "synchronous = OFF"):
        connection.execute(f"PRAGMA {pragma}")
    # Idempotent, and it means a corpus built by an earlier schema answers a query about a table
    # it has never held -- empty, rather than raising about a table that does not exist.
    connection.executescript(SCHEMA)
    _migrate(connection)
    _ensure_indices(connection)
    _CONNECTION = connection
    for warning in _staleness(connection):
        print(warning)
    return connection


def _staleness(connection: sqlite3.Connection) -> list[str]:
    """A corpus is a photograph of the project, and the photograph does not say when it was
    taken. Two things invalidate it silently: the binary changing under it, and a naming or
    typing pass running after it -- after which every answer here is the old names, with nothing
    to say a newer set exists. The second is dated by the passes themselves (`last-apply.json`),
    because the project directory's own mtime moves on every read-only headless run and a
    warning that always fires is one nobody reads.
    """
    global _STALENESS
    warnings: list[str] = []
    try:
        rows = connection.execute("SELECT * FROM meta").fetchall()
    except sqlite3.OperationalError:
        return ["corpus predates staleness stamping; re-run `corpus build` to record it"]

    marker = research_root() / "ghidra" / "last-apply.json"

    # Hashing eight DLLs end to end costs ~80 ms, and the answer only changes when one of the
    # inputs does. Key the verdict on their cheap stats -- size and mtime of every binary, plus
    # the apply stamp -- so a repeat query pays nothing while any real change still re-hashes.
    def _fingerprint() -> tuple:
        parts: list = [marker.stat().st_mtime_ns if marker.is_file() else 0]
        for one in rows:
            path = Path(one["binary"]) if one["binary"] else None
            if path is not None and path.is_file():
                stat = path.stat()
                parts.append((one["module"], stat.st_size, stat.st_mtime_ns))
            else:
                parts.append((one["module"], 0, 0))
        return tuple(parts)

    key = _fingerprint()
    if _STALENESS is not None and _STALENESS[0] == key:
        return _STALENESS[1]
    applied: dict[str, float] = {}
    if marker.is_file():
        try:
            applied = json.loads(marker.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            warnings.append(f"last-apply stamp is unreadable ({error}); staleness is unchecked")

    for row in rows:
        binary = Path(row["binary"]) if row["binary"] else None
        if binary is not None and binary.is_file() and _sha256(binary) != row["sha256"]:
            warnings.append(f"STALE: {row['module']} on disk is not the binary this corpus was "
                            f"taken from; re-dump it")
        when = applied.get(row["module"])
        if when and when > (row["dumped_at"] or 0.0):
            warnings.append(f"STALE: {row['module']} was renamed or retyped after this corpus "
                            f"was dumped; re-run `corpus dump {row['module']}`")
    _STALENESS = (key, warnings)
    return warnings


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _modules(connection: sqlite3.Connection) -> list[str]:
    return [row[0] for row in connection.execute(
        "SELECT DISTINCT module FROM functions ORDER BY module")]


def _known_module(connection: sqlite3.Connection, module: str) -> bool:
    return connection.execute("SELECT 1 FROM meta WHERE module = ? UNION "
                              "SELECT 1 FROM functions WHERE module = ? LIMIT 1",
                              (module, module)).fetchone() is not None


def _contains(text: str) -> str:
    """A LIKE pattern that matches `text` as a literal substring.

    Interpolating a search term straight into `%…%` hands the caller the wildcards: `%` matches
    everything and `_` matches anything at all, so `corpus func %` answered with twenty
    arbitrary functions and looked exactly like twenty real hits. `\\` is the escape, declared
    by the ESCAPE clause every caller of this pairs it with.
    """
    escaped = text.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_")
    return f"%{escaped}%"


def _resolve(connection: sqlite3.Connection, reference: str) -> list[sqlite3.Row]:
    """A function by address, by bare name, or by `Class::method`."""
    reference = reference.strip()
    if not reference:
        return []
    if "::" in reference:
        cls, _, member = reference.partition("::")
        return connection.execute(
            "SELECT * FROM functions WHERE ns = ? AND name = ?", (cls, member)).fetchall()
    # Normalised in Python, not in SQL. `lower(addr) = lower(?)` applies a function to the
    # COLUMN, which makes `functions_addr` unusable and sends every reference lookup -- the one
    # each of func/code/callers/callees/asm/twin begins with -- back to a full scan of 71,235
    # rows that each carry a decompilation.
    candidate = reference.lower()
    if candidate.startswith("0x"):
        candidate = candidate[2:]
    rows = connection.execute(
        "SELECT * FROM functions WHERE addr = ?", (candidate,)).fetchall()
    if rows:
        return rows
    # Exact name first for the same reason: a leading-wildcard LIKE cannot use an index, and
    # OR-ing it beside the equality denies the index to both halves.
    rows = connection.execute(
        "SELECT * FROM functions WHERE name = ?", (reference,)).fetchall()
    if rows:
        return rows
    return connection.execute(
        "SELECT * FROM functions WHERE name LIKE ? ESCAPE '\\' LIMIT 21",
        (_contains(reference),)).fetchall()


def _miss(reference: str) -> None:
    """Why nothing matched, in the terms the caller used."""
    if not reference.strip():
        print("no reference given; pass an address (10161200), a name, or Class::method")
    else:
        print(f"no function matches {reference!r} — tried it as an address, as an exact name, "
              f"and as a name substring")


def _truncated(rows: list, shown: int, reference: str) -> None:
    """`_resolve` fetches one row past what it shows, so a cut list can say it was cut."""
    if len(rows) > shown:
        print(f"… more than {shown} functions have {reference!r} in their name; narrow it or "
              f"pass an address")


def _label(row: sqlite3.Row) -> str:
    owner = row["ns"]
    name = row["name"] if owner in ("", "Global") else f"{owner}::{row['name']}"
    return f"{row['module']:18} {row['addr']:10} {name}"


def command_func(reference: str) -> int:
    connection = _connect()
    rows = _resolve(connection, reference)
    if not rows:
        _miss(reference)
        return 1
    for row in rows[:20]:
        direct = connection.execute(
            "SELECT count(*) FROM edges WHERE module = ? AND callee = ? AND kind = 'direct'",
            (row["module"], row["addr"])).fetchone()[0]
        virtual = connection.execute(
            "SELECT count(*) FROM edges WHERE module = ? AND callee = ? AND kind = 'virtual'",
            (row["module"], row["addr"])).fetchone()[0]
        callees = connection.execute(
            "SELECT count(*) FROM edges WHERE module = ? AND caller = ?",
            (row["module"], row["addr"])).fetchone()[0]
        strings = connection.execute(
            "SELECT count(*) FROM string_refs WHERE module = ? AND func_addr = ?",
            (row["module"], row["addr"])).fetchone()[0]
        slots = connection.execute(
            "SELECT DISTINCT cls, slot FROM vtables WHERE module = ? AND func = ? ORDER BY slot",
            (row["module"], row["addr"])).fetchall()
        print(f"{_label(row)}  {row['cc']}  {row['size']} bytes"
              f"  callers={direct} direct/{virtual} virtual callees={callees} strings={strings}"
              f"{'  [thunk]' if row['thunk'] else ''}")
        if slots:
            where = ", ".join(f"{one['cls']}#{one['slot']}" for one in slots[:8])
            print(f"    vtable slot of {len(slots)} class(es): {where}"
                  + (" …" if len(slots) > 8 else ""))
        _damage(row)
    _truncated(rows, 20, reference)
    return 0


# Which decompiler warnings mean the C cannot be read as the function, and which are noise.
#
# The distinction is the whole value of the flag. `Globals starting with '_' overlap smaller
# symbols` is cosmetic and rides on 696 of stdshader_dx8's 1,294 functions; bannering those would
# mark half the corpus as damaged and train a reader to skip the banner -- which is precisely the
# failure the banner exists to prevent. Only a warning that changes what the C MEANS is severe.
# How much of one answer a caller should have to read before deciding it is the wrong answer.
# Every tool here feeds an agent's context, so an unbounded print is a cost the caller cannot
# refuse; each cap says what it cut and how to get the rest.
CODE_LIMIT = 60_000
ROW_LIMIT = 400


SEVERE_WARNINGS = (
    "Removing unreachable block",        # the C is not the whole function
    "Could not recover jumptable",       # control flow is wrong: a switch printed as a call
    "Treating indirect jump as call",
    "Bad instruction",
    "Truncating control flow",
    "Unable to track spacebase",         # the locals are not the function's locals
    "Stack frame is not setup normally",
    "Type propagation algorithm not settling",
)


def _severe(warn: str) -> list[str]:
    return [one for one in SEVERE_WARNINGS if one in (warn or "")]


def _damage(row: sqlite3.Row) -> None:
    """Say when the decompiler's own output is not to be trusted whole."""
    warn = row["warn"] if "warn" in row.keys() else ""
    severe = _severe(warn)
    if not severe:
        return
    print(f"    !! DAMAGED DECOMPILATION — {'; '.join(severe)}")
    print(f"    !! read the listing instead: corpus asm {row['addr']}")


def command_code(reference: str) -> int:
    connection = _connect()
    rows = _resolve(connection, reference)
    if not rows:
        _miss(reference)
        return 1
    for row in rows[:3]:
        print(f"// {_label(row)}")
        _damage(row)
        code = row["code"] or "// no decompilation in the corpus"
        # A boundary defect makes one "function" out of dozens, and 104126e0 decompiles to
        # 300 KB. Handing that back whole is not an answer -- it is the caller's whole reading
        # budget spent on a function that `outliers` already says is not one function.
        if len(code) > CODE_LIMIT:
            lines = code.splitlines()
            kept = 0
            for index, line in enumerate(lines):
                kept += len(line) + 1
                if kept > CODE_LIMIT:
                    break
            print("\n".join(lines[:index]))
            print(f"\n// … truncated at {CODE_LIMIT // 1000} KB of {len(code) // 1000} KB "
                  f"({index} of {len(lines)} lines). This body is {row['size']} bytes, which "
                  f"is a boundary defect more often than a real function — `corpus outliers` "
                  f"lists them, and `corpus grep` searches inside it without printing it.")
        else:
            print(code)
    _truncated(rows, 3, reference)
    return 0


def command_asm(reference: str) -> int:
    """The disassembly, from the listing database beside the corpus."""
    connection = _connect()
    rows = _resolve(connection, reference)
    if not rows:
        _miss(reference)
        return 1
    listings = _listing_connection()
    if listings is None:
        print(f"no listing database at {_listing_database()}; run `corpus listing` then "
              f"`corpus build` to create it")
        return 1
    for row in rows[:2]:
        print(f"// {_label(row)}")
        found = listings.execute("SELECT asm FROM listing WHERE module = ? AND addr = ?",
                                 (row["module"], row["addr"])).fetchone()
        print(found["asm"] if found else "// this function is not in the listing database")
    return 0


def command_hop(reference: str, direction: str) -> int:
    connection = _connect()
    rows = _resolve(connection, reference)
    if not rows:
        _miss(reference)
        return 1
    row = rows[0]
    if direction == "callees":
        found = connection.execute(
            """SELECT f.*, e.kind FROM edges e JOIN functions f
               ON f.module = e.module AND f.addr = e.callee
               WHERE e.module = ? AND e.caller = ? ORDER BY e.kind, f.addr""",
            (row["module"], row["addr"])).fetchall()
        print(f"{len(found)} callees of {_label(row)}")
        for one in found:
            print(f"  {_label(one)}  [{one['kind']}]")
        # Ghidra files a call into another DLL under an `EXTERNAL:` address, which is not in
        # this module's function table -- so the JOIN above drops it and the count above is
        # short by however many imports the function calls. Silently, until now.
        imported = connection.execute(
            """SELECT DISTINCT e.callee, x.lib, x.name, x.imported FROM edges e
               LEFT JOIN externals x ON x.module = e.module AND x.addr = e.callee
               WHERE e.module = ? AND e.caller = ? AND e.callee LIKE 'EXTERNAL:%'
               ORDER BY x.lib, x.name, e.callee""",
            (row["module"], row["addr"])).fetchall()
        if imported:
            print(f"\n{len(imported)} call(s) into another DLL — imported, so the corpus holds "
                  f"no body:")
            for one in imported:
                if one["name"]:
                    original = (f"  (imported as {one['imported']})"
                                if one["imported"] and one["imported"] != one["name"] else "")
                    print(f"    {(one['lib'] or '?'):22} {one['name']}{original}")
                else:
                    # No name means the externals pass has not run for this module. Say which
                    # pass, rather than printing a synthetic address and no explanation.
                    print(f"    {one['callee']}  <no name — run `corpus externals "
                          f"{row['module']}`>")
        unresolved = connection.execute(
            "SELECT DISTINCT slot, recv FROM vcalls WHERE module = ? AND caller = ? AND cls = ''"
            " ORDER BY slot", (row["module"], row["addr"])).fetchall()
        if unresolved:
            print(f"\n{len(unresolved)} virtual dispatch(es) whose receiver class the code does "
                  f"not state:")
            for one in unresolved:
                print(f"    slot {one['slot']:4} on {one['recv']}"
                      f"    (corpus slot {one['slot']})")
        return 0

    direct = connection.execute(
        """SELECT f.* FROM edges e JOIN functions f
           ON f.module = e.module AND f.addr = e.caller
           WHERE e.module = ? AND e.callee = ? AND e.kind = 'direct' ORDER BY f.addr""",
        (row["module"], row["addr"])).fetchall()
    virtual = connection.execute(
        """SELECT f.* FROM edges e JOIN functions f
           ON f.module = e.module AND f.addr = e.caller
           WHERE e.module = ? AND e.callee = ? AND e.kind = 'virtual' ORDER BY f.addr""",
        (row["module"], row["addr"])).fetchall()

    print(f"{_label(row)}")
    print(f"\n{len(direct)} DIRECT caller(s) — a static call reference:")
    for one in direct:
        print("  " + _label(one))

    print(f"\n{len(virtual)} VIRTUAL caller(s) — the site's receiver class holds this function "
          f"at that slot:")
    for one in virtual:
        print("  " + _label(one))

    # Every table this function sits in, so the possible set is the union of its slots.
    slots = connection.execute(
        "SELECT DISTINCT slot FROM vtables WHERE module = ? AND func = ?",
        (row["module"], row["addr"])).fetchall()
    if not slots:
        print("\nthis function is in no class vtable, so it has no virtual call sites")
        return 0
    numbers = [one["slot"] for one in slots]
    placeholders = ",".join("?" * len(numbers))
    possible = connection.execute(
        f"""SELECT f.*, v.slot, v.recv FROM vcalls v JOIN functions f
            ON f.module = v.module AND f.addr = v.caller
            WHERE v.module = ? AND v.cls = '' AND v.slot IN ({placeholders})
            ORDER BY v.slot, f.addr""",
        [row["module"], *numbers]).fetchall()
    print(f"\n{len(possible)} POSSIBLE caller(s) — a dispatch at slot "
          f"{', '.join(str(n) for n in numbers)} whose receiver class the code does not state. "
          f"These are candidates, not facts:")
    for one in possible[:60]:
        print(f"  {_label(one)}  slot {one['slot']} on {one['recv']}")
    if len(possible) > 60:
        print(f"  … {len(possible) - 60} more")

    # A call through a BASE pointer resolves to the base's own implementation, so an override is
    # not reachable from it by this join. Those sites are counted rather than listed: they are
    # only callers of this function if the object was of a derived type at run time, which no
    # static reading decides.
    elsewhere = connection.execute(
        f"""SELECT count(*) FROM vcalls v
            WHERE v.module = ? AND v.cls != '' AND v.slot IN ({placeholders})
              AND v.cls NOT IN (SELECT cls FROM vtables
                                WHERE module = ? AND func = ?)""",
        [row["module"], *numbers, row["module"], row["addr"]]).fetchone()[0]
    if elsewhere:
        print(f"\n{elsewhere} further dispatch site(s) reach slot "
              f"{', '.join(str(n) for n in numbers)} through a DIFFERENT class — they land here "
              f"only if the object was of this type at run time. `corpus slot "
              f"{numbers[0]}` lists every class that fills it.")
    return 0


LITERAL = re.compile(r"[A-Za-z_][A-Za-z0-9_]{3,}")
METACHARACTERS = set(r".^$*+?{}[]\|()")


def _prefilter(pattern: str) -> str | None:
    """The longest literal run a regex must contain, usable as a full-text prefilter.

    Two conditions, and both are load-bearing:

    * no metacharacter anywhere -- a literal lifted out of an alternation or an optional group
      is not required to appear in a match, and prefiltering on one silently drops hits;
    * at least three characters -- a trigram index cannot answer a query shorter than one
      trigram, and asking it anyway returns nothing rather than everything.
    """
    if any(character in METACHARACTERS for character in pattern):
        return None
    words = [word for word in LITERAL.findall(pattern) if len(word) >= 3]
    return max(words, key=len) if words else None


def _index_is_trigram(connection: sqlite3.Connection) -> bool:
    """Whether `code_fts` can answer a substring query.

    A corpus built before the tokenizer changed still has a `code_fts`, still answers MATCH, and
    still returns a number -- just the wrong one. The prefilter is only safe over an index whose
    idea of a match is `grep`'s, so the stored definition is read rather than assumed.
    """
    row = connection.execute(
        "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'code_fts'").fetchone()
    return bool(row and "trigram" in (row[0] or ""))


def command_grep(pattern: str, module: str | None, limit: int) -> int:
    connection = _connect()
    if not pattern.strip():
        print("grep needs a pattern; an empty one matches every line of all 71,235 functions")
        return 1
    try:
        expression = re.compile(pattern, re.IGNORECASE)
    except re.error as error:
        print(f"{pattern!r} is not a valid regular expression: {error}")
        return 1
    limit = max(1, limit)
    if module and not _known_module(connection, module):
        print(f"no module named {module!r} in the corpus; it holds "
              f"{', '.join(_modules(connection))}")
        return 1
    query = "SELECT * FROM functions WHERE code != ''"
    parameters: list[str] = []

    literal = _prefilter(pattern) if _index_is_trigram(connection) else None
    if literal:
        try:
            # Through a temporary table rather than an IN list: a common literal matches tens of
            # thousands of functions, and SQLite caps how many bound parameters one statement
            # may carry.
            connection.execute("DROP TABLE IF EXISTS temp.candidates")
            connection.execute("CREATE TEMP TABLE candidates (rowid_ INTEGER PRIMARY KEY)")
            connection.execute("INSERT OR IGNORE INTO candidates "
                               "SELECT rowid FROM code_fts WHERE code_fts MATCH ?",
                               (f'"{literal}"',))
            found = connection.execute("SELECT count(*) FROM candidates").fetchone()[0]
            query = ("SELECT * FROM functions WHERE rowid IN (SELECT rowid_ FROM candidates)")
            print(f"(indexed on {literal!r}: {found} candidate functions)")
        except sqlite3.OperationalError as error:
            print(f"(no search index — scanning the whole corpus: {error})")
    else:
        print("(scanning every decompiled function — the pattern carries no literal the "
              "index can narrow on)")
    if module:
        query += " AND module = ?"
        parameters.append(module)
    hits = 0
    for row in connection.execute(query, parameters):
        lines = [line for line in (row["code"] or "").splitlines() if expression.search(line)]
        if not lines:
            continue
        print(_label(row))
        _damage(row)
        for line in lines[:6]:
            print("    " + line.strip())
        hits += 1
        if hits >= limit:
            print(f"... stopped at {limit} functions; narrow the pattern or raise --limit")
            break
    print(f"{hits} function(s) match /{pattern}/")
    return 0


def command_str(text: str, limit: int) -> int:
    connection = _connect()
    if not text:
        print("string needs some text to look for")
        return 1
    rows = connection.execute(
        "SELECT * FROM strings WHERE text LIKE ? ESCAPE '\\' ORDER BY module, addr LIMIT ?",
        (_contains(text), max(1, limit))).fetchall()
    for row in rows:
        referencing = connection.execute(
            """SELECT f.* FROM string_refs r LEFT JOIN functions f
               ON f.module = r.module AND f.addr = r.func_addr
               WHERE r.module = ? AND r.str_addr = ?""",
            (row["module"], row["addr"])).fetchall()
        print(f"{row['module']:18} {row['addr']:10} {row['text'][:100]!r}")
        for one in referencing:
            print("    " + (_label(one) if one["addr"] else "    <unowned code>"))
    print(f"{len(rows)} string(s) match {text!r}")
    return 0


def command_vtable(cls: str) -> int:
    """A class's dispatch table, slot by slot — the shape of its virtual interface."""
    connection = _connect()
    # The caller counts come back in ONE grouped query rather than one per slot: a wide class
    # has hundreds of slots, and a per-slot count turns a single answer into hundreds of
    # round trips.
    rows = connection.execute(
        """SELECT v.*, f.name, f.ns, f.size, f.warn,
                  (SELECT count(*) FROM edges e
                   WHERE e.module = v.module AND e.callee = v.func) AS callers
           FROM vtables v
           LEFT JOIN functions f ON f.module = v.module AND f.addr = v.func
           WHERE v.cls = ? ORDER BY v.module, v.sub, v.slot""", (cls,)).fetchall()
    if not rows:
        print(f"no vftable in the corpus belongs to {cls!r}")
        return 1
    module = None
    for row in rows:
        if row["module"] != module:
            module = row["module"]
            print(f"\n{module}  table {row['table_addr']}"
                  + (f"  subobject at +{row['sub']}" if row["sub"] else ""))
        name = row["name"] or "<not a function in the corpus>"
        owner = f"{row['ns']}::" if row["ns"] and row["ns"] != "Global" else ""
        print(f"  #{row['slot']:<4} {row['func']}  {owner}{name:44} callers={row['callers']}"
              f"{'  [thunked]' if row['thunk'] else ''}")
    print(f"\n{len(rows)} slot(s) in {cls}")
    return 0


def command_slot(slot: int, module: str | None) -> int:
    """Every class that fills one slot — one virtual compared across the whole hierarchy.

    This is also the honest answer to "who overrides this": a dispatch through a base pointer can
    land on any of these, and no static analysis narrows it further.
    """
    connection = _connect()
    # The class name comes from the same join the listing needs, so the whole answer is one
    # query rather than one per class.
    query = ("SELECT v.*, f.ns, f.name FROM vtables v LEFT JOIN functions f"
             " ON f.module = v.module AND f.addr = v.func"
             " WHERE v.slot = ? AND v.sub = 0")
    parameters: list = [slot]
    if module:
        query += " AND v.module = ?"
        parameters.append(module)
    rows = connection.execute(query + " ORDER BY v.module, v.cls", parameters).fetchall()
    if not rows:
        # Which absence it is decides what the caller does next: a slot nothing fills anywhere
        # is an answer, while a slot filled only in another module is a wrong question.
        elsewhere = connection.execute(
            "SELECT module, count(*) n FROM vtables WHERE slot = ? AND sub = 0 "
            "GROUP BY module ORDER BY n DESC", (slot,)).fetchall()
        if module and elsewhere:
            where = ", ".join(f"{one['module']} ({one['n']})" for one in elsewhere)
            print(f"no vftable in {module} has a slot {slot}; it is filled in {where}")
        elif elsewhere:
            print(f"no vftable in the corpus has a slot {slot}")
        else:
            widest = connection.execute("SELECT max(slot) FROM vtables").fetchone()[0]
            print(f"no vftable in the corpus has a slot {slot}; the widest table has "
                  f"{widest + 1} slots (0–{widest})")
        return 1
    for row in rows[:ROW_LIMIT]:
        print(f"  {row['module']:18} {row['cls']:42} {row['func']}  "
              + (f"{row['ns']}::{row['name']}" if row["name"] else "<not a function>"))
    if len(rows) > ROW_LIMIT:
        print(f"  … {len(rows) - ROW_LIMIT} more class(es) not listed; pass a module to narrow "
              f"it")
    sites = connection.execute(
        "SELECT count(*) FROM vcalls WHERE slot = ?" + (" AND module = ?" if module else ""),
        parameters).fetchone()[0]
    print(f"\n{len(rows)} class(es) fill slot {slot}; {sites} dispatch site(s) target it")
    return 0


def command_globals(text: str, limit: int) -> int:
    """A global datum and every function that reaches it."""
    connection = _connect()
    rows = connection.execute(
        "SELECT * FROM globals WHERE name LIKE ? ESCAPE '\\' ORDER BY module, addr LIMIT ?",
        (_contains(text), max(1, limit))).fetchall()
    if not rows:
        print(f"no global name matches {text!r}")
        return 1
    for row in rows:
        referencing = connection.execute(
            """SELECT f.* FROM global_refs r JOIN functions f
               ON f.module = r.module AND f.addr = r.func_addr
               WHERE r.module = ? AND r.addr = ? ORDER BY f.addr""",
            (row["module"], row["addr"])).fetchall()
        print(f"{row['module']:18} {row['addr']:10} {row['name']}"
              f"  {row['type']} [{row['size']} bytes]  {len(referencing)} referrer(s)")
        for one in referencing[:40]:
            print("    " + _label(one))
        if len(referencing) > 40:
            print(f"    … {len(referencing) - 40} more")
    return 0


def command_outliers(minimum: int) -> int:
    """Functions whose body swallowed its neighbours, and functions the decompiler damaged.

    A function far past the size distribution is almost always a boundary defect rather than a
    large function: everything inside it is filed under one address, so `callers` answers for the
    wrong function and the code actually wanted has no name of its own.
    """
    connection = _connect()
    print(f"# bodies of at least {minimum} bytes")
    rows = connection.execute(
        """SELECT f.*, (SELECT count(*) FROM edges e
                        WHERE e.module = f.module AND e.callee = f.addr) AS callers
           FROM functions f WHERE size >= ? ORDER BY size DESC LIMIT 60""",
        (minimum,)).fetchall()
    for row in rows:
        print(f"  {_label(row)}  {row['size']:7} bytes  callers={row['callers']}")
    print(f"  {len(rows)} function(s)")

    print("\n# damaged decompilations — only warnings that change what the C means")
    counts: dict[str, int] = {}
    severe_total = 0
    for row in connection.execute("SELECT warn FROM functions WHERE warn != ''"):
        severe = _severe(row["warn"])
        if not severe:
            continue
        severe_total += 1
        for one in severe:
            counts[one] = counts.get(one, 0) + 1
    for text, count in sorted(counts.items(), key=lambda pair: -pair[1]):
        print(f"  {count:6}  {text}")
    cosmetic = connection.execute(
        "SELECT count(*) FROM functions WHERE warn != ''").fetchone()[0] - severe_total
    print(f"  {severe_total} function(s) are damaged; {cosmetic} more carry only a cosmetic "
          f"warning and are NOT flagged")
    return 0


def command_suggest(limit: int) -> int:
    """Unnamed functions that only one class ever calls — candidates, deliberately not applied.

    Ownership by call site is an inference, not a name the image states, so this reports and
    stops. `only change what we understand` is the rule it is respecting.
    """
    connection = _connect()
    rows = connection.execute(
        """SELECT f.module, f.addr, f.size, count(DISTINCT p.ns) AS owners,
                  min(p.ns) AS owner, count(*) AS sites
           FROM functions f
           JOIN edges e ON e.module = f.module AND e.callee = f.addr
           JOIN functions p ON p.module = e.module AND p.addr = e.caller
           WHERE f.name LIKE 'FUN_%' AND p.ns NOT IN ('', 'Global', '<EXTERNAL>')
           GROUP BY f.module, f.addr HAVING owners = 1
           ORDER BY sites DESC, f.size DESC LIMIT ?""", (limit,)).fetchall()
    for row in rows:
        print(f"  {row['module']:18} {row['addr']:10} {row['size']:6} bytes"
              f"  called only by {row['owner']} ({row['sites']} site(s))")
    print(f"{len(rows)} candidate(s). Nothing was renamed.")
    return 0


def command_twin(reference: str) -> int:
    """The same function in another module.

    `vampire.dll` and `client.dll` are the server and client halves of one codebase and share an
    imagebase, so an address alone is ambiguous and a recovered name is the only join that means
    anything.
    """
    connection = _connect()
    rows = _resolve(connection, reference)
    if not rows:
        _miss(reference)
        return 1
    row = rows[0]
    if row["name"].startswith("FUN_"):
        print(f"{_label(row)} has no recovered name, so it cannot be matched across modules")
        return 1
    found = connection.execute(
        "SELECT * FROM functions WHERE name = ? AND ns = ? ORDER BY module",
        (row["name"], row["ns"])).fetchall()
    for one in found:
        marker = "  <- this one" if one["module"] == row["module"] else ""
        print(f"  {_label(one)}  {one['size']} bytes{marker}")
    print(f"{len(found)} module(s) carry {row['ns']}::{row['name']}")
    return 0


def command_fields(cls: str, offset: int | None) -> int:
    connection = _connect()
    query = "SELECT * FROM fields WHERE cls = ?"
    parameters: list = [cls]
    if offset is not None:
        query += " AND off = ?"
        parameters.append(offset)
    rows = connection.execute(query + " ORDER BY off", parameters).fetchall()
    if not rows:
        # Which of the two absences it is matters: a class nobody has a structure for is a gap
        # in the corpus, while a class that simply has no field at that offset is an answer.
        known = connection.execute(
            "SELECT count(*) FROM fields WHERE cls = ?", (cls,)).fetchone()[0]
        if known and offset is not None:
            print(f"{cls} has {known} field(s), none at +0x{offset:x}")
        else:
            print(f"no class structure named {cls!r}")
        return 1
    for row in rows:
        print(f"  +0x{row['off']:05x}  {row['name']:38} {row['type']:16} {row['note']}")
    print(f"{len(rows)} field(s) in {cls}")
    return 0


def command_readers(offset: int, cls: str | None, limit: int) -> int:
    """The ledger query: every function that touches an offset, named or raw."""
    connection = _connect()
    names = connection.execute(
        "SELECT DISTINCT cls, name FROM fields WHERE off = ?" + (" AND cls = ?" if cls else ""),
        (offset, cls) if cls else (offset,)).fetchall()
    if names:
        print(f"+0x{offset:x} is stated by:")
        for row in names:
            print(f"    {row['cls']}::{row['name']}")

    query = """SELECT a.kind, a.cls, a.field, f.* FROM accesses a
               JOIN functions f ON f.module = a.module AND f.addr = a.func_addr
               WHERE a.off = ?"""
    parameters: list = [offset]
    if cls:
        query += " AND a.cls = ?"
        parameters.append(cls)

    # Each section is fetched and counted separately. One LIMIT over the union starved the half
    # that matters: `ORDER BY a.kind` sorts 'expr' before 'named', so `readers 0x2f8 --limit 5`
    # printed five untyped candidates and "0 typed access(es)" for an offset that has them.
    limit = max(1, limit)
    sections = []
    for label, predicate in (
            ("typed access(es) — the class is known", "a.kind IN ('named','this')"),
            ("untyped access(es) at the same offset — candidates, class unknown",
             "a.kind = 'expr'")):
        total = connection.execute(
            f"SELECT count(*) FROM accesses a WHERE a.off = ?"
            + (" AND a.cls = ?" if cls else "") + f" AND {predicate}", parameters).fetchone()[0]
        rows = connection.execute(
            f"{query} AND {predicate} ORDER BY f.module, f.addr LIMIT ?",
            parameters + [limit]).fetchall()
        sections.append((label, total, rows))

    for label, total, rows in sections:
        print(f"\n{total} {label}:")
        for row in rows:
            detail = (f"   {row['cls']}::{row['field'] or '?'} ({row['kind']})"
                      if row["kind"] != "expr" else "")
            print(f"    {_label(row)}{detail}")
        if total > len(rows):
            print(f"    … {total - len(rows)} more; raise --limit")
    return 0


def command_closure(cls: str, out: Path | None, with_code: bool = True) -> int:
    """One class, whole: what RE work should read instead of re-deriving from the binary."""
    connection = _connect()
    lines: list[str] = []

    def say(text: str = "") -> None:
        lines.append(text)

    fields = connection.execute(
        "SELECT * FROM fields WHERE cls = ? ORDER BY off", (cls,)).fetchall()
    methods = connection.execute(
        "SELECT * FROM functions WHERE ns = ? ORDER BY addr", (cls,)).fetchall()
    if not fields and not methods:
        print(f"nothing in the corpus belongs to {cls!r}")
        return 1

    say(f"# {cls} — subsystem closure")
    say()
    say(f"{len(fields)} fields, {len(methods)} methods. Generated from the corpus; the binary "
        f"was not read.")
    say()
    say("## Fields")
    say()
    for row in fields:
        say(f"  +0x{row['off']:05x}  {row['name']:38} {row['type']:14} {row['note']}")
    say()
    say("## Methods")
    say()
    for row in methods:
        callers = connection.execute(
            "SELECT count(*) FROM edges WHERE module = ? AND callee = ?",
            (row["module"], row["addr"])).fetchone()[0]
        say(f"  {row['addr']}  {row['name']:44} {row['cc']:12} "
            f"{row['size']:6} bytes  callers={callers}")
    say()
    say("## Strings these methods reference")
    say()
    for row in methods:
        referenced = connection.execute(
            """SELECT s.text FROM string_refs r JOIN strings s
               ON s.module = r.module AND s.addr = r.str_addr
               WHERE r.module = ? AND r.func_addr = ?""",
            (row["module"], row["addr"])).fetchall()
        for one in referenced:
            say(f"  {row['name']:40} {one['text'][:90]!r}")
    say()
    if with_code:
        say("## Decompilation")
        say()
        for row in methods:
            say(f"### {row['addr']}  {cls}::{row['name']}")
            say()
            say(row["code"] or "// no decompilation in the corpus")
            say()
    else:
        say("Decompilation omitted; read one method with `corpus code <addr>`.")
        say()

    text = "\n".join(lines)
    if out:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8")
        print(f"{cls}: {len(fields)} fields, {len(methods)} methods -> {out}")
    else:
        print(text)
    return 0


def command_stat() -> int:
    connection = _connect()
    print(f"{_database()}  {_database().stat().st_size // (1 << 20)} MiB")
    for row in connection.execute(
            """SELECT module, count(*) AS functions,
                      sum(CASE WHEN code != '' THEN 1 ELSE 0 END) AS decompiled,
                      sum(CASE WHEN warn != '' THEN 1 ELSE 0 END) AS warned
               FROM functions GROUP BY module ORDER BY module"""):
        print(f"  {row['module']:20} {row['functions']:7} functions, "
              f"{row['decompiled']:7} decompiled, {row['warned']:5} warned")
    for table in ("edges", "vtables", "vcalls", "strings", "string_refs", "globals",
                  "global_refs", "externals", "fields", "accesses"):
        count = connection.execute(f"SELECT count(*) FROM {table}").fetchone()[0]
        print(f"  {table:20} {count}")
    for row in connection.execute(
            "SELECT kind, count(*) AS n FROM edges GROUP BY kind ORDER BY kind"):
        print(f"    edges/{row['kind']:14} {row['n']}")

    # The metric the virtual call graph exists to move: a class method nothing can be shown to
    # call is a method no reader can trace.
    orphans = connection.execute(
        """SELECT count(*) FROM functions f
           WHERE f.ns NOT IN ('', 'Global', '<EXTERNAL>')
             AND NOT EXISTS (SELECT 1 FROM edges e
                             WHERE e.module = f.module AND e.callee = f.addr)""").fetchone()[0]
    methods = connection.execute(
        "SELECT count(*) FROM functions WHERE ns NOT IN ('', 'Global', '<EXTERNAL>')"
    ).fetchone()[0]
    if methods:
        print(f"  class methods with no caller of any kind: {orphans}/{methods} "
              f"({100 * orphans / methods:.1f}%)")
    if _listing_database().is_file():
        size = _listing_database().stat().st_size // (1 << 20)
        listings = sqlite3.connect(_listing_database())
        count = listings.execute("SELECT count(*) FROM listing").fetchone()[0]
        print(f"  {_listing_database().name:20} {count} functions, {size} MiB")
    else:
        print("  listing.sqlite         absent — `corpus asm` has nothing to read")
    return 0


def _offset(text: str) -> int:
    return int(text, 16) if text.lower().startswith("0x") else int(text, 0)


def main() -> int:
    parser = argparse.ArgumentParser(prog="corpus", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    dump_parser = sub.add_parser("dump", help="run the Ghidra pass (slow)")
    dump_parser.add_argument("program", nargs="*", default=[])
    dump_parser.add_argument("--project-dir", type=Path, default=None)
    dump_parser.add_argument("--project-name", default="vtmb")
    dump_parser.add_argument("--limit", type=int, default=0)

    vtables_parser = sub.add_parser("vtables", help="run the vtable pass (cheap)")
    vtables_parser.add_argument("program", nargs="*", default=[])
    vtables_parser.add_argument("--project-dir", type=Path, default=None)
    vtables_parser.add_argument("--project-name", default="vtmb")
    vtables_parser.add_argument("--name", action="store_true",
                                help="name unambiguous slots in the project as well as dumping")
    vtables_parser.add_argument("--create", action="store_true",
                                help="make a function at a slot target that has none — code "
                                     "reached only through a vtable is never a CALL target, so "
                                     "nothing else seeds it")

    pyapi_parser = sub.add_parser("pyapi", help="apply CPython 2.1.2's declared C API (cheap, "
                                               "a WRITE pass — run before a dump)")
    pyapi_parser.add_argument("program", nargs="*", default=["vampire.dll", "engine.dll"])
    pyapi_parser.add_argument("--project-dir", type=Path, default=None)
    pyapi_parser.add_argument("--project-name", default="vtmb")
    pyapi_parser.add_argument("--apply", action="store_true",
                              help="write to the project (default: report what it would do)")

    externals_parser = sub.add_parser("externals", help="run the imported-function pass (cheap)")
    externals_parser.add_argument("program", nargs="*", default=[])
    externals_parser.add_argument("--project-dir", type=Path, default=None)
    externals_parser.add_argument("--project-name", default="vtmb")

    listing_parser = sub.add_parser("listing", help="run the disassembly pass (slow)")
    listing_parser.add_argument("program", nargs="*", default=[])
    listing_parser.add_argument("--project-dir", type=Path, default=None)
    listing_parser.add_argument("--project-name", default="vtmb")
    listing_parser.add_argument("--limit", type=int, default=0)

    build_parser = sub.add_parser("build", help="load the dumps into SQLite")
    build_parser.add_argument("program", nargs="*", default=[])

    sub.add_parser("reindex", help="rebuild the search index and the virtual call graph from "
                                   "rows already loaded — no dump is re-read")

    sub.add_parser("stat", help="what the database holds")

    for name, help_text in (("func", "one function's facts"), ("code", "one function's C"),
                            ("asm", "one function's disassembly"),
                            ("callers", "who calls it"), ("callees", "what it calls"),
                            ("twin", "the same function in another module")):
        one = sub.add_parser(name, help=help_text)
        one.add_argument("reference")

    vtable_parser = sub.add_parser("vtable", help="a class's slots, in order")
    vtable_parser.add_argument("cls")

    slot_parser = sub.add_parser("slot", help="every class that fills a slot")
    slot_parser.add_argument("slot")
    slot_parser.add_argument("--module", default=None)

    globals_parser = sub.add_parser("globals", help="a global and what touches it")
    globals_parser.add_argument("text")
    globals_parser.add_argument("--limit", type=int, default=20)

    outliers_parser = sub.add_parser("outliers", help="oversized bodies and damaged C")
    outliers_parser.add_argument("--min", dest="minimum", type=int, default=4096)

    suggest_parser = sub.add_parser("suggest", help="unnamed functions only one class calls")
    suggest_parser.add_argument("--limit", type=int, default=60)

    grep_parser = sub.add_parser("grep", help="search the decompiled corpus")
    grep_parser.add_argument("pattern")
    grep_parser.add_argument("--module", default=None)
    grep_parser.add_argument("--limit", type=int, default=40)

    str_parser = sub.add_parser("str", help="strings and their referencing functions")
    str_parser.add_argument("text")
    str_parser.add_argument("--limit", type=int, default=40)

    fields_parser = sub.add_parser("fields", help="a class's fields")
    fields_parser.add_argument("cls")
    fields_parser.add_argument("--offset", default=None)

    readers_parser = sub.add_parser("readers", help="every function that touches an offset")
    readers_parser.add_argument("offset")
    readers_parser.add_argument("--class", dest="cls", default=None)
    readers_parser.add_argument("--limit", type=int, default=60)

    closure_parser = sub.add_parser("closure", help="one class, whole")
    closure_parser.add_argument("cls")
    closure_parser.add_argument("--out", type=Path, default=None)
    closure_parser.add_argument("--no-code", action="store_true",
                                help="structure only; leave the decompilation to `code`")

    args = parser.parse_args()
    if args.command == "dump":
        return dump(list(args.program) or list(PROGRAMS), args.project_dir,
                    args.project_name, args.limit)
    if args.command == "vtables":
        return vtables(list(args.program) or list(PROGRAMS), args.project_dir,
                       args.project_name, args.name, args.create)
    if args.command == "pyapi":
        return pyapi(list(args.program), args.project_dir, args.project_name, args.apply)
    if args.command == "externals":
        return externals(list(args.program) or list(PROGRAMS), args.project_dir,
                         args.project_name)
    if args.command == "listing":
        return listing(list(args.program) or list(PROGRAMS), args.project_dir,
                       args.project_name, args.limit)
    if args.command == "build":
        return build(list(args.program) or list(PROGRAMS))
    if args.command == "reindex":
        return reindex()
    if args.command == "stat":
        return command_stat()
    if args.command == "func":
        return command_func(args.reference)
    if args.command == "code":
        return command_code(args.reference)
    if args.command == "asm":
        return command_asm(args.reference)
    if args.command == "twin":
        return command_twin(args.reference)
    if args.command == "vtable":
        return command_vtable(args.cls)
    if args.command == "slot":
        return command_slot(_offset(args.slot), args.module)
    if args.command == "globals":
        return command_globals(args.text, args.limit)
    if args.command == "outliers":
        return command_outliers(args.minimum)
    if args.command == "suggest":
        return command_suggest(args.limit)
    if args.command in ("callers", "callees"):
        return command_hop(args.reference, args.command)
    if args.command == "grep":
        return command_grep(args.pattern, args.module, args.limit)
    if args.command == "str":
        return command_str(args.text, args.limit)
    if args.command == "fields":
        return command_fields(args.cls, _offset(args.offset) if args.offset else None)
    if args.command == "readers":
        return command_readers(_offset(args.offset), args.cls, args.limit)
    return command_closure(args.cls, args.out, not args.no_code)


if __name__ == "__main__":
    raise SystemExit(main())
