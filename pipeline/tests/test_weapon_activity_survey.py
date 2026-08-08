import json
import struct
import tempfile
import unittest
from pathlib import Path

from research.tooling.probes.weapon_activity_survey import (
    constant_return,
    survey_equipment,
)


class _MiniImage:
    def __init__(self):
        self.data = bytearray(64)

    def va_to_offset(self, va):
        return va - 0x1000 if 0x1000 <= va < 0x1040 else None


class WeaponActivitySurveyTests(unittest.TestCase):
    def test_constant_return_follows_relative_jump_thunk(self):
        image = _MiniImage()
        image.data[0] = 0xE9
        struct.pack_into("<i", image.data, 1, 0x1010 - 0x1005)
        image.data[0x10] = 0xB8
        struct.pack_into("<I", image.data, 0x11, 0x12345678)
        image.data[0x15] = 0xC3
        image.data[0x20:0x23] = b"\x33\xc0\xc3"

        self.assertEqual(constant_return(image, 0x1000), (0x12345678, 0x1010))
        self.assertEqual(constant_return(image, 0x1020), (0, 0x1020))

    def test_equipment_join_prefers_most_derived_alias(self):
        classes = [
            {
                "cpp_class": "CWeaponBase",
                "bases": ["CWeaponBase", "CBaseCombatWeapon"],
                "entity_classnames": ["item_w_test"],
                "row_count": 0,
            },
            {
                "cpp_class": "CWeaponDerived",
                "bases": ["CWeaponDerived", "CWeaponBase", "CBaseCombatWeapon"],
                "entity_classnames": ["item_w_test"],
                "row_count": 2,
            },
        ]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            map_dir = root / "test_map"
            map_dir.mkdir()
            (map_dir / "test_map.ents").write_text(json.dumps({
                "map": "test_map",
                "entities": [{
                    "classname": "npc_test",
                    "targetname": "owner",
                    "additionalequipment": "item_w_test",
                    "alternateequipment": "0",
                }],
            }), encoding="utf-8")
            rows = survey_equipment(root, classes)

        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["status"], "resolved_table")
        self.assertEqual(rows[0]["cpp_classes"], ["CWeaponDerived"])
        self.assertEqual(rows[0]["discarded_base_aliases"], ["CWeaponBase"])


if __name__ == "__main__":
    unittest.main()
