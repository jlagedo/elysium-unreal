"""Read the explicit alpha range from pipeline-authored PNGs without third-party packages.

The Unreal commandlet Python environment does not load the project's Pillow dependency. The map
bake only consumes 8-bit, non-interlaced RGB/RGBA PNGs emitted by Pillow, so a small decoder for
that controlled subset lets bake verification distinguish an RGB regression from real alpha.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _paeth(a: int, b: int, c: int) -> int:
    estimate = a + b - c
    da, db, dc = abs(estimate - a), abs(estimate - b), abs(estimate - c)
    if da <= db and da <= dc:
        return a
    return b if db <= dc else c


def alpha_range(path: str | Path) -> tuple[int, int] | None:
    """Return the explicit 8-bit alpha minimum/maximum, or ``None`` for an RGB PNG.

    Raises ``ValueError`` for a corrupt PNG or a format outside the pipeline-authored subset.
    """
    data = Path(path).read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("not a PNG")

    offset = len(PNG_SIGNATURE)
    width = height = bit_depth = color_type = interlace = None
    compressed = bytearray()
    while offset + 12 <= len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        start, end = offset + 8, offset + 8 + length
        if end + 4 > len(data):
            raise ValueError("truncated PNG chunk")
        payload = data[start:end]
        if kind == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", payload)
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        offset = end + 4

    if None in (width, height, bit_depth, color_type, interlace):
        raise ValueError("PNG has no IHDR")
    if bit_depth != 8 or interlace != 0:
        raise ValueError("unsupported PNG depth or interlace")
    if color_type in (0, 2, 3):
        return None
    if color_type not in (4, 6):
        raise ValueError("unsupported PNG colour type")

    channels = 2 if color_type == 4 else 4
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    if len(raw) != height * (stride + 1):
        raise ValueError("unexpected PNG scanline size")

    previous = bytearray(stride)
    low, high = 255, 0
    cursor = 0
    for _ in range(height):
        filter_type = raw[cursor]
        cursor += 1
        encoded = raw[cursor:cursor + stride]
        cursor += stride
        row = bytearray(stride)
        for index, value in enumerate(encoded):
            left = row[index - channels] if index >= channels else 0
            up = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_type == 0:
                decoded = value
            elif filter_type == 1:
                decoded = value + left
            elif filter_type == 2:
                decoded = value + up
            elif filter_type == 3:
                decoded = value + ((left + up) // 2)
            elif filter_type == 4:
                decoded = value + _paeth(left, up, upper_left)
            else:
                raise ValueError("unsupported PNG filter")
            row[index] = decoded & 0xFF
        alpha = row[channels - 1::channels]
        low, high = min(low, min(alpha)), max(high, max(alpha))
        previous = row
    return low, high
