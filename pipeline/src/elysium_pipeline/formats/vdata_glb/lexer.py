"""Offset-carrying KeyValues lexer and tree parser for `vdata/**.txt`.

The tokenizer follows `formats/surface_property_glb/lexer.py`'s grammar: `//` outside a quoted
string starts a line comment, `"` quotes a string that may span lines and may carry `\\"`, `{`/`}`
nest, and anything else runs to the next whitespace, brace, quote or comment. Text is decoded
Latin-1 so one character is one byte and a token span is a byte span directly.

Unlike the surface-property and particle tables, a vdata file nests to arbitrary depth (a weapon's
`Activation` holds a `SoundData` that holds a named wave-pool block, a terminal's `SubDir` holds
`Function` blocks), so this module builds a real recursive tree rather than a fixed-depth walk. A
quoted **value** keeps its literal spelling -- escape sequences are not collapsed -- and instead
records where each `\\"` starts, so `value` (as spelled) and the decoded string (`Token.decoded`)
are both recoverable from one stored string: the "never state the same datum twice" rule applies
within one node, not just between core and extension.

A malformed file degrades the way `formats/particle_glb/lexer.py` does: the tree stands up to the
point the walk can no longer attribute a token to it, and everything from there to end of file is
one `unparsed` region the decoder claims verbatim, so the byte ledger stays gapless without the
parser fabricating structure the source does not support.

`parse_tree` also returns the ledger claims for every key/value/brace/directive span it recognized
-- `(offset, length, state, owner)` tuples -- because only the parser still holds the individual
key and value *tokens* once it hands back the merged `{"offset", "length"}` a published node
carries; reconstructing per-token spans from the merged node afterward would need the same
bookkeeping a second time.
"""

from __future__ import annotations

import bisect
from dataclasses import dataclass

BOM = "\xef\xbb\xbf"
_WHITESPACE = " \t\r\n\v\f"
_BARE_END = set(' \t\r\n\v\f{}"')

Claim = tuple[int, int, str, str]


class VdataLexError(ValueError):
    """The byte stream cannot be tokenized at all (defensive; a well-formed or merely malformed
    KeyValues file never raises this -- only a lexer that stalls would)."""


def decode_escapes(raw: str, escapes: list[int] | tuple[int, ...]) -> str:
    """`raw` (as spelled, escapes intact) with every `\\"` collapsed to `"`.

    `raw` and `escapes` are what a scalar node stores; this is the one place both the tree and the
    projection layer turn that pair back into the string the engine would actually read, so the
    decoded form is never a second, separately-stored copy of the value.
    """

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
        """The value with every `\\"` collapsed to `"` -- the string the engine would read."""

        return decode_escapes(self.raw, self.escapes)


def decode_text(data: bytes) -> str:
    """One character per byte, so a character offset is a byte offset."""

    return data.decode("latin-1")


def tokenize(text: str) -> list[Token]:
    """Every byte of `text` lands in exactly one token, in order."""

    tokens: list[Token] = []
    index, total = 0, len(text)
    if text.startswith(BOM):
        tokens.append(Token("bom", BOM, 0, len(BOM)))
        index = len(BOM)
    while index < total:
        char = text[index]
        if char in _WHITESPACE:
            start = index
            while index < total and text[index] in _WHITESPACE:
                index += 1
            tokens.append(Token("whitespace", text[start:index], start, index - start))
        elif char == "/" and text[index + 1:index + 2] == "/":
            start = index
            stop = text.find("\n", index)
            index = total if stop < 0 else stop
            tokens.append(Token("comment", text[start:index], start, index - start))
        elif char == '"':
            start = index
            content_start = index + 1
            index = content_start
            escapes: list[int] = []
            closed = False
            while index < total:
                if text[index] == "\\" and text[index + 1:index + 2] == '"':
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
            tokens.append(
                Token("string", raw, start, index - start, True, tuple(escapes), anomaly)
            )
        elif char in "{}":
            kind = "open" if char == "{" else "close"
            tokens.append(Token(kind, char, index, 1))
            index += 1
        else:
            start = index
            while (
                index < total
                and text[index] not in _BARE_END
                and text[index:index + 2] != "//"
            ):
                index += 1
            if index == start:                       # defensive: never emit a zero-width token
                raise VdataLexError(f"lexer stalled at byte {start}")
            tokens.append(Token("string", text[start:index], start, index - start))
    return tokens


def has_block_structure(tokens: list[Token]) -> bool:
    """Whether the file could ever form a KeyValues tree: at least one `{`/`}` token.

    `credits.txt`'s scroller mini-language and `charaction_sounds.txt`'s authoring note carry
    words and punctuation the tokenizer happily turns into bare `string` tokens, but never a
    brace; `decode.decode_vdata` reads the absence of any `open`/`close` token as the signal that
    the file is not KeyValues at all and falls back to the `freeform` grammar rather than raising
    on the first key that opens no block.
    """

    return any(token.kind in ("open", "close") for token in tokens)


