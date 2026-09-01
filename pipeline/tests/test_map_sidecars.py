"""The legacy-sidecar producer's pure functions (R3.2, MP-2.2).

`pipeline/src/elysium_pipeline/exporters/UE_map_sidecars.py` reproduces `UE_bsp_to_scene.py`'s
sidecars from the published V2 map units. Everything the whole-map run depends on that is not a
file read lives in four pure functions, and each of them is a place where a "harmless" cleanup
would change shipped data without changing a test: the hull solver's three tolerances, the output
splitter's four legacy field rules, the binary32 coordinate inversion, and the entity-lump
reconstruction the legacy regexes run over.

The whole-map byte comparison against the legacy sidecars is R3.3's differ, not these tests; these
pin the pieces a differ could only report as an unexplained delta.
"""
from __future__ import annotations

import numpy as np

from elysium_pipeline.exporters.UE_map_sidecars import (
    EntityDivergences,
    brush_hull,
    collect_entity_fields,
    entity_lump_text,
    is_output_key,
    parse_entity_blocks,
    source_planes,
    source_position,
    split_output,
)

# A 20 x 40 x 60 box centred on the model frame, as six halfspaces n.x <= d, plus a seventh
# plane at x <= 5 that only a bevel side names.
_BOX = np.array(
    [[1.0, 0.0, 0.0, 10.0], [-1.0, 0.0, 0.0, 10.0],
     [0.0, 1.0, 0.0, 20.0], [0.0, -1.0, 0.0, 20.0],
     [0.0, 0.0, 1.0, 30.0], [0.0, 0.0, -1.0, 30.0],
     [1.0, 0.0, 0.0, 5.0]],
    dtype=np.float32,
)
_BOX_CORNERS = {(x, y, z) for x in (-10.0, 10.0) for y in (-20.0, 20.0) for z in (-30.0, 30.0)}


def _sides(rows):
    return [{"plane": plane, "bevel": bevel} for plane, bevel in rows]


def _corners(points):
    return {(round(float(p[0]), 1), round(float(p[1]), 1), round(float(p[2]), 1)) for p in points}


def test_brush_hull_returns_the_box_corners_and_skips_bevel_sides():
    # Solving the bevel plane would clip x <= 5 and drop the four x = 10 corners.
    contents, points = brush_hull(_BOX, _sides([(0, 0), (1, 0), (2, 0), (3, 0), (4, 0), (5, 0), (6, 1)]), 0x1)

    assert contents == 0x1
    assert len(points) == 8
    assert _corners(points) == _BOX_CORNERS


def test_brush_hull_abstains_when_fewer_than_four_sides_survive():
    # Three real sides and three bevels: no hull, and the caller must not OR this brush's
    # contents into the entity's.
    contents, points = brush_hull(_BOX, _sides([(0, 0), (1, 0), (2, 0), (3, 1), (4, 1), (5, 1)]), 0x4000)

    assert contents == 0x4000
    assert points is None


def test_split_output_keeps_the_four_legacy_field_rules():
    # `param` is not stripped, `delay` is a plain float(), `times` normalizes an unparsable
    # field to -1, field 6 (`extra`) is dropped, and `python` is stripped.
    row = split_output(" door , Open , slow  , 0.35 , x , taxi() , dropped ")

    assert row == {
        "target": "door",
        "input": "Open",
        "param": " slow  ",
        "delay": 0.35,
        "times": -1,
        "python": "taxi()",
    }
    # Fewer than four commas is not an output at all; it stays a plain keyvalue.
    assert split_output("door,Open,,0") is None


def test_split_output_strip_param_opts_field_2_into_the_common_strip():
    # R3.4: `param` gets the same strip every other string field already gets.
    row = split_output(" door , Open , slow  , 0.35 , x , taxi() , dropped ",
                        EntityDivergences(strip_param=True))
    assert row["param"] == "slow"


def test_split_output_delay_atof_reads_the_longest_numeric_prefix():
    # R3.4: a trailing-junk delay parses the way every other `.ents` number does (`atof`, the
    # longest numeric prefix) instead of rejecting the whole token to 0.0.
    value = "door,Open,slow,3.5s,5,x"
    assert split_output(value)["delay"] == 0.0   # legacy: not a plain float() -> the 0.0 default
    row = split_output(value, EntityDivergences(delay_atof=True))
    assert row["delay"] == 3.5


