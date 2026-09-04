"""Read the authored alpha minimum from a STAGED texture DDS, without numpy or Pillow.

The sibling of `png_alpha.py`, and for the same reason: bake verification runs inside the Unreal
commandlet interpreter, which loads neither the project's Pillow nor its numpy, so the reader for
the one controlled file shape it needs lives here.

The shape is exactly what `importers/texture_dds.stage_dds` writes -- a DX10-header DDS of 8-bit
four-channel pixels, mips largest first (`B8G8R8A8_UNORM`, DXGI 87, for everything the lane
decodes out of a block-compressed KTX2 level; `R8G8B8A8_UNORM`, DXGI 28/29/30, for an
already-uncompressed source). Alpha is the fourth byte in both orders, so the channel order does
not matter here. Only the **top mip** is read: it is what Unreal's own alpha detection resolves
`AutoDXT` against, and a mip is a filtered copy of it.

Why the question is asked at all: on the V2 lane a baked `Texture2D` reports
`HasAlphaChannel = False` exactly when `TC_Default` resolved `AutoDXT` to DXT1, which happens
exactly when no source texel is non-opaque. "No alpha channel" is therefore a statement about the
AUTHORED payload, not about the bake -- and telling the two apart is what
`bake_verify.verify_v2_materials` needs (`seam_map_map.md` -> the alpha assertion).
"""

from __future__ import annotations

from pathlib import Path
import struct

DDS_MAGIC = b"DDS "
DX10_FOURCC = b"DX10"
HEADER_BYTES = 124
DX10_HEADER_BYTES = 20
DDPF_FOURCC = 0x4

#: DXGI formats this reader admits: the four-byte, 8-bit-per-channel ones the staging lane emits.
#: Every one of them carries alpha in the fourth byte (`texture_dds.VK_FORMATS`).
FOUR_BYTE_DXGI = frozenset({28, 29, 30, 87, 88, 91})


class DdsAlphaError(ValueError):
    """The file is not a staged DDS this reader admits."""


def alpha_minimum(path: str | Path) -> int:
    """The smallest alpha byte in the file's top mip; `255` when every texel is opaque.

    Raises `DdsAlphaError` for anything outside the staged subset -- a caller that cannot read the
    payload must say so rather than assume either answer.
    """
    data = Path(path).read_bytes()
    if len(data) < 4 + HEADER_BYTES or data[:4] != DDS_MAGIC:
        raise DdsAlphaError("not a DDS")
    header_size, _flags, height, width = struct.unpack_from("<IIII", data, 4)
    if header_size != HEADER_BYTES:
        raise DdsAlphaError("DDS header size is not 124")
    pf_flags, fourcc = struct.unpack_from("<I4s", data, 76 + 4)
    if not (pf_flags & DDPF_FOURCC) or fourcc != DX10_FOURCC:
        raise DdsAlphaError("staged DDS files carry a DX10 header")
    offset = 4 + HEADER_BYTES
    dxgi = struct.unpack_from("<I", data, offset)[0]
    offset += DX10_HEADER_BYTES
    if dxgi not in FOUR_BYTE_DXGI:
        raise DdsAlphaError("DXGI format %d is not a four-byte 8-bit format" % dxgi)
    if width <= 0 or height <= 0:
        raise DdsAlphaError("DDS declares an empty top mip")
    end = offset + width * height * 4
    if end > len(data):
        raise DdsAlphaError("DDS is shorter than its own top mip")
    return min(data[offset + 3:end:4])
