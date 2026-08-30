"""Byte ledger assembly for one expression-table unit.

The decoder walks each member's bytes and records what it claimed as a flat list of
`(offset, length, state, owner)` tuples; this module is the only place that turns such a list
into a finished, gapless ledger row through the shared `ByteLedger`.
"""

from __future__ import annotations

from typing import Any, Iterable

from elysium_pipeline.formats.unit_contract import ByteLedger


def ledger_row(
    path: str, data: bytes, claims: Iterable[tuple[int, int, str, str]]
) -> dict[str, Any]:
    """Turn one member's flat claim list into its finished, gapless ledger row."""

    ledger = ByteLedger(path, data)
    for offset, length, state, owner in claims:
        ledger.claim(offset, length, state, owner)
    return ledger.finish()
