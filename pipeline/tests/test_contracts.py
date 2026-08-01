from __future__ import annotations

import os
import ast
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from elysium_pipeline.formats import bsp, mdl
from elysium_pipeline import paths


REPO = Path(__file__).resolve().parents[2]
POLICY_SPEC = importlib.util.spec_from_file_location(
    "elysium_repo_policy", REPO / "dev/check_repo_policy.py"
)
assert POLICY_SPEC and POLICY_SPEC.loader
POLICY = importlib.util.module_from_spec(POLICY_SPEC)
POLICY_SPEC.loader.exec_module(POLICY)


class CoordinateContractTests(unittest.TestCase):
    def test_source_to_unreal_contract(self) -> None:
        self.assertEqual(bsp.source_to_unreal(1.0, 2.0, 3.0), (2.54, -5.08, 7.62))
        self.assertEqual(bsp.source_dir_to_unreal(1.0, 2.0, 3.0), (1.0, -2.0, 3.0))

    def test_coordinate_owners_are_unique(self) -> None:
        definitions: list[Path] = []
        for root in (REPO / "pipeline/src", REPO / "pipeline/unreal"):
            for source in root.rglob("*.py"):
                text = source.read_text(encoding="utf-8")
                names = {
                    node.name for node in ast.walk(ast.parse(text))
                    if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
                }
                if {"source_to_unreal", "source_dir_to_unreal"} & names:
                    definitions.append(source)
        self.assertEqual(definitions, [REPO / "pipeline/src/elysium_pipeline/formats/bsp.py"])

    def test_legacy_godot_transform_is_absent(self) -> None:
        self.assertFalse(hasattr(bsp, "source_to_godot"))

    def test_obj_writer_reverses_reflected_winding(self) -> None:
        source = (
            REPO / "pipeline/src/elysium_pipeline/exporters/UE_bsp_to_scene.py"
        ).read_text(encoding="utf-8")
        self.assertIn(
            'o.write(f"f {a+1}/{a+1} {c+1}/{c+1} {b+1}/{b+1}\\n")',
            source,
        )

    def test_model_obj_writer_is_unreal_only(self) -> None:
        mesh = mdl.Mesh("test")
        mesh.verts = [
            (1.0, 2.0, 3.0, 0.0, 0.0),
            (2.0, 2.0, 3.0, 1.0, 0.0),
            (1.0, 3.0, 3.0, 0.0, 1.0),
        ]
        mesh.tris = [(0, 1, 2)]
        with tempfile.TemporaryDirectory() as out:
            mdl.write_obj_scene(
                [mesh],
                "test",
                out,
                [],
                lambda _key: None,
                {},
            )
            obj = (Path(out) / "test.obj").read_text(encoding="utf-8")
        self.assertIn("v 2.540000 -5.080000 7.620000", obj)
        self.assertIn("f 1/1 3/3 2/2", obj)


class PropMaterialContractTests(unittest.TestCase):
    def _mtl_for_vmt(self, vmt_body: str) -> str:
        mesh = mdl.Mesh("glasswin")
        mesh.verts = [
            (1.0, 2.0, 3.0, 0.0, 0.0),
            (2.0, 2.0, 3.0, 1.0, 0.0),
            (1.0, 3.0, 3.0, 0.0, 1.0),
        ]
        mesh.tris = [(0, 1, 2)]

        def read_bytes(path):
            return vmt_body.encode("ascii") if path == "materials/glasswin.vmt" else None

        with tempfile.TemporaryDirectory() as out:
            mdl.write_obj_scene([mesh], "test", out, [], read_bytes, {})
            return (Path(out) / "test.mtl").read_text(encoding="utf-8")

    def test_translucent_prop_material_carries_blend_flag(self) -> None:
        mtl = self._mtl_for_vmt(
            '"VertexLitGeneric"\n{\n"$basetexture" "props/glasswin"\n"$translucent" "1"\n}\n'
        )
        self.assertIn("blend 1", mtl)

    def test_alphatest_prop_material_carries_illum_flag(self) -> None:
        mtl = self._mtl_for_vmt(
            '"VertexLitGeneric"\n{\n"$basetexture" "props/glasswin"\n"$alphatest" "1"\n}\n'
        )
        self.assertIn("illum 4", mtl)


