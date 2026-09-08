"""The console-script grammar: commands, bindings, aliases, cvars and script expressions.

A console script is a sequence of commands separated by line ends or `;`, with `//` comments and
double-quoted arguments; `unbindall`, `bind`, `alias`, `exec` and `stuffcmds` are commands like
any other.
"""

from __future__ import annotations

import re
from typing import Any

from elysium_pipeline.formats.engine_config_glb import lexer
from elysium_pipeline.formats.engine_config_glb.coverage import whitespace_omission
from elysium_pipeline.formats.unit_contract.ledger import ByteLedger

#: Command names this grammar gives their own typed row; a two-token line with any other name
#: (and that is not a declared alias) is a `cvars[]` row instead.
_TYPED_VERBS = frozenset({"bind", "alias", "exec", "stuffcmds"})

_CALL_SHAPE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*\(.*\)$", re.DOTALL)


def _token_dict(token: lexer.Token) -> dict[str, Any]:
    return {"text": token.text, "quoted": token.quoted}


def _looks_like_script(text: str) -> bool:
    stripped = text.strip()
    if stripped.startswith("__main__."):
        return True
    return bool(_CALL_SHAPE.match(stripped))


def _translate_key(source: str) -> tuple[str, str]:
    """`(key, sourceKey)`: the SEMICOLON alias resolves to the literal character it stands in
    for, because a bare, unquoted `;` cannot be written as a console-script token."""

    if source.strip().upper() == "SEMICOLON":
        return ";", source
    return source, source


def split_subcommands(text: str) -> list[dict[str, Any]]:
    """Re-tokenize one alias body or bind command string into its own `name`/`args` list.

    The bytes here already belong to the outer `bind`/`alias` argument that the ledger claimed,
    so a sub-command carries no offset of its own -- it would name a place inside the enclosing
    quoted string, not a place in the file's own coordinate space.
    """

    tokens = [t for t in lexer.tokenize_console(text) if t.kind not in ("whitespace", "comment")]
    commands: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    for token in tokens:
        if token.kind == "semicolon":
            current = None
            continue
        if current is None:
            current = {"name": token.text, "args": []}
            commands.append(current)
        else:
            current["args"].append(_token_dict(token))
    return commands


