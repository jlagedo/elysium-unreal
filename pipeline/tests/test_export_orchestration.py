from __future__ import annotations

import unittest
from unittest import mock

import pytest

from elysium_pipeline import export_manager, wield_corpus
from elysium_pipeline.exporters import export_all

class ExportProfileTests(unittest.TestCase):
    def test_texture_glb_corpus_admits_each_tth_identity_once(self) -> None:
        index = {
            "materials/a/brick.tth": object(),
            "materials/a/brick.ttz": object(),
            "materials/b/glass.tth": object(),
            "materials/b/glass.vmt": object(),
            "models/not-a-texture.tth": object(),
        }
        assert export_manager._texture_glb_sources(index) == ["a/brick", "b/glass"]

    def test_all_texture_glbs_reuses_one_patch_first_index(self) -> None:
        from pathlib import Path
        from types import SimpleNamespace

        from elysium_pipeline.exporters import texture_glb
        from elysium_pipeline.formats import install
        from elysium_pipeline.validation import texture_glb as validation

        index = {
            "materials/a/brick.tth": object(),
            "materials/a/brick.ttz": object(),
            "materials/b/glass.tth": object(),
        }
        config = SimpleNamespace(
            game_root=Path("C:/game"),
            work_root=Path("C:/work"),
            export_root=Path("C:/export"),
            export_v2_root=Path("C:/export_v2"),
        )

        def write(_index, texture, output_root):
            assert _index is index
            return output_root / (texture.replace("/", "_") + ".glb")

        summary = {
            "asset": "vtmb:texture:test",
            "accountedBytes": 10,
            "sourceBytes": 10,
        }
        with (
            mock.patch.object(install, "build_index", return_value=index) as build_index,
            mock.patch.object(texture_glb, "export", side_effect=write) as export,
            mock.patch.object(validation, "validate", return_value=summary) as validate,
        ):
            destinations = export_manager.export_all_texture_glbs(
                config, object(), jobs=1
            )

        assert len(destinations) == 2
        build_index.assert_called_once_with()
        assert export.call_count == 2
        assert validate.call_count == 2

    def test_material_glb_corpus_admits_each_addressable_vmt_once(self) -> None:
        index = {
            "materials/a/brick.vmt": object(),
            "materials/a/brick.tth": object(),
            "materials/b/glass.vmt": object(),
            # The engine composes `materials/<search path><name>.vmt`, so a VMT packed outside
            # `materials/` names no material and is not a unit.
            "models/character/monster/hengeyokai/hengeyokai_frozen.vmt": object(),
        }
        assert export_manager._material_glb_sources(index) == ["a/brick", "b/glass"]

    def test_all_material_glbs_reuses_one_patch_first_index(self) -> None:
        from pathlib import Path
        from types import SimpleNamespace

        from elysium_pipeline.exporters import material_glb
        from elysium_pipeline.formats import install
        from elysium_pipeline.validation import material_glb as validation

        index = {
            "materials/a/brick.vmt": object(),
            "materials/b/glass.vmt": object(),
        }
        config = SimpleNamespace(
            game_root=Path("C:/game"),
            work_root=Path("C:/work"),
            export_root=Path("C:/export"),
            export_v2_root=Path("C:/export_v2"),
        )

        def write(_index, material, output_root):
            assert _index is index
            return output_root / (material.replace("/", "_") + ".glb")

        summary = {
            "asset": "vtmb:material:test",
            "accountedBytes": 10,
            "sourceBytes": 10,
            "anomalies": [],
            "missingTextures": [],
        }
        with (
            mock.patch.object(install, "build_index", return_value=index) as build_index,
            mock.patch.object(material_glb, "export", side_effect=write) as export,
            mock.patch.object(validation, "validate", return_value=summary) as validate,
        ):
            destinations = export_manager.export_all_material_glbs(
                config, object(), jobs=1
            )

        assert len(destinations) == 2
        build_index.assert_called_once_with()
        assert export.call_count == 2
        assert validate.call_count == 2

    def test_character_glb_corpus_admits_only_models_with_topology(self) -> None:
        index = {
            "models/character/a/body.mdl": object(),
            "models/character/a/body.dx80.vtx": object(),
            "models/character/b/body.mdl": object(),
            "models/character/b/body.dx7_2bone.vtx": object(),
            "models/character/shared/bank.mdl": object(),
            "models/scenery/prop.mdl": object(),
            "models/scenery/prop.dx80.vtx": object(),
        }
        assert export_manager._character_glb_models(index) == [
                "models/character/a/body.mdl",
                "models/character/b/body.mdl",
            ]

    def test_all_character_glbs_reuses_one_index_and_anorm_table(self) -> None:
        from pathlib import Path
        from types import SimpleNamespace

        from elysium_pipeline.exporters import character_glb
        from elysium_pipeline.formats import install, mdl_skel
        from elysium_pipeline.validation import character_glb as validation

        index = {
            "models/character/a/body.mdl": object(),
            "models/character/a/body.dx80.vtx": object(),
            "models/character/b/body.mdl": object(),
            "models/character/b/body.dx7_2bone.vtx": object(),
        }
        config = SimpleNamespace(
            game_root=Path("C:/game"),
            work_root=Path("C:/work"),
            export_root=Path("C:/export"),
            export_v2_root=Path("C:/export_v2"),
        )

        def write(_index, model, output_root, *, anorms):
            assert _index is index
            assert anorms is vectors
            return output_root / (model.rsplit("/", 1)[-1][:-4] + ".glb")

        vectors = [(1.0, 0.0, 0.0)]
        summary = {
            "asset": "vtmb:character-body:test",
            "accountedBytes": 10,
            "sourceBytes": 10,
        }
        with (
            mock.patch.object(install, "build_index", return_value=index) as build_index,
            mock.patch.object(mdl_skel, "load_anorms", return_value=vectors) as load_anorms,
            mock.patch.object(character_glb, "export", side_effect=write) as export,
            mock.patch.object(validation, "validate", return_value=summary) as validate,
        ):
            destinations = export_manager.export_all_character_glbs(
                config, object(), jobs=1
            )

        assert len(destinations) == 2
        build_index.assert_called_once_with()
        load_anorms.assert_called_once_with()
        assert export.call_count == 2
        assert validate.call_count == 2

    def test_export_all_runs_every_seam_and_reports_a_failing_one(self) -> None:
        # A seam is an independent corpus costing its own hours; one failure must not cancel the
        # seams still to run, and must not be swallowed either.
        from pathlib import Path
        from types import SimpleNamespace

        config = SimpleNamespace(
            game_root=Path("C:/game"),
            work_root=Path("C:/work"),
            export_root=Path("C:/work/exports"),
            export_v2_root=Path("C:/work/exports_v2"),
        )
        ran = []

        def corpus(name, result):
            def run(_config, _runner, *, jobs=None):
                ran.append(name)
                if isinstance(result, Exception):
                    raise result
                return result

            return run

        seams = (
            ("texture", corpus("texture", [Path("a.glb")])),
            ("material", corpus(
                "material", export_manager.OfflineExportFailure("2 of 9 failed"))),
            ("character", corpus("character", [Path("b.glb"), Path("c.glb")])),
        )
        with mock.patch.object(export_manager, "GLB_SEAMS", seams):
            with pytest.raises(export_manager.OfflineExportFailure) as raised:
                export_manager.export_all_glb_seams(config, object())

        assert ran == ["texture", "material", "character"]
        assert "material" in str(raised.value)
        assert "2 of 9 failed" in str(raised.value)

    def test_export_all_returns_every_seam_that_published(self) -> None:
        from pathlib import Path
        from types import SimpleNamespace

        config = SimpleNamespace(
            game_root=Path("C:/game"),
            work_root=Path("C:/work"),
            export_root=Path("C:/work/exports"),
            export_v2_root=Path("C:/work/exports_v2"),
        )

        def corpus(destinations):
            return lambda _config, _runner, *, jobs=None: destinations

        seams = (
            ("texture", corpus([Path("a.glb")])),
            ("material", corpus([Path("b.glb"), Path("c.glb")])),
            ("character", corpus([])),
        )
        with mock.patch.object(export_manager, "GLB_SEAMS", seams):
            published = export_manager.export_all_glb_seams(config, object())

        assert {seam: len(paths) for seam, paths in published.items()} == {"texture": 1, "material": 2, "character": 0}

    def test_glb_seams_publish_under_the_export_v2_root(self) -> None:
        # The isolated seams feed the new bake pipeline, so they must never land inside the
        # bake corpus `elysium clean` and the manifest own.
        from pathlib import Path
        from types import SimpleNamespace

        config = SimpleNamespace(
            game_root=Path("C:/game"),
            work_root=Path("C:/work"),
            export_root=Path("C:/work/exports"),
            export_v2_root=Path("C:/work/exports_v2"),
        )
        for seam in ("characters", "textures", "materials", "surface-properties"):
            root = export_manager._export_v2_root(config, seam)
            assert root == Path("C:/work/exports_v2") / seam
            assert config.export_root not in root.parents

    def test_glb_seams_reject_a_config_without_an_export_v2_root(self) -> None:
        from pathlib import Path
        from types import SimpleNamespace

        config = SimpleNamespace(
            game_root=Path("C:/game"),
            work_root=Path("C:/work"),
            export_root=Path("C:/work/exports"),
            export_v2_root=None,
        )
        with pytest.raises(ValueError):
            export_manager._require_export_v2_config(config)

    def test_glb_corpus_workers_are_spawn_importable(self) -> None:
        import pickle

        from elysium_pipeline import workers

        for worker in (workers.character_glb_worker, workers.texture_glb_worker,
                       workers.material_glb_worker):
            restored = pickle.loads(pickle.dumps(worker))
            assert restored.__name__ == worker.__name__

    def test_only_ents_consuming_bundles_wait_on_the_maps(self) -> None:
        # `audio` and `npc` read the exported per-map `.ents`; every other bundle reads the
        # install (or the pre-graph shared corpus) and starts immediately. `npc` keeps its one
        # inter-bundle edge on the exported vdata mirror.
        from pathlib import Path
        from types import SimpleNamespace

        bundles = [
            "audio", "particles", "scripts", "signs", "vdata", "items",
            "cfg", "scenes", "ui", "use-icons", "npc",
        ]
        config = SimpleNamespace(export_root=Path("/fake/export/root"))
        tasks = export_manager._bundle_tasks(
            config, bundles, ["m1", "m2"], {}, {bundle: "fp" for bundle in bundles})
        by_name = {task.name: task for task in tasks}
        map_edges = ("map:m1", "map:m2")
        assert by_name["bundle:audio"].dependencies == map_edges
        assert by_name["bundle:npc"].dependencies == (*map_edges, "bundle:vdata")
        for bundle in bundles:
            if bundle in ("audio", "npc"):
                continue
            with self.subTest(bundle=bundle):
                assert by_name[f"bundle:{bundle}"].dependencies == ()

    def test_npc_bundle_drops_the_vdata_edge_when_vdata_is_not_requested(self) -> None:
        from pathlib import Path
        from types import SimpleNamespace

        config = SimpleNamespace(export_root=Path("/fake/export/root"))
        tasks = export_manager._bundle_tasks(
            config, ["npc"], ["m1"], {}, {"npc": "fp"})
        assert tasks[0].dependencies == ("map:m1",)

    def test_items_bundle_outputs_include_both_manifests(self) -> None:
        from pathlib import Path

        export_root = Path("/fake/export/root")
        outputs = export_manager._bundle_outputs(export_root, "items")
        assert export_root / "items" / "ground_models.json" in outputs
        assert wield_corpus.manifest_path(export_root) in outputs

    def test_structured_failure_is_strict(self) -> None:
        result = export_all.ExportBatchResult(
            maps=[
                export_all.ExportTaskResult(
                    name="missing_map",
                    status="failed",
                    error="not installed",
                )
            ]
        )
        with pytest.raises(export_all.ExportFailed) as caught:
            result.require_success()
        assert caught.value.result is result
