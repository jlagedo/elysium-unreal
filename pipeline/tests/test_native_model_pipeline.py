"""Dependency order and failed native receipts at the public profile/map boundary."""
import json
from types import SimpleNamespace
from unittest import mock

import pytest

from elysium_pipeline import character_pipeline, export_manager, native_model_pipeline as flow, unreal
from elysium_pipeline.importers import expression_tables, model_catalogues


def _config(tmp_path):
    config = SimpleNamespace(work_root=tmp_path / "work", export_v2_root=tmp_path / "glb")
    for family in ("models", "vdata/items", "expression-tables"):
        path = config.export_v2_root / family / "fixture.glb"
        path.parent.mkdir(parents=True)
        path.write_bytes(b"publication")
    for family in ("materials", "models"):
        path = config.work_root / "import" / family / "manifest.json"
        path.parent.mkdir(parents=True)
        path.write_text("{}", encoding="utf-8")
    return config


def test_one_import_orders_expressions_cast_and_global_catalogues(tmp_path):
    config = _config(tmp_path)
    order = []
    manifest = {"selectedUnits": ["vtmb:model:a"]}
    with (
        mock.patch.object(flow, "import_expression_tables", side_effect=lambda *a, **k: order.append("expressions")),
        mock.patch.object(character_pipeline, "import_characters", side_effect=lambda *a, **k: order.append("characters") or manifest) as chars,
        mock.patch.object(flow, "import_model_catalogues", side_effect=lambda *a, **k: order.append("catalogues")) as cats,
    ):
        assert flow.import_map_dependencies(config, None, bodies=["vtmb:model:a"], force=True) is manifest
    assert order == ["expressions", "characters", "catalogues"]
    chars.assert_called_once_with(config, None, bodies=["vtmb:model:a"], force=True, log=print)
    cats.assert_called_once_with(config, None, force=True, log=print)


@pytest.mark.parametrize("step", ["expressions", "characters"])
def test_failed_dependency_stops_before_catalogue_publication(tmp_path, step):
    config = _config(tmp_path)
    with (
        mock.patch.object(flow, "import_expression_tables", side_effect=RuntimeError("failed") if step == "expressions" else None),
        mock.patch.object(character_pipeline, "import_characters", side_effect=RuntimeError("failed")) as chars,
        mock.patch.object(flow, "import_model_catalogues") as cats,
    ):
        with pytest.raises(RuntimeError, match="failed"):
            flow.import_map_dependencies(config, None)
    cats.assert_not_called()
    assert chars.call_count == (0 if step == "expressions" else 1)


def test_fresh_profile_has_clear_prerequisites_before_any_offline_work(tmp_path):
    config = SimpleNamespace(work_root=tmp_path, export_v2_root=tmp_path / "absent")
    with mock.patch.object(export_manager, "run_offline_profile") as offline:
        with pytest.raises(RuntimeError, match="import models --all"):
            export_manager.export_profile(config, None, "all")
    offline.assert_not_called()
    assert not config.export_v2_root.exists()


