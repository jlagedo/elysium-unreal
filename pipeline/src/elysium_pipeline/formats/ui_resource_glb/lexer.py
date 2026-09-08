"""Offset-carrying KeyValues lexer and tree parser for the ui-resource seam.

The tokenizer and recursive tree builder are copied from `formats/vdata_glb/lexer.py`, borrowing
the KeyValues tree shape from the vdata spec:
`//` outside a quoted string starts a line comment, `"` quotes a string that may span lines and
may carry `\\"`, `{`/`}` nest to arbitrary depth, and anything else runs to the next whitespace,
brace, quote or comment. A malformed file degrades the same way: the tree stands up to the point
the walk can no longer attribute a token to it, and everything from there to end of file is one
`unparsed` region the decoder claims verbatim.

Every offset is in *source bytes*. For a Latin-1 member a character is a byte, so a text offset
and a byte offset are the same number; for the two UTF-16 LE members `tokenize` widens every
offset `_tokenize_units` returns by a factor of two, since one Python character is one UTF-16
code unit (two bytes) for the BMP text this seam's members carry.

A `Token.escapes` entry stays in *character* units, matching `Token.raw`'s own indexing, whatever
the source encoding is; only offsets stated outside the token (`Token.offset`, `Token.length`)
are in source bytes.
"""

from __future__ import annotations

from dataclasses import dataclass

BOM = "\xef\xbb\xbf"
UTF16_BOM = b"\xff\xfe"
_WHITESPACE = " \t\r\n\v\f"
_BARE_END = set(' \t\r\n\v\f{}"')

Claim = tuple[int, int, str, str]


class UiResourceLexError(ValueError):
    """The byte stream cannot be tokenized at all (defensive)."""


def decode_escapes(raw: str, escapes: list[int] | tuple[int, ...]) -> str:
    """`raw` (as spelled, escapes intact) with every `\\"` collapsed to `"`."""

    if not escapes:
        return raw
    escape_set = set(escapes)
    out: list[str] = []
    index = 0
    while index < len(raw):
        if index in escape_set:
            out.append('"')
            index += 2
            continue
        out.append(raw[index])
        index += 1
    return "".join(out)


@dataclass(frozen=True, slots=True)
class Token:
    kind: str          # "string" | "open" | "close" | "comment" | "whitespace" | "bom"
    raw: str            # the literal source slice: quotes stripped, escapes NOT collapsed
    offset: int         # byte offset of the token's first byte
    length: int         # byte length of the whole token span, quotes and escapes included
    quoted: bool = False
    escapes: tuple[int, ...] = ()   # offsets within `raw` where a `\"` sequence begins
    anomaly: str = ""    # a named departure from the grammar, or "" for a well-formed token

    @property
    def end(self) -> int:
        return self.offset + self.length

    def decoded(self) -> str:
        return decode_escapes(self.raw, self.escapes)


def decode_text(data: bytes, encoding: str) -> str:
    """One character per *code unit* for the encoding named; offsets stay in source bytes.

    `latin-1` is one byte per character, so a character offset is already a byte offset. For
    `utf-16-le` every character the caller works with here is one UTF-16 code unit widened back to
    its two-byte offset by `tokenize`, not by this function -- `tokenize` branches on `encoding`
    and multiplies every offset it emits by two, so this function can stay a plain decode.
    """

    if encoding == "utf-16-le":
        return data.decode("utf-16-le")
    return data.decode("latin-1")


