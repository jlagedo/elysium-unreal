"""The four line/row grammars `seam_map_ui_resource.md` names besides `keyvalues`.

`titles` (`scripts/titles.txt`), `settings-scr` (`scripts/settings.scr`), `tab-rows` and
`key-value-lines` (the `.lst` key-binding tables and `liblist.gam`), `line-list` (`rooms.lst`)
and `won-lists` (`woncomm.lst`) each carry their own line shape rather than the recursive
KeyValues tree, so each gets its own small, purpose-built scanner here. Every scanner claims
100% of its member's bytes -- gapless, one owner per range -- the same way `decode.py`'s
`keyvalues` grammar does, and never fabricates structure a malformed line does not support: an
unrecognized shape is claimed under an `unparsed`/`unidentified` owner and named in
`typedUnidentified` rather than dropped.
"""

from __future__ import annotations

import re
from typing import Any

from elysium_pipeline.formats.ui_resource_glb import lexer
from elysium_pipeline.formats.ui_resource_glb.coverage import new_ledger, omitted_proven_rows
from elysium_pipeline.formats.ui_resource_glb.model import (
    KEY_VALUE_LINE_KEYS,
    TAB_ROW_KEYS,
    UiResourceModel,
)
from elysium_pipeline.formats.ui_resource_glb.source import UiResourceSourceClosure

_KNOWN_DIRECTIVES = frozenset(
    {"position", "effect", "color", "color2", "fadein", "fadeout", "holdtime", "fxtime"}
)
_DIRECTIVE_PATTERN = re.compile(r"^([A-Za-z0-9_]+)\s*(.*)$")
_OPTION_TYPES = frozenset({"BOOL", "STRING", "NUMBER", "LIST"})


def _parse_numbers(text: str) -> list[float] | None:
    if not text.strip():
        return []
    values: list[float] = []
    for token in text.split():
        try:
            values.append(float(token))
        except ValueError:
            return None
    return values


# --------------------------------------------------------------------------------------------
# titles
# --------------------------------------------------------------------------------------------


