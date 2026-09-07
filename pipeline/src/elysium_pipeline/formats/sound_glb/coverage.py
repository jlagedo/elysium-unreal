"""Gapless source-byte accountability for one sound unit.

Since schema 1.1.0 the unit does carry a verbatim copy of each member (the source capsule), but
that never excuses the decode: the ledger is still what proves the unit *read* what it carries.
Every byte of the audio member and of the `.lip` companion is claimed exactly once by the record
that paid for it, and a range labelled `reserved-zero` or `padding-zero` is proven zero before
publication.
"""

from __future__ import annotations

from typing import Any

from elysium_pipeline.formats.unit_contract.coverage import coverage_block
from elysium_pipeline.formats.unit_contract.ledger import ByteLedger

#: The extension keys every sound unit represents, whatever its container.
MAPPED_KEYS = ("identity", "sourceResolution", "payload", "codec")

#: The keys a unit names in `coverage.mapped` only where its members gave it something to put
#: there, so an empty list is never claimed as a decode that happened.
CONDITIONAL_KEYS = (
    "chunks", "frames", "tags", "lip", "resolution", "dependencies", "anomalies", "omissions",
)


def mapped_keys(model) -> list[str]:
    """The extension keys this unit represents, named once."""

    present = {
        "chunks": bool(model.chunks),
        "frames": bool(model.frames),
        "tags": any(value is not None for value in model.tags.values()),
        "lip": model.lip is not None,
        "resolution": model.resolution is not None,
        "dependencies": bool(model.dependencies),
        "anomalies": bool(model.anomalies),
        "omissions": bool(model.omissions),
    }
    return list(MAPPED_KEYS) + [key for key in CONDITIONAL_KEYS if present[key]]


def byte_ledger(model) -> list[dict[str, Any]]:
    """One ledger row per source member, built from the claims the decode recorded."""

    rows: list[dict[str, Any]] = []
    for member in model.members:
        ledger = ByteLedger(member.path, member.data, span_offset=member.span_offset)
        for path, offset, length, state, owner in model.claims:
            if path == member.path:
                ledger.claim(offset, length, state, owner)
        rows.append(ledger.finish())
    return rows


def build(model) -> dict[str, Any]:
    """The unit's `coverage` block: what was represented, what was carried, what was proven."""

    return coverage_block(
        mapped=mapped_keys(model),
        typed_unidentified=list(model.typed_unidentified),
        omitted_proven=list(model.omitted_proven),
        byte_ledger=byte_ledger(model),
        unresolved=list(model.unresolved),
        unsupported=list(model.unsupported),
    )
