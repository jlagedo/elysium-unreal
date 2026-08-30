"""The coverage block both font_glb units publish, built on the shared byte ledger.

`decode.py` claims every byte through `elysium_pipeline.formats.unit_contract.ByteLedger`; this
module only assembles the six-list `coverage` object the contract's extension root carries,
naming the top-level extension keys a reader can expect `mapped` content in.
"""

from __future__ import annotations

from typing import Any, Iterable

from elysium_pipeline.formats.unit_contract import coverage_block

#: Top-level extension keys a font unit's `mapped` semantic content lives in.
FONT_MAPPED_SECTIONS = (
    "identity",
    "sourceResolution",
    "header",
    "charMap",
    "glyphs",
    "trailer",
    "pages",
    "fontList",
)

#: Top-level extension keys the font-list unit's `mapped` semantic content lives in.
FONT_LIST_MAPPED_SECTIONS = ("identity", "sourceResolution", "rows", "comments")


def font_coverage(
    *,
    typed_unidentified: Iterable[Any],
    unresolved: Iterable[Any],
    unsupported: Iterable[Any],
    byte_ledger: Iterable[Any],
    omitted_proven: Iterable[Any] = (),
) -> dict[str, Any]:
    return coverage_block(
        mapped=FONT_MAPPED_SECTIONS,
        typed_unidentified=typed_unidentified,
        byte_ledger=byte_ledger,
        unresolved=unresolved,
        unsupported=unsupported,
        omitted_proven=omitted_proven,
    )


def font_list_coverage(
    *,
    unresolved: Iterable[Any],
    unsupported: Iterable[Any],
    byte_ledger: Iterable[Any],
    omitted_proven: Iterable[Any] = (),
) -> dict[str, Any]:
    return coverage_block(
        mapped=FONT_LIST_MAPPED_SECTIONS,
        byte_ledger=byte_ledger,
        unresolved=unresolved,
        unsupported=unsupported,
        omitted_proven=omitted_proven,
    )
