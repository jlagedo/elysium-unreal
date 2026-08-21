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
"""

from __future__ import annotations

import io
import json
import os
import sys
import traceback
from contextlib import redirect_stdout
from pathlib import Path


def _load_local_environment() -> None:
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


_load_local_environment()

import corpus  # noqa: E402  -- imported after the environment it reads is in place

PROTOCOL_VERSION = "2025-06-18"

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
                       "does, the C is not the whole function and vtmb_asm is the fallback.",
        "inputSchema": {"type": "object", "required": ["reference"], "properties": {
            "reference": {"type": "string"}}},
    },
    {
        "name": "vtmb_asm",
        "description": "The disassembly of a VtMB function. Use when vtmb_code reports damage, "
                       "or when the question is about a constant the decompiler folded away -- a "
                       "varargs or __thiscall KeyValues call shows as a bare GetInt() in C while "
                       "the listing still shows the PUSH of the key name.",
        "inputSchema": {"type": "object", "required": ["reference"], "properties": {
            "reference": {"type": "string"}}},
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
            "reference": {"type": "string"}}},
    },
    {
        "name": "vtmb_vtable",
        "description": "One class's dispatch table, slot by slot, with the function each slot "
                       "holds and how many callers it has -- the shape of its virtual interface.",
        "inputSchema": {"type": "object", "required": ["cls"], "properties": {
            "cls": {"type": "string"}}},
    },
    {
        "name": "vtmb_slot",
        "description": "Every class that fills a given vtable slot: one virtual compared across "
                       "the whole hierarchy. This is also the honest answer to 'who overrides "
                       "this' -- a dispatch through a base pointer can land on any of them.",
        "inputSchema": {"type": "object", "required": ["slot"], "properties": {
            "slot": {"type": "integer"}, "module": {"type": "string"}}},
    },
    {
        "name": "vtmb_globals",
        "description": "A named global in .data/.rdata -- a cvar object, a vftable, a datamap, a "
                       "counter -- and every function that reaches it.",
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
        "description": "Every function the given one calls.",
        "inputSchema": {"type": "object", "required": ["reference"], "properties": {
            "reference": {"type": "string"}}},
    },
    {
        "name": "vtmb_grep",
        "description": "Regex search over every decompiled function in the corpus. Use this "
                       "instead of running a Ghidra script.",
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
        "description": "A class's fields by offset, recovered from its datamap.",
        "inputSchema": {"type": "object", "required": ["cls"], "properties": {
            "cls": {"type": "string"}, "offset": {"type": "string"}}},
    },
    {
        "name": "vtmb_readers",
        "description": "THE FIELD LEDGER. Every function that touches a struct offset, both "
                       "the typed accesses (class known) and the untyped candidates. This is "
                       "what makes 'nothing else reads this field' provable.",
        "inputSchema": {"type": "object", "required": ["offset"], "properties": {
            "offset": {"type": "string", "description": "e.g. 0x2f8"},
            "cls": {"type": "string"}, "limit": {"type": "integer"}}},
    },
    {
        "name": "vtmb_closure",
        "description": "One class, whole: fields, methods with their caller counts, and the "
                       "strings they reference. The unit of RE work. Decompilation is omitted "
                       "here because a large class runs to hundreds of KB -- read a method with "
                       "vtmb_code, or write the full version with `corpus closure --out`.",
        "inputSchema": {"type": "object", "required": ["cls"], "properties": {
            "cls": {"type": "string"}}},
    },
    {
        "name": "vtmb_stat",
        "description": "What the corpus holds, per module.",
        "inputSchema": {"type": "object", "properties": {}},
    },
]


def _offset(value: str) -> int:
    return int(value, 16) if value.lower().startswith("0x") else int(value, 0)


def _call(name: str, arguments: dict) -> str:
    buffer = io.StringIO()
    with redirect_stdout(buffer):
        if name == "vtmb_func":
            corpus.command_func(arguments["reference"])
        elif name == "vtmb_code":
            corpus.command_code(arguments["reference"])
        elif name == "vtmb_asm":
            corpus.command_asm(arguments["reference"])
        elif name == "vtmb_vtable":
            corpus.command_vtable(arguments["cls"])
        elif name == "vtmb_slot":
            corpus.command_slot(int(arguments["slot"]), arguments.get("module"))
        elif name == "vtmb_globals":
            corpus.command_globals(arguments["text"], int(arguments.get("limit") or 20))
        elif name == "vtmb_twin":
            corpus.command_twin(arguments["reference"])
        elif name == "vtmb_callers":
            corpus.command_hop(arguments["reference"], "callers")
        elif name == "vtmb_callees":
            corpus.command_hop(arguments["reference"], "callees")
        elif name == "vtmb_grep":
            corpus.command_grep(arguments["pattern"], arguments.get("module"),
                                int(arguments.get("limit") or 40))
        elif name == "vtmb_string":
            corpus.command_str(arguments["text"], int(arguments.get("limit") or 40))
        elif name == "vtmb_fields":
            corpus.command_fields(arguments["cls"],
                                  _offset(arguments["offset"]) if arguments.get("offset") else None)
        elif name == "vtmb_readers":
            corpus.command_readers(_offset(arguments["offset"]), arguments.get("cls"),
                                   int(arguments.get("limit") or 60))
        elif name == "vtmb_closure":
            corpus.command_closure(arguments["cls"], None, with_code=False)
        elif name == "vtmb_stat":
            corpus.command_stat()
        else:
            raise ValueError(f"unknown tool {name!r}")
    return buffer.getvalue() or "(no output)"


def _send(message: dict) -> None:
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()


def main() -> int:
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
                "serverInfo": {"name": "vtmb-corpus", "version": "1.0"}}})
        elif method == "tools/list":
            _send({"jsonrpc": "2.0", "id": identifier, "result": {"tools": TOOLS}})
        elif method == "tools/call":
            parameters = request.get("params", {})
            try:
                text = _call(parameters.get("name", ""), parameters.get("arguments") or {})
                _send({"jsonrpc": "2.0", "id": identifier, "result": {
                    "content": [{"type": "text", "text": text}]}})
            except Exception as error:                      # surfaced, never swallowed
                traceback.print_exc(file=sys.stderr)
                _send({"jsonrpc": "2.0", "id": identifier, "result": {
                    "isError": True,
                    "content": [{"type": "text", "text": f"{type(error).__name__}: {error}"}]}})
        elif identifier is not None:
            _send({"jsonrpc": "2.0", "id": identifier,
                   "error": {"code": -32601, "message": f"unsupported method {method!r}"}})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
