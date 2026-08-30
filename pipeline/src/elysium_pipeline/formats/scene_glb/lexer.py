"""Offset-carrying word-list lexer and parser for the Faceposer `.vcd` grammar.

Plain ASCII, CRLF. Every line is a whitespace-separated word list -- quoted words keep spaces --
optionally followed by a `{ ... }` block, and that one rule parses actors, channels, events and
every sub-block alike. This module tokenizes the byte stream one character per byte (Latin-1, so
a character offset is a byte offset) and builds the generic record tree the grammar describes;
what a record's words mean is entirely the decoder's business, not this module's.
"""

from __future__ import annotations

from dataclasses import dataclass, field


class SceneLexError(ValueError):
    """The byte stream does not form a well-nested word-list/brace grammar at all."""


_WHITESPACE = " \t\r\n\v\f"


@dataclass(frozen=True, slots=True)
class Token:
    kind: str          # "word" | "open" | "close" | "comment" | "whitespace"
    text: str          # decoded content; for a quoted word this is the unescaped value
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
            tokens.append(Token("word", "".join(out), start, index - start, True, anomaly))
        elif char in "{}":
            kind = "open" if char == "{" else "close"
            tokens.append(Token(kind, char, index, 1))
            index += 1
        else:
            start = index
            while (
                index < total
                and text[index] not in _WHITESPACE
                and text[index] not in "{}\""
                and text[index:index + 2] != "//"
            ):
                index += 1
            if index == start:                       # defensive: never emit a zero-width token
                raise SceneLexError(f"lexer stalled at byte {start}")
            tokens.append(Token("word", text[start:index], start, index - start))
    return tokens


@dataclass(slots=True)
class Record:
    """One word-list line, its optional `{ }` block, and the records nested inside it."""

    words: list[Token]
    open: Token | None = None
    close: Token | None = None
    children: list["Record"] = field(default_factory=list)

    @property
    def keyword(self) -> str:
        return self.words[0].text.strip().lower() if self.words else ""

    @property
    def offset(self) -> int:
        if self.words:
            return self.words[0].offset
        return self.open.offset if self.open is not None else 0


def _same_line(text: str, prev_end: int, next_offset: int) -> bool:
    return "\n" not in text[prev_end:next_offset]


def parse_records(text: str, tokens: list[Token]) -> tuple[list[Record], list[dict]]:
    """Every top-level record the significant token stream declares, plus structural anomalies.

    `tokens` is the whitespace/comment-free stream: word, open and close tokens only. A record's
    header is every consecutive word token that shares its first word's source line; the record
    then owns an optional `{ }` block of further records, parsed by the very same rule, all the
    way down -- actors, channels, events and an `event_ramp`'s bare two-word rows alike.
    """

    anomalies: list[dict] = []
    position = 0

    def parse_one() -> Record:
        nonlocal position
        if tokens[position].kind != "word":
            raise SceneLexError(
                f"byte {tokens[position].offset}: unexpected {tokens[position].kind!r}"
            )
        words = [tokens[position]]
        position += 1
        while (
            position < len(tokens)
            and tokens[position].kind == "word"
            and _same_line(text, words[-1].end, tokens[position].offset)
        ):
            words.append(tokens[position])
            position += 1
        record = Record(words)
        if position < len(tokens) and tokens[position].kind == "open":
            record.open = tokens[position]
            position += 1
            while position < len(tokens) and tokens[position].kind != "close":
                record.children.append(parse_one())
            if position < len(tokens) and tokens[position].kind == "close":
                record.close = tokens[position]
                position += 1
            else:
                anomalies.append(
                    {"role": "unclosed-block-at-end-of-file", "offset": record.open.offset}
                )
        return record

    top: list[Record] = []
    while position < len(tokens):
        if tokens[position].kind == "close":
            raise SceneLexError(f"byte {tokens[position].offset}: closing brace opens no block")
        top.append(parse_one())
    return top, anomalies


def record_span_text(text: str, record: Record) -> str:
    """The record's own verbatim source slice, header through its closing brace (or its last
    token, when the block never closed)."""

    start = record.offset
    if record.close is not None:
        end = record.close.end
    elif record.open is not None:
        end = record.open.end
    elif record.words:
        end = record.words[-1].end
    else:
        end = start
    return text[start:end]
