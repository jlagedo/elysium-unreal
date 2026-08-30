"""Offset-carrying KeyValues lexer and parser for one `sound/schemes/*.txt` file.

The tokenizer is `formats/surface_property_glb/lexer.py`'s: the text is decoded Latin-1 so one
character is one byte and a token span is a byte span directly, and the grammar is Source
KeyValues (`//` outside a quoted string starts a line comment, `"` quotes a string that may span
lines and may carry `\\"`, `{` and `}` nest, anything else runs to the next whitespace, brace,
quote or comment).

The parser differs from the surface-property one: a sound scheme is not a flat sequence of named
blocks, it is one root block (the literal `SoundScheme`) holding named blocks one level deep. A
block inside `SoundScheme` never itself opens a nested block -- every key inside one is a scalar
pair -- so the parser refuses a second level of nesting the same way the surface-property one
refuses a first.
"""

from __future__ import annotations

from dataclasses import dataclass, field


class SoundSchemeLexError(ValueError):
    """The byte stream does not form a sound-scheme table at all."""


BOM = "\xef\xbb\xbf"
_WHITESPACE = " \t\r\n\v\f"
_BARE_END = set(' \t\r\n\v\f{}"')

ROOT_NAME = "soundscheme"


@dataclass(frozen=True, slots=True)
class Token:
    kind: str          # "string" | "open" | "close" | "comment" | "whitespace" | "bom"
    text: str          # the decoded content; for a string this is the unescaped value
    offset: int        # byte offset of the token's first byte
    length: int        # byte length of the whole token span, quotes and escapes included
    quoted: bool = False
    anomaly: str = ""  # a named departure from the grammar, or "" for a well-formed token


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
            index += 1
            out: list[str] = []
            closed = False
            while index < total:
                if text[index] == "\\" and text[index + 1:index + 2] == '"':
                    out.append('"')
                    index += 2
                    continue
                if text[index] == '"':
                    index += 1
                    closed = True
                    break
                out.append(text[index])
                index += 1
            anomaly = "" if closed else "unterminated-quoted-string"
            tokens.append(Token("string", "".join(out), start, index - start, True, anomaly))
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
                raise SoundSchemeLexError(f"lexer stalled at byte {start}")
            tokens.append(Token("string", text[start:index], start, index - start))
    return tokens


@dataclass(slots=True)
class Pair:
    """One `key value`, or a valueless `key` the engine reads as present-and-empty."""

    key: Token
    value: Token | None = None


@dataclass(slots=True)
class Block:
    """One named block one level below the root: its name token, its braces and its pairs."""

    name: Token
    open_token: Token
    close_token: Token | None
    pairs: list[Pair] = field(default_factory=list)
    anomalies: list[dict[str, object]] = field(default_factory=list)

    @property
    def offset(self) -> int:
        return self.name.offset


@dataclass(slots=True)
class Root:
    """The `SoundScheme` root: its name token, its braces and the blocks it holds.

    `trailing` is every significant (string/open/close) token found after the root's own closing
    brace -- a file that has one is malformed, but its bytes are still claimed by the caller (see
    `formats.sound_scheme_glb.decode`) rather than left as an unaccounted-for ledger gap.
    """

    name: Token
    open_token: Token
    close_token: Token | None
    blocks: list[Block] = field(default_factory=list)
    anomalies: list[dict[str, object]] = field(default_factory=list)
    trailing: list[Token] = field(default_factory=list)


def _parse_pairs(significant: list[Token], cursor: int, owner: str) -> tuple[list[Pair], Token | None, list[dict], int]:
    """Every `key value` pair up to the block's own closing brace.

    A nested `{` here would be a third grammar level the format has no rule for, so it is refused
    rather than silently absorbed.
    """

    pairs: list[Pair] = []
    anomalies: list[dict[str, object]] = []
    close_token: Token | None = None
    while cursor < len(significant):
        token = significant[cursor]
        if token.kind == "close":
            close_token = token
            cursor += 1
            break
        if token.kind == "open":
            raise SoundSchemeLexError(f"byte {token.offset}: {owner} declares a nested block")
        cursor += 1
        following = significant[cursor] if cursor < len(significant) else None
        if following is None or following.kind != "string":
            anomalies.append({"role": "valueless-key", "offset": token.offset, "key": token.text})
            pairs.append(Pair(token, None))
            continue
        pairs.append(Pair(token, following))
        cursor += 1
    for pair in pairs:
        for token in (pair.key, pair.value):
            if token is not None and token.anomaly:
                anomalies.append({"role": token.anomaly, "offset": token.offset, "length": token.length})
    return pairs, close_token, anomalies, cursor


def parse(tokens: list[Token]) -> Root:
    """The one root the file declares, holding every block one level below it.

    A well-formed file is `SoundScheme { <blocks> }` and nothing else; a block is a bare name
    followed by its own `{ <pairs> }`. Anything else is refused, because there is no field an
    invented shape could stand for.
    """

    significant = [token for token in tokens if token.kind in ("string", "open", "close")]
    if not significant:
        raise SoundSchemeLexError("the file declares no SoundScheme root")
    root_name = significant[0]
    if root_name.kind != "string" or root_name.text.strip().lower() != ROOT_NAME:
        raise SoundSchemeLexError(
            f"byte {root_name.offset}: the file does not open with the literal SoundScheme"
        )
    if len(significant) < 2 or significant[1].kind != "open":
        raise SoundSchemeLexError(f"byte {root_name.offset}: SoundScheme opens no block")
    root_open = significant[1]
    cursor = 2
    blocks: list[Block] = []
    root_close: Token | None = None
    root_anomalies: list[dict[str, object]] = []
    while cursor < len(significant):
        token = significant[cursor]
        if token.kind == "close":
            root_close = token
            cursor += 1
            break
        if token.kind != "string":
            raise SoundSchemeLexError(f"byte {token.offset}: {token.text!r} outside any block")
        if cursor + 1 >= len(significant) or significant[cursor + 1].kind != "open":
            raise SoundSchemeLexError(f"byte {token.offset}: block {token.text!r} opens no block")
        block_open = significant[cursor + 1]
        cursor += 2
        pairs, close_token, anomalies, cursor = _parse_pairs(
            significant, cursor, f"block {token.text!r}"
        )
        if close_token is None:
            anomalies.append({"role": "unclosed-block-at-end-of-file", "offset": block_open.offset})
        blocks.append(Block(token, block_open, close_token, pairs, anomalies))
    if root_close is None:
        root_anomalies.append({"role": "unclosed-block-at-end-of-file", "offset": root_open.offset})
    trailing = list(significant[cursor:]) if root_close is not None else []
    if trailing:
        root_anomalies.append(
            {"role": "trailing-content-after-root", "offset": trailing[0].offset}
        )
    return Root(root_name, root_open, root_close, blocks, root_anomalies, trailing)
