"""The byte ledger every export_v2 unit publishes.

The ledger is what proves the decode is complete: it partitions the member -- or the span the
unit was cut from -- into ranges, each naming the record that paid for it, and it fails
publication when one byte is left over. A unit also carries the member verbatim in its source
capsule (`capsule.py`), and that copy never excuses a range: a capsule says what the bytes were,
the ledger says what the decode made of every one of them.
"""

from __future__ import annotations

from collections import Counter
import hashlib
import json
from typing import Any, Iterable, Mapping

#: The grades a byte range can carry. `mapped-string` and `mapped-text` separate a decoded
#: null-terminated string and a decoded text region from a binary record, so that a text unit and
#: a binary unit answer in one vocabulary.
LEDGER_STATES = frozenset(
    {
        "mapped",
        "mapped-string",
        "mapped-text",
        "derived",
        "omitted-proven",
        "reserved-zero",
        "padding-zero",
    }
)

#: The two states the source is required to store as zero. Claiming either over a non-zero byte
#: aborts publication, which is the only reason a `-zero` claim is believable at all.
ZERO_STATES = frozenset({"reserved-zero", "padding-zero"})

#: The row shape `ByteLedger.finish` returns, in publication order.
LEDGER_ROW_KEYS = (
    "sourcePath",
    "sourceSha256",
    "byteLength",
    "accountedBytes",
    "coveragePercent",
    "stateBytes",
    "rangesSha256",
    "ranges",
)


class ByteLedgerError(ValueError):
    """A claim or a published row contradicts the byte accountability rule."""