def decode_titles(closure: UiResourceSourceClosure) -> UiResourceModel:
    member = closure.member
    text = lexer.decode_text(member.data, "latin-1")
    spans = lexer.line_spans(text)
    ledger = new_ledger(member)

    comments: list[dict[str, Any]] = []
    directives: list[dict[str, Any]] = []
    captions: list[dict[str, Any]] = []
    anomalies: list[dict[str, Any]] = []
    typed_unidentified: list[dict[str, Any]] = []
    state: dict[str, dict[str, Any]] = {}
    whitespace_total = 0

    index = 0
    total = len(spans)
    while index < total:
        start, end = spans[index]
        stripped = text[start:end].rstrip("\r\n").strip()
        if not stripped:
            ledger.claim(start, end - start, "omitted-proven", "whitespace")
            whitespace_total += end - start
            index += 1
            continue
        if stripped.startswith("//"):
            ledger.claim(start, end - start, "mapped-text", f"comments[{len(comments)}]")
            comments.append({"index": len(comments), "offset": start, "length": end - start,
                              "text": stripped})
            index += 1
            continue
        if stripped.startswith("$"):
            match = _DIRECTIVE_PATTERN.match(stripped[1:])
            name = (match.group(1) if match else stripped[1:]).lower()
            args = (match.group(2) if match else "").strip()
            ledger.claim(start, end - start, "mapped-text", f"titles.directives[{len(directives)}]")
            row = {"index": len(directives), "name": name, "args": args,
                   "values": _parse_numbers(args), "raw": stripped, "offset": start,
                   "length": end - start}
            directives.append(row)
            if name in _KNOWN_DIRECTIVES:
                state[name] = {"args": args, "values": row["values"], "offset": start}
            else:
                typed_unidentified.append(
                    {"reason": "unrecognized-directive", "name": name, "offset": start}
                )
            index += 1
            continue

        # A caption header: a bare identifier whose very next line opens `{`.
        name_offset = start
        following = spans[index + 1] if index + 1 < total else None
        following_text = (
            text[following[0]:following[1]].rstrip("\r\n").strip() if following else None
        )
        if following is None or following_text != "{":
            typed_unidentified.append(
                {"reason": "unrecognized-line", "text": stripped, "offset": start}
            )
            ledger.claim(start, end - start, "mapped-text", "titles.unparsed")
            index += 1
            continue

        caption_index = len(captions)
        ledger.claim(start, end - start, "mapped-text", f"titles.captions[{caption_index}].name")
        ledger.claim(following[0], following[1] - following[0], "mapped-text",
                     f"titles.captions[{caption_index}].open")
        cursor = index + 2
        text_lines: list[str] = []
        closed = False
        while cursor < total:
            line_start, line_end = spans[cursor]
            line_stripped = text[line_start:line_end].rstrip("\r\n").strip()
            if line_stripped == "}":
                ledger.claim(line_start, line_end - line_start, "mapped-text",
                             f"titles.captions[{caption_index}].close")
                closed = True
                cursor += 1
                break
            ledger.claim(line_start, line_end - line_start, "mapped-text",
                         f"titles.captions[{caption_index}].text[{len(text_lines)}]")
            text_lines.append(text[line_start:line_end].rstrip("\r\n"))
            cursor += 1
        if not closed:
            anomalies.append({"role": "unterminated-block", "offset": following[0]})
        captions.append(
            {
                "index": caption_index,
                "name": stripped,
                "text": "\n".join(text_lines),
                "directives": {name: dict(row) for name, row in state.items()},
                "offset": name_offset,
                "length": (spans[cursor - 1][1] if closed else following[1]) - name_offset,
            }
        )
        index = cursor

    anomalies.extend(lexer.source_anomalies(member.data, text, "latin-1"))

    return UiResourceModel(
        key=closure.key, asset=closure.asset, category=closure.category,
        grammar=closure.grammar, encoding=closure.encoding, member=member,
        tree=None, scheme=None, layout=None, menu=None, hud=None,
        titles={"directives": directives, "captions": captions},
        rows=None, substitutions=None, strings=None, options=None, menu_scene=None,
        dependencies=[], comments=comments, anomalies=anomalies, omissions=[],
        typed_unidentified=typed_unidentified, unresolved=[], unsupported=[],
        ledger_row=ledger.finish(),
        omitted_proven=omitted_proven_rows(whitespace_total),
        dormant=closure.dormant, dormant_evidence=closure.dormant_evidence,
    )


# --------------------------------------------------------------------------------------------
# settings-scr
# --------------------------------------------------------------------------------------------


