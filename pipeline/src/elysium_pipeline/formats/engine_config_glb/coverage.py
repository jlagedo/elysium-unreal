"""The byte ledger and coverage block for one engine-config unit, built on the shared package.

Every unit is cut from exactly one whole member -- no span, no siblings -- so the ledger is
always exactly one row, gapless over the whole file the closure was decoded from.
"""

from __future__ import annotations

import hashlib
from typing import Any, Iterable

from elysium_pipeline.formats.engine_config_glb.model import GRAMMAR_FIELDS
from elysium_pipeline.formats.unit_contract import ByteLedger, coverage_block as _coverage_block
from elysium_pipeline.formats.unit_contract.origin import SourceMember

#: The field names every complete unit represents directly, published in `coverage.mapped`,
#: whatever grammar it is.
_BASE_MAPPED_FIELDS = (
    "identity",
    "sourceResolution",
    "grammar",
    "dependencies",
    "comments",
    "anomalies",
    "omissions",
)

#: The `omissions[]` role backing every `omitted-proven` whitespace claim (see `whitespace_omission`).
WHITESPACE_OMISSION_ROLE = "insignificant-whitespace"


def new_ledger(member: SourceMember) -> ByteLedger:
    return ByteLedger(member.path, member.data)


def whitespace_omission(ledger_row: dict[str, Any], data: bytes | None = None) -> list[dict[str, Any]]:
    """One `omissions[]` row backing every `omitted-proven` `whitespace`-owned range in
    `ledger_row`.

    Mirrors `formats/material_glb/decode.py` and `formats/surface_property_glb/decode.py`'s own
    `keyvalues-insignificant-whitespace` row: an `omitted-proven` ledger claim needs an evidence
    row somewhere, and separator whitespace between records is the one this seam claims that way.
    The row carries the claimed range count and total length always, and a digest of the claimed
    bytes themselves when the member's own data is passed, so the omission is evidence-backed
    rather than a bare role name over however many bytes the ledger happens to carry.
    Returns nothing when the ledger carries no such claim, so an empty or purely-binary member
    never states a row it has no bytes to back.
    """

    ranges = [
        row
        for row in ledger_row.get("ranges", [])
        if row.get("state") == "omitted-proven" and row.get("owner") == "whitespace"
    ]
    if not ranges:
        return []
    row: dict[str, Any] = {
        "role": WHITESPACE_OMISSION_ROLE,
        "reason": "separator-bytes-carry-no-grammar-meaning",
        "count": len(ranges),
        "length": sum(int(entry["length"]) for entry in ranges),
    }
    if data is not None:
        digest = hashlib.sha256(
            b"".join(data[entry["offset"]:entry["offset"] + entry["length"]] for entry in ranges)
        ).hexdigest()
        row["sha256"] = digest
    return [row]


def coverage_block(
    *,
    ledger_row: dict[str, Any],
    grammar: str,
    typed_unidentified: Iterable[Any] = (),
    omitted_proven: Iterable[Any] = (),
    unresolved: Iterable[Any] = (),
    unsupported: Iterable[Any] = (),
) -> dict[str, Any]:
    mapped = _BASE_MAPPED_FIELDS + GRAMMAR_FIELDS[grammar]
    return _coverage_block(
        mapped=mapped,
        typed_unidentified=typed_unidentified,
        omitted_proven=omitted_proven,
        byte_ledger=[ledger_row],
        unresolved=unresolved,
        unsupported=unsupported,
    )
