"""The BIN payload one map-visibility unit carries: one decompressed bitset per recorded row.

Every row that decoded to at least one byte is a `SCALAR`/`UNSIGNED_BYTE` accessor of
`rowByteLength` bytes over a single buffer view, so the decompressed set is core glTF data a
general reader can take without the extension, and the extension states only what glTF cannot:
which cluster the row belongs to, where its compressed span sits and which offset two clusters
shared. A row that decoded to nothing gets no accessor -- `accessor.count` has a minimum of one
in glTF 2.0 -- and is stated by its entry's `emptyRow` and its `*-row-short` anomaly instead.

Accessor offsets are padded to four bytes because the container's own rule is that a view -- and
every accessor inside it -- starts aligned. The padding is product bytes, not source bytes: it
pays for nothing in the ledger and the source's own rows are byte-for-byte the accessor's extent.
"""

from __future__ import annotations

from typing import Any

#: `UNSIGNED_BYTE`, and the one element shape a bitset row has.
COMPONENT_TYPE = 5121
ACCESSOR_TYPE = "SCALAR"
ALIGNMENT = 4


class BitsetPayload:
    """Append decompressed rows, get back the accessor index each one was written as."""

    __slots__ = ("buffer", "accessors")

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.accessors: list[dict[str, Any]] = []

    def add(self, row: bytes) -> int:
        """Write one non-empty row and return the accessor that reads it."""

        if not row:
            raise ValueError("a bitset accessor holds at least one byte")
        offset = len(self.buffer)
        self.buffer.extend(row)
        self.buffer.extend(b"\0" * (-len(row) % ALIGNMENT))
        self.accessors.append(
            {
                "bufferView": 0,
                "byteOffset": offset,
                "componentType": COMPONENT_TYPE,
                "type": ACCESSOR_TYPE,
                "count": len(row),
            }
        )
        return len(self.accessors) - 1

    @property
    def binary(self) -> bytes:
        return bytes(self.buffer)

    def views(self) -> list[dict[str, Any]]:
        """The one view every accessor reads through, or none when no row was recorded."""

        if not self.buffer:
            return []
        return [{"buffer": 0, "byteOffset": 0, "byteLength": len(self.buffer)}]
