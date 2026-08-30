"""The offset-carrying tokenizer the ENTITIES lump is read with, as the engine reads it.

`formats/kv.py` answers "what does this file say"; this module additionally answers "which bytes
said it", which the seam needs on two counts: the unit's byte ledger is written against the token
spans, and a repeated key is authored evidence rather than an overwrite.

The grammar is the one `MapEntity_ParseToken` implements in `vampire.dll` (`0x10136ce0`), not a
line format:

* bytes `<= 0x20` are insignificant whitespace;
* `//` starts a comment that runs to the end of the line;
* `"` opens a string that ends at the next `"` or at end of input, and `\\x` inside it yields
  `x` -- `\\n` yields a newline -- so an escaped quote does not close the string;
* `{`, `}`, `(`, `)` and `'` are one-character tokens;
* anything else runs to the next whitespace or one-character token.

The text is decoded Latin-1 so one character is one byte and a token span is a byte span.
"""

from __future__ import annotations

from dataclasses import dataclass, field

#: The one-character tokens `MapEntity_ParseToken` is handed (`vampire.dll` `0x105794c0`).
BREAK_CHARACTERS = "{}()'"

#: A quoted string's escape prefix, and the one escape that is not the following byte itself.
ESCAPE = "\\"
ESCAPE_NEWLINE = "n"


class MapEntitiesLexError(ValueError):
    """The byte stream does not tokenize at all."""


@dataclass(frozen=True, slots=True)
class Token:
    kind: str          # "string" | "bare" | "break" | "comment" | "whitespace"
    text: str          # the decoded content; for a string, unescaped and without its quotes
    offset: int        # byte offset of the token's first byte inside the lump span
    length: int        # byte length of the whole token span, quotes and escapes included
    quoted: bool = False
    anomaly: str = ""  # a named departure from the grammar, or "" for a well-formed token

    @property
    def end(self) -> int:
        return self.offset + self.length

    @property
    def significant(self) -> bool:
        return self.kind in ("string", "bare", "break")


def decode_text(data: bytes) -> str:
    """One character per byte, so a character offset is a byte offset."""

    return bytes(data).decode("latin-1")


def tokenize(text: str) -> list[Token]:
    """Every byte of `text` lands in exactly one token, in order."""

    tokens: list[Token] = []
    index, total = 0, len(text)
    while index < total:
        char = text[index]
        if char <= " ":
            start = index
            while index < total and text[index] <= " ":
                index += 1
            tokens.append(Token("whitespace", text[start:index], start, index - start))
            continue
        if char == "/" and text[index + 1:index + 2] == "/":
            start = index
            stop = text.find("\n", index)
            index = total if stop < 0 else stop
            tokens.append(Token("comment", text[start:index], start, index - start))
            continue
        if char == '"':
            start = index
            index += 1
            out: list[str] = []
            closed = False
            while index < total:
                current = text[index]
                if current == ESCAPE and index + 1 < total:
                    following = text[index + 1]
                    out.append("\n" if following == ESCAPE_NEWLINE else following)
                    index += 2
                    continue
                if current == '"':
                    index += 1
                    closed = True
                    break
                out.append(current)
                index += 1
            anomaly = "" if closed else "unterminated-quoted-string"
            tokens.append(Token("string", "".join(out), start, index - start, True, anomaly))
            continue
        if char in BREAK_CHARACTERS:
            tokens.append(Token("break", char, index, 1))
            index += 1
            continue
        start = index
        while index < total and text[index] > " " and text[index] not in BREAK_CHARACTERS:
            index += 1
        if index == start:                          # defensive: never emit a zero-width token
            raise MapEntitiesLexError(f"lexer stalled at byte {start}")
        tokens.append(Token("bare", text[start:index], start, index - start))
    return tokens


@dataclass(slots=True)
class Pair:
    """One `key value`, or a key the closing brace arrived before the value of."""

    key: Token
    value: Token | None = None


@dataclass(slots=True)
class Block:
    """One `{ … }` entity block: its braces, its pairs and how it departed from the grammar."""

    index: int
    open_token: Token
    close_token: Token | None = None
    pairs: list[Pair] = field(default_factory=list)
    anomalies: list[dict[str, object]] = field(default_factory=list)

    @property
    def offset(self) -> int:
        return self.open_token.offset

    @property
    def end(self) -> int:
        return (self.close_token or self.open_token).end


@dataclass(slots=True)
class Document:
    """The whole lump: its blocks, its comments and any token that sits outside a block.

    Whitespace has no list of its own: the tokenizer tiles the text, so the bytes no block,
    comment or stray token claims are exactly the insignificant whitespace, and the decode's
    ledger fills them by difference rather than by a second list that could disagree.
    """

    blocks: list[Block] = field(default_factory=list)
    comments: list[Token] = field(default_factory=list)
    strays: list[Token] = field(default_factory=list)


def parse(tokens: list[Token]) -> Document:
    """Group the token stream into blocks exactly the way `CEntityMapData::GetNextKey` does.

    A key is any token that is not the block's closing brace; the value is the token after it. A
    `}` arriving where a value was expected ends the block and leaves the key without one, which
    is the case retail warns about and keeps the entity for.
    """

    document = Document()
    document.comments = [token for token in tokens if token.kind == "comment"]
    significant = [token for token in tokens if token.significant]
    cursor = 0
    while cursor < len(significant):
        token = significant[cursor]
        if not (token.kind == "break" and token.text == "{"):
            document.strays.append(token)
            cursor += 1
            continue
        block = Block(index=len(document.blocks), open_token=token)
        cursor += 1
        while cursor < len(significant):
            key = significant[cursor]
            if key.kind == "break" and key.text == "}":
                block.close_token = key
                cursor += 1
                break
            cursor += 1
            value = significant[cursor] if cursor < len(significant) else None
            if value is None:
                block.pairs.append(Pair(key, None))
                break
            if value.kind == "break" and value.text == "}":
                block.pairs.append(Pair(key, None))
                block.close_token = value
                block.anomalies.append(
                    {
                        "role": "closing-brace-without-data",
                        "key": key.text,
                        "offset": key.offset,
                    }
                )
                cursor += 1
                break
            block.pairs.append(Pair(key, value))
            cursor += 1
        if block.close_token is None:
            block.anomalies.append(
                {"role": "unterminated-block", "offset": block.open_token.offset}
            )
        document.blocks.append(block)
    return document


__all__ = [
    "BREAK_CHARACTERS",
    "Block",
    "Document",
    "MapEntitiesLexError",
    "Pair",
    "Token",
    "decode_text",
    "parse",
    "tokenize",
]
