from __future__ import annotations

import os
import ast
import importlib.util
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from PIL import Image

from elysium_pipeline.formats import bsp, mdl, tex_to_png, vmt
from elysium_pipeline.formats.glass import derive_normal, is_glass
from elysium_pipeline import paths, unreal as unreal_driver
from elysium_pipeline.validation.png_alpha import alpha_range


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
    @staticmethod
    def _triangle(material: str) -> mdl.Mesh:
        mesh = mdl.Mesh(material)
        mesh.verts = [
            (1.0, 2.0, 3.0, 0.0, 0.0),
            (2.0, 2.0, 3.0, 1.0, 0.0),
            (1.0, 3.0, 3.0, 0.0, 1.0),
        ]
        mesh.tris = [(0, 1, 2)]
        return mesh

    def _mtl_for_vmt(self, vmt_body: str) -> str:
        mesh = self._triangle("glasswin")

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

    def test_semantic_glass_prop_carries_glass_and_derived_normal(self) -> None:
        vmt_body = (
            '"VertexLitGeneric"\n{\n"$basetexture" "props/glasswin"\n'
            '"$translucent" "1"\n"$envmap" "env_cubemap"\n}\n'
        )

        def read_bytes(path):
            if path == "materials/glasswin.vmt":
                return vmt_body.encode("ascii")
            if path in ("materials/props/glasswin.tth", "materials/props/glasswin.ttz"):
                return b"synthetic"
            return None

        source = Image.new("RGBA", (5, 5), (110, 140, 160, 80))
        source.putpixel((2, 2), (180, 200, 210, 80))
        with tempfile.TemporaryDirectory() as out, mock.patch(
            "elysium_pipeline.formats.tex_to_png.decode", return_value=source
        ):
            mdl.write_obj_scene(
                [self._triangle("glasswin")], "test", out, [], read_bytes, {})
            mtl = (Path(out) / "test.mtl").read_text(encoding="utf-8")
            normal = Path(out) / "tex" / "props_glasswin_glass_n.png"
            self.assertTrue(normal.is_file())
        self.assertIn("blend 1", mtl)
        self.assertIn("glass 1", mtl)
        self.assertIn("bumpmap tex/props_glasswin_glass_n.png", mtl)

    def test_authored_glass_bumpmap_takes_precedence(self) -> None:
        vmt_body = (
            '"VertexLitGeneric"\n{\n"$basetexture" "props/glasswin"\n'
            '"$translucent" "1"\n"$envmap" "env_cubemap"\n'
            '"$bumpmap" "props/authored"\n}\n'
        )

        def read_bytes(path):
            if path == "materials/glasswin.vmt":
                return vmt_body.encode("ascii")
            if path.endswith((".tth", ".ttz")):
                return path.encode("ascii")
            return None

        def decode(tth, _ttz):
            color = (128, 128, 255, 255) if b"authored" in tth else (100, 130, 150, 80)
            return Image.new("RGBA", (4, 4), color)

        with tempfile.TemporaryDirectory() as out, mock.patch(
            "elysium_pipeline.formats.tex_to_png.decode", side_effect=decode
        ):
            mdl.write_obj_scene(
                [self._triangle("glasswin")], "test", out, [], read_bytes, {})
            mtl = (Path(out) / "test.mtl").read_text(encoding="utf-8")
            self.assertFalse((Path(out) / "tex" / "props_glasswin_glass_n.png").exists())
        self.assertIn("bumpmap tex/props_authored_n.png", mtl)

    def test_source_refract_prop_exports_dudv_as_distortion_not_albedo(self) -> None:
        vmt_body = (
            '"Refract"\n{\n"$dudvmap" "props/rain_dudv"\n'
            '"$refractamount" ".01"\n"$model" "1"\n}\n'
        )

        def read_bytes(path):
            if path == "materials/glasswin.vmt":
                return vmt_body.encode("ascii")
            if path in ("materials/props/rain_dudv.tth", "materials/props/rain_dudv.ttz"):
                return b"synthetic"
            return None

        signed = Image.new("RGBA", (2, 1))
        signed.putdata([(0, 0, 127, 255), (255, 1, 127, 255)])
        with tempfile.TemporaryDirectory() as out, mock.patch(
            "elysium_pipeline.formats.tex_to_png.decode", return_value=signed
        ):
            mdl.write_obj_scene(
                [self._triangle("glasswin")], "test", out, [], read_bytes, {})
            mtl = (Path(out) / "test.mtl").read_text(encoding="utf-8")
            normal_path = Path(out) / "tex" / "props_rain_dudv_refract_n.png"
            with Image.open(normal_path) as normal:
                self.assertEqual(list(normal.get_flattened_data()), [
                    (128, 128, 255), (127, 129, 255)])

        self.assertIn("refract 0.010000", mtl)
        self.assertIn("refractmap tex/props_rain_dudv_refract_n.png", mtl)
        self.assertNotIn("blend 1", mtl)
        self.assertNotIn("glass 1", mtl)
        self.assertNotIn("map_Kd", mtl)

    def test_source_refract_prefers_authored_normal_over_dudv_fallback(self) -> None:
        vmt_body = (
            '"Refract"\n{\n"$dudvmap" "props/old_dudv"\n'
            '"$normalmap" "props/authored_normal"\n"$refractamount" "2"\n}\n'
        )
        decoded = []

        def read_bytes(path):
            if path == "materials/glasswin.vmt":
                return vmt_body.encode("ascii")
            if path in (
                    "materials/props/authored_normal.tth",
                    "materials/props/authored_normal.ttz"):
                return path.encode("ascii")
            return None

        def decode(tth, _ttz):
            decoded.append(tth)
            return Image.new("RGBA", (1, 1), (128, 128, 255, 255))

        with tempfile.TemporaryDirectory() as out, mock.patch(
            "elysium_pipeline.formats.tex_to_png.decode", side_effect=decode
        ):
            mdl.write_obj_scene(
                [self._triangle("glasswin")], "test", out, [], read_bytes, {})
            mtl = (Path(out) / "test.mtl").read_text(encoding="utf-8")

        self.assertEqual(len(decoded), 1)
        self.assertIn(b"authored_normal", decoded[0])
        self.assertIn("refractmap tex/props_authored_normal_refract_n.png", mtl)
        self.assertNotIn("old_dudv_refract_n", mtl)

    def _export_texture(self, flag: str) -> Image.Image:
        vmt_body = (
            '"VertexLitGeneric"\n{\n"$basetexture" "props/shared"\n'
            + (f'"${flag}" "1"\n' if flag else "")
            + "}\n"
        )

        def read_bytes(path):
            if path == "materials/glasswin.vmt":
                return vmt_body.encode("ascii")
            if path in ("materials/props/shared.tth", "materials/props/shared.ttz"):
                return b"synthetic"
            return None

        source = Image.new("RGBA", (2, 1))
        source.putdata([(10, 20, 30, 0), (40, 50, 60, 191)])
        with tempfile.TemporaryDirectory() as out, mock.patch(
            "elysium_pipeline.formats.tex_to_png.decode", return_value=source
        ):
            mdl.write_obj_scene(
                [self._triangle("glasswin")], "test", out, [], read_bytes, {})
            with Image.open(Path(out) / "tex" / "props_shared.png") as exported:
                return exported.copy()

    def test_translucent_prop_preserves_source_alpha(self) -> None:
        image = self._export_texture("translucent")
        self.assertEqual(image.mode, "RGBA")
        self.assertEqual([image.getpixel((x, 0))[3] for x in range(2)], [0, 191])

    def test_alphatest_prop_preserves_source_alpha(self) -> None:
        image = self._export_texture("alphatest")
        self.assertEqual(image.mode, "RGBA")
        self.assertEqual([image.getpixel((x, 0))[3] for x in range(2)], [0, 191])

    def test_opaque_prop_stays_rgb(self) -> None:
        self.assertEqual(self._export_texture("").mode, "RGB")

    def test_shared_basetexture_promotes_cached_rgb_to_rgba(self) -> None:
        vmts = {
            "materials/opaque.vmt": (
                '"VertexLitGeneric"\n{\n"$basetexture" "props/shared"\n}\n'
            ),
            "materials/glass.vmt": (
                '"VertexLitGeneric"\n{\n"$basetexture" "props/shared"\n'
                '"$translucent" "1"\n}\n'
            ),
        }

        def read_bytes(path):
            if path in vmts:
                return vmts[path].encode("ascii")
            if path in ("materials/props/shared.tth", "materials/props/shared.ttz"):
                return b"synthetic"
            return None

        source = Image.new("RGBA", (1, 1), (10, 20, 30, 73))
        with tempfile.TemporaryDirectory() as out, mock.patch(
            "elysium_pipeline.formats.tex_to_png.decode", return_value=source
        ):
            mdl.write_obj_scene(
                [self._triangle("opaque"), self._triangle("glass")],
                "test", out, [], read_bytes, {})
            with Image.open(Path(out) / "tex" / "props_shared.png") as exported:
                self.assertEqual(exported.mode, "RGBA")
                self.assertEqual(exported.getchannel("A").getpixel((0, 0)), 73)


