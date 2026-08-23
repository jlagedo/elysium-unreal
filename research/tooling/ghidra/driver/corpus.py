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

    names [--apply]          the tracked names overlay: what it states, what it changes
    harvest                  propose overlay rows from the addresses `docs/` already names

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
import bisect
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
    "vphysics.dll",
    "vampire_python21.dll",
    "tier0.dll",
    "vstdlib.dll",
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
CREATE TABLE IF NOT EXISTS global_refs (
    module TEXT, addr TEXT, func_addr TEXT, kind TEXT DEFAULT '');
CREATE TABLE IF NOT EXISTS strings (module TEXT, addr TEXT, text TEXT);
CREATE TABLE IF NOT EXISTS string_refs (module TEXT, str_addr TEXT, func_addr TEXT);
CREATE TABLE IF NOT EXISTS fields (
    module TEXT, cls TEXT, off INTEGER, name TEXT, len INTEGER, type TEXT, note TEXT);
CREATE TABLE IF NOT EXISTS accesses (
    module TEXT, func_addr TEXT, cls TEXT, off INTEGER, field TEXT, kind TEXT);
CREATE TABLE IF NOT EXISTS interfaces (
    name TEXT PRIMARY KEY, module TEXT, factory TEXT, object TEXT, cls TEXT, slots INTEGER);
CREATE TABLE IF NOT EXISTS iface_globals (
    module TEXT, addr TEXT, name TEXT, PRIMARY KEY (module, addr));
CREATE TABLE IF NOT EXISTS xedges (
    module TEXT, caller TEXT, slot INTEGER, iface TEXT, to_module TEXT, to_addr TEXT);
CREATE TABLE IF NOT EXISTS data_refs (
    module TEXT, addr TEXT, func_addr TEXT, writes INTEGER);
CREATE TABLE IF NOT EXISTS externals (
    module TEXT, addr TEXT, lib TEXT, name TEXT, imported TEXT, PRIMARY KEY (module, addr));
CREATE TABLE IF NOT EXISTS meta (
    module TEXT PRIMARY KEY, binary TEXT, sha256 TEXT, dumped_at REAL, project_at REAL);
