import math
import struct
import unittest

from elysium_pipeline.formats import mdl_skel


class MdlAnimationEventTests(unittest.TestCase):
    def test_decodes_v2531_descriptor_relative_events(self):
        descriptor = 32
        event_base = 160
        image = bytearray(320)
        struct.pack_into("<ii", image, descriptor + 20, 2, event_base - descriptor)
        struct.pack_into("<fii64s", image, event_base, 0.25, 1003, 0, b"7\0")
        struct.pack_into(
            "<fii64s", image, event_base + 76, 0.75, 2050, 0, b"left foot\0"
        )

        events = mdl_skel.read_events(image, descriptor)

        self.assertEqual(
            events,
            (
                mdl_skel.Event(0.25, 1003, 0, "7"),
                mdl_skel.Event(0.75, 2050, 0, "left foot"),
            ),
        )

    def test_rejects_out_of_range_or_unterminated_event_arrays(self):
        descriptor = 16
        image = bytearray(128)
        struct.pack_into("<ii", image, descriptor + 20, 2, 80)
        self.assertEqual(mdl_skel.read_events(image, descriptor), ())

        struct.pack_into("<ii", image, descriptor + 20, 1, 48)
        image[descriptor + 48:descriptor + 48 + 76] = b"\x7f" * 76
        struct.pack_into("<fii", image, descriptor + 48, math.nan, 1003, 0)
        self.assertEqual(mdl_skel.read_events(image, descriptor), ())


if __name__ == "__main__":
    unittest.main()
