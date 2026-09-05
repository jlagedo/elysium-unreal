"""R7.4 (water-complete contract 3, `docs/architecture/water-architecture.md`): a face row
carrying a non-0/non-255 lightstyle (`objects/surf`'s 34 faces on `sm_pier_1`, 21 of them also
`32`) becomes its own world/sky chunk, keyed beside the existing (cell, opaque) bucket by the
style its `_V2Material.light_style` names -- `stage_level` tags that chunk's actor
`ElysiumBakedTags::LightStyle(style)` (`elysium.style=<n>`) and `set_fog` leaves every primitive's
CustomPrimitiveData slot 6 at 1.0 by default, so the light rig's clock only ever has to touch the
tagged ones.

`_chunk_world` is a pure function of `(model, mats, blend, cell_cm)` -- no `unreal` needed to
exercise it directly, and `chunk_style_suffix`/`parse_chunk_style` are a pure round trip -- but the
module is an editor entry point that cannot otherwise import, so it is loaded the same
fake-editor-module way `test_bake_map_sky.py`/`test_level_sidecar_recipe.py` load it.
"""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]


def _fake_unreal():
    editor = SimpleNamespace(
        does_directory_exist=lambda target: True,
        make_directory=lambda target: True,
        does_asset_exist=lambda target: False,
        load_asset=lambda target: None,
        list_assets=lambda package, recursive=True, include_folder=True: [],
    )
    return SimpleNamespace(
        AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
        MaterialEditingLibrary=object(),
        GeometryScript_Collision=object(),
        EditorAssetLibrary=editor,
        Paths=SimpleNamespace(project_dir=lambda: str(REPO)),
        SystemLibrary=SimpleNamespace(get_command_line=lambda: ""),
        AssetRegistryHelpers=SimpleNamespace(
            get_asset_registry=mock.Mock(side_effect=SystemExit(0))),
        LinearColor=lambda *values: values,
        log=lambda *a, **k: None,
        log_warning=lambda *a, **k: None,
        log_error=lambda *a, **k: None,
    )


