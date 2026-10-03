"""The reach cut (0019 story 7) on hand-built inputs: population, the schedule-unit chain, the arm
table's naming, and the walk's fan-out restricted to the map's classes."""

from __future__ import annotations

import collections
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import kernel_ledger as kl  # noqa: E402
import kernel_reach as kr  # noqa: E402

FACTORIES = {"npc_vcop": "CNPC_VCop", "npc_vrat": "CNPC_VRat", "npc_maker": "CNPCMaker"}


def _row(classname: str, **pairs: str) -> dict:
    return {"classname": classname, "keyValues": [{"key": k, "value": v} for k, v in pairs.items()]}


def test_population_expands_makers_and_ignores_non_npc_rows():
    rows = [_row("npc_VCop"), _row("worldspawn"), _row("npc_maker", NPCType="npc_VRat"),
            _row("npc_maker", npctype="npc_VRat"), _row("npc_maker", NPCType="npc_Nothing"),
            _row("npc_maker")]
    pop = kr.population(rows, FACTORIES)
    assert pop.placed == collections.Counter({"npc_maker": 4, "npc_VCop": 1})
    assert pop.made == collections.Counter({"npc_VRat": 2})
    assert pop.unresolved == collections.Counter({"npc_Nothing": 1})
    assert pop.classes == collections.Counter({"CNPCMaker": 4, "CNPC_VRat": 2, "CNPC_VCop": 1})


def _unit(root: Path, unit: str, cls: str, parent: str, texts: dict[str, str], tasks=()) -> None:
    folder = root / unit
    folder.mkdir(parents=True)
    rows = []
    for order, (name, body) in enumerate(texts.items()):
        (folder / f"{name.lower()}.sch").write_text(body, encoding="utf-8")
        rows.append({"order": order, "name": name, "file": f"{name.lower()}.sch"})
    space = {"className": cls, "classNames": [cls], "texts": rows,
             "spaces": {"schedule": {"parentUnit": parent}},
             "registrations": {"task": [{"name": n, "localId": i} for n, i in tasks]}}
    (folder / "space.json").write_text(json.dumps(space), encoding="utf-8")


def _corpus(root: Path) -> kr.ScheduleCorpus:
    _unit(root, "cai_basenpc", "CAI_BaseNPC", "",
          {"SCHED_IDLE": "Schedule SCHED_IDLE Tasks TASK_WAIT 1 Interrupts COND_NEW_ENEMY"},
          tasks=[("TASK_WAIT", 1)])
    _unit(root, "cnpc_vhuman", "CNPC_VHuman", "cai_basenpc",
          {"SCHED_VHUMAN_X": "Schedule SCHED_VHUMAN_X Tasks TASK_WAIT 2 TASK_VHUMAN_Y 0"},
          tasks=[("TASK_VHUMAN_Y", 300)])
    (root / "vocabulary.json").write_text(json.dumps(
        {"classes": [{"className": "CNPC_VCop", "unit": "cnpc_vhuman"}]}), encoding="utf-8")
    return kr.ScheduleCorpus(root)


def test_schedule_chain_and_task_names(tmp_path):
    corpus = _corpus(tmp_path)
    assert corpus.class_unit["CNPC_VCop"] == "cnpc_vhuman"
    assert corpus.chain("cnpc_vhuman") == ["cnpc_vhuman", "cai_basenpc"]
    assert [r.name for r in corpus.texts("cnpc_vhuman")] == ["SCHED_VHUMAN_X"]
    # A local id resolves down the chain: the species space first, then its parents.
    assert corpus.task_name("cnpc_vhuman", 300) == "TASK_VHUMAN_Y"
    assert corpus.task_name("cnpc_vhuman", 1) == "TASK_WAIT"
    assert corpus.task_name("cai_basenpc", 300) is None


def test_armed_names_read_from_the_arm_table(tmp_path):
    corpus = _corpus(tmp_path / "corpus")
    source = tmp_path / kr.TASK_ARMS_SOURCE
    source.parent.mkdir(parents=True)
    source.write_text('{ TEXT("CAI_BaseNPC"), 0x1, 442 },\n{ TEXT("CNPC_VHuman"), 999, 444 },\n',
                      encoding="utf-8")
    assert kr.armed_task_names(tmp_path, corpus) == {"TASK_WAIT"}


def _ledger() -> kl.Ledger:
    """A ledger holding only what `walk_from` reads: a root dispatching slot 7 through `this`."""
    ledger = object.__new__(kl.Ledger)
    ledger.depth = 6
    ledger.functions = {a: kl.Function(a, a, ns, 1, False, "", "")
                        for a, ns in (("root", "CAI_BaseNPC"), ("cop7", "CNPC_VCop"),
                                      ("rat7", "CNPC_VRat"), ("leaf", "CNPC_VRat"))}
    ledger.edges = {"rat7": {("leaf", "direct")}}
    ledger.this_dispatch = {"root": {7}}
    ledger.slot_bodies = {7: {"CNPC_VCop": "cop7", "CNPC_VRat": "rat7"}}
    return ledger


def test_walk_fans_out_to_the_map_classes_only():
    ledger = _ledger()
    whole, _ = ledger.walk_from({"root"}, ["CNPC_VCop", "CNPC_VRat"])
    assert set(whole) == {"root", "cop7", "rat7", "leaf"}
    cut, edges = ledger.walk_from({"root"}, ["CNPC_VCop"])
    assert set(cut) == {"root", "cop7"}
    assert edges == {("root", "cop7", "slot-candidate")}


def test_tsv_round_trips_the_function_set(tmp_path):
    path = tmp_path / "m.tsv"
    path.write_text("\t".join(kr.TSV_COLUMNS) + "\n"
                    "task\tTASK_WAIT\t\tCNPC_VCop\tarmed\t1\n"
                    "function\t0x10292de0\tNPCThink\t-\trule\t\n"
                    "species\t0x10372150\tTranslateSchedule\tCNPC_VCop#440\trule\t\n", encoding="utf-8")
    assert kr.read_reached(path) == {"10292de0", "10372150"}
