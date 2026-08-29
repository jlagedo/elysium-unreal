"""Minimal DDS container writing, used only as a decode envelope.

Blender 5.2 has no KTX2 reader but decodes DDS through OpenImageIO, and the block bytes
in a KTX2 level are byte-identical to the block bytes in a DDS surface. Wrapping a level
in a 128-byte header therefore turns Blender's own decoder into our BC decoder, with no
third-party dependency and no temporary file.

Only what that envelope needs is implemented: no DX10 header, no uncompressed layouts,
no volume textures.
"""

from __future__ import annotations

import struct

MAGIC = b"DDS "
HEADER_SIZE = 128

DDSD_CAPS = 0x1
DDSD_HEIGHT = 0x2
DDSD_WIDTH = 0x4
DDSD_PIXELFORMAT = 0x1000
DDSD_MIPMAPCOUNT = 0x20000
DDSD_LINEARSIZE = 0x80000

DDPF_FOURCC = 0x4

DDSCAPS_COMPLEX = 0x8
DDSCAPS_TEXTURE = 0x1000
DDSCAPS_MIPMAP = 0x400000

DDSCAPS2_CUBEMAP = 0x200
#: All six face bits together with the cubemap bit.
DDSCAPS2_CUBEMAP_ALL = 0xFE00

#: Flags for a block-compressed surface. DDSD_LINEARSIZE, not DDSD_PITCH: pitch is the
#: uncompressed-layout flag and describes a scanline, which a block surface has none of.
BASE_FLAGS = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_LINEARSIZE


def build(
    payload: bytes,
    width: int,
    height: int,
    fourcc: bytes,
    *,
    level_zero_bytes: int | None = None,
    mip_count: int = 1,
    cubemap: bool = False,
) -> bytes:
    """Wrap block-compressed `payload` in a DDS file.

    `payload` is the surface data exactly as it will be read: one level, or a full mip
    chain, or six faces. `level_zero_bytes` is the size of the largest level when the
    payload holds more than one; it defaults to the whole payload.

    `DDSCAPS_COMPLEX` is mandatory whenever more than one surface is present. Without it
    OpenImageIO refuses to seek past mip level 0 and rejects a cubemap outright.
    """
    if len(fourcc) != 4:
        raise ValueError(f"FourCC must be four bytes, got {fourcc!r}")

    flags = BASE_FLAGS
    caps = DDSCAPS_TEXTURE
    caps2 = 0

    if mip_count > 1:
        flags |= DDSD_MIPMAPCOUNT
        caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP
    if cubemap:
        caps |= DDSCAPS_COMPLEX
        caps2 = DDSCAPS2_CUBEMAP_ALL

    header = bytearray(HEADER_SIZE)
    header[0:4] = MAGIC
    struct.pack_into("<I", header, 4, 124)
    struct.pack_into("<I", header, 8, flags)
    struct.pack_into("<I", header, 12, height)
    struct.pack_into("<I", header, 16, width)
    struct.pack_into("<I", header, 20, level_zero_bytes if level_zero_bytes is not None else len(payload))
    struct.pack_into("<I", header, 24, 0)
    struct.pack_into("<I", header, 28, mip_count if mip_count > 1 else 0)
    struct.pack_into("<I", header, 76, 32)
    struct.pack_into("<I", header, 80, DDPF_FOURCC)
    header[84:88] = fourcc
    struct.pack_into("<I", header, 108, caps)
    struct.pack_into("<I", header, 112, caps2)

    return bytes(header) + payload


def bc1_blocks_are_opaque(payload: bytes) -> bool:
    """Whether every BC1 block selects the four-colour mode.

    BC1's other mode (`color0 <= color1`) decodes index-3 texels to transparent black,
    so a plain DXT1 surface can still carry punch-through alpha. When no block uses that
    mode the decoded alpha plane is uniformly opaque and can be ignored.
    """
    endpoints = memoryview(payload).cast("B")
    for offset in range(0, len(payload) - 7, 8):
        color0 = endpoints[offset] | (endpoints[offset + 1] << 8)
        color1 = endpoints[offset + 2] | (endpoints[offset + 3] << 8)
        if color0 <= color1:
            return False
    return True
