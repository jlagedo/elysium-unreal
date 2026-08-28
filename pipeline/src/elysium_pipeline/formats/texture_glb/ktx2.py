"""Restricted deterministic KTX 2.0 writer for the admitted VtMB formats."""

from __future__ import annotations

import hashlib
import math
import struct

from elysium_pipeline.formats.texture_glb.decode import FMT_DXT1, FMT_DXT3, FMT_DXT5, FMT_UVWQ8888


IDENTIFIER = b"\xabKTX 20\xbb\r\n\x1a\n"


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def _sample(bit_offset: int, bit_count: int, channel: int, upper: int) -> list[int]:
    return [
        bit_offset | ((bit_count - 1) << 16) | (channel << 24),
        0,
        0,
        upper,
    ]


def _dfd(model) -> bytes:
    info = model.format
    if info.source_enum == FMT_DXT1:
        color_model, samples, bytes_plane = 128, [_sample(0, 64, 1, 0xFFFFFFFF)], 8
        block = (3, 3, 0, 0)
        primaries, transfer = 1, 1
    elif info.source_enum in (FMT_DXT3, FMT_DXT5):
        color_model = 129 if info.source_enum == FMT_DXT3 else 130
        samples = [_sample(0, 64, 15, 0xFFFFFFFF), _sample(64, 64, 0, 0xFFFFFFFF)]
        bytes_plane = 16
        block = (3, 3, 0, 0)
        primaries, transfer = 1, 1
    else:
        color_model, block, bytes_plane = 1, (0, 0, 0, 0), info.block_bytes
        if info.source_enum == FMT_UVWQ8888:
            primaries, transfer, upper = 0, 0, 1
        else:
            primaries, transfer, upper = 1, 1, 255
        channel_ids = {"R": 0, "G": 1, "B": 2, "A": 15, "U": 0, "V": 1, "W": 2, "Q": 15}
        samples = [
            _sample(index * 8, 8, channel_ids[channel], upper)
            for index, channel in enumerate(info.channels)
        ]
    block_size = 24 + len(samples) * 16
    words = [
        4 + block_size,
        0,
        2 | (block_size << 16),
        color_model | (primaries << 8) | (transfer << 16),
        block[0] | (block[1] << 8) | (block[2] << 16) | (block[3] << 24),
        bytes_plane,
        0,
    ]
    for sample in samples:
        words.extend(sample)
    return struct.pack(f"<{len(words)}I", *words)


def build(model) -> tuple[bytes, dict[str, object]]:
    levels = [b"".join(level.images) for level in model.levels]
    if not levels or any(not level for level in levels):
        raise ValueError("KTX2 requires non-empty image levels")
    dfd = _dfd(model)
    level_count = len(levels)
    dfd_offset = 80 + level_count * 24
    dfd_length = len(dfd)
    alignment = math.lcm(model.format.block_bytes, 4)
    cursor = _align(dfd_offset + dfd_length, alignment)
    offsets = [0] * level_count
    physical = bytearray(cursor)
    physical[dfd_offset:dfd_offset + dfd_length] = dfd
    for level_index in reversed(range(level_count)):
        cursor = _align(cursor, alignment)
        if len(physical) < cursor:
            physical.extend(b"\0" * (cursor - len(physical)))
        offsets[level_index] = cursor
        physical.extend(levels[level_index])
        cursor += len(levels[level_index])

    layer_count = model.frames if model.frames > 1 else 0
    face_count = 6 if model.cubemap else 1
    header = bytearray(IDENTIFIER)
    header.extend(struct.pack(
        "<9I",
        model.format.vk_format,
        1,
        model.width,
        model.height,
        0,
        layer_count,
        face_count,
        level_count,
        0,
    ))
    header.extend(struct.pack("<4I2Q", dfd_offset, dfd_length, 0, 0, 0, 0))
    for index, level in enumerate(levels):
        header.extend(struct.pack("<3Q", offsets[index], len(level), len(level)))
    physical[:len(header)] = header
    payload = bytes(physical)
    return payload, {
        "mimeType": "image/ktx2",
        "byteLength": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "vkFormat": model.format.vk_name,
        "vkFormatValue": model.format.vk_format,
        "textureType": "cubemap-array" if model.cubemap and model.frames > 1 else (
            "cubemap" if model.cubemap else ("2d-array" if model.frames > 1 else "2d")
        ),
        "supercompression": "none",
    }
