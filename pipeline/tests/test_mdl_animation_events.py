import math
import struct

from elysium_pipeline.formats import mdl_skel


def test_decodes_v2531_descriptor_relative_events():
    descriptor = 32
    event_base = 160
    image = bytearray(320)
    struct.pack_into("<ii", image, descriptor + 20, 2, event_base - descriptor)
    struct.pack_into("<fii64s", image, event_base, 0.25, 1003, 0, b"7\0")
    struct.pack_into(
        "<fii64s", image, event_base + 76, 0.75, 2050, 0, b"left foot\0"
    )

    events = mdl_skel.read_events(image, descriptor)

    assert events == (
        mdl_skel.Event(0.25, 1003, 0, "7"),
        mdl_skel.Event(0.75, 2050, 0, "left foot"),
    )


def test_rejects_out_of_range_or_unterminated_event_arrays():
    descriptor = 16
    image = bytearray(128)
    struct.pack_into("<ii", image, descriptor + 20, 2, 80)
    assert mdl_skel.read_events(image, descriptor) == ()

    struct.pack_into("<ii", image, descriptor + 20, 1, 48)
    image[descriptor + 48:descriptor + 48 + 76] = b"\x7f" * 76
    struct.pack_into("<fii", image, descriptor + 48, math.nan, 1003, 0)
    assert mdl_skel.read_events(image, descriptor) == ()


def _seq(label, events):
    return mdl_skel.Seq(label=label, base=0, frames=1, fps=30.0, activity="", actweight=0,
                        flags=0, events=events)


CLIPS = (
    _seq("walk", (mdl_skel.Event(0.25, 2050, 0, "left foot"),
                  mdl_skel.Event(0.75, 2051, 0, "right foot"))),
    _seq("idle", ()),
    _seq("run", (mdl_skel.Event(0.1, 2050, 0, "left foot"),
                 mdl_skel.Event(0.5, 4020, 0, ""))),
)


# EventTableTests
# The interned per-owning-model timeline the blend sidecar carries.

def test_only_sequences_carrying_events_appear():
    table = mdl_skel.event_table(CLIPS)
    assert sorted(table["events"]) == ["run", "walk"]


def test_options_intern_once_with_the_empty_payload_at_zero():
    table = mdl_skel.event_table(CLIPS)
    assert table["event_options"] == ["", "left foot", "right foot"]
    assert table["event_fields"] == ["cycle", "event", "type", "options_i"]
    assert table["events"]["walk"] == [[0.25, 2050, 0, 1], [0.75, 2051, 0, 2]]
    # `run` reuses the interned string and names the empty payload by index 0.
    assert table["events"]["run"] == [[0.1, 2050, 0, 1], [0.5, 4020, 0, 0]]


def test_row_order_is_the_descriptors_own():
    # Two records on one cycle: the dispatcher fires them in array order, so the sidecar
    # must not sort them by anything.
    clip = _seq("throw", (mdl_skel.Event(0.5, 3005, 0, "b"),
                          mdl_skel.Event(0.5, 2040, 0, "a")))
    assert mdl_skel.event_table((clip,))["events"]["throw"] == [[0.5, 3005, 0, 1], [0.5, 2040, 0, 2]]


def test_a_model_with_no_event_authors_no_block():
    assert mdl_skel.event_table((_seq("idle", ()),)) == {}


def test_the_sidecar_carries_events_beside_grids_and_autolayers():
    # `pose_parameters` is read off the image; an all-zero header declares none.
    image = bytearray(1024)
    sidecar = mdl_skel.blend_sidecar(image, {}, CLIPS)
    assert sidecar["grids"] == {}
    assert "autolayers" not in sidecar
    assert sorted(sidecar["events"]) == ["run", "walk"]


def test_a_model_authoring_none_of_the_four_ships_no_sidecar():
    image = bytearray(1024)
    assert mdl_skel.blend_sidecar(image, {}, (_seq("idle", ()),)) == {}


RECORD = mdl_skel.Movement(endframe=4, motionflags=0x1040, v0=2.0, v1=4.0, angle=0.0,
                           vector=(1.0, 0.0, 0.0), position=(3.0, 0.0, 0.0))


# MovementTableTests
# The per-owning-model displacement paths the same sidecar carries.
#
# A `Seq` built outside `local_sequences` from a raw animation carries `movement=None` and
# was never asked; `()` is the model's own answer that the animation authors no displacement.
# The sidecar has to keep those apart, because the second is what makes retail's
# `Studio_AnimMovement` refuse and the first is a reader looking at an older file.

def test_a_clip_nobody_asked_about_authors_no_table():
    assert mdl_skel.movement_table((_seq("idle", ()),)) == {}


def test_asked_and_empty_ships_the_columns_with_no_rows():
    clip = _seq("idle", ())._replace(movement=())
    table = mdl_skel.movement_table((clip,))
    assert table["movement_fields"][0] == "end_frame"
    assert "movement" not in table


def test_only_sequences_carrying_a_record_appear():
    clips = (_seq("idle", ())._replace(movement=()),
             _seq("lunge", ())._replace(movement=(RECORD,)))
    table = mdl_skel.movement_table(clips)
    assert sorted(table["movement"]) == ["lunge"]
    assert table["movement"]["lunge"] == [[4, 0x1040, 5.08, 10.16, 0.0, 1.0, 0.0, 0.0, 7.62, 0.0, 0.0]]


def test_movement_alone_is_reason_enough_to_write_the_sidecar():
    # An attack bank authors no grid at all -- every melee sequence is a single cell -- so a
    # sidecar gated on the other three would drop the only place the lunge is stated.
    image = bytearray(1024)
    sidecar = mdl_skel.blend_sidecar(image, {}, (_seq("lunge", ())._replace(
        movement=(RECORD,)),))
    assert sidecar["grids"] == {}
    assert "events" not in sidecar
    assert sorted(sidecar["movement"]) == ["lunge"]
