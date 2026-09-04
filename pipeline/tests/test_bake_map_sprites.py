"""R6.1 (`docs/architecture/seam_map_map.md` -> "Sprites (R6.1)"): the V2 map bake places one
billboard actor per `env_sprite` off the staged `sprites` table.

What can change shipped content without any other test noticing: the **row** the offline stage
derives from an entity block (`CSprite::Spawn`'s scale clamp and hidden rule, the mode -> blend
table, the VMT's orientation, the texture size), the **placement** the editor half writes from it
(the sky transform, the child name, the tags the runtime buckets by), and the manifest version
the two halves share. The corpus case counts the three maps' rows against the entity units.
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

from elysium_pipeline import map_transport, shared_corpus
from elysium_pipeline.formats.bsp import source_to_unreal
from elysium_pipeline.importers import map_geometry as MG

REPO = Path(__file__).resolve().parents[2]
WORKING_MAPS = ("sp_tutorial_1", "sm_pawnshop_1", "sm_hub_1")


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
        "elysium_test_bake_map_v2_sprites", REPO / "pipeline/unreal/bake_map_v2.py")
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


class _FakeSky:
    def __init__(self, threshold_x):
        self.threshold_x = threshold_x

    def is_sky(self, source_point):
        return source_point[0] > self.threshold_x


def _block(*pairs):
    return list(pairs)


GLOWA = _block(("classname", "env_sprite"), ("model", "materials/sprites/glowa.vmt"),
               ("origin", "128 -64 32"), ("rendermode", "3"), ("scale", "0.5"),
               ("rendercolor", "255 200 100"), ("renderamt", "180"))

WORLDSPAWN = _block(("classname", "worldspawn"))


def test_sprite_records_read_every_env_sprite_block_by_lump_ordinal():
    blocks = [
        WORLDSPAWN,                                                       # 0
        GLOWA,                                                            # 1: unnamed, on
        _block(("classname", "light"), ("origin", "0 0 0")),              # 2
        _block(("classname", "env_sprite"), ("targetname", "lamp"),       # 3: named, no Start On
               ("model", "materials/sprites/glowb.vmt"), ("origin", "0 0 0"),
               ("rendermode", "5"), ("scale", "12"), ("spawnflags", "2")),
        _block(("classname", "env_sprite"), ("targetname", "cop"),        # 4: named + Start On
               ("model", "materials/sprites/coplights.vmt"), ("origin", "9000 0 0"),
               ("rendermode", "1"), ("scale", "0"), ("spawnflags", "1"), ("renderfx", "14")),
        _block(("classname", "env_sprite"), ("targetname", "born_dark"),  # 5: start_hidden wins
               ("model", "materials/sprites/glowa.vmt"), ("origin", "0 0 0"),
               ("spawnflags", "1"), ("StartHidden", "1"), ("rendercolor", "300 -5 x")),
    ]
    records = MG._sprite_records(blocks, _FakeSky(5000.0))
    assert [r.index for r in records] == [1, 3, 4, 5]
    corona, lamp, cop, dark = records

    # The material key is the install key; the origin takes the R5.1 frame.
    assert corona.material == "sprites/glowa"
    assert corona.position == pytest.approx(source_to_unreal(128.0, -64.0, 32.0), abs=1e-6)
    assert corona.mode == 3 and corona.scale == 0.5
    assert corona.color == (255, 200, 100) and corona.alpha == 180
    assert corona.fx == 0
    # CSprite::Spawn: unnamed -> on; a named sprite without spawnflag 1 -> off; the flag -> on;
    # start_hidden wins over the flag.
    assert corona.hidden is False
    assert lamp.hidden is True
    assert cop.hidden is False
    assert dark.hidden is True
    # The scale clamps to 0..8 and an authored 0 reads as 1, `CSprite::Spawn`'s own reading.
    assert lamp.scale == 8.0
    assert cop.scale == 1.0
    assert cop.fx == 14 and cop.mode == 1
    # Colour defaults to white and clamps; the sky rule is the placements' own.
    assert dark.color == (255, 0, 0) and dark.alpha == 255 and dark.mode == 0
    assert [r.sky for r in records] == [False, False, True, False]


def _material_sidecar(orientation=None, texture="sprites/glowa"):
    parameters = [{"key": "$basetexture", "value": texture}]
    if orientation is not None:
        parameters.insert(0, {"key": "$spriteorientation", "value": orientation})
    return {
        "master": "/Game/ElysiumGenerated/Materials/V2/M_V2_Sprite",
        "parameters": parameters,
        "textureBindings": [{"asset": "vtmb:texture:" + texture, "parameter": "BaseTexture"}],
    }


def _texture_sidecar(width, height, key="sprites/glowa"):
    return {"assetPath": "/ElysiumBaked/Textures/%s/T_%s" % tuple(key.rsplit("/", 1)),
            "width": width, "height": height}


def test_resolve_sprite_table_joins_the_material_and_texture_sidecars():
    records = MG._sprite_records([WORLDSPAWN, GLOWA], _FakeSky(5000.0))
    materials = {"sprites/glowa": _material_sidecar("vp_parallel")}
    textures = {"sprites/glowa": _texture_sidecar(128, 64)}
    rows = MG.resolve_sprite_table(records, materials.get, textures.get, map_name="fake")
    assert len(rows) == 1
    row = rows[0]
    assert row["index"] == 1
    assert row["asset"] == "/ElysiumBaked/Materials/sprites/MI_glowa"
    assert row["texture"] == "/ElysiumBaked/Textures/sprites/T_glowa"
    assert (row["width"], row["height"]) == (128, 64)
    assert row["blend"] == "Additive" and row["glow"] is True
    assert row["upright"] is False
    assert row["color"] == [255, 200, 100] and row["alpha"] == 180
    assert row["position"] == pytest.approx(list(source_to_unreal(128.0, -64.0, 32.0)), abs=1e-6)

    # `parallel_upright` is the one orientation that constrains the billboard; the mode table.
    upright = MG.sprite_row(records[0], _material_sidecar("parallel_upright"),
                            _texture_sidecar(32, 32), "/ElysiumBaked/Materials/sprites/MI_glowa")
    assert upright["upright"] is True
    # The blend per mode is the Sprite shader's own per-`$spriterendermode` blend state
    # (`stdshader_dx8.dll` `1000eca0`), not the mode's Source name: the two glow modes are
    # `SRC_ALPHA, ONE` (additive) and 8 is the premultiplied `ONE, INV_SRC_ALPHA`.
    for mode, blend, glow in ((0, "Opaque", False), (1, "Translucent", False),
                              (3, "Additive", True), (5, "Additive", False),
                              (8, "AlphaComposite", False), (9, "Additive", True)):
        record = MG.SpriteRecord(index=7, name="", material="sprites/x", position=(0.0, 0.0, 0.0),
                                 scale=1.0, mode=mode, color=(255, 255, 255), alpha=255, fx=0,
                                 hidden=False, sky=False)
        row = MG.sprite_row(record, _material_sidecar(), _texture_sidecar(8, 8), "/x/MI_x")
        assert (row["blend"], row["glow"]) == (blend, glow)
    # Mode 6 names no sprite program: a loud failure, never a grey card.
    six = MG.SpriteRecord(index=7, name="", material="sprites/x", position=(0.0, 0.0, 0.0),
                          scale=1.0, mode=6, color=(255, 255, 255), alpha=255, fx=0,
                          hidden=False, sky=False)
    with pytest.raises(MG.MapGeometryError):
        MG.sprite_row(six, _material_sidecar(), _texture_sidecar(8, 8), "/x/MI_x")
    # A material or texture the lanes have not staged fails the map with the key named.
    with pytest.raises(MG.MapGeometryError, match="sprites/glowa"):
        MG.resolve_sprite_table(records, {}.get, textures.get, map_name="fake")
    with pytest.raises(MG.MapGeometryError, match="not staged by the texture lane"):
        MG.resolve_sprite_table(records, materials.get, {}.get, map_name="fake")


def _staged_row(module, **overrides):
    row = {
        "index": 309, "name": "lamp", "material": "sprites/glowa",
        "asset": "/ElysiumBaked/Materials/sprites/MI_glowa",
        "texture": "/ElysiumBaked/Textures/sprites/T_glowa", "width": 128, "height": 64,
        "position": [1010.0, 2020.0, 3030.0], "scale": 0.5, "mode": 3, "blend": "Additive",
        "glow": True, "color": [255, 200, 100], "alpha": 180, "fx": 0, "upright": False,
        "hidden": True, "sky": False,
    }
    row.update(overrides)
    return module._SpriteRow(row)


def test_sprite_actor_values_size_tags_and_the_sky_transform(module):
    row = _staged_row(module)
    values = module.sprite_actor_values(row)
    # `scale x texture` in Source units; the proxy turns it into cm (or the glow rule).
    assert values["size_inches"] == (64.0, 32.0)
    assert values["position"] == (1010.0, 2020.0, 3030.0) and values["scale"] == 1.0
    assert values["label"] == "Sprite_309_glowa" and values["folder"] == "Sprites"
    assert values["tags"] == ("elysium.sprite", "elysium.ent=309")
    # A miniature sprite takes the sky transform a prop takes and the miniature's scale.
    sky = module.sprite_actor_values(
        _staged_row(module, sky=True), sky_scale=16.0, sky_origin=(1000.0, 2000.0, 3000.0))
    assert sky["position"] == pytest.approx((160.0, 320.0, 480.0))
    assert sky["scale"] == 16.0
    assert sky["label"] == "Sprite_309_glowa_sky" and sky["folder"] == "Sky/Sprites"
    # The child is named per (imported MI_, blend), under the shared sprites package.
    assert module.sprite_child_name(row.asset, row.blend) == "MI_Sprite_sprites_glowa_Additive"
    assert module.V2_SPRITE_MATERIAL_PACKAGE == "/ElysiumBaked/Sprites"
    # The row round-trips into the level recipe.
    assert row.as_dict()["index"] == 309 and row.as_dict()["hidden"] is True


def test_the_two_halves_share_the_manifest_version(module):
    # The invariant is that the two halves agree, not the number they agree on: the
    # constant is restated across the numpy boundary and bumps whenever a table is added
    # (R7.2 took it to 8), and a literal here only teaches the next bump to edit it here too.
    assert module.MANIFEST_VERSION == MG.MANIFEST_VERSION


@pytest.mark.parametrize("map_name", WORKING_MAPS)
def test_every_env_sprite_of_the_working_corpus_is_a_row(map_name):
    unit = MG.sidecars.unit_paths(map_name)["root"]
    if not unit.is_file():
        pytest.skip(f"no exported map root unit at {unit}")
    if not map_transport.is_map_on_v2_models(map_name):
        pytest.skip(f"{map_name} is not on MapsOnV2Models")
    geometry = MG.read_geometry(map_name)
    join = MG.sidecars.prepare_join(map_name)
    blocks = join.pair_blocks
    sprites = [(index, dict((k.lower(), v) for k, v in pairs)) for index, pairs in enumerate(blocks)
               if dict((k.lower(), v) for k, v in pairs).get("classname", "").lower() == "env_sprite"]
    assert [record.index for record in geometry.sprites] == [index for index, _ in sprites]
    assert geometry.counts["sprites"] == len(sprites)
    for record, (_, keys) in zip(geometry.sprites, sprites):
        assert record.material == shared_corpus.material_key(keys.get("model", ""))
        assert 0.0 < record.scale <= MG.SPRITE_MAX_SCALE
        assert record.mode in MG.SPRITE_BLEND_BY_MODE
    # The hidden rule is the spawn rule, row by row, against the block's own keys.
    for record, (_, keys) in zip(geometry.sprites, sprites):
        start_on = int(MG.sidecars.atof(keys.get("spawnflags", "0"))) & MG.SF_SPRITE_START_ON
        assert record.hidden == (keys.get("starthidden", "0") == "1"
                                 or (bool(keys.get("targetname", "")) and not start_on))
