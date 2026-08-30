"""Offset-carrying whitespace tokenizer for the `.ain` node-graph grammar.

The `.ain` file has no quoting, no comments and no nesting: it is CRLF, ASCII, and
whitespace-tokenized end to end (`seam_map_nav_graph.md`, "The `.ain` grammar"). `tokenize` walks
the decoded text once and returns every non-whitespace run as a `Token`, carrying its byte offset
and 1-based source line, so the decoder can group tokens into records while the byte ledger claims
the exact spans the records and the whitespace between them occupy.
"""

from __future__ import annotations

from dataclasses import dataclass

_WHITESPACE = " \t\r\n\v\f"


@dataclass(frozen=True, slots=True)
class Token:
    text: str
    offset: int
    length: int
    line: int          # 1-based line number of the token's first byte

    @property
    def end(self) -> int:
        return self.offset + self.length


def decode_text(data: bytes) -> str:
    """One character per byte, so a character offset is a byte offset."""

    return data.decode("latin-1")


def tokenize(text: str) -> list[Token]:
    """Every non-whitespace run of `text`, in order, with its 1-based source line.

    Byte ranges not covered by any returned token are exactly the format's insignificant
    whitespace: spaces, tabs, CRLF and blank lines. Nothing else is discarded.
    """

    tokens: list[Token] = []
    index, total, line = 0, len(text), 1
    while index < total:
        char = text[index]
        if char in _WHITESPACE:
            if char == "\n":
                line += 1
            index += 1
            continue
        start, start_line = index, line
        while index < total and text[index] not in _WHITESPACE:
            index += 1
        tokens.append(Token(text[start:index], start, index - start, start_line))
    return tokens
