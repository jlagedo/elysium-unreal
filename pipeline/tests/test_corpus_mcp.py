"""The corpus server's bounds: the 60 s stop, the journal, and the default reply caps.

A synthetic corpus -- built here from the corpus's own schema, with the same trigram search index
`corpus build` makes -- stands in for the real one, which is a 342 MB database no test can fixture.
What is pinned is behaviour a caller can see: a query that outruns its deadline answers with
`TIMEOUT after N s: ... narrow it with <parameter>` and the server stays usable; every call is
journaled; a default reply is at most 20,000 characters and each cut names the parameter that gets
the rest; and the search index is only ever trusted to narrow a `grep` when it provably does not
drop a match.
"""

from __future__ import annotations

import re
import sqlite3
import sys
import time
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import corpus  # noqa: E402
import corpus_mcp  # noqa: E402

MODULE = "vampire.dll"
BODY_LINES = 2000
SLOTS = 700
FIELDS = 800


def _function(connection, addr, name, ns, code="", size=10, warn=""):
    connection.execute(
        "INSERT INTO functions (module, addr, name, ns, size, cc, thunk, code, warn, name_src, "
        "name_dump) VALUES (?,?,?,?,?,?,?,?,?,?,?)",
        (MODULE, addr, name, ns, size, "__thiscall", 0, code, warn, "", ""))


def _build(corpus_dir: Path) -> None:
    """A small corpus: one huge body, one small, a 700-slot class, an 800-field class, a slot that
    120 classes fill, and a listing for the huge body."""
    corpus_dir.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(corpus_dir / "corpus.sqlite")
    connection.row_factory = sqlite3.Row
    connection.executescript(corpus.SCHEMA)
    corpus._migrate(connection)
    big = "\n".join(f"  iVar{n} = this->m_value{n % 50} + {n};" for n in range(BODY_LINES))
    _function(connection, "10001000", "Big", "NPC_A", f"void NPC_A::Big(void)\n{{\n{big}\n}}", 9000)
    _function(connection, "1028a380", "SelectSchedule", "NPC_A",
              "int NPC_A::SelectSchedule(void)\n{\n  return StartTask(this);\n}\n")
    _function(connection, "1028a3f0", "StartTask", "NPC_A",
              "int NPC_A::StartTask(void)\n{\n  RunTask(this);\n  return 0;\n}\n")
    for slot in range(SLOTS):
        # Every third slot is the class's own body; the others are inherited from `Base`.
        owner = "NPC_A" if slot % 3 == 0 else "Base"
        addr = f"{0x10100000 + slot * 16:08x}"
        _function(connection, addr, f"vfunc{slot}", owner, f"void {owner}::vfunc{slot}(void) {{}}\n")
        connection.execute("INSERT INTO vtables VALUES (?,?,?,?,?,?,?)",
                           (MODULE, "10500000", "NPC_A", 0, slot, addr, 0))
    for number in range(120):
        connection.execute("INSERT INTO vtables VALUES (?,?,?,?,?,?,?)",
                           (MODULE, f"{0x10600000 + number * 4:08x}", f"CNPC_Species{number:03d}",
                            0, 1, "10100010", 0))
    for number in range(FIELDS):
        connection.execute("INSERT INTO fields VALUES (?,?,?,?,?,?,?)",
                           (MODULE, "NPC_A", number * 4, f"m_field{number}", 1, "int", ""))
    connection.execute("INSERT INTO fields VALUES (?,?,?,?,?,?,?)",
                       (MODULE, "NPC_A", 0x5cc0, "m_scriptState", 1, "int", ""))
    corpus._build_index(connection)
    connection.commit()
    connection.close()
    listing = sqlite3.connect(corpus_dir / "listing.sqlite")
    listing.execute("CREATE TABLE listing (module TEXT, addr TEXT, asm TEXT, "
                    "PRIMARY KEY (module, addr))")
    asm = "\n".join(f"{0x10001000 + n * 3:08x}  NOP" for n in range(1000))
    listing.execute("INSERT INTO listing VALUES (?,?,?)", (MODULE, "10001000", asm))
    listing.commit()
    listing.close()


