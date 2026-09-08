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

import json
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pytest

from elysium_pipeline import paths

from elysium_pipeline.exporters.UE_map_sidecars import (
    EntityDivergences,
    brush_cull_max_cm,
    brush_hull,
    collect_entity_fields,
    entity_lump_text,
    is_output_key,
    meshed_faces,
    parse_entity_blocks,
    rope_material_id,
    source_planes,
    source_position,
    split_output,
    write_ropes,
)
from elysium_pipeline.importers.materials import asset_path_for

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


def test_split_output_keep_extra_adds_field_6_verbatim_only_when_authored():
    # R3.4: `extra` (field 6, everything past `python`) is present only when the split actually
    # reaches it, verbatim including any further commas (`FUN_100ccf90`'s residue join).
    fields = EntityDivergences(keep_extra=True)
    assert split_output(" door , Open , slow  , 0.35 , x , taxi() , a,b ", fields)["extra"] == " a,b "
    six_field = split_output("door,Open,slow,0.35,1,py", fields)
    assert "extra" not in six_field
    assert "extra" not in split_output("door,Open,slow,0.35,1,py")   # legacy default: dropped


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
    # changes vertex sets.

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
    # example is on `sm_diner_1`.
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


# --- R6.4: brush fade distances ------------------------------------------------------------------


@pytest.mark.parametrize(
    ("classname", "keys", "expected"),
    [
        ("func_lod", {"DisappearDist": "2500"}, 6350.0),          # sp_tutorial_1's own rows
        ("func_lod", {"DisappearDist": "2200"}, 5588.0),
        ("FUNC_LOD", {"DisappearDist": "3000.5abc"}, 7621.27),    # C atof: the longest numeric prefix
        ("func_lod", {"DisappearDist": "0"}, None),               # zero is "never culled", not "at zero"
        ("func_lod", {}, None),
        ("func_areaportalwindow", {"FadeStartDist": "1000", "FadeDist": "1280"}, None),  # the R7 owner call
        ("func_brush", {"DisappearDist": "2500"}, None),          # the key means nothing on another class
    ],
)
def test_brush_cull_max_cm_is_disappear_dist_times_2_54_on_func_lod_only(classname, keys, expected):
    assert brush_cull_max_cm(classname, keys) == expected


WORKING_MAPS = ("sp_tutorial_1", "sm_pawnshop_1", "sm_hub_1")


@pytest.mark.parametrize("map_name", WORKING_MAPS)
def test_every_meshed_func_lod_row_carries_its_cull_range_in_the_ents(map_name):
    """Corpus-gated: the shipped `.ents` (the file the R4.1 asset must reproduce) carries
    `cull_max_cm` on every meshed `func_lod`, equal to its own `DisappearDist x 2.54`, and on no
    other row."""

    ents = paths.export_root() / map_name / f"{map_name}.ents"
    if not ents.is_file():
        pytest.skip(f"{ents} is not exported on this machine")
    rows = json.loads(ents.read_text(encoding="ascii"))["entities"]
    lods = [row for row in rows if row["classname"].lower() == "func_lod" and row.get("brush_mesh")]
    assert lods, f"{map_name} has no meshed func_lod row"
    for row in lods:
        assert row["cull_max_cm"] == brush_cull_max_cm("func_lod", row["keys"])
        assert row["brush_mesh"] == f"brush_{row['model']}"
    assert [row for row in rows if "cull_max_cm" in row] == lods



# --- R7.4 (G25): a 3D-skybox brush entity contributes no collision -------------------------


