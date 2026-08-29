"""Contract tests for the GLB container reader."""

from __future__ import annotations

import json
import struct
import tempfile
from pathlib import Path

import pytest

from core import glb

from . import support


def _write(root: Path, payload: bytes, name: str = "unit.glb") -> Path:
    path = root / name
    path.write_bytes(payload)
    return path


def test_reads_json_and_binary(tmp_path: Path) -> None:
    path = _write(tmp_path, support.build_glb({"asset": {"version": "2.0"}}, b"\x01\x02\x03\x04"))
    document, binary = glb.read(path)
    assert document["asset"]["version"] == "2.0"
    assert binary[:4] == b"\x01\x02\x03\x04"


def test_json_only_read_skips_the_binary_chunk(tmp_path: Path) -> None:
    # The panel path re-reads headers constantly; paging in a 35 MB BIN chunk to
    # answer a question about JSON would dominate the tool's cost.
    path = _write(tmp_path, support.build_glb({"asset": {"version": "2.0"}}, b"\xff" * 4096))
    assert glb.read_json(path)["asset"]["version"] == "2.0"


def test_document_without_binary_chunk_yields_empty_bytes(tmp_path: Path) -> None:
    path = _write(tmp_path, support.build_glb({"asset": {"version": "2.0"}}))
    _document, binary = glb.read(path)
    assert binary == b""


def test_rejects_a_file_that_is_not_a_glb(tmp_path: Path) -> None:
    path = _write(tmp_path, b"not a glb at all, really")
    with pytest.raises(glb.GlbError):
        glb.read(path)
    with pytest.raises(glb.GlbError):
        glb.read_json(path)


def test_rejects_an_unsupported_container_version(tmp_path: Path) -> None:
    body = support.build_glb({"asset": {"version": "2.0"}})
    path = _write(tmp_path, body[:4] + struct.pack("<I", 3) + body[8:])
    with pytest.raises(glb.GlbError):
        glb.read(path)


def test_rejects_a_document_with_no_json_chunk(tmp_path: Path) -> None:
    chunk = struct.pack("<II", 4, glb.CHUNK_BIN) + b"\x00\x00\x00\x00"
    path = _write(tmp_path, b"glTF" + struct.pack("<II", 2, 12 + len(chunk)) + chunk)
    with pytest.raises(glb.GlbError):
        glb.read(path)


BINARY = bytes(range(64))
DOCUMENT = {
    "bufferViews": [
        {"buffer": 0, "byteOffset": 8, "byteLength": 16},
        {"buffer": 0, "byteLength": 4},
        {"buffer": 1, "byteOffset": 0, "byteLength": 4},
        {"buffer": 0, "byteOffset": 60, "byteLength": 32},
    ]
}


def test_slices_at_the_declared_offset() -> None:
    assert glb.buffer_view_bytes(DOCUMENT, BINARY, 0) == BINARY[8:24]


def test_byte_offset_defaults_to_zero() -> None:
    assert glb.buffer_view_bytes(DOCUMENT, BINARY, 1) == BINARY[0:4]


def test_rejects_a_view_into_a_buffer_that_is_not_the_bin_chunk() -> None:
    with pytest.raises(glb.GlbError):
        glb.buffer_view_bytes(DOCUMENT, BINARY, 2)


def test_rejects_a_view_that_runs_past_the_binary() -> None:
    with pytest.raises(glb.GlbError):
        glb.buffer_view_bytes(DOCUMENT, BINARY, 3)


def test_rejects_a_view_that_does_not_exist() -> None:
    with pytest.raises(glb.GlbError):
        glb.buffer_view_bytes(DOCUMENT, BINARY, 9)


def test_json_chunk_padding_does_not_break_parsing() -> None:
    # glTF pads the JSON chunk with spaces to a four-byte boundary.
    payload = support.build_glb({"a": 1})
    length = struct.unpack_from("<I", payload, 12)[0]
    assert length % 4 == 0
    assert json.loads(payload[20 : 20 + length]) == {"a": 1}
