"""The reach cut (spec 0019 story 7): what one map's NPCs can execute, as a query over the ledger.

Retail: none. Seeded from the map's population — every placed classname and every maker's
`NPCType`, resolved to a retail class through `kernel_factories.tsv` — it answers:

* the schedule texts those classes load: every text registered in the class's schedule unit and
  the units its id spaces parent on (`space.json` `parentUnit`), read from the deployed corpus
  (`Content/ElysiumCorpus/ai/schedules/`) and parsed by the pipeline's own parser;
* the task and condition identities those texts name, each task marked armed or unported against
  the port's arm table (`ElysiumTaskOps.cpp` `GTaskArms`);
* the kernel functions the map reaches: the ledger's walk re-run from the map classes' primary
  vtables, the kernel roots and the helpers every NPC owns, with a `this` dispatch fanning out to
  the map's classes only;
* the species rows: a reached body a map class holds at a slot where neither `CAI_BaseNPC` nor
  `CAI_BaseNPCTroika` holds it.

Two over-approximations, stated in every report: a class reaches every text its id-space chain
registers (selection is not simulated), and a slot-candidate edge is a candidate, as in the ledger.
The population comes from the published `<map>.entities.glb` under `exports_v2/maps/`.
"""

from __future__ import annotations

import collections
import csv
import json
import re
from dataclasses import dataclass, field
from pathlib import Path

from elysium_pipeline.formats.ai_schedule_glb.parser import parse
from elysium_pipeline.paths import export_v2_root

SCHEDULE_CORPUS = ("Content", "ElysiumCorpus", "ai", "schedules")
TASK_ARMS_SOURCE = Path("Source/ElysiumUE/Private/Substrate/ElysiumTaskOps.cpp")
TASK_ARM_RE = re.compile(r'\{ TEXT\("(\w+)"\), (0x[0-9a-fA-F]+|\d+), (\d+) \},')
MAKER_TYPE_KEY = "npctype"
ENTITY_PAIRS_KEY = "keyValues"
BASE_CLASSES = ("CAI_BaseNPC", "CAI_BaseNPCTroika")
# The slots the spec names (0019/7): PreSelectSchedule, SelectSchedule, TranslateSchedule,
# StartTask, RunTask, and the species melee / ranged selectors SelectSchedule reaches.
SCHEDULE_SLOTS = {437: "PreSelectSchedule", 438: "SelectSchedule", 440: "TranslateSchedule",
                  442: "StartTask", 444: "RunTask", 604: "SelectScheduleMeleeCombat",
                  605: "SelectScheduleRangedCombat"}
TSV_COLUMNS = ("kind", "key", "name", "classes", "verdict", "note")
REACH_DIR = "reach"


def forward_factories(path: Path) -> dict[str, str]:
    """Casefolded classname -> the one retail class its factory builds."""
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return {row["classname"].casefold(): row["retail_class"]
                for row in csv.DictReader(stream, delimiter="\t")}


@dataclass
class Population:
    """Who the map puts in the world: placed classnames and what its makers spawn, by retail class."""
    placed: collections.Counter = field(default_factory=collections.Counter)    # classname -> n
    made: collections.Counter = field(default_factory=collections.Counter)      # maker NPCType -> n
    unresolved: collections.Counter = field(default_factory=collections.Counter)  # maker NPCType only
    classes: collections.Counter = field(default_factory=collections.Counter)   # retail class -> n


def population(rows: list[dict], factories: dict[str, str]) -> Population:
    """Every entity row whose classname a factory builds, and every maker's `NPCType`.

    A placed row no factory builds is not an NPC-kernel class (brushes, props, logic) and is not
    counted; a maker naming a classname no factory builds is listed as unresolved."""
    pop = Population()
    for row in rows:
        classname = str(row.get("classname", ""))
        cls = factories.get(classname.casefold())
        if cls is None:
            continue
        pop.placed[classname] += 1
        pop.classes[cls] += 1
        if cls.startswith("CNPCMaker"):
            pairs = {str(p.get("key", "")).casefold(): str(p.get("value") or "")
                     for p in row.get(ENTITY_PAIRS_KEY) or ()}
            spawned = pairs.get(MAKER_TYPE_KEY, "")
            if not spawned:
                continue
            made = factories.get(spawned.casefold())
            if made is None:
                pop.unresolved[spawned] += 1
                continue
            pop.made[spawned] += 1
            pop.classes[made] += 1
    return pop


def entities_path(map_name: str) -> Path:
    return export_v2_root() / "maps" / f"{map_name}.entities.glb"