class LightingBakeContractTests(unittest.TestCase):
    def test_spot_cones_use_both_authored_cosines(self) -> None:
        source = (REPO / "pipeline/unreal/bake_map.py").read_text(encoding="utf-8")
        self.assertIn("stopdot = float(tok[11])", source)
        self.assertIn("stopdot2 = float(tok[12])", source)
        self.assertIn("component.set_inner_cone_angle(min(inner, outer))", source)
        self.assertNotIn("outer * 0.6", source)


class PathContractTests(unittest.TestCase):
    def test_work_root_derivations(self) -> None:
        with tempfile.TemporaryDirectory() as work:
            with mock.patch.dict(
                os.environ,
                {"ELYSIUM_WORK_ROOT": work},
                clear=False,
            ):
                os.environ.pop("ELYSIUM_EXPORT_ROOT", None)
                root = Path(work).resolve()
                self.assertEqual(paths.export_root(), root / "exports")
                self.assertEqual(paths.research_root(), root / "research")
                self.assertEqual(paths.cache_root(), root / "cache")
                self.assertEqual(paths.log_root(), root / "logs")
                self.assertEqual(paths.scratch_root(), root / "scratch")

    def test_missing_work_root_has_no_repository_fallback(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(RuntimeError, "ELYSIUM_WORK_ROOT"):
                paths.work_root()


class NamingContractTests(unittest.TestCase):
    def test_legacy_godot_scripts_are_absent(self) -> None:
        legacy = (
            "pipeline/src/elysium_pipeline/exporters/bsp_to_obj.py",
            "pipeline/src/elysium_pipeline/exporters/menu_extract.py",
            "pipeline/src/elysium_pipeline/enhancement/build_grade_lut.py",
            "pipeline/src/elysium_pipeline/validation/make_testmap.py",
            "pipeline/src/elysium_pipeline/validation/render_obj.py",
            "research/tooling/probes/sdfgi_probe.py",
            "research/tooling/probes/probe_lightstyles_where.py",
        )
        self.assertFalse([path for path in legacy if (REPO / path).exists()])

    def test_unreal_exporters_keep_ue_prefix(self) -> None:
        exporters = REPO / "pipeline/src/elysium_pipeline/exporters"
        expected = {
            "UE_bsp_to_scene.py",
            "UE_extract_cfg.py",
            "UE_extract_scenes.py",
            "UE_extract_scripts.py",
            "UE_extract_signs.py",
            "UE_extract_sounds.py",
            "UE_extract_ui.py",
            "UE_extract_vdata.py",
            "UE_use_icons.py",
        }
        self.assertTrue(expected.issubset({path.name for path in exporters.glob("UE_*.py")}))

    def test_gltf_exemption_remains_present(self) -> None:
        self.assertTrue(
            (REPO / "pipeline/src/elysium_pipeline/formats/mdl_gltf.py").is_file()
        )


class RepositoryPolicyTests(unittest.TestCase):
    def test_forbidden_assets_and_backups_are_detected(self) -> None:
        for path in (
            "Content/Test.uasset",
            "Plugins/ElysiumBaked/Content/Test.umap",
            "scratch/note.py",
            "Source/Fix.cpp.orig",
            "logs/build.log",
            "extracted/maps/sp_theatre.bsp",
            "research/evidence/vampire.gpr",
        ):
            with self.subTest(path=path):
                self.assertIsNotNone(POLICY.prohibited(path))

    def test_authored_source_is_not_prohibited(self) -> None:
        self.assertIsNone(POLICY.prohibited("pipeline/src/elysium_pipeline/formats/bsp.py"))


if __name__ == "__main__":
    unittest.main()
