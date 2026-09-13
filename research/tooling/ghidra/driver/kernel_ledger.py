#!/usr/bin/env python3
"""The NPC kernel ledger: every function, field and vtable slot of `CAI_BaseNPC`'s family, as tables.

Owner-run archaeology over the whole-body corpus (`corpus.py`'s SQLite), the same policy as
`research/tooling/gen_action_tables.py`: off every build path, regenerated on demand, reviewed as
text, `--check` verifies the committed output without writing. It emits addresses, names,
offsets, edges and counts — the category `docs/vtmb/` already commits — and never a decompiled
body.

What it answers, once, for the whole kernel instead of once per story:

* `classes.md`   — the family tree: the 77 classes whose primary vtable spans the NPC slot
                   range, with their direct base, entity classnames and body counts.
* `slots.md`     — one row per vtable slot: the base body, the Troika body, every species
                   override, the recovered name, dispatch sites, and where the port and the
                   oracle already cite it.
* `fields.md`    — one row per offset of the flattened `CAI_BaseNPCTroika` layout: name, type,
                   the functions that WRITE it and the ones that READ it, and the port member.
* `functions.md` — one row per function in the kernel's closure: class, name, size, damage,
                   slots, callers, fields written / read, strings, port and oracle citations.
* `graph.tsv`    — the call edges inside the closure (`direct`, `virtual`, `slot-candidate`).
* `order.md`     — the build order: strongly connected components collapsed, then a layered
                   topological order where a function sits after every writer of every field it
                   reads and after every function it calls.
* `coverage.md`  — what the port and the oracle already cite, what nothing does, stale citations,
                   damaged bodies, and the counts a commit message quotes.
* `unnamed.md`   — closure functions still named `FUN_`, ranked: the naming backlog.
* `index.md`     — address → the `docs/vtmb` file and section that walks it.

Two caveats, stated in the tables' README: READ/WRITE is a regex over the decompiled C (the
corpus stores which offsets a body touches, not the direction), and a `slot-candidate` edge is a
candidate, not a fact — it says a body dispatches through `this` at a slot the family fills.

Usage::

    uv run elysium research kernel_ledger
    uv run elysium research kernel_ledger --check
    uv run elysium research kernel_ledger --out <dir> --depth 6
"""

from __future__ import annotations

import argparse
import collections
import datetime as _dt
import os
import re
import sqlite3
import sys
from dataclasses import dataclass, field
from pathlib import Path


def _load_local_environment() -> None:
    """`.elysium.local.env`, read directly, so the tool also runs outside `uv run elysium`."""
    if os.environ.get("ELYSIUM_WORK_ROOT"):
        return
    for parent in Path(__file__).resolve().parents:
        candidate = parent / ".elysium.local.env"
        if not candidate.is_file():
            continue
        for line in candidate.read_text(encoding="utf-8-sig").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            name, _, value = line.partition("=")
            os.environ.setdefault(name.strip(), value.strip().strip("'\""))
        return


_load_local_environment()

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))
sys.path.insert(0, str(_HERE.parents[1]))   # research/tooling, for `probes`
sys.path.insert(0, str(_HERE.parents[3]))   # the repository, for `research.tooling.probes`
import corpus  # noqa: E402  -- `_severe`, `_corpus_dir`: the corpus's own reading of itself

from elysium_pipeline.paths import repo_root  # noqa: E402

MODULE = "vampire.dll"
# A class belongs to the family when its primary vtable reaches the NPC slot range. The goal and
# behavior classes stop at 248 slots; every `CAI_BaseNPC` descendant carries 580+.
FAMILY_MIN_SLOTS = 580
# Helper classes the kernel owns outright: their bodies are seeds even though their vtables are
# short, and their fields are reported beside the NPC's.
HELPER_CLASSES = (
    "CAI_Motor", "CAI_Navigator", "CAI_Pathfinder", "CAI_Path", "CAI_Hint", "CAISound",
    "CAI_Senses", "CAI_Memory", "CAI_Squad", "CAI_PatrolPath", "CAI_StandoffBehavior",
    "CAI_StandoffGoal", "CAI_GoalEntity", "CAI_InterestingPlace",
    "CAI_InterestingPlaceConverstation", "CAI_DelayedCondition", "CAI_DelayedConditionList",
    "CAI_MoveAndShootOverlay", "CStealthKillRules", "CNPCMaker", "CNPCMaker_Fleshpile",
    "CNPCMaker_Zombie", "CCineNPC", "CCineAI", "CCineAISchedule",
)
# Classes below the kernel. Their bodies fill inherited slots, so they are rows, but the walk does
# not follow their callees: that way lies the whole entity system.
BOUNDARY_CLASSES = frozenset({
    "CBaseEntity", "CBaseAnimating", "CBaseFlex", "CBaseActor", "CBaseCombatWeapon",
    "CBasePlayer", "CHL2_Player", "CAI_BaseActor",
})
# The kernel's own roots beside the slot bodies.
ROOT_FUNCTIONS = ("10292de0", "1026f110")   # NPCThink, RunAI
DEFAULT_DEPTH = 6
ENTRY_MAX_CALLERS = 100
DEFAULT_OUTPUT = ("docs", "vtmb", "npc-kernel")
GENERATED_BANNER = "<!-- generated by `uv run elysium research kernel_ledger`; do not hand-edit -->"

# The port and the oracle cite retail by these two spellings and nothing else.
ADDRESS_RE = re.compile(r"\b0x(10[0-9a-f]{6})\b")
OFFSET_RE = re.compile(r"\+0x([0-9a-f]{2,4})\b")
SEAM_RE = re.compile(r"SEAM:|CHOSEN, NOT RECOVERED|[Uu]nrecovered")
# Decompiled-C shapes. `this + 0xNN` and `this->name` are the typed forms; `field_0xNNNN` is what
# the typer names a member the datamap does not state.
THIS_OFFSET_RE = re.compile(r"\bthis\s*\+\s*(0x[0-9a-fA-F]+|\d+)\)")
# The receiver of a `__fastcall`/`__thiscall` body the typer could not name arrives as `param_1`;
# when the body never mentions `this`, `param_1 + 0xNN` is the same member access, guessed.
PARAM1_OFFSET_RE = re.compile(r"\bparam_1\s*\+\s*(0x[0-9a-fA-F]+|\d+)\)")
THIS_NAMED_RE = re.compile(r"\bthis->([A-Za-z_][A-Za-z0-9_]*)")
FIELD_OFFSET_NAME_RE = re.compile(r"^field_0x([0-9a-fA-F]+)$")
ASSIGN_TAIL_RE = re.compile(r"^\s*(?:=(?!=)|\+\+|--|[-+*/%|&^]=|<<=|>>=)")
DEREF_ASSIGN_TAIL_RE = re.compile(r"^\s*\)\s*(?:=(?!=)|\+\+|--|[-+*/%|&^]=|<<=|>>=)")
COPY_CALLEE_RE = re.compile(r"(?:memcpy|memset|strcpy|strncpy|_strncpy|Q_strncpy|V_strncpy)$")
CAST_NOISE_RE = re.compile(
    r"[()\s*&]|\b(?:void|int|char|undefined\d*|float|uint|byte|short|ushort|longlong)\b")
