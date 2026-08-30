"""The byte ledger and coverage object for one particle unit.

A particle unit is cut from exactly one member -- there is no shared table to account for
separately -- so the ledger runs over the whole member and the seam adds nothing to
`formats/unit_contract`'s ledger and coverage machinery beyond the owners it claims with.
"""

from __future__ import annotations

from typing import Any, Iterable

from elysium_pipeline.formats.unit_contract import ByteLedger, coverage_block as _coverage_block


def new_ledger(path: str, data: bytes) -> ByteLedger:
    return ByteLedger(path, data)


def particle_coverage(
    *,
    mapped: Iterable[str],
    typed_unidentified: Iterable[dict[str, Any]],
    omissions: Iterable[dict[str, Any]],
    ledger_row: dict[str, Any],
    unresolved: Iterable[dict[str, Any]] = (),
    unsupported: Iterable[dict[str, Any]] = (),
) -> dict[str, Any]:
    """The six-key `coverage` object every export_v2 unit publishes.

    `omissions` doubles as `omittedProven`: an evidence-backed omission (an empty member, a
    malformed file's unparsed tail) is the same fact whether a reader asks for it by the seam's own
    name or by the contract's semantic-state name.
    """

    return _coverage_block(
        mapped=mapped,
        typed_unidentified=typed_unidentified,
        omitted_proven=omissions,
        byte_ledger=[ledger_row],
        unresolved=unresolved,
        unsupported=unsupported,
    )
