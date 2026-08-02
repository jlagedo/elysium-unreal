import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

from elysium_pipeline.formats import particles, tex_to_png, vmt, weather


def wet_vmt(scales=(0.6, 0.6, 0.6), *, comments=False):
    blocks = []
    for channel, scale in enumerate(scales):
        suffix = " // authored" if comments else ""
        blocks.append(
            f'''"gLoBaLwEtNeSs" {{ "resultVar" "$envmaptint[{channel}]" "scale" "{scale}" }}{suffix}'''
        )
    return '''"LightmappedGeneric" {
        "$basetexture" "street/wet"
        "$envmap" "env_cubemap"
        "Proxies" { %s }
    }''' % "\n".join(blocks)


class GlobalWetnessVmtTests(unittest.TestCase):
    def test_proxy_casing_comments_quoting_and_scalar_extraction(self):
        self.assertEqual(vmt.parse(wet_vmt(comments=True))["globalwetness"], 0.6)

    def test_absent_proxy_is_not_wetness_driven(self):
        self.assertIsNone(vmt.parse('LightmappedGeneric { "$basetexture" "x" }')["globalwetness"])

    def test_partial_malformed_and_unequal_triples_fail(self):
        for document in (
            wet_vmt((0.6, 0.6)),
            wet_vmt((0.6, "nope", 0.6)),
            wet_vmt((0.6, 0.5, 0.6)),
        ):
            with self.subTest(document=document):
                with self.assertRaises(vmt.VmtContractError):
                    vmt.parse(document)

    def test_patch_inherits_parent_proxy(self):
        result = vmt.parse(
            'Patch { "include" "materials/base.vmt" }',
            resolve_include=lambda _path: wet_vmt((0.25, 0.25, 0.25)),
        )
        self.assertEqual(result["globalwetness"], 0.25)


class CubemapDdsTests(unittest.TestCase):
    def test_six_faces_keep_vtf_order_and_bgra_bytes(self):
        colours = [
            (1, 2, 3, 4), (11, 12, 13, 14), (21, 22, 23, 24),
            (31, 32, 33, 34), (41, 42, 43, 44), (51, 52, 53, 54),
        ]
        data = tex_to_png.cubemap_dds([
            Image.new("RGBA", (2, 2), colour) for colour in colours
        ])
        self.assertEqual(data[:4], b"DDS ")
        self.assertEqual(int.from_bytes(data[12:16], "little"), 2)
        self.assertEqual(int.from_bytes(data[16:20], "little"), 2)
        self.assertEqual(int.from_bytes(data[112:116], "little"), 0xFE00)
        pixels = data[128:]
        face_bytes = 2 * 2 * 4
        for index, (r, g, b, a) in enumerate(colours):
            self.assertEqual(
                pixels[index * face_bytes:index * face_bytes + 4], bytes((b, g, r, a))
            )

    def test_missing_unequal_and_non_square_faces_fail(self):
        with self.assertRaises(ValueError):
            tex_to_png.cubemap_dds([Image.new("RGBA", (2, 2))] * 5)
        with self.assertRaises(ValueError):
            tex_to_png.cubemap_dds(
                [Image.new("RGBA", (2, 2))] * 5 + [Image.new("RGBA", (4, 4))]
            )
        with self.assertRaises(ValueError):
            tex_to_png.cubemap_dds([Image.new("RGBA", (2, 3))] * 6)


class WorldMaterialWetnessGraphTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (
            Path(__file__).resolve().parents[1] / "unreal" / "make_world_materials.py"
        ).read_text(encoding="utf-8")

    def test_source_cube_is_additive_and_excluded_from_lumen_capture(self):
        self.assertIn(
            'mat, "WetnessUsesSourceCube", darkened_base, base_color', self.source
        )
        self.assertIn(
            'connect(source_view, "", lumen_safe_source, "Normal")', self.source
        )
        self.assertIn(
            'connect(black, "", lumen_safe_source, "RayTraced")', self.source
        )
        self.assertIn(
            'connect(lumen_safe_source, "", primary_emissive, "B")', self.source
        )

    def test_reflection_debug_range_compares_debug_value_against_threshold(self):
        self.assertIn(
            'lower.set_editor_property("const_y", index - 0.5)', self.source
        )
        self.assertIn('connect(debug, "", lower, "X")', self.source)
        self.assertIn(
            'upper.set_editor_property("const_y", index + 0.5)', self.source
        )
        self.assertIn('connect(debug, "", upper, "X")', self.source)
        self.assertIn('debug_enabled.set_editor_property("const_y", 0.5)', self.source)
        self.assertIn('connect(debug, "", debug_enabled, "X")', self.source)


