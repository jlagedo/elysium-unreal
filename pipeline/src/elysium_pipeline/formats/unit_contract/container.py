"""The binary glTF container every export_v2 unit is written into and read back from.

One source closure yields one byte-identical product, so the encoder is fixed: compact
separators, writer key order, no NaN or infinity, JSON padded to four bytes with spaces and BIN
padded with zeros. `read_glb` parses the same layout with no tolerance, because a validator that
repairs what it reads cannot testify that the writer produced it.
"""

from __future__ import annotations

from dataclasses import asdict, is_dataclass
from enum import Enum
import json
import os
from pathlib import Path, PurePath
import struct
from typing import Any

GLB_MAGIC = 0x46546C67
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942

#: Header (12) plus one chunk header (8); the smallest thing that can carry a JSON chunk.
_HEADER_BYTES = 12
_CHUNK_HEADER_BYTES = 8


class GlbContainerError(ValueError):
    """The bytes are not the container this contract publishes."""


def plain(value: Any) -> Any:
    """JSON-coercible form of a decode record.

    Dataclasses become their field mapping, tuples become arrays, paths become forward-slashed
    strings and enums become their value, so a seam can build its extension out of the records it
    decoded rather than out of hand-written dictionaries.
    """

    if is_dataclass(value) and not isinstance(value, type):
        return plain(asdict(value))
    if isinstance(value, Enum):
        return plain(value.value)
    if isinstance(value, PurePath):
        return value.as_posix()
    if isinstance(value, dict):
        return {str(key): plain(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [plain(item) for item in value]
    return value


def generator(kind_title: str) -> str:
    return f"Elysium {kind_title} GLB Exporter"


def asset_block(kind_title: str) -> dict[str, str]:
    return {"version": "2.0", "generator": generator(kind_title)}


def encode_glb(document: dict, binary: bytes = b"") -> bytes:
    """Serialize one unit. The BIN chunk is omitted when the unit has no accessor."""

    json_data = json.dumps(
        plain(document), separators=(",", ":"), ensure_ascii=False, allow_nan=False
    ).encode("utf-8")
    json_data += b" " * (-len(json_data) % 4)
    total = _HEADER_BYTES + _CHUNK_HEADER_BYTES + len(json_data)
    padded_binary = b""
    if binary:
        padded_binary = bytes(binary) + b"\0" * (-len(binary) % 4)
        total += _CHUNK_HEADER_BYTES + len(padded_binary)
    output = bytearray(struct.pack("<III", GLB_MAGIC, 2, total))
    output.extend(struct.pack("<II", len(json_data), JSON_CHUNK))
    output.extend(json_data)
    if binary:
        output.extend(struct.pack("<II", len(padded_binary), BIN_CHUNK))
        output.extend(padded_binary)
    return bytes(output)


def write_glb(document: dict, binary: bytes, destination: Path) -> None:
    """Write the unit to a temporary sibling and rename it over the destination.

    Validation runs before the write, so a destination that exists is always a unit that passed:
    the rename is what keeps a failed export from leaving a half-written file behind.
    """

    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_bytes(encode_glb(document, binary))
    os.replace(temporary, destination)


def read_glb(path: Path) -> tuple[dict[str, Any], bytes]:
    """Parse a published unit: the JSON chunk, then the BIN chunk if the unit carries one."""

    return decode_glb(Path(path).read_bytes(), str(path))


def decode_glb(data: bytes, path: Any = "the unit") -> tuple[dict[str, Any], bytes]:
    """The same parse over bytes a caller already holds; `path` only names them in errors.

    A reader that has read the file once -- to hash it, say -- parses it through this rather than
    reading it a second time.
    """

    if len(data) < _HEADER_BYTES + _CHUNK_HEADER_BYTES:
        raise GlbContainerError(f"{path} is only {len(data)} bytes")
    magic, version, total = struct.unpack_from("<III", data)
    if magic != GLB_MAGIC:
        raise GlbContainerError(f"{path} does not open with the glTF magic")
    if version != 2:
        raise GlbContainerError(f"{path} declares glTF container version {version}")
    if total != len(data):
        raise GlbContainerError(f"{path} declares {total} bytes and holds {len(data)}")
    position = _HEADER_BYTES
    chunks: list[tuple[int, bytes]] = []
    while position < len(data):
        if position + _CHUNK_HEADER_BYTES > len(data):
            raise GlbContainerError(f"{path} ends inside a chunk header")
        size, kind = struct.unpack_from("<II", data, position)
        position += _CHUNK_HEADER_BYTES
        end = position + size
        if size % 4 or end > len(data):
            raise GlbContainerError(f"{path} carries a chunk of {size} bytes that does not fit")
        chunks.append((kind, data[position:end]))
        position = end
    if not chunks or chunks[0][0] != JSON_CHUNK:
        raise GlbContainerError(f"{path} must open with a JSON chunk")
    if len(chunks) > 2:
        raise GlbContainerError(f"{path} carries {len(chunks)} chunks; a unit carries JSON+BIN")
    if len(chunks) == 2 and chunks[1][0] != BIN_CHUNK:
        raise GlbContainerError(f"{path} carries a second chunk that is not BIN")
    try:
        document = json.loads(chunks[0][1].decode("utf-8").rstrip(" "))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise GlbContainerError(f"{path} carries invalid GLB JSON: {error}") from error
    if not isinstance(document, dict):
        raise GlbContainerError(f"{path} carries a JSON chunk that is not an object")
    return document, chunks[1][1] if len(chunks) == 2 else b""

