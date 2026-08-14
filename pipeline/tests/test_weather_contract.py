import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

from elysium_pipeline import shared_corpus
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


class ImpactParticleContractTests(unittest.TestCase):
    """The non-precipitation vocabulary the cinematic emitters use."""

    DEFINITIONS = {
        "impact_emitter": '''Particle {
            loop 1
            spawn { particle impact_fx burst 40 phi "-40~40" theta "-40~40" x 2 y 3 z "-1" }
            spawn { particle impact_fx rate 30 }
        }''',
        "impact_fx": '''Particle {
            frames 30 sprite bloodspray size 5 height "3,1" width 2 movealign 1
            parent_speed 1 radius_speed "-40~-100,0" elevation_speed "0,-150" depth_offset 10
            red "255~150" green 0 blue 0 color "100,0" mask "255,0"
        }''',
    }

    def compile(self):
        return particles.compile_closure(
            ["impact_emitter"], self.DEFINITIONS.get, {"bloodspray"}.__contains__
        )

    def test_burst_and_cartesian_spawn_offset(self):
        spawns = self.compile()["definitions"]["impact_emitter"]["spawns"]
        self.assertAlmostEqual(spawns[0]["burst"]["values"][0], 40.0)
        offset = spawns[0]["offset_cm"]
        self.assertAlmostEqual(offset["x"]["values"][0], 5.08)
        # `y` takes the same Source reflection the velocities do.
        self.assertAlmostEqual(offset["y"]["values"][0], -7.62)
        self.assertAlmostEqual(offset["z"]["values"][0], -2.54)
        self.assertNotIn("offset_cm", spawns[1])
        self.assertAlmostEqual(spawns[1]["rate"]["values"][0], 30.0)

    def test_spherical_speeds_convert_and_parent_speed_stays_dimensionless(self):
        fx = self.compile()["definitions"]["impact_fx"]
        radial = fx["radial_velocity_cm_per_second"]
        self.assertAlmostEqual(radial["radius"]["values"][0], -101.6)
        self.assertAlmostEqual(radial["elevation"]["values"][1], -381.0)
        self.assertEqual(radial["radius"]["kind"], "range")
        # A fraction of the parent's velocity, not a length.
        self.assertAlmostEqual(fx["parent_speed"]["values"][0], 1.0)
        # Unresolved unit, so carried verbatim.
        self.assertAlmostEqual(fx["depth_offset"]["values"][0], 10.0)

    def test_rgb_channels_and_width_are_carried(self):
        fx = self.compile()["definitions"]["impact_fx"]
        self.assertEqual(fx["red"]["values"], [255.0, 150.0])
        self.assertEqual(fx["green"]["values"], [0.0])
        self.assertEqual(fx["blue"]["values"], [0.0])
        self.assertAlmostEqual(fx["width_cm"]["values"][0], 5.08)