def _load_bake_map(export_root: str):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_bake_map_lightstyle", REPO / "pipeline/unreal/bake_map.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"unreal": _fake_unreal()}), \
            mock.patch.dict(os.environ, {"ELYSIUM_EXPORT_ROOT": export_root}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        try:
            # bake_map is an editor entry point, so importing it runs main(); the faked registry
            # scan exits it at once, with every definition already made.
            spec.loader.exec_module(module)
        except SystemExit:
            pass
    return module


@pytest.fixture(scope="module")
def module():
    with tempfile.TemporaryDirectory() as out:
        yield _load_bake_map(out)


# ------------------------------------------------------------------------ chunk_style_suffix

def test_style_zero_suffix_is_empty(module) -> None:
    # The overwhelming majority: nothing on a map carries a lightstyle, and every chunk name must
    # stay exactly what it was before this lane existed.
    assert module.chunk_style_suffix(0) == ""


def test_nonzero_style_suffix_and_round_trip(module) -> None:
    assert module.chunk_style_suffix(5) == "_S5"
    assert module.parse_chunk_style("SM_World_1_-2_3_S5") == ("SM_World_1_-2_3", 5)


def test_unsuffixed_name_parses_to_style_zero(module) -> None:
    assert module.parse_chunk_style("SM_World_T_1_-2_3") == ("SM_World_T_1_-2_3", 0)


def test_negative_coordinates_do_not_confuse_the_style_parse(module) -> None:
    # A styled chunk two cells negative in every axis: only the trailing `_S<digits>` run is a
    # style, never a coordinate.
    name = "SM_Sky_T_-4_-8_-12" + module.chunk_style_suffix(32)
    base, style = module.parse_chunk_style(name)
    assert base == "SM_Sky_T_-4_-8_-12"
    assert style == 32


# ------------------------------------------------------------------------------- _chunk_world

def _model(*groups):
    """One triangle per named group, all centred at the origin (cell (0, 0, 0))."""
    positions = [(0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (0.0, 10.0, 0.0)]
    return SimpleNamespace(positions=positions,
                           groups={name: [0, 1, 2] for name in groups})


def _mat(opaque=True, light_style=0):
    return SimpleNamespace(opaque=opaque, light_style=light_style)


def test_unstyled_groups_share_one_bucket_as_before(module) -> None:
    bake = module.Bake.__new__(module.Bake)
    model = _model("a", "b")
    mats = {"a": _mat(), "b": _mat()}
    buckets = bake._chunk_world(model, mats, [], module.CELL_CM)
    assert set(buckets.keys()) == {(0, 0, 0, True, 0)}
    assert set(buckets[(0, 0, 0, True, 0)].keys()) == {"a", "b"}


def test_styled_group_gets_its_own_bucket_in_the_same_cell(module) -> None:
    bake = module.Bake.__new__(module.Bake)
    model = _model("objects/surf", "brick/wall")
    mats = {"objects/surf": _mat(light_style=1), "brick/wall": _mat()}
    buckets = bake._chunk_world(model, mats, [], module.CELL_CM)
    assert set(buckets.keys()) == {(0, 0, 0, True, 0), (0, 0, 0, True, 1)}
    assert list(buckets[(0, 0, 0, True, 1)].keys()) == ["objects/surf"]
    assert list(buckets[(0, 0, 0, True, 0)].keys()) == ["brick/wall"]


def test_a_material_with_no_light_style_attribute_reads_as_unstyled(module) -> None:
    # The legacy lane's own `bake_lib.MatDef` carries no `light_style` at all -- `_chunk_world`
    # must not raise on it, and must bucket it exactly as an explicit style-0 material would.
    bake = module.Bake.__new__(module.Bake)
    model = _model("legacy/unit")
    mats = {"legacy/unit": SimpleNamespace(opaque=True)}
    buckets = bake._chunk_world(model, mats, [], module.CELL_CM)
    assert set(buckets.keys()) == {(0, 0, 0, True, 0)}


# -------------------------------------------------------------------------- chunk_actor_tags

def test_an_unstyled_world_chunk_carries_only_its_lane_tag(module) -> None:
    # Every map before this lane existed, and every unstyled chunk on a map with styles: the tag
    # list must be exactly what it was, or the runtime's world walk changes on 100% of the corpus.
    assert module.chunk_actor_tags("SM_World_1_-2_3") == [module.TAG_WORLD]
    assert module.chunk_actor_tags("SM_World_T_1_-2_3") == [module.TAG_WORLD]


def test_an_unstyled_sky_chunk_carries_the_sky_lane_tag(module) -> None:
    assert module.chunk_actor_tags("SM_Sky_T_-4_-8_-12") == [module.TAG_SKY]


def test_a_styled_chunk_carries_the_light_style_tag_beside_its_lane_tag(module) -> None:
    # `ElysiumBakedTags::LightStyle(1)` -- the pier's 34 `objects/surf` foam cards. Beside the
    # lane tag, never instead of it: `UElysiumMapVisuals::AdoptBakedLevel` reaches a styled chunk
    # only through the `elysium.world`/`elysium.sky` branch it already took.
    name = "SM_World_1_-2_3" + module.chunk_style_suffix(1)
    assert module.chunk_actor_tags(name) == [module.TAG_WORLD, "elysium.style=1"]


def test_a_styled_sky_chunk_keeps_both_facts(module) -> None:
    name = "SM_Sky_T_-4_-8_-12" + module.chunk_style_suffix(32)
    assert module.chunk_actor_tags(name) == [module.TAG_SKY, "elysium.style=32"]


def test_the_style_tag_spelling_is_the_prefix_the_runtime_parses(module) -> None:
    # One fact, two spellings: `bake_map.LIGHT_STYLE_TAG_PREFIX` restates
    # `ElysiumBakedTags::LightStyle`'s `"elysium.style=%d"`, and `bake_verify` restates it again.
    name = "SM_World_0_0_0" + module.chunk_style_suffix(7)
    assert module.chunk_actor_tags(name)[1] == module.LIGHT_STYLE_TAG_PREFIX + "7"


# ---------------------------------------------------------------------------------- set_fog

def test_set_fog_appends_the_light_style_default(module) -> None:
    written = {}

    def capture(index, values):
        written["index"] = index
        written["values"] = list(values)

    component = SimpleNamespace(set_default_custom_primitive_data_float_array=capture)
    module.set_fog(component, [0.1, 0.2, 0.3, 1.0, 100.0, 0.001])
    assert written["index"] == module.FOG_CPD_COLOR
    assert written["values"] == [0.1, 0.2, 0.3, 1.0, 100.0, 0.001, module.LIGHT_STYLE_CPD_DEFAULT]
    assert written["values"][module.LIGHT_STYLE_CPD_SLOT] == 1.0


# ------------------------------------------------------------------------- brush_slot_style

# A brush entity's mesh is never placed in the baked level, so the `elysium.style=<n>` actor tag
# above cannot reach it: `UElysiumEntityBodies::BuildBrushVisual` builds one component per body long
# after `AdoptBakedLevel` walked the level. The style therefore rides the mesh's own material slot
# names -- which is where `sm_pier_1`'s 17 `objects/surf` foam bodies, the census's motivating case
# for G6, actually carry it. These pin the fold both halves depend on.


# The `section_key` -> `safe_name` -> `brush_slot_style` round trip is pinned in
# `test_map_geometry.py`, where `map_geometry` imports normally: this module loads `bake_map.py`
# through a patched `sys.modules`, which a numpy import cannot survive.


def test_every_section_must_agree_on_one_style() -> None:
    # One primitive carries one CustomPrimitiveData slot, so a mesh that mixes styles -- or mixes a
    # styled section with an unstyled one -- animates on none rather than darkening the unstyled
    # half along with the styled one.
    from elysium_pipeline.asset_names import brush_slot_style

    assert brush_slot_style(["a_style1", "b_style1"]) == 1
    assert brush_slot_style(["a_style1", "b_style32"]) == 0
    assert brush_slot_style(["a_style1", "b"]) == 0
    assert brush_slot_style([]) == 0


def test_a_slot_name_that_only_looks_styled_is_not_one() -> None:
    from elysium_pipeline.asset_names import brush_slot_style

    assert brush_slot_style(["style1"]) == 0           # no group key folded away in front of it
    assert brush_slot_style(["a_style"]) == 0          # no index
    assert brush_slot_style(["a_style1x"]) == 0        # not an index
    assert brush_slot_style(["a_style0"]) == 0         # 0 is "the base style", never animated
