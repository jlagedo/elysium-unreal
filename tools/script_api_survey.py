# -*- coding: utf-8 -*-
"""Survey the **engine call surface** VtMB's scripts use, across all three script surfaces.

`python_bridge.md` establishes *how* the binding works (a datamap reflection layer, not a
109-method API). This tool answers the complementary question the port needs: **which names do
the shipped scripts actually reach for, how often, and which of them does our runtime back for
real?** It is the demand side of the ledger — the input to the Ghidra probe that recovers what
each unbacked name does in `vampire.dll`.

Reads (all under `out/`, the gitignored regenerable mirror — nothing game-sourced is written):

  * `out/scripts/**/*.py`   — 41 level scripts + `vamputil`/`fileutil`/`zvtool` (PL2)
  * `out/dlg/**/*.dlg`      — 147 dialogues; field 4 (condition, eval) + field 5 (action, exec)
  * `out/*/*.ents`          — the exported maps' output **field 6** Python payloads (PL/1.2)

Every called name is bucketed:

  engine-global   one of the 11 `vampire` module globals
  character       one of the 24 Character method-table entries
  entity-base     one of the 12 `Entity` base method-table entries
  entity-input    a name our class registry registers as an entity input (datamap dispatch)
  script          defined by a `def`/`class` somewhere in the corpus itself (vamputil, level scripts)
  stdlib          `random` / `time` / `string` / `nt` / builtins
  UNRESOLVED      none of the above — the residue this survey exists to find

and cross-checked against the runtime: `ElysiumScriptNatives.cpp`'s binding table says whether a
bound name is real or a logged stub, and `D.Input(TEXT("..."))` across the class implementations
says which entity inputs exist. The report ranks every gap by call count, so the build order is
demand-ordered rather than guessed.

Read-only. Usage: `python tools/script_api_survey.py [--json <path>] [--top N]`.
Sibling of `ent_survey.py` (entity/I-O layer) and `dlg_sheet_survey.py` (player-sheet reads).
"""
import argparse
import collections
import glob
import json
import os
import re

TOOLS = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(TOOLS, "out")
SRC = os.path.join(os.path.dirname(TOOLS), "Source", "ElysiumUE", "Private")

# --- The engine tables, verbatim from python_bridge.md (method tables read out of vampire.dll) ---

MODULE_GLOBALS = [
    "FindPlayer", "FindEntityByName", "FindEntitiesByName", "FindEntitiesByClass",
    "ScheduleTask", "SquadSeesPlayer", "CreateEntityNoSpawn", "CallEntitySpawn",
    "ChangeMap", "OneOfSet", "IsPCMalk",
]

CHARACTER_METHODS = [
    "React", "SetExpression", "SetDisposition", "SetGesture", "HasItem", "GiveItem",
    "RemoveItem", "AmmoCount", "GiveAmmo", "HasWeaponEquipped", "StartBarter", "WorldMap",
    "SewerMap", "SetQuest", "CurrentMoney", "IsMale", "SeductiveFeed", "SetCamera",
    "CalcFeat", "DialogDiscipline", "BumpStat", "GetMasqueradeLevel", "GetQuestState",
    "IsFollowerOf",
]

# 13 table records, 12 distinct names (GetCenter is duplicated); there is no SetModelName.
ENTITY_BASE = [
    "__init__", "GetOrigin", "GetAngles", "GetCenter", "GetModelName", "GetAngleVectors",
    "SetOrigin", "SetAngles", "SetModel", "SetName", "GetName", "IsAlive",
]

G_METHODS = ["ClearAll", "keys"]          # the PyDataManager method table (0x1058f5d0)
FILELIKE_METHODS = ["read", "readline"]   # the unidentified file-like type (0x1058f620)

STDLIB = set("""
    abs apply callable chr cmp coerce dir divmod eval filter float getattr hasattr hash hex id
    input int isinstance issubclass len list long map max min oct open ord pow range raw_input
    reduce repr round setattr str tuple type unichr unicode vars xrange zip
    append count extend index insert pop remove reverse sort has_key get items keys values
    update clear copy join split splitlines strip lstrip rstrip replace find rfind lower upper
    startswith endswith atoi atof randint random choice shuffle uniform seed time clock sleep
    write writelines close readlines flush seek tell truncate
    getcwd listdir mkdir stat remove rename rmdir unlink path exists isdir isfile
    dump dumps load loads
""".split())

