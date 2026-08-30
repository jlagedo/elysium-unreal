"""Coverage for a product over other products.

The corpus index carries no byte ledger: every byte it describes is ledgered by the unit that
owns it, and a second ledger here would make two units authoritative for one member. So coverage
grades the walk rather than bytes -- `mapped[]` names the sections and `unresolved[]` **is** the
unclaimed member list, which is why a complete index has none.
"""

from __future__ import annotations

from typing import Any, Sequence

from elysium_pipeline.formats.corpus_index_glb.model import COVERAGE_SECTIONS, Member
from elysium_pipeline.formats.unit_contract import coverage_block


def unresolved_rows(members: Sequence[Member]) -> list[dict[str, Any]]:
    """One row per unclaimed member: the path, and that no seam claims it."""

    return [
        {
            "path": member.path,
            "extension": member.extension,
            "origin": member.source.origin,
            "reason": "no seam claims this member and no residue category names it",
        }
        for member in members
        if member.disposition == "unclaimed"
    ]


def coverage(members: Sequence[Member]) -> dict[str, Any]:
    """The index's `coverage`: the sections it maps, and the members nothing claims."""

    return coverage_block(
        mapped=list(COVERAGE_SECTIONS),
        unresolved=unresolved_rows(members),
    )