@pytest.mark.parametrize("map_name", ("sm_pier_1",) + WORKING_MAPS)
def test_a_sky_brush_entity_carries_no_hulls_in_the_ents(map_name):
    """Corpus-gated. `sm_pier_1`'s `brush_8/9/10` are sky-flagged `func_brush` Solids that lived at
    raw z ~= 4939 -- above the map's own `world_maxs.z 512` -- and the runtime scales a miniature's
    hulls by the sky scale (x16 here), which landed them at world z -644..-628: three invisible
    collision slabs 21 inches under the harbour surface, with no VtMB counterpart. The miniature is
    drawn from its own camera and nothing travels into it, so it contributes no collider.

    Everything else about the row stays: the visual (`brush_mesh`), the contents word and
    `blocks_player` are the entity's, and a brush entity in the play volume still collides."""

    from elysium_pipeline.exporters.UE_map_sidecars import build_entities, prepare_join

    unit = paths.export_v2_root() / "maps" / f"{map_name}.glb"
    if not unit.is_file():
        pytest.skip(f"no exported map root unit at {unit}")

    join = prepare_join(map_name)
    rows, stats = build_entities(join.units, join.sky, join.pair_blocks, join.brush_meshes)
    brush_rows = [row for row in rows if "hulls" in row]
    sky_rows = [row for row in brush_rows if row.get("sky")]

    assert brush_rows, f"{map_name} has no brush entity at all"
    assert all(row["hulls"] == [] for row in sky_rows)
    assert any(row["hulls"] for row in brush_rows)
    # The count the file states is the count it carries, miniatures excluded.
    assert stats["hulls"] == sum(len(row["hulls"]) for row in brush_rows)
    if map_name == "sm_pier_1":
        # The three the audit measured, still present as entities with their visuals.
        assert len(sky_rows) >= 3
        assert all("contents" in row for row in sky_rows)


# --- R6.5: the `.ropes` line carries the material's unit id, and nothing about its look -----

@pytest.mark.parametrize("keys, expected", [
    ({}, "vtmb:material:cable/cable"),
    ({"ropeshader": "2"}, "vtmb:material:cable/chain"),
    ({"ropeshader": "1", "ropematerial": "cable/rope_x"}, "vtmb:material:cable/rope"),
    ({"ropematerial": "Cable\\ChainB"}, "vtmb:material:cable/chainb"),
    ({"ropematerial": "materials/cable/metalcable.vmt"}, "vtmb:material:cable/metalcable"),
])
def test_rope_material_id_is_the_shader_row_or_the_authored_material(keys, expected):
    # `RopeShader` wins over `RopeMaterial` (CRopeKeyframe::KeyValue); the key is
    # `shared_corpus.material_key`, so the runtime's fold lands on the imported `MI_`.
    assert rope_material_id(keys) == expected
    assert asset_path_for(expected[len("vtmb:material:"):]).startswith("/ElysiumBaked/Materials/")


def test_write_ropes_emits_twelve_tokens_led_by_the_material_id(tmp_path):
    blocks = [
        [("classname", "move_rope"), ("targetname", "a1"), ("NextKey", "a2"),
         ("origin", "0 0 0"), ("RopeShader", "2"), ("Type", "2"), ("Width", "2"),
         ("Slack", "25"), ("Dangling", "1")],
        [("classname", "keyframe_rope"), ("targetname", "a2"), ("origin", "100 0 0"),
         ("RopeMaterial", "cable/rope")],
        # a chain end with no NextKey emits no segment
    ]
    units = SimpleNamespace(name="synthetic")
    report = write_ropes(units, blocks, tmp_path)
    assert report == {"segments": 1, "nodes": 2}
    lines = (tmp_path / "synthetic.ropes").read_text().splitlines()
    assert len(lines) == 1
    tokens = lines[0].split()
    assert len(tokens) == 12
    assert tokens[0] == "vtmb:material:cable/chain"          # the start node's shader row
    assert tokens[9] == "2"                                   # Type 2 -> two nodes
    assert tokens[11] == "1"                                  # Dangling
    assert asset_path_for("cable/chain") == "/ElysiumBaked/Materials/cable/MI_chain"


