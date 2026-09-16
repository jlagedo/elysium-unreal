# -*- coding: utf-8 -*-
"""Query surface of the AI helper classes, recovered as an address-backed interface.

The NPC-side bodies that query hints, places, patrols, squads, the attack coordinator,
standoff and the sound list are ``CAI_BaseNPC::`` methods or unnamed ``FUN_`` bodies in the
ledger, so a class-prefix join finds nothing.  This probe therefore carries the recovered helper
operation address, query name and answer as one reviewed manifest, and joins each operation to
its callers in ``graph.tsv``, its ledger name in ``functions.md`` and its oracle sections in
``index.md``. Missing bodies, callers and answers are validation errors rather than blank cells.

Read-only unless ``--json`` is supplied.  Generated reports belong below
``$ELYSIUM_WORK_ROOT/research`` and must not be committed.

Usage::

    uv run elysium research ai_infra_surface
    uv run elysium research ai_infra_surface --json <external-path>
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import re
from pathlib import Path

from elysium_pipeline.paths import repo_root

#: The recovered world-infrastructure interface.  These are the actual helper operations the
#: kernel calls, including unnamed bodies: a name-prefix join cannot recover them.  Each row is
#: ``(object, address, recovered query name, retail answer)``.  The prose is deliberately held
#: here rather than borrowed from the porting-verdict overlay: story 1 requires an answer even
#: when a body has not yet received a port verdict.
QUERY_SPECS = (
    ("hint", 0x102D1180, "stand position",
     "returns the point where the hint says this NPC should stand"),
    ("hint", 0x102D1420, "release",
     "clears the owner and delays the hint's next use by the supplied seconds"),
    ("hint", 0x102D14C0, "is unusable",
     "true while disabled, reuse-delayed, or owned by a live entity"),
    ("hint", 0x102D1AF0, "find near",
     "returns the nearest admissible hint matching type, flags, group and radius"),
    ("hint", 0x102D24B0, "find near target",
     "returns the nearest admissible hint of the requested group around a target point"),
    ("hint", 0x102D2980, "find tactical",
     "returns the tactical hint matching the requested search flags and range"),

    ("place", 0x102DAD60, "eligible",
     "answers enabled, group, capacity, type and visitor eligibility for one NPC"),
    ("place", 0x102DA7C0, "claim",
     "adds the NPC to the place's visitor set and records the active marker"),
    ("place", 0x102DA600, "release",
     "removes the NPC's claim and visitor record and performs the leave bookkeeping"),
    ("place", 0x102DAAC0, "disable visitor walk",
     "walks current visitors: DISAPPEAR visitors are removed and all others TaskFail(0x23)"),

    ("patrol", 0x10307AA0, "clear path",
     "clears the patrol object's node list and resets its path state"),
    ("patrol", 0x10307B40, "set type",
     "stores the authored patrol traversal type"),
    ("patrol", 0x10307BF0, "append node",
     "appends one authored node id to the patrol path"),
    ("patrol", 0x10307B60, "reset point",
     "sets the current index to the first point for the active traversal type"),
    ("patrol", 0x10307B80, "next point",
     "advances by type; returns true when repeats are exhausted, otherwise wraps or reverses"),
    ("patrol", 0x10307C20, "first index",
     "returns min(node-count minus one, the traversal type's first-index cap)"),
    ("patrol", 0x10307D30, "allocate",
     "returns a pooled patrol-path object, growing the pool when it is dry"),
    ("patrol", 0x10307DB0, "free",
     "returns a patrol-path object to the pool"),
    ("patrol", 0x1029F730, "interest record",
     "returns the current node's cached interesting-place record only after its chance roll wins"),

    ("squad", 0x10315800, "find or create",
     "finds the named squad or creates it, then admits the NPC with retail's 16-member cap"),
    ("squad", 0x103158F0, "remove member",
     "removes the NPC and compacts the member array, including retail's overwrite defect"),
    ("squad", 0x103160A0, "member count",
     "returns the squad's current member count"),
    ("squad", 0x103160C0, "member",
     "returns the member at an index, or null for every index while member zero is disconnected"),
    ("squad", 0x103161A0, "new enemy",
     "publishes a newly acquired enemy to squad members and their shared enemy memory"),
    ("squad", 0x10316660, "set focus",
     "stores the squad focus entity and focus position"),
    ("squad", 0x103166B0, "get focus",
     "returns the squad focus entity and position"),
    ("squad", 0x10273E10, "shared enemies",
     "returns squad+8 while connected and the disconnected global enemy store otherwise"),
    ("squad", 0x1026D050, "disconnect",
     "increments the disconnect refcount and leaves shared memory on the zero-to-one edge"),
    ("squad", 0x1026D0C0, "reconnect",
     "decrements with a zero floor and rejoins shared memory when the count reaches zero"),
    ("squad", 0x10316700, "leave",
     "does nothing: retail's LeaveSquad body is an empty RET 4 stub"),

    ("coordinator", 0x1025DB50, "is full",
     "true when the registered melee count has reached the coordinator's slot count"),
    ("coordinator", 0x1025DB70, "admit",
     "admits the NPC to a melee slot when capacity and registration rules allow it"),
    ("coordinator", 0x1025DCA0, "request attacker slot",
     "requests the attacker-side melee slot and answers whether it was acquired"),
    ("coordinator", 0x1025DDD0, "release",
     "removes the NPC from the coordinator's registered handle array"),
    ("coordinator", 0x1025DE90, "is unregistered",
     "returns true when a linear scan finds no coordinator entry for this NPC"),

    ("standoff", 0x102C87A0, "activate",
     "clamps aggressiveness, resolves actors, marks active and enables the goal on each actor"),
    ("standoff", 0x102C8830, "deactivate",
     "clamps aggressiveness, disables the goal on each actor, then removes the listener"),
    ("standoff", 0x102CDC50, "remove",
     "deactivates an active goal through slot 243 before base removal"),
    ("standoff", 0x102C79E0, "translate activity",
     "translates low-aim and cover activities from goal state, hint type and owned weapon"),

    ("sound", 0x101BAC90, "insert",
     "adds a typed, owned sound with origin, integer radius, insertion time and expiry"),
    ("sound", 0x101BB150, "sound by index",
     "returns the active shared-list sound at an index, rejecting invalid indices"),
    ("sound", 0x1030F940, "listen",
     "links every newly inserted sound matching interests and CanHearSound, then stamps listen time"),
    ("sound", 0x1030F7B0, "can hear",
     "answers freshness, owner, range, hearing scalar, stealth, occlusion and QueryHearSound gates"),
)

#: Calls hidden behind jump thunks or virtual input dispatch do not appear as direct graph edges.
CALLER_OVERRIDES = {
    0x1026D0C0: ("FUN_10009601 (jump thunk)",),
    0x102C87A0: ("CAI_GoalEntity::InputActivate (slot 241)",),
    0x102C79E0: ("CAI_StandoffBehavior::TranslateActivity (slot 22)",),
    0x102CDC50: ("CBaseEntity removal dispatch (slot 180)",),
}

CALLER_NAMES_LIMIT = 3

ADDRESS_CELL = re.compile(r"`0x([0-9a-fA-F]+)`")


def _unnamed(address: int) -> str:
    return f"FUN_{address:08x}"


def _cells(line: str) -> list[str]:
    return [cell.strip() for cell in line.split("|")]


def parse_functions(path: Path) -> tuple[dict[int, str], dict[int, str]]:
    """Every ledger row: address -> (column-2 name, column-7 `Callers` cell)."""
    names: dict[int, str] = {}
    callers_cell: dict[int, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "|" not in line:
            continue
        cells = _cells(line)
        match = ADDRESS_CELL.fullmatch(cells[1]) if len(cells) > 7 else None
        if match:
            address = int(match.group(1), 16)
            names[address] = cells[2]
            callers_cell[address] = cells[7]
    return names, callers_cell


def parse_call_graph(path: Path) -> tuple[dict[int, set[int]], dict[int, set[int]]]:
    """graph.tsv as (callee -> callers, caller -> callees)."""
    callers: dict[int, set[int]] = collections.defaultdict(set)
    callees: dict[int, set[int]] = collections.defaultdict(set)
    for line in path.read_text(encoding="utf-8").splitlines()[1:]:
        parts = line.split("\t")
        if len(parts) < 2 or not parts[0] or not parts[1]:
            continue
        caller, callee = int(parts[0], 16), int(parts[1], 16)
        callers[callee].add(caller)
        callees[caller].add(callee)
    return callers, callees


def oracle_sections(path: Path) -> dict[int, list[str]]:
    """index.md as the row's Address column -> its section titles.

    A line's Sections cell holds `path § title` entries; the next entry's `docs/` path is the
    only reliable separator, because a title may itself carry commas and addresses.
    """
    sections: dict[int, list[str]] = collections.defaultdict(list)
    for line in path.read_text(encoding="utf-8").splitlines():
        cells = _cells(line)
        match = ADDRESS_CELL.fullmatch(cells[1]) if len(cells) > 3 else None
        if not match:
            continue
        address = int(match.group(1), 16)
        for entry in cells[3].split(", docs/"):
            entry = entry if entry.startswith("docs/") else "docs/" + entry
            if " § " in entry:
                title = entry.split(" § ", 1)[1]
                if title not in sections[address]:
                    sections[address].append(title)
    return sections


def build_report() -> dict:
    root = repo_root()
    names, callers_cell = parse_functions(root / "docs/vtmb/npc-kernel/functions.md")
    callers, _ = parse_call_graph(root / "docs/vtmb/npc-kernel/graph.tsv")
    sections = oracle_sections(root / "docs/vtmb/npc-kernel/index.md")

    rows = []
    missing_ledger = []
    missing_callers = []
    for obj, address, query, answer in QUERY_SPECS:
        name = names.get(address, _unnamed(address))
        caller_set = callers.get(address, set())
        caller_names = [names.get(c, _unnamed(c)) for c in sorted(caller_set)]
        for caller in CALLER_OVERRIDES.get(address, ()):
            if caller not in caller_names:
                caller_names.append(caller)
        # Reconnect is reached through a jump thunk whose target is absent from functions.md.
        if address not in names and address != 0x1026D0C0:
            missing_ledger.append(f"{obj} 0x{address:08x}")
        if not caller_names:
            missing_callers.append(f"{obj} 0x{address:08x}")
        row = {
            "object": obj,
            "address": address,
            "name": name,
            "query": query,
            "answer": answer,
            "callers_cell": callers_cell.get(address, ""),
            "callers": len(caller_names),
            "caller_names": caller_names,
            "oracle_sections": sections.get(address, []),
        }
        rows.append(row)

    if missing_ledger:
        raise ValueError("query surface bodies missing from functions.md: " + ", ".join(missing_ledger))
    if missing_callers:
        raise ValueError("query surface bodies have no recovered caller: " + ", ".join(missing_callers))
    if any(not row["answer"].strip() for row in rows):
        raise ValueError("query surface contains a blank retail answer")

    counts = []
    for obj in dict.fromkeys(obj for obj, _, _, _ in QUERY_SPECS):
        obj_rows = [row for row in rows if row["object"] == obj]
        counts.append({
            "object": obj,
            "queries": len(obj_rows),
            "named": sum(1 for row in obj_rows if not row["name"].startswith("FUN_")),
            "with_callers": sum(1 for row in obj_rows if row["callers"]),
            "with_answers": sum(1 for row in obj_rows if row["answer"]),
        })
    return {"rows": rows, "counts": counts}


def _cell(value) -> str:
    text = "" if value is None else str(value)
    return text.replace("|", "\\|").replace("\n", " ")


def _md_table(headers: list[str], rows: list[list]) -> list[str]:
    lines = ["| " + " | ".join(headers) + " |",
             "| " + " | ".join("---" for _ in headers) + " |"]
    for row in rows:
        lines.append("| " + " | ".join(_cell(cell) for cell in row) + " |")
    return lines


def print_report(report: dict) -> None:
    print("_Computed by `uv run elysium research ai_infra_surface` from the recovered "
          "address-backed helper interface, joined with `graph.tsv`, `functions.md` and "
          "`index.md`._")
    print()
    for obj in dict.fromkeys(row["object"] for row in report["rows"]):
        print(f"### {obj}")
        rows = []
        for row in report["rows"]:
            if row["object"] != obj:
                continue
            rows.append([
                row["query"],
                f"0x{row['address']:08x}",
                row["name"],
                " / ".join(row["caller_names"][:CALLER_NAMES_LIMIT]),
                row["answer"],
                "; ".join(row["oracle_sections"]),
            ])
        for line in _md_table(
                ["query", "retail address", "ledger name", "caller (<=3)",
                 "what it answers", "oracle section"], rows):
            print(line)
        print()

    print("### Counts")
    rows = [
        [entry["object"], entry["queries"], entry["named"], entry["with_callers"],
         entry["with_answers"]]
        for entry in report["counts"]
    ]
    for line in _md_table(
            ["object", "queries", "with a name", "with >=1 caller", "with an answer"], rows):
        print(line)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", help="write the complete generated ledger here")
    args = parser.parse_args()
    report = build_report()
    print_report(report)
    if args.json:
        parent = os.path.dirname(os.path.abspath(args.json))
        if parent:
            os.makedirs(parent, exist_ok=True)
        with open(args.json, "w") as handle:
            json.dump(report, handle, indent=1, sort_keys=True)


if __name__ == "__main__":
    main()
