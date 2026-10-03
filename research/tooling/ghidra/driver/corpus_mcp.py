#!/usr/bin/env python3
"""stdio MCP server over the whole-body corpus.

The corpus answers the questions that used to cost a headless Ghidra run and the project
lock -- who calls this, what else touches this offset, which functions mention this string,
what does this class look like whole. Exposing them as MCP tools is what removes the run from
the loop entirely: an agent asks the database.

Pure stdlib, newline-delimited JSON-RPC on stdin/stdout, the same shape as
`elysium_pipeline.devtools.mcp_proxy`. Every tool is read-only; nothing here can modify a
Ghidra project, and the corpus itself is rebuilt by `corpus dump` / `corpus build`.

Register it beside the in-game server:

    "vtmb-corpus": {"type": "stdio", "command": "uv", "args": ["run", "--no-sync", "python",
        "research/tooling/ghidra/driver/corpus_mcp.py"]}

It is launched directly rather than through `uv run elysium research`, because the CLI writes
a run report to stdout and stdout here carries only protocol messages. That means the local
environment is not loaded for it, so it reads `.elysium.local.env` itself.

Every call is bounded and journaled. It runs under a 60 s deadline (`corpus.begin_deadline`: an
SQLite progress handler for the SQL, a clock check between rows for the Python loops) and past it
answers `TIMEOUT after 60 s: <tool> <args> -- narrow it with <parameter>` rather than holding the
caller's turn. A default reply is held to 20,000 characters, each cut naming the parameter that
gets more. Each call is appended to `$ELYSIUM_WORK_ROOT/logs/corpus-mcp.tsv` (time, tool,
arguments, seconds, reply characters), and one over 10 s also to `slow-queries.tsv` there (time,
source, query, seconds), the project's slow-query log.
"""

from __future__ import annotations

import io
import json
import os
import sqlite3
import sys
import time
import traceback
from contextlib import redirect_stdout
from datetime import datetime, timezone
from pathlib import Path


def load_local_environment() -> None:
    """`.elysium.local.env`, read directly: the CLI that normally loads it is not in the path."""
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
    print("corpus_mcp: no .elysium.local.env found; ELYSIUM_WORK_ROOT is unset and every"
          " query will fail", file=sys.stderr)


# `corpus` reads nothing from the environment when it is imported, so the environment is loaded
# by `main()` and not here: importing this module (a test, a probe) must not write `os.environ`.
import corpus  # noqa: E402

PROTOCOL_VERSION = "2025-06-18"

# The stop. A query past this is abandoned and the caller is told to narrow it.
DEADLINE_SECONDS = 60.0
# A call past this is also written to the slow-query log.
SLOW_SECONDS = 10.0

JOURNAL_HEADER = "time\ttool\targuments\tseconds\treply_chars"
SLOW_HEADER = "time\tsource\tquery\tseconds"
SLOW_SOURCE = "corpus-mcp"

_LOOKUP_DIR = Path(__file__).resolve().parents[2] / "lookup"


def _int_property(description: str) -> dict:
    return {"type": "integer", "description": description}