# Receivers whose method calls are engine-bound rather than script-defined.
ENTITY_RECEIVERS = re.compile(
    r"^(pc|npc|player|ent|e|char|character|self|Find\w*|.*\.?Find\w*\(.*\))$", re.I)

COMMENT = re.compile(r"#.*$")
STRING = re.compile(r"'''.*?'''|\"\"\".*?\"\"\"|'[^'\n]*'|\"[^\"\n]*\"", re.S)
DEF = re.compile(r"^\s*(?:def|class)\s+(\w+)", re.M)
# Module-level aliasing of an engine name: `Find = __main__.FindEntityByName`. Every level script
# opens with a block of these, so an alias is by far the commonest spelling of an engine call.
ALIAS = re.compile(r"^(\w+)\s*=\s*(?:__main__\s*\.\s*)?(\w+)\s*$", re.M)
# `a.b.c(` / `a.b(` / `name(` — capture the dotted receiver (may be empty) and the called name.
CALL = re.compile(r"\b((?:\w+\s*\.\s*)*)(\w+)\s*\(")
# `a.b = ` (not ==), the attribute-write path (__setattr__ / a keyfield write)
ATTR_SET = re.compile(r"\b((?:\w+\s*\.\s*)*\w+)\s*\.\s*(\w+)\s*=(?!=)")


def strip_source(text):
    """Blank out string literals then comments, so neither contributes a phantom call."""
    text = STRING.sub(lambda m: " " * len(m.group(0)), text)
    return COMMENT.sub("", text)


def receiver_of(dotted):
    """'__main__ . G .' -> ('__main__.G', 'G'); '' -> ('', '')."""
    r = re.sub(r"\s+", "", dotted).rstrip(".")
    return (r, r.rsplit(".", 1)[-1]) if r else ("", "")


class Survey(object):
    def __init__(self):
        self.calls = collections.Counter()            # (receiver_tail, name) -> count
        self.surfaces = collections.defaultdict(collections.Counter)   # name -> surface -> n
        self.sites = collections.defaultdict(list)    # name -> [where]
        self.attr_set = collections.Counter()         # (receiver_tail, attr) -> count
        self.defined = set()                          # every def/class in the corpus
        self.aliases = {}                             # alias -> engine/script name it stands for
        self.g_flags = collections.Counter()

    def resolve(self, name, seen=None):
        """Follow module-level aliases to the name that actually names the binding."""
        seen = seen or set()
        while name in self.aliases and name not in seen:
            seen.add(name)
            name = self.aliases[name]
        return name

    def scan(self, text, surface, where):
        body = strip_source(text)
        for dotted, name in CALL.findall(body):
            full, tail = receiver_of(dotted)
            if name in ("if", "while", "for", "print", "return", "not", "and", "or", "in",
                        "elif", "else", "def", "class", "import", "del", "assert"):
                continue
            self.calls[(tail, name)] += 1
            self.surfaces[name][surface] += 1
            if len(self.sites[name]) < 6:
                self.sites[name].append("%s [%s]" % (where, full or "<bare>"))
        for dotted, attr in ATTR_SET.findall(body):
            _, tail = receiver_of(dotted + ".")
            self.attr_set[(tail, attr)] += 1
            if tail == "G":
                self.g_flags[attr] += 1


def load_py(s):
    """`scripts/lib/` is the shipped CPython 2.1 stdlib (pickle, string, random, ...) — it is not
    game script and its calls are not engine calls, so it is excluded. `zvtool/` is Troika's own
    in-game dev tool: shipped, so it is surveyed, but tagged separately from game logic."""
    files = [p for p in sorted(glob.glob(os.path.join(OUT, "scripts", "**", "*.py"), recursive=True))
             if os.sep + "lib" + os.sep not in p]
    for p in files:
        text = open(p, "r", errors="replace").read()
        s.defined.update(DEF.findall(text))
        for alias, target in ALIAS.findall(strip_source(text)):
            if alias != target:
                s.aliases.setdefault(alias, target)
    for p in files:
        rel = os.path.relpath(p, OUT).replace("\\", "/")
        surface = "zvtool" if "/zvtool" in rel else "py"
        s.scan(open(p, "r", errors="replace").read(), surface, rel)
    return len(files)