def decode_settings_scr(closure: UiResourceSourceClosure) -> UiResourceModel:
    member = closure.member
    text = lexer.decode_text(member.data, "latin-1")
    tokens = lexer.tokenize(text, "latin-1", escape_quotes=False)
    significant = [token for token in tokens if token.kind in ("string", "open", "close")]

    ledger = new_ledger(member)
    comments: list[dict[str, Any]] = []
    whitespace_total = 0
    for token in tokens:
        if token.kind == "comment":
            ledger.claim(token.offset, token.length, "mapped-text", f"comments[{len(comments)}]")
            comments.append({"index": len(comments), "offset": token.offset,
                              "length": token.length, "text": token.raw})
        elif token.kind == "whitespace":
            ledger.claim(token.offset, token.length, "omitted-proven", "whitespace")
            whitespace_total += token.length
        elif token.kind == "bom":
            ledger.claim(token.offset, token.length, "mapped", "bom")

    anomalies: list[dict[str, Any]] = list(lexer.source_anomalies(member.data, text, "latin-1"))
    typed_unidentified: list[dict[str, Any]] = []

    def claim(token, owner: str) -> None:
        ledger.claim(token.offset, token.length, "mapped-text", owner)

    def is_word(position: int, word: str) -> bool:
        return (
            position < len(significant)
            and significant[position].kind == "string"
            and not significant[position].quoted
            and significant[position].raw.upper() == word
        )

    cursor = 0
    n = len(significant)
    version = None
    if is_word(cursor, "VERSION"):
        claim(significant[cursor], "options.versionKeyword")
        cursor += 1
        if cursor < n and significant[cursor].kind == "string":
            version = significant[cursor].raw
            claim(significant[cursor], "options.version")
            cursor += 1
    else:
        typed_unidentified.append({"reason": "missing-version", "offset": 0})

    sections: list[dict[str, Any]] = []
    while cursor < n and is_word(cursor, "DESCRIPTION"):
        section_index = len(sections)
        claim(significant[cursor], f"options.sections[{section_index}].keyword")
        cursor += 1
        name = None
        if cursor < n and significant[cursor].kind == "string":
            name = significant[cursor].raw
            claim(significant[cursor], f"options.sections[{section_index}].name")
            cursor += 1
        if cursor >= n or significant[cursor].kind != "open":
            anomalies.append({"role": "unterminated-block", "offset": significant[cursor - 1].end})
            break
        claim(significant[cursor], f"options.sections[{section_index}].open")
        cursor += 1

        options: list[dict[str, Any]] = []
        while cursor < n and significant[cursor].kind == "string":
            option_index = len(options)
            prefix = f"options.sections[{section_index}].options[{option_index}]"
            cvar_token = significant[cursor]
            claim(cvar_token, f"{prefix}.cvar")
            cursor += 1
            if cursor >= n or significant[cursor].kind != "open":
                anomalies.append({"role": "unterminated-block", "offset": cvar_token.offset})
                break
            claim(significant[cursor], f"{prefix}.open")
            cursor += 1

            prompt = None
            if cursor < n and significant[cursor].kind == "string":
                prompt = significant[cursor].raw
                claim(significant[cursor], f"{prefix}.prompt")
                cursor += 1

            option_type = None
            type_info: list[str] = []
            if cursor < n and significant[cursor].kind == "open":
                claim(significant[cursor], f"{prefix}.type.open")
                cursor += 1
                if cursor < n and significant[cursor].kind == "string" and not significant[cursor].quoted:
                    option_type = significant[cursor].raw.upper()
                    claim(significant[cursor], f"{prefix}.type.word")
                    cursor += 1
                while cursor < n and significant[cursor].kind == "string":
                    claim(significant[cursor], f"{prefix}.type.info[{len(type_info)}]")
                    type_info.append(significant[cursor].raw)
                    cursor += 1
                if cursor < n and significant[cursor].kind == "close":
                    claim(significant[cursor], f"{prefix}.type.close")
                    cursor += 1
                else:
                    anomalies.append({"role": "unterminated-block", "offset": cvar_token.offset})

            default_value = None
            if cursor < n and significant[cursor].kind == "open":
                claim(significant[cursor], f"{prefix}.default.open")
                cursor += 1
                if cursor < n and significant[cursor].kind == "string":
                    default_value = significant[cursor].raw
                    claim(significant[cursor], f"{prefix}.default.value")
                    cursor += 1
                if cursor < n and significant[cursor].kind == "close":
                    claim(significant[cursor], f"{prefix}.default.close")
                    cursor += 1
                else:
                    anomalies.append({"role": "unterminated-block", "offset": cvar_token.offset})

            if cursor < n and significant[cursor].kind == "close":
                claim(significant[cursor], f"{prefix}.close")
                cursor += 1
            else:
                anomalies.append({"role": "unterminated-block", "offset": cvar_token.offset})

            type_details: dict[str, Any] = {"raw": type_info}
            if option_type == "NUMBER" and len(type_info) >= 2:
                try:
                    type_details["min"] = float(type_info[0])
                    type_details["max"] = float(type_info[1])
                except ValueError:
                    pass
            elif option_type == "LIST":
                type_details["options"] = [
                    {"label": type_info[i], "value": type_info[i + 1]}
                    for i in range(0, len(type_info) - 1, 2)
                ]
            if option_type not in _OPTION_TYPES:
                typed_unidentified.append(
                    {"reason": "unknown-option-type", "cvar": cvar_token.raw, "value": option_type,
                     "offset": cvar_token.offset}
                )
            options.append(
                {
                    "cvar": cvar_token.raw, "prompt": prompt, "type": option_type,
                    "typeInfo": type_details, "default": default_value,
                    "offset": cvar_token.offset,
                }
            )

        if cursor < n and significant[cursor].kind == "close":
            claim(significant[cursor], f"options.sections[{section_index}].close")
            cursor += 1
        else:
            anomalies.append({"role": "unterminated-block", "offset": significant[cursor - 1].end if cursor else 0})
        sections.append({"name": name, "options": options})

    while cursor < n:
        claim(significant[cursor], "options.unparsed")
        typed_unidentified.append({"reason": "unparsed-tail", "offset": significant[cursor].offset})
        cursor += 1

    return UiResourceModel(
        key=closure.key, asset=closure.asset, category=closure.category,
        grammar=closure.grammar, encoding=closure.encoding, member=member,
        tree=None, scheme=None, layout=None, menu=None, hud=None, titles=None, rows=None,
        substitutions=None, strings=None,
        options={"version": version, "sections": sections},
        menu_scene=None, dependencies=[], comments=comments, anomalies=anomalies, omissions=[],
        typed_unidentified=typed_unidentified, unresolved=[], unsupported=[],
        ledger_row=ledger.finish(),
        omitted_proven=omitted_proven_rows(whitespace_total),
        dormant=closure.dormant, dormant_evidence=closure.dormant_evidence,
    )


