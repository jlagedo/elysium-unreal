"""Pure row-level classification rules, shared by the decoder and the validator.

Every function here derives its answer from already-extracted text -- never from raw member
bytes -- so `decode.py` and `validation/dialogue_glb.py` can each call the same rule and agree,
one working from freshly split cells and the other from a published unit's own `lines[]`.
"""

from __future__ import annotations

from typing import Any

from elysium_pipeline.formats.dialogue_glb.model import (
    MARKER_AUTO_END,
    MARKER_AUTO_LINK,
    MARKER_STARTING_CONDITION,
    ROLE_NPC_LINE,
    ROLE_PADDING,
    ROLE_PC_CHOICE,
)


def classify_link(link_raw: str) -> tuple[str, int | None, str | None]:
    """`(role, linkTarget, unresolvedReason)` for one row's raw column-3 cell.

    `unresolvedReason` is set only when the cell is neither empty, `#`, nor an integer -- a shape
    the format does not document, carried as `unresolved` rather than guessed at.
    """

    stripped = link_raw.strip()
    if stripped == "":
        return ROLE_PADDING, None, None
    if stripped == "#":
        return ROLE_NPC_LINE, None, None
    try:
        return ROLE_PC_CHOICE, int(stripped), None
    except ValueError:
        return ROLE_PADDING, None, "link-neither-hash-nor-integer-nor-empty"


def classify_marker(role: str, text_male: str) -> str | None:
    """The control-row marker for one row, or `None` for an ordinary line."""

    if role == ROLE_PC_CHOICE:
        trimmed = text_male.strip().lower()
        if trimmed == "(auto-link)":
            return MARKER_AUTO_LINK
        if trimmed == "(auto-end)":
            return MARKER_AUTO_END
    low = text_male.lower()
    if "starting condition" in low or "starting-condition" in low or "starting_condition" in low:
        return MARKER_STARTING_CONDITION
    return None


def stage_directions(column: int, text: str, content_offset: int) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """`(directions, anomalies)` -- every well-formed `[...]` span, and every unterminated `[`."""

    directions: list[dict[str, Any]] = []
    anomalies: list[dict[str, Any]] = []
    i, n = 0, len(text)
    while i < n:
        if text[i] == "[":
            end = text.find("]", i + 1)
            if end == -1:
                anomalies.append(
                    {"role": "unterminated-stage-direction", "sourceOffset": content_offset + i}
                )
                i += 1
            else:
                directions.append(
                    {"column": column, "text": text[i:end + 1], "sourceOffset": content_offset + i}
                )
                i = end + 1
        else:
            i += 1
    return directions, anomalies
