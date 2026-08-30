"""The map root's byte ledger and coverage block.

The root's one member is the whole BSP, so its ledger runs over the whole file: the header, the
directory, the game-lump directory, the trailer, every root lump and the bytes between them, plus
the three sub-units' spans as `omitted-proven` ranges naming the unit that owns them. That is the
partition proof restated as a range table -- if the ledger closes, the four units account for
every byte of the member exactly once.
"""

from __future__ import annotations

from typing import Any

from elysium_pipeline.formats.unit_contract import ByteLedger, coverage_block


def byte_ledger(model) -> list[dict[str, Any]]:
    """One ledger row, over the whole BSP member the four units are cut from."""

    ledger = ByteLedger(model.source_path, model.source.data)
    for offset, length, state, owner in model.claims:
        ledger.claim(offset, length, state, owner)
    return [ledger.finish()]


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
