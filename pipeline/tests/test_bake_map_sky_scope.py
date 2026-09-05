"""R6.7 (`docs/architecture/seam_map_map.md` -> "3D-skybox composition (R6.7)"): every lane the
corpus places inside the 3D-skybox miniature goes through the one transform and carries the one
scope marker.

What can change shipped content without any other test noticing: a sky-flagged sprite row or
detail record placed at its raw miniature-space position (a lamp sixteen times too close, at
1/16 size), a miniature actor the runtime cannot tell from a world one (stamped with the world's
fog, left standing when the miniature is toggled off), and the transform's source -- the V2 lane
answers it from the staged manifest, never from the legacy `<map>.sky` sidecar.
"""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
from unittest import mock

import pytest

from elysium_pipeline.importers import map_geometry as MG

REPO = Path(__file__).resolve().parents[2]


def _fake_unreal():
    """The editor module surface `bake_lib` touches at import (`test_bake_map_lights`)."""
    editor = SimpleNamespace(
        does_directory_exist=lambda target: True,
        make_directory=lambda target: True,
        does_asset_exist=lambda target: False,
        load_asset=lambda target: None,
        list_assets=lambda package, recursive=True, include_folder=True: [],
    )
    return SimpleNamespace(
        AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
        MaterialEditingLibrary=object(),
        GeometryScript_Collision=object(),
        EditorAssetLibrary=editor,
        Paths=SimpleNamespace(project_dir=lambda: str(REPO)),
        SystemLibrary=SimpleNamespace(get_command_line=lambda: ""),
        LinearColor=lambda *values: values,
        log=lambda *a, **k: None,
        log_warning=lambda *a, **k: None,
        log_error=lambda *a, **k: None,
    )


def _load_bake_map_v2(export_root: str):
    spec = importlib.util.spec_from_file_location(
        "elysium_test_bake_map_v2_sky_scope", REPO / "pipeline/unreal/bake_map_v2.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"unreal": _fake_unreal()}), \
            mock.patch.dict(os.environ, {"ELYSIUM_EXPORT_ROOT": export_root}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        spec.loader.exec_module(module)
    return module


@pytest.fixture(scope="module")
def module():
    with tempfile.TemporaryDirectory() as out:
        yield _load_bake_map_v2(out)


#: One miniature: `sky_camera` at (1000, 2000, 3000) cm, scale 16 -- the manifest's `sky` block.
SKY_BLOCK = {"ok": True, "scale": 16.0, "origin": [1000.0, 2000.0, 3000.0]}
#: A point 10 / 20 / 30 cm past the camera in miniature space lands 160 / 320 / 480 cm out.
INSIDE = (1010.0, 2020.0, 3030.0)
PLACED = (160.0, 320.0, 480.0)


def _sprite_row(module, sky):
    return module._SpriteRow({
        "index": 309, "name": "lamp", "material": "sprites/glowa",
        "asset": "/ElysiumBaked/Materials/sprites/MI_glowa",
        "texture": "/ElysiumBaked/Textures/sprites/T_glowa", "width": 128, "height": 64,
        "position": list(INSIDE), "scale": 0.5, "mode": 3, "blend": "Translucent",
        "glow": True, "color": [255, 200, 100], "alpha": 180, "fx": 0, "upright": False,
        "hidden": False, "sky": sky,
    })


def test_a_sky_flagged_sprite_row_takes_the_miniature_transform_and_the_marker(module):
    scale, origin = module.miniature_transform(SKY_BLOCK)
    sky = module.sprite_actor_values(_sprite_row(module, True), scale, origin)
    assert sky["position"] == pytest.approx(PLACED)
    assert sky["scale"] == 16.0
    # The class tag and the entity tag the runtime buckets by, then the scope marker.
    assert sky["tags"] == ("elysium.sprite", "elysium.ent=309", "elysium.sky")
    assert sky["folder"] == "Sky/Sprites"
    # A world row is untouched by the miniature and carries no marker.
    world = module.sprite_actor_values(_sprite_row(module, False), scale, origin)
    assert world["position"] == INSIDE and world["scale"] == 1.0
    assert world["tags"] == ("elysium.sprite", "elysium.ent=309")


def test_a_sky_flagged_detail_record_takes_the_miniature_transform_and_the_marker(module):
    scale, origin = module.miniature_transform(SKY_BLOCK)
    records = [
        MG.DetailPlacement(index=0, model=0, stem="weed", model_path="models/weed.mdl",
                           position=INSIDE, rotation=(0.0, 0.0, 0.0, 1.0), sway=0, sky=False),
        MG.DetailPlacement(index=1, model=0, stem="weed", model_path="models/weed.mdl",
                           position=INSIDE, rotation=(0.0, 0.0, 0.0, 1.0), sway=0, sky=True),
    ]
    groups = module.detail_instance_rows(records, scale, origin)
    # The miniature record is its own component, at the transform and the miniature's scale.
    assert list(groups) == [("vtmb:model:weed", False), ("vtmb:model:weed", True)]
    position, _rotation, instance_scale, _sway = groups[("vtmb:model:weed", True)][0]
    assert position == pytest.approx(PLACED) and instance_scale == 16.0
    assert groups[("vtmb:model:weed", False)][0][0] == INSIDE and groups[("vtmb:model:weed", False)][0][2] == 1.0
    # The component actor's tags: class, model, then the scope marker for the sky group only.
    assert module.detail_actor_tags("weed", True) == (
        "elysium.detail", "elysium.model=weed", "elysium.sky")
    assert module.detail_actor_tags("weed", False) == ("elysium.detail", "elysium.model=weed")


def test_the_lane_places_through_the_manifests_own_sky_block(module):
    # The manifest's `sky` block is the whole answer -- the staged reader takes it as written and
    # the legacy `<map>.sky` sidecar is never consulted on the V2 lane.
    assert module.miniature_transform(SKY_BLOCK) == (16.0, (1000.0, 2000.0, 3000.0))
    # Source's default on a map with no `sky_camera`: scale 16 about the world origin, and the
    # producer flags nothing sky, so the numbers place nothing.
    assert module.miniature_transform({"ok": False, "scale": 16.0, "origin": [0, 0, 0]}) == (
        16.0, (0.0, 0.0, 0.0))
    # The marker is `bake_map.TAG_SKY` / `ElysiumBakedTags::Sky` restated, and the two shape
    # terms exist so a level baked before the marker re-authors.
    assert module.TAG_SKY == "elysium.sky"
    assert module.DETAIL_ACTOR_SHAPE == 3 and module.SPRITE_ACTOR_SHAPE == 3
