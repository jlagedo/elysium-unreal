"""Offset-carrying row/cell splitter for the `.dlg` physical format.

Latin-1 text, one row per CRLF line. A row is thirteen cells, each `{` TAB `content` TAB `}`,
concatenated with no separator; the row is split on `}{` and each cell stripped of its brace and
tabs. The module works on the member's raw bytes throughout -- Latin-1 assigns one code point per
byte, so a character offset and a byte offset are the same number, and offsets can be published
straight into the unit's byte ledger.
"""

from __future__ import annotations

from dataclasses import dataclass

from elysium_pipeline.formats.dialogue_glb.model import CellSpan


def decode_text(data: bytes) -> str:
    """One character per byte, so a character offset is a byte offset."""

    return data.decode("latin-1")


@dataclass(frozen=True, slots=True)
class Row:
    """One row: its cells, offsets relative to the member.

    `has_line_break` is false only for the file's last row when the file's last byte is not a
    CRLF -- four files in the shipped corpus end this way, their final row a `(Starting
    Condition)` sentinel with no trailing newline -- so the row is still a row, it just owns no
    `lineBreak` range.
    """

    index: int
    offset: int
    length: int              # the row's own bytes, terminator excluded
    cells: tuple[CellSpan, ...]
    has_line_break: bool = True


def split_cells(text: str, row_offset: int) -> tuple[CellSpan, ...]:
    """Every cell of one row, in source order, offsets relative to the *member*.

    Cells are found by locating every `}{` boundary in the row text -- literally, as the format
    specifies -- then reattaching the brace each boundary consumed. A cell that does not open
    `{` TAB and close TAB `}` is `malformed-cell`: its whole span is still returned, just flagged,
    so the caller can claim its bytes and record the anomaly rather than losing them.
    """

    boundaries: list[int] = []
    pos = text.find("}{")
    while pos != -1:
        boundaries.append(pos + 1)
        pos = text.find("}{", pos + 2)
    starts = [0] + boundaries
    ends = boundaries + [len(text)]

    cells: list[CellSpan] = []
    for index, (start, end) in enumerate(zip(starts, ends)):
        raw = text[start:end]
        # An empty cell collapses the symmetric `{` TAB content TAB `}` shape to `{` TAB `}` --
        # three bytes, one shared tab -- so the minimum well-formed length is three, not four,
        # and `raw[1]`/`raw[-2]` legitimately name the same byte in that case.
        well_formed = (
            len(raw) >= 3 and raw[0] == "{" and raw[1] == "\t" and raw[-1] == "}" and raw[-2] == "\t"
        )
        if well_formed:
            content = raw[2:-2]
            content_offset = row_offset + start + 2
            content_length = len(content)
        else:
            # A malformed cell still usually carries a recognisable `{` TAB ... TAB? `}` wrapper
            # around real text -- just not the exact symmetric shape `well_formed` requires (a
            # missing closing TAB, or a boundary the `}{` scan mis-split). Strip a leading `{`+TAB
            # and a trailing TAB?+`}` where present so the published text is the cell's content,
            # not its broken framing; the anomaly (`malformed-cell`) is what records the
            # departure, and the whole raw span is still what the caller ledger-claims.
            prefix = 2 if raw[:2] == "{\t" else 0
            body = raw[prefix:]
            if body.endswith("\t}"):
                suffix = 2
            elif body.endswith("}"):
                suffix = 1
            else:
                suffix = 0
            content = raw[prefix: len(raw) - suffix] if suffix else raw[prefix:]
            content_offset = row_offset + start + prefix
            content_length = len(content)
        cells.append(
            CellSpan(
                index=index,
                offset=row_offset + start,
                length=end - start,
                text=content,
                well_formed=well_formed,
                content_offset=content_offset,
                content_length=content_length,
            )
        )
    return tuple(cells)


def split_rows(text: str) -> tuple[tuple[Row, ...], int]:
    """Every row of the file, plus the offset where genuine trailing bytes begin.

    A row is exactly its cells and its terminator, gapless across the whole file: every
    CRLF-terminated row is one row,
    and whatever non-empty content is left after the last CRLF is one more row missing only its
    terminator, not residue -- a file with a missing final newline is still a file of complete
    rows. `trailing` is left for a genuinely empty (or, defensively, incidental) remainder.
    """

    rows: list[Row] = []
    cursor = 0
    index = 0
    while True:
        terminator = text.find("\r\n", cursor)
        if terminator == -1:
            break
        row_text = text[cursor:terminator]
        rows.append(
            Row(
                index=index,
                offset=cursor,
                length=terminator - cursor,
                cells=split_cells(row_text, cursor),
            )
        )
        cursor = terminator + 2
        index += 1
    if cursor < len(text) and text[cursor:].strip():
        # Non-whitespace content with no terminator is a row the file simply forgot to close
        # with a newline (four shipped files end exactly this way, on a `(Starting Condition)`
        # sentinel); whitespace-only leftover is not shaped like a row at all and is left for
        # `trailing` below.
        row_text = text[cursor:]
        rows.append(
            Row(
                index=index,
                offset=cursor,
                length=len(text) - cursor,
                cells=split_cells(row_text, cursor),
                has_line_break=False,
            )
        )
        cursor = len(text)
    return tuple(rows), cursor