class MapParticleDocumentTests(unittest.TestCase):
    DEFINITIONS = {
        "good_emitter": 'Particle { loop 1 spawn { particle good_fx burst 4 } }',
        "good_fx": 'Particle { frames 10 sprite spark size 1 }',
        "force_feeding_emitter":
            'Particle { frames 10 spawn { particle force_feeding_fx1 burst 20 } }',
        "force_feeding_fx1": 'Particle { frames 10 sprite spark size 4 }',
        # Uses a key the contract has not established.
        "broken_emitter": 'Particle { frames 10 sprite spark sortfront 1 }',
    }

    def document(self):
        entities = {"entities": [
            {"classname": "env_particle", "targetname": "attached",
             "origin": [1.0, 2.0, 3.0],
             "keys": {"particle_definition": "good_emitter", "active": "1",
                      "attach_type": "2", "parentname": "Sire2", "bone": "Bip01 Neck",
                      "bounds": "512"}},
            # Spelled as a path with an extension - the same file.
            {"classname": "env_particle", "targetname": "pathspelled",
             "keys": {"particle_definition": "particles/good_emitter.txt"}},
            {"classname": "env_particle", "targetname": "unresolvable",
             "keys": {"particle_definition": "broken_emitter"}},
            {"classname": "env_particle", "targetname": "nodefinition", "keys": {}},
            {"classname": "logic_relay", "targetname": "notaparticle", "keys": {}},
        ]}
        return particles.build_particle_document(
            "testmap", entities, self.DEFINITIONS.get, {"spark"}.__contains__
        )

    def test_attachment_keys_and_definition_spelling(self):
        document = self.document()
        self.assertEqual(document["schema"], particles.MAP_PARTICLE_SCHEMA)
        # The keyless entity is skipped; the logic_relay is not an emitter.
        self.assertEqual([e["targetname"] for e in document["emitters"]],
                         ["attached", "pathspelled", "unresolvable"])
        attached = document["emitters"][0]
        self.assertEqual(attached["attach_type"], 2)
        self.assertEqual(attached["parentname"], "Sire2")
        self.assertEqual(attached["bone"], "Bip01 Neck")
        self.assertAlmostEqual(attached["bounds_cm"], 512 * 2.54)
        # `particles/good_emitter.txt` resolves to the same definition as `good_emitter`.
        self.assertEqual(document["emitters"][1]["particle_definition"], "good_emitter")

    def test_one_bad_definition_does_not_lose_the_others(self):
        document = self.document()
        self.assertIn("good_emitter", document["particles"]["definitions"])
        self.assertIn("good_fx", document["particles"]["definitions"])
        self.assertNotIn("broken_emitter", document["particles"]["definitions"])
        self.assertEqual([u["definition"] for u in document["unresolved"]], ["broken_emitter"])
        self.assertIn("sortfront", document["unresolved"][0]["reason"])
        # The emitter is still listed, so the map records what it wanted to play.
        self.assertEqual(document["emitters"][2]["particle_definition"], "broken_emitter")

    def test_a_map_with_no_entities_still_bakes_gameplay_event_roots(self):
        document = particles.build_particle_document(
            "empty", {"entities": []}, self.DEFINITIONS.get, {"spark"}.__contains__)
        self.assertEqual(document["emitters"], [])
        self.assertEqual(document["particles"]["roots"], ["force_feeding_emitter"])
        self.assertIn("force_feeding_fx1", document["particles"]["definitions"])

    def test_spawn_wrapper_accepts_zero_frames_and_inert_sortfront(self):
        wrapper, refs, sprites = particles.compile_definition(
            "muzzleflash_emitter_up",
            'Particle { frames 0 spawn { particle W_thirtyeight_flash-1 burst 1 '
            'z 12 depth_offset 1 } }',
        )
        child, child_refs, child_sprites = particles.compile_definition(
            "W_thirtyeight_flash-1",
            'Particle { frames 2 sprite flash sortfront 0 }',
        )
        self.assertEqual(wrapper["frames"], 0)
        self.assertEqual(refs, {"w_thirtyeight_flash-1"})
        self.assertEqual(sprites, set())
        self.assertEqual(wrapper["spawns"][0]["depth_offset"]["values"], [1.0])
        self.assertAlmostEqual(wrapper["spawns"][0]["offset_cm"]["z"]["values"][0], 30.48)
        self.assertFalse(child["sortfront"])
        self.assertEqual(child_refs, set())
        self.assertEqual(child_sprites, {"flash"})

        with self.assertRaisesRegex(particles.ParticleContractError, "sortfront"):
            particles.compile_definition(
                "sorted_fx", 'Particle { frames 2 sprite flash sortfront 1 }'
            )
        with self.assertRaisesRegex(particles.ParticleContractError, "frames"):
            particles.compile_definition("zero_sprite", 'Particle { frames 0 sprite flash }')


class HeightTextureTests(unittest.TestCase):
    def test_only_solid_non_sky_props_become_placed_cover(self):
        with tempfile.TemporaryDirectory() as directory:
            # The map directory holds placements; the mesh they name is the shared corpus's.
            root = Path(directory) / "map"
            root.mkdir()
            props = shared_corpus.props_dir(root.parent)
            props.mkdir(parents=True)
            (props / "awning.obj").write_text(
                "v 0 0 0\nv 10 0 0\nv 0 10 0\nf 1 2 3\n", encoding="utf-8"
            )
            (root / "map.props").write_text(
                "awning 100 200 300 0 0 0 1 1 0 0 models/awning.mdl\n"
                "awning 400 500 600 0 0 0 1 0 0 0 models/awning.mdl\n"
                "awning 700 800 900 0 0 0 1 1 0 1 models/awning.mdl\n",
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