# --------------------------------------------------------------------------------------------
# tab-rows / key-value-lines
# --------------------------------------------------------------------------------------------

_KEY_ROW_KEYS = frozenset({"scripts/kb_keys.lst", "scripts/kb_trans.lst"})


def _typed_row_fields(key: str, cells: list[dict[str, Any]]) -> dict[str, Any]:
    if key in KEY_VALUE_LINE_KEYS:
        return {
            "entryKey": cells[0]["text"] if cells else None,
            "entryValue": cells[1]["text"] if len(cells) > 1 else None,
        }
    if key in _KEY_ROW_KEYS:
        keynum = None
        if cells:
            try:
                keynum = int(cells[0]["text"])
            except ValueError:
                keynum = None
        return {
            "keynum": keynum,
            "name1": cells[1]["text"] if len(cells) > 1 else None,
            "name2": cells[2]["text"] if len(cells) > 2 else None,
            "colorToken": cells[3]["text"] if len(cells) > 3 else None,
        }
    if key in TAB_ROW_KEYS:
        return {
            "action": cells[0]["text"] if cells else None,
            "description": cells[1]["text"] if len(cells) > 1 else None,
        }
    return {}


def decode_row_grammar(closure: UiResourceSourceClosure) -> UiResourceModel:
    member = closure.member
    text = lexer.decode_text(member.data, closure.encoding)
    tokens = lexer.tokenize(text, closure.encoding, escape_quotes=False)
    # `lexer.line_spans` indexes `text`, i.e. one Python character per source unit; every token's
    # own offset is already in source *bytes* (`lexer.tokenize` scales it for a UTF-16 member).
    # Scaling the line spans the same way keeps both coordinate systems the same for
    # `scripts/kb_trans.lst`, the one UTF-16 LE member this grammar decodes -- without it, a
    # byte offset compared against a code-unit span misattributes every row past the BOM.
    unit_width = 2 if closure.encoding == "utf-16-le" else 1
    spans = [(start * unit_width, end * unit_width) for start, end in lexer.line_spans(text)]
    ledger = new_ledger(member)

    comments: list[dict[str, Any]] = []
    whitespace_total = 0
    for token in tokens:
        if token.kind == "comment":
            ledger.claim(token.offset, token.length, "mapped-text", f"comments[{len(comments)}]")
            comments.append({"index": len(comments), "offset": token.offset,
                              "length": token.length, "text": token.raw})
        elif token.kind == "whitespace":
            ledger.claim(token.offset, token.length, "omitted-proven", "whitespace")
            whitespace_total += token.length
        elif token.kind == "bom":
            ledger.claim(token.offset, token.length, "mapped", "bom")

    string_tokens = [token for token in tokens if token.kind == "string"]
    grouped = lexer.group_tokens_by_line(string_tokens, spans)

    entries: list[dict[str, Any]] = []
    for span, line_tokens in zip(spans, grouped):
        if not line_tokens:
            # A blank line or a comment-only line: every byte in `span` is already claimed above
            # (whitespace/comment tokens are claimed unconditionally, independent of grouping),
            # so publishing a row here would fabricate a `cells: []` entry the source never
            # authored (seam_map_unit_contract.md, "Non-canonical storage").
            continue
        index = len(entries)
        cells = []
        for token in line_tokens:
            ledger.claim(token.offset, token.length, "mapped-text",
                         f"rows.entries[{index}].cells[{len(cells)}]")
            cells.append(
                {
                    "text": token.decoded() if token.quoted else token.raw,
                    "quoted": token.quoted,
                    "offset": token.offset,
                    "length": token.length,
                }
            )
        entry = {"index": index, "offset": span[0], "length": span[1] - span[0], "cells": cells}
        entry.update(_typed_row_fields(closure.key, cells))
        entries.append(entry)

    anomalies = lexer.source_anomalies(member.data, text, closure.encoding)

    return UiResourceModel(
        key=closure.key, asset=closure.asset, category=closure.category,
        grammar=closure.grammar, encoding=closure.encoding, member=member,
        tree=None, scheme=None, layout=None, menu=None, hud=None, titles=None,
        rows={"columns": None, "entries": entries},
        substitutions=None, strings=None, options=None, menu_scene=None,
        dependencies=[], comments=comments, anomalies=anomalies, omissions=[],
        typed_unidentified=[], unresolved=[], unsupported=[],
        ledger_row=ledger.finish(),
        omitted_proven=omitted_proven_rows(whitespace_total),
        dormant=closure.dormant, dormant_evidence=closure.dormant_evidence,
    )


