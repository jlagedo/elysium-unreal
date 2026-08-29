"""Contract tests for the DDS decode envelope.

The header is asserted byte by byte because it exists only to be handed to a decoder we
do not control. A flag that is merely plausible will be tolerated by OpenImageIO today
and reinterpreted by something else tomorrow.
"""

from __future__ import annotations

import struct
import unittest

from core import dds


def field(header: bytes, offset: int) -> int:
    return struct.unpack_from("<I", header, offset)[0]


class HeaderLayoutTests(unittest.TestCase):
    def setUp(self) -> None:
        self.payload = b"\xaa" * 32
        self.header = dds.build(self.payload, 8, 4, b"DXT1")

    def test_magic_and_structure_sizes_are_the_declared_constants(self) -> None:
        self.assertEqual(self.header[0:4], b"DDS ")
        self.assertEqual(field(self.header, 4), 124)
        self.assertEqual(field(self.header, 76), 32)
        self.assertEqual(len(self.header), dds.HEADER_SIZE + len(self.payload))

    def test_width_and_height_are_not_transposed(self) -> None:
        # Height precedes width in a DDS header and reads naturally the other way.
        self.assertEqual(field(self.header, 12), 4)
        self.assertEqual(field(self.header, 16), 8)

    def test_compressed_surfaces_declare_linear_size_not_pitch(self) -> None:
        # Pitch describes a scanline, which a block-compressed surface does not have.
        flags = field(self.header, 8)
        self.assertTrue(flags & dds.DDSD_LINEARSIZE)
        self.assertFalse(flags & 0x8, "DDSD_PITCH must not be set on a block surface")
        self.assertEqual(
            flags,
            dds.DDSD_CAPS | dds.DDSD_HEIGHT | dds.DDSD_WIDTH | dds.DDSD_PIXELFORMAT | dds.DDSD_LINEARSIZE,
        )

    def test_pixel_format_declares_a_fourcc(self) -> None:
        self.assertEqual(field(self.header, 80), dds.DDPF_FOURCC)
        self.assertEqual(self.header[84:88], b"DXT1")

    def test_linear_size_defaults_to_the_whole_payload(self) -> None:
        self.assertEqual(field(self.header, 20), len(self.payload))

    def test_payload_follows_the_header_unmodified(self) -> None:
        self.assertEqual(self.header[dds.HEADER_SIZE :], self.payload)

    def test_rejects_a_fourcc_that_is_not_four_bytes(self) -> None:
        with self.assertRaises(ValueError):
            dds.build(self.payload, 8, 4, b"DXT")


class SingleSurfaceTests(unittest.TestCase):
    def test_a_lone_level_declares_neither_mipmaps_nor_complexity(self) -> None:
        header = dds.build(b"\x00" * 8, 4, 4, b"DXT1")
        self.assertFalse(field(header, 8) & dds.DDSD_MIPMAPCOUNT)
        self.assertEqual(field(header, 28), 0)
        self.assertEqual(field(header, 108), dds.DDSCAPS_TEXTURE)
        self.assertEqual(field(header, 112), 0)


class MipChainTests(unittest.TestCase):
    def test_a_chain_sets_complex_so_a_reader_can_seek_past_level_zero(self) -> None:
        # OpenImageIO refuses miplevel != 0 unless DDSCAPS_COMPLEX is set, so a chain
        # written without it is silently truncated to its base level.
        header = dds.build(b"\x00" * 40, 8, 8, b"DXT5", level_zero_bytes=32, mip_count=3)
        caps = field(header, 108)
        self.assertTrue(caps & dds.DDSCAPS_COMPLEX)
        self.assertTrue(caps & dds.DDSCAPS_MIPMAP)
        self.assertTrue(field(header, 8) & dds.DDSD_MIPMAPCOUNT)
        self.assertEqual(field(header, 28), 3)

    def test_linear_size_describes_level_zero_not_the_whole_chain(self) -> None:
        header = dds.build(b"\x00" * 40, 8, 8, b"DXT5", level_zero_bytes=32, mip_count=3)
        self.assertEqual(field(header, 20), 32)


class CubemapTests(unittest.TestCase):
    def test_a_cube_declares_every_face_and_complexity(self) -> None:
        # Without DDSCAPS_COMPLEX a cube DDS is rejected outright rather than degraded.
        header = dds.build(b"\x00" * 48, 4, 4, b"DXT1", cubemap=True)
        self.assertTrue(field(header, 108) & dds.DDSCAPS_COMPLEX)
        self.assertEqual(field(header, 112), dds.DDSCAPS2_CUBEMAP_ALL)
        self.assertTrue(field(header, 112) & dds.DDSCAPS2_CUBEMAP)


class Bc1AlphaTests(unittest.TestCase):
    """BC1 carries punch-through alpha in the mode where color0 <= color1."""

    @staticmethod
    def block(color0: int, color1: int) -> bytes:
        return struct.pack("<HH", color0, color1) + b"\x00" * 4

    def test_four_colour_blocks_are_reported_opaque(self) -> None:
        payload = self.block(0xF800, 0x0001) + self.block(0x07E0, 0x0002)
        self.assertTrue(dds.bc1_blocks_are_opaque(payload))

    def test_a_single_punch_through_block_makes_the_surface_transparent(self) -> None:
        payload = self.block(0xF800, 0x0001) + self.block(0x0001, 0xF800)
        self.assertFalse(dds.bc1_blocks_are_opaque(payload))

    def test_equal_endpoints_select_the_punch_through_mode(self) -> None:
        # The test is color0 > color1, so equality is the transparent branch.
        self.assertFalse(dds.bc1_blocks_are_opaque(self.block(0x1234, 0x1234)))

    def test_an_empty_surface_is_vacuously_opaque(self) -> None:
        self.assertTrue(dds.bc1_blocks_are_opaque(b""))


if __name__ == "__main__":
    unittest.main()
