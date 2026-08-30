"""Byte claims on the way to the ledger.

A map is the largest member any seam decodes, and the shared `ByteLedger` checks each new claim
against every claim already made, so a decoder that claims one range per record turns the ledger
into quadratic work. Every claim here is therefore made at the granularity the format's own
records have -- one range per record array -- and the runs a variable-length container produces
are merged before they reach the ledger, under the longest owner path the merged runs agree on.

Merging is deliberately narrow, because the merged row's `owner` is what the ledger publishes as
the record that paid for the range:

* it never crosses a state boundary, so no byte changes the grade it was decoded under;
* it never crosses a partition boundary -- `merge(claims, boundaries=...)` takes the offsets the
  partition's regions start at, so one ledger range never spans two lumps or leaves the region
  its owner names;
* it happens only where the two owners share a dotted prefix, so the merged owner is a path both
  runs are below. Two owners with nothing in common stay two rows.
"""

from __future__ import annotations

from typing import Iterable

#: `(offset, length, state, owner)`, in the file's own coordinates.
Claim = tuple[int, int, str, str]


class ClaimError(ValueError):
    """Two claims disagree about one byte."""


def common_owner(left: str, right: str) -> str | None:
    """The longest dotted path both owners are below, or None when they share no prefix.

    None is the answer that keeps the ledger honest: a range whose two halves were paid for by
    unrelated records has no single owner, so the two runs stay two rows rather than being
    published under whichever of the two happened to sort first.
    """

    if left == right:
        return left
    shared: list[str] = []
    for one, other in zip(left.split("."), right.split(".")):
        if one != other:
            break
        shared.append(one)
    return ".".join(shared) if shared else None


def merge(claims: Iterable[Claim], *, boundaries: Iterable[int] = ()) -> list[Claim]:
    """Sort claims by offset and fuse the adjacent ones one owner path can still name.

    Two adjacent claims fuse only when they carry the same state, no partition boundary sits
    between them, and their owners share a dotted prefix; the fused range is published under that
    prefix. Everything else stays a row of its own.

    Raises when two claims overlap: a byte with two owners has no ledger, and quietly keeping
    the first would publish a range table that does not describe the decode.
    """

    stops = {int(value) for value in boundaries}
    ordered = sorted(claims, key=lambda claim: (claim[0], claim[1]))
    merged: list[Claim] = []
    for offset, length, state, owner in ordered:
        if length <= 0:
            continue
        if merged:
            last_offset, last_length, last_state, last_owner = merged[-1]
            end = last_offset + last_length
            if offset < end:
                raise ClaimError(
                    f"{owner} range {offset}+{length} overlaps {last_owner} "
                    f"range {last_offset}+{last_length}"
                )
            if offset == end and state == last_state and offset not in stops:
                shared = common_owner(last_owner, owner)
                if shared is not None:
                    merged[-1] = (last_offset, last_length + length, state, shared)
                    continue
        merged.append((offset, length, state, owner))
    return merged


def tail(
    data: bytes,
    offset: int,
    length: int,
    owner: str,
    reason: str,
    omissions: list[dict[str, object]],
    *,
    lump: int | None = None,
    evidence: str = "no record of this lump's record size addresses this range",
) -> list[Claim]:
    """The bytes a lump holds beyond the records its record size explains.

    Zero fill is `padding-zero`; anything else is an evidence-backed omission, because a lump
    longer than its record count explains is storage no record of this format addresses.
    """

    if length <= 0:
        return []
    chunk = data[offset:offset + length]
    if not any(chunk):
        return [(offset, length, "padding-zero", f"{owner}.padding")]
    row: dict[str, object] = {
        "role": reason,
        "sourceOffset": offset,
        "byteLength": length,
        "owner": owner,
        "evidence": evidence,
    }
    if lump is not None:
        row["lump"] = lump
    if length <= 64:
        row["bytesHex"] = chunk.hex()
    omissions.append(row)
    return [(offset, length, "omitted-proven", f"{owner}.unused")]


def strided(
    offset: int, count: int, stride: int, state: str, owner: str
) -> list[Claim]:
    """One claim for a whole record array; the rows themselves carry their own offsets."""

    if count <= 0 or stride <= 0:
        return []
    return [(offset, count * stride, state, owner)]

