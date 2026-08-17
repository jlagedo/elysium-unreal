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


def _seq(label, events):
    return mdl_skel.Seq(label=label, base=0, frames=1, fps=30.0, activity="", actweight=0,
                        flags=0, events=events)


class EventTableTests(unittest.TestCase):
    """The interned per-owning-model timeline the blend sidecar carries."""

    CLIPS = (
        _seq("walk", (mdl_skel.Event(0.25, 2050, 0, "left foot"),
                      mdl_skel.Event(0.75, 2051, 0, "right foot"))),
        _seq("idle", ()),
        _seq("run", (mdl_skel.Event(0.1, 2050, 0, "left foot"),
                     mdl_skel.Event(0.5, 4020, 0, ""))),
    )

    def test_only_sequences_carrying_events_appear(self):
        table = mdl_skel.event_table(self.CLIPS)
        self.assertEqual(sorted(table["events"]), ["run", "walk"])

    def test_options_intern_once_with_the_empty_payload_at_zero(self):
        table = mdl_skel.event_table(self.CLIPS)
        self.assertEqual(table["event_options"], ["", "left foot", "right foot"])
        self.assertEqual(table["event_fields"], ["cycle", "event", "type", "options_i"])
        self.assertEqual(table["events"]["walk"], [[0.25, 2050, 0, 1], [0.75, 2051, 0, 2]])
        # `run` reuses the interned string and names the empty payload by index 0.
        self.assertEqual(table["events"]["run"], [[0.1, 2050, 0, 1], [0.5, 4020, 0, 0]])

    def test_row_order_is_the_descriptors_own(self):
        # Two records on one cycle: the dispatcher fires them in array order, so the sidecar
        # must not sort them by anything.
        clip = _seq("throw", (mdl_skel.Event(0.5, 3005, 0, "b"),
                              mdl_skel.Event(0.5, 2040, 0, "a")))
        self.assertEqual(mdl_skel.event_table((clip,))["events"]["throw"],
                         [[0.5, 3005, 0, 1], [0.5, 2040, 0, 2]])

    def test_a_model_with_no_event_authors_no_block(self):
        self.assertEqual(mdl_skel.event_table((_seq("idle", ()),)), {})

    def test_the_sidecar_carries_events_beside_grids_and_autolayers(self):
        # `pose_parameters` is read off the image; an all-zero header declares none.
        image = bytearray(1024)
        sidecar = mdl_skel.blend_sidecar(image, {}, self.CLIPS)
        self.assertEqual(sidecar["grids"], {})
        self.assertNotIn("autolayers", sidecar)
        self.assertEqual(sorted(sidecar["events"]), ["run", "walk"])

    def test_a_model_authoring_none_of_the_three_ships_no_sidecar(self):
        image = bytearray(1024)
        self.assertEqual(mdl_skel.blend_sidecar(image, {}, (_seq("idle", ()),)), {})


if __name__ == "__main__":
    unittest.main()
