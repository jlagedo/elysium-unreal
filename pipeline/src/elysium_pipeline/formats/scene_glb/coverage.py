"""Coverage assembly for the Scene GLB seam, built on the shared byte ledger.

`decode.py` claims every byte through `unit_contract.ByteLedger` directly, one range at a time,
as it walks the record tree; this module's job is only to shape the finished ledger row and the
model's `unresolved` / `unsupported` rows into the coverage block the shared contract publishes.
"""

from __future__ import annotations

from typing import Any

from elysium_pipeline.formats.unit_contract import coverage_block

#: The scene's own top-level fields that are always fully represented, in core-or-extension form.
MAPPED_FIELDS = (
    "identity", "sourceResolution", "version", "fps", "snap", "actors",
    "scriptExpressions", "comments",
)


def build_coverage(
    *,
    byte_ledger_row: dict[str, Any],
    unresolved: list[dict[str, Any]],
    unsupported: list[dict[str, Any]],
    omissions: list[dict[str, Any]] = (),
) -> dict[str, Any]:
    """The `coverage` object: the mapped-field list, the one-member byte ledger, and whatever
    this decode could not resolve or represent.

    `omissions` doubles as `omittedProven`: an evidence-backed omission (insignificant whitespace,
    an empty member) is the same fact whether a reader asks for it by the seam's own root-level
    `omissions[]` name or by the contract's semantic-state name.
    """

    return coverage_block(
        mapped=list(MAPPED_FIELDS),
        omitted_proven=omissions,
        byte_ledger=[byte_ledger_row],
        unresolved=unresolved,
        unsupported=unsupported,
    )