def load_dlg(s):
    files = sorted(glob.glob(os.path.join(OUT, "dlg", "**", "*.dlg"), recursive=True))
    rows = 0
    for p in files:
        rel = os.path.relpath(p, OUT).replace("\\", "/")
        for line in open(p, "r", encoding="latin-1", errors="replace"):
            seg = re.findall(r"\{(.*?)\}", line)
            if len(seg) < 6:
                continue
            rows += 1
            # col-4 is a *gate* on a PC line and an *action* on an NPC line (resolved by data,
            # decisions.md 2026-07-24), so it is labelled by column, not by role.
            for idx, surface in ((4, "dlg4"), (5, "dlg5")):
                snippet = seg[idx].strip()
                if snippet and snippet != "#":
                    s.scan(snippet, surface, rel)
    return len(files), rows


def load_ents(s):
    """Output field 6 is the Python payload (VtMB extends Source's 5 comma fields to 7)."""
    files = sorted(glob.glob(os.path.join(OUT, "*", "*.ents")))
    payloads = 0
    for p in files:
        rel = os.path.basename(p)
        for line in open(p, "r", errors="replace"):
            if "," not in line:
                continue
            parts = line.rstrip("\n").split(",")
            if len(parts) < 7:
                continue
            payload = parts[5].strip()
            if payload:
                payloads += 1
                s.scan(payload, "ents", rel)
    return len(files), payloads


def runtime_natives():
    """What the runtime binds today -> {name: 'real'|'stub'}.

    Two sources: `ElysiumScriptNatives.cpp`'s shared binding table (the 11 globals + 24 Character
    methods, whose own note text says whether each is backed or a logged stub), and the CPython
    host's real `PyMethodDef` tables in `ElysiumPythonEntity.cpp` (the `Entity` base methods and
    the module globals, which are implemented C functions rather than table entries).
    """
    out = {}
    try:
        text = open(os.path.join(SRC, "ElysiumScriptNatives.cpp"), "r", errors="replace").read()
    except OSError:
        text = ""
    for name, _method, note in re.findall(
            r'\{\s*TEXT\("(\w+)"\),\s*(true|false),\s*TEXT\("([^"]*)"\)', text):
        out[name] = "stub" if note.startswith("stub") else "real"

    try:
        py = open(os.path.join(SRC, "ElysiumPythonEntity.cpp"), "r", errors="replace").read()
    except OSError:
        return out
    for table in ("GEntityMethods", "GModuleGlobals"):
        block = re.search(re.escape(table) + r"\[\]\s*=\s*\{(.*?)\n\t\};", py, re.S)
        if not block:
            continue
        for name in re.findall(r'\{\s*"(\w+)"\s*,', block.group(1)):
            out.setdefault(name, "real")
    return out


def runtime_inputs():
    """Every entity input the class registry registers: D.Input(TEXT("Name")."""
    names = set()
    for p in glob.glob(os.path.join(SRC, "*.cpp")):
        text = open(p, "r", errors="replace").read()
        names.update(re.findall(r'\bD\.Input\(\s*TEXT\("(\w+)"\)', text))
        names.update(re.findall(r'\bInput\(\s*TEXT\("(\w+)"\)', text))
    return names


