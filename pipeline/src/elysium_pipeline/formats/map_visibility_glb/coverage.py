"""The byte ledger and coverage block one map-visibility unit publishes.

The unit is cut from spans rather than from whole files, so it publishes one ledger row per owned
lump, each gapless over that lump's bytes alone. Lump 4's row is the header, the offset table,
one `derived` range per compressed row and the runs no row reaches; each portal lump's row is the
single `mapped` range that carries the whole lump under the owner name its `typedUnidentified`
entry is filed under.
"""

from __future__ import annotations

from typing import Any

from elysium_pipeline.formats.unit_contract import ByteLedger, coverage_block


def byte_ledger(model) -> list[dict[str, Any]]:
    """One row per source member, in the order `sourceResolution` lists them."""

    rows: list[dict[str, Any]] = []
    for member in model.members:
        ledger = ByteLedger(member.path, member.data, span_offset=member.span_offset)
        for offset, length, state, owner in model.claims.get(member.path, ()):
            ledger.claim(offset, length, state, owner)
        rows.append(ledger.finish())
    return rows


def coverage(model) -> dict[str, Any]:
    """The six coverage lists, with the ledger the decode's claims close."""

    return coverage_block(
        mapped=model.mapped,
        typed_unidentified=model.typed_unidentified,
        omitted_proven=model.omitted_proven,
        byte_ledger=byte_ledger(model),
        unresolved=model.unresolved,
        unsupported=model.unsupported,
    )