def inputs(repo: Path, maps: list[str]) -> list[Path]:
    """What a cut of `maps` reads beyond the ledger's own inputs: each map's published entities
    and the deployed schedule corpus (`kernel_cache.stamped` fingerprints them)."""
    if not maps:
        return []
    return [*(entities_path(m) for m in maps), repo.joinpath(*SCHEDULE_CORPUS)]


def read_map_entities(map_name: str) -> list[dict]:
    from elysium_pipeline.formats.map_entities_glb.model import MAP_ENTITIES_EXTENSION
    from elysium_pipeline.formats.unit_contract import read_glb

    path = entities_path(map_name)
    if not path.is_file():
        raise SystemExit(f"kernel_reach: no published entities for {map_name!r} ({path})")
    document, _ = read_glb(path)
    return document["extensions"][MAP_ENTITIES_EXTENSION]["entities"]


class ScheduleCorpus:
    """The deployed schedule units: which unit a class's space is, the chain it parents on, and
    each unit's texts parsed."""

    def __init__(self, root: Path):
        if not (root / "vocabulary.json").is_file():
            raise SystemExit(f"kernel_reach: no deployed schedule corpus at {root} "
                             "(`uv run elysium import ai-schedules`)")
        self.root = root
        self.units: dict[str, dict] = {}
        self.class_unit: dict[str, str] = {}
        for path in sorted(root.glob("*/space.json")):
            unit = path.parent.name
            space = json.loads(path.read_text(encoding="utf-8"))
            self.units[unit] = space
            for cls in space.get("classNames") or [space["className"]]:
                self.class_unit[cls] = unit
        vocabulary = json.loads((root / "vocabulary.json").read_text(encoding="utf-8"))
        for row in vocabulary["classes"]:
            self.class_unit.setdefault(row["className"], row["unit"])
        self._texts: dict[str, list] = {}

    def chain(self, unit: str) -> list[str]:
        """`unit` and every unit its schedule space parents on, nearest first."""
        out = []
        while unit and unit not in out:
            out.append(unit)
            unit = (self.units[unit]["spaces"].get("schedule") or {}).get("parentUnit") or ""
        return out

    def texts(self, unit: str) -> list:
        """The unit's parsed records, in registration order."""
        if unit not in self._texts:
            records = []
            for text in sorted(self.units[unit]["texts"], key=lambda t: t["order"]):
                records += parse((self.root / unit / text["file"]).read_bytes())
            self._texts[unit] = records
        return self._texts[unit]

    def task_name(self, unit: str, local_id: int) -> str | None:
        """The task name `unit`'s chain registers at `local_id`: a local id is unique down one
        chain, because a space refuses an id below its first registration."""
        for link in self.chain(unit):
            for row in self.units[link]["registrations"]["task"]:
                if row["localId"] == local_id:
                    return row["name"]
        return None


def armed_task_names(repo: Path, corpus: ScheduleCorpus) -> set[str]:
    """The task identities a port arm answers: each `GTaskArms` row's local id, named through its
    retail class's task space (the identity-level test `FElysiumTaskOpTable::BuildFromArmed` makes)."""
    text = (repo / TASK_ARMS_SOURCE).read_text(encoding="utf-8")
    armed = set()
    for cls, local, _slot in TASK_ARM_RE.findall(text):
        unit = corpus.class_unit.get(cls)
        name = corpus.task_name(unit, int(local, 0)) if unit else None
        if name:
            armed.add(name)
    return armed


@dataclass
class Reach:
    map_name: str
    pop: Population
    units: dict[str, list[str]] = field(default_factory=dict)          # class -> unit chain
    schedules: dict[str, set[str]] = field(default_factory=dict)       # text name -> classes
    tasks: dict[str, set[str]] = field(default_factory=dict)           # task name -> classes
    task_steps: collections.Counter = field(default_factory=collections.Counter)
    conditions: dict[str, set[str]] = field(default_factory=dict)      # condition -> classes
    armed: set[str] = field(default_factory=set)
    functions: dict[str, int] = field(default_factory=dict)            # addr -> depth
    holders: dict[str, set[str]] = field(default_factory=dict)         # addr -> `Cls#slot`
    species: dict[str, set[str]] = field(default_factory=dict)         # addr -> `Cls#slot`
    no_unit: list[str] = field(default_factory=list)


