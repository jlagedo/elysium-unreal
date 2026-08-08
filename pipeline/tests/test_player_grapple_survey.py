import struct
import unittest

from research.tooling.probes.player_grapple_survey import (
    decode_player_grapple_modes,
    direct_relative_calls,
)


class _MiniImage:
    def __init__(self):
        self.data = bytearray(0x400)
        self.table = 0x100

    def va_to_offset(self, va):
        return va - 0x10000000 if 0x10000000 <= va < 0x10000400 else None

    def offset_to_va(self, offset):
        return 0x10000000 + offset

    def read_u32_va(self, va):
        offset = self.va_to_offset(va)
        return struct.unpack_from("<I", self.data, offset)[0]

    def section_bytes(self, name):
        self.assert_text(name)
        return 0, bytes(self.data)

    @staticmethod
    def assert_text(name):
        if name != ".text":
            raise ValueError(name)


class PlayerGrappleSurveyTests(unittest.TestCase):
    def test_direct_relative_call_scan_resolves_target(self):
        image = _MiniImage()
        target = 0x10000300
        call = 0x10000020
        image.data[0x20] = 0xE8
        struct.pack_into("<i", image.data, 0x21, target - (call + 5))
        image.data[0x40] = 0xE8
        struct.pack_into("<i", image.data, 0x41, 4)
        self.assertEqual(direct_relative_calls(image, target), [call])

    def test_mode_table_decodes_all_initial_activity_constants(self):
        image = _MiniImage()
        table_va = 0x10328D74
        # Rebase the mini mapper around the retail table while keeping leaf bytes local.
        image.va_to_offset = lambda va: (
            va - table_va if table_va <= va < table_va + 0x24 else
            va - 0x10000000 if 0x10000000 <= va < 0x10000400 else None)
        image.read_u32_va = lambda va: struct.unpack_from(
            "<I", image.data, image.va_to_offset(va))[0]
        registrations = []
        for mode in range(9):
            leaf = 0x10000100 + mode * 0x10
            struct.pack_into("<I", image.data, mode * 4, leaf)
            image.data[0x100 + mode * 0x10] = 0xB8
            struct.pack_into("<I", image.data, 0x101 + mode * 0x10, 100 + mode)
            registrations.append({"id": 100 + mode, "name": "ACT_MODE_%d" % mode})

        modes = decode_player_grapple_modes(image, registrations)
        self.assertEqual(len(modes), 9)
        self.assertEqual(modes[0]["initial_activity"], "ACT_MODE_0")
        self.assertEqual(modes[8]["initial_activity"], "ACT_MODE_8")
        self.assertEqual(modes[3]["continuation_leaf"], "0x10165d90")
        self.assertFalse(modes[7]["has_pinned_binary_producer"])


if __name__ == "__main__":
    unittest.main()
