"""The byte ledgers and coverage blocks the two shader-program kinds publish.

`decode.py` decides which record pays for which range -- it is the only place that knows -- and
hands the claims here as `(offset, length, state, owner)` rows. This module runs them through the
shared `ByteLedger`, which is what proves the partition is gapless, non-overlapping and honest
about its `-zero` claims, and assembles the six-list coverage object around the result.
"""

from __future__ import annotations

from typing import Any, Iterable, Sequence

from elysium_pipeline.formats.unit_contract import ByteLedger, SourceMember, coverage_block

#: Top-level extension keys a readable-source unit's `mapped` semantic content lives in.
SOURCE_MAPPED_SECTIONS = ("identity", "sourceResolution", "source")

#: Top-level extension keys a compiled-program unit's `mapped` semantic content lives in.
PROGRAM_MAPPED_SECTIONS = (
    "identity",
    "sourceResolution",
    "header",
    "comboTable",
    "combos",
    "sourceComparison",
)


def build_ledger(
    member: SourceMember, claims: Sequence[tuple[int, int, str, str]]
) -> dict[str, Any]:
    """One member's ledger row from the claims the decoder made against it."""

    ledger = ByteLedger(member.path, member.data, span_offset=member.span_offset)
    for offset, length, state, owner in claims:
        ledger.claim(offset, length, state, owner)
    return ledger.finish()


def shader_source_coverage(
    *,
    typed_unidentified: Iterable[Any] = (),
    omitted_proven: Iterable[Any] = (),
    byte_ledger: Iterable[Any] = (),
    unresolved: Iterable[Any] = (),
    unsupported: Iterable[Any] = (),
) -> dict[str, Any]:
    return coverage_block(
        mapped=SOURCE_MAPPED_SECTIONS,
        typed_unidentified=typed_unidentified,
        omitted_proven=omitted_proven,
        byte_ledger=byte_ledger,
        unresolved=unresolved,
        unsupported=unsupported,
    )


def shader_program_coverage(
    *,
    typed_unidentified: Iterable[Any] = (),
    omitted_proven: Iterable[Any] = (),
    byte_ledger: Iterable[Any] = (),
    unresolved: Iterable[Any] = (),
    unsupported: Iterable[Any] = (),
) -> dict[str, Any]:
    return coverage_block(
        mapped=PROGRAM_MAPPED_SECTIONS,
        typed_unidentified=typed_unidentified,
        omitted_proven=omitted_proven,
        byte_ledger=byte_ledger,
        unresolved=unresolved,
        unsupported=unsupported,
    )
