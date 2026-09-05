"""`pipeline/unreal/bake_verify.py::verify_water` (R7.1) and its R7.4 (water-complete) extensions:
the fluid/pieces/leaf-and-near-box counts on each volume, the `_Underside` twin instance every
staged `underside` materials row names (contract 1), and the carrier every staged `lightStyle`
materials row needs (contract 3) -- the world/sky chunk's actor tag for a world face, the brush
mesh's own `_style<n>` slot names for a brush-entity face, which is where `sm_pier_1`'s 17
`objects/surf` foam bodies actually live.

The module is an editor entry point, loaded the same fake-`unreal` way `test_bake_verify_lanes.py`
loads it.
"""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import sys
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]


def _fake_unreal(existing_assets=()):
    existing = set(existing_assets)
    editor = SimpleNamespace(
        does_directory_exist=lambda target: True,
        make_directory=lambda target: True,
        does_asset_exist=lambda target: target in existing,
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
        LinearColor=lambda *values: values,
        log=lambda *a, **k: None,
        log_warning=lambda *a, **k: None,
        log_error=lambda *a, **k: None,
    )


def _load(existing_assets=()):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_bake_verify_water", REPO / "pipeline/unreal/bake_verify.py")
    assert spec and spec.loader
    loaded = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"unreal": _fake_unreal(existing_assets)}), \
            mock.patch.dict(os.environ, {"ELYSIUM_WORK_ROOT": str(REPO)}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        with pytest.raises(SystemExit):
            spec.loader.exec_module(loaded)   # main() refuses: no -BakeMaps= on the command line
    return loaded


@pytest.fixture
def module():
    return _load()


class FakeStruct:
    def __init__(self, **data):
        self._data = dict(data)

    def get_editor_property(self, name):
        return self._data[name]


class FakeActor:
    def __init__(self, tags, materials=None):
        self.tags = list(tags)
        self.static_mesh_component = (
            FakeStruct(static_mesh=FakeMesh(materials)) if materials is not None else None)


class FakeMaterial:
    def __init__(self, path):
        self._path = path

    def get_path_name(self):
        # Unreal spells an asset reference `<package>.<object>`; the stage's rows carry the
        # package path alone, which is what `chunk_bound_materials` folds to.
        return "%s.%s" % (self._path, self._path.rsplit("/", 1)[-1])


class FakeMesh:
    def __init__(self, material_paths, slot_names=None):
        names = list(slot_names or [""] * len(material_paths))
        self._slots = [FakeStruct(material_interface=FakeMaterial(path), material_slot_name=name)
                       for path, name in zip(material_paths, names)]

    def get_editor_property(self, name):
        assert name == "static_materials"
        return list(self._slots)


# --------------------------------------------------------------------------- _verify_water_volume

def test_verify_water_volume_matches_on_the_original_four_fields(module) -> None:
    volume = FakeStruct(index=0, surface_z_cm=-100.0, brushes=[FakeStruct(), FakeStruct()])
    row = {"index": 0, "surfaceZCm": -100.0, "brushes": [{}, {}]}
    assert module._verify_water_volume(volume, row) == []


def test_verify_water_volume_reports_a_disagreeing_index(module) -> None:
    volume = FakeStruct(index=1, surface_z_cm=-100.0, brushes=[])
    row = {"index": 0, "surfaceZCm": -100.0, "brushes": []}
    problems = module._verify_water_volume(volume, row)
    assert any("index 1, staged 0" in p for p in problems)


def test_verify_water_volume_skips_fluid_when_the_row_stages_none(module) -> None:
    # `volume.get_editor_property("fluid")` is never even called when the row carries no fluid --
    # an older manifest or a pre-Lane-D actor must not explode here.
    volume = FakeStruct(index=0, surface_z_cm=0.0, brushes=[])
    row = {"index": 0, "surfaceZCm": 0.0, "brushes": []}
    assert module._verify_water_volume(volume, row) == []


def test_verify_water_volume_checks_fluid_when_staged(module) -> None:
    fluid = FakeStruct(has_fluid=True, index=5, density=1000.0)
    volume = FakeStruct(index=0, surface_z_cm=0.0, brushes=[], fluid=fluid)
    row = {"index": 0, "surfaceZCm": 0.0, "brushes": [],
          "fluid": {"index": 5, "density": 1000.0}}
    assert module._verify_water_volume(volume, row) == []


def test_verify_water_volume_flags_a_missing_fluid_flag(module) -> None:
    fluid = FakeStruct(has_fluid=False, index=0, density=0.0)
    volume = FakeStruct(index=0, surface_z_cm=0.0, brushes=[], fluid=fluid)
    row = {"index": 0, "surfaceZCm": 0.0, "brushes": [], "fluid": {"index": 5, "density": 1000.0}}
    problems = module._verify_water_volume(volume, row)
    assert any("has_fluid is false" in p for p in problems)


def test_verify_water_volume_checks_piece_and_box_counts(module) -> None:
    volume = FakeStruct(index=0, surface_z_cm=0.0, brushes=[], pieces=[FakeStruct()],
                        leaf_boxes_cm=[], near_boxes_cm=[FakeStruct(), FakeStruct()])
    row = {"index": 0, "surfaceZCm": 0.0, "brushes": [],
          "pieces": [{}], "leafBoxesCm": [{}], "nearBoxesCm": [{}, {}]}
    problems = module._verify_water_volume(volume, row)
    assert any("leafBoxesCm" in p for p in problems)
    assert not any("pieces" in p for p in problems)
    assert not any("nearBoxesCm" in p for p in problems)


# ----------------------------------------------------------------------- _verify_water_undersides

def test_verify_water_undersides_passes_when_every_twin_exists() -> None:
    module = _load(existing_assets={"/ElysiumBaked/Materials/water/MI_sewer_water_Underside"})
    manifest = {"materials": {
        "water/sewer_water#underside": {
            "underside": True,
            "undersideAsset": "/ElysiumBaked/Materials/water/MI_sewer_water_Underside"},
        "water/sewer_water": {"underside": False, "undersideAsset": None},
    }}
    assert module._verify_water_undersides(manifest, "sm_hub_1") == []


def test_verify_water_undersides_flags_a_missing_twin() -> None:
    module = _load(existing_assets=())
    manifest = {"materials": {
        "water/sewer_water#underside": {
            "underside": True,
            "undersideAsset": "/ElysiumBaked/Materials/water/MI_sewer_water_Underside"},
    }}
    errors = module._verify_water_undersides(manifest, "sm_hub_1")
    assert len(errors) == 1
    assert "MI_sewer_water_Underside" in errors[0]


def test_verify_water_undersides_ignores_a_row_with_no_twin_named(module) -> None:
    manifest = {"materials": {
        "brick/wall": {"underside": False, "undersideAsset": None},
    }}
    assert module._verify_water_undersides(manifest, "sm_hub_1") == []


# ------------------------------------------------------------------- _verify_water_lightstyle_tags

def test_verify_water_lightstyle_tags_passes_when_a_chunk_carries_the_tag(module) -> None:
    manifest = {"materials": {
        "objects/surf#style1": {"lightStyle": 1},
        "objects/surf": {"lightStyle": None},
    }}
    actors = [FakeActor(["elysium.world", "elysium.style=1"]), FakeActor(["elysium.world"])]
    assert module._verify_water_lightstyle_tags(actors, {}, manifest, "sm_pier_1") == []


def test_verify_water_lightstyle_tags_flags_an_untagged_style(module) -> None:
    manifest = {"materials": {"objects/surf#style1": {"lightStyle": 1}}}
    actors = [FakeActor(["elysium.world"])]
    errors = module._verify_water_lightstyle_tags(actors, {}, manifest, "sm_pier_1")
    assert len(errors) == 1
    assert "1" in errors[0]


def test_verify_water_lightstyle_tags_ignores_actors_outside_world_or_sky(module) -> None:
    # A tag on the wrong actor class (a prop, say) does not satisfy the check -- only
    # `elysium.world`/`elysium.sky` chunks are what the light rig's clock walks.
    manifest = {"materials": {"objects/surf#style1": {"lightStyle": 1}}}
    actors = [FakeActor(["elysium.prop", "elysium.style=1"])]
    errors = module._verify_water_lightstyle_tags(actors, {}, manifest, "sm_pier_1")
    assert len(errors) == 1


def test_verify_water_lightstyle_tags_is_a_noop_with_no_styled_rows(module) -> None:
    manifest = {"materials": {"objects/surf": {"lightStyle": None}}}
    assert module._verify_water_lightstyle_tags([], {}, manifest, "sm_pier_1") == []


def test_verify_water_lightstyle_tags_accepts_a_brush_mesh_as_the_carrier(module) -> None:
    # The `sm_pier_1` shape: every `objects/surf#style1` group is in a BRUSH scene, so no chunk
    # actor carries `elysium.style=1` and the check must read the brush mesh's slot names instead
    # (`ElysiumLightStyle::StyleFromSlotNames`, `UElysiumMapVisuals::RegisterRuntimeBrush`).
    manifest = {"materials": {"objects/surf#style1": {"lightStyle": 1}}}
    meshes = {"brush_11": FakeMesh(["/ElysiumBaked/Materials/objects/MI_surf"],
                                   ["objects_surf_style1"])}
    assert module._verify_water_lightstyle_tags([], meshes, manifest, "sm_pier_1") == []


def test_verify_water_lightstyle_tags_still_flags_a_style_no_carrier_names(module) -> None:
    manifest = {"materials": {"objects/surf#style1": {"lightStyle": 1}}}
    meshes = {"brush_11": FakeMesh(["/ElysiumBaked/Materials/objects/MI_surf"], ["objects_surf"])}
    errors = module._verify_water_lightstyle_tags([], meshes, manifest, "sm_pier_1")
    assert len(errors) == 1
    assert "brush mesh" in errors[0]


# --------------------------------------------------------------------------------- verify_water

def test_verify_water_short_circuits_off_v2_models(module, monkeypatch) -> None:
    monkeypatch.setattr(module.map_transport, "is_map_on_v2_models", lambda name: False)
    assert module.verify_water([], "some_legacy_map") == []


def test_verify_water_names_the_missing_manifest(module, monkeypatch) -> None:
    monkeypatch.setattr(module.map_transport, "is_map_on_v2_models", lambda name: True)
    monkeypatch.setattr(module, "_staged_manifest", lambda name: None)
    errors = module.verify_water([], "sm_hub_1")
    assert len(errors) == 1
    assert "no staged map_geometry manifest" in errors[0]


def test_verify_water_runs_underside_and_lightstyle_checks_even_with_no_water_volumes(
        module, monkeypatch) -> None:
    # A map can carry a styled or undersided surface with no `water.volumes[]` row at all (e.g. a
    # lightstyle on ordinary geometry) -- those two checks must not be gated on a water actor.
    monkeypatch.setattr(module.map_transport, "is_map_on_v2_models", lambda name: True)
    monkeypatch.setattr(module, "_staged_manifest", lambda name: {
        "water": {"volumes": []},
        "materials": {"objects/surf#style1": {"lightStyle": 1}},
    })
    errors = module.verify_water([], "sm_pier_1")
    assert any("lightstyle" in e for e in errors)


# ------------------------------------------------------------------ _verify_water_section_bindings

#: One water map's worth of staged rows: an up-facing surface group and its down-facing twin, the
#: shape `sm_pier_1` stages (`water/invisible_water` + `water/invisible_water#underside`).
_SURFACE = "/ElysiumBaked/Materials/water/MI_invisible_water"
_UNDERSIDE = _SURFACE + "_Underside"
_SECTION_MANIFEST = {
    "materials": {
        "water/invisible_water": {
            "asset": _SURFACE, "underside": False, "undersideAsset": None},
        "water/invisible_water#underside": {
            "asset": _SURFACE, "underside": True, "undersideAsset": _UNDERSIDE},
    },
    "water": {"faces": [
        {"group": "water/invisible_water"},
        {"group": "water/invisible_water#underside"},
    ]},
}


def test_chunk_bound_materials_reads_world_and_sky_chunks_only(module) -> None:
    actors = [
        FakeActor([module.WORLD_TAG], [_SURFACE]),
        FakeActor([module.SKY_TAG], [_UNDERSIDE]),
        # A prop actor binds materials too, and none of them is a world surface.
        FakeActor(["elysium.prop"], ["/ElysiumBaked/Materials/props/MI_crate"]),
    ]
    assert module.chunk_bound_materials(actors) == {_SURFACE, _UNDERSIDE}


def test_chunk_bound_materials_also_reads_brush_entity_meshes(module) -> None:
    # `read_geometry` stages a brush model through the same `_build_scene` the world goes through,
    # so a `water.faces[]` row can name a brush scene -- and then the group's instance is bound on
    # `/<map>/Brushes/SM_brush_<n>` and on no chunk actor at all.
    meshes = {"brush_6": FakeMesh([_SURFACE])}
    assert module.chunk_bound_materials([], meshes) == {_SURFACE}


def test_verify_water_section_bindings_accepts_a_brush_bound_group(module) -> None:
    manifest = {
        "materials": {"water/blackwater": {"asset": _SURFACE, "underside": False,
                                           "undersideAsset": None}},
        "water": {"faces": [{"group": "water/blackwater", "scene": "brush_6"}]},
    }
    meshes = {"brush_6": FakeMesh([_SURFACE])}
    assert module._verify_water_section_bindings([], meshes, manifest, "sm_pier_1") == []


def test_verify_water_section_bindings_passes_when_both_halves_are_bound(module) -> None:
    actors = [FakeActor([module.WORLD_TAG], [_SURFACE, _UNDERSIDE])]
    assert module._verify_water_section_bindings(actors, {}, _SECTION_MANIFEST, "sm_pier_1") == []


def test_verify_water_section_bindings_flags_an_underside_group_bound_to_the_surface(
        module) -> None:
    # The exact defect an existence check cannot see: the twin is imported, but the down-facing
    # section fell back to the surface instance -- so the water reflects the sky from below.
    actors = [FakeActor([module.WORLD_TAG], [_SURFACE])]
    errors = module._verify_water_section_bindings(actors, {}, _SECTION_MANIFEST, "sm_pier_1")
    assert len(errors) == 1
    assert "water/invisible_water#underside" in errors[0]
    assert _UNDERSIDE in errors[0]


def test_verify_water_section_bindings_flags_a_group_with_no_materials_row(module) -> None:
    manifest = {"materials": {}, "water": {"faces": [{"group": "water/invisible_water"}]}}
    errors = module._verify_water_section_bindings([], {}, manifest, "sm_pier_1")
    assert len(errors) == 1
    assert "no materials row" in errors[0]


def test_verify_water_section_bindings_is_a_noop_on_a_map_with_no_water_faces(module) -> None:
    assert module._verify_water_section_bindings([], {}, {"materials": {}, "water": {}},
                                                 "sp_tutorial_1") == []
