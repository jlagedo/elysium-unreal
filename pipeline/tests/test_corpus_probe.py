"""The acceptance probe: every `vtmb_*` tool on `CAI_BaseNPC`, `StartTask` and `0x1028a380`, and
`where` on five addresses, against the real corpus and this checkout -- each under 10 s and, by
default, under 20 KB.

Opt in with `pytest -m corpus -s` to see the table (tool, arguments, seconds, reply characters).
Skipped when the corpus has not been built on this machine.
"""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))
sys.path.insert(0, str(REPO / "research" / "tooling" / "lookup"))

import corpus  # noqa: E402
import corpus_mcp  # noqa: E402

pytestmark = pytest.mark.corpus

SUBJECTS = ("CAI_BaseNPC", "StartTask", "0x1028a380")
WHERE = ("0x1028a380", "0x102827f0", "0x10273390", "0x102a0940", "CAI_BaseNPC::SelectSchedule")
BUDGET_SECONDS = 10.0


def _probes() -> list[tuple[str, dict]]:
    """One call per tool per subject, each in the form the tool takes it."""
    probes: list[tuple[str, dict]] = []
    for subject in SUBJECTS:
        probes += [("vtmb_func", {"reference": subject}), ("vtmb_code", {"reference": subject}),
                   ("vtmb_asm", {"reference": subject}), ("vtmb_callers", {"reference": subject}),
                   ("vtmb_callees", {"reference": subject}), ("vtmb_twin", {"reference": subject}),
                   ("vtmb_grep", {"pattern": subject}), ("vtmb_string", {"text": subject}),
                   ("vtmb_globals", {"text": subject})]
    probes += [("vtmb_vtable", {"cls": "CAI_BaseNPC"}), ("vtmb_fields", {"cls": "CAI_BaseNPC"}),
               ("vtmb_closure", {"cls": "CAI_BaseNPC"}), ("vtmb_slot", {"slot": 442}),
               ("vtmb_slot", {"slot": 1}), ("vtmb_readers", {"offset": "0x5cc0"}),
               ("vtmb_readers", {"offset": "0x5cc0", "cls": "CAI_BaseNPC"}), ("vtmb_iface", {}),
               ("vtmb_stat", {}),
               ("vtmb_grep", {"pattern": "StartTask|RunTask"}),
               ("vtmb_code", {"reference": "0x103692c0"})]
    probes += [("vtmb_where", {"query": query}) for query in WHERE]
    return probes


@pytest.fixture(scope="module")
def table():
    if not corpus._database().is_file():
        pytest.skip(f"no corpus at {corpus._database()}")
    rows = []
    for tool, arguments in _probes():
        started = time.perf_counter()
        text, failed = corpus_mcp.run_tool(tool, arguments)
        rows.append((tool, arguments, time.perf_counter() - started, len(text), failed, text))
    print("\n%-14s %-48s %8s %9s" % ("tool", "arguments", "seconds", "chars"))
    for tool, arguments, seconds, chars, failed, _ in rows:
        print("%-14s %-48s %8.3f %9d%s" % (tool, json.dumps(arguments)[:48], seconds, chars,
                                            "  ERROR" if failed else ""))
    return rows


def test_every_probe_answers_inside_the_budget(table):
    slow = [(tool, arguments, seconds) for tool, arguments, seconds, *_ in table
            if seconds >= BUDGET_SECONDS]
    assert not slow, slow


def test_no_default_reply_is_over_20_kb(table):
    big = [(tool, arguments, chars) for tool, arguments, _, chars, *_ in table
           if chars > corpus.REPLY_LIMIT]
    assert not big, big


def test_a_probe_that_found_nothing_did_not_fail(table):
    # `where` and the corpus tools answer a miss in words; an error here is a broken tool.
    broken = [(tool, arguments, text[:120]) for tool, arguments, _, _, failed, text in table if failed]
    assert not broken, broken


def test_where_answers_in_under_a_second_once_the_index_is_built(table):
    # The first `where` of a run may build the index; the rest are answers from it.
    answers = [seconds for tool, _, seconds, *_ in table if tool == "vtmb_where"]
    assert len(answers) == len(WHERE) and max(answers[1:]) < 1.0, answers


def test_a_cold_rebuild_of_this_checkout_is_seconds(tmp_path, monkeypatch):
    """The bound the index is built to: a cold rebuild under 10 s, an answer under 1 s."""
    import addr_index
    monkeypatch.setenv("ELYSIUM_WORK_ROOT", str(tmp_path / "work"))
    monkeypatch.setattr(addr_index, "_CURRENT", None)
    started = time.perf_counter()
    index = addr_index.ensure_index(REPO, tmp_path / "cold")
    cold = time.perf_counter() - started
    started = time.perf_counter()
    answer = addr_index.render(index, addr_index.plan(index, "0x1028a380"))
    warm = time.perf_counter() - started
    index.close()
    print(f"\ncold rebuild {cold:.2f} s; one answer {warm:.3f} s")
    assert cold < 10 and warm < 1 and answer.startswith("== 0x1028a380")
