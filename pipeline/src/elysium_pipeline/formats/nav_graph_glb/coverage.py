"""Gapless byte accountability for one nav-graph's `.ain` and `.loc` members.

Every owner in the nav-graph seam's byte-ledger-owners table claims one contiguous range
-- a header line's label and value, one node's tokens, one link's tokens -- including whatever
whitespace separates its own tokens; `whitespace` then sweeps whatever the decoder's own claims do
not reach, which the grammar guarantees is exactly the separators between records (spaces, tabs,
CRLF, blank lines). Both members go through the shared `unit_contract.ByteLedger`.
"""

from __future__ import annotations

from typing import Any, Iterable

from elysium_pipeline.formats.unit_contract import ByteLedger, SourceMember

_WHITESPACE_BYTES = frozenset(b" \t\r\n\v\f")


class NavGraphCoverageError(ValueError):
    """The claimed ranges do not leave only whitespace for the catch-all owner."""


def _fill_whitespace(ledger: ByteLedger, owner: str = "whitespace") -> None:
    """Claim every byte no other owner claimed, and prove it is insignificant whitespace.

    This is what makes the per-record claims below sufficient: a seam that correctly attributes
    every token never needs to enumerate the separators between them one at a time.
    """

    data = ledger.data
    rows = sorted(ledger.ranges, key=lambda row: int(row["offset"]))
    cursor = 0
    for row in rows:
        start = int(row["offset"])
        if start > cursor:
            gap = data[cursor:start]
            if any(byte not in _WHITESPACE_BYTES for byte in gap):
                raise NavGraphCoverageError(
                    f"{ledger.source_path}: byte {cursor} is unclaimed and not whitespace"
                )
            ledger.claim(cursor, start - cursor, "mapped-text", owner)
        cursor = max(cursor, start + int(row["length"]))
    if cursor < len(data):
        gap = data[cursor:]
        if any(byte not in _WHITESPACE_BYTES for byte in gap):
            raise NavGraphCoverageError(
                f"{ledger.source_path}: byte {cursor} is unclaimed and not whitespace"
            )
        ledger.claim(cursor, len(data) - cursor, "mapped-text", owner)


def build_ain_ledger(member: SourceMember, claims: Iterable[tuple[int, int, str, str]]) -> dict[str, Any]:
    """The `.ain` member's ledger row: every decoder claim, then the whitespace left over."""

    ledger = ByteLedger(member.path, member.data)
    for offset, length, state, owner in claims:
        ledger.claim(offset, length, state, owner)
    _fill_whitespace(ledger)
    return ledger.finish()


def build_loc_raw_ledger(member: SourceMember) -> dict[str, Any]:
    """The `.loc` member's ledger row when the stamp does not parse as a decimal plus CRLF.

    The whole file is claimed under one owner (`stamp.raw`) rather than the labelled
    `stamp`/`stamp.lineEnd` split, since a companion that fails the grammar check has no proven
    line-ending to separate out; the byte ledger still reaches 100% so the malformed companion
    never blocks publication of the `.ain`-derived unit.
    """

    ledger = ByteLedger(member.path, member.data)
    if member.data:
        ledger.claim(0, len(member.data), "mapped-text", "stamp.raw")
    return ledger.finish()


def build_loc_ledger(
    member: SourceMember,
    *,
    stamp_offset: int,
    stamp_length: int,
    line_end_offset: int,
    line_end_length: int,
) -> dict[str, Any]:
    """The `.loc` member's ledger row: the decimal stamp, then its CRLF."""

    ledger = ByteLedger(member.path, member.data)
    ledger.claim(stamp_offset, stamp_length, "mapped-text", "stamp")
    ledger.claim(line_end_offset, line_end_length, "mapped-text", "stamp.lineEnd")
    _fill_whitespace(ledger)
    return ledger.finish()
