"""Line-oriented lexer for `materials/fonts/fontlist.txt`.

The registry is one row per line, `"Face" size weight flags`, terminated `\\r\\n` on every
shipped copy. This module answers "which bytes said it": every token carries the byte span of its
whole line, terminator included, so the font-list unit's byte ledger is written against the spans
directly rather than reconstructed from a re-join.
"""

from __future__ import annotations

from dataclasses import dataclass
import re

ROW_PATTERN = re.compile(r'^"(?P<face>[^"]*)"\s+(?P<size>\d+)\s+(?P<weight>\d+)\s+(?P<flags>\d+)\s*$')


def decode_text(data: bytes) -> str:
    """One character per byte, so a character offset is a byte offset."""

    return data.decode("latin-1")


@dataclass(frozen=True, slots=True)
class Line:
    """One line of the registry, its byte span including whatever terminator it carried."""

    index: int
    offset: int
    length: int
    text: str          # the line's content, terminator stripped
    kind: str          # "row" | "comment" | "blank" | "malformed"
    face: str = ""
    size: int = 0
    weight: int = 0
    flags: int = 0

    @property
    def end(self) -> int:
        return self.offset + self.length


def tokenize(text: str) -> list[Line]:
    """Every byte of `text` lands in exactly one line, in source order."""

    lines: list[Line] = []
    offset, total, index = 0, len(text), 0
    while offset < total:
        newline = text.find("\n", offset)
        end = newline + 1 if newline != -1 else total
        raw = text[offset:end]
        stripped = raw.rstrip("\r\n")
        content = stripped.strip()
        if not content:
            kind, face, size, weight, flags = "blank", "", 0, 0, 0
        elif content.startswith("//"):
            kind, face, size, weight, flags = "comment", "", 0, 0, 0
        else:
            match = ROW_PATTERN.match(stripped)
            if match is None:
                kind, face, size, weight, flags = "malformed", "", 0, 0, 0
            else:
                kind = "row"
                face = match.group("face")
                size = int(match.group("size"))
                weight = int(match.group("weight"))
                flags = int(match.group("flags"))
        lines.append(Line(index, offset, end - offset, stripped, kind, face, size, weight, flags))
        offset = end
        index += 1
    return lines