TOOLS = [
    {
        "name": "vtmb_func",
        "description": "One VtMB function's facts: module, address, class, calling convention, "
                       "size, and how many callers, callees and strings it has. Accepts an "
                       "address (10161200), a name, or Class::method.",
        "inputSchema": {"type": "object", "required": ["reference"], "properties": {
            "reference": {"type": "string"}}},
    },
    {
        "name": "vtmb_code",
        "description": "The decompiled C of a VtMB function, as the corpus captured it "
                       "(names and struct field types already applied). Prints a DAMAGED banner "
                       "when the decompiler dropped blocks or failed a jump table -- when it "
                       "does, the C is not the whole function and vtmb_asm is the fallback. "
                       "A reply is at most 20 KB; the cut names the line it stopped at, and "
                       "from_line / lines page through the rest.",
        "inputSchema": {"type": "object", "required": ["reference"], "properties": {
            "reference": {"type": "string"},
            "from_line": _int_property("first line of the page, 1-based (default 1)"),
            "lines": _int_property("page length in lines; raises the cap to 100 KB")}},
    },
    {
        "name": "vtmb_asm",
        "description": "The disassembly of a VtMB function. Use when vtmb_code reports damage, "
                       "or when the question is about a constant the decompiler folded away -- a "
                       "varargs or __thiscall KeyValues call shows as a bare GetInt() in C while "
                       "the listing still shows the PUSH of the key name. A page of 400 lines "
                       "by default; from and to take a line number or an address.",
        "inputSchema": {"type": "object", "required": ["reference"], "properties": {
            "reference": {"type": "string"},
            "max_lines": _int_property("page length in lines (default 400)"),
            "from": {"type": "string", "description": "first line: a line number, or an address "
                                                       "(0x1028a380, or 6+ hex digits)"},
            "to": {"type": "string", "description": "last line, likewise"}}},
    },
    {
        "name": "vtmb_callers",
        "description": "Every function that calls the given one, in three labelled sections: "
                       "DIRECT (a static call), VIRTUAL (a dispatch site whose receiver class "
                       "holds this function at that slot), and POSSIBLE (a dispatch at the same "
                       "slot whose receiver class the code does not state -- candidates, not "
                       "facts). VtMB dispatches its game logic virtually, so for most class "
                       "methods the direct section is empty and the other two carry the answer.",
        "inputSchema": {"type": "object", "required": ["reference"], "properties": {
            "reference": {"type": "string"},
            "limit": _int_property("rows per section (default 100)")}},
    },
    {
        "name": "vtmb_vtable",
        "description": "One class's dispatch table, slot by slot, with the function each slot "
                       "holds and how many callers it has -- the shape of its virtual interface. "
                       "A reply is at most 20 KB; slots reads a range, overridden_only keeps "
                       "the slots whose body is this class's own.",
        "inputSchema": {"type": "object", "required": ["cls"], "properties": {
            "cls": {"type": "string"},
            "slots": {"type": "string", "description": "430-450, 442, or 5,9,12-20"},
            "overridden_only": {"type": "boolean", "description": "only the slots this class "
                                                                   "itself defines"}}},
    },
    {
        "name": "vtmb_slot",
        "description": "Every class that fills a given vtable slot: one virtual compared across "
                       "the whole hierarchy. This is also the honest answer to 'who overrides "
                       "this' -- a dispatch through a base pointer can land on any of them. The "
                       "first 50 classes; the count at the end is of all of them.",
        "inputSchema": {"type": "object", "required": ["slot"], "properties": {
            "slot": {"type": "integer"}, "module": {"type": "string"},
            "limit": _int_property("classes to list (default 50)")}},
    },
    {
        "name": "vtmb_iface",
        "description": "Named interfaces — how a call LEAVES its binary. Source publishes each "
                       "module's services by versioned name (VEngineServer014), and both ends "
                       "are literals in the image, so a dispatch through an interface global "
                       "resolves to a real function in another DLL. Pass a pattern, or nothing "
                       "for the whole map. vtmb_callers and vtmb_callees report these edges in "
                       "their own CROSS-MODULE section.",
        "inputSchema": {"type": "object", "properties": {
            "pattern": {"type": "string", "description": "e.g. VEngineServer"}}},
    },
    {
        "name": "vtmb_globals",
        "description": "A global in .data/.rdata -- a cvar object, a vftable, a datamap, a "
                       "counter -- and every function that reaches it, by NAME or by bare "
                       "ADDRESS (20b42980), named or not. The referrers are split by how they "
                       "touch it: what a singleton is, is stated by whatever assigns it, and a "
                       "global C++ object is assigned by the constructor it is passed to as "
                       "`this` rather than by any write to its address.",
        "inputSchema": {"type": "object", "required": ["text"], "properties": {
            "text": {"type": "string"}, "limit": {"type": "integer"}}},
    },
    {
        "name": "vtmb_twin",
        "description": "The same function in the other module. vampire.dll and client.dll are "
                       "the server and client halves of one codebase and share an imagebase, so "
                       "an address alone is ambiguous and the recovered name is the only join.",
        "inputSchema": {"type": "object", "required": ["reference"], "properties": {
            "reference": {"type": "string"}}},
    },
    {
        "name": "vtmb_callees",
        "description": "Every function the given one calls: direct and virtual edges, then the "
                       "calls into other DLLs whose bodies the corpus does not hold, then the "
                       "virtual dispatches whose receiver class the code does not state.",
        "inputSchema": {"type": "object", "required": ["reference"], "properties": {
            "reference": {"type": "string"},
            "limit": _int_property("rows per section (default 100)")}},
    },
    {
        "name": "vtmb_grep",
        "description": "Regex search over every decompiled function in the corpus, "
                       "case-insensitively. Use this instead of running a Ghidra script. The "
                       "literals a pattern must contain -- including those of an alternation, "
                       "a|b|c -- are answered from a trigram index in milliseconds; a pattern "
                       "with none scans all 60 MB and says so. Reports one hit per function "
                       "with up to six matching lines, and stops at `limit` functions or at "
                       "20 KB -- the count on the last line is what was printed, so a pattern "
                       "that hits a stop was not counted to the end.",
        "inputSchema": {"type": "object", "required": ["pattern"], "properties": {
            "pattern": {"type": "string"},
            "module": {"type": "string", "description": "e.g. vampire.dll"},
            "limit": {"type": "integer"}}},
    },
    {
        "name": "vtmb_string",
        "description": "Strings containing the given text, with the functions that reference "
                       "each -- how a keyfield, message or classname is traced to its handler.",
        "inputSchema": {"type": "object", "required": ["text"], "properties": {
            "text": {"type": "string"}, "limit": {"type": "integer"}}},
    },
    {
        "name": "vtmb_fields",
        "description": "A class's fields by offset, recovered from its datamap. A reply is at "
                       "most 20 KB; range reads an offset range, name keeps the fields whose "
                       "name contains a text.",
        "inputSchema": {"type": "object", "required": ["cls"], "properties": {
            "cls": {"type": "string"}, "offset": {"type": "string"},
            "range": {"type": "string", "description": "0x100-0x1ff"},
            "name": {"type": "string", "description": "substring of the field name"}}},
    },
    {
        "name": "vtmb_readers",
        "description": "THE FIELD LEDGER. Every function that touches a struct offset, both "
                       "the typed accesses (class known) and the untyped candidates. This is "
                       "what makes 'nothing else reads this field' provable. Both sections "
                       "are counted in full and `limit` applies to each separately, so a small "
                       "limit never hides one behind the other.",
        "inputSchema": {"type": "object", "required": ["offset"], "properties": {
            "offset": {"type": "string", "description": "e.g. 0x2f8"},
            "cls": {"type": "string"}, "limit": {"type": "integer"}}},
    },
    {
        "name": "vtmb_closure",
        "description": "One class, whole: fields, methods with their caller counts, and the "
                       "strings they reference. The unit of RE work. Decompilation is omitted "
                       "here because a large class runs to hundreds of KB -- read a method with "
                       "vtmb_code, or write the full version with `corpus closure --out`. The "
                       "default prints the first rows of each section; brief is the counts and "
                       "the busiest methods; sections selects fields, methods and/or strings.",
        "inputSchema": {"type": "object", "required": ["cls"], "properties": {
            "cls": {"type": "string"},
            "sections": {"type": "string", "description": "comma list of fields, methods, "
                                                           "strings; each to `limit` rows "
                                                           "(default 400)"},
            "brief": {"type": "boolean", "description": "counts and the 12 busiest methods"},
            "limit": _int_property("rows per section")}},
    },
    {
        "name": "vtmb_stat",
        "description": "What the corpus holds, per module.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "vtmb_where",
        "description": "Where the project already speaks about a retail address, name, field or "
                       "slot -- ask this BEFORE grepping docs/vtmb/. Takes 0x1028a380 (or "
                       "1028a380), CAI_BaseNPC::SelectSchedule, m_scriptState, a bare member "
                       "name, or `slot 442`. Answers in one reply: its docs/vtmb sections (file, "
                       "heading, line), its rows in the npc-kernel ledger tables (functions, "
                       "fields, slots, entries, checklists, ...), and the port source lines that "
                       "cite it. Built from a generated index, answered in milliseconds.",
        "inputSchema": {"type": "object", "required": ["query"], "properties": {
            "query": {"type": "string"},
            "limit": _int_property("lines per section (default 8)")}},
    },
]