def _tokenize_units(text: str, unit_width: int, *, escape_quotes: bool) -> list[Token]:
    """Tokenize `text` from its first character; offsets are stated in units of `unit_width`
    bytes each. The caller has already stripped and separately claimed any byte-order mark.

    `escape_quotes` is the Source KeyValues `\\"` convention -- true for the `keyvalues` grammar's
    own files. The row/line grammars (`tab-rows`, `key-value-lines`, `won-lists`) never author
    that escape, and `kb_keys.lst`/`kb_trans.lst` name the literal backslash key as `"\\"`: read
    with escaping on, that `\\"` reads as an escaped quote rather than a one-character value
    closed by the very next quote, and every quoted cell after it in the file mis-tokenizes. This
    is a spec deviation (the seam names one KeyValues-shaped tree for every grammar) the decoder
    needed against real data, recorded in `specDeviations`.
    """

    tokens: list[Token] = []
    index, total = 0, len(text)
    while index < total:
        char = text[index]
        if char in _WHITESPACE:
            start = index
            while index < total and text[index] in _WHITESPACE:
                index += 1
            tokens.append(
                Token("whitespace", text[start:index], start * unit_width, (index - start) * unit_width)
            )
        elif char == "/" and text[index + 1:index + 2] == "/":
            start = index
            stop = text.find("\n", index)
            index = total if stop < 0 else stop
            tokens.append(
                Token("comment", text[start:index], start * unit_width, (index - start) * unit_width)
            )
        elif char == '"':
            start = index
            content_start = index + 1
            index = content_start
            escapes: list[int] = []
            closed = False
            while index < total:
                if escape_quotes and text[index] == "\\" and text[index + 1:index + 2] == '"':
                    escapes.append(index - content_start)
                    index += 2
                    continue
                if text[index] == '"':
                    raw = text[content_start:index]
                    index += 1
                    closed = True
                    break
                index += 1
            else:
                raw = text[content_start:index]
            anomaly = "" if closed else "unterminated-quoted-string"
            # `escapes` indexes `raw`, a character string, in character units -- not scaled by
            # `unit_width` -- so `decode_escapes` (which walks `raw` by character index) reads a
            # UTF-16 token's escapes correctly.
            tokens.append(
                Token("string", raw, start * unit_width, (index - start) * unit_width, True,
                      tuple(escapes), anomaly)
            )
        elif char in "{}":
            kind = "open" if char == "{" else "close"
            tokens.append(Token(kind, char, index * unit_width, unit_width))
            index += 1
        else:
            start = index
            while (
                index < total
                and text[index] not in _BARE_END
                and text[index:index + 2] != "//"
            ):
                index += 1
            if index == start:
                raise UiResourceLexError(f"lexer stalled at unit {start}")
            tokens.append(Token("string", text[start:index], start * unit_width, (index - start) * unit_width))
    return tokens


def tokenize(text: str, encoding: str = "latin-1", *, escape_quotes: bool = True) -> list[Token]:
    """Every unit of `text` lands in exactly one token, offsets stated in source bytes.

    `text` is what `decode_text` returned: one Python character per byte for Latin-1, one per
    UTF-16 code unit for `utf-16-le`. A leading byte-order mark is claimed as its own `bom` token
    -- one code unit (two bytes) for UTF-16, three literal bytes for the UTF-8 BOM the Latin-1
    files occasionally carry -- and every following offset is shifted past it. `escape_quotes`
    is documented on `_tokenize_units`.
    """

    unit_width = 2 if encoding == "utf-16-le" else 1
    bom_text = "﻿" if encoding == "utf-16-le" else BOM
    has_bom = text.startswith(bom_text)
    tokens: list[Token] = []
    body = text
    shift = 0
    if has_bom:
        shift = len(bom_text) * unit_width
        tokens.append(Token("bom", bom_text, 0, shift))
        body = text[len(bom_text):]
    tokens.extend(
        _offset_shift(_tokenize_units(body, unit_width, escape_quotes=escape_quotes), shift)
    )
    return tokens


def _offset_shift(tokens: list[Token], shift: int) -> list[Token]:
    if not shift:
        return tokens
    return [
        Token(t.kind, t.raw, t.offset + shift, t.length, t.quoted, t.escapes, t.anomaly)
        for t in tokens
    ]


class _State:
    __slots__ = ("text", "unparsed_offset", "anomalies", "claims", "directive_count")

    def __init__(self, text: str) -> None:
        self.text = text
        self.unparsed_offset: int | None = None
        self.anomalies: list[dict] = []
        self.claims: list[Claim] = []
        self.directive_count = 0

    def fail(self, offset: int) -> None:
        if self.unparsed_offset is None:
            self.unparsed_offset = offset
            self.anomalies.append({"role": "unterminated-block", "offset": offset})
            # `parse_tree` re-claims every significant token at or past `unparsed_offset` as
            # `tree.unparsed` once parsing gives up. Any claim already recorded for a token in
            # that range -- the failing block's own key/braces, or a descendant that a nested
            # call claimed before the failure bubbled up -- would collide with that sweep, so
            # drop it here rather than let the caller re-add an overlapping range.
            self.claims = [claim for claim in self.claims if claim[0] < offset]

    def same_line(self, end: int, start: int) -> bool:
        return "\n" not in self.text[end:start]


def _dotted(path: tuple[int, ...]) -> str:
    return ".".join(str(part) for part in path)


def _directive_name(token: Token) -> str | None:
    if token.quoted or not token.raw.startswith("#") or len(token.raw) < 2:
        return None
    return token.raw[1:].lower()


def _make_scalar(index: int, key: Token, value: Token | None) -> dict:
    return {
        "index": index,
        "kind": "scalar",
        "key": key.decoded().strip().lower(),
        "sourceKey": key.decoded(),
        "quotedKey": key.quoted,
        "value": "" if value is None else value.raw,
        "quotedValue": bool(value is not None and value.quoted),
        "escapes": list(value.escapes) if value is not None else [],
        "offset": key.offset,
        "length": (value.end if value is not None else key.end) - key.offset,
    }


