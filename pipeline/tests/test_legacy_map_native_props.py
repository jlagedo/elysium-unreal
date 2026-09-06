"""R9 static map transport embeds R8 native skeletal references without old index reads."""
import ast
import os
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import pytest

from elysium_pipeline import shared_corpus
from elysium_pipeline.asset_paths import baked_unit, corpus_path
from pipeline.unreal import model_catalogue_views as views


ID = "vtmb:model:scenery/switch"
MODEL = "models/scenery/switch.mdl"


def _row(**fields):
    return SimpleNamespace(get_editor_property=lambda name: fields[name])


def _fixture(tmp_path, *, skeletal=True, full_identity=True):
    # Extract only the method: importing the worker would execute its editor entrypoint.
    path = Path(__file__).resolve().parents[1] / "unreal/bake_map.py"
    tree = ast.parse(path.read_text(encoding="utf-8"))
    method = next(n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == "_place_props")
    component = mock.Mock()
    component.get_material_slot_names.return_value = ["face"]
    actor = mock.Mock(skeletal_visual=component, static_mesh_component=component)
    actor.configure_rest.return_value = True
    actors = mock.Mock()
    actors.spawn_actor_from_class.return_value = actor
    clip = _row(label="idle", weight=1, sequence="", base_cell=baked_unit(ID, "A", "idle_cell"))
    native = _row(asset_id=ID, model_path=MODEL, acceptance_issues=[], clips=[clip],
                  rest_candidates=[0], source_absent=False, static_rest_suffices=not skeletal,
                  static_source_representation=not skeletal, static_equivalent_proven=not skeletal,
                  static_equivalent=not skeletal, static_topology_equivalent=True,
                  has_cloth=False, skeletal_mesh=baked_unit(ID, "SK"), static_mesh=baked_unit(ID, "SM"))
    skin = _row(asset_id=ID, family_count=2, representations=[_row(kind="skeletal",
        slots=[_row(index=0, slot_name="face", skin_references=[2])],
        families=[_row(index=i, cells=[_row(skin_reference=2, material="/native/skin" + str(i))])
                  for i in range(2)])])
    static_path = shared_corpus.BAKED_MESHES + "/" + shared_corpus.mesh_asset("switch")
    assets = {corpus_path("model", "DA", "PlacedModels"): _row(data=_row(models={ID: native})),
              corpus_path("model", "DA", "PropSkins"): _row(data=_row(models={ID: skin})),
              baked_unit(ID, "SK"): object(), clip.get_editor_property("base_cell"): object(),
              static_path: object(), "/native/skin1": object()}
    engine = SimpleNamespace(EditorAssetLibrary=SimpleNamespace(load_asset=mock.Mock(side_effect=assets.get)),
        Vector=lambda *v: v, Quat=lambda *v: SimpleNamespace(rotator=lambda: v),
        ElysiumPlacedModelActor="native_actor", StaticMeshActor="static_actor")
    namespace = {"os": os, "unreal": engine, "SC": shared_corpus, "log": lambda *a: None,
        "PROFILE_PROP_SOLID": "solid", "PROFILE_PICK_ONLY": "pick", "TAG_PROP": "prop", "TAG_SKY": "sky"}
    exec(compile(ast.Module(body=[method], type_ignores=[]), str(path), "exec"), namespace)
    host = SimpleNamespace(dir=str(tmp_path), map="probe", shared_mesh_pkg=shared_corpus.BAKED_MESHES,
        corpus_manifest={"models": {"switch": {"model": MODEL}}}, _apply_prop_skin=mock.Mock(return_value=1))
    # Keep an ignored line: its ordinal contributes to Source's deterministic rest choice.
    (tmp_path / "probe.props").write_text("# comment\nswitch 1 2 3 0 0 0 1 1 1 0" +
        (" " + MODEL if full_identity else "") + "\n", encoding="utf-8")
    return namespace["_place_props"], host, actors, actor, component, engine, assets, static_path


@pytest.mark.parametrize("full_identity", [True, False])
def test_legacy_map_uses_native_rest_and_skin_with_shared_collision(tmp_path, full_identity):
    run, host, actors, actor, component, engine, assets, static = _fixture(tmp_path, full_identity=full_identity)
    with mock.patch.object(views, "select_rest", wraps=views.select_rest) as select:
        assert run(host, actors) == (1, 0)
    assert select.call_args.args[1] == 1
    actor.configure_rest.assert_called_once_with(assets[baked_unit(ID, "SK")],
        assets[baked_unit(ID, "A", "idle_cell")], assets[static], True)
    component.set_material.assert_called_once_with(0, assets["/native/skin1"])
    host._apply_prop_skin.assert_not_called()
    paths = [call.args[0] for call in engine.EditorAssetLibrary.load_asset.call_args_list]
    assert not any(path.startswith(("/ElysiumBaked/Props/", "/ElysiumBaked/Characters/")) for path in paths)


def test_static_transport_and_its_skin_remain_available_for_r9(tmp_path):
    run, host, actors, actor, component, engine, assets, static = _fixture(tmp_path, skeletal=False)
    assert run(host, actors) == (1, 0)
    actor.configure_rest.assert_not_called()
    component.set_static_mesh.assert_called_once_with(assets[static])
    host._apply_prop_skin.assert_called_once_with(component, assets[static], "switch", 1)


@pytest.mark.parametrize("missing,match", [
    (corpus_path("model", "DA", "PlacedModels"), "catalogue is absent"),
    (baked_unit(ID, "SK"), "native rest asset"),
    (baked_unit(ID, "A", "idle_cell"), "native rest asset"),
    ("/native/skin1", "native skin material absent"),
])
def test_missing_native_dependencies_cannot_fall_back_to_old_packages(tmp_path, missing, match):
    run, host, actors, _, _, _, assets, _ = _fixture(tmp_path)
    del assets[missing]
    with pytest.raises((ValueError, RuntimeError), match=match):
        run(host, actors)


def test_wrong_native_skin_slot_is_refused(tmp_path):
    run, host, actors, _, component, _, _, _ = _fixture(tmp_path)
    component.get_material_slot_names.return_value = ["wrong"]
    with pytest.raises(RuntimeError, match="native skin slot differs"):
        run(host, actors)


def test_old_row_without_full_source_identity_is_refused(tmp_path):
    run, host, actors, _, _, _, _, _ = _fixture(tmp_path, full_identity=False)
    host.corpus_manifest = {}
    with pytest.raises(ValueError, match="full source model path"):
        run(host, actors)
