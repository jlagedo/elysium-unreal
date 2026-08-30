"""The offset-carrying line lexer for `materials/dxshaders/*.psh`.

A `.psh` is line-oriented text, so the lexical unit that owns bytes is the physical line: the
ledger cuts each line into the code it parsed and the comment that followed it, and the two
together span the line including its terminator. Nothing is normalized on the way through --
`text` is the line as written, the terminator kind is recorded rather than folded, and the
declared encoding is whatever the bytes actually are -- because `lines[]` is what makes the source
text recoverable exactly.
"""

from __future__ import annotations

from elysium_pipeline.formats.shader_program_glb.model import SourceLine

#: The two comment markers the shipped sources use. The ps.1.x assembler documents `;`; the
#: water shaders also spell C++ `//`, so both are recognized and the spelling is recorded.
COMMENT_MARKERS = (";", "//")


def decode_text(data: bytes) -> tuple[str, str]:
    """The member's text and the encoding it was read as.

    ASCII is what the corpus holds. A member that is not ASCII is read as latin-1, which
    round-trips every byte, so `lines[]` still reproduces the file exactly and the decoder can
    record the departure instead of guessing a code page.
    """

    if data.isascii():
        return data.decode("ascii"), "ascii"
    return data.decode("latin-1"), "latin-1"


def _comment_start(text: str) -> tuple[int, str] | None:
    positions = [(text.find(marker), marker) for marker in COMMENT_MARKERS]
    found = [(index, marker) for index, marker in positions if index >= 0]
    if not found:
        return None
    return min(found)


def split_lines(data: bytes) -> list[SourceLine]:
    """Partition the member into physical lines, each carrying its own byte span.

    The spans are contiguous and cover the member end to end, including a final line that the
    file does not terminate; `ending` names which terminator the line actually had.
    """

    text, _ = decode_text(data)
    lines: list[SourceLine] = []
    offset = 0
    index = 0
    length = len(text)
    while offset < length:
        break_at = text.find("\n", offset)
        if break_at < 0:
            body_end = length
            ending = "none"
            next_offset = length
        else:
            next_offset = break_at + 1
            if break_at > offset and text[break_at - 1] == "\r":
                body_end = break_at - 1
                ending = "crlf"
            else:
                body_end = break_at
                ending = "lf"
        body = text[offset:body_end]
        marker_hit = _comment_start(body)
        if marker_hit is None:
            code, marker, comment_offset, comment = body, None, None, None
        else:
            at, marker = marker_hit
            code = body[:at]
            comment_offset = offset + at
            comment = body[at:]
        lines.append(
            SourceLine(
                index=index,
                offset=offset,
                length=next_offset - offset,
                text=body,
                ending=ending,
                code=code,
                marker=marker,
                comment_offset=comment_offset,
                comment=comment,
                kind="unclassified",
            )
        )
        offset = next_offset
        index += 1
    return lines


def classify(line: SourceLine, kind: str) -> SourceLine:
    """The same line, tagged with what the parser made of it."""

    return SourceLine(
        index=line.index,
        offset=line.offset,
        length=line.length,
        text=line.text,
        ending=line.ending,
        code=line.code,
        marker=line.marker,
        comment_offset=line.comment_offset,
        comment=line.comment,
        kind=kind,
    )