# --------------------------------------------------------------------------------------------
# line-list
# --------------------------------------------------------------------------------------------


def decode_line_list(closure: UiResourceSourceClosure) -> UiResourceModel:
    """Plain text lines, one room name per line -- except a `//` line, which is a comment, not a
    row (`seam_map_ui_resource.md`'s "Byte ledger owners" names `comments[i]` as the owner of one
    `//` comment, whatever grammar the member is in)."""

    member = closure.member
    text = lexer.decode_text(member.data, "latin-1")
    spans = lexer.line_spans(text)
    ledger = new_ledger(member)
    entries = []
    comments: list[dict[str, Any]] = []
    whitespace_total = 0
    for start, end in spans:
        stripped = text[start:end].rstrip("\r\n").strip()
        if not stripped:
            # A blank line names no room: claim it as ordinary insignificant whitespace rather
            # than fabricating a `{"text": ""}` row the source never authored
            # (seam_map_unit_contract.md, "Non-canonical storage").
            ledger.claim(start, end - start, "omitted-proven", "whitespace")
            whitespace_total += end - start
            continue
        if stripped.startswith("//"):
            ledger.claim(start, end - start, "mapped-text", f"comments[{len(comments)}]")
            comments.append({"index": len(comments), "offset": start, "length": end - start,
                              "text": stripped})
            continue
        ledger.claim(start, end - start, "mapped-text", f"rows.entries[{len(entries)}]")
        entries.append(
            {"index": len(entries), "text": text[start:end].rstrip("\r\n"), "offset": start,
             "length": end - start}
        )
    anomalies = lexer.source_anomalies(member.data, text, "latin-1")
    return UiResourceModel(
        key=closure.key, asset=closure.asset, category=closure.category,
        grammar=closure.grammar, encoding=closure.encoding, member=member,
        tree=None, scheme=None, layout=None, menu=None, hud=None, titles=None,
        rows={"columns": None, "entries": entries},
        substitutions=None, strings=None, options=None, menu_scene=None,
        dependencies=[], comments=comments, anomalies=anomalies, omissions=[],
        typed_unidentified=[], unresolved=[], unsupported=[],
        ledger_row=ledger.finish(),
        omitted_proven=omitted_proven_rows(whitespace_total),
        dormant=closure.dormant, dormant_evidence=closure.dormant_evidence,
    )


# --------------------------------------------------------------------------------------------
# won-lists
# --------------------------------------------------------------------------------------------


