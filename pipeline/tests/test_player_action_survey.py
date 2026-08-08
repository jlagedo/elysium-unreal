import struct
import unittest

from research.tooling.probes.player_action_survey import (
    ACTION_POLICIES,
    PLAYER_ACTION_COUNT,
    PLAYER_ACTION_NAMES,
    decode_action_names,
    indirect_calls_with_displacement,
    scan_player_anim_vdata,
    validate_instruction_evidence,
)


class _MiniImage:
    def __init__(self):
        self.image_base = 0x10000000
        self.data = bytearray(0x1000)

    def va_to_offset(self, va):
        if self.image_base <= va < self.image_base + len(self.data):
            return va - self.image_base
        if PLAYER_ACTION_NAMES <= va < PLAYER_ACTION_NAMES + PLAYER_ACTION_COUNT * 4:
            return 0x100 + va - PLAYER_ACTION_NAMES
        return None

    def offset_to_va(self, offset):
        return self.image_base + offset

    def read_u32_va(self, va):
        offset = self.va_to_offset(va)
        return struct.unpack_from("<I", self.data, offset)[0]

    def read_cstring_va(self, va):
        offset = self.va_to_offset(va)
        end = self.data.index(0, offset)
        return bytes(self.data[offset:end]).decode("ascii")

    def section_bytes(self, name):
        if name != ".text":
            raise ValueError(name)
        return 0, bytes(self.data)


class PlayerActionSurveyTests(unittest.TestCase):
    def test_action_name_table_decodes_all_seventeen_codes(self):
        image = _MiniImage()
        cursor = 0x400
        for code in range(PLAYER_ACTION_COUNT):
            name = ACTION_POLICIES[code]["expected_name"].encode("ascii") + b"\0"
            struct.pack_into("<I", image.data, 0x100 + code * 4,
                             image.image_base + cursor)
            image.data[cursor:cursor + len(name)] = name
            cursor += len(name)
        rows = decode_action_names(image)
        self.assertEqual(len(rows), 17)
        self.assertEqual(rows[0]["expected_name"], "PLAYER_IDLE")
        self.assertEqual(rows[16]["expected_name"], "PLAYER_LEAVE_AIMING")
        self.assertEqual(
            [row["code"] for row in rows
             if row["reachability"] != "reachable"],
            [3, 6, 15, 16],
        )

    def test_indirect_call_scan_requires_call_modrm_and_exact_displacement(self):
        image = _MiniImage()
        image.data[0x20:0x26] = b"\xff\x90\x04\x07\x00\x00"
        image.data[0x40:0x46] = b"\xff\x92\x04\x07\x00\x00"
        image.data[0x60:0x66] = b"\x8b\x90\x04\x07\x00\x00"
        image.data[0x80:0x86] = b"\xff\x90\x08\x07\x00\x00"
        self.assertEqual(
            indirect_calls_with_displacement(image, 0x704),
            [0x10000020, 0x10000040],
        )

    def test_player_anim_vdata_preserves_each_occurrence(self):
        rows = scan_player_anim_vdata([
            ("vdata/a.txt", b'"Player_Anim" "PLAYER_VOMIT"\n'),
            ("vdata/b.txt", b'"Player_Anim"\t"PLAYER_BLOCK"\n'
                              b'"Player_Anim" "PLAYER_BLOCK"\n'),
            ("vdata/c.txt", b'"Gesture_Anim" "gesture"\n'),
        ])
        self.assertEqual([row["value"] for row in rows],
                         ["PLAYER_VOMIT", "PLAYER_BLOCK", "PLAYER_BLOCK"])
        self.assertEqual(rows[1]["path"], "vdata/b.txt")

    def test_instruction_evidence_requires_exact_bytes(self):
        image = _MiniImage()
        image.data[0x20:0x25] = b"\x01\x02\x03\x04\x05"
        rows = validate_instruction_evidence(
            image,
            ((0x10000020, b"\x01\x02\x03\x04\x05", "test contract"),),
        )
        self.assertEqual(rows[0]["address"], "0x10000020")
        with self.assertRaisesRegex(ValueError, "instruction evidence drift"):
            validate_instruction_evidence(
                image,
                ((0x10000020, b"\x01\x02\x03\x04\x06", "drift"),),
            )


if __name__ == "__main__":
    unittest.main()