JMP_RE = re.compile(r"\bJMP\s+0x([0-9a-fA-F]{8})\b")
CLASSNAMES_RE = re.compile(
    r"const TCHAR\* (\w+)_Classnames\[\] = \{([^}]*)\}", re.S)
GCLASS_ROW_RE = re.compile(r'\{ TEXT\("(\w+)"\), TEXT\("(\w+)"\),')
TEXT_RE = re.compile(r'TEXT\("([^"]+)"\)')


def _hex(addr: str) -> str:
    return f"0x{addr}"


def _fmt_off(off: int) -> str:
    return f"+0x{off:04x}"


@dataclass
class Function:
    addr: str
    name: str
    ns: str
    size: int
    thunk: bool
    warn: str
    code: str
    depth: int = -1
    cc: str = ""
    slots: list[tuple[str, int]] = field(default_factory=list)
    writes: set[int] = field(default_factory=set)
    reads: set[int] = field(default_factory=set)
    guessed: set[int] = field(default_factory=set)   # offsets reached through `param_1`
    strings: list[str] = field(default_factory=list)

    @property
    def label(self) -> str:
        if self.ns in ("", "Global"):
            return self.name
        return f"{self.ns}::{self.name}"

    @property
    def damaged(self) -> list[str]:
        return corpus._severe(self.warn)

    @property
    def unnamed(self) -> bool:
        return self.name.startswith("FUN_") or self.name.startswith("thunk_FUN_")


@dataclass
class Citation:
    path: str
    line: int
    seam: bool
    text: str