def test_clean_profile_keeps_the_v2_corpus_and_kind_roots_it_reads(tmp_path):
    # `reconstruct` always cleans; a clean must regenerate the profile's outputs without
    # erasing the GLB corpus or the static native lanes the R8 import chain consumes.
    config = _config(tmp_path)
    config.repo_root = tmp_path / "repo"
    config.game_root = tmp_path / "game"
    config.export_root = config.work_root / "exports"
    config.log_root = tmp_path / "logs"
    for path in (config.repo_root, config.game_root, config.export_root, config.log_root):
        path.mkdir(parents=True, exist_ok=True)
    # The standard `work/exports_v2` root is adopted by name; a clean refuses any other.
    config.export_v2_root.rename(config.work_root / "exports_v2")
    config.export_v2_root = config.work_root / "exports_v2"
    baked = config.repo_root / "Plugins" / "ElysiumBaked" / "Content"
    for folder in ("Models/character", "Materials", "Shared", "sp_tutorial_1"):
        (baked / folder).mkdir(parents=True)
        (baked / folder / "asset.uasset").write_text("x")
    (config.export_root / "sp_tutorial_1").mkdir()
    (config.export_root / "sp_tutorial_1" / "map.obj").write_text("loose")

    with (
        mock.patch.object(export_manager, "run_offline_profile", return_value=([], {})),
        mock.patch.object(export_manager, "_bake_profile_maps"),
        mock.patch.object(export_manager, "ensure_policy_content"),
        mock.patch.object(export_manager, "ensure_corpus_bake"),
        mock.patch.object(flow, "import_map_dependencies") as deps,
        mock.patch.object(export_manager.ContentDigestCache, "write"),
    ):
        export_manager.export_profile(config, None, "all", clean=True)
    # The clean itself lives in run_offline_profile (mocked above): exercise it directly.
    with (
        mock.patch.object(export_manager, "ensure_corpus_export"),
        mock.patch.object(export_manager, "_decoder_closures", side_effect=RuntimeError("stop")),
        mock.patch("elysium_pipeline.formats.install.build_index", return_value={}),
        mock.patch("elysium_pipeline.exporters.export_all.maps_for_profile", return_value=[]),
        mock.patch("elysium_pipeline.exporters.export_all.bundles_for_profile", return_value=[]),
    ):
        with pytest.raises(RuntimeError, match="stop"):
            export_manager.run_offline_profile(config, "all", clean=True, force=False, jobs=1)
    assert "force" not in deps.call_args.kwargs  # a clean never forces another lane's re-authoring
    assert (config.export_v2_root / "models/fixture.glb").read_bytes() == b"publication"
    assert (config.work_root / "import" / "models" / "manifest.json").is_file()
    assert (baked / "Models/character/asset.uasset").is_file()
    assert (baked / "Materials/asset.uasset").is_file()
    assert not (baked / "Shared").exists()
    assert not (baked / "sp_tutorial_1").exists()
    assert not (config.export_root / "sp_tutorial_1").exists()


@pytest.mark.parametrize("lane", ["expressions", "catalogues"])
@pytest.mark.parametrize("report", [None, {"complete": False}, {"complete": True, "failed": ["missing product"]}])
def test_absent_or_failed_receipt_never_reuses_previous_success(tmp_path, lane, report):
    config = _config(tmp_path)
    module = expression_tables if lane == "expressions" else model_catalogues
    stage = "stage_expression_tables" if lane == "expressions" else "stage_model_catalogues"
    launcher = "expression_tables" if lane == "expressions" else "model_catalogues"
    root = module.staging_root(config.work_root)
    root.mkdir(parents=True)
    receipt = root / ("import_report.json" if lane == "expressions" else "model_catalogues_import_report.json")
    receipt.write_text(json.dumps({"complete": True, "imported": 1, "reused": 0}), encoding="utf-8")

    def launch(*args, **kwargs):
        assert not receipt.exists()
        if report is not None:
            receipt.write_text(json.dumps(report), encoding="utf-8")

    with (
        mock.patch.object(module, stage, return_value={"complete": True, "stageFailures": [], "keep": ["/asset"]}),
        mock.patch.object(unreal, launcher, side_effect=launch),
    ):
        importer = flow.import_expression_tables if lane == "expressions" else flow.import_model_catalogues
        with pytest.raises(RuntimeError, match="import failed"):
            importer(config, None)


def test_incomplete_expression_product_count_is_refused(tmp_path):
    config = _config(tmp_path)
    root = expression_tables.staging_root(config.work_root)
    root.mkdir(parents=True)
    def launch(*a, **k):
        (root / "import_report.json").write_text(json.dumps({"complete": True, "imported": 1, "reused": 0}))
    with (
        mock.patch.object(expression_tables, "stage_expression_tables", return_value={
            "complete": True, "stageFailures": [], "keep": ["/a", "/b"]}),
        mock.patch.object(unreal, "expression_tables", side_effect=launch),
    ):
        with pytest.raises(RuntimeError, match="every product"):
            flow.import_expression_tables(config, None)