class PngAlphaContractTests(unittest.TestCase):
    def test_reads_rgba_alpha_range_and_rejects_rgb_as_alpha(self) -> None:
        with tempfile.TemporaryDirectory() as out:
            rgba = Path(out) / "rgba.png"
            rgb = Path(out) / "rgb.png"
            image = Image.new("RGBA", (2, 1))
            image.putdata([(0, 0, 0, 17), (0, 0, 0, 239)])
            image.save(rgba)
            image.convert("RGB").save(rgb)
            self.assertEqual(alpha_range(rgba), (17, 239))
            self.assertIsNone(alpha_range(rgb))


class SourceRefractContractTests(unittest.TestCase):
    def test_vmt_parser_keeps_refract_shader_inputs_without_basetexture(self) -> None:
        info = vmt.parse(
            '"Refract"\n{\n"$dudvmap" "Props\\Rain_DUDV"\n'
            '"$refractamount" ".01"\n}\n')
        self.assertTrue(info["refract"])
        self.assertIsNone(info["basetexture"])
        self.assertEqual(info["dudvmap"], "props/rain_dudv")
        self.assertEqual(info["refractamount"], 0.01)

    def test_uvwq_signed_vectors_convert_deterministically_to_tangent_normal(self) -> None:
        raw = bytes((0, 0, 127, 255, 127, 128, 127, 255, 255, 1, 127, 255))
        decoded = tex_to_png._decode_mip(
            raw, 3, 1, tex_to_png.FMT_UVWQ8888)
        first = tex_to_png.dudv_to_normal(decoded)
        second = tex_to_png.dudv_to_normal(decoded)
        self.assertEqual(first.tobytes(), second.tobytes())
        self.assertEqual(list(first.get_flattened_data()), [
            (128, 128, 255), (255, 0, 255), (127, 129, 255)])
        self.assertEqual(
            tex_to_png.mip_byte_size(3, 1, tex_to_png.FMT_UVWQ8888), 12)


