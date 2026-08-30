"""Gapless byte accountability for one map's ENTITIES lump, on the shared ledger.

The unit is cut from part of a larger file, so its ledger runs over the lump span alone: claim
offsets are relative to the span's first byte, and `span_offset` is where that byte sits in the
BSP, so an error can be phrased in file coordinates. The map root proves the whole file
partitions into these spans plus named non-unit bytes before any sub-unit is published
(`formats/map_glb/partition.py`).

Every range is `mapped-text` -- the lump is text, and a text byte belongs to the record whose
token it is -- except the terminator, which is `reserved-zero`, and anything past it, which is
`omitted-proven` with a digest.
"""

from __future__ import annotations

from typing import Any

from elysium_pipeline.formats.unit_contract import ByteLedger, coverage_block
from elysium_pipeline.formats.unit_contract.ledger import ByteLedgerError

#: The unit's own fields that are fully represented, in core-or-extension form. The semantic
#: states grade a source field or record, so a byte-ledger owner such as `whitespace` -- which no
#: published field states -- is named by the ledger alone and not listed here.
MAPPED_FIELDS = (
    "identity", "sourceResolution", "map", "coordinateTransform", "worldspawn", "entities",
    "entities[].keyValues", "entities[].outputs", "entities[].references", "classCensus",
    "scriptExpressions", "comments",
)


class OrderedByteLedger(ByteLedger):
    """The shared ledger, claimed in ascending offset order.

    A text unit claims one range per token rather than one per record, and the largest map
    authors tens of thousands of them; the shared `claim` weighs each new range against every
    range already claimed, which is quadratic at that count. Claims arriving in ascending order
    can only meet the range before them, so this subclass hides the rest of the table for the
    duration of one claim -- every check the shared ledger makes still runs, over the only row
    that can fail it -- and `finish` still proves the whole table is ordered, gapless and exactly
    as long as the span, which is what makes an overlap impossible rather than merely unseen.
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


def byte_ledger_row(model) -> dict[str, Any]:
    """The one ledger row this unit publishes, over the lump span alone."""

    member = model.member
    ledger = OrderedByteLedger(member.path, member.data, span_offset=member.span_offset)
    for claim in sorted(model.claims, key=lambda row: row.offset):
        ledger.claim(claim.offset, claim.length, claim.state, claim.owner)
    return ledger.finish()


def build_coverage(model) -> dict[str, Any]:
    """The `coverage` object: the mapped-field list, the ledger, and what is left over.

    `omissions` doubles as `omittedProven`: an evidence-backed omission is the same fact whether
    a reader asks for it by the seam's own root-level name or by the contract's semantic-state
    name. `unsupported` is empty by construction -- the format has no reserved vocabulary -- and
    `unresolved` names only a block the tokenizer could not close.
    """

    return coverage_block(
        mapped=list(MAPPED_FIELDS),
        omitted_proven=list(model.omissions),
        byte_ledger=[byte_ledger_row(model)],
        unresolved=list(model.unresolved),
        unsupported=list(model.unsupported),
    )


__all__ = ["MAPPED_FIELDS", "OrderedByteLedger", "build_coverage", "byte_ledger_row"]