def _make_block(index: int, key: Token, open_token: Token, close_token: Token | None,
                 children: list[dict]) -> dict:
    return {
        "index": index,
        "kind": "block",
        "key": key.decoded().strip().lower(),
        "sourceKey": key.decoded(),
        "quotedKey": key.quoted,
        "children": children,
        "offset": key.offset,
        "length": (close_token.end if close_token is not None else open_token.end) - key.offset,
    }


def _make_directive(index: int, key: Token, argument: Token | None) -> dict:
    return {
        "index": index,
        "kind": "directive",
        "name": _directive_name(key),
        "sourceKey": key.raw,
        "argument": None if argument is None else argument.raw,
        "offset": key.offset,
        "length": (argument.end if argument is not None else key.end) - key.offset,
    }


def _extend_bare_value(node: dict, extra: Token, state: _State, path: str) -> None:
    node["value"] = f"{node['value']} {extra.raw}"
    node["length"] = extra.end - node["offset"]
    state.claims.append((extra.offset, extra.length, "mapped-text", f"tree.{path}.value"))
    state.anomalies.append(
        {"role": "unquoted-token-with-space", "offset": extra.offset, "path": path}
    )


def _parse_children(
    significant: list[Token],
    cursor: int,
    path: tuple[int, ...],
    state: _State,
    *,
    top_level: bool,
) -> tuple[list[dict], int, Token | None]:
    children: list[dict] = []
    seen_scalar_keys: set[str] = set()
    while cursor < len(significant):
        if state.unparsed_offset is not None:
            return children, cursor, None
        token = significant[cursor]
        if token.kind == "close":
            if top_level:
                state.anomalies.append({"role": "unterminated-block", "offset": token.offset})
                state.claims.append((token.offset, token.length, "mapped-text", "tree.unparsed"))
                cursor += 1
                continue
            return children, cursor + 1, token
        if token.kind == "open":
            state.fail(token.offset)
            return children, cursor, None

        key_token = token
        following = significant[cursor + 1] if cursor + 1 < len(significant) else None
        child_index = len(children)
        child_path = path + (child_index,)
        child_path_str = _dotted(child_path)

        directive_name = _directive_name(key_token)
        if directive_name is not None:
            argument = following if (following is not None and following.kind == "string") else None
            children.append(_make_directive(child_index, key_token, argument))
            directive_index = state.directive_count
            state.directive_count += 1
            # Two narrow claims -- the directive word, then its argument -- rather than one span
            # merging both: the whitespace between them is ordinary insignificant whitespace and
            # stays for the generic whitespace pass, the same split a scalar's key/value get.
            state.claims.append(
                (key_token.offset, key_token.length, "mapped-text",
                 f"tree.directives[{directive_index}].name")
            )
            if argument is not None:
                state.claims.append(
                    (argument.offset, argument.length, "mapped-text",
                     f"tree.directives[{directive_index}].argument")
                )
            cursor += 2 if argument is not None else 1
            continue

        if following is not None and following.kind == "open":
            grandchildren, next_cursor, close_token = _parse_children(
                significant, cursor + 2, child_path, state, top_level=False
            )
            children.append(_make_block(child_index, key_token, following, close_token, grandchildren))
            state.claims.append(
                (key_token.offset, key_token.length, "mapped-text", f"tree.{child_path_str}.key")
            )
            state.claims.append(
                (following.offset, following.length, "mapped-text", f"tree.{child_path_str}.braces")
            )
            if close_token is None:
                state.fail(key_token.offset)
                return children, next_cursor, None
            state.claims.append(
                (close_token.offset, close_token.length, "mapped-text",
                 f"tree.{child_path_str}.braces")
            )
            cursor = next_cursor
            continue

        if following is not None and following.kind == "string":
            folded = key_token.decoded().strip().lower()
            if folded in seen_scalar_keys:
                state.anomalies.append(
                    {"role": "repeated-key", "offset": key_token.offset,
                     "key": folded, "path": child_path_str}
                )
            seen_scalar_keys.add(folded)
            children.append(_make_scalar(child_index, key_token, following))
            state.claims.append(
                (key_token.offset, key_token.length, "mapped-text", f"tree.{child_path_str}.key")
            )
            state.claims.append(
                (following.offset, following.length, "mapped-text",
                 f"tree.{child_path_str}.value")
            )
            cursor += 2
            continue

        if (
            not key_token.quoted
            and children
            and children[-1]["kind"] == "scalar"
            and not children[-1]["quotedValue"]
            and children[-1]["value"] != ""
            and state.same_line(children[-1]["offset"] + children[-1]["length"], key_token.offset)
        ):
            _extend_bare_value(children[-1], key_token, state, _dotted(path + (len(children) - 1,)))
            cursor += 1
            continue

        state.anomalies.append(
            {"role": "valueless-key", "offset": key_token.offset, "path": child_path_str}
        )
        children.append(_make_scalar(child_index, key_token, None))
        state.claims.append(
            (key_token.offset, key_token.length, "mapped-text", f"tree.{child_path_str}.key")
        )
        cursor += 1

    return children, cursor, None


