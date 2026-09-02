"""R7.3 (`docs/architecture/seam_map_map.md` -> "Import -- effects (R7.3)"): the map stage's
particle tree, at the seam the legacy compiler failed.

One row, the one the note authorizes: `fire2_emitter` -- the most-placed root (204 rows), whose
file ships with unbalanced braces and which `formats/particles.py::compile_definition` rejects --
stages to a tree whose two live leaves (`Flames2`, `FlameGlow2`; the `Smoke1` / `FlameEmbers1`
blocks sit in the unparsed region the lexer cannot structure) carry their lifetimes in seconds at
`fps` 30, and `barrelfireemitter` (115 rows, unresolved on every map before) resolves with every
child present. The particle units are the real export corpus (skip, loudly, when absent); the
texture lane's sidecar is a fake reader, because this seam is the tree, not the texture import.
"""

from __future__ import annotations

import pytest

from elysium_pipeline import paths
from elysium_pipeline.importers import effects as E


def _flat(ramp):
    return [value for key in ramp for value in key]


def _texture(key):
    stem = key.rsplit("/", 1)[-1]
    return {"assetPath": f"/ElysiumBaked/Textures/particles/T_{stem}", "width": 64, "height": 32}


def test_fire2_emitter_stages_to_its_two_live_leaves_and_barrelfireemitter_resolves():
    try:
        root = paths.export_v2_root()
    except RuntimeError:
        pytest.skip("no ELYSIUM_WORK_ROOT on this machine")
    if not (root / "particles" / "fire2_emitter.glb").is_file():
        pytest.skip(f"no exported particle units under {root / 'particles'}")

    builder = E.TreeBuilder(E.particle_unit_reader(root), _texture)
    tree = builder.build("fire2_emitter", "Fire2_Emitter")

    nodes = tree["nodes"]
    root_node = nodes[0]
    assert tree["root"] == "vtmb:particle:fire2_emitter" and tree["name"] == "Fire2_Emitter"
    assert root_node["kind"] == "root" and root_node["resolved"] is True
    # `fps 30`, no `frames`: the root's own clock is the one-second default.
    assert root_node["fps"] == 30.0 and root_node["lifetime_s"] == pytest.approx(1.0)
    assert root_node["loop"] is True and root_node["draws"] is False and root_node["spawns"]

    # The two live leaves, in block order, each reached through its own spawn block.
    assert [n["name"] for n in nodes[1:]] == ["Flames2", "FlameGlow2"]
    assert [n["kind"] for n in nodes[1:]] == ["leaf", "leaf"]
    assert all(n["parent"] == 0 and n["via"] == "spawn" and n["resolved"] for n in nodes[1:])
    assert [n["blockIndex"] for n in nodes[1:]] == [0, 1]
    assert tree["stats"] == {"leafCount": 2, "depth": 1, "maxKeyframes": 15}

    flames, glow = nodes[1], nodes[2]
    # Flames2: `frames 10`, `max_frames 15` at the default fps -> 1/3 s, min 1/3 s, max 1/2 s.
    assert flames["lifetime_s"] == pytest.approx(10 / 30)
    assert flames["lifetime_min_s"] == pytest.approx(10 / 30)
    assert flames["lifetime_max_s"] == pytest.approx(15 / 30)
    # FlameGlow2: `frames 60` -> 2 s.
    assert glow["lifetime_s"] == pytest.approx(2.0)
    # The spawn block, in the product's units: `rate 18`, `theta 0~360`, `phi 180~-180`, `z 0`.
    assert flames["spawn"]["rate"] == [[0.0, 18.0, 18.0]]
    assert flames["spawn"]["theta_deg"] == [[0.0, 0.0, 360.0]]
    assert flames["spawn"]["phi_deg"] == [[0.0, 180.0, -180.0]]
    assert flames["spawn"]["timescale"] == 1.0 and flames["spawn"]["distance"] is False
    # The leaf's own ramps: `size "10,8,8,4,2,2"` in cm, evenly spaced; `radius_speed "25,0,0"`
    # in cm/s; `mask "0"` folded to 0..1; `depth_offset -2` in cm; the sprite off the texture lane.
    assert _flat(flames["size_cm"]) == pytest.approx(_flat(
        [[0.0, 25.4, 25.4], [0.2, 20.32, 20.32], [0.4, 20.32, 20.32], [0.6, 10.16, 10.16],
         [0.8, 5.08, 5.08], [1.0, 5.08, 5.08]]))
    assert _flat(flames["radius_speed_cm_s"]) == pytest.approx(_flat(
        [[0.0, 63.5, 63.5], [0.5, 0.0, 0.0], [1.0, 0.0, 0.0]]))
    assert _flat(flames["green"]) == pytest.approx(_flat(
        [[0.0, 180 / 255, 1.0], [1.0, 100 / 255, 100 / 255]]))
    assert flames["mask"] == [[0.0, 0.0, 0.0]]
    assert flames["depth_offset_cm"] == pytest.approx(-2 * 2.54)
    assert flames["sprite"] == {
        "id": "vtmb:image:particles/flamemass.tga",
        "texture": "/ElysiumBaked/Textures/particles/T_flamemass",
        "size_px": [64, 32], "aspect": [0.5, 0.25]}
    assert flames["collide"] is None and flames["movealign"] is False
    assert builder.unresolved_children == [] and builder.missing_textures == []

    barrel = E.TreeBuilder(E.particle_unit_reader(root), _texture).build(
        "barrelfireemitter", "BarrelFireEmitter")
    assert barrel["nodes"][0]["resolved"] is True
    assert all(n["resolved"] for n in barrel["nodes"])
    assert barrel["stats"]["leafCount"] >= 1
    assert any(n["draws"] and n["sprite"] for n in barrel["nodes"])