# What to narrow when a call is stopped or cut, per tool. The deadline and the 20 KB guard both
# name it, so the caller learns which argument to change rather than only that it must.
HINTS = {
    "vtmb_func": "a more specific reference (an address)",
    "vtmb_code": "from_line= / lines=",
    "vtmb_asm": "from= / to= / max_lines=",
    "vtmb_callers": "limit=",
    "vtmb_callees": "limit=",
    "vtmb_vtable": "slots= / overridden_only=",
    "vtmb_slot": "limit= / module=",
    "vtmb_iface": "pattern=",
    "vtmb_globals": "limit= or a longer text",
    "vtmb_twin": "a more specific reference (an address)",
    "vtmb_grep": "module=, limit= or a longer pattern",
    "vtmb_string": "limit= or a longer text",
    "vtmb_fields": "range= / name= / offset=",
    "vtmb_readers": "cls= / limit=",
    "vtmb_closure": "sections= / brief / limit=",
    "vtmb_stat": "nothing: it takes no arguments",
    "vtmb_where": "limit= or a narrower query",
}

# An argument that says how much the caller wants. A reply to a call that carries one has been
# sized by the caller, so the 20 KB guard steps back to the page ceiling.
SIZING = {"from_line", "lines", "max_lines", "from", "to", "slots", "range", "name", "sections",
          "limit", "offset"}