class GlassMaterialContractTests(unittest.TestCase):
    @staticmethod
    def _info(shader="lightmappedgeneric", **overrides):
        info = {
            "shader": shader,
            "basetexture": "glass/window",
            "translucent": True,
            "envmap": "env_cubemap",
            "additive": False,
            "decal": False,
            "water": False,
        }
        info.update(overrides)
        return info

    def test_world_and_prop_lit_reflective_glass_classify(self) -> None:
        self.assertTrue(is_glass(self._info(), "glass/pawnwndwglass"))
        self.assertTrue(is_glass(
            self._info(shader="vertexlitgeneric",
                       basetexture="models/scenery/misc/wall_clock/clockglass"),
            "models/scenery/misc/wall_clock/clockglass"))

    def test_non_glass_transparency_combinations_stay_generic(self) -> None:
        self.assertFalse(is_glass(
            self._info(envmap=None, basetexture="models/scenery/theater/neta"),
            "models/scenery/theater/neta"))
        self.assertFalse(is_glass(self._info(shader="unlitgeneric"), "effects/glass_fleck"))
        self.assertFalse(is_glass(self._info(additive=True), "models/light/glass"))
        self.assertFalse(is_glass(self._info(decal=True), "glass/poster"))
        self.assertFalse(is_glass(self._info(water=True), "glass/water"))
        self.assertFalse(is_glass(
            self._info(basetexture="models/scenery/theater/curtains"),
            "models/scenery/theater/curtains"))

    def test_derived_normal_is_deterministic_and_flat_outside_mask(self) -> None:
        source = Image.new("RGBA", (7, 7), (80, 100, 120, 90))
        for y in range(7):
            for x in range(7):
                source.putpixel((x, y), (60 + x * 20, 70 + y * 15, 100, 90))
        mask = Image.new("L", (7, 7), 255)
        for y in range(7):
            mask.putpixel((3, y), 0)
        first = derive_normal(source, mask)
        second = derive_normal(source, mask)
        self.assertEqual(first.tobytes(), second.tobytes())
        self.assertEqual(first.getpixel((3, 3)), (128, 128, 255))
        self.assertNotEqual(first.getpixel((1, 3)), (128, 128, 255))

    def test_derived_normal_resamples_independently_sized_mask(self) -> None:
        source = Image.new("RGBA", (8, 8), (0, 0, 0, 255))
        for y in range(8):
            for x in range(8):
                value = x * 30
                source.putpixel((x, y), (value, value, value, 255))
        mask = Image.new("L", (2, 2), 0)
        mask.putpixel((0, 0), 255)
        mask.putpixel((0, 1), 255)

        normal = derive_normal(source, mask, blur_radius=0.0)

        self.assertEqual(normal.size, source.size)
        self.assertNotEqual(normal.getpixel((1, 3)), (128, 128, 255))
        self.assertEqual(normal.getpixel((6, 3)), (128, 128, 255))

    def test_uniform_glass_normal_is_neutral(self) -> None:
        normal = derive_normal(Image.new("RGBA", (5, 5), (100, 120, 140, 80)))
        self.assertEqual(set(normal.get_flattened_data()), {(128, 128, 255)})