def compute(ledger, map_name: str, rows: list[dict], corpus: ScheduleCorpus,
            factories: dict[str, str]) -> Reach:
    pop = population(rows, factories)
    reach = Reach(map_name, pop, armed=armed_task_names(ledger.repo, corpus))
    for cls in sorted(pop.classes):
        unit = corpus.class_unit.get(cls)
        if unit is None:
            if cls in ledger.family:
                reach.no_unit.append(cls)
            continue
        reach.units[cls] = corpus.chain(unit)
        for link in reach.units[cls]:
            for record in corpus.texts(link):
                reach.schedules.setdefault(record.name, set()).add(cls)
                for task in record.tasks:
                    reach.tasks.setdefault(task.name, set()).add(cls)
                for cond in record.interrupts:
                    reach.conditions.setdefault(cond.name, set()).add(cls)
    # Steps count once per text, not once per class reaching it.
    for unit in sorted({u for chain in reach.units.values() for u in chain}):
        for record in corpus.texts(unit):
            for task in record.tasks:
                reach.task_steps[task.name] += 1

    family = [c for c in ledger.family if c in pop.classes]
    placeable = {factories[k] for k in factories}
    seeds: set[str] = set()
    for cls in family:
        seeds.update(ledger.primary_table(cls).values())
    for cls in ledger.helpers:
        # A helper the map places (a maker, a scripted sequence) seeds only when placed; one every
        # NPC owns (motor, navigator, senses) seeds whenever an NPC is on the map.
        if (cls in pop.classes) or (cls not in placeable and family):
            seeds.update(ledger.primary_table(cls).values())
    if family:
        seeds.update(a for a in _roots(ledger))
    reach.functions, _ = ledger.walk_from(seeds, family)

    base_tables = [ledger.primary_table(b) for b in BASE_CLASSES]
    for cls in sorted(set(family) | {c for c in ledger.helpers if c in pop.classes}):
        for slot, body in sorted(ledger.primary_table(cls).items()):
            if body not in reach.functions:
                continue
            reach.holders.setdefault(body, set()).add(f"{cls}#{slot}")
            if cls in family and cls not in BASE_CLASSES \
                    and all(table.get(slot) != body for table in base_tables):
                reach.species.setdefault(body, set()).add(f"{cls}#{slot}")
    return reach


def _roots(ledger) -> list[str]:
    from kernel_ledger import ROOT_FUNCTIONS
    return [a for a in ROOT_FUNCTIONS if a in ledger.functions]


def _verdict(ledger, addr: str) -> tuple[str, str]:
    row = ledger.verdicts.get(addr)
    return (row.verdict, row.target) if row else ("", "")


def _cls_cell(classes) -> str:
    return ", ".join(sorted(classes)) or "-"


def render_tsv(ledger, reach: Reach) -> str:
    rows = [TSV_COLUMNS]
    for cls, n in sorted(reach.pop.classes.items()):
        rows.append(("class", cls, "", "", "", str(n)))
    for name in sorted(reach.schedules):
        rows.append(("schedule", name, "", _cls_cell(reach.schedules[name]), "", ""))
    for name in sorted(reach.tasks):
        rows.append(("task", name, "", _cls_cell(reach.tasks[name]),
                     "armed" if name in reach.armed else "unported", str(reach.task_steps[name])))
    for name in sorted(reach.conditions):
        rows.append(("condition", name, "", _cls_cell(reach.conditions[name]), "", ""))
    for addr in sorted(reach.functions):
        fn = ledger.functions[addr]
        verdict, target = _verdict(ledger, addr)
        kind = "species" if addr in reach.species else "function"
        layer = ledger.layer_of.get(addr)
        rows.append((kind, f"0x{addr}", fn.label, _cls_cell(reach.holders.get(addr, ())),
                     verdict, "" if layer is None else f"layer {layer}"))
    return "".join("\t".join(r) + "\n" for r in rows)


def read_reached(path: Path) -> set[str]:
    """The function addresses (bare, lower-case) a committed reach TSV lists."""
    with path.open(encoding="utf-8", newline="") as stream:
        return {row["key"][2:] for row in csv.DictReader(stream, delimiter="\t")
                if row["kind"] in ("function", "species")}