def parse_tree(tokens: list[Token], text: str) -> tuple[list[dict], list[dict], list[Claim], int | None]:
    significant = [token for token in tokens if token.kind in ("string", "open", "close")]
    state = _State(text)
    children, cursor, _ = _parse_children(significant, 0, (), state, top_level=True)
    unparsed_offset = state.unparsed_offset
    if unparsed_offset is None and cursor < len(significant):
        unparsed_offset = significant[cursor].offset
        state.anomalies.append({"role": "unterminated-block", "offset": unparsed_offset})
    if unparsed_offset is not None:
        for token in significant:
            if token.offset >= unparsed_offset:
                state.claims.append((token.offset, token.length, "mapped-text", "tree.unparsed"))
    return children, state.anomalies, state.claims, unparsed_offset


def walk(nodes: list[dict], path: tuple[int, ...] = ()):
    for node in nodes:
        child_path = path + (node["index"],)
        yield _dotted(child_path), node
        if node["kind"] == "block":
            yield from walk(node["children"], child_path)


def line_spans(text: str) -> list[tuple[int, int]]:
    """`(offset, end)` for every physical line, terminator included, gapless over `text`."""

    spans: list[tuple[int, int]] = []
    start, total = 0, len(text)
    while start < total:
        newline = text.find("\n", start)
        end = total if newline < 0 else newline + 1
        spans.append((start, end))
        start = end
    return spans


def line_of(offset: int, spans: list[tuple[int, int]]) -> int:
    """The index of the physical line containing `offset`."""

    for index, (start, end) in enumerate(spans):
        if start <= offset < end:
            return index
    return len(spans) - 1 if spans else 0


def group_tokens_by_line(
    tokens: list[Token], spans: list[tuple[int, int]]
) -> list[list[Token]]:
    """Every `string`/`comment` token, bucketed by the physical line it starts on."""

    rows: list[list[Token]] = [[] for _ in spans]
    for token in tokens:
        if token.kind not in ("string", "comment"):
            continue
        rows[line_of(token.offset, spans)].append(token)
    return rows


def mixed_line_ending_offset(text: str) -> int | None:
    """The character offset of the first line terminator that breaks a consistent `\\r\\n` or
    bare `\\n` convention, or `None` when the file is consistent throughout.

    Shared by every grammar (the seam's "Anomalies and omissions" is not qualified by grammar), so
    a patched install carrying a `tab-rows`/`titles`/`settings-scr`/
    `line-list`/`won-lists` member with mixed endings still reports it.
    """

    if "\r\n" not in text:
        return None
    index, total = 0, len(text)
    while index < total:
        char = text[index]
        if char == "\n" and (index == 0 or text[index - 1] != "\r"):
            return index
        if char == "\r" and text[index + 1:index + 2] != "\n":
            return index
        index += 1
    return None


def non_ascii_runs(data: bytes) -> list[dict]:
    """One `non-ascii-latin1` anomaly row per contiguous run of bytes above `0x7F`.

    Only meaningful for a Latin-1 member: a UTF-16 LE member's high bytes are ordinary code-unit
    halves, not out-of-band text. Shared by every grammar for the same reason
    `mixed_line_ending_offset` is.
    """

    rows: list[dict] = []
    start = None
    for offset, byte in enumerate(data):
        if byte > 0x7F:
            if start is None:
                start = offset
        elif start is not None:
            rows.append({"role": "non-ascii-latin1", "offset": start, "length": offset - start,
                         "text": data[start:offset].decode("latin-1")})
            start = None
    if start is not None:
        rows.append({"role": "non-ascii-latin1", "offset": start, "length": len(data) - start,
                     "text": data[start:].decode("latin-1")})
    return rows


def source_anomalies(data: bytes, text: str, encoding: str) -> list[dict]:
    """The two encoding-shaped anomalies every grammar checks for, whatever its own line/tree
    shape is: mixed line endings, and (Latin-1 only) non-ASCII bytes."""

    anomalies: list[dict] = []
    if encoding == "latin-1":
        anomalies.extend(non_ascii_runs(data))
    unit_width = 2 if encoding == "utf-16-le" else 1
    mixed_offset = mixed_line_ending_offset(text)
    if mixed_offset is not None:
        anomalies.append({"role": "mixed-line-endings", "offset": mixed_offset * unit_width})
    return anomalies