@pytest.fixture
def work(tmp_path, monkeypatch) -> Path:
    """A work root holding the synthetic corpus, with the module's caches cleared."""
    root = tmp_path / "work"
    monkeypatch.setenv("ELYSIUM_WORK_ROOT", str(root))
    _build(root / "research" / "ghidra" / "corpus")
    for name in ("_CONNECTION", "_LISTING", "_STALENESS"):
        monkeypatch.setattr(corpus, name, None)
    corpus._FTS_VERDICT.clear()
    corpus.end_deadline()
    yield root
    for name in ("_CONNECTION", "_LISTING"):
        held = getattr(corpus, name)
        if held is not None:
            held.close()
    corpus.end_deadline()


def reply(tool: str, **arguments) -> str:
    text, failed = corpus_mcp.run_tool(tool, arguments)
    assert not failed, text
    return text


# ---------------------------------------------------------------------------
# The stop and the journal.

def _runaway() -> int:
    """A statement that would run for minutes: a recursive count to a billion."""
    connection = corpus._connect()
    connection.execute("WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c "
                       "WHERE x < 1000000000) SELECT count(*) FROM c").fetchone()
    return 0


def test_a_query_past_its_deadline_times_out_and_the_server_stays_usable(work, monkeypatch):
    monkeypatch.setattr(corpus, "command_stat", _runaway)
    started = time.perf_counter()
    text, failed = corpus_mcp.run_tool("vtmb_stat", {}, deadline=0.3)
    assert failed
    assert time.perf_counter() - started < 5
    assert text.startswith("TIMEOUT after 0.3 s: vtmb_stat {}")
    assert "narrow it with" in text
    # The abandoned statement left nothing behind: the next call, with no deadline pressure, works.
    monkeypatch.undo()
    assert "SelectSchedule" in reply("vtmb_func", reference="0x1028a380")


def test_python_work_that_is_not_sql_is_stopped_too(work, monkeypatch):
    def spin() -> int:
        while True:
            corpus.check_deadline()
        return 0

    monkeypatch.setattr(corpus, "command_stat", spin)
    text, failed = corpus_mcp.run_tool("vtmb_stat", {}, deadline=0.2)
    assert failed and text.startswith("TIMEOUT after 0.2 s")


def test_the_timeout_names_the_parameter_that_narrows_each_tool(work, monkeypatch):
    monkeypatch.setattr(corpus, "command_grep", lambda *a, **k: _runaway())
    text, _ = corpus_mcp.run_tool("vtmb_grep", {"pattern": "x"}, deadline=0.2)
    assert "module=, limit= or a longer pattern" in text
    monkeypatch.setattr(corpus, "command_code", lambda *a, **k: _runaway())
    text, _ = corpus_mcp.run_tool("vtmb_code", {"reference": "1"}, deadline=0.2)
    assert "from_line= / lines=" in text


def test_every_call_is_journaled_and_a_slow_one_also_reaches_the_slow_query_log(work, monkeypatch):
    reply("vtmb_func", reference="1028a380")
    reply("vtmb_stat")
    rows = (work / "logs" / "corpus-mcp.tsv").read_text(encoding="utf-8").splitlines()
    assert rows[0] == "time\ttool\targuments\tseconds\treply_chars"
    assert [row.split("\t")[1] for row in rows[1:]] == ["vtmb_func", "vtmb_stat"]
    func = rows[1].split("\t")
    assert re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", func[0])
    assert json_args(func[2]) == {"reference": "1028a380"}
    assert float(func[3]) >= 0 and int(func[4]) > 0
    assert not (work / "logs" / "slow-queries.tsv").exists()          # nothing took 10 s

    monkeypatch.setattr(corpus_mcp, "SLOW_SECONDS", -1.0)             # now everything is slow
    reply("vtmb_func", reference="1028a380")
    slow = (work / "logs" / "slow-queries.tsv").read_text(encoding="utf-8").splitlines()
    assert slow[0] == "time\tsource\tquery\tseconds"
    cells = slow[1].split("\t")
    assert cells[1] == "corpus-mcp" and cells[2].startswith("vtmb_func ") and float(cells[3]) >= 0


