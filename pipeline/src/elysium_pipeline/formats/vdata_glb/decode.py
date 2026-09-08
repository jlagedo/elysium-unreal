"""Decode one vdata unit: the KeyValues tree, the delimited rows, or a freeform line list.

Grammar selection: `experience_table.txt` is the one
pipe-delimited file; every other file is attempted as KeyValues. A file that tokenizes with no
`{`/`}` at all -- `credits.txt` and its variants, `masquerade.txt`, `charaction_sounds.txt` -- is
not KeyValues by any reading (a real vdata KeyValues file always wraps its data in at least one
root block), so it falls back to a `freeform` line list instead of raising on the first bare word
that opens no block; this third grammar is a spec deviation this decoder needed and documents.
"""

from __future__ import annotations

import bisect
from typing import Any, Callable

from elysium_pipeline.formats.vdata_glb import lexer, projection as projection_module
from elysium_pipeline.formats.vdata_glb.coverage import new_ledger
from elysium_pipeline.formats.vdata_glb.model import VdataModel, is_delimited
from elysium_pipeline.formats.vdata_glb.source import VdataSourceClosure

PathExists = Callable[[str], bool]
SoundGroupLookup = Callable[[str], list[str]]


class VdataDecodeError(RuntimeError):
    """The selected vdata source cannot be decoded."""


class _NotKeyValues(Exception):
    """Internal signal: the file tokenizes with no block structure at all."""


def _non_ascii_anomalies(data: bytes) -> list[dict[str, Any]]:
    return [{"role": "non-ascii-byte", "offset": offset, "byte": byte}
            for offset, byte in enumerate(data) if byte > 0x7F]


def _line_spans(text: str) -> list[tuple[int, int]]:
    """`(offset, end)` for every physical line, terminator included, gapless over `text`."""

    spans: list[tuple[int, int]] = []
    start, total = 0, len(text)
    while start < total:
        newline = text.find("\n", start)
        end = total if newline < 0 else newline + 1
        spans.append((start, end))
        start = end
    return spans


def _attach_comments(
    comment_tokens: list[lexer.Token], node_spans: list[tuple[int, int, str]], text: str
) -> list[dict[str, Any]]:
    """`comments[]`: every `//` comment, joined to the node it trails or precedes.

    A comment sharing its line with the token before it reads as documenting that token (the
    overwhelmingly common shape in the corpus: `"weight" "2" // added by wesp`); a comment that
    opens its own line reads as documenting whatever comes next. The vdata spec states only
    the second rule ("attached to the node they precede"); the first is this decoder's own
    resolution of the many trailing comments the spec's own examples show, recorded in
    `specDeviations`.
    """

    by_end = sorted(range(len(node_spans)), key=lambda i: node_spans[i][1])
    ends = [node_spans[i][1] for i in by_end]
    by_start = sorted(range(len(node_spans)), key=lambda i: node_spans[i][0])
    starts = [node_spans[i][0] for i in by_start]

    rows: list[dict[str, Any]] = []
    for index, token in enumerate(comment_tokens):
        row: dict[str, Any] = {
            "index": index,
            "text": token.raw,
            "offset": token.offset,
            "length": token.length,
            "attachment": "leading",
            "path": None,
        }
        position = bisect.bisect_right(ends, token.offset) - 1
        if position >= 0:
            candidate = node_spans[by_end[position]]
            if "\n" not in text[candidate[1]:token.offset]:
                row["attachment"] = "trailing"
                row["path"] = candidate[2]
                rows.append(row)
                continue
        position = bisect.bisect_left(starts, token.end)
        if position < len(starts):
            row["path"] = node_spans[by_start[position]][2]
        rows.append(row)
    return rows


