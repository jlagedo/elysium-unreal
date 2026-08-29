"""Contract tests for the DDS decode envelope.

The header is asserted byte by byte because it exists only to be handed to a decoder we
do not control. A flag that is merely plausible will be tolerated by OpenImageIO today
and reinterpreted by something else tomorrow.
"""

from __future__ import annotations

import struct

import pytest

from core import dds

def field(header: bytes, offset: int) -> int:
    return struct.unpack_from("<I", header, offset)[0]


PAYLOAD = b"\xaa" * 32
HEADER = dds.build(PAYLOAD, 8, 4, b"DXT1")


def test_magic_and_structure_sizes_are_the_declared_constants() -> None:
    assert HEADER[0:4] == b"DDS "
    assert field(HEADER, 4) == 124
    assert field(HEADER, 76) == 32
    assert len(HEADER) == dds.HEADER_SIZE + len(PAYLOAD)


def test_width_and_height_are_not_transposed() -> None:
    # Height precedes width in a DDS header and reads naturally the other way.
    assert field(HEADER, 12) == 4
    assert field(HEADER, 16) == 8


def test_compressed_surfaces_declare_linear_size_not_pitch() -> None:
    # Pitch describes a scanline, which a block-compressed surface does not have.
    flags = field(HEADER, 8)
    assert flags & dds.DDSD_LINEARSIZE
    assert not flags & 0x8, "DDSD_PITCH must not be set on a block surface"
    assert flags == dds.DDSD_CAPS | dds.DDSD_HEIGHT | dds.DDSD_WIDTH | dds.DDSD_PIXELFORMAT | dds.DDSD_LINEARSIZE


def test_pixel_format_declares_a_fourcc() -> None:
    assert field(HEADER, 80) == dds.DDPF_FOURCC
    assert HEADER[84:88] == b"DXT1"


def test_linear_size_defaults_to_the_whole_payload() -> None:
    assert field(HEADER, 20) == len(PAYLOAD)


def test_payload_follows_the_header_unmodified() -> None:
    assert HEADER[dds.HEADER_SIZE :] == PAYLOAD


def test_rejects_a_fourcc_that_is_not_four_bytes() -> None:
    with pytest.raises(ValueError):
        dds.build(PAYLOAD, 8, 4, b"DXT")


def test_a_lone_level_declares_neither_mipmaps_nor_complexity() -> None:
    header = dds.build(b"\x00" * 8, 4, 4, b"DXT1")
    assert not field(header, 8) & dds.DDSD_MIPMAPCOUNT
    assert field(header, 28) == 0
    assert field(header, 108) == dds.DDSCAPS_TEXTURE
    assert field(header, 112) == 0


def test_a_chain_sets_complex_so_a_reader_can_seek_past_level_zero() -> None:
    # OpenImageIO refuses miplevel != 0 unless DDSCAPS_COMPLEX is set, so a chain
    # written without it is silently truncated to its base level.
    header = dds.build(b"\x00" * 40, 8, 8, b"DXT5", level_zero_bytes=32, mip_count=3)
    caps = field(header, 108)
    assert caps & dds.DDSCAPS_COMPLEX
    assert caps & dds.DDSCAPS_MIPMAP
    assert field(header, 8) & dds.DDSD_MIPMAPCOUNT
    assert field(header, 28) == 3


def test_linear_size_describes_level_zero_not_the_whole_chain() -> None:
    header = dds.build(b"\x00" * 40, 8, 8, b"DXT5", level_zero_bytes=32, mip_count=3)
    assert field(header, 20) == 32


def test_a_cube_declares_every_face_and_complexity() -> None:
    # Without DDSCAPS_COMPLEX a cube DDS is rejected outright rather than degraded.
    header = dds.build(b"\x00" * 48, 4, 4, b"DXT1", cubemap=True)
    assert field(header, 108) & dds.DDSCAPS_COMPLEX
    assert field(header, 112) == dds.DDSCAPS2_CUBEMAP_ALL
    assert field(header, 112) & dds.DDSCAPS2_CUBEMAP


# Bc1AlphaTests
# BC1 carries punch-through alpha in the mode where color0 <= color1.

# Bc1AlphaTests
# BC1 carries punch-through alpha in the mode where color0 <= color1.

# Bc1AlphaTests
# BC1 carries punch-through alpha in the mode where color0 <= color1.

def block(color0: int, color1: int) -> bytes:
    return struct.pack("<HH", color0, color1) + b"\x00" * 4


def test_four_colour_blocks_are_reported_opaque() -> None:
    payload = block(0xF800, 0x0001) + block(0x07E0, 0x0002)
    assert dds.bc1_blocks_are_opaque(payload)


def test_a_single_punch_through_block_makes_the_surface_transparent() -> None:
    payload = block(0xF800, 0x0001) + block(0x0001, 0xF800)
    assert not dds.bc1_blocks_are_opaque(payload)


def test_equal_endpoints_select_the_punch_through_mode() -> None:
    # The test is color0 > color1, so equality is the transparent branch.
    assert not dds.bc1_blocks_are_opaque(block(0x1234, 0x1234))


def test_an_empty_surface_is_vacuously_opaque() -> None:
    assert dds.bc1_blocks_are_opaque(b"")