def test_a_timed_out_call_is_journaled_with_the_timeout_as_its_reply(work, monkeypatch):
    monkeypatch.setattr(corpus, "command_stat", _runaway)
    text, _ = corpus_mcp.run_tool("vtmb_stat", {}, deadline=0.2)
    last = (work / "logs" / "corpus-mcp.tsv").read_text(encoding="utf-8").splitlines()[-1]
    assert int(last.split("\t")[4]) == len(text)


def json_args(cell: str) -> dict:
    import json
    return json.loads(cell)


# ---------------------------------------------------------------------------
# The caps.

def test_code_stops_at_20_kb_names_the_next_line_and_pages_on(work):
    first = reply("vtmb_code", reference="0x10001000")
    assert len(first) <= corpus.REPLY_LIMIT
    match = re.search(r"lines 1-(\d+) of (\d+) shown .*from_line=(\d+) continues", first)
    assert match, first[-300:]
    shown, total, nxt = (int(group) for group in match.groups())
    assert total == BODY_LINES + 3 and nxt == shown + 1 and shown < total
    second = reply("vtmb_code", reference="0x10001000", from_line=nxt, lines=3)
    assert f"iVar{nxt - 3} " in second                      # the body's line `nxt` (3 header lines)
    assert "page ends here" in second and f"from_line={nxt + 3}" in second


def test_code_with_lines_is_exactly_that_page(work):
    text = reply("vtmb_code", reference="0x10001000", from_line=3, lines=2)
    body = [line for line in text.splitlines() if line.startswith("  iVar")]
    assert body == ["  iVar0 = this->m_value0 + 0;", "  iVar1 = this->m_value1 + 1;"]


def test_a_small_body_is_whole_and_says_nothing_of_pages(work):
    text = reply("vtmb_code", reference="0x1028a380")
    assert "return StartTask(this);" in text and "from_line" not in text


def test_asm_is_400_lines_by_default_and_from_takes_an_address(work):
    first = reply("vtmb_asm", reference="0x10001000")
    assert len([line for line in first.splitlines() if re.match(r"[0-9a-f]{8}  ", line)]) == 400
    assert "lines 1-400 of 1000 shown" in first and "from=401" in first
    page = reply("vtmb_asm", reference="0x10001000", **{"from": "0x1000100c", "max_lines": 3})
    rows = [line.split()[0] for line in page.splitlines() if re.match(r"[0-9a-f]{8}  ", line)]
    assert rows == ["1000100c", "1000100f", "10001012"]
    bounded = reply("vtmb_asm", reference="0x10001000", **{"from": 10, "to": 12})
    assert len([line for line in bounded.splitlines() if re.match(r"[0-9a-f]{8}  ", line)]) == 3


def test_vtable_default_is_cut_at_20_kb_and_slots_and_overridden_only_narrow_it(work):
    default = reply("vtmb_vtable", cls="NPC_A")
    assert len(default) <= corpus.REPLY_LIMIT
    assert re.search(r"… \d+ more slot\(s\), #\d+ to #699, past the 20 KB default; slots=\d+-699",
                     default)
    ranged = reply("vtmb_vtable", cls="NPC_A", slots="440-444")
    assert [int(n) for n in re.findall(r"^  #(\d+)", ranged, re.M)] == [440, 441, 442, 443, 444]
    own = reply("vtmb_vtable", cls="NPC_A", slots="0-8", overridden_only=True)
    assert [int(n) for n in re.findall(r"^  #(\d+)", own, re.M)] == [0, 3, 6]
    assert "3 slot(s) in NPC_A (in the range and this class's own; the table has 700)" in own