def classify(name, receivers, survey, natives, inputs):
    name = survey.resolve(name)
    if name in MODULE_GLOBALS:
        return "engine-global"
    if name in CHARACTER_METHODS:
        return "character"
    if name in ENTITY_BASE:
        return "entity-base"
    if name in G_METHODS and "G" in receivers:
        return "G-method"
    if name in inputs:
        return "entity-input"
    if name in survey.defined:
        return "script"
    if name in STDLIB or name in FILELIKE_METHODS:
        return "stdlib"
    return "UNRESOLVED"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--json", help="write the full ranked ledger here")
    ap.add_argument("--top", type=int, default=40, help="rows per section (default 40)")
    args = ap.parse_args()

    s = Survey()
    n_py = load_py(s)
    n_dlg, n_rows = load_dlg(s)
    n_ents, n_payloads = load_ents(s)

    natives = runtime_natives()
    inputs = runtime_inputs()

    # Fold (receiver, spelling) -> the resolved name, so `Find`/`FindList` count against the
    # engine global they alias. Keep the spellings and the receiver set for the report.
    totals = collections.Counter()
    receivers = collections.defaultdict(collections.Counter)
    spellings = collections.defaultdict(collections.Counter)
    surfaces = collections.defaultdict(collections.Counter)
    for (recv, spelling), n in s.calls.items():
        name = s.resolve(spelling)
        totals[name] += n
        receivers[name][recv or "<bare>"] += n
        spellings[name][spelling] += n
    # Surface counts are per *spelling*, so fold them once per spelling, not once per receiver.
    for spelling, per_surface in s.surfaces.items():
        name = s.resolve(spelling)
        for surf, m in per_surface.items():
            surfaces[name][surf] += m

    ledger = []
    for name, n in totals.most_common():
        kind = classify(name, receivers[name], s, natives, inputs)
        ledger.append({
            "name": name,
            "calls": n,
            "kind": kind,
            "backing": natives.get(name, "-"),
            "surfaces": dict(surfaces[name]),
            "spellings": dict(spellings[name].most_common(4)),
            "receivers": dict(receivers[name].most_common(4)),
            "sites": s.sites[name],
        })

    print("=" * 78)
    print("VtMB script -> engine call surface")
    print("=" * 78)
    print("corpus: %d .py  |  %d .dlg (%d rows)  |  %d .ents (%d field-6 payloads)"
          % (n_py, n_dlg, n_rows, n_ents, n_payloads))
    print("runtime: %d natives bound (%d real / %d stub), %d entity inputs registered"
          % (len(natives), sum(1 for v in natives.values() if v == "real"),
             sum(1 for v in natives.values() if v == "stub"), len(inputs)))
    print("distinct called names: %d   total call sites: %d"
          % (len(totals), sum(totals.values())))
    print()

    by_kind = collections.Counter(e["kind"] for e in ledger)
    calls_by_kind = collections.Counter()
    for e in ledger:
        calls_by_kind[e["kind"]] += e["calls"]
    print("%-16s %8s %10s" % ("kind", "names", "calls"))
    print("-" * 36)
    for kind, cnt in by_kind.most_common():
        print("%-16s %8d %10d" % (kind, cnt, calls_by_kind[kind]))
    print()

    def section(title, rows, limit):
        print("-" * 78)
        print(title)
        print("-" * 78)
        print("%-28s %7s %-14s %-8s %s" % ("name", "calls", "kind", "backing", "surfaces"))
        for e in rows[:limit]:
            surf = " ".join("%s:%d" % (k, v) for k, v in sorted(e["surfaces"].items()))
            print("%-28s %7d %-14s %-8s %s"
                  % (e["name"], e["calls"], e["kind"], e["backing"], surf))
        if len(rows) > limit:
            print("... %d more" % (len(rows) - limit))
        print()

    # 1. The bound engine surface, by whether the runtime backs it for real.
    engine = [e for e in ledger if e["kind"] in ("engine-global", "character", "entity-base")]
    section("ENGINE API — the method tables, demand-ranked", engine, args.top)

    stubbed = [e for e in engine if e["backing"] == "stub"]
    section("ENGINE API — bound but STUBBED (the build-order queue)", stubbed, args.top)

    unbound = [e for e in engine if e["backing"] == "-"]
    section("ENGINE API — in the tables, NOT in the runtime binding table", unbound, args.top)

    # 2. The residue: what the scripts call that nothing here explains -> Ghidra targets.
    unresolved = [e for e in ledger if e["kind"] == "UNRESOLVED"]
    section("UNRESOLVED — Ghidra probe targets, demand-ranked", unresolved, args.top)

    # 3. Entity inputs the scripts fire that our registry does not have.
    print("-" * 78)
    print("ENTITY INPUTS fired from script but not registered (candidate datamap inputs)")
    print("-" * 78)
    cand = [e for e in unresolved
            if any(ENTITY_RECEIVERS.match(r) for r in e["receivers"] if r != "<bare>")]
    for e in cand[:args.top]:
        print("%-28s %7d  recv=%s" % (e["name"], e["calls"],
                                      ",".join(list(e["receivers"])[:3])))
    print("... %d candidates total" % len(cand))
    print()

    print("-" * 78)
    print("ATTRIBUTE WRITES — the __setattr__ path (keyfield / property-bag writes)")
    print("-" * 78)
    for (recv, attr), n in s.attr_set.most_common(args.top):
        if recv == "G":
            continue
        print("%-20s . %-26s %6d" % (recv, attr, n))
    print()
    print("G flags written: %d distinct, %d writes"
          % (len(s.g_flags), sum(s.g_flags.values())))

    if args.json:
        with open(args.json, "w") as f:
            json.dump({
                "corpus": {"py": n_py, "dlg": n_dlg, "dlg_rows": n_rows,
                           "ents": n_ents, "payloads": n_payloads},
                "ledger": ledger,
                "attr_writes": [{"recv": r, "attr": a, "n": n}
                                for (r, a), n in s.attr_set.most_common()],
                "g_flags": s.g_flags.most_common(),
            }, f, indent=1)
        print("\nwrote %s" % args.json)


if __name__ == "__main__":
    main()
