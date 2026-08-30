"""Gapless byte accountability for one image member, built on the shared `ByteLedger`."""

from __future__ import annotations

from typing import Any

from elysium_pipeline.formats.unit_contract import ByteLedger, coverage_block


def gap_ranges(ledger: ByteLedger) -> list[tuple[int, int]]:
    """The `(offset, length)` byte ranges no earlier claim in `ledger` reaches, in file order."""

    covered = sorted(
        (int(row["offset"]), int(row["offset"]) + int(row["length"])) for row in ledger.ranges
    )
    total = ledger.byte_length
    cursor = 0
    gaps: list[tuple[int, int]] = []
    for start, end in covered:
        if start > cursor:
            gaps.append((cursor, start - cursor))
        cursor = max(cursor, end)
    if cursor < total:
        gaps.append((cursor, total - cursor))
    return gaps


def claim_uncovered_gaps(ledger: ByteLedger, owner: str) -> tuple[int, list[dict[str, Any]]]:
    """Claim every byte no earlier claim reached as `omitted-proven`, under one owner.

    A TGA member's structures are addressed by offset rather than laid out end to end (a 2.0
    footer's extension area), so the decoder claims what it understands and this sweeps what is
    left -- `trailing-bytes` bytes and any region a malformed offset made unreachable -- so the
    ledger is gapless either way. Every swept range is graded `omitted-proven` unconditionally,
    per this seam's own rule for `tga.trailing`. Returns the total number of bytes swept and the
    list of swept ranges (each carrying its own `sourceOffset`), so the caller can decide whether
    the sweep is worth an `omissions` row and can name where the swept bytes live.
    """

    claimed = 0
    ranges: list[dict[str, Any]] = []
    for start, length in gap_ranges(ledger):
        ledger.claim(start, length, "omitted-proven", owner)
        claimed += length
        ranges.append({"sourceOffset": start, "byteLength": length})
    return claimed, ranges


def claim_uncovered_gaps_graded(
    ledger: ByteLedger, owner: str, data: bytes
) -> tuple[int, list[dict[str, Any]]]:
    """Claim every byte no earlier claim reached under one owner, grading each contiguous swept
    range on its own content: all-zero sweeps as `padding-zero`, any other sweep as
    `omitted-proven`.

    A BMP's own owner for this sweep (`bmp.trailing`) is this seam's addition, not the source
    format's -- unlike a TGA's `tga.trailing`, nothing forces those trailing bytes to be anything
    but alignment fill, so a genuinely zero-filled sweep is graded the same way `bmp.gap` and
    `bmp.row[i].pad` already grade their own zero fill. Returns the number of bytes graded
    `omitted-proven` (not `padding-zero`) and the list of those non-zero ranges (each carrying its
    own `sourceOffset`), so the caller only raises an `omissions` row when the sweep found bytes
    that are not proven zero fill.
    """

    claimed = 0
    non_zero: list[dict[str, Any]] = []
    for start, length in gap_ranges(ledger):
        chunk = data[start:start + length]
        if any(chunk):
            ledger.claim(start, length, "omitted-proven", owner)
            claimed += length
            non_zero.append({"sourceOffset": start, "byteLength": length})
        else:
            ledger.claim(start, length, "padding-zero", owner)
    return claimed, non_zero


def palette_usage(
    max_index: int, first_index: int, entries: int
) -> tuple[dict[str, Any] | None, dict[str, Any] | None]:
    """Compare the largest pixel index the pixels actually use against the palette the source
    declares, in the palette's own index space (`first_index` may be non-zero for a TGA).

    Returns an `unused-palette-entries` omission when trailing entries the pixels never reach are
    still published, and a `palette-index-out-of-range` anomaly when a pixel instead addresses a
    slot the stored map does not have -- the two ends of the same comparison, so a pixel index
    below `first_index` is never silently folded into an inflated unused count.
    """

    if not entries:
        return None, None
    used_index = max_index - first_index
    if used_index < 0 or used_index >= entries:
        return None, {
            "role": "palette-index-out-of-range",
            "reason": "a pixel index falls outside the colour map the source declares",
            "maxIndex": max_index,
            "colorMapFirst": first_index,
            "colorMapEntries": entries,
        }
    if used_index < entries - 1:
        return {
            "role": "unused-palette-entries",
            "reason": "palette entries beyond the largest index the pixels use",
            "count": entries - 1 - used_index,
        }, None
    return None, None


def build_image_coverage(
    *,
    ledger_row: dict[str, Any],
    mapped: list[str] | None = None,
    typed_unidentified: list[dict[str, Any]] | None = None,
    omitted_proven: list[dict[str, Any]] | None = None,
    unresolved: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """The `coverage` object: the one ledger row this seam ever publishes, plus the semantic
    lists the decode filled in.

    `omitted_proven` doubles as the extension root's own `omissions`: an evidence-backed omission
    (unused palette entries, a trailing-bytes sweep) is the same fact whether a reader asks for it
    by the seam's own `omissions` name or by the contract's `coverage.omittedProven` name, the
    same precedent `formats/particle_glb/coverage.py` and `formats/scene_glb/coverage.py` state
    for their own units. This seam never produces an `unsupported` row -- an unsupported format or
    depth fails the export outright -- so `coverage_block`'s `unsupported` list is always empty.
    """

    return coverage_block(
        mapped=mapped or [],
        typed_unidentified=typed_unidentified or [],
        omitted_proven=omitted_proven or [],
        byte_ledger=[ledger_row],
        unresolved=unresolved or [],
    )