def render_md(ledger, reach: Reach) -> str:
    from kernel_ledger import CORE_BANDS, VERDICT_WORDS, UNSETTLED_VERDICT

    pop = reach.pop
    tasks = sorted(reach.tasks)
    unported = [t for t in tasks if t not in reach.armed]
    core = set(ledger.core())
    out = ledger._head(
        f"NPC kernel — reach of `{reach.map_name}`",
        "Spec 0019 story 7: what this map's NPCs can execute. Seeded from the population (placed "
        "classnames and makers' `NPCType`, through `kernel_factories.tsv`). A class reaches every "
        "schedule text its id-space chain registers — selection is not simulated — and a `this` "
        "dispatch fans out to the map's classes only. Every row is in `" + reach.map_name
        + ".tsv` beside this file.")
    out += ["## Population", "", "| Retail class | Count | Schedule unit chain |", "|---|---|---|"]
    for cls, n in sorted(pop.classes.items()):
        chain = " → ".join(reach.units.get(cls, [])) or "—"
        out.append(f"| `{cls}` | {n} | {chain} |")
    out += ["", "Placed: " + ", ".join(f"`{k}` {v}" for k, v in sorted(pop.placed.items())) + ".",
            "Made by makers: " + (", ".join(f"`{k}` {v}" for k, v in sorted(pop.made.items())) or "none")
            + "."]
    if pop.unresolved:
        out.append("Maker `NPCType`s no factory builds: "
                   + ", ".join(f"`{k}` {v}" for k, v in sorted(pop.unresolved.items())) + ".")
    if reach.no_unit:
        out.append("Family classes with no schedule unit: " + ", ".join(f"`{c}`" for c in reach.no_unit) + ".")
    out += ["", "## Measure", "", "| Measure | Count |", "|---|---|",
            f"| Schedule texts | {len(reach.schedules)} |",
            f"| Task identities | {len(tasks)} |",
            f"| … armed by the port | {len(tasks) - len(unported)} |",
            f"| … unported | {len(unported)} |",
            f"| Task steps in the reached texts | {sum(reach.task_steps.values())} |",
            f"| … naming an unported identity | {sum(reach.task_steps[t] for t in unported)} |",
            f"| Condition identities | {len(reach.conditions)} |",
            f"| Functions | {len(reach.functions)} |",
            f"| … core | {len(core & set(reach.functions))} |",
            f"| Species rows | {len(reach.species)} |",
            "", "## Core functions by layer band", "",
            "| Band | Reached core | " + " | ".join(f"`{w}`" for w in VERDICT_WORDS)
            + f" | `{UNSETTLED_VERDICT}` | No verdict |",
            "|---|---|" + "---|" * (len(VERDICT_WORDS) + 2)]
    top = len(ledger.layers) - 1
    for lo, hi in CORE_BANDS:
        rows = [a for a in ledger.band_core(lo, hi) if a in reach.functions]
        words = collections.Counter(_verdict(ledger, a)[0] for a in rows)
        out.append(f"| {lo}–{min(hi, top)} | {len(rows)} | "
                   + " | ".join(str(words[w]) for w in VERDICT_WORDS)
                   + f" | {words[UNSETTLED_VERDICT]} | {words['']} |")
    out += ["", "## Schedule slots", "",
            "The bodies each map class runs at the slots the 0002 selector and task stories port.", "",
            "| Class | " + " | ".join(f"{s} {n}" for s, n in SCHEDULE_SLOTS.items()) + " |",
            "|---|" + "---|" * len(SCHEDULE_SLOTS)]
    for cls in sorted(c for c in pop.classes if c in ledger.family):
        table = ledger.primary_table(cls)
        out.append(f"| `{cls}` | " + " | ".join(
            f"`0x{table[s]}`" if s in table else "—" for s in SCHEDULE_SLOTS) + " |")
    out += ["", "## Task identities", "",
            "*Steps* counts the reached texts' steps naming the identity.", "",
            "| Task | Steps | Port | Classes |", "|---|---|---|---|"]
    for name in sorted(tasks, key=lambda t: (t in reach.armed, -reach.task_steps[t], t)):
        port = "armed" if name in reach.armed else "**unported**"
        out.append(f"| `{name}` | {reach.task_steps[name]} | {port} | {_cls_cell(reach.tasks[name])} |")
    out += ["", "## Species rows", "",
            "A reached body a map class holds where neither `CAI_BaseNPC` nor `CAI_BaseNPCTroika` does.", "",
            "| Function | Held at | Layer | Verdict | Target |", "|---|---|---|---|---|"]
    for addr in sorted(reach.species, key=lambda a: (ledger.layer_of.get(a, -1), a)):
        verdict, target = _verdict(ledger, addr)
        out.append(f"| {ledger._fn_cell(addr)} | {', '.join(sorted(reach.species[addr]))} "
                   f"| {ledger.layer_of.get(addr, '—')} | {verdict or '—'} | {target or '—'} |")
    out += ["", "## Conditions", "",
            ", ".join(f"`{c}`" for c in sorted(reach.conditions)) or "none", ""]
    return "\n".join(out)
