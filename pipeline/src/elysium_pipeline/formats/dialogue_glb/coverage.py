"""Byte ledger and coverage assembly for the Dialogue GLB seam, over the shared contract."""

from __future__ import annotations

from typing import Any, Iterable

from elysium_pipeline.formats.unit_contract import (
    LEDGER_STATES,
    ZERO_STATES,
    ByteLedger,
    ByteLedgerError,
    coverage_block,
)


def new_ledger(source_path: str, data: bytes) -> ByteLedger:
    """One gapless ledger over a `.dlg` member's whole bytes."""

    return ByteLedger(source_path, data)


class SequentialClaimer:
    """Claims into a `ByteLedger` in `O(1)` per call, for a decoder that claims in row/cell order.

    `ByteLedger.claim()` proves non-overlap by scanning every range already accepted, which is
    the only check that can matter when a caller might claim out of order -- but this seam's
    decoder never does: a `.dlg` file is walked row by row and cell by cell, so every claim's
    offset is at or past the end of the one before it. An offset at or past that high-water mark
    cannot overlap anything already claimed -- the same fact the ledger's scan would establish,
    proven here in `O(1)` by remembering the mark instead of rescanning. A `.dlg` unit issues
    3-4 claims per cell across up to ~2,000 rows (~78,000 claims for the corpus's largest file),
    so the scan's `O(n)` per call made decoding it quadratic overall.

    This depends on nothing beyond `ByteLedger`'s already-published range shape and the shared
    `LEDGER_STATES`/`ZERO_STATES` vocabularies, so the fast path lives in this seam's own code,
    not in the shared ledger. A claim that does arrive out of order (never observed from this
    seam's decoder, but not assumed impossible) falls back to the ledger's own `claim()`, so it
    is still rejected -- or accepted -- exactly as `ByteLedger` itself would.
    """

    __slots__ = ("_ledger", "_end")

    def __init__(self, ledger: ByteLedger) -> None:
        self._ledger = ledger
        self._end = 0

    def claim(self, offset: int, length: int, state: str, owner: str) -> None:
        offset, length = int(offset), int(length)
        if offset < self._end:
            self._ledger.claim(offset, length, state, owner)
            self._end = max(self._end, offset + length)
            return
        ledger = self._ledger
        if state not in LEDGER_STATES:
            raise ByteLedgerError(f"{ledger.source_path}: invalid ledger state {state!r}")
        if not owner:
            raise ByteLedgerError(f"{ledger.source_path}: range {offset}+{length} has no owner")
        data = ledger.data
        if offset < 0 or length < 0 or offset + length > len(data):
            raise ByteLedgerError(
                f"{ledger.source_path}: {owner} range {offset}+{length} exceeds "
                f"{len(data)} (file offset {ledger.absolute(offset)})"
            )
        if not length:
            return
        if state in ZERO_STATES and any(data[offset:offset + length]):
            raise ByteLedgerError(f"{ledger.source_path}: {owner} labels non-zero bytes as {state}")
        ledger.ranges.append({"offset": offset, "length": length, "state": state, "owner": owner})
        self._end = offset + length


def finish_coverage(
    ledger: ByteLedger,
    *,
    mapped: Iterable[Any] = (),
    typed_unidentified: Iterable[Any] = (),
    omitted_proven: Iterable[Any] = (),
    unresolved: Iterable[Any] = (),
    unsupported: Iterable[Any] = (),
) -> dict[str, Any]:
    """The `coverage` block, carrying the ledger's one finished row."""

    row = ledger.finish()
    return coverage_block(
        mapped=mapped,
        typed_unidentified=typed_unidentified,
        omitted_proven=omitted_proven,
        byte_ledger=[row],
        unresolved=unresolved,
        unsupported=unsupported,
    )