class ToolInput(Exception):
    """A caller's argument is unusable. Its message is the whole answer they get, so it says
    what was wrong and what a working value looks like -- `int()`'s own ValueError does not."""


def _offset(value: str) -> int:
    try:
        return int(value, 16) if value.lower().startswith("0x") else int(value, 0)
    except (TypeError, ValueError):
        raise ToolInput(f"{value!r} is not an offset; write it in hex as 0x2f8, or in decimal "
                        f"as 760") from None


def _slot(arguments: dict) -> int:
    value = arguments.get("slot")
    try:
        return int(value)
    except (TypeError, ValueError):
        raise ToolInput(f"'slot' must be a vtable slot number; got {value!r}") from None


def _text(arguments: dict, key: str, tool: str) -> str:
    value = arguments.get(key)
    if not isinstance(value, str) or not value.strip():
        raise ToolInput(f"{tool} needs a non-empty {key!r}; got {value!r}")
    return value


def _count(arguments: dict, key: str, fallback: int) -> int:
    value = arguments.get(key)
    if value in (None, ""):
        return fallback
    try:
        number = int(value)
    except (TypeError, ValueError):
        raise ToolInput(f"{key!r} must be a whole number; got {value!r}") from None
    if number < 1:
        raise ToolInput(f"{key!r} must be at least 1; got {number}")
    return number


def _optional_count(arguments: dict, key: str) -> int | None:
    return None if arguments.get(key) in (None, "") else _count(arguments, key, 1)


def _flag(arguments: dict, key: str) -> bool:
    value = arguments.get(key)
    if isinstance(value, str):
        return value.strip().lower() in ("1", "true", "yes", "on")
    return bool(value)


def _where(arguments: dict) -> str:
    """The address index. Imported on first use, so a server whose `lookup/` directory is absent
    still answers every other tool."""
    if str(_LOOKUP_DIR) not in sys.path:
        sys.path.insert(0, str(_LOOKUP_DIR))
    import addr_index
    return addr_index.where(_text(arguments, "query", "vtmb_where"),
                            _count(arguments, "limit", addr_index.DEFAULT_ROWS))


