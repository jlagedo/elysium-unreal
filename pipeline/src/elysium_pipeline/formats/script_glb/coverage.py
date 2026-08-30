"""Gapless byte accountability and the coverage object for one script unit.

The source is text, so every one of its bytes belongs to exactly one physical line and the line
table is what pays for them. The companion is binary, and every one of its bytes belongs to one
marshalled field -- the field's type-code byte included -- so a marshal record and the ledger
range name the same place. Neither format pads, so no range is ever claimed `padding-zero`.
"""

from __future__ import annotations

from typing import Any, Iterable, Sequence

from elysium_pipeline.formats.script_glb.lexer import BOM
from elysium_pipeline.formats.script_glb.model import SourceLine
from elysium_pipeline.formats.script_glb.pyc import Claim
from elysium_pipeline.formats.unit_contract import ByteLedger, SourceMember, coverage_block

#: What the unit says it represented, named by the extension key that carries it.
MAPPED_FIELDS = (
    "identity",
    "sourceResolution",
    "source",
    "tokens",
    "structure",
    "references",
    "entityNames",
    "dependencies",
)


def source_ledger(member: SourceMember, lines: Sequence[SourceLine], has_bom: bool) -> dict:
    """One row for the `.py` member: the byte-order mark, then one range per physical line."""

    ledger = ByteLedger(member.path, member.data, span_offset=member.span_offset)
    if has_bom:
        ledger.claim(0, len(BOM), "mapped", "source.bom")
    for line in lines:
        # The line table is already cut past the mark, so a line span and its range agree.
        ledger.claim(line.offset, line.length, "mapped-text", f"source.lines[{line.index}]")
    return ledger.finish()


def compiled_ledger(member: SourceMember, claims: Iterable[Claim]) -> dict:
    """One row for the `.pyc` member, claimed field by field out of the marshal decode."""

    ledger = ByteLedger(member.path, member.data, span_offset=member.span_offset)
    for claim in claims:
        ledger.claim(claim.offset, claim.length, claim.state, claim.owner)
    return ledger.finish()


def sentinel_rows(references: Sequence[Any]) -> list[dict[str, Any]]:
    """The `omitted-proven` row every sentinel reference owes, in reference-table order."""

    return [
        {
            "field": f"references[{index}]",
            "role": reference.kind,
            "asset": reference.asset,
            "sourceOffset": reference.source_offset,
            "reason": reference.sentinel_reason,
        }
        for index, reference in enumerate(references)
        if reference.sentinel_reason is not None
    ]


def build_coverage(
    *,
    ledgers: Sequence[dict],
    has_pyc: bool,
    references: Sequence[Any],
    typed_unidentified: Sequence[dict],
    omitted_proven: Sequence[dict] = (),
) -> dict[str, Any]:
    """The six coverage lists, with every sentinel reference graded and its reason recorded.

    Nothing this seam decodes is ever `unresolved` or `unsupported`: a marshal type code outside
    the 2.1 set has unknown width, so the reader cannot say where the object ends and fails the
    unit at that byte rather than publishing a row that asserts more than is known.
    """

    mapped = list(MAPPED_FIELDS)
    if has_pyc:
        mapped.append("pyc")
    return coverage_block(
        mapped=mapped,
        typed_unidentified=list(typed_unidentified),
        omitted_proven=list(omitted_proven) + sentinel_rows(references),
        byte_ledger=list(ledgers),
        unresolved=(),
        unsupported=(),
    )
