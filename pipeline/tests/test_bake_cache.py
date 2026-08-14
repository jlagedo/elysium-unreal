from __future__ import annotations

import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest

from elysium_pipeline import bake_cache, shared_corpus
from elysium_pipeline.tasking import Manifest


BAKE_MAP_SOURCE = """
VALUE = 1

def helper():
    return VALUE

class Bake:
    def __init__(self): pass
    def load_masters(self): return True
    def load_sources(self): return True
    def flush(self): return False
    def _texture_jobs(self): return []
    def stage_textures(self): self._texture_jobs()
    def resolve_textures(self): self._texture_jobs()
    def stage_materials(self): self.resolve_textures()
    def resolve_materials(self): pass
    def stage_world(self): self.resolve_materials()
    def stage_sky(self): self.resolve_materials()
    def stage_props(self): self.resolve_materials()
    def stage_level(self): self.resolve_materials()

def bake_one():
    return helper()
"""

DRIVER_SOURCE = """
def _run(): pass
def editor_executable(): pass
def bake_maps(): pass
def unrelated_runtime_driver(): pass
"""


class BakeCacheTests(unittest.TestCase):
    #: The corpus rows the fixture map reaches: one material its `.mtl` binds, one reached only
    #: through the slot table of the stem it places, and one entry no map draws.
    MAP_MATERIAL = "world/wall"
    PROP_MATERIAL = "models/scenery/prop"
    UNDRAWN_MATERIAL = "world/unused"

    #: The catalogue key the fixture map's one `.props` row joins to, and one the map never places.
    PLACED_STEM = "scenery_prop"
    UNPLACED_STEM = "scenery_other"

    def _corpus_rows(self) -> tuple[dict, dict]:
        materials = {
            self.MAP_MATERIAL: {
                "albedo": "tex/wall.png",
                "env_mask": "tex/wall_envmask.png",
                "scissor": False,
            },
            self.PROP_MATERIAL: {"albedo": "tex/prop.png", "scissor": False},
            self.UNDRAWN_MATERIAL: {
                "albedo": "tex/unused.png",
                "env_mask": "tex/unused_envmask.png",
                "scissor": False,
            },
        }
        models = {
            "prop": {
                "model": "models/scenery/prop.mdl",
                "materials": {"prop": self.PROP_MATERIAL},
                "physics": False,
            },
            "unplaced": {
                "model": "models/scenery/unplaced.mdl",
                "materials": {"unplaced": self.UNDRAWN_MATERIAL},
                "physics": False,
            },
        }
        return materials, models

    def _write_corpus(self, export: Path, materials: dict, models: dict) -> None:
        corpus = export / "shared"
        corpus.mkdir(parents=True, exist_ok=True)
        (corpus / "manifest.json").write_text(
            json.dumps(shared_corpus.build_manifest(
                textures={},
                materials={key: {"map_scoped": False} for key in materials},
                models=models,
            )),
            encoding="utf-8",
        )
        (corpus / "materials.json").write_text(
            json.dumps(shared_corpus.build_materials(materials)), encoding="utf-8")
        # The parsed documents are memoized on the file's stat identity, which a rewrite inside
        # one test can repeat.
        bake_cache._CORPUS_DOCUMENT_CACHE.clear()

    def _placed_index_rows(self) -> dict:
        return {
            self.PLACED_STEM: {"model": "models/scenery/prop.mdl", "static_equivalent": True},
            self.UNPLACED_STEM: {"model": "models/scenery/other.mdl", "static_equivalent": True},
        }

    def _write_placed_index(self, export: Path, rows: dict) -> None:
        npc = export / "npc"
        npc.mkdir(parents=True, exist_ok=True)
        (npc / "npc_index.json").write_text(
            json.dumps({"manifest_version": 7, "placed_models": rows}), encoding="utf-8")
        bake_cache._CORPUS_DOCUMENT_CACHE.clear()

    def _workspace(self, temporary: str):
        repo = Path(temporary) / "repo"
        export = Path(temporary) / "exports"
        unreal_root = repo / "pipeline" / "unreal"
        package_root = repo / "pipeline" / "src" / "elysium_pipeline"
        unreal_root.mkdir(parents=True)
        package_root.mkdir(parents=True)
        (unreal_root / "bake_map.py").write_text(BAKE_MAP_SOURCE, encoding="utf-8")
        (unreal_root / "bake_lib.py").write_text("VALUE = 1\n", encoding="utf-8")
        for generator in (
            "make_particle_systems.py",
            "make_world_materials.py",
            "make_decal_material.py",
            "mat_fog.py",
        ):
            (unreal_root / generator).write_text("VALUE = 1\n", encoding="utf-8")
        (package_root / "unreal.py").write_text(DRIVER_SOURCE, encoding="utf-8")
        (package_root / "bake_cache.py").write_text(
            "def _canonical(value): return value\n"
            "def asset_recipe_fingerprint(): pass\n"
            "def level_sidecar_recipe(): pass\n",
            encoding="utf-8",
        )

        map_name = "test_map"
        map_root = export / map_name
        (map_root / "brushes").mkdir(parents=True)
        (map_root / "tex" / "cube").mkdir(parents=True)
        corpus = export / "shared"
        (corpus / "tex").mkdir(parents=True)
        (corpus / "props").mkdir(parents=True)
        self._write_corpus(export, *self._corpus_rows())
        self._write_placed_index(export, self._placed_index_rows())
        (corpus / "tex" / "wall.png").write_bytes(b"texture")
        (corpus / "tex" / "wall_envmask.png").write_bytes(b"mask")
        (corpus / "tex" / "unused_envmask.png").write_bytes(b"unused mask")
        (corpus / "props" / "prop.obj").write_text("prop", encoding="utf-8")
        for suffix, contents in {
            ".obj": "world",
            ".blend": "0\n",
            ".mtl": f"newmtl wall\nmat {self.MAP_MATERIAL}\n",
            "_sky.obj": "sky",
            ".props": "prop 0 0 0 0 0 0 1 1 0 0 models/scenery/prop.mdl\n",
            ".decals": "decal",
            ".lights": "light",
            ".env": "fog 0",
            ".sky": "scale 16",
            ".spawn": "origin 0 0 0",
            ".particles.json": json.dumps({"particles": {"roots": [], "sprites": []}}),
            ".ents": "runtime only",
        }.items():
            (map_root / f"{map_name}{suffix}").write_text(contents, encoding="utf-8")
        (map_root / "brushes" / "brush_1.obj").write_text("brush", encoding="utf-8")
        (map_root / "tex" / "cube" / "cubemapdefault.dds").write_bytes(b"cube")

        baked = repo / "Plugins" / "ElysiumBaked" / "Content" / map_name
        outputs = {
            "Textures/T_wall.uasset": b"texture",
            "Materials/MI_wall.uasset": b"material",
            "Meshes/SM_World_0_0_0.uasset": b"world",
            "Meshes/SM_Sky_0_0_0.uasset": b"sky",
            "Particles/NS_test.uasset": b"particles",
            f"{map_name}.umap": b"level",
        }
        for relative, contents in outputs.items():
            destination = baked / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(contents)

        config = SimpleNamespace(repo_root=repo, export_root=export)
        manifest = Manifest(export / ".elysium-manifest.json")
        full_plan = {map_name: bake_cache.STAGES}
        policies = {
            stage: bake_cache.stage_code_fingerprint(config, stage)
            for stage in bake_cache.STAGES
        }
        bake_cache.AssetReceiptStore(export, map_name).replace_stages({
            stage: {"policy": policies[stage], "assets": {}}
            for stage in bake_cache.STAGES
        })
        bake_cache.record_stages(manifest, config, full_plan)
        return config, manifest, map_name, map_root, baked

    def test_unchanged_map_has_no_stale_stage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, _, _ = self._workspace(temporary)
            self.assertEqual(bake_cache.plan_stages(manifest, config, [name]), {})

    def test_missing_asset_schema_receipt_forces_conservative_migration(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, _, _ = self._workspace(temporary)
            bake_cache.AssetReceiptStore(config.export_root, name).path.unlink()
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]),
                {name: bake_cache.STAGES},
            )

    def test_runtime_only_sidecar_does_not_invalidate_bake(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, root, _ = self._workspace(temporary)
            (root / f"{name}.hulls").write_text("changed runtime data", encoding="utf-8")
            self.assertEqual(bake_cache.plan_stages(manifest, config, [name]), {})

    def test_entity_placement_reaches_the_level_stage(self) -> None:
        # `.ents` stopped being runtime-only when prop meshes moved to the corpus: the level
        # stage reads each entity's `model_mesh` stem to join it to the shared mesh.
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, root, _ = self._workspace(temporary)
            (root / f"{name}.ents").write_text("changed placement", encoding="utf-8")
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]), {name: ("level",)})

    def test_texture_pixel_change_only_invalidates_texture_stage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, root, _ = self._workspace(temporary)
            (root / "tex" / "cube" / "cubemapdefault.dds").write_bytes(b"changed cube")
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]),
                {name: ("textures",)},
            )

    def test_world_geometry_change_invalidates_world_and_level(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, root, _ = self._workspace(temporary)
            (root / f"{name}.obj").write_text("changed world", encoding="utf-8")
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]),
                {name: ("world", "level")},
            )

    def test_placement_change_only_invalidates_level(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, root, _ = self._workspace(temporary)
            (root / f"{name}.spawn").write_text("origin 1 0 0", encoding="utf-8")
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]),
                {name: ("level",)},
            )

    def test_particle_change_only_invalidates_particle_stage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, root, _ = self._workspace(temporary)
            (root / f"{name}.particles.json").write_text(
                json.dumps({"particles": {"roots": ["spark"], "sprites": []}}),
                encoding="utf-8",
            )
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]),
                {name: ("particles",)},
            )

    def test_a_corpus_entry_the_map_never_draws_leaves_every_stage_current(self) -> None:
        # The corpus documents describe the whole install. A map depends on the slice it draws,
        # so re-decoding somebody else's material is not this map's business.
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, _, _ = self._workspace(temporary)
            materials, models = self._corpus_rows()
            materials[self.UNDRAWN_MATERIAL] = {"albedo": "tex/unused.png", "scissor": True}
            self._write_corpus(config.export_root, materials, models)
            self.assertEqual(bake_cache.plan_stages(manifest, config, [name]), {})

    def test_a_material_the_mtl_names_reaches_every_stage_that_binds_it(self) -> None:
        # One definition, so one change: the map's materials, geometry and level all resolve
        # against `shared/materials.json` and go stale together when the record they bind does.
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, _, _ = self._workspace(temporary)
            materials, models = self._corpus_rows()
            materials[self.MAP_MATERIAL] = {"albedo": "tex/wall.png", "scissor": True}
            self._write_corpus(config.export_root, materials, models)
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]),
                {name: ("materials", "world", "sky", "level")},
            )

    def test_a_material_reached_through_a_placed_slot_reaches_the_same_stages(self) -> None:
        # A placed stem's slot table is the second way a map names a corpus material.
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, _, _ = self._workspace(temporary)
            materials, models = self._corpus_rows()
            materials[self.PROP_MATERIAL] = {"albedo": "tex/prop.png", "blend": True}
            self._write_corpus(config.export_root, materials, models)
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]),
                {name: ("materials", "world", "sky", "level")},
            )

    def test_a_placed_stems_manifest_row_reaches_the_same_stages(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, _, _ = self._workspace(temporary)
            materials, models = self._corpus_rows()
            models["prop"] = {**models["prop"], "physics": True}
            self._write_corpus(config.export_root, materials, models)
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]),
                {name: ("materials", "world", "sky", "level")},
            )

    def test_a_placed_models_catalogue_row_reaches_only_the_level_stage(self) -> None:
        # The level stage joins each `.props` row to `npc/npc_index.json` to decide its actor and
        # its rest clip, so the rows the map places are level inputs -- and only those rows.
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, _, _ = self._workspace(temporary)
            rows = self._placed_index_rows()
            rows[self.UNPLACED_STEM] = {**rows[self.UNPLACED_STEM], "static_equivalent": False}
            self._write_placed_index(config.export_root, rows)
            self.assertEqual(bake_cache.plan_stages(manifest, config, [name]), {})

            rows[self.PLACED_STEM] = {**rows[self.PLACED_STEM], "static_equivalent": False}
            self._write_placed_index(config.export_root, rows)
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]), {name: ("level",)})

    def test_env_mask_inputs_are_the_drawn_materials_existing_masks(self) -> None:
        # The material stage stamps `EnvMaskCoarseMip` out of the mask PNG's own header, so the
        # masks the map's materials name are its inputs; another material's mask is not.
        with tempfile.TemporaryDirectory() as temporary:
            config, _, name, _, _ = self._workspace(temporary)
            corpus = config.export_root / "shared"
            self.assertEqual(
                bake_cache._slice_env_mask_paths(config.export_root, name),
                [corpus / "tex" / "wall_envmask.png"],
            )
            # A record naming a mask the corpus never decoded contributes no input.
            materials, models = self._corpus_rows()
            materials[self.MAP_MATERIAL] = {
                **materials[self.MAP_MATERIAL], "env_mask": "tex/absent_envmask.png"}
            self._write_corpus(config.export_root, materials, models)
            self.assertEqual(bake_cache._slice_env_mask_paths(config.export_root, name), [])

    def test_a_byte_identical_corpus_rewrite_leaves_every_stage_current(self) -> None:
        # A re-decode that lands the same bytes is not a change, whatever the mtime says.
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, _, _ = self._workspace(temporary)
            self._write_corpus(config.export_root, *self._corpus_rows())
            self.assertEqual(bake_cache.plan_stages(manifest, config, [name]), {})

    def test_a_map_no_longer_owns_a_props_stage(self) -> None:
        # Prop meshes belong to the shared corpus scope, so no map plans one.
        self.assertNotIn("props", bake_cache.STAGES)

    def test_deleted_generated_asset_invalidates_its_stage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, _, baked = self._workspace(temporary)
            (baked / "Textures" / "T_wall.uasset").unlink()
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]),
                {name: ("textures",)},
            )

    def test_force_selects_every_stage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, _, _ = self._workspace(temporary)
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name], force=True),
                {name: bake_cache.STAGES},
            )

    def test_asset_recipe_fingerprint_is_semantic_and_policy_scoped(self) -> None:
        recipe = {"source": "tex/wall.png", "sha256": "a" * 64, "role": "albedo"}
        initial = bake_cache.asset_recipe_fingerprint(
            "textures", "/ElysiumBaked/test/Textures/T_wall", "policy", recipe)
        self.assertEqual(
            initial,
            bake_cache.asset_recipe_fingerprint(
                "textures", "/ElysiumBaked/test/Textures/T_wall", "policy",
                {"role": "albedo", "sha256": "a" * 64, "source": "tex/wall.png"}),
        )
        self.assertNotEqual(
            initial,
            bake_cache.asset_recipe_fingerprint(
                "textures", "/ElysiumBaked/test/Textures/T_wall", "policy",
                {**recipe, "sha256": "b" * 64}),
        )
        self.assertNotEqual(
            initial,
            bake_cache.asset_recipe_fingerprint(
                "textures", "/ElysiumBaked/test/Textures/T_wall", "new-policy", recipe),
        )

    def test_asset_receipt_store_replaces_only_selected_stages(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            store = bake_cache.AssetReceiptStore(root, "test_map")
            store.replace_stages({
                "textures": {"policy": "one", "assets": {
                    "/T": {"fingerprint": "a"},
                }},
                "materials": {"policy": "one", "assets": {
                    "/M": {"fingerprint": "b"},
                }},
            })
            store.replace_stages({
                "textures": {"policy": "two", "assets": {
                    "/T2": {"fingerprint": "c"},
                }},
            })
            loaded = bake_cache.AssetReceiptStore(root, "test_map")
            self.assertEqual(set(loaded.stage_assets("textures")), {"/T2"})
            self.assertEqual(set(loaded.stage_assets("materials")), {"/M"})

    def test_malformed_asset_receipt_falls_back_to_empty(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / bake_cache.ASSET_RECEIPT_DIR / "test_map.json"
            path.parent.mkdir(parents=True)
            path.write_text('{"schema":"wrong","stages":{"textures":{"assets":{"/T":{}}}}}',
                            encoding="utf-8")
            self.assertEqual(
                bake_cache.AssetReceiptStore(root, "test_map").stage_assets("textures"), {})

    def test_level_sidecar_recipe_is_semantic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            name = "test_map"
            (root / f"{name}.props").write_text(
                "prop 1 2 3 0 0 0 1 6 0 0\n", encoding="utf-8")
            (root / f"{name}.env").write_text(
                "# ignored\nfog 1\nfogstart 1.0\nfogend 10\n", encoding="utf-8")
            first = bake_cache.level_sidecar_recipe(root, name)
            (root / f"{name}.props").write_text(
                "\nprop 1.000 2.0 3.00 0.0 0 0 1.000 6 0 0\n", encoding="utf-8")
            (root / f"{name}.env").write_text(
                "fog 1\nfogstart 1\nfogend 10.000\nunused value\n", encoding="utf-8")
            self.assertEqual(bake_cache.level_sidecar_recipe(root, name), first)
            (root / f"{name}.props").write_text(
                "prop 2 2 3 0 0 0 1 6 0 0\n", encoding="utf-8")
            self.assertNotEqual(bake_cache.level_sidecar_recipe(root, name), first)

    def test_a_corpus_run_plan_states_the_one_shared_scope(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, _, _, _, _ = self._workspace(temporary)
            path, document = bake_cache.create_corpus_run_plan(config, force=False)
            self.assertEqual(json.loads(path.read_text(encoding="utf-8")), document)
            self.assertEqual(document["schema"], bake_cache.ASSET_RUN_SCHEMA)
            self.assertEqual(document["version"], bake_cache.ASSET_SCHEMA_VERSION)
            self.assertFalse(document["force"])
            self.assertEqual(set(document["maps"]), {shared_corpus.SCOPE})
            scope = document["maps"][shared_corpus.SCOPE]
            self.assertEqual(scope["stages"], list(shared_corpus.STAGES))
            self.assertEqual(set(scope["fingerprints"]), set(shared_corpus.STAGES))
            self.assertEqual(set(scope["policies"]), set(shared_corpus.STAGES))

    def test_a_corpus_policy_moves_only_when_its_bake_code_does(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, _, _, _, _ = self._workspace(temporary)
            first = bake_cache.create_corpus_run_plan(config, force=False)[1]
            second = bake_cache.create_corpus_run_plan(config, force=True)[1]
            self.assertNotEqual(first["run_id"], second["run_id"])
            self.assertTrue(second["force"])
            self.assertEqual(
                first["maps"][shared_corpus.SCOPE]["policies"],
                second["maps"][shared_corpus.SCOPE]["policies"],
            )
            self.assertEqual(
                first["maps"][shared_corpus.SCOPE]["fingerprints"],
                second["maps"][shared_corpus.SCOPE]["fingerprints"],
            )
            (config.repo_root / "pipeline" / "unreal" / "bake_lib.py").write_text(
                "VALUE = 222\n", encoding="utf-8")
            third = bake_cache.create_corpus_run_plan(config, force=False)[1]
            for stage in shared_corpus.STAGES:
                self.assertNotEqual(
                    third["maps"][shared_corpus.SCOPE]["policies"][stage],
                    first["maps"][shared_corpus.SCOPE]["policies"][stage],
                )

    def test_a_corpus_stage_reads_only_its_own_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, _, _, _, _ = self._workspace(temporary)
            before = {
                stage: bake_cache.corpus_stage_input_fingerprint(config, stage)
                for stage in shared_corpus.STAGES
            }
            (config.export_root / "shared" / "props" / "prop.obj").write_text(
                "changed prop", encoding="utf-8")
            after = {
                stage: bake_cache.corpus_stage_input_fingerprint(config, stage)
                for stage in shared_corpus.STAGES
            }
            self.assertEqual(before["textures"], after["textures"])
            self.assertEqual(before["materials"], after["materials"])
            self.assertNotEqual(before["props"], after["props"])

    def test_asset_run_report_rejects_inconsistent_counts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, _, name, _, baked = self._workspace(temporary)
            run_id = "test-run"
            object_path = f"/ElysiumBaked/{name}/Textures/T_wall"
            report = {
                "schema": bake_cache.ASSET_RUN_SCHEMA,
                "version": bake_cache.ASSET_SCHEMA_VERSION,
                "run_id": run_id,
                "map": name,
                "stages": {"textures": {
                    "policy": "policy",
                    "assets": {object_path: {
                        "object_path": object_path,
                        "fingerprint": "a" * 64,
                        "output": str(baked / "Textures" / "T_wall.uasset"),
                    }},
                    "built": 0,
                    "reused": 0,
                    "pruned": 0,
                }},
            }
            bake_cache.write_asset_run_report(config.export_root, report)
            plan = {
                "run_id": run_id,
                "maps": {name: {
                    "stages": ["textures"],
                    "policies": {"textures": "policy"},
                }},
            }
            with self.assertRaisesRegex(RuntimeError, "count mismatch"):
                bake_cache.load_asset_run_reports(config, plan)


if __name__ == "__main__":
    unittest.main()
