"""Offset-carrying KeyValues lexer and parser for one VMT.

`formats/kv.py` answers "what does this file say"; this module additionally answers "which bytes
said it", which is what the material unit's byte ledger is written against. The text is decoded
Latin-1 so one character is one byte and a token span is a byte span directly.

The grammar is Source KeyValues: `//` outside a quoted string starts a line comment, `"` quotes a
string that may span lines and may carry `\\"`, `{` and `}` nest, and anything else runs to the
next whitespace, brace, quote or comment.

Eleven shipped VMTs are not well-formed under that grammar. The engine's own reader is permissive,
so refusing them would publish less than the install holds; instead each departure is parsed the
way the engine resolves it and recorded as a named anomaly the unit publishes.
"""

from __future__ import annotations

from dataclasses import dataclass, field


class MaterialLexError(ValueError):
    """The VMT's byte stream does not form a KeyValues document at all."""


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
            # A stray quote runs to end of file. The engine's reader ends the token there rather
            # than rejecting the material, so the same bytes are claimed and the shape is named.
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
                raise MaterialLexError(f"lexer stalled at byte {start}")
            tokens.append(Token("string", text[start:index], start, index - start))
    return tokens


@dataclass(slots=True)
class Pair:
    """One `key value`, `key { ... }`, or valueless `key` with the tokens that produced it."""

    key: Token
    value: Token | None = None
    block: "Block | None" = None


@dataclass(slots=True)
class Block:
    open_token: Token | None
    close_token: Token | None
    pairs: list[Pair] = field(default_factory=list)


@dataclass(slots=True)
class Document:
    shader: Token
    root: Block
    anomalies: list[dict[str, object]] = field(default_factory=list)
    #: Significant tokens past the root block's close: a stray brace the engine ignores.
    trailing: list[Token] = field(default_factory=list)


def parse(tokens: list[Token]) -> Document:
    """The shader name and root block of a VMT, plus every named grammar departure.

    A VMT's root is one named block: the shader name, then `{ ... }`.
    """

    significant = [token for token in tokens if token.kind in ("string", "open", "close")]
    if not significant:
        raise MaterialLexError("the material declares no shader")
    shader = significant[0]
    if shader.kind != "string":
        raise MaterialLexError("the material's first token is not a shader name")
    if len(significant) < 2 or significant[1].kind != "open":
        raise MaterialLexError("the material's shader block does not open")

    anomalies: list[dict[str, object]] = [
        {"role": token.anomaly, "offset": token.offset, "length": token.length}
        for token in tokens
        if token.anomaly
    ]

    def parse_block(start: int, depth: int) -> tuple[Block, int]:
        open_token = significant[start]
        cursor = start + 1
        pairs: list[Pair] = []
        while cursor < len(significant):
            token = significant[cursor]
            if token.kind == "close":
                return Block(open_token, token, pairs), cursor + 1
            if token.kind == "open":
                # A commented-out block header leaves its body behind; the body is still data.
                nested, cursor = parse_block(cursor, depth + 1)
                anomalies.append(
                    {"role": "anonymous-block", "offset": token.offset, "depth": depth}
                )
                pairs.append(Pair(token, None, nested))
                continue
            cursor += 1
            if cursor < len(significant) and significant[cursor].kind == "open":
                nested, cursor = parse_block(cursor, depth + 1)
                pairs.append(Pair(token, None, nested))
                continue
            if cursor >= len(significant) or significant[cursor].kind == "close":
                # `"nomip"` before the closing brace: a flag the engine reads as present-and-empty.
                anomalies.append(
                    {"role": "valueless-key", "offset": token.offset, "key": token.text}
                )
                pairs.append(Pair(token, None, None))
                continue
            pairs.append(Pair(token, significant[cursor]))
            cursor += 1
        anomalies.append(
            {"role": "unclosed-block-at-end-of-file", "offset": open_token.offset, "depth": depth}
        )
        return Block(open_token, None, pairs), cursor

    root, consumed = parse_block(1, 0)
    trailing = significant[consumed:]
    for token in trailing:
        anomalies.append({"role": "content-after-shader-block", "offset": token.offset})
    return Document(shader, root, anomalies, trailing)