class BakeTextureImportContractTests(unittest.TestCase):
    @staticmethod
    def _load_bake_lib(fake_unreal):
        spec = importlib.util.spec_from_file_location(
            "elysium_test_bake_lib", REPO / "pipeline/unreal/bake_lib.py")
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {"unreal": fake_unreal}):
            spec.loader.exec_module(module)
        return module

    def test_existing_texture_is_submitted_for_in_place_replacement(self) -> None:
        tasks = []
        asset = object()

        class FakeTask:
            pass

        tools = SimpleNamespace(import_asset_tasks=lambda submitted: tasks.extend(submitted))
        editor = SimpleNamespace(
            does_directory_exist=lambda _target: True,
            make_directory=lambda _target: True,
            does_asset_exist=lambda _target: True,
            load_asset=lambda _target: asset,
        )
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: tools),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
            AssetImportTask=FakeTask,
            EditorAssetLibrary=editor,
        )
        module = self._load_bake_lib(fake_unreal)

        result = module.import_textures([("updated.png", "T_existing")], "/Test")
        self.assertEqual(result, {"T_existing": asset})
        self.assertEqual(len(tasks), 1)
        self.assertTrue(tasks[0].replace_existing)
        self.assertTrue(tasks[0].replace_existing_settings)

    def test_bake_mtl_parser_keeps_semantic_glass_flag(self) -> None:
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
        )
        module = self._load_bake_lib(fake_unreal)
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "glass.mtl"
            path.write_text(
                "newmtl pane\nmap_Kd tex/pane.png\nblend 1\nglass 1\n"
                "bumpmap tex/pane_glass_n.png\n",
                encoding="utf-8")
            mat = module.read_mtl(path)["pane"]
        self.assertTrue(mat.blend)
        self.assertTrue(mat.glass)
        self.assertEqual(mat.bump, "tex/pane_glass_n.png")

    def test_bake_mtl_parser_keeps_source_refract_contract(self) -> None:
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
        )
        module = self._load_bake_lib(fake_unreal)
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "refract.mtl"
            path.write_text(
                "newmtl rain\nrefract 0.010000\n"
                "refractmap tex/rain_refract_n.png\n",
                encoding="utf-8")
            mat = module.read_mtl(path)["rain"]
        self.assertTrue(mat.refract)
        self.assertFalse(mat.opaque)
        self.assertEqual(mat.refract_amount, 0.01)
        self.assertEqual(mat.refract_map, "tex/rain_refract_n.png")


class UnrealBakeDriverContractTests(unittest.TestCase):
    def test_texture_bake_enables_commandlet_rendering(self) -> None:
        submitted = []
        runner = SimpleNamespace(
            run=lambda command, cwd: submitted.append((command, cwd))
            or SimpleNamespace(returncode=0)
        )
        config = SimpleNamespace(
            repo_root=REPO,
            project=REPO / "ElysiumUE.uproject",
        )
        with mock.patch.object(
                unreal_driver, "editor_executable", return_value=Path("UnrealEditor-Cmd.exe")):
            unreal_driver.bake_maps(config, runner, ["sp_theatre"])

        self.assertEqual(len(submitted), 1)
        self.assertIn("-AllowCommandletRendering", submitted[0][0])


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