def _decode_delimited(member) -> tuple[list[dict[str, Any]], list[dict[str, Any]], dict[str, Any]]:
    text = lexer.decode_text(member.data)
    ledger = new_ledger(member)
    rows: list[dict[str, Any]] = []
    for index, (start, end) in enumerate(_line_spans(text)):
        content = text[start:end].rstrip("\r\n")
        stripped = content.strip()
        if stripped.lower().startswith("> total experience value"):
            kind = "header"
        elif content.startswith(">"):
            kind = "comment"
        elif len(content) < 3:
            kind = "skipped"
        else:
            parts = content.split("|")
            if len(parts) >= 3:
                kind = "data"
            else:
                kind = "unparsed"
        row: dict[str, Any] = {"index": index, "kind": kind, "offset": start, "length": end - start}
        if kind == "data":
            parts = content.split("|")
            row["key"] = parts[0].strip()
            row["description"] = parts[1].strip()
            row["value"] = "|".join(parts[2:]).strip()
        else:
            row["text"] = content
        rows.append(row)
        ledger.claim(start, end - start, "mapped-text", f"rows[{index}]")
    return rows, _non_ascii_anomalies(member.data), ledger.finish()


def _decode_freeform(member) -> tuple[list[dict[str, Any]], list[dict[str, Any]], dict[str, Any]]:
    text = lexer.decode_text(member.data)
    ledger = new_ledger(member)
    rows: list[dict[str, Any]] = []
    for index, (start, end) in enumerate(_line_spans(text)):
        content = text[start:end].rstrip("\r\n")
        rows.append({"index": index, "kind": "line", "text": content, "offset": start, "length": end - start})
        ledger.claim(start, end - start, "mapped-text", f"rows[{index}]")
    return rows, _non_ascii_anomalies(member.data), ledger.finish()


def _decode_keyvalues(
    member,
) -> tuple[str | None, dict[str, Any], list[dict[str, Any]], list[dict[str, Any]],
           list[dict[str, Any]], list[dict], list[dict[str, Any]], dict[str, Any]]:
    """`(rootKey, tree, comments, anomalies, typedUnidentified, topNodes, omissions, ledgerRow)`."""

    text = lexer.decode_text(member.data)
    try:
        tokens = lexer.tokenize(text)
    except lexer.VdataLexError as error:
        raise VdataDecodeError(f"{member.path}: {error}") from error
    significant = [token for token in tokens if token.kind in ("string", "open", "close")]

    if significant and not lexer.has_block_structure(tokens):
        raise _NotKeyValues()

    if not significant:
        top_nodes, anomalies, claims, unparsed_offset = [], [], [], None
    else:
        top_nodes, anomalies, claims, unparsed_offset = lexer.parse_tree(tokens, text)

    ledger = new_ledger(member)
    for offset, length, state, owner in claims:
        ledger.claim(offset, length, state, owner)

    comment_tokens = [token for token in tokens if token.kind == "comment"]
    node_spans = [
        (node["offset"], node["offset"] + node["length"], path)
        for path, node in lexer.walk(top_nodes)
    ]
    comments = _attach_comments(comment_tokens, node_spans, text)
    for index, token in enumerate(comment_tokens):
        ledger.claim(token.offset, token.length, "mapped-text", f"comments[{index}]")

    whitespace_total = 0
    bom_total = 0
    for token in tokens:
        if token.kind == "whitespace":
            ledger.claim(token.offset, token.length, "omitted-proven", "whitespace")
            whitespace_total += token.length
        elif token.kind == "bom":
            ledger.claim(token.offset, token.length, "omitted-proven", "bom")
            bom_total += token.length

    omissions: list[dict[str, Any]] = []
    if whitespace_total:
        omissions.append({
            "role": "whitespace",
            "reason": "insignificant-keyvalues-separator-bytes",
            "byteLength": whitespace_total,
        })
    if bom_total:
        omissions.append({
            "role": "bom",
            "reason": "utf-8-byte-order-mark-prefix",
            "byteLength": bom_total,
        })

    root_key = top_nodes[0]["sourceKey"] if top_nodes else None
    tree = {"children": top_nodes, "unparsedOffset": unparsed_offset}

    ws_fix_anomalies: list[dict[str, Any]] = []
    for comment_row in comments:
        if comment_row["attachment"] != "trailing" or not comment_row["path"]:
            continue
        old_value = projection_module.ws_fix_value(comment_row["text"])
        if old_value is not None:
            ws_fix_anomalies.append(
                {"role": "commented-key", "path": comment_row["path"],
                 "value": old_value, "offset": comment_row["offset"]}
            )

    typed_unidentified = [
        {"field": node.get("name") or node["sourceKey"], "offset": node["offset"],
         "reason": "directive", "path": path}
        for path, node in lexer.walk(top_nodes)
        if node["kind"] == "directive"
    ]

    all_anomalies = anomalies + _non_ascii_anomalies(member.data) + ws_fix_anomalies
    return (
        root_key, tree, comments, all_anomalies, typed_unidentified, top_nodes, omissions,
        ledger.finish(),
    )