def parse_console_script(member_path: str, data: bytes) -> dict[str, Any]:
    """Decode one console script into `commands[]` plus the typed projections, and a gapless
    byte ledger over the whole member."""

    text = lexer.decode_text(data)
    tokens = lexer.tokenize_console(text)
    ledger = ByteLedger(member_path, data)

    comments: list[dict[str, Any]] = []
    anomalies: list[dict[str, Any]] = []

    has_crlf = "\r\n" in text
    has_lone_lf = bool(re.search(r"(?<!\r)\n", text))
    if has_crlf and has_lone_lf:
        anomalies.append({"role": "mixed-line-endings"})

    commands: list[dict[str, Any]] = []
    current_name: lexer.Token | None = None
    current_args: list[lexer.Token] = []
    current_end = 0

    def flush() -> None:
        nonlocal current_name, current_args, current_end
        if current_name is None:
            return
        index = len(commands)
        start = current_name.offset
        end = current_end
        ledger.claim(start, end - start, "mapped-text", f"commands[{index}]")
        for tok in (current_name, *current_args):
            if tok.anomaly:
                anomalies.append(
                    {"role": tok.anomaly, "offset": tok.offset, "length": tok.length}
                )
        commands.append(
            {
                "index": index,
                "name": current_name.text,
                "args": [_token_dict(tok) for tok in current_args],
                "offset": start,
                "length": end - start,
            }
        )
        current_name, current_args = None, []

    for token in tokens:
        if token.kind == "whitespace":
            if "\n" in token.text:
                flush()
            if current_name is None:
                ledger.claim(token.offset, token.length, "omitted-proven", "whitespace")
            else:
                # No newline, and a command is still open: this whitespace sits between two of
                # its own tokens (claimed once `flush()` spans name..last-token) or, when nothing
                # else follows the command, trails it before a `//` comment or EOF -- a case
                # `flush()`'s own range would not otherwise reach. Extending `current_end` here
                # means `flush()` always claims through it either way, so it is never left over.
                current_end = token.end
            continue
        if token.kind == "comment":
            flush()
            ledger.claim(token.offset, token.length, "mapped-text", f"comments[{len(comments)}]")
            comments.append({"offset": token.offset, "text": token.text})
            continue
        if token.kind == "semicolon":
            if current_name is not None:
                current_end = token.end
                flush()
            else:
                ledger.claim(token.offset, token.length, "omitted-proven", "whitespace")
            continue
        # string token
        if current_name is None:
            current_name = token
            current_args = []
            current_end = token.end
        else:
            current_args.append(token)
            current_end = token.end
    flush()

    # -- typed projections, derived from commands[] without any further ledger claim --
    alias_names: set[str] = set()
    for command in commands:
        if command["name"].strip().lower() == "alias" and command["args"]:
            alias_names.add(command["args"][0]["text"].strip().lower())

    bindings: dict[str, dict[str, Any]] = {}
    aliases: dict[str, dict[str, Any]] = {}
    cvars: list[dict[str, Any]] = []
    script_expressions: list[dict[str, Any]] = []

    for command in commands:
        name = command["name"]
        folded = name.strip().lower()
        args = command["args"]
        index = command["index"]

        if folded not in ("bind", "alias"):
            # A `bind`/`alias` value is scanned once, below, through its own split into
            # sub-commands; scanning it again here would report the same text twice. A solo
            # `"__main__.foo()"` line names the expression in the command's own name, not an
            # argument, so the name is checked here too.
            if _looks_like_script(name):
                script_expressions.append({"origin": "command", "index": index, "text": name})
            for arg in args:
                if _looks_like_script(arg["text"]):
                    script_expressions.append(
                        {"origin": "command", "index": index, "text": arg["text"]}
                    )

        if folded == "bind" and len(args) >= 2:
            key, source_key = _translate_key(args[0]["text"])
            command_text = args[1]["text"]
            sub_commands = split_subcommands(command_text)
            for sub in sub_commands:
                if _looks_like_script(sub["name"]):
                    script_expressions.append(
                        {"origin": "binding", "index": index, "text": sub["name"]}
                    )
                for sub_arg in sub["args"]:
                    if _looks_like_script(sub_arg["text"]):
                        script_expressions.append(
                            {"origin": "binding", "index": index, "text": sub_arg["text"]}
                        )
            record = {
                "key": key,
                "sourceKey": source_key,
                "command": command_text,
                "commands": sub_commands,
                "commandIndex": index,
            }
            folded_key = key.lower()
            if folded_key in bindings:
                anomalies.append(
                    {"role": "repeated-bind", "offset": command["offset"], "key": key}
                )
            bindings[folded_key] = record
            continue

        if folded == "alias" and args:
            alias_name = args[0]["text"]
            body = args[1]["text"] if len(args) >= 2 else ""
            sub_commands = split_subcommands(body)
            uses: list[dict[str, Any]] = []
            seen_uses: set[str] = set()
            for sub in sub_commands:
                candidate = sub["name"].strip().lower()
                if candidate in alias_names and candidate not in seen_uses:
                    seen_uses.add(candidate)
                    uses.append({"name": sub["name"], "resolved": True})
                if _looks_like_script(sub["name"]):
                    script_expressions.append(
                        {"origin": "alias", "index": index, "text": sub["name"]}
                    )
                for sub_arg in sub["args"]:
                    if _looks_like_script(sub_arg["text"]):
                        script_expressions.append(
                            {"origin": "alias", "index": index, "text": sub_arg["text"]}
                        )
            record = {
                "name": alias_name,
                "body": body,
                "commands": sub_commands,
                "uses": uses,
                "commandIndex": index,
            }
            folded_name = alias_name.lower()
            if folded_name in aliases:
                anomalies.append(
                    {"role": "repeated-alias", "offset": command["offset"], "name": alias_name}
                )
            aliases[folded_name] = record
            continue

        if len(args) == 1 and folded not in _TYPED_VERBS and folded not in alias_names:
            cvars.append(
                {
                    "index": index,
                    "name": name,
                    "value": args[0]["text"],
                    "archived": True,
                }
            )

    ledger_row = ledger.finish()
    omissions: list[dict[str, Any]] = []
    if not data:
        omissions.append({"role": "empty-member"})
    omissions.extend(whitespace_omission(ledger_row, data))

    return {
        "commands": commands,
        "bindings": list(bindings.values()),
        "aliases": list(aliases.values()),
        "cvars": cvars,
        "scriptExpressions": script_expressions,
        "comments": comments,
        "anomalies": anomalies,
        "omissions": omissions,
        "ledgerRow": ledger_row,
    }