def test_fields_default_is_cut_and_range_and_name_narrow_it(work):
    default = reply("vtmb_fields", cls="NPC_A")
    assert len(default) <= corpus.REPLY_LIMIT and "past the 20 KB default; range=0x" in default
    ranged = reply("vtmb_fields", cls="NPC_A", **{"range": "0x10-0x1c"})
    assert [line.split()[1] for line in ranged.splitlines() if line.startswith("  +0x")] == [
        "m_field4", "m_field5", "m_field6", "m_field7"]
    named = reply("vtmb_fields", cls="NPC_A", name="scriptstate")
    assert "m_scriptState" in named and "1 of 801 field(s) in NPC_A match" in named


def test_slot_lists_50_classes_and_counts_all_of_them(work):
    # 120 species and `NPC_A` itself fill slot 1.
    text = reply("vtmb_slot", slot=1)
    assert len([line for line in text.splitlines() if line.startswith("  vampire.dll")]) == 50
    assert "… 71 more class(es) not listed; limit= raises the cap, module= narrows it" in text
    assert "121 class(es) fill slot 1" in text
    assert len([line for line in reply("vtmb_slot", slot=1, limit=200).splitlines()
                if line.startswith("  vampire.dll")]) == 121


def test_closure_prints_the_first_rows_brief_the_busiest_and_sections_only_those(work):
    default = reply("vtmb_closure", cls="NPC_A")
    assert len(default) < corpus.REPLY_LIMIT
    assert "… 761 more field(s); sections=fields lists them, limit= sets how many rows" in default
    brief = reply("vtmb_closure", cls="NPC_A", brief=True)
    assert len(brief) < len(default) and "the 12 busiest by caller count" in brief
    methods = reply("vtmb_closure", cls="NPC_A", sections="methods", limit=2)
    assert "## Methods" in methods and "## Fields" not in methods
    assert "limit= raises the row count" in methods
    text, failed = corpus_mcp.run_tool("vtmb_closure", {"cls": "NPC_A", "sections": "bogus"})
    assert failed and "no closure section 'bogus'" in text


def test_a_reply_a_tool_cannot_cap_itself_is_cut_by_the_guard_and_names_the_parameter(work):
    big = "line\n" * 10_000
    cut = corpus_mcp._guard("vtmb_globals", {"text": "x"}, big)
    assert len(cut) < corpus.REPLY_LIMIT + 200 and "narrow it with limit= or a longer text" in cut
    # A caller that sized its own reply is held to the page ceiling instead.
    sized = corpus_mcp._guard("vtmb_globals", {"text": "x", "limit": 500}, big)
    assert sized == big


def test_a_bad_range_is_the_callers_fault_and_says_how_to_write_one(work):
    text, failed = corpus_mcp.run_tool("vtmb_vtable", {"cls": "NPC_A", "slots": "x-y"})
    assert failed and "write one number (442), a span (430-450)" in text


# ---------------------------------------------------------------------------
# The search index: answered from it where it provably agrees with the regex.

PATTERNS = (
    r"thunk_FUN_102ee140|thunk_FUN_102ee620|thunk_FUN_102ee680",
    r"SetNPCTransparent|BlocksTraces|m_bBlocksTraces =",
    r"CheckStuck\(|thunk_FUN_103cb920",
    r",\*\(\w+ \*\)\(\w+ \+ 0x2c\),",
    r"m_scriptState\s*[=!]=\s*[0-9]",
    r"CAI_BaseNPC::(?:Select|Start)Schedule",
    r"(?i:SELECTSCHEDULE)x?",
    r"x{0}NextAttack",
    r"(MeleeSwing)+Step",
    r"[Mm]elee(Swing)?Update",
    r"(?:abc|m_NPCState)\s*==",
    r"a(bc)?d",
    r"gone|Gone",
    r"(?=abc)abcdef",
    r"[ab]cde",
    r"xyz+",
)

TEXTS = (
    "thunk_FUN_102ee620(x);", "iVar1 = m_bBlocksTraces = 0;", "CheckStuck(this)", "foo,*(int *)(p + 0x2c),bar",
    "if (m_scriptState == 3) {", "CAI_BaseNPC::StartSchedule(", "calling selectschedulexx", "NextAttack",
    "MeleeSwingMeleeSwingStep", "MeleeUpdate", "x m_NPCState == 1", "ad", "abcd", "GONE", "abcdef",
    "bcde", "xyzzz", "nothing of interest here", "", "thunk_FUN_102ee", "BlocksTraces",
)