def ranges_sha256(source_path: str, byte_length: int, ranges: Iterable[Mapping[str, Any]]) -> str:
    """The contract digest of a range table.

    The canonical form is key-sorted and separator-compact, so two writers that agree on the
    partition agree on the digest whatever order they emitted the row keys in.
    """

    canonical = json.dumps(
        {"path": source_path, "byteLength": int(byte_length), "ranges": list(ranges)},
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


class ByteLedger:
    """Gapless accountability for one member's bytes, or for one span of one member.

    `data` is what the ledger is authoritative for: the whole member, or the span's bytes alone.
    Claim offsets are relative to the start of `data`; `span_offset` is where that start sits in
    the containing file, and is used to phrase an error in file coordinates.
    """

    __slots__ = ("source_path", "data", "span_offset", "ranges")

    def __init__(self, source_path: str, data: bytes, *, span_offset: int = 0) -> None:
        if span_offset < 0:
            raise ByteLedgerError(f"{source_path}: negative span offset {span_offset}")
        self.source_path = source_path
        self.data = bytes(data)
        self.span_offset = int(span_offset)
        self.ranges: list[dict[str, Any]] = []

    @property
    def byte_length(self) -> int:
        return len(self.data)

    def absolute(self, offset: int) -> int:
        """Where a ledger offset sits in the containing file."""

        return self.span_offset + int(offset)

    def claim(self, offset: int, length: int, state: str, owner: str) -> None:
        offset, length = int(offset), int(length)
        if state not in LEDGER_STATES:
            raise ByteLedgerError(f"{self.source_path}: invalid ledger state {state!r}")
        if not owner:
            raise ByteLedgerError(f"{self.source_path}: range {offset}+{length} has no owner")
        if offset < 0 or length < 0 or offset + length > len(self.data):
            raise ByteLedgerError(
                f"{self.source_path}: {owner} range {offset}+{length} exceeds "
                f"{len(self.data)} (file offset {self.absolute(offset)})"
            )
        if not length:
            return
        if state in ZERO_STATES and any(self.data[offset:offset + length]):
            raise ByteLedgerError(f"{self.source_path}: {owner} labels non-zero bytes as {state}")
        end = offset + length
        for row in self.ranges:
            start, size = int(row["offset"]), int(row["length"])
            if offset < start + size and start < end:
                raise ByteLedgerError(
                    f"{self.source_path}: {owner} range {offset}+{length} overlaps "
                    f"{row['owner']} range {start}+{size}"
                )
        self.ranges.append({"offset": offset, "length": length, "state": state, "owner": owner})

    def finish(self) -> dict[str, Any]:
        rows = sorted(self.ranges, key=lambda row: int(row["offset"]))
        cursor = 0
        totals: Counter[str] = Counter()
        for row in rows:
            offset, length = int(row["offset"]), int(row["length"])
            if offset != cursor:
                kind = "overlap" if offset < cursor else "gap"
                raise ByteLedgerError(
                    f"{self.source_path}: byte ledger {kind} at {cursor}, "
                    f"next range starts {offset}"
                )
            cursor += length
            totals[str(row["state"])] += length
        if cursor != len(self.data):
            raise ByteLedgerError(
                f"{self.source_path}: byte ledger ends at {cursor}/{len(self.data)}"
            )
        return {
            "sourcePath": self.source_path,
            "sourceSha256": hashlib.sha256(self.data).hexdigest(),
            "byteLength": len(self.data),
            "accountedBytes": cursor,
            "coveragePercent": 100.0,
            "stateBytes": dict(sorted(totals.items())),
            "rangesSha256": ranges_sha256(self.source_path, len(self.data), rows),
            "ranges": rows,
        }


def verify_ledger_row(row: Mapping[str, Any], data: bytes | None = None) -> None:
    """Re-check a published ledger row without the writer that produced it.

    With `data` the check is the export-time one: the row is also weighed against the member
    bytes, which is the only place a `-zero` claim can be proven.
    """

    if not isinstance(row, Mapping):
        raise ByteLedgerError("byte ledger row is not a record")
    missing = [key for key in LEDGER_ROW_KEYS if key not in row]
    if missing:
        raise ByteLedgerError(f"byte ledger row is missing {', '.join(missing)}")
    path = str(row["sourcePath"])
    length = int(row["byteLength"])
    if length < 0:
        raise ByteLedgerError(f"{path}: negative byteLength {length}")
    ranges = row["ranges"]
    if not isinstance(ranges, list):
        raise ByteLedgerError(f"{path}: byte ledger ranges are missing")
    cursor = 0
    totals: Counter[str] = Counter()
    for index, entry in enumerate(ranges):
        if not isinstance(entry, Mapping):
            raise ByteLedgerError(f"{path}: byte range {index} is not a record")
        offset, size = int(entry.get("offset", -1)), int(entry.get("length", -1))
        state, owner = str(entry.get("state", "")), str(entry.get("owner", ""))
        if state not in LEDGER_STATES:
            raise ByteLedgerError(f"{path}: byte range {index} has state {state!r}")
        if not owner:
            raise ByteLedgerError(f"{path}: byte range {index} has no owner")
        if size <= 0:
            raise ByteLedgerError(f"{path}: byte range {index} claims {size} bytes")
        if offset != cursor:
            raise ByteLedgerError(f"{path}: byte range {index} starts at {offset}, not {cursor}")
        if offset + size > length:
            raise ByteLedgerError(f"{path}: byte range {index} overruns the source")
        if data is not None and state in ZERO_STATES and any(data[offset:offset + size]):
            raise ByteLedgerError(f"{path}: byte range {index} is a false {state} claim")
        cursor += size
        totals[state] += size
    if cursor != length:
        raise ByteLedgerError(f"{path}: byte ledger accounts {cursor}/{length} bytes")
    if int(row["accountedBytes"]) != length:
        raise ByteLedgerError(f"{path}: accountedBytes disagrees with the range table")
    if float(row["coveragePercent"]) != 100.0:
        raise ByteLedgerError(f"{path}: byte coverage is not 100%")
    if dict(sorted(totals.items())) != row["stateBytes"]:
        raise ByteLedgerError(f"{path}: state totals disagree with the range table")
    if ranges_sha256(path, length, ranges) != row["rangesSha256"]:
        raise ByteLedgerError(f"{path}: range table digest disagrees")
    if data is not None:
        if len(data) != length:
            raise ByteLedgerError(f"{path}: source is {len(data)} bytes, ledger claims {length}")
        if hashlib.sha256(data).hexdigest() != str(row["sourceSha256"]):
            raise ByteLedgerError(f"{path}: source digest disagrees")