def _dispatch(name: str, arguments: dict) -> None:
    """Run one corpus command; what it prints is its reply."""
    if name == "vtmb_func":
        corpus.command_func(_text(arguments, "reference", name))
    elif name == "vtmb_code":
        corpus.command_code(_text(arguments, "reference", name),
                            _count(arguments, "from_line", 1), _optional_count(arguments, "lines"))
    elif name == "vtmb_asm":
        corpus.command_asm(_text(arguments, "reference", name),
                           _count(arguments, "max_lines", corpus.ASM_LINES),
                           arguments.get("from"), arguments.get("to"))
    elif name == "vtmb_vtable":
        corpus.command_vtable(_text(arguments, "cls", name), arguments.get("slots"),
                              _flag(arguments, "overridden_only"))
    elif name == "vtmb_slot":
        corpus.command_slot(_slot(arguments), arguments.get("module"),
                            _count(arguments, "limit", corpus.SLOT_LIMIT))
    elif name == "vtmb_iface":
        corpus.command_iface(arguments.get("pattern") or None)
    elif name == "vtmb_globals":
        corpus.command_globals(_text(arguments, "text", name), _count(arguments, "limit", 20))
    elif name == "vtmb_twin":
        corpus.command_twin(_text(arguments, "reference", name))
    elif name == "vtmb_callers":
        corpus.command_hop(_text(arguments, "reference", name), "callers",
                           _count(arguments, "limit", corpus.HOP_LIMIT))
    elif name == "vtmb_callees":
        corpus.command_hop(_text(arguments, "reference", name), "callees",
                           _count(arguments, "limit", corpus.HOP_LIMIT))
    elif name == "vtmb_grep":
        corpus.command_grep(_text(arguments, "pattern", name), arguments.get("module"),
                            _count(arguments, "limit", 40))
    elif name == "vtmb_string":
        corpus.command_str(_text(arguments, "text", name), _count(arguments, "limit", 40))
    elif name == "vtmb_fields":
        corpus.command_fields(_text(arguments, "cls", name),
                              _offset(arguments["offset"]) if arguments.get("offset") else None,
                              arguments.get("range"), arguments.get("name"))
    elif name == "vtmb_readers":
        corpus.command_readers(_offset(_text(arguments, "offset", name)),
                               arguments.get("cls"), _count(arguments, "limit", 60))
    elif name == "vtmb_closure":
        corpus.command_closure(_text(arguments, "cls", name), None, with_code=False,
                               sections=arguments.get("sections"),
                               brief=_flag(arguments, "brief"),
                               limit=_optional_count(arguments, "limit"))
    elif name == "vtmb_stat":
        corpus.command_stat()
    else:
        raise ToolInput(f"no tool named {name!r}; this server offers "
                        + ", ".join(one["name"] for one in TOOLS))


def _call(name: str, arguments: dict) -> str:
    """One tool's reply as text, with no deadline and no journal -- the body `run_tool` bounds."""
    if name == "vtmb_where":
        return _where(arguments)
    buffer = io.StringIO()
    try:
        with redirect_stdout(buffer):
            _dispatch(name, arguments)
    except ValueError as error:                 # a bad `slots=` / `range=` / `sections=` value
        raise ToolInput(str(error)) from error
    return buffer.getvalue() or "(no output)"


def _guard(name: str, arguments: dict, text: str) -> str:
    """Hold a reply to the characters its caller can carry.

    The tools that print rows cap themselves and say which parameter gets more; this is the floor
    under the ones that cannot (`globals`, `iface`, `readers`): a reply past 20,000 characters is
    cut at a line and says what was cut. A call that carries a sizing argument (`lines`, `limit`,
    `slots`...) chose its own size, so only the 100 KB page ceiling applies to it.
    """
    ceiling = corpus.PAGE_LIMIT if SIZING.intersection(arguments) else corpus.REPLY_LIMIT
    if len(text) <= ceiling:
        return text
    cut = text.rfind("\n", 0, ceiling)
    cut = cut if cut > 0 else ceiling
    return (text[:cut] + f"\n… reply cut at {ceiling:,} of {len(text):,} characters; narrow it "
            f"with {HINTS.get(name, 'a narrower argument')}")


def _timeout(name: str, arguments: dict, seconds: float) -> str:
    shown = json.dumps(arguments, ensure_ascii=False, separators=(",", ":"))
    if len(shown) > 200:
        shown = shown[:200] + "…"
    return (f"TIMEOUT after {seconds:g} s: {name} {shown} — narrow it with "
            f"{HINTS.get(name, 'a narrower argument')}")


def _logs() -> Path:
    from elysium_pipeline.paths import log_root
    return log_root()


