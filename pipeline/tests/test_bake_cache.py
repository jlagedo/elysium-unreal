from __future__ import annotations

import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest

from elysium_pipeline import bake_cache
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
    def _workspace(self, temporary: str):
        repo = Path(temporary) / "repo"
        export = Path(temporary) / "exports"
        unreal_root = repo / "pipeline" / "unreal"
        package_root = repo / "pipeline" / "src" / "elysium_pipeline"
        unreal_root.mkdir(parents=True)
        package_root.mkdir(parents=True)
        (unreal_root / "bake_map.py").write_text(BAKE_MAP_SOURCE, encoding="utf-8")
        (unreal_root / "bake_lib.py").write_text("VALUE = 1\n", encoding="utf-8")
        (unreal_root / "make_particle_systems.py").write_text("VALUE = 1\n", encoding="utf-8")
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
        (map_root / "props" / "tex").mkdir(parents=True)
        (map_root / "tex").mkdir(parents=True)
        for suffix, contents in {
            ".obj": "world",
            ".blend": "0\n",
            ".mtl": "newmtl wall\n",
            "_sky.obj": "sky",
            ".props": "prop placement",
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
        (map_root / "props" / "prop.obj").write_text("prop", encoding="utf-8")
        (map_root / "props" / "prop.mtl").write_text("newmtl prop", encoding="utf-8")
        (map_root / "tex" / "wall.png").write_bytes(b"texture")

        baked = repo / "Plugins" / "ElysiumBaked" / "Content" / map_name
        outputs = {
            "Textures/T_wall.uasset": b"texture",
            "Materials/MI_wall.uasset": b"material",
            "Meshes/SM_World_0_0_0.uasset": b"world",
            "Meshes/SM_Sky_0_0_0.uasset": b"sky",
            "Props/SM_prop.uasset": b"prop",
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
            (root / f"{name}.ents").write_text("changed runtime data", encoding="utf-8")
            self.assertEqual(bake_cache.plan_stages(manifest, config, [name]), {})

    def test_texture_pixel_change_only_invalidates_texture_stage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, root, _ = self._workspace(temporary)
            (root / "tex" / "wall.png").write_bytes(b"changed pixels")
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

    def test_prop_change_invalidates_props_and_level(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, root, _ = self._workspace(temporary)
            (root / "props" / "prop.obj").write_text("changed prop", encoding="utf-8")
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]),
                {name: ("props", "level")},
            )

    def test_prop_stage_code_change_does_not_invalidate_other_asset_stages(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            config, manifest, name, _, _ = self._workspace(temporary)
            bake_map = config.repo_root / "pipeline" / "unreal" / "bake_map.py"
            source = bake_map.read_text(encoding="utf-8").replace(
                "def stage_props(self): self.resolve_materials()",
                "def stage_props(self): self.resolve_materials(); helper()",
            )
            bake_map.write_text(source, encoding="utf-8")
            self.assertEqual(
                bake_cache.plan_stages(manifest, config, [name]),
                {name: ("props", "level")},
            )

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
