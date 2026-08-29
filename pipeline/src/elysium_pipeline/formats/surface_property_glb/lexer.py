"""Offset-carrying KeyValues lexer and parser for the surface-property table.

`formats/kv.py` answers "what does this file say", and collapses a repeated key onto its last
value while doing so. This module additionally answers "which bytes said it" and keeps every
repeat, which the seam needs on both counts: the unit's byte ledger is written against the spans,
and a repeated `stepleft` is a variation the engine picks between rather than an overwrite.

The text is decoded Latin-1 so one character is one byte and a token span is a byte span directly.
The grammar is Source KeyValues: `//` outside a quoted string starts a line comment, `"` quotes a
string that may span lines and may carry `\\"`, `{` and `}` nest, and anything else runs to the
next whitespace, brace, quote or comment.
"""

from __future__ import annotations

from dataclasses import dataclass, field


class SurfacePropertyLexError(ValueError):
    """The byte stream does not form a surface-property table at all."""


BOM = "\xef\xbb\xbf"
_WHITESPACE = " \t\r\n\v\f"
_BARE_END = set(' \t\r\n\v\f{}"')


@dataclass(frozen=True, slots=True)
class Token:
    kind: str          # "string" | "open" | "close" | "comment" | "whitespace" | "bom"
    text: str          # the decoded content; for a string this is the unescaped value
    offset: int        # byte offset of the token's first byte
    length: int        # byte length of the whole token span, quotes and escapes included
    quoted: bool = False
    anomaly: str = ""  # a named departure from the grammar, or "" for a well-formed token

    @property
    def end(self) -> int:
        return self.offset + self.length


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
                raise SurfacePropertyLexError(f"lexer stalled at byte {start}")
            tokens.append(Token("string", text[start:index], start, index - start))
    return tokens


@dataclass(slots=True)
class Pair:
    """One `key value`, or a valueless `key` the engine reads as present-and-empty."""

    key: Token
    value: Token | None = None


@dataclass(slots=True)
class Entry:
    """One named surface block: its name token, its braces, and the pairs between them."""

    name: Token
    open_token: Token
    close_token: Token | None
    pairs: list[Pair] = field(default_factory=list)
    anomalies: list[dict[str, object]] = field(default_factory=list)

    @property
    def offset(self) -> int:
        return self.name.offset

    @property
    def end(self) -> int:
        return (self.close_token or self.open_token).end


def parse(tokens: list[Token]) -> list[Entry]:
    """Every named block the token stream declares, in source order.

    The table is a flat sequence of named blocks; a surface names its fields, never another
    block. A nested block is therefore refused rather than published, because there is no field
    it could stand for and inventing one would put a shape in the corpus the format has no rule
    for.
    """

    significant = [token for token in tokens if token.kind in ("string", "open", "close")]
    entries: list[Entry] = []
    cursor = 0
    while cursor < len(significant):
        name = significant[cursor]
        if name.kind != "string":
            raise SurfacePropertyLexError(
                f"byte {name.offset}: {name.text!r} outside any surface entry"
            )
        if cursor + 1 >= len(significant) or significant[cursor + 1].kind != "open":
            raise SurfacePropertyLexError(
                f"byte {name.offset}: surface {name.text!r} opens no block"
            )
        entry = Entry(name, significant[cursor + 1], None)
        if name.anomaly:
            entry.anomalies.append(
                {"role": name.anomaly, "offset": name.offset, "length": name.length}
            )
        cursor += 2
        while cursor < len(significant):
            token = significant[cursor]
            if token.kind == "close":
                entry.close_token = token
                cursor += 1
                break
            if token.kind == "open":
                raise SurfacePropertyLexError(
                    f"byte {token.offset}: surface {name.text!r} declares a nested block"
                )
            cursor += 1
            following = significant[cursor] if cursor < len(significant) else None
            if following is None or following.kind != "string":
                entry.anomalies.append(
                    {"role": "valueless-key", "offset": token.offset, "key": token.text}
                )
                entry.pairs.append(Pair(token, None))
                continue
            entry.pairs.append(Pair(token, following))
            cursor += 1
        if entry.close_token is None:
            entry.anomalies.append(
                {"role": "unclosed-block-at-end-of-file", "offset": entry.open_token.offset}
            )
        for pair in entry.pairs:
            for token in (pair.key, pair.value):
                if token is not None and token.anomaly:
                    entry.anomalies.append(
                        {"role": token.anomaly, "offset": token.offset, "length": token.length}
                    )
        entries.append(entry)
    return entries