def test_source_position_inverts_the_transform_in_binary32():
    # 1.5 Source inches -- the half-thickness of sm_pawnshop_1's `havenrm` door panel -- does not
    # come back bit-exactly in binary64, and does in binary32. Solving hulls on the binary64 value
    # changes vertex sets (`seam_map_map.md`).
    gltf = (1.5 * 0.0254, 3.0 * 0.0254, -6.0 * 0.0254)
    assert gltf[0] / 0.0254 != 1.5

    assert source_position(gltf) == (1.5, 6.0, 3.0)

    # A plane row travels the same way: direction for the normal, position scale for the distance.
    planes = source_planes([{"normal": [0.0, 1.0, -0.0], "dist": -3.4036}])
    assert planes.dtype == np.float32
    assert list(planes[0]) == [0.0, 0.0, 1.0, np.float32(-134.0)]


def test_is_output_key_default_is_the_legacy_shape_test():
    # `^(On|Out)` case-insensitively, regardless of what any datamap declares -- the legacy
    # sidecar's rule, and `write_entities`'s default.
    assert is_output_key("game_ui", "PlayerOn", EntityDivergences()) is False
    assert is_output_key("logic_relay", "OnTrigger", EntityDivergences()) is True
    assert is_output_key("trigger_player_activity_level", "OnTrigger", EntityDivergences()) is True


def test_is_output_key_datamap_typing_promotes_and_demotes():
    # R3.4: opting in swaps the shape test for the class's datamap. `game_ui`'s `PlayerOn` is
    # declared under a name the shape test misses (promoted); `trigger_player_activity_level`'s
    # `OnTrigger` is shape-matched but the datamap does not declare it (demoted) -- the shipped
    # example `seam_map_map_entities.md` -> "Outputs" names on `sm_diner_1`.
    typed = EntityDivergences(datamap_output_typing=True)
    assert is_output_key("game_ui", "PlayerOn", typed) is True
    assert is_output_key("trigger_player_activity_level", "OnTrigger", typed) is False
    # An ordinary output is unaffected either way.
    assert is_output_key("logic_relay", "OnTrigger", typed) is True


def test_collect_entity_fields_default_keeps_case_variants_as_separate_slots():
    # Legacy: "Frob" and "frob" are two different `keys` slots, each keeping its own last value.
    _outputs, keys = collect_entity_fields([("Frob", "1"), ("frob", "2")])
    assert keys == {"Frob": "1", "frob": "2"}


def test_collect_entity_fields_fold_keys_collapses_case_variants_and_keeps_last_spelling():
    # R3.4: opting in matches the entities unit's own identity rule (`decode.py`'s `occurrences`
    # map, keyed by the already-folded key) -- one slot, spelled the way the *last* occurrence
    # authored it, not forced lowercase.
    fields = EntityDivergences(fold_keys=True)
    _outputs, keys = collect_entity_fields([("Frob", "1"), ("frob", "2")], fields)
    assert keys == {"frob": "2"}

    # An entity that never repeats a key under two spellings is unaffected either way.
    _outputs, unaffected = collect_entity_fields([("RenderColor", "255 0 0")], fields)
    assert unaffected == {"RenderColor": "255 0 0"}


def test_entity_lump_text_reproduces_the_embedded_quote_the_legacy_regex_trips_on():
    # sm_hub_1's logic_auto authors setArea(\"santa_monica\") inside an output value. The unit
    # unescapes it; the legacy pair regex stopped at the escaped quote and re-paired the rest of
    # the block. The producer has to hand the regex the escaped text back, or the sidecar gains an
    # `origin` the shipped one does not have.
    rows = [
        {
            "index": 0,
            "keyValues": [
                {"sourceKey": "classname", "value": "logic_auto",
                 "quotedKey": True, "quotedValue": True, "byteLength": 24},
                {"sourceKey": "OnMapLoad", "value": ',,,0,-1,setArea("santa_monica"),',
                 "quotedKey": True, "quotedValue": True, "byteLength": 48},
                {"sourceKey": "origin", "value": "-2586.48 -2091.1 -111",
                 "quotedKey": True, "quotedValue": True, "byteLength": 32},
            ],
        }
    ]

    text = entity_lump_text(rows)
    assert '\\"santa_monica\\"' in text

    pairs = parse_entity_blocks(text)[0]
    assert pairs == [
        ("classname", "logic_auto"),
        ("OnMapLoad", ",,,0,-1,setArea(\\"),
        ("),", "origin"),
    ]
