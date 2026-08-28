"""Gapless source-byte accountability for texture members."""

from __future__ import annotations

from collections import Counter
import hashlib
import json


class TextureByteCoverageError(ValueError):
    pass


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
            raise TextureByteCoverageError(f"{self.path}: invalid state {state!r}")
        if offset < 0 or length < 0 or offset + length > len(self.data):
            raise TextureByteCoverageError(
                f"{self.path}: {owner} range {offset}+{length} exceeds {len(self.data)}"
            )
        if not length:
            return
        if state.endswith("-zero") and any(self.data[offset:offset + length]):
            raise TextureByteCoverageError(
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
                raise TextureByteCoverageError(
                    f"{self.path}: byte ledger {kind} at {cursor}, next range starts {offset}"
                )
            cursor += length
            totals[str(row["state"])] += length
        if cursor != len(self.data):
            raise TextureByteCoverageError(
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
