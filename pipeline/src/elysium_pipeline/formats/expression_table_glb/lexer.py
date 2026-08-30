"""Offset-carrying line lexer for a Faceposer expression/phoneme TXT.

The grammar is line-oriented, not nested KeyValues: `//` starts a whole-line comment, a blank
line is insignificant whitespace, `$keys` and `$hasweighting` are directives, and every other
line is one row of `name class [values...] description`. The text is decoded Latin-1 so one
character is one byte and a line's offset is a byte offset directly -- `formats/surface_property_
glb/lexer.py` establishes the same convention for its own grammar.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import shlex

BOM = "\xef\xbb\xbf"


class ExpressionTableLexError(ValueError):
    """The byte stream does not tokenize as a Faceposer expression TXT."""


@dataclass(frozen=True, slots=True)
class Line:
    kind: str                 # "bom" | "blank" | "comment" | "keys" | "directive" | "row"
    offset: int
    length: int
    content: str               # the line without its terminator
    tokens: tuple[str, ...] = ()


def decode_text(data: bytes) -> str:
    """One character per byte, so a character offset is a byte offset."""

    return data.decode("latin-1")


def _classify(content: str) -> tuple[str, tuple[str, ...]]:
    stripped = content.strip()
    if not stripped:
        return "blank", ()
    if stripped.startswith("//"):
        return "comment", ()
    try:
        tokens = tuple(shlex.split(stripped, posix=True))
    except ValueError as error:
        raise ExpressionTableLexError(f"{error}") from error
    if not tokens:
        return "blank", ()
    if tokens[0].lower() == "$keys":
        return "keys", tokens
    if tokens[0].startswith("$"):
        return "directive", tokens
    return "row", tokens


def tokenize(text: str) -> list[Line]:
    """Every byte of `text` lands in exactly one `Line`, in order."""

    lines: list[Line] = []
    index, total = 0, len(text)
    if text.startswith(BOM):
        lines.append(Line("bom", 0, len(BOM), ""))
        index = len(BOM)
    while index < total:
        newline = text.find("\n", index)
        end = total if newline < 0 else newline + 1
        content = text[index:end]
        if content.endswith("\r\n"):
            content = content[:-2]
        elif content.endswith("\n") or content.endswith("\r"):
            content = content[:-1]
        kind, tokens = _classify(content)
        lines.append(Line(kind, index, end - index, content, tokens))
        index = end
    return lines
