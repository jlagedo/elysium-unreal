import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

import pytest

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
        assert vmt.parse(wet_vmt(comments=True))["globalwetness"] == 0.6

    def test_absent_proxy_is_not_wetness_driven(self):
        assert vmt.parse('LightmappedGeneric { "$basetexture" "x" }')["globalwetness"] is None

    def test_partial_malformed_and_unequal_triples_fail(self):
        for document in (
            wet_vmt((0.6, 0.6)),
            wet_vmt((0.6, "nope", 0.6)),
            wet_vmt((0.6, 0.5, 0.6)),
        ):
            with self.subTest(document=document):
                with pytest.raises(vmt.VmtContractError):
                    vmt.parse(document)

    def test_patch_inherits_parent_proxy(self):
        result = vmt.parse(
            'Patch { "include" "materials/base.vmt" }',
            resolve_include=lambda _path: wet_vmt((0.25, 0.25, 0.25)),
        )
        assert result["globalwetness"] == 0.25


def test_six_faces_keep_vtf_order_and_bgra_bytes():
    colours = [
        (1, 2, 3, 4), (11, 12, 13, 14), (21, 22, 23, 24),
        (31, 32, 33, 34), (41, 42, 43, 44), (51, 52, 53, 54),
    ]
    data = tex_to_png.cubemap_dds([
        Image.new("RGBA", (2, 2), colour) for colour in colours
    ])
    assert data[:4] == b"DDS "
    assert int.from_bytes(data[12:16], "little") == 2
    assert int.from_bytes(data[16:20], "little") == 2
    assert int.from_bytes(data[112:116], "little") == 0xFE00
    pixels = data[128:]
    face_bytes = 2 * 2 * 4
    for index, (r, g, b, a) in enumerate(colours):
        assert pixels[index * face_bytes:index * face_bytes + 4] == bytes((b, g, r, a))


def test_missing_unequal_and_non_square_faces_fail():
    with pytest.raises(ValueError):
        tex_to_png.cubemap_dds([Image.new("RGBA", (2, 2))] * 5)
    with pytest.raises(ValueError):
        tex_to_png.cubemap_dds(
            [Image.new("RGBA", (2, 2))] * 5 + [Image.new("RGBA", (4, 4))]
        )
    with pytest.raises(ValueError):
        tex_to_png.cubemap_dds([Image.new("RGBA", (2, 3))] * 6)


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


def compile(*, sprites=None):
    available = sprites or {"dropletfast", "fortituderings", "d_targetblob"}
    return particles.compile_closure(
        ["rain_follow_emitter"], DEFINITIONS.get, available.__contains__
    )


def test_comment_handling_dependency_collision_and_unit_conversion():
    result = compile()
    assert list(result["definitions"]) == ["rain_follow_emitter", "raindrops2", "rainsplash_new", "rainstain"]
    rain = result["definitions"]["raindrops2"]
    assert rain["velocity_cm_per_second"]["x"]["values"][0] == pytest.approx(50.8, abs=1e-7)
    assert rain["velocity_cm_per_second"]["y"]["values"][0] == pytest.approx(-50.8, abs=1e-7)
    assert rain["collision"]["decal"]["particle"] == "rainstain"
    assert "disabled" not in result["definitions"]


