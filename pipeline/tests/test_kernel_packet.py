"""The reading side of pass R on fixtures: walk sections and rows, the row diff, the merge, the
packet and its check. The corpus and the readers are never touched; the walks are pinned text in
the shape the briefs ask for, and the family / skeleton / verdict / checklist files are written
into a temporary work root.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
from pathlib import Path

import pytest

os.environ.setdefault("ELYSIUM_WORK_ROOT", tempfile.gettempdir())
os.environ.setdefault("ELYSIUM_VTMB_ROOT", tempfile.gettempdir())

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import kernel_packet as kp  # noqa: E402

WALK_A = """\
## 0x10271900 CAI_BaseNPC::UpdateEnemyPos

**Branches**

| branch | what is compared with what | taken means | not-taken means |
|---|---|---|---|
| 0x10271914 | navigator +0x18 m_navType == 3 (integer) | return | test 1 |
| 0x1027191d | m_navType == 1 | return | ask the goal type |
| 0x10271a66 | float yaw delta vs [0x10497c80] 12.0, > | invalidate the route | return |

**Arguments**

| call | receiver (ECX) | stack args in order | return used as |
|---|---|---|---|
| 0x10271923 | navigator | none | goal type, compared with 2 (0x10271928) |
| 0x1027199a | this | none | the enemy pointer |

**Effect**

Re-points the goal at the enemy or refreshes the chase point. Returns void.

**Unrecovered**

none

## 0x10271b10 CAI_BaseNPC::UpdateTargetPos

Branches: none

Arguments: none

Effect: chains to nothing.

Unrecovered: none
"""

WALK_B = """\
## 0x10271900 CAI_BaseNPC::UpdateEnemyPos

Branches:
| branch | what is compared with what | taken means | not-taken means |
|---|---|---|---|
| 0x10271914 | navigator +0x18 m_navType == 3 (integer, 0x10271911) | return | test 1 |
| 0x1027191d | m_navType != 1 | ask the goal type | return |
| 0x10271a66 | yaw delta vs 12.0, > (float) | invalidate the route (0x10007b4e) | return |

Arguments:
| call | receiver (ECX) | stack args in order | return used as |
|---|---|---|---|
| 0x10271923 | navigator (this+0x5d34) | none | goal type |
| 0x1027199a | this | none | GetEnemy() result |

Effect: the same.
Unrecovered: 0x10271a36 the 80.0 threshold's unit.

## 0x10271b10 CAI_BaseNPC::UpdateTargetPos

Branches: none
Arguments: none
Effect: nothing.
Unrecovered: none
"""

DRILL = """\
### Settled branches
| branch | what is compared with what | taken means | not-taken means | settlement |
|---|---|---|---|---|
| 0x1027191d | m_navType == 1 (JZ after CMP EAX,1) | return | ask the goal type | A; 1027191a CMP EAX,0x1 / 1027191d JZ 0x10271a93 |

