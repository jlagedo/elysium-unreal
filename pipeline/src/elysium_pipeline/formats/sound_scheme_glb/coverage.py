"""The byte ledger and coverage block for one sound-scheme unit, built on the shared package.

A sound scheme carries no span and no sibling members, so the ledger is always exactly one row:
gapless over the whole file the closure was decoded from.
"""

from __future__ import annotations

from typing import Any, Iterable

from elysium_pipeline.formats.unit_contract import ByteLedger, coverage_block as _coverage_block
from elysium_pipeline.formats.unit_contract.origin import SourceMember

#: The field names every complete unit represents directly, published in `coverage.mapped`.
MAPPED_FIELDS = (
    "identity",
    "sourceResolution",
    "parameters",
    "scheme",
    "dependencies",
    "comments",
    "anomalies",
    "omissions",
)


def new_ledger(member: SourceMember) -> ByteLedger:
    """A gapless ledger over the whole member -- a sound scheme is never cut from a span."""

    return ByteLedger(member.path, member.data)


def coverage_block(
    *,
    ledger_row: dict[str, Any],
    unresolved: Iterable[Any] = (),
    unsupported: Iterable[Any] = (),
) -> dict[str, Any]:
    """The six-key coverage object: what the seam maps, plus the one byte-ledger row."""

    return _coverage_block(
        mapped=MAPPED_FIELDS,
        byte_ledger=[ledger_row],
        unresolved=unresolved,
        unsupported=unsupported,
    )