def test_missing_asset_and_unsupported_live_field_fail():
    with pytest.raises(particles.ParticleContractError):
        compile(sprites={"dropletfast", "fortituderings"})
    changed = dict(DEFINITIONS)
    changed["raindrops2"] = changed["raindrops2"].replace(
        "sprite dropletfast", "sprite dropletfast unknown_live_field 1"
    )
    with pytest.raises(particles.ParticleContractError):
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
        assert spawns[0]["burst"]["values"][0] == pytest.approx(40.0, abs=1e-7)
        offset = spawns[0]["offset_cm"]
        assert offset["x"]["values"][0] == pytest.approx(5.08, abs=1e-7)
        # `y` takes the same Source reflection the velocities do.
        assert offset["y"]["values"][0] == pytest.approx(-7.62, abs=1e-7)
        assert offset["z"]["values"][0] == pytest.approx(-2.54, abs=1e-7)
        assert "offset_cm" not in spawns[1]
        assert spawns[1]["rate"]["values"][0] == pytest.approx(30.0, abs=1e-7)

    def test_spherical_speeds_convert_and_parent_speed_stays_dimensionless(self):
        fx = self.compile()["definitions"]["impact_fx"]
        radial = fx["radial_velocity_cm_per_second"]
        assert radial["radius"]["values"][0] == pytest.approx(-101.6, abs=1e-7)
        assert radial["elevation"]["values"][1] == pytest.approx(-381.0, abs=1e-7)
        assert radial["radius"]["kind"] == "range"
        # A fraction of the parent's velocity, not a length.
        assert fx["parent_speed"]["values"][0] == pytest.approx(1.0, abs=1e-7)
        # Unresolved unit, so carried verbatim.
        assert fx["depth_offset"]["values"][0] == pytest.approx(10.0, abs=1e-7)

    def test_rgb_channels_and_width_are_carried(self):
        fx = self.compile()["definitions"]["impact_fx"]
        assert fx["red"]["values"] == [255.0, 150.0]
        assert fx["green"]["values"] == [0.0]
        assert fx["blue"]["values"] == [0.0]
        assert fx["width_cm"]["values"][0] == pytest.approx(5.08, abs=1e-7)


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
        assert document["schema"] == particles.MAP_PARTICLE_SCHEMA
        # The keyless entity is skipped; the logic_relay is not an emitter.
        assert [e["targetname"] for e in document["emitters"]] == ["attached", "pathspelled", "unresolvable"]
        attached = document["emitters"][0]
        assert attached["attach_type"] == 2
        assert attached["parentname"] == "Sire2"
        assert attached["bone"] == "Bip01 Neck"
        assert attached["bounds_cm"] == pytest.approx(512 * 2.54, abs=1e-7)
        # `particles/good_emitter.txt` resolves to the same definition as `good_emitter`.
        assert document["emitters"][1]["particle_definition"] == "good_emitter"

    def test_one_bad_definition_does_not_lose_the_others(self):
        document = self.document()
        assert "good_emitter" in document["particles"]["definitions"]
        assert "good_fx" in document["particles"]["definitions"]
        assert "broken_emitter" not in document["particles"]["definitions"]
        assert [u["definition"] for u in document["unresolved"]] == ["broken_emitter"]
        assert "sortfront" in document["unresolved"][0]["reason"]
        # The emitter is still listed, so the map records what it wanted to play.
        assert document["emitters"][2]["particle_definition"] == "broken_emitter"

    def test_a_map_with_no_entities_still_bakes_gameplay_event_roots(self):
        document = particles.build_particle_document(
            "empty", {"entities": []}, self.DEFINITIONS.get, {"spark"}.__contains__)
        assert document["emitters"] == []
        assert document["particles"]["roots"] == ["force_feeding_emitter"]
        assert "force_feeding_fx1" in document["particles"]["definitions"]

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
        assert wrapper["frames"] == 0
        assert refs == {"w_thirtyeight_flash-1"}
        assert sprites == set()
        assert wrapper["spawns"][0]["depth_offset"]["values"] == [1.0]
        assert wrapper["spawns"][0]["offset_cm"]["z"]["values"][0] == pytest.approx(30.48, abs=1e-7)
        assert not child["sortfront"]
        assert child_refs == set()
        assert child_sprites == {"flash"}

        with pytest.raises(particles.ParticleContractError, match="sortfront"):
            particles.compile_definition(
                "sorted_fx", 'Particle { frames 2 sprite flash sortfront 1 }'
            )
        with pytest.raises(particles.ParticleContractError, match="frames"):
            particles.compile_definition("zero_sprite", 'Particle { frames 0 sprite flash }')


def test_only_solid_non_sky_props_become_placed_cover():
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
        assert len(triangles) == 1
        assert triangles[0][0] == (100.0, 200.0, 300.0)
        assert triangles[0][2] == (100.0, 210.0, 300.0)


def test_r16_sentinel_bounds_resolution_and_decode():
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
        assert image.shape == (2048, 2048)
        assert int(image[-1, -1]) == 0
        sample = int(image[1, 1])
        assert sample > 0
        decoded = metadata["min_z_cm"] + (sample - 1) * metadata["z_scale_cm"]
        assert decoded == pytest.approx(50.0, abs=metadata["z_scale_cm"])
        assert metadata["format"] == "R16_UNORM"