class Ledger:
    """One read-only pass over the corpus, then the derivations the tables are rendered from."""

    def __init__(self, module: str, depth: int, repo: Path):
        self.module = module
        self.depth = depth
        self.repo = repo
        base = corpus._corpus_dir()
        self.db = sqlite3.connect(f"file:{base / 'corpus.sqlite'}?mode=ro", uri=True)
        self.db.row_factory = sqlite3.Row
        listing = base / "listing.sqlite"
        self.listing = (sqlite3.connect(f"file:{listing}?mode=ro", uri=True)
                        if listing.is_file() else None)
        self.meta = dict(self.db.execute(
            "SELECT module, binary, sha256, dumped_at FROM meta WHERE module = ?",
            (module,)).fetchone() or {})
        self.functions: dict[str, Function] = {}
        self.thunk_target: dict[str, str] = {}
        self.edges: dict[str, set[tuple[str, str]]] = collections.defaultdict(set)
        self.family: list[str] = []
        self.helpers: list[str] = []
        self.bases: dict[str, str] = {}
        self.classnames: dict[str, list[str]] = {}
        self.vtable_addr: dict[str, str] = {}
        self.slot_bodies: dict[int, dict[str, str]] = collections.defaultdict(dict)  # slot → cls → func
        self.slot_count: dict[str, int] = {}
        self.this_dispatch: dict[str, set[int]] = collections.defaultdict(set)
        self.slot_dispatch_sites: dict[int, int] = collections.Counter()
        self.fields: dict[int, sqlite3.Row] = {}
        self.class_fields: dict[str, dict[str, int]] = collections.defaultdict(dict)
        self.species_fields: dict[str, list[sqlite3.Row]] = collections.defaultdict(list)
        self.closure: dict[str, int] = {}
        self.closure_edges: set[tuple[str, str, str]] = set()
        self.port_addr: dict[str, list[Citation]] = collections.defaultdict(list)
        self.port_off: dict[int, list[Citation]] = collections.defaultdict(list)
        self.oracle_addr: dict[str, list[Citation]] = collections.defaultdict(list)
        self.oracle_off: dict[int, list[Citation]] = collections.defaultdict(list)
        self.spec_addr: dict[str, list[Citation]] = collections.defaultdict(list)
        self.port_member: dict[int, str] = {}
        self.oracle_sections: dict[str, list[tuple[str, str]]] = collections.defaultdict(list)
        self.stale_port: list[Citation] = []
        self.layers: list[list[list[str]]] = []
        self.layer_of: dict[str, int] = {}
        self.unwritten: dict[str, set[int]] = collections.defaultdict(set)
        self.later_producers: dict[str, dict[int, list[str]]] = collections.defaultdict(dict)
        self.all_callers: dict[str, set[str]] = collections.defaultdict(set)
        self.other_touches: dict[int, list[str]] = collections.defaultdict(list)
        self.globals: dict[str, str] = {}
        self.string_addrs: set[str] = set()
        self.vtable_owner: dict[str, str] = {}
        self.ranges: list[tuple[int, int, str]] = []
        self.interior: dict[str, list[Citation]] = collections.defaultdict(list)
        self.stale_kinds: dict[str, str] = {}

    # -- loading -------------------------------------------------------------------------------

    def load(self) -> None:
        self._load_functions()
        self._load_thunks()
        self._load_edges()
        self._load_family()
        self._load_fields()
        self._load_dispatch()
        self._load_hierarchy()

    def _load_functions(self) -> None:
        for row in self.db.execute(
                "SELECT addr, name, ns, size, thunk, warn, code, cc FROM functions WHERE module = ?",
                (self.module,)):
            self.functions[row["addr"]] = Function(
                row["addr"], row["name"], row["ns"] or "", row["size"] or 0,
                bool(row["thunk"]), row["warn"] or "", row["code"] or "", cc=row["cc"] or "")
        for row in self.db.execute(
                "SELECT addr, name FROM names WHERE module = ?", (self.module,)):
            fn = self.functions.get(row["addr"])
            if fn and fn.unnamed:
                fn.name = row["name"]
                if "::" in row["name"] and fn.ns in ("", "Global"):
                    fn.ns, fn.name = row["name"].split("::", 1)
        self.ranges = sorted((int(a, 16), int(a, 16) + max(f.size, 1), a)
                             for a, f in self.functions.items())
        for row in self.db.execute(
                "SELECT addr, name FROM globals WHERE module = ?", (self.module,)):
            self.globals[row["addr"]] = row["name"] or ""
        self.string_addrs = {row[0] for row in self.db.execute(
            "SELECT addr FROM strings WHERE module = ?", (self.module,))}
        for row in self.db.execute(
                "SELECT DISTINCT table_addr, cls FROM vtables WHERE module = ?", (self.module,)):
            self.vtable_owner[row["table_addr"]] = row["cls"]

    def locate(self, addr: str) -> tuple[str, str]:
        """What a cited address is: a function start, a site inside one, a global, a string, a
        vtable, or nothing the corpus knows."""
        if addr in self.functions:
            return "function", addr
        value = int(addr, 16)
        import bisect
        i = bisect.bisect_right(self.ranges, (value, 1 << 40, "")) - 1
        if i >= 0 and self.ranges[i][0] <= value < self.ranges[i][1]:
            return "inside", self.ranges[i][2]
        if addr in self.globals:
            return "global", self.globals[addr]
        if addr in self.string_addrs:
            return "string", ""
        if addr in self.vtable_owner:
            return "vtable", self.vtable_owner[addr]
        return "unknown", ""

    def _load_thunks(self) -> None:
        """A thunk is `JMP target`; the corpus stores no edge for it, so the listing says."""
        thunks = [a for a, f in self.functions.items() if f.thunk]
        if self.listing is not None:
            for addr in thunks:
                row = self.listing.execute(
                    "SELECT asm FROM listing WHERE module = ? AND addr = ?",
                    (self.module, addr)).fetchone()
                if not row:
                    continue
                match = JMP_RE.search(row[0])
                if match and match.group(1).lower() in self.functions:
                    self.thunk_target[addr] = match.group(1).lower()
        for addr in thunks:
            if addr in self.thunk_target:
                continue
            name = self.functions[addr].name
            if name.startswith("thunk_FUN_"):
                target = name[len("thunk_FUN_"):].lower()
                if target in self.functions:
                    self.thunk_target[addr] = target

    def resolve(self, addr: str) -> str:
        seen = set()
        while addr in self.thunk_target and addr not in seen:
            seen.add(addr)
            addr = self.thunk_target[addr]
        return addr

    def _load_edges(self) -> None:
        for row in self.db.execute(
                "SELECT caller, callee, kind FROM edges WHERE module = ?", (self.module,)):
            callee = self.resolve(row["callee"])
            caller = self.resolve(row["caller"])
            if callee in self.functions and caller != callee:
                self.edges[caller].add((callee, row["kind"] or "direct"))
                self.all_callers[callee].add(caller)

    def _load_family(self) -> None:
        rows = self.db.execute(
            "SELECT cls, table_addr, max(slot) + 1 AS slots FROM vtables "
            "WHERE module = ? AND sub = 0 GROUP BY cls", (self.module,)).fetchall()
        for row in rows:
            self.slot_count[row["cls"]] = row["slots"]
            self.vtable_addr[row["cls"]] = row["table_addr"]
        self.family = sorted(r["cls"] for r in rows if r["slots"] >= FAMILY_MIN_SLOTS)
        self.helpers = [c for c in HELPER_CLASSES if c in self.slot_count or self._has_fields(c)]
        placeholders = ",".join("?" * len(self.family))
        for row in self.db.execute(
                f"SELECT cls, slot, func FROM vtables WHERE module = ? AND sub = 0 "
                f"AND cls IN ({placeholders})", (self.module, *self.family)):
            self.slot_bodies[row["slot"]][row["cls"]] = self.resolve(row["func"])
        for row in self.db.execute(
                "SELECT cls, slot, func FROM vtables WHERE module = ? AND sub = 0",
                (self.module,)):
            fn = self.functions.get(self.resolve(row["func"]))
            if fn is not None:
                fn.slots.append((row["cls"], row["slot"]))

    def _has_fields(self, cls: str) -> bool:
        return self.db.execute("SELECT 1 FROM fields WHERE module = ? AND cls = ? LIMIT 1",
                               (self.module, cls)).fetchone() is not None

    def _load_fields(self) -> None:
        for row in self.db.execute(
                "SELECT * FROM fields WHERE module = ? AND cls = 'CAI_BaseNPCTroika' ORDER BY off",
                (self.module,)):
            self.fields[row["off"]] = row
        for cls in self.family + self.helpers:
            for row in self.db.execute(
                    "SELECT * FROM fields WHERE module = ? AND cls = ? ORDER BY off",
                    (self.module, cls)):
                self.class_fields[cls][row["name"]] = row["off"]
                if cls in self.family and row["off"] not in self.fields:
                    self.species_fields[cls].append(row)

    def _load_dispatch(self) -> None:
        for row in self.db.execute(
                "SELECT caller, slot, cls, recv FROM vcalls WHERE module = ?", (self.module,)):
            self.slot_dispatch_sites[row["slot"]] += 1
            if row["cls"] == "" and row["recv"] == "*(int *)this":
                self.this_dispatch[self.resolve(row["caller"])].add(row["slot"])
            elif row["cls"] in self.family:
                self.this_dispatch[self.resolve(row["caller"])].add(row["slot"])

    def _load_hierarchy(self) -> None:
        """Direct bases from the pinned binary's RTTI, else from the committed class table."""
        try:
            from probes import npc_translation_survey, weapon_activity_survey  # noqa: WPS433
            binary = Path(self.meta.get("binary") or "")
            if binary.is_file():
                data = binary.read_bytes()
                image = weapon_activity_survey.PEImage(data)
                classes = npc_translation_survey.find_npc_classes(image)
                classes = npc_translation_survey.decode_translation_slots(image, classes)
                for row in classes:
                    self.bases[row["cpp_class"]] = row["direct_base"]
                    self.classnames[row["cpp_class"]] = list(row.get("entity_classnames", []))
                if self.bases:
                    return
        except Exception as error:  # noqa: BLE001 -- the committed table is the fallback
            print(f"kernel_ledger: RTTI walk unavailable ({error}); reading the committed table",
                  file=sys.stderr)
        table = self.repo / "Source/ElysiumUE/Private/Visual/ElysiumNpcActivityTables.cpp"
        if not table.is_file():
            return
        text = table.read_text(encoding="utf-8")
        for cls, base in GCLASS_ROW_RE.findall(text):
            self.bases[cls] = base
        for cls, body in CLASSNAMES_RE.findall(text):
            self.classnames[cls] = TEXT_RE.findall(body)

    # -- derivations ---------------------------------------------------------------------------

    def walk(self) -> None:
        seeds = {self.resolve(f) for bodies in self.slot_bodies.values() for f in bodies.values()}
        for cls in self.helpers:
            for row in self.db.execute(
                    "SELECT func FROM vtables WHERE module = ? AND cls = ? AND sub = 0",
                    (self.module, cls)):
                seeds.add(self.resolve(row["func"]))
        seeds.update(a for a in ROOT_FUNCTIONS if a in self.functions)
        seeds = {s for s in seeds if s in self.functions}
        depth = {s: 0 for s in seeds}
        frontier = sorted(seeds)
        while frontier:
            nxt = []
            for addr in frontier:
                fn = self.functions[addr]
                if fn.ns in BOUNDARY_CLASSES or depth[addr] >= self.depth:
                    continue
                outs: set[tuple[str, str]] = set(self.edges.get(addr, ()))
                for slot in self.this_dispatch.get(addr, ()):
                    for body in self.slot_bodies.get(slot, {}).values():
                        outs.add((body, "slot-candidate"))
                for callee, kind in outs:
                    if callee not in self.functions:
                        continue
                    self.closure_edges.add((addr, callee, kind))
                    if callee not in depth:
                        depth[callee] = depth[addr] + 1
                        nxt.append(callee)
            frontier = sorted(nxt)
        self.closure = depth
        for addr, d in depth.items():
            self.functions[addr].depth = d
        self.closure_edges = {e for e in self.closure_edges if e[1] in self.closure}

    def directions(self) -> None:
        for addr in self.closure:
            fn = self.functions[addr]
            names = self.class_fields.get(fn.ns) or self.class_fields.get("CAI_BaseNPCTroika", {})
            fn.reads, fn.writes = self._scan(fn.code, names)
            if fn.cc in ("__fastcall", "__thiscall") and "this" not in fn.code:
                reads, writes = self._scan(fn.code, names, PARAM1_OFFSET_RE)
                fn.guessed = reads | writes
                fn.reads |= reads
                fn.writes |= writes
        # The corpus's own untyped tier: every body in the module that touches the offset through
        # a receiver it could not type. Outside the closure these are the producers that live in
        # another subsystem (a spawner writing an NPC's save position), so they are listed as
        # candidates beside the classified closure rows.
        for row in self.db.execute(
                "SELECT DISTINCT func_addr, off FROM accesses WHERE module = ? AND off IS NOT NULL",
                (self.module,)):
            addr = self.resolve(row["func_addr"])
            if addr in self.functions and addr not in self.closure \
                    and addr not in self.other_touches[row["off"]]:
                self.other_touches[row["off"]].append(addr)
        for row in self.db.execute(
                "SELECT r.func_addr, s.text FROM string_refs r JOIN strings s "
                "ON s.module = r.module AND s.addr = r.str_addr WHERE r.module = ?",
                (self.module,)):
            fn = self.functions.get(self.resolve(row["func_addr"]))
            if fn is not None and fn.addr in self.closure:
                fn.strings.append(row["text"])

    @staticmethod
    def _scan(code: str, names: dict[str, int],
              offset_re: re.Pattern = THIS_OFFSET_RE) -> tuple[set[int], set[int]]:
        """Offsets a body touches, split by whether the touch is an assignment.

        `*(T *)((int)this + 0xNN) = v` and `this->name = v` are writes; `++`, `--` and the compound
        assignments too; a `this + 0xNN` handed to a copy routine as its destination as well.
        Everything else is a read. Direction is what the corpus does not store, so this is the
        one heuristic in the ledger, and the README says so.
        """
        reads: set[int] = set()
        writes: set[int] = set()

        def classify(off: int, start: int, end: int) -> None:
            tail = code[end:end + 12]
            head = code[max(0, start - 40):start]
            if ASSIGN_TAIL_RE.match(tail) or DEREF_ASSIGN_TAIL_RE.match(tail):
                writes.add(off)
            elif COPY_CALLEE_RE.search(CAST_NOISE_RE.sub("", head)):
                writes.add(off)   # the destination argument of a copy routine
            else:
                reads.add(off)

        for match in offset_re.finditer(code):
            off = int(match.group(1), 0)
            classify(off, match.start(), match.end())
        if offset_re is not THIS_OFFSET_RE:
            return reads, writes
        for match in THIS_NAMED_RE.finditer(code):
            name = match.group(1)
            off = names.get(name)
            if off is None:
                anon = FIELD_OFFSET_NAME_RE.match(name)
                if not anon:
                    continue
                off = int(anon.group(1), 16)
            classify(off, match.start(), match.end())
        return reads, writes

    def citations(self) -> None:
        """Where the port and the oracle already speak about an address or an offset."""
        source = self.repo / "Source" / "ElysiumUE"
        for path in sorted(source.rglob("*")):
            if path.suffix not in (".h", ".cpp"):
                continue
            self._scan_citations(path, self.port_addr, self.port_off, member=True)
        for path in sorted((self.repo / "docs" / "vtmb").rglob("*.md")):
            if path.parent.name == DEFAULT_OUTPUT[-1]:
                continue
            self._scan_citations(path, self.oracle_addr, self.oracle_off, sections=True)
        for path in sorted((self.repo / "docs" / "specs").rglob("*.md")):
            self._scan_citations(path, self.spec_addr, collections.defaultdict(list))
        for table in (self.port_addr, self.oracle_addr, self.spec_addr):
            for addr in list(table):
                kind, owner = self.locate(addr)
                if kind == "inside":
                    self.interior[owner].extend(table[addr])
                elif kind == "unknown" and table is self.port_addr:
                    self.stale_port.extend(table[addr])
                if kind != "function":
                    self.stale_kinds[addr] = kind

    def cites(self, table: dict[str, list[Citation]], addr: str) -> list[Citation]:
        """Citations of a function: its start address and every site inside its body."""
        found = list(table.get(addr, []))
        for c in self.interior.get(addr, []):
            if c not in found and any(c in v for v in table.values()):
                found.append(c)
        return found

    def _scan_citations(self, path: Path, by_addr: dict, by_off: dict,
                        member: bool = False, sections: bool = False) -> None:
        rel = path.relative_to(self.repo).as_posix()
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        section = ""
        for number, line in enumerate(lines, 1):
            if sections and line.startswith("#"):
                section = line.lstrip("#").strip()
            seam = bool(SEAM_RE.search(line))
            for match in ADDRESS_RE.finditer(line):
                addr = match.group(1).lower()
                by_addr[addr].append(Citation(rel, number, seam, line.strip()))
                if sections and section:
                    self.oracle_sections[addr].append((rel, section))
            for match in OFFSET_RE.finditer(line):
                off = int(match.group(1), 16)
                by_off[off].append(Citation(rel, number, seam, line.strip()))
                if member and path.suffix == ".h" and off not in self.port_member:
                    ident = self._declared_member(lines, number - 1)
                    if ident:
                        self.port_member[off] = ident

    @staticmethod
    def _declared_member(lines: list[str], index: int) -> str:
        """The identifier a header declares on the cited line or the next few: a guess, labelled."""
        decl = re.compile(r"^\s*(?:[A-Za-z_][\w:<>,\s\*&]*?)\s+(\w+)\s*(?:=|;|\{)")
        for line in lines[index:index + 4]:
            stripped = line.strip()
            if stripped.startswith("//") or stripped.startswith("*") or not stripped:
                continue
            match = decl.match(line)
            if match and not stripped.startswith(("virtual", "return", "if", "for")):
                return match.group(1)
            if stripped.startswith(("virtual", "void", "bool", "int", "float", "double")):
                return ""
        return ""

    def order(self, candidates: bool = False) -> None:
        """Build order over the call graph: a function after its callees.

        Field dependencies are not edges here. The kernel's words are read-modify-written from
        everywhere, so writer→reader edges collapse 1,300 functions into one cycle and say nothing.
        They are annotations instead: `later_producers` names the fields a function reads whose
        every writer sits in a later layer, and `unwritten` the fields nothing in the closure
        writes. Slot-candidate edges are off by default for the same reason.
        """
        nodes = sorted(self.closure)
        kinds = {"direct", "virtual"} | ({"slot-candidate"} if candidates else set())
        succ: dict[str, set[str]] = {a: set() for a in nodes}
        for caller, callee, kind in self.closure_edges:
            if caller != callee and kind in kinds:
                succ[callee].add(caller)
        components = _tarjan(nodes, succ)
        comp_of = {a: i for i, comp in enumerate(components) for a in comp}
        csucc: dict[int, set[int]] = collections.defaultdict(set)
        indeg = collections.Counter()
        for a in nodes:
            for b in succ[a]:
                ca, cb = comp_of[a], comp_of[b]
                if ca != cb and cb not in csucc[ca]:
                    csucc[ca].add(cb)
                    indeg[cb] += 1
        ready = sorted(i for i in range(len(components)) if indeg[i] == 0)
        layers: list[list[list[str]]] = []
        while ready:
            layers.append([sorted(components[i]) for i in ready])
            nxt = []
            for i in ready:
                for j in sorted(csucc[i]):
                    indeg[j] -= 1
                    if indeg[j] == 0:
                        nxt.append(j)
            ready = sorted(set(nxt))
        self.layers = layers
        for i, layer in enumerate(layers):
            for comp in layer:
                for addr in comp:
                    self.layer_of[addr] = i
        writers: dict[int, set[str]] = collections.defaultdict(set)
        for addr in nodes:
            for off in self.functions[addr].writes:
                writers[off].add(addr)
        for addr in nodes:
            mine = self.layer_of[addr]
            for off in self.functions[addr].reads:
                if off not in self.fields:
                    continue
                if not writers[off]:
                    self.unwritten[addr].add(off)
                    continue
                later = [w for w in writers[off] if w != addr and self.layer_of[w] > mine]
                if later and len(later) == len([w for w in writers[off] if w != addr]):
                    self.later_producers[addr][off] = sorted(later)

    # -- rendering -----------------------------------------------------------------------------

    def render(self) -> dict[str, str]:
        return {
            "classes.md": self._render_classes(),
            "slots.md": self._render_slots(),
            "fields.md": self._render_fields(),
            "functions.md": self._render_functions(),
            "graph.tsv": self._render_graph(),
            "order.md": self._render_order(),
            "coverage.md": self._render_coverage(),
            "unnamed.md": self._render_unnamed(),
            "entries.md": self._render_entries(),
            "index.md": self._render_index(),
        }

    def _head(self, title: str, blurb: str) -> list[str]:
        sha = (self.meta.get("sha256") or "")[:16]
        return [GENERATED_BANNER, f"# {title}", "",
                f"_{self.module} sha256 `{sha}…`, corpus dumped "
                f"{_dt.datetime.fromtimestamp(self.meta.get('dumped_at') or 0):%Y-%m-%d}; "
                f"closure depth {self.depth}. Rebuild: `uv run elysium research kernel_ledger`._",
                "", blurb, ""]

    def _fn_cell(self, addr: str, off: int | None = None) -> str:
        fn = self.functions.get(addr)
        if fn is None:
            return f"`{_hex(addr)}`"
        flag = " ‼" if fn.damaged else ""
        guess = "?" if off is not None and off in fn.guessed else ""
        return f"`{_hex(addr)}` {fn.label}{flag}{guess}"

    def _off_cell(self, fn: Function, off: int) -> str:
        return f"`{_fmt_off(off)}`" + ("?" if off in fn.guessed else "")

    def npc_range_start(self) -> int:
        """The first offset past `CBaseCombatCharacter`'s layout: where the NPC's own words begin."""
        row = self.db.execute(
            "SELECT max(off + len) FROM fields WHERE module = ? AND cls = 'CBaseCombatCharacter'",
            (self.module,)).fetchone()
        return int(row[0] or 0)

    def touches_npc(self, fn: Function, start: int) -> bool:
        return any(o >= start for o in fn.reads | fn.writes)

    @staticmethod
    def _list_cell(items: list[str], limit: int = 8) -> str:
        if not items:
            return "—"
        shown = ", ".join(items[:limit])
        return shown if len(items) <= limit else f"{shown}, +{len(items) - limit} more"

    def _cite_cell(self, cites: list[Citation], limit: int = 3) -> str:
        # Sorted, so a set-ordered caller cannot make the tables differ between runs.
        keys = sorted({(c.path, c.line) for c in cites})
        return self._list_cell([f"{p}:{n}" for p, n in keys], limit)

    def _render_classes(self) -> str:
        out = self._head("NPC kernel — classes",
                         f"The {len(self.family)} classes whose primary vtable spans the NPC slot "
                         f"range (≥ {FAMILY_MIN_SLOTS} slots), plus the helper classes the kernel "
                         "owns. Direct bases come from the RTTI walk of the pinned binary, or from "
                         "the committed `ElysiumNpcActivityTables.cpp` when it is absent.")
        out += ["| Class | Direct base | vtable | Slots | Own bodies | Entity classnames |",
                "|---|---|---|---|---|---|"]
        own = collections.Counter()
        for slot, bodies in self.slot_bodies.items():
            for cls, func in bodies.items():
                fn = self.functions.get(func)
                if fn and fn.ns == cls:
                    own[cls] += 1
        for cls in self.family:
            out.append(f"| `{cls}` | `{self.bases.get(cls, '')}` | `{_hex(self.vtable_addr.get(cls, ''))}`"
                       f" | {self.slot_count.get(cls, 0)} | {own[cls]} "
                       f"| {self._list_cell([f'`{n}`' for n in self.classnames.get(cls, [])], 6)} |")
        out += ["", "## Helper classes", "", "| Class | Slots | Fields |", "|---|---|---|"]
        for cls in self.helpers:
            out.append(f"| `{cls}` | {self.slot_count.get(cls, 0)} | {len(self.class_fields.get(cls, {}))} |")
        return "\n".join(out) + "\n"

    def _render_slots(self) -> str:
        out = self._head("NPC kernel — vtable slots",
                         "One row per primary-vtable slot of the family. *Base* is `CAI_BaseNPC`'s "
                         "body, *Troika* is `CAI_BaseNPCTroika`'s when it differs, *Overrides* are "
                         "the species bodies that differ from both. *Sites* counts every dispatch "
                         "through that slot in the module (any receiver class). ‼ marks a damaged "
                         "decompilation.")
        out += ["| Slot | Base | Troika | Overrides | Sites | Port | Oracle |",
                "|---|---|---|---|---|---|---|"]
        for slot in sorted(self.slot_bodies):
            bodies = self.slot_bodies[slot]
            base = bodies.get("CAI_BaseNPC", "")
            troika = bodies.get("CAI_BaseNPCTroika", "")
            overrides = sorted({(f, c) for c, f in bodies.items()
                                if f not in (base, troika) and c not in ("CAI_BaseNPC", "CAI_BaseNPCTroika")})
            grouped: dict[str, list[str]] = collections.defaultdict(list)
            for f, c in overrides:
                grouped[f].append(c)
            over_cells = [f"{self._fn_cell(f)} ({', '.join(sorted(cs))})" for f, cs in sorted(grouped.items())]
            addrs = {a for a in (base, troika, *grouped) if a}
            port = [c for a in addrs for c in self.port_addr.get(a, [])]
            oracle = [c for a in addrs for c in self.oracle_addr.get(a, [])]
            out.append(f"| {slot} | {self._fn_cell(base) if base else '—'} "
                       f"| {self._fn_cell(troika) if troika and troika != base else '—'} "
                       f"| {self._list_cell(over_cells, 6)} | {self.slot_dispatch_sites.get(slot, 0)} "
                       f"| {self._cite_cell(port)} | {self._cite_cell(oracle)} |")
        return "\n".join(out) + "\n"

    def _render_fields(self) -> str:
        out = self._head("NPC kernel — fields",
                         "One row per offset of the flattened `CAI_BaseNPCTroika` layout (the "
                         "`CAI_BaseNPC` layout is its prefix). *Writers* assign the offset; *Readers* "
                         "touch it without assigning. Direction is a regex over the decompiled C. "
                         "*Outside touches* are bodies outside the closure that the corpus saw touch "
                         "the offset through an untyped receiver — candidates, since another "
                         "structure may collide numerically. *Port member* is the identifier "
                         "declared where the port cites the offset — a guess, not a join.")
        out += ["| Offset | Name | Type | Writers | Readers | Outside touches | Port member | Port | Oracle |",
                "|---|---|---|---|---|---|---|---|---|"]
        writers: dict[int, list[str]] = collections.defaultdict(list)
        readers: dict[int, list[str]] = collections.defaultdict(list)
        for addr in sorted(self.closure):
            fn = self.functions[addr]
            for off in fn.writes:
                writers[off].append(addr)
            for off in fn.reads:
                readers[off].append(addr)
        for off, row in sorted(self.fields.items()):
            out.append(f"| `{_fmt_off(off)}` | `{row['name']}` | `{row['type']}`[{row['len']}] "
                       f"| {self._list_cell([self._fn_cell(a, off) for a in writers.get(off, [])], 6)} "
                       f"| {self._list_cell([self._fn_cell(a, off) for a in readers.get(off, [])], 6)} "
                       f"| {self._list_cell([self._fn_cell(a) for a in sorted(self.other_touches.get(off, []))], 4)} "
                       f"| {('`' + self.port_member[off] + '`') if off in self.port_member else '—'} "
                       f"| {self._cite_cell(self.port_off.get(off, []))} "
                       f"| {self._cite_cell(self.oracle_off.get(off, []))} |")
        touched = sorted(o for o in set(writers) | set(readers) if o not in self.fields and o < 0x10000)
        out += ["", "## Offsets touched through `this` that the Troika layout does not state", "",
                "Species-only members, or a member the datamap never declared (`field_0x…`).", "",
                "| Offset | Species field | Writers | Readers | Outside touches |", "|---|---|---|---|---|"]
        species = {r["off"]: (cls, r["name"]) for cls, rows in self.species_fields.items() for r in rows}
        for off in touched:
            sp = species.get(off)
            out.append(f"| `{_fmt_off(off)}` | {('`' + sp[0] + '::' + sp[1] + '`') if sp else '—'} "
                       f"| {self._list_cell([self._fn_cell(a) for a in writers.get(off, [])], 4)} "
                       f"| {self._list_cell([self._fn_cell(a) for a in readers.get(off, [])], 4)} "
                       f"| {self._list_cell([self._fn_cell(a) for a in sorted(self.other_touches.get(off, []))], 4)} |")
        return "\n".join(out) + "\n"

    def _render_functions(self) -> str:
        out = self._head("NPC kernel — functions",
                         "Every function the kernel reaches: the family's slot bodies, `NPCThink` "
                         "and `RunAI`, and their callees to the closure depth, stopping at the entity "
                         "base classes. *Depth* 0 is a seed. ‼ marks a damaged decompilation. "
                         "*Layer* is its position in `order.md`. *Slots* lists `Class#slot` the body "
                         "fills. *Callers* counts closure edges by kind (`d`irect, `v`irtual, "
                         "slot-`c`andidate) and direct callers outside the closure. A citation of an "
                         "address inside the body counts for the body. *Seam* is a port citation "
                         "line carrying `SEAM`, `CHOSEN, NOT RECOVERED` or `unrecovered`.")
        out += ["| Address | Function | Size | Depth | Layer | Slots | Callers | Writes | Reads | Strings | Port | Oracle | Seam |",
                "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
        callers: dict[str, collections.Counter] = collections.defaultdict(collections.Counter)
        for caller, callee, kind in self.closure_edges:
            callers[callee][kind] += 1
        for addr in sorted(self.closure):
            fn = self.functions[addr]
            slots = self._list_cell([f"`{c}#{s}`" for c, s in sorted(fn.slots) if c in self.family or c in self.helpers], 3)
            cnt = callers[addr]
            outside = len(self.all_callers.get(addr, ())) - cnt["direct"] - cnt["virtual"]
            call_cell = f"{cnt['direct']}d/{cnt['virtual']}v/{cnt['slot-candidate']}c" + (f" +{outside} outside" if outside > 0 else "")
            port = self.cites(self.port_addr, addr)
            seam = any(c.seam for c in port)
            out.append(f"| `{_hex(addr)}` | {fn.label}{' ‼' if fn.damaged else ''} | {fn.size} | {fn.depth} | {self.layer_of.get(addr, '')} "
                       f"| {slots} | {call_cell} "
                       f"| {self._list_cell([self._off_cell(fn, o) for o in sorted(fn.writes)], 6)} "
                       f"| {self._list_cell([self._off_cell(fn, o) for o in sorted(fn.reads)], 6)} "
                       f"| {self._list_cell([_short(s) for s in fn.strings], 2)} "
                       f"| {self._cite_cell(port)} | {self._cite_cell(self.cites(self.oracle_addr, addr))} "
                       f"| {'yes' if seam else ''} |")
        return "\n".join(out) + "\n"

    def _render_graph(self) -> str:
        lines = ["caller\tcallee\tkind"]
        for caller, callee, kind in sorted(self.closure_edges):
            lines.append(f"{caller}\t{callee}\t{kind}")
        return "\n".join(lines) + "\n"

    def _render_order(self) -> str:
        out = self._head("NPC kernel — build order",
                         "Strongly connected components of the closure's call graph (direct and "
                         "resolved virtual edges), layered by Kahn's algorithm: a layer's functions "
                         "call only earlier layers. A component with more than one function is a "
                         "cycle and is listed as one unit. Field dependencies are annotations, not "
                         "edges: *Producers later* names fields a function reads whose every writer "
                         "sits in a later layer (port the writer first, or the seam answers nothing "
                         "for a while); *Unwritten* names fields nothing in the closure writes — "
                         "their producer is another subsystem's, see `fields.md` *Outside touches*.")
        for i, layer in enumerate(self.layers):
            singles = [c for c in layer if len(c) == 1]
            cycles = [c for c in layer if len(c) > 1]
            out += ["", f"## Layer {i} — {sum(len(c) for c in layer)} functions", ""]
            if cycles:
                out.append(f"Cycles ({len(cycles)}):")
                out.append("")
                for comp in cycles:
                    out.append(f"- {len(comp)} functions: " + self._list_cell([self._fn_cell(a) for a in comp], 8))
                out.append("")
            out.append("| Function | Producers later | Unwritten reads |")
            out.append("|---|---|---|")
            for comp in singles:
                addr = comp[0]
                later = self.later_producers.get(addr, {})
                later_cell = self._list_cell(
                    [f"`{_fmt_off(o)}` ← {', '.join(_hex(w) for w in ws[:3])}" for o, ws in sorted(later.items())], 4)
                unw = self._list_cell([f"`{_fmt_off(o)}`" for o in sorted(self.unwritten.get(addr, ()))], 6)
                out.append(f"| {self._fn_cell(addr)} | {later_cell if later else ''} "
                           f"| {unw if self.unwritten.get(addr) else ''} |")
        return "\n".join(out) + "\n"

    def _render_coverage(self) -> str:
        closure = sorted(self.closure)
        cited = [a for a in closure if self.cites(self.port_addr, a)]
        walked = [a for a in closure if self.cites(self.oracle_addr, a)]
        neither = [a for a in closure if a not in cited and a not in walked]
        kinds = collections.Counter(self.stale_kinds[a] for a in self.port_addr if a in self.stale_kinds)
        damaged = [a for a in closure if self.functions[a].damaged]
        unnamed = [a for a in closure if self.functions[a].unnamed]
        member = [o for o in self.fields if o in self.port_member]
        f_cited = [o for o in self.fields if o in self.port_off]
        no_override = [s for s, b in self.slot_bodies.items() if len(set(b.values())) == 1]
        out = self._head("NPC kernel — coverage",
                         "What the port and the oracle already cite, what nothing does, and what the "
                         "corpus cannot yet read.")
        out += [
            "| Measure | Count |", "|---|---|",
            f"| Family classes | {len(self.family)} |",
            f"| Slots | {len(self.slot_bodies)} |",
            f"| Slots with a single body across the family | {len(no_override)} |",
            f"| Closure functions | {len(closure)} |",
            f"| … cited by the port | {len(cited)} |",
            f"| … cited by the oracle | {len(walked)} |",
            f"| … cited by neither | {len(neither)} |",
            f"| … damaged decompilation | {len(damaged)} |",
            f"| … still `FUN_` | {len(unnamed)} |",
            f"| Troika fields | {len(self.fields)} |",
            f"| … cited by the port | {len(f_cited)} |",
            f"| … with a guessed port member | {len(member)} |",
            f"| Closure edges | {len(self.closure_edges)} |",
            f"| Build layers | {len(self.layers)} |",
            f"| Port-cited addresses inside a function body | {kinds['inside']} |",
            f"| Port-cited addresses that are globals / strings / vtables | {kinds['global']} / {kinds['string']} / {kinds['vtable']} |",
            f"| Stale port citations (address the corpus does not know) | {len(self.stale_port)} |",
        ]
        out += ["", "## Port citations of addresses outside the closure", "",
                "Cited by `Source/**`, present in the corpus, not reached by the walk: either the "
                "walk's boundary or a citation of something that is not the kernel's.", "",
                "| Address | Function | Where |", "|---|---|---|"]
        for addr in sorted(a for a in self.port_addr if a in self.functions and a not in self.closure):
            out.append(f"| `{_hex(addr)}` | {self.functions[addr].label} | {self._cite_cell(self.port_addr[addr])} |")
        out += ["", "## Kernel entry points cited by nothing", "",
                "Closure functions with an outside caller that neither the port nor the oracle "
                "mentions: the cross-subsystem producers stories keep discovering late.", "",
                "| Function | Outside callers |", "|---|---|"]
        for addr in sorted(self.closure):
            outside = [c for c in self.all_callers.get(addr, ()) if c not in self.closure]
            if outside and addr in neither:
                out.append(f"| {self._fn_cell(addr)} | {self._list_cell([self._fn_cell(a) for a in sorted(outside)], 4)} |")
        out += ["", "## Stale port citations", "", "| Where | Line |", "|---|---|"]
        for c in self.stale_port:
            out.append(f"| {c.path}:{c.line} | `{_short(c.text, 100)}` |")
        out += ["", "## Damaged decompilations in the closure", "",
                "Read these with `corpus asm <addr>`; their READ/WRITE and edges are partial.", "",
                "| Address | Function | Warnings |", "|---|---|---|"]
        for addr in damaged:
            fn = self.functions[addr]
            out.append(f"| `{_hex(addr)}` | {fn.label} | {'; '.join(fn.damaged)} |")
        return "\n".join(out) + "\n"

    def _render_unnamed(self) -> str:
        callers = collections.Counter()
        for _, callee, _ in self.closure_edges:
            callers[callee] += 1
        rows = []
        for addr in self.closure:
            fn = self.functions[addr]
            if fn.unnamed:
                rows.append((callers[addr] + len(fn.writes) + len(fn.reads), addr))
        rows.sort(key=lambda r: (-r[0], r[1]))
        out = self._head("NPC kernel — unnamed functions",
                         "Closure functions the image does not name and no `docs/vtmb` naming pass "
                         "has claimed, ranked by callers plus fields touched. The naming backlog: a "
                         "name here lands in `corpus names` with its evidence.")
        out += ["| Rank | Address | Slots | Callers | Writes | Reads | Strings | Port |",
                "|---|---|---|---|---|---|---|---|"]
        for rank, (_, addr) in enumerate(rows, 1):
            fn = self.functions[addr]
            out.append(f"| {rank} | `{_hex(addr)}` "
                       f"| {self._list_cell([f'`{c}#{s}`' for c, s in sorted(fn.slots)], 2)} | {callers[addr]} "
                       f"| {len(fn.writes)} | {len(fn.reads)} | {self._list_cell([_short(s) for s in fn.strings], 2)} "
                       f"| {self._cite_cell(self.port_addr.get(addr, []))} |")
        return "\n".join(out) + "\n"

    def _render_entries(self) -> str:
        rows = []
        start = self.npc_range_start()
        owned = set(self.family) | set(self.helpers)
        for addr in self.closure:
            fn = self.functions[addr]
            if fn.depth > 1 or fn.ns in BOUNDARY_CLASSES:
                continue
            if fn.ns not in owned and not self.touches_npc(fn, start):
                continue
            outside = sorted(c for c in self.all_callers.get(addr, ()) if c not in self.closure)
            if outside and len(outside) <= ENTRY_MAX_CALLERS:
                rows.append((len(outside), addr, outside))
        rows.sort(key=lambda r: (-r[0], r[1]))
        out = self._head("NPC kernel — entry points",
                         "Seeds and their direct callees (depth ≤ 1) that something outside the "
                         "closure calls directly, and that are the kernel's own: a family or helper "
                         "class method, or a body touching an offset past `CBaseCombatCharacter`'s "
                         f"layout (`{_fmt_off(start)}`). This is the kernel's surface as the rest of "
                         "the game sees it — the player's rule pass, spawners, console commands, "
                         "dialogue, scripted sequences. Bodies of the entity base classes and "
                         f"routines with more than {ENTRY_MAX_CALLERS} outside callers are left out. "
                         "Each row is a producer or consumer another subsystem's story has to name.")
        out += ["| Function | Outside callers | Who |", "|---|---|---|"]
        for count, addr, outside in rows:
            out.append(f"| {self._fn_cell(addr)} | {count} "
                       f"| {self._list_cell([self._fn_cell(a) for a in outside], 6)} |")
        return "\n".join(out) + "\n"

    def _render_index(self) -> str:
        out = self._head("NPC kernel — oracle index",
                         "Every closure function a `docs/vtmb` file mentions — by its start address "
                         "or by a site inside it — with the section that mentions it. The reverse "
                         "join lists prose addresses the corpus does not know at all.")
        out += ["| Address | Function | Sections |", "|---|---|---|"]
        for addr in sorted(self.closure):
            secs = list(self.oracle_sections.get(addr, []))
            for c in self.interior.get(addr, []):
                if c.path.startswith("docs/vtmb/"):
                    secs.extend(self.oracle_sections.get(self._addr_on_line(c), []))
            if not secs:
                continue
            uniq = sorted(set(secs))
            out.append(f"| `{_hex(addr)}` | {self.functions[addr].label} "
                       f"| {self._list_cell([f'{p} § {s}' for p, s in uniq], 4)} |")
        out += ["", "## Prose addresses the corpus does not know", "", "| Address | Where |", "|---|---|"]
        for addr in sorted(a for a in self.oracle_addr if self.locate(a)[0] == "unknown"):
            out.append(f"| `{_hex(addr)}` | {self._cite_cell(self.oracle_addr[addr])} |")
        return "\n".join(out) + "\n"

    @staticmethod
    def _addr_on_line(c: Citation) -> str:
        match = ADDRESS_RE.search(c.text)
        return match.group(1).lower() if match else ""


def _short(text: str, limit: int = 40) -> str:
    text = text.replace("\r", "").replace("\n", "⏎").replace("|", "¦")
    return f"`{text[:limit]}{'…' if len(text) > limit else ''}`"


def _tarjan(nodes: list[str], succ: dict[str, set[str]]) -> list[list[str]]:
    """Strongly connected components, iterative so a 4,000-node graph does not hit the stack."""
    index: dict[str, int] = {}
    low: dict[str, int] = {}
    on_stack: set[str] = set()
    stack: list[str] = []
    components: list[list[str]] = []
    counter = 0
    for root in nodes:
        if root in index:
            continue
        work = [(root, iter(sorted(succ.get(root, ()))))]
        index[root] = low[root] = counter
        counter += 1
        stack.append(root)
        on_stack.add(root)
        while work:
            node, children = work[-1]
            advanced = False
            for child in children:
                if child not in index:
                    index[child] = low[child] = counter
                    counter += 1
                    stack.append(child)
                    on_stack.add(child)
                    work.append((child, iter(sorted(succ.get(child, ())))))
                    advanced = True
                    break
                if child in on_stack:
                    low[node] = min(low[node], index[child])
            if advanced:
                continue
            work.pop()
            if work:
                parent = work[-1][0]
                low[parent] = min(low[parent], low[node])
            if low[node] == index[node]:
                comp = []
                while True:
                    top = stack.pop()
                    on_stack.discard(top)
                    comp.append(top)
                    if top == node:
                        break
                components.append(sorted(comp))
    return components


def build(module: str, depth: int, repo: Path) -> Ledger:
    ledger = Ledger(module, depth, repo)
    ledger.load()
    ledger.walk()
    ledger.directions()
    ledger.citations()
    ledger.order()
    return ledger


def emit(rendered: dict[str, str], out_dir: Path, check: bool) -> int:
    if check:
        failed = []
        for name, text in rendered.items():
            path = out_dir / name
            # `newline=""`: compare the bytes as written, with no universal-newline translation.
            with open(path, encoding="utf-8", newline="") if path.is_file() else open(os.devnull) as fh:
                current = fh.read() if path.is_file() else None
            if current != text:
                failed.append(name)
        if failed:
            print("kernel_ledger --check: out of date: " + ", ".join(failed))
            return 1
        print(f"kernel_ledger --check: {len(rendered)} files match {out_dir}")
        return 0
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, text in rendered.items():
        (out_dir / name).write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {len(rendered)} files to {out_dir}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--module", default=MODULE)
    parser.add_argument("--depth", type=int, default=DEFAULT_DEPTH,
                        help="how many calls past a seed the walk follows")
    parser.add_argument("--out", help="directory for the tables (default docs/vtmb/npc-kernel)")
    parser.add_argument("--check", action="store_true",
                        help="verify the committed tables match; write nothing")
    args = parser.parse_args(argv)
    repo = repo_root()
    out_dir = Path(args.out) if args.out else repo.joinpath(*DEFAULT_OUTPUT)
    ledger = build(args.module, args.depth, repo)
    rendered = ledger.render()
    print(f"family {len(ledger.family)} classes, {len(ledger.slot_bodies)} slots, "
          f"closure {len(ledger.closure)} functions, {len(ledger.closure_edges)} edges, "
          f"{len(ledger.fields)} fields, {len(ledger.layers)} layers")
    return emit(rendered, out_dir, args.check)


if __name__ == "__main__":
    sys.exit(main())
