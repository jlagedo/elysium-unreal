"""The lighting unit's byte ledgers and coverage block.

This unit is cut from five spans of one file, so it publishes one ledger per span and each is
gapless over that span alone. Lump 8 is the only member whose claims are per-record at the
format's own granularity -- one range per `(face, set, style)` block, plus one per orphan run --
because that is what makes an accessor of raw `ColorRGBExp32` bytes inspectable.
"""

from __future__ import annotations

from typing import Any

from elysium_pipeline.formats.unit_contract import (
    ByteLedger,
    ByteLedgerError,
    coverage_block,
)


class SpanLedger(ByteLedger):
    """The shared `ByteLedger`, claimed in ascending offset order.

    The shared `claim` weighs every new range against every range already claimed, which is
    quadratic in the number of ranges; one lighting lump is claimed 52,188 times on `la_museum_1`.
    A claim that arrives in ascending order can only meet the range before it, so this subclass
    hides the rest of the table for the duration of one claim and delegates: every rule the
    contract owns -- the state vocabulary, the owner, the bounds, the zero-state verification --
    still runs, over the only row that can fail the overlap test. `finish()` is inherited
    unchanged and still sweeps the whole table for gaps, overlaps, the closing byte, the state
    totals and the range digest, which is what makes an overlap impossible rather than unseen.
    """

    __slots__ = ()

    def claim(self, offset: int, length: int, state: str, owner: str) -> None:
        if self.ranges and int(offset) < int(self.ranges[-1]["offset"]):
            raise ByteLedgerError(
                f"{self.source_path}: {owner} claims {offset} after "
                f"{self.ranges[-1]['offset']}; claims arrive in ascending order"
            )
        table = self.ranges
        window = table[-1:]                         # the only row an ascending claim can meet
        size = len(window)
        self.ranges = window
        try:
            super().claim(offset, length, state, owner)
        finally:
            accepted = self.ranges[size:]
            self.ranges = table
            table.extend(accepted)


def byte_ledger(model) -> list[dict[str, Any]]:
    """One ledger row per member, in the order `sourceResolution` publishes them."""

    rows: list[dict[str, Any]] = []
    for member in model.source.members():
        ledger = SpanLedger(member.path, member.data, span_offset=member.span_offset)
        for offset, length, state, owner in sorted(
            model.claims.get(member.path, ()), key=lambda claim: claim[0]
        ):
            ledger.claim(offset, length, state, owner)
        rows.append(ledger.finish())
    return rows


def coverage(model) -> dict[str, Any]:
    """The six coverage lists, with one closed ledger per owned span."""

    return coverage_block(
        mapped=model.mapped,
        typed_unidentified=model.typed_unidentified,
        omitted_proven=model.omitted_proven,
        byte_ledger=byte_ledger(model),
        unresolved=model.unresolved,
        unsupported=model.unsupported,
    )
