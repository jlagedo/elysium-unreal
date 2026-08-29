"""Binary glTF container reading.

A GLB is a 12-byte header followed by length-prefixed chunks. Only the JSON chunk and
the single BIN chunk carry anything the review tool needs, so this reader stops as soon
as it has what the caller asked for: the corpus holds 1.9 GB of extension JSON and
re-reading a 35 MB JSON chunk to answer a panel query is the tool's worst cost.
"""

from __future__ import annotations

import json
import struct
from pathlib import Path

MAGIC = b"glTF"
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942

_HEADER = struct.Struct("<4sII")
_CHUNK = struct.Struct("<II")


class GlbError(Exception):
    """The bytes are not a GLB this reader can use."""


def read_json(path: str | Path) -> dict:
    """Return the JSON chunk alone, without paging in the BIN chunk."""
    with open(path, "rb") as handle:
        magic, version, _length = _HEADER.unpack(handle.read(_HEADER.size))
        if magic != MAGIC:
            raise GlbError(f"not a GLB: {path}")
        if version != 2:
            raise GlbError(f"unsupported GLB version {version}: {path}")
        chunk_length, chunk_type = _CHUNK.unpack(handle.read(_CHUNK.size))
        if chunk_type != CHUNK_JSON:
            raise GlbError(f"first chunk is not JSON: {path}")
        return json.loads(handle.read(chunk_length))


def read(path: str | Path) -> tuple[dict, bytes]:
    """Return `(document, binary)`. `binary` is empty when the file has no BIN chunk."""
    data = Path(path).read_bytes()
    magic, version, _length = _HEADER.unpack_from(data, 0)
    if magic != MAGIC:
        raise GlbError(f"not a GLB: {path}")
    if version != 2:
        raise GlbError(f"unsupported GLB version {version}: {path}")

    document: dict | None = None
    binary = b""
    offset = _HEADER.size
    while offset + _CHUNK.size <= len(data):
        chunk_length, chunk_type = _CHUNK.unpack_from(data, offset)
        offset += _CHUNK.size
        chunk = data[offset : offset + chunk_length]
        offset += chunk_length
        if chunk_type == CHUNK_JSON:
            document = json.loads(chunk)
        elif chunk_type == CHUNK_BIN:
            binary = chunk
    if document is None:
        raise GlbError(f"no JSON chunk: {path}")
    return document, binary


def buffer_view_bytes(document: dict, binary: bytes, index: int) -> bytes:
    """Slice one bufferView out of the BIN chunk."""
    views = document.get("bufferViews") or []
    try:
        view = views[index]
    except IndexError:
        raise GlbError(f"bufferView {index} does not exist") from None
    if view.get("buffer", 0) != 0:
        raise GlbError("only buffer 0 (the BIN chunk) is supported")
    start = view.get("byteOffset", 0)
    end = start + view["byteLength"]
    if end > len(binary):
        raise GlbError(f"bufferView {index} runs past the BIN chunk")
    return binary[start:end]
