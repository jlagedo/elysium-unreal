"""Precise decoded source values beside glTF's float32 render projection.

The payload is little-endian binary64 in a zlib stream. Shape, byte count and SHA-256 are
explicit. This module uses only the standard library so offline and editor readers share it.
These are decoded values, never an opaque replacement for decoding the source member.
"""
from __future__ import annotations

from array import array
import hashlib
import math
import sys
import zlib


ENCODING = "zlib-float64-le"


class PrecisionError(ValueError):
    pass


def encode(view_writer, values: bytes, shape):
    count = math.prod(shape)
    if any(type(n) is not int or n < 0 for n in shape) or count * 8 != len(values):
        raise PrecisionError("precise source shape does not match its bytes")
    return {"bufferView": view_writer(zlib.compress(values)), "encoding": ENCODING,
            "shape": list(shape), "sha256": hashlib.sha256(values).hexdigest()}


def decode(document, binary, record, expected_shape):
    if record.get("encoding") != ENCODING or record.get("shape") != list(expected_shape):
        raise PrecisionError("precise source encoding/shape mismatch")
    if any(type(n) is not int or n < 0 for n in expected_shape):
        raise PrecisionError("invalid precise source shape")
    index = record.get("bufferView")
    views = document.get("bufferViews", ())
    if type(index) is not int or not 0 <= index < len(views):
        raise PrecisionError("precise source buffer view is missing")
    view = views[index]
    start, size = view.get("byteOffset", 0), view["byteLength"]
    if view.get("buffer", 0) != 0 or min(start, size) < 0 or start + size > len(binary):
        raise PrecisionError("precise source buffer view exceeds the BIN chunk")
    byte_count = math.prod(expected_shape) * 8
    inflater = zlib.decompressobj()
    try:
        raw = inflater.decompress(binary[start:start + size], byte_count + 1)
    except zlib.error as exc:
        raise PrecisionError(f"invalid precise source stream: {exc}") from exc
    if len(raw) != byte_count or not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:
        raise PrecisionError("precise source byte count/stream termination mismatch")
    if hashlib.sha256(raw).hexdigest() != record.get("sha256"):
        raise PrecisionError("precise source digest mismatch")
    values = array("d")
    values.frombytes(raw)
    if sys.byteorder != "little":
        values.byteswap()
    if not all(math.isfinite(value) for value in values):
        raise PrecisionError("non-finite precise source value")
    return values