CREATE TABLE IF NOT EXISTS names (
    module TEXT, addr TEXT, name TEXT, tier TEXT, evidence TEXT,
    PRIMARY KEY (module, addr));"""

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
    "globals_addr": "globals (addr)",
    "externals_name": "externals (name)",
    "data_refs_addr": "data_refs (module, addr, writes)",
    "xedges_caller": "xedges (module, caller)",
    "xedges_to": "xedges (to_module, to_addr)",
    "data_refs_func": "data_refs (module, func_addr)",
    "global_refs_addr": "global_refs (module, addr, kind)",
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
# An unnamed datum, as the decompiler writes it. `DumpCorpus` deliberately skips these: Ghidra
# mints a dynamic label for anything referenced at all, and tens of thousands of `DAT_<addr>`
# rows would bury the labels that mean something (`cvar_<name>`, `datamap_<Class>`). But the
# objects VtMB dispatches through hardest are exactly the unnamed ones -- `*DAT_1070b22c` alone
# carries 1,941 virtual calls -- so they are indexed HERE, from the decompiled C, into their own
# table rather than into `globals`.
DATA_TOKEN = re.compile(r"\b(_{0,2}(?:DAT|UNK|PTR|FLOAT|DOUBLE|BYTE|WORD|DWORD|QWORD|UINT|INT)"
                        r"[A-Za-z0-9_]*?_([0-9a-fA-F]{8}))\b")
# The same token on the left of a single `=`, and not through a dereference. Which function
# ASSIGNS a singleton is what names its type; `*DAT_x = 0` writes through the pointer and says
# nothing about what the pointer is, so the lookbehind keeps those out.
DATA_WRITE = re.compile(r"(?<![*&\w])" + DATA_TOKEN.pattern + r"\s*(?:\[[^\]]*\])?\s*=(?!=)")
# Source publishes a module's services by VERSIONED NAME, and both halves of that handshake are
# stated in the decompiled C.
#
#   provider (engine.dll):  InterfaceReg::InterfaceReg(&reg, FUN_2010ad70, "VEngineServer014")
#   factory  (engine.dll):  undefined4 * FUN_2010ad70(void) { return &DAT_213057f4; }
#   consumer (vampire.dll): DAT_1070b22c = (*param_1)("VEngineServer014", 0)
#
# That chain is what makes a dispatch through a global singleton resolvable: the object's first
# dword is its vftable, the vftable names its class, and the class's slot N is the function
# 1,941 sites in another module are actually calling. Nothing here is inferred -- every link is
# a literal the image carries.
IFACE_REGISTER = re.compile(
    r"InterfaceReg::InterfaceReg\s*\(\s*&?\w*?_?[0-9a-fA-F]{8}\s*,\s*"
    r"(?:thunk_)?FUN_([0-9a-fA-F]{8})\s*,\s*&?s_([A-Za-z_][A-Za-z0-9_]*?)_[0-9a-fA-F]{8}\s*\)")
IFACE_FACTORY = re.compile(r"return\s+\(?[^;]*?&\w*?_?([0-9a-fA-F]{8})\s*;")
# `DAT_x = <anything>("Name", ...)`. Deliberately loose about what stands between the datum and
# the name -- the decompiler writes the factory call four different ways, `(*param_1)`,
# `(*(code *)param_1)`, `(*DAT_21300c7c)`, each under its own cast. Precision comes from the
# NAME instead: a match is only kept when it is an interface some module actually registers.
IFACE_ACQUIRE = re.compile(
    r"(_{0,2}(?:DAT|PTR|UNK)[A-Za-z0-9_]*?_([0-9a-fA-F]{8}))\s*=\s*"
    r"[^;]{0,120}?&?s_([A-Za-z_][A-Za-z0-9_]*?)_[0-9a-fA-F]{8}")

# Below this, an "address" is a small constant the decompiler happened to render as a datum
# (`DAT_00000018` is a read through a null pointer, not a global).
LOWEST_IMAGE = 0x400000

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
                                      ("global_refs", "kind", "TEXT DEFAULT ''"),
                                      ("functions", "warn", "TEXT DEFAULT ''"),
                                      ("functions", "name_src", "TEXT DEFAULT ''"),
                                      ("functions", "name_dump", "TEXT DEFAULT ''")):
        present = {row[1] for row in connection.execute(f"PRAGMA table_info({table})")}
        if not present or column in present:
            continue
        print(f"migrating: {table} gains {column}")
        connection.execute(f"ALTER TABLE {table} ADD COLUMN {column} {definition}")
    connection.commit()


# ---------------------------------------------------------------------------
# The names overlay.
#
# A name argued out rather than read out is knowledge the image does not carry, so it cannot
# live in the Ghidra project: that project is derived from the user's own binary, is gitignored,
# and `dump` rebuilds it from scratch. The overlay is the tracked half -- a text file applied on
# top of whatever the dump carried, the same shape as a generator rebuilding an authored asset
# from its captured text. It holds names and addresses, which is what `docs/vtmb/` already
# tracks; it holds no bytes off the user's install.

# The tier is never merged away, because a guess that prints like a recovered name is how a
# wrong fact enters a document. `binary` is a name the image itself states -- a VProf scope, an
# RTTI method, an export. `doc` is one a `docs/` topic file states beside the address, which is
# this project's own recovered record. `inferred` is argued from call sites or slot position and
# nothing else.
NAME_TIERS = ("binary", "doc", "inferred")

NAMES_HEADER = "\t".join(("module", "addr", "name", "tier", "evidence"))


def _names_file() -> Path:
    return repo_root() / "research" / "tooling" / "ghidra" / "driver" / "names.tsv"


def _read_names(path: Path) -> tuple[list[tuple[str, str, str, str, str]], list[str]]:
    """Parse the overlay. A malformed row is reported and dropped, never silently repaired."""
    rows: list[tuple[str, str, str, str, str]] = []
    complaints: list[str] = []
    if not path.is_file():
        return rows, complaints
    seen: dict[tuple[str, str], int] = {}
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip() or line.lstrip().startswith("#") or line == NAMES_HEADER:
            continue
        parts = line.split("\t")
        if len(parts) != 5:
            complaints.append(f"line {number}: {len(parts)} column(s), expected 5")
            continue
        module, addr, name, tier, evidence = (one.strip() for one in parts)
        addr = addr.lower().removeprefix("0x")
        if module not in PROGRAMS:
            complaints.append(f"line {number}: {module!r} is not a corpus module")
        elif not re.fullmatch(r"[0-9a-f]{6,8}", addr):
            complaints.append(f"line {number}: {addr!r} is not an address")
        elif not re.fullmatch(r"[A-Za-z_][\w]*(::~?[\w]+)?", name):
            complaints.append(f"line {number}: {name!r} is not a symbol")
        elif tier not in NAME_TIERS:
            complaints.append(f"line {number}: tier {tier!r} is not one of {NAME_TIERS}")
        elif not evidence:
            complaints.append(f"line {number}: {module} {addr} states no evidence")
        elif (module, addr) in seen:
            complaints.append(f"line {number}: {module} {addr} restates line {seen[module, addr]}")
        else:
            seen[module, addr] = number
            rows.append((module, addr, name, tier, evidence))
            continue
    return rows, complaints


def _load_names(connection: sqlite3.Connection) -> int:
    rows, complaints = _read_names(_names_file())
    for one in complaints:
        print(f"names overlay: {one}")
    connection.execute("DELETE FROM names")
    connection.executemany("INSERT INTO names VALUES (?, ?, ?, ?, ?)", rows)
    connection.commit()
    return len(rows)


def _is_unnamed(name: str) -> bool:
    return bool(re.match(r"(thunk_)?(FUN|SUB)_[0-9a-fA-F]+$", name or ""))


def _apply_names(connection: sqlite3.Connection, write: bool = True,
                 verbose: bool = True) -> dict[str, int]:
    """Write the overlay onto `functions`, and refuse to overwrite a name the dump recovered.

    A row that disagrees with a recovered name is reported and left alone. One of the two is
    wrong and the overlay cannot tell which, so silently preferring either would launder a
    disagreement into a fact.
    """
    tally = {"applied": 0, "agreed": 0, "conflict": 0, "collision": 0, "absent": 0,
             "reverted": 0}
    # A name deleted from the overlay has to come back off the function, or the file stops being
    # the record of what the corpus states. `name_dump` is what the dump called it.
    for module, addr, dumped in connection.execute(
            "SELECT module, addr, name_dump FROM functions WHERE name_src != '' AND "
            "NOT EXISTS (SELECT 1 FROM names n WHERE n.module = functions.module "
            "AND n.addr = lower(functions.addr))").fetchall():
        # `name_dump` always carries its namespace, so an empty or `Global` one round-trips as
        # well as a named one. A row written before that shape reverts to the neutral namespace,
        # which is what a bare dumped name meant.
        namespace, bare = dumped.rsplit("::", 1) if "::" in dumped else ("", dumped)
        if write:
            connection.execute(
                "UPDATE functions SET name = ?, ns = ?, name_src = '', name_dump = '' "
                "WHERE module = ? AND addr = ?", (bare, namespace, module, addr))
        tally["reverted"] += 1

    # A dry run writes nothing, so the corpus cannot answer for a name two overlay rows both
    # claim. `claimed` is what this pass has handed out, and it is read beside the corpus.
    claimed: dict[tuple[str, str, str], str] = {}
    for module, addr, name, tier, evidence in connection.execute(
            "SELECT module, addr, name, tier, evidence FROM names ORDER BY module, addr"):
        row = connection.execute(
            "SELECT name, ns, name_src, name_dump, thunk FROM functions "
            "WHERE module = ? AND lower(addr) = ?", (module, addr)).fetchone()
        if row is None:
            tally["absent"] += 1
            if verbose:
                print(f"names overlay: {module} {addr} is not a function entry point "
                      f"({evidence})")
            continue
        namespace, bare = name.rsplit("::", 1) if "::" in name else (row["ns"], name)
        current = (f"{row['ns']}::{row['name']}" if row["ns"] not in ("", "Global")
                   else row["name"])
        if row["name"] == bare and row["ns"] == namespace:
            tally["agreed"] += 1
            continue
        if not _is_unnamed(row["name"]) and not row["name_src"]:
            tally["conflict"] += 1
            if verbose:
                print(f"names overlay: {module} {addr} is {current} in the dump and {name} in "
                      f"the overlay -- left as the dump has it ({evidence})")
            continue
        # A name already carried by a DIFFERENT body in the same module is refused. Three
        # functions answering `CVDmg_t::Apply` is a worse answer than one FUN_ -- and this is
        # the shape the same-address conflict test cannot see, because the disagreement is
        # between two addresses rather than about one. A thunk sharing its target's name is not
        # a collision; it is what a thunk is.
        taken = [one["addr"] for one in connection.execute(
            "SELECT addr FROM functions WHERE module = ? AND ns = ? AND name = ? "
            "AND lower(addr) != ? AND thunk = 0", (module, namespace, bare, addr)).fetchall()]
        holder = claimed.get((module, namespace, bare))
        if holder is not None and holder != addr and holder not in taken:
            taken.append(holder)
        if taken:
            tally["collision"] += 1
            if verbose:
                print(f"names overlay: {module} {addr} would be a second {name}, which "
                      f"{', '.join(taken[:3])} already carries "
                      f"-- not applied ({evidence})")
            continue
        dumped = row["name_dump"] or f"{row['ns']}::{row['name']}"
        if write:
            connection.execute(
                "UPDATE functions SET name = ?, ns = ?, name_src = ?, name_dump = ? "
                "WHERE module = ? AND lower(addr) = ?",
                (bare, namespace, tier, dumped, module, addr))
        if not row["thunk"]:
            claimed[module, namespace, bare] = addr
        tally["applied"] += 1
    if write:
        connection.commit()

    # The per-row guard only sees rows it is applying. A duplicate created before the guard
    # existed, or by a dump that later recovered the same name elsewhere, would otherwise stay
    # invisible -- so the whole overlay is checked against the corpus each time.
    for row in connection.execute(
            """SELECT f.module, f.ns, f.name, count(*) AS holders FROM functions f
               WHERE f.thunk = 0 AND EXISTS (SELECT 1 FROM functions o WHERE o.module = f.module
                     AND o.ns = f.ns AND o.name = f.name AND o.name_src != '')
               GROUP BY f.module, f.ns, f.name HAVING holders > 1"""):
        tally["collision"] += 1
        if verbose:
            owner = f"{row['ns']}::{row['name']}" if row["ns"] not in ("", "Global")                 else row["name"]
            print(f"names overlay: {row['module']} has {row['holders']} bodies named {owner}")
    return tally


def _apply_overlay(connection: sqlite3.Connection) -> None:
    """Re-apply the overlay after a load, so a dump never silently drops a recovered name."""
    loaded = _load_names(connection)
    tally = _apply_names(connection)
    if loaded or tally["reverted"]:
        print(f"names overlay: {loaded} row(s), {tally['applied']} applied, "
              f"{tally['agreed']} already agree, {tally['conflict']} conflict with the dump, "
              f"{tally['collision']} collide with a name already in use, "
              f"{tally['absent']} name no function entry point, {tally['reverted']} reverted")


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


def dump(programs: list[str], project_dir: Path | None, project_name: str, limit: int,
         globals_only: bool = False) -> int:
    """The Ghidra pass, into the sidecars `build` loads.

    `globals_only` refreshes the named-datum sidecar alone and leaves the others untouched. The
    decompilation is what makes a dump an overnight run, and the globals pass does not need it:
    when the rule for what counts as a named datum changes, this is the whole cost of picking the
    change up, and the following `build` reads the other sidecars off disk exactly as they stand.
    """
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
        if globals_only:
            args.append("only=globals")
        if limit:
            args.append(f"limit={limit}")
        print(f"[{program}] dump" + (" (globals only)" if globals_only else ""))
        _run(["-ProjDir", str(project), "-ProjName", project_name, "-NoFid",
              "-Program", program, "-Script", "DumpCorpus", "-ScriptArgs", " ".join(args)], log)
        for kind in (("globals",) if globals_only
                     else ("functions", "strings", "globals", "fields")):
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
                  "vtables", "vcalls", "globals", "global_refs", "data_refs", "externals"):
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
            # Named columns, not positional: the overlay's `name_src`/`name_dump` are set after
            # a load rather than by it, and a bare VALUES list breaks the moment the table
            # gains one. `INSERT OR REPLACE` clears them, which is correct -- a fresh dump is
            # the new ground truth, and `_apply_overlay` re-applies the overlay on top.
            connection.execute(
                "INSERT OR REPLACE INTO functions "
                "(module, addr, name, ns, size, cc, thunk, code, warn) "
                "VALUES (?,?,?,?,?,?,?,?,?)",
                (module, record["a"], record["n"], record.get("ns") or "", record.get("sz") or 0,
                 record.get("cc") or "", 1 if record.get("thunk") else 0, code,
                 "; ".join(record.get("w") or [])))
            rows += 1
            for callee in record.get("callees") or []:
                connection.execute("INSERT INTO edges VALUES (?,?,?,'direct')",
                                   (module, record["a"], callee))
                edges += 1
            accesses += _derive_accesses(connection, module, record, code)
            _derive_data_refs(connection, module, record, code)
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
                    # `<func addr>:<kind>`; a dump from before the kinds existed has no colon.
                    func_addr, _, kind = reference.partition(":")
                    connection.execute("INSERT INTO global_refs VALUES (?,?,?,?)",
                                       (module, record["a"], func_addr, kind))

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


def _derive_data_refs(connection: sqlite3.Connection, module: str, record: dict,
                      code: str) -> int:
    """Which functions touch each unnamed datum, and which of them write it."""
    if not code:
        return 0
    writes = {addr.lower() for _, addr in DATA_WRITE.findall(code)}
    rows = []
    for _, addr in DATA_TOKEN.findall(code):
        addr = addr.lower()
        if int(addr, 16) < LOWEST_IMAGE:
            continue
        rows.append((module, addr, record["a"], 1 if addr in writes else 0))
    rows = list(dict.fromkeys(rows))
    connection.executemany("INSERT INTO data_refs VALUES (?,?,?,?)", rows)
    return len(rows)


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


VFTABLE_ASSIGN = re.compile(r"&vftable_([A-Za-z_][A-Za-z0-9_]*)")


def _object_class(connection: sqlite3.Connection, bodies: dict, module: str,
                  obj: str) -> tuple[str, int]:
    """The class of a singleton, read from whatever assigns its vftable.

    NOT read from the image. A global C++ object's vftable pointer is written by its constructor
    at static-init, so the dword in the file is zero or whatever the linker left -- the same trap
    `docs/vtmb/script_api.md` records for datamaps, where an image read reports `INPUTS (0)` for a
    class carrying 25. The constructor states it in plain text instead:

        DAT_213057f4 = &vftable_CVEngineServer;

    Construction writes each base's vftable before the most-derived one, so more than one name
    can appear. `staticinit_*` is Ghidra's label for the global's own initializer and is
    preferred; otherwise the widest table wins, and a tie is left to the first by name so the
    answer does not depend on row order.
    """
    candidates: list[tuple[bool, str]] = []
    for row in connection.execute(
            "SELECT func_addr FROM data_refs WHERE module = ? AND addr = ? AND writes = 1",
            (module, obj)):
        code = bodies.get((module, row["func_addr"]))
        if not code:
            continue
        owner = connection.execute("SELECT name FROM functions WHERE module = ? AND addr = ?",
                                   (module, row["func_addr"])).fetchone()
        initializer = bool(owner and owner["name"].startswith("staticinit_"))
        for line in code.splitlines():
            if not re.search(r"(?<![*&\w])\w*_%s\b\s*=(?!=)" % obj, line):
                continue
            for name in VFTABLE_ASSIGN.findall(line):
                candidates.append((initializer, name.split("_at")[0]))
    if not candidates:
        return "", 0
    widths = {}
    for _, name in candidates:
        found = connection.execute(
            "SELECT count(*) n FROM vtables WHERE module = ? AND cls = ? AND sub = 0",
            (module, name)).fetchone()
        widths[name] = found["n"] if found else 0
    best = sorted(candidates, key=lambda one: (not one[0], -widths[one[1]], one[1]))[0][1]
    return best, widths[best]


def _resolve_interfaces(connection: sqlite3.Connection) -> tuple[int, int, int]:
    """The cross-module half of the call graph: who provides each named interface, which global
    in each consuming module holds it, and what a dispatch through that global actually reaches.
    """
    connection.execute("DELETE FROM interfaces")
    connection.execute("DELETE FROM iface_globals")
    connection.execute("DELETE FROM xedges")

    # --- provider side ---------------------------------------------------------------------
    bodies = {(row["module"], row["addr"]): row["code"] for row in connection.execute(
        "SELECT module, addr, code FROM functions WHERE code != ''")}
    provided = 0
    for (module, _), code in list(bodies.items()):
        if "InterfaceReg::InterfaceReg" not in code:
            continue
        for factory, name in IFACE_REGISTER.findall(code):
            factory = factory.lower()
            body = bodies.get((module, factory))
            if not body:
                continue
            match = IFACE_FACTORY.search(body)
            if not match:
                continue
            obj = match.group(1).lower()
            cls, slots = _object_class(connection, bodies, module, obj)
            connection.execute(
                "INSERT OR REPLACE INTO interfaces VALUES (?,?,?,?,?,?)",
                (name, module, factory, obj, cls, slots))
            provided += 1

    # --- consumer side ---------------------------------------------------------------------
    known = {row[0] for row in connection.execute("SELECT name FROM interfaces")}
    held = 0
    for (module, _), code in bodies.items():
        if "= (" not in code:
            continue
        for _, datum, name in IFACE_ACQUIRE.findall(code):
            if name not in known:
                continue
            connection.execute("INSERT OR REPLACE INTO iface_globals VALUES (?,?,?)",
                               (module, datum.lower(), name))
            held += 1

    # --- the edges -------------------------------------------------------------------------
    written = 0
    for row in connection.execute(
            """SELECT g.module, g.addr, g.name, i.module AS provider, i.cls
               FROM iface_globals g JOIN interfaces i ON i.name = g.name
               WHERE i.cls != ''""").fetchall():
        sites = connection.execute(
            "SELECT DISTINCT caller, slot FROM vcalls WHERE module = ? AND cls = '' "
            "AND recv LIKE ?", (row["module"], f"%{row['addr']}%")).fetchall()
        for site in sites:
            target = connection.execute(
                "SELECT func FROM vtables WHERE module = ? AND cls = ? AND slot = ? AND sub = 0",
                (row["provider"], row["cls"], site["slot"])).fetchone()
            if not target:
                continue
            connection.execute("INSERT INTO xedges VALUES (?,?,?,?,?,?)",
                               (row["module"], site["caller"], site["slot"], row["name"],
                                row["provider"], target["func"]))
            written += 1
    return provided, held, written


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


def _report_interfaces(connection: sqlite3.Connection) -> None:
    """Resolve the named-interface graph and say what came of it."""
    provided, held, crossed = _resolve_interfaces(connection)
    connection.commit()
    distinct = connection.execute("SELECT count(*) FROM interfaces").fetchone()[0]
    typed = connection.execute("SELECT count(*) FROM interfaces WHERE cls != ''").fetchone()[0]
    print(f"named interfaces: {provided} registration(s) naming {distinct} interface(s), "
          f"{typed} of them resolved to a class; held by {held} global(s) in the consuming "
          f"modules, {crossed} cross-module dispatch edge(s)")


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

    _report_interfaces(connection)
    _apply_overlay(connection)
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
    _report_interfaces(connection)
    _apply_overlay(connection)
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
        if row["name_src"]:
            evidence = connection.execute(
                "SELECT evidence FROM names WHERE module = ? AND addr = lower(?)",
                (row["module"], row["addr"])).fetchone()
            print(f"    name is [{row['name_src']}], not stated by the image"
                  f"{': ' + evidence[0] if evidence else ''}"
                  f"  (the dump calls it {row['name_dump']})")
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
        # A dispatch through a named interface leaves the module entirely, so it is neither a
        # local edge nor an unresolved site -- it is an answer, in another binary.
        crossing = connection.execute(
            """SELECT DISTINCT x.slot, x.iface, x.to_module, x.to_addr, f.ns, f.name
               FROM xedges x LEFT JOIN functions f
                 ON f.module = x.to_module AND f.addr = x.to_addr
               WHERE x.module = ? AND x.caller = ? ORDER BY x.iface, x.slot""",
            (row["module"], row["addr"])).fetchall()
        if crossing:
            print(f"\n{len(crossing)} call(s) into another module through a named interface:")
            for one in crossing:
                owner = f"{one['ns']}::" if one["ns"] and one["ns"] != "Global" else ""
                print(f"    {one['iface']:24} slot {one['slot']:4}  {one['to_module']:14} "
                      f"{one['to_addr']}  {owner}{one['name'] or '<not a function>'}")

        unresolved = connection.execute(
            """SELECT DISTINCT slot, recv FROM vcalls WHERE module = ? AND caller = ? AND cls = ''
               AND NOT EXISTS (SELECT 1 FROM iface_globals g
                               WHERE g.module = vcalls.module AND vcalls.recv LIKE '%' || g.addr
                                     || '%')
               ORDER BY slot""", (row["module"], row["addr"])).fetchall()
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

    crossing = connection.execute(
        """SELECT f.*, x.iface, x.slot, x.module AS from_module FROM xedges x JOIN functions f
           ON f.module = x.module AND f.addr = x.caller
           WHERE x.to_module = ? AND x.to_addr = ? ORDER BY x.module, f.addr""",
        (row["module"], row["addr"])).fetchall()
    if crossing:
        print(f"\n{len(crossing)} CROSS-MODULE caller(s) — another binary reaches this through a "
              f"named interface, so no local reference exists:")
        for one in crossing[:60]:
            print(f"  {_label(one)}  via {one['iface']} slot {one['slot']}")
        if len(crossing) > 60:
            print(f"  … {len(crossing) - 60} more")

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


def _datum_address(text: str) -> str | None:
    """A datum's address as the corpus stores it, or None when the text is not one.

    A question about a global arrives as whatever the listing showed -- `DAT_1070b22c`,
    `0x1070b22c`, a bare `1070b22c` -- and all three name the same row.
    """
    candidate = text.lower().removeprefix("0x")
    for prefix in ("dat_", "_dat_", "ptr_", "unk_"):
        if candidate.startswith(prefix):
            candidate = candidate[len(prefix):]
    if not re.fullmatch(r"[0-9a-f]{6,8}", candidate):
        return None
    return candidate.rjust(8, "0")


def _unnamed_datum(connection: sqlite3.Connection, text: str) -> int:
    """A datum by bare address, from the references derived out of the decompiled C.

    The objects VtMB dispatches through hardest carry no label at all -- `DAT_1070b22c` alone
    takes 1,941 virtual calls -- so `globals` has no row for them and answering "no global name
    matches" would be reporting the index's blind spot as a fact about the game. Writers are
    listed first and separately: what a singleton IS is stated by whatever assigns it.
    """
    candidate = _datum_address(text)
    if candidate is None:
        return 1
    rows = connection.execute(
        "SELECT module, writes, func_addr FROM data_refs WHERE addr = ? ORDER BY module, writes "
        "DESC, func_addr", (candidate,)).fetchall()
    if not rows:
        return 1
    for module in dict.fromkeys(row["module"] for row in rows):
        here = [row for row in rows if row["module"] == module]
        writers = [row for row in here if row["writes"]]
        dispatch = connection.execute(
            "SELECT count(*) n, count(DISTINCT slot) s FROM vcalls WHERE module = ? AND "
            "recv LIKE ?", (module, f"%{candidate}%")).fetchone()
        print(f"{module:18} {candidate}  unnamed datum — {len(here)} referrer(s), "
              f"{len(writers)} of them write it"
              + (f"; {dispatch['n']} virtual call(s) dispatch through it over "
                 f"{dispatch['s']} distinct slot(s)" if dispatch["n"] else ""))
        iface = connection.execute(
            """SELECT g.name, i.module AS provider, i.cls, i.slots FROM iface_globals g
               LEFT JOIN interfaces i ON i.name = g.name
               WHERE g.module = ? AND g.addr = ?""", (module, candidate)).fetchone()
        if iface:
            print(f"    holds the named interface {iface['name']!r}"
                  + (f", provided by {iface['provider']} as {iface['cls']} "
                     f"({iface['slots']} slots)" if iface["cls"] else
                     " — no provider in the corpus, so its dispatches cannot be resolved"))
        for label, group in (("writes it", writers),
                             ("reads it", [row for row in here if not row["writes"]])):
            if not group:
                continue
            print(f"    {len(group)} {label}:")
            for row in group[:20]:
                owner = connection.execute(
                    "SELECT * FROM functions WHERE module = ? AND addr = ?",
                    (module, row["func_addr"])).fetchone()
                print("      " + (_label(owner) if owner else f"{module} {row['func_addr']}"))
            if len(group) > 20:
                print(f"      … {len(group) - 20} more")
    return 0


def command_globals(text: str, limit: int) -> int:
    """A global datum and every function that reaches it."""
    connection = _connect()
    # By name OR by bare address. A question about a global starts from a decompiled listing,
    # which states the address and only sometimes a name, so an address that IS in `globals`
    # answering "no global matches" reports the query's blind spot as a fact about the module.
    rows = connection.execute(
        "SELECT * FROM globals WHERE name LIKE ? ESCAPE '\\' OR addr = ? "
        "ORDER BY module, addr LIMIT ?",
        (_contains(text), _datum_address(text) or "", max(1, limit))).fetchall()
    if not rows:
        if _unnamed_datum(connection, text) == 0:
            return 0
        print(f"no global name matches {text!r}, and no unnamed datum at that address either")
        return 1
    for row in rows:
        referencing = connection.execute(
            """SELECT f.*, r.kind FROM global_refs r JOIN functions f
               ON f.module = r.module AND f.addr = r.func_addr
               WHERE r.module = ? AND r.addr = ? ORDER BY f.addr""",
            (row["module"], row["addr"])).fetchall()
        # A label and a data item are independent: a datum can be named and carry no type, which
        # is the ordinary state of a global C++ object sitting in the BSS tail.
        described = (f"{row['type']} [{row['size']} bytes]" if row["type"] else "untyped")
        print(f"{row['module']:18} {row['addr']:10} {row['name']}"
              f"  {described}  {len(referencing)} referrer(s)")
        # A __thiscall constructor writes the object through ECX inside the callee, so the
        # registration site holds the only statement of what the datum IS and would otherwise
        # be filed with the readers.
        for label, wanted in (("construct it, passing it as `this`", ("t",)),
                              ("write it", ("w",)),
                              ("read it", ("r",)),
                              ("take its address", ("a",))):
            group = [one for one in referencing if one["kind"] in wanted]
            if not group:
                continue
            print(f"    {len(group)} {label}:")
            for one in group[:40]:
                print("      " + _label(one))
            if len(group) > 40:
                print(f"      … {len(group) - 40} more")
        # A dump from before the kinds existed carries none, and every row lands here.
        unclassified = [one for one in referencing if not one["kind"]]
        for one in unclassified[:40]:
            print("    " + _label(one))
        if len(unclassified) > 40:
            print(f"    … {len(unclassified) - 40} more")
    return 0


def command_iface(pattern: str | None) -> int:
    """Every named interface: who provides it, as what class, and who dispatches through it.

    Source publishes a module's services by versioned name, and both ends of that handshake are
    literals in the image — `InterfaceReg::InterfaceReg(&reg, factory, "VEngineServer014")` on
    the provider, `DAT_x = (*factory)("VEngineServer014", 0)` on the consumer. Joining them is
    what makes a call that leaves the binary an edge rather than an unresolved dispatch.
    """
    connection = _connect()
    query = "SELECT * FROM interfaces"
    parameters: list = []
    if pattern:
        query += " WHERE name LIKE ? ESCAPE '\\'"
        parameters.append(_contains(pattern))
    rows = connection.execute(query + " ORDER BY module, name", parameters).fetchall()
    if not rows:
        print(f"no named interface matches {pattern!r}" if pattern
              else "no interface registrations in the corpus")
        return 1
    for row in rows:
        holders = connection.execute(
            "SELECT module, addr FROM iface_globals WHERE name = ? ORDER BY module",
            (row["name"],)).fetchall()
        edges = connection.execute(
            "SELECT count(*) n, count(DISTINCT slot) s FROM xedges WHERE iface = ?",
            (row["name"],)).fetchone()
        print(f"{row['name']:28} {row['module']:16} "
              + (f"{row['cls']} ({row['slots']} slots)" if row["cls"]
                 else "<class not recovered — no constructor assigns its vftable>"))
        if holders:
            print("    held by: " + ", ".join(f"{one['module']} {one['addr']}"
                                              for one in holders))
        if edges["n"]:
            print(f"    {edges['n']} dispatch(es) over {edges['s']} slot(s) resolve through it")
    print(f"\n{len(rows)} interface(s)")
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


def command_names(apply: bool) -> int:
    """The overlay: what it states, and what applying it would change."""
    connection = _connect()
    path = _names_file()
    if not path.is_file():
        print(f"no overlay at {path}")
        print("`corpus harvest` proposes rows from the addresses docs/ already names")
        return 1
    loaded = _load_names(connection)
    tiers = connection.execute(
        "SELECT tier, count(*) FROM names GROUP BY tier ORDER BY 2 DESC").fetchall()
    print(f"{loaded} name(s) in {path.relative_to(repo_root()).as_posix()}: "
          + ", ".join(f"{count} {tier}" for tier, count in tiers))
    tally = _apply_names(connection, write=apply)
    print(f"{'applied' if apply else 'would apply'} {tally['applied']}, "
          f"{tally['agreed']} already agree, {tally['conflict']} conflict with the dump, "
          f"{tally['collision']} collide with a name already in use, "
          f"{tally['absent']} name no function entry point, {tally['reverted']} reverted")
    if not apply:
        print("nothing was written; pass --apply")
    return 0


# A `docs/` topic file states a name beside an address in several shapes -- ``Name`` then
# ``0x…``, the reverse, a table row carrying both -- and states plenty of addresses with no name
# at all. Rather than trust one shape, take every backticked symbol within a window of the
# address and let agreement across the documentation set do the ranking.
HARVEST_WINDOW = 120

_ADDR = re.compile(r"0x([0-9A-Fa-f]{8})\b")
# In prose a recovered name is backticked, and requiring the backticks is most of the precision.
# In C++ and Python it is written bare as often as not -- "vampire.dll 0x10167470 -> FindEntityFOV
# 0x10341c30" -- so those trees get the looser pattern and lean on `--max-distance` instead.
_SYMBOL = re.compile(r"`([A-Za-z_][\w]*(?:::~?[\w]+)?)`")
_SYMBOL_CODE = re.compile(r"`?\b([A-Za-z_][\w]*(?:::~?[\w]+)?)\b`?")

# Where a VtMB address gets a name written beside it. `docs/` is the recovered record; the other
# three cite the same addresses in comments beside the code that reproduces them.
HARVEST_ROOTS = (("docs", ("*.md",), False),
                 ("research", ("*.md", "*.py", "*.java"), True),
                 ("Source", ("*.cpp", "*.h"), True),
                 ("pipeline", ("*.py",), True))


def _plausible(name: str) -> bool:
    """A symbol a function could be called, as against a field, a flag or an English word."""
    if re.match(r"(thunk_)?(FUN|SUB|DAT|LAB|UNK)_", name):
        return False
    # This repo's own types stand next to VtMB addresses constantly -- the comment above a
    # reproduction cites the original it reproduces. They are never the original's name.
    if "Elysium" in name:
        return False
    if "::" in name:
        return True
    # A bare Source class name is the commonest thing standing next to an address, and it is
    # never the function's name: `CBasePlayer`, `CUserCmd`, `CAI_BaseNPC`.
    if re.fullmatch(r"[CI][A-Z]\w*", name):
        return False
    # CamelCase with an internal capital: `ItemPostFrame`, not `cycle`, `reach` or `curtime`.
    return bool(re.fullmatch(r"[A-Z][A-Za-z0-9]*", name)) and bool(re.search(r"[a-z][A-Z]", name))


def command_harvest(out: Path | None, limit: int, max_distance: int) -> int:
    """Propose overlay rows from the addresses `docs/` already names.

    The output is a candidate file for review, never the overlay itself. Proximity is evidence
    of nothing on its own -- the docs pair an address with the function above it as readily as
    with its own -- so a row here is a question for a reader, not a name.
    """
    connection = _connect()
    files: list[tuple[Path, bool]] = []
    for name, patterns, is_code in HARVEST_ROOTS:
        root = repo_root() / name
        if not root.is_dir():
            print(f"harvest: no {name}/ in this checkout - skipped")
            continue
        for pattern in patterns:
            files.extend((path, is_code) for path in root.rglob(pattern))
    if not files:
        print("harvest: nothing to read")
        return 1

    # A pairing at distance 0-6 is a template the docs actually use -- ``Name`` (``0x…``) and
    # its reverse. Past that it is proximity, which is evidence of nothing on its own, so
    # `--max-distance` is how a caller asks for the reviewable half.
    #
    # Each name is offered to its NEAREST address and to no other. A doc listing two pairs in a
    # row -- "interpolation `0x100C4110`, `AddGlobalFlexController` `0x100C4880`" -- otherwise
    # hands the second name to the first address as well, which reads like corroboration and is
    # simply the sentence's punctuation. Ties go to the name written BEFORE its address, which
    # is the template the documentation set actually uses.
    # addr -> name -> {source doc: nearest distance}
    seen: dict[str, dict[str, dict[str, int]]] = {}
    for path, is_code in sorted(files):
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as problem:
            print(f"harvest: {path} unreadable: {problem}")
            continue
        where = path.relative_to(repo_root()).as_posix()
        addresses = [(one.start(), one.end(), one.group(1).lower())
                     for one in _ADDR.finditer(text)]
        if not addresses:
            continue
        starts = [one[0] for one in addresses]
        for found in (_SYMBOL_CODE if is_code else _SYMBOL).finditer(text):
            name = found.group(1)
            if not _plausible(name):
                continue
            pivot = bisect.bisect_left(starts, found.start())
            best: tuple[tuple[int, int], str] | None = None
            for index in range(max(0, pivot - 1), min(len(addresses), pivot + 2)):
                start, end, addr = addresses[index]
                before = found.end() <= start
                gap = (start - found.end()) if before else (found.start() - end)
                if gap < 0 or gap > max_distance:
                    continue
                rank = (gap, 0 if before else 1)
                if best is None or rank < best[0]:
                    best = (rank, addr)
            if best is None:
                continue
            (gap, _), addr = best
            sources = seen.setdefault(addr, {}).setdefault(name, {})
            sources[where] = min(gap, sources.get(where, gap))

    rows: list[tuple[str, str, str, str, str, int, list[str]]] = []
    conflicts: list[tuple[str, str, str, str]] = []
    for addr, candidates in seen.items():
        entry = connection.execute(
            "SELECT module, addr, name, ns FROM functions WHERE lower(addr) = ?",
            (addr,)).fetchall()
        if len(entry) != 1:
            # Ambiguous across modules (vampire.dll and client.dll share an imagebase) or not an
            # entry point at all. Either way a reader has to say which, so it is not proposed.
            continue
        row = entry[0]
        ranked = sorted(candidates.items(),
                        key=lambda one: (-len(one[1]), min(one[1].values()), one[0]))
        name, sources = ranked[0]
        current = (f"{row['ns']}::{row['name']}" if row["ns"] not in ("", "Global")
                   else row["name"])
        if not _is_unnamed(row["name"]):
            # The docs name a method bare as often as they qualify it, and a bare name matching
            # the dump's method half is the same name, not a second opinion about it.
            agrees = current == name or ("::" not in name and row["name"] == name)
            if not agrees:
                conflicts.append((row["module"], addr, current, name))
            continue
        rows.append((row["module"], addr, name, "doc",
                     "; ".join(sorted(sources)), len(sources),
                     [one for one, _ in ranked[1:4]]))

    rows.sort(key=lambda one: (-one[5], one[0], one[1]))
    target = out or (research_root() / "ghidra" / "names" / "harvest.tsv")
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("# Candidates, not names. Review each row, then move it into\n"
                     f"# {_names_file().relative_to(repo_root()).as_posix()}.\n"
                     "# `alt:` lists the runners-up the same window offered.\n")
        stream.write(NAMES_HEADER + "\n")
        for module, addr, name, tier, evidence, agreeing, alternates in rows[:limit]:
            if alternates:
                stream.write(f"# alt: {', '.join(alternates)}\n")
            stream.write("\t".join((module, addr, name, tier, evidence)) + "\n")

    print(f"{len(seen)} address(es) across {len(files)} file(s) carry a plausible name nearby")
    print(f"{len(rows)} of them are an unnamed function in exactly one module -> {target}")
    if conflicts:
        print(f"{len(conflicts)} already carry a different name in the dump:")
        for module, addr, current, proposed in conflicts[:limit]:
            print(f"  {module:18} {addr}  dump {current}  docs {proposed}")
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
                  "global_refs", "externals", "data_refs", "fields", "accesses"):
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
    dump_parser.add_argument("--globals-only", action="store_true",
                             help="refresh only the named-datum sidecar, skipping the "
                                  "decompilation; the other sidecars stay as they are and the "
                                  "following `build` reads them off disk")

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

    iface_parser = sub.add_parser("iface", help="named interfaces: provider, class, consumers")
    iface_parser.add_argument("pattern", nargs="?", default=None)

    outliers_parser = sub.add_parser("outliers", help="oversized bodies and damaged C")
    outliers_parser.add_argument("--min", dest="minimum", type=int, default=4096)

    suggest_parser = sub.add_parser("suggest", help="unnamed functions only one class calls")
    suggest_parser.add_argument("--limit", type=int, default=60)

    names_parser = sub.add_parser("names", help="the tracked names overlay and what it changes")
    names_parser.add_argument("--apply", action="store_true",
                              help="write the overlay onto the corpus (default: report only)")

    harvest_parser = sub.add_parser(
        "harvest", help="propose overlay rows from the addresses docs/ already names")
    harvest_parser.add_argument("--out", type=Path, default=None)
    harvest_parser.add_argument("--limit", type=int, default=2000)
    harvest_parser.add_argument("--max-distance", type=int, default=HARVEST_WINDOW,
                                help="how far from the address a name may stand; 6 keeps only "
                                     "the adjacent pairings the docs use as a template")

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
                    args.project_name, args.limit, args.globals_only)
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
    if args.command == "iface":
        return command_iface(args.pattern)
    if args.command == "outliers":
        return command_outliers(args.minimum)
    if args.command == "suggest":
        return command_suggest(args.limit)
    if args.command == "names":
        return command_names(args.apply)
    if args.command == "harvest":
        return command_harvest(args.out, args.limit, args.max_distance)
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