@pytest.mark.parametrize("map_name", ["sp_tutorial_1", "sm_pawnshop_1", "sm_hub_1"])
def test_every_exported_rope_line_names_an_importable_material(map_name):
    # Corpus-gated: the three working maps' `.ropes` as the runtime reads them.
    try:
        path = paths.export_root() / map_name / f"{map_name}.ropes"
    except Exception:  # noqa: BLE001 - no roots on this machine
        pytest.skip("no export root")
    if not path.is_file():
        pytest.skip(f"no exported .ropes for {map_name}")
    lines = [line.split() for line in path.read_text().splitlines() if line.strip()]
    assert lines
    for tokens in lines:
        assert len(tokens) == 12
        assert tokens[0].startswith("vtmb:material:")
        assert asset_path_for(tokens[0][len("vtmb:material:"):]).startswith(
            "/ElysiumBaked/Materials/")


def _meshed_units(rows):
    """A units stand-in for `meshed_faces`: one texinfo and one texdata per named material."""

    faces = []
    texinfos = []
    textures = []
    for index, (material, no_draw) in enumerate(rows):
        faces.append({"numEdges": 4, "texInfo": index, "noDraw": no_draw})
        texinfos.append({"texData": index})
        textures.append({"asset": f"vtmb:material:{material}"})
    root = {
        "faces": faces,
        "texinfos": texinfos,
        "textures": textures,
        "models": [{"index": 0, "firstFace": 0, "numFaces": len(faces)}],
    }
    return SimpleNamespace(root=root)


def test_meshed_faces_drops_a_nodraw_face_the_tools_name_test_cannot_see():
    """R7.1 follow-up: `%compilenodraw` outside `tools/` is a skip, and the name test still is one.

    `tools/toolstrigger` leaves the flag clear, so only the name says it draws nothing; a
    `%compilenodraw` unit outside the `tools/` namespace leaves the name clear, so only the flag
    says it. Dropping either test puts an opaque sheet in the world.
    """

    units = _meshed_units([
        ("brick/bricks01", False),
        ("effects/nodrawsheet", True),
        ("tools/toolstrigger", False),
        ("tools/toolsblack", False),
    ])
    scenes = meshed_faces(units, SimpleNamespace(faces=set()), set())
    assert scenes == {"world": [0, 3], "sky": [], "brush": {}}


def test_a_compilewater_face_is_never_dropped_for_nodraw():
    """R7.4, owner decision 2 -- the named modernization "surface on nodraw water".

    All 50 of `sm_pier_1`'s water faces are `SURF_NODRAW` and were dropped, which left the map's
    whole swimmable volume drawing nothing: VtMB painted the ocean as a skybox card because a 2004
    engine could not draw a live surface there, and the port draws the authored plane instead. The
    exemption is on `%compilewater`, the key vbsp itself read to make the brush water -- not on the
    material's name or family, because `water/invisible_water` resolves to `M_V2_Unlit` today and
    the pier's own `_depth_33` patch is a different unit facing the other way.

    The predicate takes the unit's OWN key: only that spelling reaches `%compilewater` through a
    patched unit's `patchBase`.
    """

    rows = [
        ("water/invisible_water", True),
        ("maps/sm_pier_1/water/invisible_water_depth_33", True),
        ("effects/nodrawsheet", True),
        ("tools/toolstrigger", True),
    ]
    units = _meshed_units(rows)
    asked: list[str] = []

    def compile_water(material):
        asked.append(material)
        return "water" in material

    scenes = meshed_faces(units, SimpleNamespace(faces=set()), set(), compile_water)
    assert scenes == {"world": [0, 1], "sky": [], "brush": {}}
    # The `tools/` name test runs first and is not up for negotiation: a trigger brush is not water
    # however its material is flagged.
    assert "tools/toolstrigger" not in asked
    assert asked[:2] == ["water/invisible_water",
                         "maps/sm_pier_1/water/invisible_water_depth_33"]

    # No predicate is the sidecar producer, which has no material staging tree behind it: the drop
    # stands, byte for byte as before.
    assert meshed_faces(units, SimpleNamespace(faces=set()), set()) == {
        "world": [], "sky": [], "brush": {}}
