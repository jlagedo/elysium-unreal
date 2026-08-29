"""KTX 2.0 container reading.

A texture unit carries one complete, standalone KTX2 file in a single bufferView. The
core glTF has no `images` entry pointing at it, so a reader that ignores the extension
sees a buffer and no picture -- parsing this is the only way to get pixels.

Two details in the spec are easy to get backwards and both are load-bearing here:
the level index begins at byte 80, after a 32-byte index block the header size alone
does not account for; and the index is ordered largest-level-first while the level data
is physically stored smallest-first, so level offsets decrease as the index advances.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

IDENTIFIER = b"\xabKTX 20\xbb\r\n\x1a\n"

#: Byte offset of the level index: 12 identifier + 36 header + 32 index.
LEVEL_INDEX_OFFSET = 80

_HEADER = struct.Struct("<9I")
_LEVEL = struct.Struct("<QQQ")

SUPERCOMPRESSION_NONE = 0
SUPERCOMPRESSION_BASISLZ = 1
SUPERCOMPRESSION_ZSTD = 2
SUPERCOMPRESSION_ZLIB = 3

# vkFormat values the corpus uses. Block-compressed formats carry a DDS FourCC because
# Blender decodes them by wrapping the level in a DDS header; the rest are decoded by
# indexing bytes directly.
VK_R8G8B8_UNORM = 23
VK_R8G8B8A8_UNORM = 37
VK_R8G8B8A8_UINT = 41
VK_B8G8R8A8_UNORM = 44
VK_BC1_RGB_UNORM = 131
VK_BC1_RGBA_UNORM = 133
VK_BC2_UNORM = 135
VK_BC3_UNORM = 137


@dataclass(frozen=True)
class BlockFormat:
    """How one vkFormat lays out bytes."""

    name: str
    #: Bytes per 4x4 block for compressed formats, else bytes per texel.
    unit_bytes: int
    compressed: bool
    #: DDS FourCC for the compressed formats, else None.
    fourcc: bytes | None = None
    #: Channel count of the uncompressed byte layout, else None.
    channels: int | None = None


FORMATS = {
    VK_R8G8B8_UNORM: BlockFormat("VK_FORMAT_R8G8B8_UNORM", 3, False, channels=3),
    VK_R8G8B8A8_UNORM: BlockFormat("VK_FORMAT_R8G8B8A8_UNORM", 4, False, channels=4),
    VK_R8G8B8A8_UINT: BlockFormat("VK_FORMAT_R8G8B8A8_UINT", 4, False, channels=4),
    VK_B8G8R8A8_UNORM: BlockFormat("VK_FORMAT_B8G8R8A8_UNORM", 4, False, channels=4),
    VK_BC1_RGB_UNORM: BlockFormat("VK_FORMAT_BC1_RGB_UNORM_BLOCK", 8, True, b"DXT1"),
    VK_BC1_RGBA_UNORM: BlockFormat("VK_FORMAT_BC1_RGBA_UNORM_BLOCK", 8, True, b"DXT1"),
    VK_BC2_UNORM: BlockFormat("VK_FORMAT_BC2_UNORM_BLOCK", 16, True, b"DXT3"),
    VK_BC3_UNORM: BlockFormat("VK_FORMAT_BC3_UNORM_BLOCK", 16, True, b"DXT5"),
}


class Ktx2Error(Exception):
    """The bytes are not a KTX2 file this reader can use."""


@dataclass(frozen=True)
class Level:
    """One mip level's placement in the file."""

    index: int
    byte_offset: int
    byte_length: int
    uncompressed_byte_length: int
    width: int
    height: int


@dataclass(frozen=True)
class Ktx2:
    """A parsed KTX2 header and level index."""

    vk_format: int
    type_size: int
    width: int
    height: int
    depth: int
    layer_count: int
    face_count: int
    level_count: int
    supercompression: int
    levels: tuple[Level, ...]

    @property
    def layers(self) -> int:
        """Array elements. `layerCount == 0` means "not an array", i.e. one element."""
        return max(1, self.layer_count)

    @property
    def faces(self) -> int:
        return max(1, self.face_count)

    @property
    def is_cubemap(self) -> bool:
        return self.face_count == 6

    @property
    def is_array(self) -> bool:
        return self.layer_count > 0

    @property
    def format(self) -> BlockFormat | None:
        return FORMATS.get(self.vk_format)

    @property
    def images_per_level(self) -> int:
        return self.layers * self.faces


def _level_dimensions(width: int, height: int, level: int) -> tuple[int, int]:
    """Level dimensions per the spec's `floor(size * 2^-level)`, floored at 1."""
    return max(1, width >> level), max(1, height >> level)


def parse(data: bytes) -> Ktx2:
    """Parse the header and level index of a KTX2 file."""
    if data[:12] != IDENTIFIER:
        raise Ktx2Error("not a KTX2 file")

    (
        vk_format,
        type_size,
        width,
        height,
        depth,
        layer_count,
        face_count,
        level_count,
        supercompression,
    ) = _HEADER.unpack_from(data, 12)

    # `levelCount == 0` asks a loader to generate the chain from the base level. It is
    # illegal for block-compressed formats; the index still holds one entry either way.
    levels = []
    for index in range(max(1, level_count)):
        offset = LEVEL_INDEX_OFFSET + index * _LEVEL.size
        byte_offset, byte_length, uncompressed = _LEVEL.unpack_from(data, offset)
        level_width, level_height = _level_dimensions(width, height, index)
        levels.append(
            Level(
                index=index,
                byte_offset=byte_offset,
                byte_length=byte_length,
                uncompressed_byte_length=uncompressed,
                width=level_width,
                height=level_height,
            )
        )

    return Ktx2(
        vk_format=vk_format,
        type_size=type_size,
        width=width,
        height=height,
        depth=depth,
        layer_count=layer_count,
        face_count=face_count,
        level_count=level_count,
        supercompression=supercompression,
        levels=tuple(levels),
    )


def image_bytes(
    data: bytes, texture: Ktx2, level: int = 0, layer: int = 0, face: int = 0
) -> bytes:
    """Return the bytes of one image within a level.

    Images are concatenated in the order layer, then face, then depth slice, so a
    cubemap face or an array element is a fixed-size slice of the level's data.
    """
    if texture.supercompression != SUPERCOMPRESSION_NONE:
        raise Ktx2Error(
            f"supercompression scheme {texture.supercompression} is not decoded here"
        )
    try:
        entry = texture.levels[level]
    except IndexError:
        raise Ktx2Error(f"level {level} does not exist") from None
    if not 0 <= layer < texture.layers:
        raise Ktx2Error(f"layer {layer} out of range")
    if not 0 <= face < texture.faces:
        raise Ktx2Error(f"face {face} out of range")

    count = texture.images_per_level
    if entry.byte_length % count:
        raise Ktx2Error(
            f"level {level} length {entry.byte_length} does not divide into {count} images"
        )
    size = entry.byte_length // count
    start = entry.byte_offset + (layer * texture.faces + face) * size
    return data[start : start + size]


def expected_image_bytes(texture: Ktx2, level: int) -> int:
    """The byte count one image of a level should occupy, for integrity checking."""
    block = texture.format
    if block is None:
        raise Ktx2Error(f"unknown vkFormat {texture.vk_format}")
    entry = texture.levels[level]
    if block.compressed:
        blocks_x = max(1, (entry.width + 3) // 4)
        blocks_y = max(1, (entry.height + 3) // 4)
        return blocks_x * blocks_y * block.unit_bytes
    # Rows are tightly packed; there is no alignment padding to 4.
    return entry.width * entry.height * block.unit_bytes