def decode_vdata(
    closure: VdataSourceClosure,
    *,
    resolve_model: PathExists | None = None,
    resolve_sound_group: SoundGroupLookup | None = None,
    resolve_asset: PathExists | None = None,
) -> VdataModel:
    """The complete vdata unit for one source closure."""

    member = closure.member

    if not member.data:
        empty_ledger = new_ledger(member).finish()
        return VdataModel(
            key=closure.key,
            asset=closure.asset,
            subtree=closure.subtree,
            variant=closure.variant,
            member=member,
            grammar="keyvalues",
            root_key=None,
            tree={"children": [], "unparsedOffset": None},
            rows=[],
            projection={"kind": "open", "vocabulary": "open", "rootKey": None,
                        "evidence": "empty-member", "sections": {}},
            comments=[],
            dependencies=[],
            anomalies=[],
            omissions=[{"reason": "empty-member"}],
            typed_unidentified=[],
            ledger_row=empty_ledger,
        )

    if is_delimited(closure.key):
        rows, anomalies, ledger_row = _decode_delimited(member)
        return VdataModel(
            key=closure.key,
            asset=closure.asset,
            subtree=closure.subtree,
            variant=closure.variant,
            member=member,
            grammar="delimited",
            root_key=None,
            tree=None,
            rows=rows,
            projection={"kind": "closed", "vocabulary": "closed", "rootKey": None,
                        "evidence": "docs/vtmb/game_runtime.md -> XP & leveling"},
            comments=[],
            dependencies=[],
            anomalies=anomalies,
            omissions=[],
            typed_unidentified=[],
            ledger_row=ledger_row,
        )

    try:
        (root_key, tree, comments, anomalies, typed_unidentified,
         top_nodes, omissions, ledger_row) = _decode_keyvalues(member)
    except _NotKeyValues:
        rows, anomalies, ledger_row = _decode_freeform(member)
        return VdataModel(
            key=closure.key,
            asset=closure.asset,
            subtree=closure.subtree,
            variant=closure.variant,
            member=member,
            grammar="freeform",
            root_key=None,
            tree=None,
            rows=rows,
            projection={"kind": "open", "vocabulary": "open", "rootKey": None,
                        "evidence": "no-block-structure"},
            comments=[],
            dependencies=[],
            anomalies=anomalies,
            omissions=[],
            typed_unidentified=[],
            ledger_row=ledger_row,
        )

    proj, dependencies, proj_typed_unidentified = projection_module.build_projection(
        root_key,
        top_nodes,
        resolve_model=resolve_model,
        resolve_sound_group=resolve_sound_group,
        resolve_asset=resolve_asset,
    )

    return VdataModel(
        key=closure.key,
        asset=closure.asset,
        subtree=closure.subtree,
        variant=closure.variant,
        member=member,
        grammar="keyvalues",
        root_key=root_key,
        tree=tree,
        rows=[],
        projection=proj,
        comments=comments,
        dependencies=dependencies,
        anomalies=anomalies,
        omissions=omissions,
        typed_unidentified=typed_unidentified + proj_typed_unidentified,
        ledger_row=ledger_row,
    )