def _append(path: Path, header: str, row: str) -> None:
    """Append one TSV row, writing the header first when the file is new. A journal that cannot
    be written must never fail the call it describes."""
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        fresh = not path.is_file() or path.stat().st_size == 0
        with path.open("a", encoding="utf-8", newline="\n") as handle:
            if fresh:
                handle.write(header + "\n")
            handle.write(row + "\n")
    except OSError as error:
        print(f"corpus_mcp: journal {path.name} not written: {error}", file=sys.stderr)


def _cell(text: str, width: int = 300) -> str:
    text = " ".join(text.replace("\t", " ").split())
    return text if len(text) <= width else text[:width] + "…"


def journal(name: str, arguments: dict, seconds: float, reply_chars: int) -> None:
    """The call, in `corpus-mcp.tsv`; and in `slow-queries.tsv` too when it took over 10 s."""
    try:
        logs = _logs()
    except Exception as error:                  # no work root: nothing to journal into
        print(f"corpus_mcp: no log root, call not journaled: {error}", file=sys.stderr)
        return
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    shown = _cell(json.dumps(arguments, ensure_ascii=False, separators=(",", ":")))
    _append(logs / "corpus-mcp.tsv", JOURNAL_HEADER,
            f"{stamp}\t{name}\t{shown}\t{seconds:.3f}\t{reply_chars}")
    if seconds > SLOW_SECONDS:
        _append(logs / "slow-queries.tsv", SLOW_HEADER,
                f"{stamp}\t{SLOW_SOURCE}\t{name} {shown}\t{seconds:.3f}")


def run_tool(name: str, arguments: dict, deadline: float = DEADLINE_SECONDS) -> tuple[str, bool]:
    """One bounded, journaled call: (reply text, is_error)."""
    started = time.perf_counter()
    corpus.begin_deadline(deadline)
    failed = False
    try:
        text = _guard(name, arguments, _call(name, arguments))
    except ToolInput as error:                      # the caller's fault, and fixable
        text, failed = str(error), True
    except corpus.QueryTimeout:
        text, failed = _timeout(name, arguments, deadline), True
    except sqlite3.OperationalError as error:
        if "interrupted" in str(error) and corpus.deadline_expired():
            text = _timeout(name, arguments, deadline)
        else:
            traceback.print_exc(file=sys.stderr)
            text = f"{type(error).__name__}: {error}"
        failed = True
    except Exception as error:                      # surfaced, never swallowed
        traceback.print_exc(file=sys.stderr)
        text, failed = f"{type(error).__name__}: {error}", True
    finally:
        corpus.end_deadline()
    journal(name, arguments, time.perf_counter() - started, len(text))
    return text, failed


# The protocol channel. A tool's own `print` is captured per call by `_call`; replies go out on
# the stream this module was started with, whatever `sys.stdout` is at that moment.
_PROTOCOL = sys.stdout


def _send(message: dict) -> None:
    _PROTOCOL.write(json.dumps(message) + "\n")
    _PROTOCOL.flush()


def main() -> int:
    load_local_environment()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            request = json.loads(line)
        except json.JSONDecodeError as error:
            print(f"corpus_mcp: undecodable line: {error}", file=sys.stderr)
            continue

        method = request.get("method")
        identifier = request.get("id")
        if method == "initialize":
            _send({"jsonrpc": "2.0", "id": identifier, "result": {
                "protocolVersion": request.get("params", {}).get(
                    "protocolVersion", PROTOCOL_VERSION),
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "vtmb-corpus", "version": "1.1"}}})
        elif method == "tools/list":
            _send({"jsonrpc": "2.0", "id": identifier, "result": {"tools": TOOLS}})
        elif method == "tools/call":
            parameters = request.get("params", {})
            text, failed = run_tool(parameters.get("name", ""), parameters.get("arguments") or {})
            result = {"content": [{"type": "text", "text": text}]}
            if failed:
                result["isError"] = True
            _send({"jsonrpc": "2.0", "id": identifier, "result": result})
        elif identifier is not None:
            _send({"jsonrpc": "2.0", "id": identifier,
                   "error": {"code": -32601, "message": f"unsupported method {method!r}"}})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
