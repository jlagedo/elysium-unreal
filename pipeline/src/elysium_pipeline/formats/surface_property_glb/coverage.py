"""Gapless source-byte accountability for one surface-property entry.

The table is text, so every byte belongs to exactly one lexical region: a token the decode read,
a comment it captured verbatim, or the insignificant whitespace the KeyValues grammar discards.
The unit's ledger runs over its own entry's bytes -- from its name token through its closing
brace -- because that span is what the unit is authoritative for. The bytes between entries
belong to the table, not to any one surface, and `source.load_table` accounts for them there.
"""

from __future__ import annotations

from collections import Counter
import hashlib
import json


class SurfacePropertyByteCoverageError(ValueError):
    pass


#: `padding-zero` and `reserved-zero` are carried for vocabulary parity with the binary seams.
#: A text table reaches neither: it has no alignment fill and no declared-zero field.
ALLOWED_STATES = {
    "mapped",
    "derived",
    "omitted-proven",
    "padding-zero",
    "reserved-zero",
}


class ByteLedger:
    def __init__(self, path: str, data: bytes):
        self.path = path
        self.data = data
        self.ranges: list[dict[str, object]] = []

    def claim(self, offset: int, length: int, state: str, owner: str) -> None:
        if state not in ALLOWED_STATES:
            raise SurfacePropertyByteCoverageError(f"{self.path}: invalid state {state!r}")
        if offset < 0 or length < 0 or offset + length > len(self.data):
            raise SurfacePropertyByteCoverageError(
                f"{self.path}: {owner} range {offset}+{length} exceeds {len(self.data)}"
            )
        if not length:
            return
        if state.endswith("-zero") and any(self.data[offset:offset + length]):
            raise SurfacePropertyByteCoverageError(
                f"{self.path}: {owner} labels non-zero bytes as {state}"
            )
        self.ranges.append(
            {"offset": offset, "length": length, "state": state, "owner": owner}
        )

    def finish(self) -> dict[str, object]:
        rows = sorted(self.ranges, key=lambda row: int(row["offset"]))
        cursor = 0
        totals = Counter()
        for row in rows:
            offset = int(row["offset"])
            length = int(row["length"])
            if offset != cursor:
                kind = "overlap" if offset < cursor else "gap"
                raise SurfacePropertyByteCoverageError(
                    f"{self.path}: byte ledger {kind} at {cursor}, next range starts {offset}"
                )
            cursor += length
            totals[str(row["state"])] += length
        if cursor != len(self.data):
            raise SurfacePropertyByteCoverageError(
                f"{self.path}: byte ledger ends at {cursor}/{len(self.data)}"
            )
        canonical = json.dumps(
            {"path": self.path, "byteLength": len(self.data), "ranges": rows},
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        return {
            "sourcePath": self.path,
            "sourceSha256": hashlib.sha256(self.data).hexdigest(),
            "byteLength": len(self.data),
            "accountedBytes": len(self.data),
            "coveragePercent": 100.0,
            "stateBytes": dict(sorted(totals.items())),
            "rangesSha256": hashlib.sha256(canonical).hexdigest(),
            "ranges": rows,
        }