class ParticleClosureTests(unittest.TestCase):
    DEFINITIONS = {
        "rain_follow_emitter": '''Particle {
            loop 1 precipitation 1
            // disabled blocks must not become live dependencies
            // spawn { particle disabled rate 999 }
            spawn { particle raindrops2 rate 1000 radius 0 theta "0~360" phi 0 }
        }''',
        "raindrops2": '''Particle {
            sprite dropletfast frames 15 movealign 1
            x_speed 20 y_speed 20 z_speed "-400~-600" size 3 height 10
            color "0,80(10)" mask 0 precipitation 1
            collide {
                spawn { particle rainsplash_new friction 0 bounce 0 }
                decal { particle rainstain }
            }
        }''',
        "rainsplash_new": '''Particle {
            sprite fortituderings frames 12 flat 1 x_speed 0 y_speed 0 z_speed 0
            size "1,8,14" rotation 0 color "60,0" mask "40,0" precipitation 1
        }''',
        "rainstain": '''Particle {
            sprite d_targetblob frames 30 size "2~4" color "10,0" mask "90,0"
            precipitation 1
        }''',
    }

    def compile(self, *, sprites=None):
        available = sprites or {"dropletfast", "fortituderings", "d_targetblob"}
        return particles.compile_closure(
            ["rain_follow_emitter"], self.DEFINITIONS.get, available.__contains__
        )

    def test_comment_handling_dependency_collision_and_unit_conversion(self):
        result = self.compile()
        self.assertEqual(
            list(result["definitions"]),
            ["rain_follow_emitter", "raindrops2", "rainsplash_new", "rainstain"],
        )
        rain = result["definitions"]["raindrops2"]
        self.assertAlmostEqual(rain["velocity_cm_per_second"]["x"]["values"][0], 50.8)
        self.assertAlmostEqual(rain["velocity_cm_per_second"]["y"]["values"][0], -50.8)
        self.assertEqual(rain["collision"]["decal"]["particle"], "rainstain")
        self.assertNotIn("disabled", result["definitions"])

    def test_missing_asset_and_unsupported_live_field_fail(self):
        with self.assertRaises(particles.ParticleContractError):
            self.compile(sprites={"dropletfast", "fortituderings"})
        changed = dict(self.DEFINITIONS)
        changed["raindrops2"] = changed["raindrops2"].replace(
            "sprite dropletfast", "sprite dropletfast unknown_live_field 1"
        )
        with self.assertRaises(particles.ParticleContractError):
            particles.compile_closure(
                ["rain_follow_emitter"], changed.get,
                {"dropletfast", "fortituderings", "d_targetblob"}.__contains__,
            )


class HeightTextureTests(unittest.TestCase):
    def test_only_solid_non_sky_props_become_placed_cover(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "props").mkdir()
            (root / "props" / "awning.obj").write_text(
                "v 0 0 0\nv 10 0 0\nv 0 10 0\nf 1 2 3\n", encoding="utf-8"
            )
            (root / "map.props").write_text(
                "awning 100 200 300 0 0 0 1 1 0 0\n"
                "awning 400 500 600 0 0 0 1 0 0 0\n"
                "awning 700 800 900 0 0 0 1 1 0 1\n",
                encoding="utf-8",
            )
            triangles = weather.static_prop_cover_triangles(root, "map")
            self.assertEqual(len(triangles), 1)
            self.assertEqual(triangles[0][0], (100.0, 200.0, 300.0))
            self.assertEqual(triangles[0][2], (100.0, 210.0, 300.0))

    def test_r16_sentinel_bounds_resolution_and_decode(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "height.png"
            metadata = weather.rasterize_height(
                [((0.0, 0.0, 50.0), (100.0, 0.0, 50.0), (0.0, 100.0, 50.0))],
                path,
                (0.0, 0.0, -100.0),
                (28971.24, 19639.28, 1300.48),
                resolution=2048,
            )
            image = np.asarray(Image.open(path), dtype=np.uint16)
            self.assertEqual(image.shape, (2048, 2048))
            self.assertEqual(int(image[-1, -1]), 0)
            sample = int(image[1, 1])
            self.assertGreater(sample, 0)
            decoded = metadata["min_z_cm"] + (sample - 1) * metadata["z_scale_cm"]
            self.assertAlmostEqual(decoded, 50.0, delta=metadata["z_scale_cm"])
            self.assertEqual(metadata["format"], "R16_UNORM")


if __name__ == "__main__":
    unittest.main()