### Settled calls
| call | receiver (ECX) | stack args in order | return used as | settlement |
|---|---|---|---|---|
"""


def test_sections_key_by_address_and_join_chunks():
    text = "## 0x102a1910 X — chunk 1\n\nA\n\n## 0x102a1910 X — chunk 2\n\nB\n\n## 0x10271900 Y\n\nC\n"
    s = kp.sections(text)
    assert set(s) == {"102a1910", "10271900"}
    assert "A" in s["102a1910"] and "B" in s["102a1910"]


def test_rows_read_branch_and_call_tables_under_either_label():
    a, b = kp.sections(WALK_A)["10271900"], kp.sections(WALK_B)["10271900"]
    assert list(kp.rows(a, "branch")) == ["0x10271914", "0x1027191d", "0x10271a66"]
    assert list(kp.rows(b, "call")) == ["0x10271923", "0x1027199a"]
    assert kp.rows(a, "call")["0x1027199a"][1] == "this"


def test_judge_finds_the_sense_conflict_and_leaves_prose_alone():
    a, b = kp.sections(WALK_A)["10271900"], kp.sections(WALK_B)["10271900"]
    ra, rb = kp.rows(a, "branch"), kp.rows(b, "branch")
    assert kp.judge(ra["0x10271914"], rb["0x10271914"], "branch")[0] == "AGREE"
    assert kp.judge(ra["0x1027191d"], rb["0x1027191d"], "branch")[0] == "CONFLICT"
    # B cites an address in its consequence cell; a citation is not a literal disagreement.
    assert kp.judge(ra["0x10271a66"], rb["0x10271a66"], "branch")[0] in ("AGREE", "DEPTH")
    ca, cb = kp.rows(a, "call"), kp.rows(b, "call")
    assert kp.judge(ca["0x10271923"], cb["0x10271923"], "call")[0] != "CONFLICT"


def test_diff_rows_tally():
    A, B = kp.sections(WALK_A), kp.sections(WALK_B)
    verdicts = [v for _, _, _, v, *_ in kp.diff_rows(["0x10271900", "0x10271b10"], A, B)]
    assert verdicts.count("CONFLICT") == 1 and "MISSING" not in verdicts


def test_merge_row_takes_the_drill_on_a_conflict():
    settled = kp._settled({"d": DRILL})
    assert ("branch", "0x1027191d") in settled
    ra = ["0x1027191d", "m_navType == 1", "return", "ask the goal type"]
    rb = ["0x1027191d", "m_navType != 1", "ask the goal type", "return"]
    row = kp.merge_row("branch", "0x1027191d", "CONFLICT", ra, rb, settled, "luna", "Sol")
    assert row.startswith("| 0x1027191d | m_navType == 1 (JZ after CMP EAX,1) | return | ask the goal type | drill: A;")
    assert "luna: m_navType == 1" in row and "Sol: m_navType != 1" in row
    unsettled = kp.merge_row("branch", "0x1027191d", "CONFLICT", ra, rb, {}, "luna", "Sol")
    assert "CONFLICT unsettled" in unsettled
    one = kp.merge_row("call", "0x1", "ONE-SIDED", None, ["0x1", "this", "none", "ignored"], {}, "luna", "Sol")
    assert one.endswith("| Sol only |")


@pytest.fixture
def work_root(tmp_path, monkeypatch):
    """A family with two rule rows and one dead row, its skeleton, verdicts and checklist."""
    fams = tmp_path / "research" / "npc-kernel-checklist" / "families-19-29"
    skel = tmp_path / "research" / "npc-kernel-checklist" / "skeletons-19-29"
    fams.mkdir(parents=True)
    skel.mkdir(parents=True)
    (fams / "Fix19.tsv").write_text("10271900\trule\t19-29\tT\tE\n10271b10\trule\t19-29\tT\tE\n"
                                    "10271d10\tdead\t19-29\t-\tE\n", encoding="utf-8")
    sk = [{"addr": "0x10271900", "label": "CAI_BaseNPC::UpdateEnemyPos", "class": "CAI_BaseNPC", "size": 410,
           "instructions": 90, "damaged": "",
           "arms": [{"at": "0x10271914", "op": "JZ", "taken": "0x10271a93", "next": "0x1027191a"},
                    {"at": "0x1027191d", "op": "JZ", "taken": "0x10271a93", "next": "0x10271923"},
                    {"at": "0x10271a66", "op": "JNZ", "taken": "0x10271a93", "next": "0x10271a68"}],
           "rets": ["0x10271a99"], "tables": [], "reads": [], "writes": [], "globals": [],
           "calls": [{"at": "0x10271923", "kind": "direct", "target": "0x102ee620", "what": "0x100037e2 (thunk) → 0x102ee620 GetGoalType"},
                     {"at": "0x1027199a", "kind": "virtual", "slot": 167, "receiver": "this", "what": "virtual slot 167 on this"}]},
          {"addr": "0x10271b10", "label": "CAI_BaseNPC::UpdateTargetPos", "class": "CAI_BaseNPC", "size": 40,
           "instructions": 9, "damaged": "", "arms": [], "rets": ["0x10271b30"], "tables": [], "reads": [],
           "writes": [], "globals": [], "calls": []},
          {"addr": "0x10271d10", "label": "CAI_BaseNPC::CheckTarget", "class": "CAI_BaseNPC", "size": 418,
           "instructions": 90, "damaged": "", "arms": [{"at": "0x10271d20", "op": "JZ", "taken": "0x1", "next": "0x2"}],
           "rets": [], "tables": [], "reads": [], "writes": [], "globals": [], "calls": []}]
    (skel / "Fix19.json").write_text(json.dumps(sk), encoding="utf-8")
    (skel / "Fix19.md").write_text("# Skeletons — Fix19\n\n" + "\n".join(
        f"## {s['addr']} {s['label']}\n\nclass `{s['class']}`\n\n### Arm inventory\n\n(arms)\n" for s in sk),
        encoding="utf-8")
    verdicts = tmp_path / "kernel_verdicts.tsv"
    verdicts.write_text("address\tverdict\tband\ttarget\tevidence\n"
                        "10271900\trule\t19-29\tFElysiumNpc::UpdateEnemyPos\t[0019/1 obs=timing: the chase point]\n"
                        "10271b10\trule\t19-29\tFElysiumNpc::UpdateTargetPos\t[0019/1 obs=timing: the target lane]\n"
                        "10271d10\tdead\t19-29\t-\t[0019/1 dead=nothing observes it]\n", encoding="utf-8")
    monkeypatch.setattr(kp, "research_root", lambda: tmp_path / "research")
    real = kp.verdicts
    monkeypatch.setattr(kp, "verdicts", lambda path=None: real(verdicts))
    monkeypatch.setattr(kp, "checklist", lambda path=None: {})
    return tmp_path


def _verdicts(path):
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines()[1:]:
        c = line.split("\t")
        out[c[0]] = (c[1], c[3], c[4])
    return out


def test_packet_holds_every_rule_row_and_skips_the_dead_one(work_root):
    A, B = kp.sections(WALK_A), kp.sections(WALK_B)
    text, stats = kp.build_packet("Fix19", A, B, {"d": DRILL}, "", "A=luna, B=Sol")
    assert stats["rule"] == 2 and stats["skipped"] == 1
    assert stats["CONFLICT"] == 1 and stats["drilled"] == 1 and stats["unsettled"] == 0
    assert "## 0x10271900 CAI_BaseNPC::UpdateEnemyPos" in text
    assert "## 0x10271b10" in text and "## 0x10271d10" not in text
    assert "| `0x10271d10` | CAI_BaseNPC::CheckTarget | 418 | `dead` | no — dead |" in text
    assert "**Verdict (checklist-19-29, verbatim):** `rule` · FElysiumNpc::UpdateEnemyPos · [0019/1 obs=timing: the chase point]" in text
    assert "| 0x1027191d | m_navType == 1 (JZ after CMP EAX,1) | return | ask the goal type | drill:" in text
    assert "- Sol: 0x10271a36 the 80.0 threshold's unit." in text
    out = kp.families_dir() / "Fix19-READING.md"
    out.write_text(text, encoding="utf-8")
    assert kp.check_packet("Fix19") == []


def test_check_packet_refuses_a_missing_row_and_a_missing_arm(work_root):
    A, B = kp.sections(WALK_A), kp.sections(WALK_B)
    text, _ = kp.build_packet("Fix19", A, B, {}, "", "A=luna, B=Sol")
    out = kp.families_dir() / "Fix19-READING.md"
    out.write_text(text.replace("| 0x10271a66 |", "| 0x10271a67 |"), encoding="utf-8")
    problems = kp.check_packet("Fix19")
    assert any("0x10271900: 1 arms without a branch row: 0x10271a66" in p for p in problems)
    out.write_text(text.split("## 0x10271b10")[0], encoding="utf-8")
    problems = kp.check_packet("Fix19")
    assert any("0x10271b10: no section" in p for p in problems)


def test_packet_keeps_a_one_reader_row_verbatim(work_root):
    A, B = kp.sections(WALK_A), {}
    text, stats = kp.build_packet("Fix19", A, B, {}, "", "A=luna, B=Sol")
    assert stats["missing_b"] == 2
    assert "Only one reader covered this row (luna)" in text
    (kp.families_dir() / "Fix19-READING.md").write_text(text, encoding="utf-8")
    assert kp.check_packet("Fix19") == []