class _State:
    """Threads the parse's shared, order-sensitive bookkeeping through the recursion.

    `unparsed_offset` is set the moment a shape cannot be resolved; every frame checks it before
    doing more work, so exactly one anomaly explains the failure no matter how deep it occurred.
    """

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
            self.anomalies.append({"role": "unbalanced-braces", "offset": offset})

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
    """Fold an orphaned bare token into the preceding scalar's unquoted value.

    The engine's own lexer would have read `key  some value with spaces` as one value if it had
    been quoted; unquoted, this tokenizer -- like the engine's -- cuts at the first whitespace, so
    the remaining words land as keys with nothing behind them. Where the shape is unambiguous (the
    orphan is itself unquoted, on the same line, and cannot itself open a block or take a value),
    it is read as a continuation of the value rather than published as a second, meaningless
    `valueless-key` node.
    """

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
    """Parse one block's (or the document's) children starting right after its opening token.

    Returns `(children, next_cursor, close_token)`. `close_token` is `None` when the block never
    closed -- end of file was reached, or a shape below it could not be resolved -- which the
    caller reads as "stop; publish an `unparsed` tail from here."
    """

    children: list[dict] = []
    seen_scalar_keys: set[str] = set()
    while cursor < len(significant):
        if state.unparsed_offset is not None:
            return children, cursor, None
        token = significant[cursor]
        if token.kind == "close":
            if top_level:
                # A stray close at the document level matches nothing; note it and keep going so
                # a trailing, otherwise well-formed sibling still parses.
                state.anomalies.append({"role": "unbalanced-braces", "offset": token.offset})
                state.claims.append((token.offset, token.length, "mapped-text", "tree.unparsed"))
                cursor += 1
                continue
            return children, cursor + 1, token
        if token.kind == "open":
            # An orphan `{` with no key naming it (a commented-out block leaves exactly this
            # shape). The tree already built stands; everything from here is unparsed.
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
            owner = f"tree.directives[{directive_index}]"
            state.claims.append((key_token.offset, key_token.length, "mapped-text", owner))
            if argument is not None:
                # A separate claim, not one span from the key through the argument, because the
                # whitespace between them is `whitespace`'s to claim, not this directive's.
                state.claims.append((argument.offset, argument.length, "mapped-text", owner))
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
                    {"role": "repeated-scalar-key", "offset": key_token.offset,
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

        # A key with nothing usable behind it: end of file, a close, or another key. If the
        # preceding sibling is an unquoted scalar on the same line, this is that value's overflow.
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
    """The document's top-level nodes, its anomalies, its ledger claims, and where an unparsed
    tail begins (`None` when the whole file structured cleanly)."""

    significant = [token for token in tokens if token.kind in ("string", "open", "close")]
    state = _State(text)
    children, cursor, _ = _parse_children(significant, 0, (), state, top_level=True)
    unparsed_offset = state.unparsed_offset
    if unparsed_offset is None and cursor < len(significant):
        # Defensive: the loop above only stops early when `unparsed_offset` is set, but a future
        # change that adds another early-return path should not silently drop a tail.
        unparsed_offset = significant[cursor].offset
        state.anomalies.append({"role": "unbalanced-braces", "offset": unparsed_offset})
    if unparsed_offset is not None:
        # Everything from the failure point on that no successful claim already covers is claimed
        # here, once, token by token -- this is what keeps the byte ledger gapless over a file the
        # tree could not fully structure. A block that runs out of tokens before its own close
        # (rather than meeting an orphan `{` or an unresolvable key) leaves nothing uncovered: its
        # own key/open/every child up to end of file were claimed by the recursion as it went, and
        # `unparsed_offset` here only marks where the caller noticed the close never came.
        covered = sorted((claim[0], claim[0] + claim[1]) for claim in state.claims)
        for token in significant:
            if token.offset < unparsed_offset:
                continue
            index = bisect.bisect_right(covered, (token.offset, float("inf"))) - 1
            if index >= 0 and covered[index][0] <= token.offset < covered[index][1]:
                continue
            state.claims.append((token.offset, token.length, "mapped-text", "tree.unparsed"))
    return children, state.anomalies, state.claims, unparsed_offset


def walk(nodes: list[dict], path: tuple[int, ...] = ()):
    """Every node in the tree, depth-first, paired with its dotted path."""

    for node in nodes:
        child_path = path + (node["index"],)
        yield _dotted(child_path), node
        if node["kind"] == "block":
            yield from walk(node["children"], child_path)