@pytest.mark.parametrize("pattern", PATTERNS)
def test_the_prefilter_never_drops_a_match(pattern):
    """Whatever text the regex matches, the full-text query must match as well -- the index may
    over-select, because the regex still decides, but one miss is a silent hole in an answer."""
    query = corpus._prefilter(pattern)
    connection = sqlite3.connect(":memory:")
    connection.execute("CREATE VIRTUAL TABLE t USING fts5(code, tokenize='trigram')")
    for number, text in enumerate(TEXTS):
        connection.execute("INSERT INTO t(rowid, code) VALUES (?, ?)", (number, text))
    regex = re.compile(pattern, re.IGNORECASE)
    wanted = {number for number, text in enumerate(TEXTS) if regex.search(text)}
    if query is None:
        return                                  # no prefilter: the scan is the answer
    narrowed = {row[0] for row in connection.execute(
        "SELECT rowid FROM t WHERE t MATCH ?", (query,))}
    assert wanted <= narrowed, (pattern, query, wanted - narrowed)


def test_an_alternation_is_prefiltered_on_what_every_branch_needs_and_a_hole_is_not():
    assert corpus._prefilter("abc|def") == '("abc" OR "def")'
    # `.` can be anything, so a branch with nothing of three characters in it matches anything.
    assert corpus._prefilter("abc|d.f") is None
    assert corpus._prefilter("thunk_FUN_102ee140|thunk_FUN_102ee620") == \
        '"thunk_FUN_102ee" AND ("140" OR "620")'
    assert corpus._prefilter("ab") is None and corpus._prefilter(r"\w+") is None
    assert corpus._prefilter('say "hi" now') == '"say ""hi"" now"'


def test_grep_answers_an_alternation_from_the_index_and_agrees_with_a_scan(work, capsys):
    corpus.command_grep("SelectSchedule|StartTask", None, 100)
    indexed = capsys.readouterr().out
    assert "(indexed on (\"electSchedule\" OR \"tartTask\"): " in indexed          # `S` is factored out
    assert "2 function(s) match" in indexed          # `Big` and the slot bodies carry neither
    corpus._FTS_VERDICT[id(corpus._connect())] = (False, "forced")
    corpus.command_grep("SelectSchedule|StartTask", None, 100)
    scanned = capsys.readouterr().out
    assert "search index is stale" in scanned and "2 function(s) match" in scanned


def test_a_stale_index_is_not_trusted_and_grep_scans_instead(work, capsys):
    connection = corpus._connect()
    # Move 10% of the rows' text under the index, the way a re-dump with new names would.
    connection.execute("UPDATE functions SET code = code || ' -- renamed: ThunkedNowGone' "
                       "WHERE rowid % 10 = 0")
    connection.commit()
    corpus._FTS_VERDICT.clear()
    current, why = corpus._index_is_current(connection)
    assert not current and "sampled functions differ from the text it was built from" in why
    truth = connection.execute("SELECT count(*) FROM functions WHERE code LIKE '%ThunkedNowGone%'"
                               ).fetchone()[0]
    assert truth > 0
    corpus.command_grep("ThunkedNowGone", None, 100000)
    out = capsys.readouterr().out
    assert "search index is stale" in out and f"{truth} function(s) match" in out


def test_a_fresh_index_is_trusted(work):
    current, why = corpus._index_is_current(corpus._connect())
    assert current and why == ""


def test_the_hot_lookups_do_not_read_the_decompilation(work):
    """`callers`, `twin` and the class tools name functions; none of them needs their C, which is
    98 KB for `CAI_BaseNPC::StartTask`. `_resolve` stops short of the column unless asked."""
    connection = corpus._connect()
    light = corpus._resolve(connection, "0x10001000")[0]
    assert "code" not in light.keys() and "warn" not in light.keys()
    assert "code" in corpus._resolve(connection, "0x10001000", "code")[0].keys()
