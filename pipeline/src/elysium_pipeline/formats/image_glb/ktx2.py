"""Restricted deterministic KTX 2.0 writer for the five admitted VtMB image formats.

Every image unit wraps one level, one face, one layer of uncompressed pixels -- never a mip
chain, never a cubemap -- so this is a single-level specialisation of the same Khronos Data
Format Descriptor construction `formats/texture_glb/ktx2.py` uses for its uncompressed formats.
"""

from __future__ import annotations

import hashlib
import math
import struct

IDENTIFIER = b"\xabKTX 20\xbb\r\n\x1a\n"

VK_FORMAT_B8G8R8A8_UNORM = 44
VK_FORMAT_B8G8R8_UNORM = 30
VK_FORMAT_A1R5G5B5_UNORM_PACK16 = 8
VK_FORMAT_R8_UNORM = 9
VK_FORMAT_R8_UINT = 13

_VK_NAMES = {
    VK_FORMAT_B8G8R8A8_UNORM: "VK_FORMAT_B8G8R8A8_UNORM",
    VK_FORMAT_B8G8R8_UNORM: "VK_FORMAT_B8G8R8_UNORM",
    VK_FORMAT_A1R5G5B5_UNORM_PACK16: "VK_FORMAT_A1R5G5B5_UNORM_PACK16",
    VK_FORMAT_R8_UNORM: "VK_FORMAT_R8_UNORM",
    VK_FORMAT_R8_UINT: "VK_FORMAT_R8_UINT",
}

_CHANNEL_IDS = {"R": 0, "G": 1, "B": 2, "A": 15}

#: colorPrimaries=1 (BT709), transferFunction=1 (SRGB) for genuine colour channels; an index
#: plane carries neither, mirroring how `texture_glb.ktx2` treats its own UINT channel format.
#: `typeSize` per the KTX 2.0 header: for a packed format it is the byte size of the whole packed
#: word (2 for `A1R5G5B5_UNORM_PACK16`); for every unpacked byte-channel format here it is 1. A
#: loader uses this field to decide whether the payload needs endian conversion.
_FORMAT_SPECS = {
    VK_FORMAT_B8G8R8A8_UNORM: {
        "bytes": 4, "typeSize": 1, "primaries": 1, "transfer": 1,
        "samples": [(0, 8, "B", 255), (8, 8, "G", 255), (16, 8, "R", 255), (24, 8, "A", 255)],
    },
    VK_FORMAT_B8G8R8_UNORM: {
        "bytes": 3, "typeSize": 1, "primaries": 1, "transfer": 1,
        "samples": [(0, 8, "B", 255), (8, 8, "G", 255), (16, 8, "R", 255)],
    },
    VK_FORMAT_A1R5G5B5_UNORM_PACK16: {
        "bytes": 2, "typeSize": 2, "primaries": 1, "transfer": 1,
        "samples": [(0, 5, "B", 31), (5, 5, "G", 31), (10, 5, "R", 31), (15, 1, "A", 1)],
    },
    VK_FORMAT_R8_UNORM: {
        "bytes": 1, "typeSize": 1, "primaries": 1, "transfer": 1,
        "samples": [(0, 8, "R", 255)],
    },
    VK_FORMAT_R8_UINT: {
        "bytes": 1, "typeSize": 1, "primaries": 0, "transfer": 0,
        "samples": [(0, 8, "R", 1)],
    },
}


def bytes_per_pixel(vk_format: int) -> int:
    return _FORMAT_SPECS[vk_format]["bytes"]


def type_size_for(vk_format: int) -> int:
    """The KTX2 header's `typeSize` field for one admitted `vkFormat`."""

    return _FORMAT_SPECS[vk_format]["typeSize"]


def vk_format_name(vk_format: int) -> str:
    return _VK_NAMES[vk_format]


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def _sample_words(bit_offset: int, bit_count: int, channel: str, upper: int) -> list[int]:
    return [
        bit_offset | ((bit_count - 1) << 16) | (_CHANNEL_IDS[channel] << 24),
        0,
        0,
        upper,
    ]


def dfd_bytes(vk_format: int) -> bytes:
    """The Basic Data Format Descriptor for one admitted `vkFormat`.

    Public so the validator can recompute the expected descriptor independently of whatever
    bytes a file on disk actually carries at its declared offset.
    """

    spec = _FORMAT_SPECS[vk_format]
    samples = [_sample_words(*sample) for sample in spec["samples"]]
    block_size = 24 + len(samples) * 16
    words = [
        4 + block_size,
        0,
        2 | (block_size << 16),
        1 | (spec["primaries"] << 8) | (spec["transfer"] << 16),
        0,                      # texelBlockDimension0-3: all zero -> a 1x1x1x1 texel block
        spec["bytes"],
        0,
    ]
    for sample in samples:
        words.extend(sample)
    return struct.pack(f"<{len(words)}I", *words)


def build(width: int, height: int, vk_format: int, pixel_data: bytes) -> tuple[bytes, dict[str, object]]:
    """One KTX2 file: one level, one face, `layerCount` 0, no supercompression."""

    if vk_format not in _FORMAT_SPECS:
        raise ValueError(f"unsupported KTX2 vkFormat {vk_format}")
    plane = bytes_per_pixel(vk_format)
    if width <= 0 or height <= 0:
        raise ValueError(f"KTX2 requires a positive extent, got {width}x{height}")
    if len(pixel_data) != width * height * plane:
        raise ValueError(
            f"pixel data is {len(pixel_data)} bytes; {width}x{height}@{plane} needs "
            f"{width * height * plane}"
        )
    dfd = dfd_bytes(vk_format)
    dfd_offset = 80 + 24               # header(80) + one level-index entry(24)
    dfd_length = len(dfd)
    alignment = math.lcm(plane, 4)
    level_offset = _align(dfd_offset + dfd_length, alignment)
    physical = bytearray(level_offset)
    physical[dfd_offset:dfd_offset + dfd_length] = dfd
    physical.extend(pixel_data)

    header = bytearray(IDENTIFIER)
    header.extend(
        struct.pack("<9I", vk_format, type_size_for(vk_format), width, height, 0, 0, 1, 1, 0)
    )
    header.extend(struct.pack("<4I2Q", dfd_offset, dfd_length, 0, 0, 0, 0))
    header.extend(struct.pack("<3Q", level_offset, len(pixel_data), len(pixel_data)))
    physical[:len(header)] = header

    payload = bytes(physical)
    return payload, {
        "mimeType": "image/ktx2",
        "byteLength": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "vkFormat": vk_format_name(vk_format),
        "vkFormatValue": vk_format,
    }