def decode_won_lists(closure: UiResourceSourceClosure) -> UiResourceModel:
    member = closure.member
    text = lexer.decode_text(member.data, "latin-1")
    tokens = lexer.tokenize(text, "latin-1", escape_quotes=False)
    significant = [token for token in tokens if token.kind in ("string", "open", "close")]
    ledger = new_ledger(member)

    comments: list[dict[str, Any]] = []
    whitespace_total = 0
    for token in tokens:
        if token.kind == "comment":
            ledger.claim(token.offset, token.length, "mapped-text", f"comments[{len(comments)}]")
            comments.append({"index": len(comments), "offset": token.offset,
                              "length": token.length, "text": token.raw})
        elif token.kind == "whitespace":
            ledger.claim(token.offset, token.length, "omitted-proven", "whitespace")
            whitespace_total += token.length
        elif token.kind == "bom":
            ledger.claim(token.offset, token.length, "mapped", "bom")

    entries: list[dict[str, Any]] = []
    anomalies: list[dict[str, Any]] = list(lexer.source_anomalies(member.data, text, "latin-1"))
    typed_unidentified: list[dict[str, Any]] = []
    cursor = 0
    n = len(significant)
    while cursor < n:
        name_token = significant[cursor]
        entry_index = len(entries)
        if name_token.kind != "string":
            typed_unidentified.append({"reason": "unexpected-token", "offset": name_token.offset})
            ledger.claim(name_token.offset, name_token.length, "mapped-text", "rows.unparsed")
            cursor += 1
            continue
        if cursor + 1 >= n or significant[cursor + 1].kind != "open":
            ledger.claim(name_token.offset, name_token.length, "mapped-text",
                         f"rows.entries[{entry_index}].name")
            typed_unidentified.append(
                {"reason": "block-name-without-block", "name": name_token.raw,
                 "offset": name_token.offset}
            )
            entries.append(
                {"index": entry_index, "name": name_token.raw, "servers": [],
                 "offset": name_token.offset, "length": name_token.length}
            )
            cursor += 1
            continue

        ledger.claim(name_token.offset, name_token.length, "mapped-text",
                     f"rows.entries[{entry_index}].name")
        open_token = significant[cursor + 1]
        ledger.claim(open_token.offset, open_token.length, "mapped-text",
                     f"rows.entries[{entry_index}].open")
        cursor += 2
        servers: list[dict[str, Any]] = []
        while cursor < n and significant[cursor].kind == "string":
            server_token = significant[cursor]
            ledger.claim(server_token.offset, server_token.length, "mapped-text",
                         f"rows.entries[{entry_index}].servers[{len(servers)}]")
            servers.append(
                {"address": server_token.raw, "offset": server_token.offset,
                 "length": server_token.length}
            )
            cursor += 1
        if cursor < n and significant[cursor].kind == "close":
            close_token = significant[cursor]
            ledger.claim(close_token.offset, close_token.length, "mapped-text",
                         f"rows.entries[{entry_index}].close")
            end = close_token.end
            cursor += 1
        else:
            anomalies.append({"role": "unterminated-block", "offset": open_token.offset})
            end = servers[-1]["offset"] + servers[-1]["length"] if servers else open_token.end
        entries.append(
            {"index": entry_index, "name": name_token.raw, "servers": servers,
             "offset": name_token.offset, "length": end - name_token.offset}
        )

    return UiResourceModel(
        key=closure.key, asset=closure.asset, category=closure.category,
        grammar=closure.grammar, encoding=closure.encoding, member=member,
        tree=None, scheme=None, layout=None, menu=None, hud=None, titles=None,
        rows={"columns": None, "entries": entries},
        substitutions=None, strings=None, options=None, menu_scene=None,
        dependencies=[], comments=comments, anomalies=anomalies, omissions=[],
        typed_unidentified=typed_unidentified, unresolved=[], unsupported=[],
        ledger_row=ledger.finish(),
        omitted_proven=omitted_proven_rows(whitespace_total),
        dormant=closure.dormant, dormant_evidence=closure.dormant_evidence,
    )
