"""`pipeline/unreal/bake_verify.py`: the two lanes its material checks answer for.

A converted map (`MapsOnV2Models`) and a legacy one bind different assets for the same surface,
and the checks that could not tell them apart were reporting about packages nobody draws -- the
per-map `<map>/Materials/MI_...` the V2 bake prunes rather than writes, and the legacy PNG/shared
corpus pair a converted map never binds. What is pinned here is the join each lane makes: the
legacy slot for a patched surface, the V2 slot the staged manifests name for that same surface,
and the alpha set the V2 check derives from them.

The module is an editor entry point, so it is loaded with a fake `unreal` -- and it can be loaded
at all because `main()` refuses an empty map selection instead of defaulting to one map.
"""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import sys
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]


def _fake_unreal():
    """The editor module surface `bake_lib` touches at import (`test_bake_map_sprites`)."""
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


@pytest.fixture(scope="module")
def module():
    spec = importlib.util.spec_from_file_location(
        "elysium_test_bake_verify", REPO / "pipeline/unreal/bake_verify.py")
    assert spec and spec.loader
    loaded = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"unreal": _fake_unreal()}), \
            mock.patch.dict(os.environ, {"ELYSIUM_WORK_ROOT": str(REPO)}):
        sys.modules.pop("pipeline.unreal.bake_lib", None)
        with pytest.raises(SystemExit):
            spec.loader.exec_module(loaded)   # main() refuses: no -BakeMaps= on the command line
    return loaded


def _surface(name, material_key, *, local=False):
    """The fields `_material_slot` reads off a `bake_lib` MatDef."""
    return SimpleNamespace(name=name, material_key=material_key, decal=False,
                           wetness_driven=False, local=local)


#: One `$envmap` surface as the two lanes record it. The slot carries the cubemap the map's own
#: VBSP patched in (`shared_corpus.CUBEMAP_TAG`); the unit underneath it is corpus-wide.
PATCHED_SLOT = "glass/glass01@c-1868_-2601_184"
PATCHED_ASSET = "/ElysiumBaked/Materials/maps/sm_pawnshop_1/glass/MI_glass01"
CORPUS_ASSET = "/ElysiumBaked/Materials/glass/MI_glass01"

STAGED = {
    PATCHED_ASSET: {
        "assetPath": PATCHED_ASSET,
        "unit": "vtmb:material:maps/sm_pawnshop_1/glass/glass01",
        # A patched unit states only what the map changed: its parent is the corpus instance, and
        # it names neither a blend mode nor a texture of its own.
        "parent": CORPUS_ASSET,
        "basePropertyOverrides": {},
        "textures": {},
    },
    CORPUS_ASSET: {
        "assetPath": CORPUS_ASSET,
        "unit": "vtmb:material:glass/glass01",
        "parent": "/Game/ElysiumGenerated/Materials/V2/M_V2_LitTranslucent",
        "basePropertyOverrides": {"blendMode": "Translucent"},
        "textures": {"BaseTexture": "/ElysiumBaked/Textures/glass/T_glass01"},
    },
    "/ElysiumBaked/Materials/brick/MI_wall": {
        "assetPath": "/ElysiumBaked/Materials/brick/MI_wall",
        "unit": "vtmb:material:brick/wall",
        "parent": "/Game/ElysiumGenerated/Materials/V2/M_V2_Lit",
        "basePropertyOverrides": {"blendMode": "Opaque"},
        "textures": {"BaseTexture": "/ElysiumBaked/Textures/brick/T_wall"},
    },
    "/ElysiumBaked/Materials/foliage/MI_fern": {
        "assetPath": "/ElysiumBaked/Materials/foliage/MI_fern",
        "unit": "vtmb:material:foliage/fern",
        "parent": "/Game/ElysiumGenerated/Materials/V2/M_V2_Lit",
        "basePropertyOverrides": {"blendMode": "Masked", "opacityMaskClipValue": 0.5},
        "textures": {"BaseTexture": "/ElysiumBaked/Textures/foliage/T_fern"},
    },
}

#: The map's own staged manifest: keyed by surface slot, naming the instance the bake bound. Two
#: slots on the same corpus unit, one of them the patched one.
MAP_MATERIALS = {
    PATCHED_SLOT: {"asset": PATCHED_ASSET, "master": "M_V2_LitTranslucent",
                   "blendMode": "Translucent"},
    "glass/glass01": {"asset": CORPUS_ASSET, "master": "M_V2_LitTranslucent",
                      "blendMode": "Translucent"},
    "brick/wall": {"asset": "/ElysiumBaked/Materials/brick/MI_wall", "master": "M_V2_Lit",
                   "blendMode": "Opaque"},
}


def _indexes(module):
    return STAGED, {module._unit_key(row): row for row in STAGED.values()}


def test_the_legacy_slot_is_still_this_map_s_own_package_for_a_patched_surface(module):
    # Unchanged, and pinned because the V2 branch is now the one that moved: a legacy map DOES
    # author `<map>/Materials/MI_<slot>` for a surface VBSP patched a cubemap into, and every
    # other surface reads the legacy shared corpus under its material key.
    package = "/ElysiumBaked/sp_theatre"
    assert module._material_slot(package, _surface(PATCHED_SLOT, "glass/glass01")) == (
        "/ElysiumBaked/sp_theatre/Materials", "MI_glass_glass01_c_1868__2601_184")
    assert module._material_slot(package, _surface("glass/glass01", "glass/glass01")) == (
        "/ElysiumBaked/Shared/Materials", "MI_glass_glass01")


def test_the_v2_slot_is_the_staged_corpus_instance_a_patched_surface_overrides(module):
    by_asset, by_unit = _indexes(module)
    units, missing = module.v2_bound_units(
        MAP_MATERIALS, {"foliage/fern": "fern.mtl/foliage/fern"}, by_asset, by_unit)
    assert missing == []
    # Both glass slots -- the patched one and the plain one -- answer with the ONE corpus
    # instance, the asset the V2 bake actually bound; the per-map package the legacy slot names
    # is never reached, because that lane prunes it rather than authoring it.
    assert sorted(units) == ["brick/wall", "foliage/fern", "glass/glass01"]
    row, owner = units["glass/glass01"]
    assert row["assetPath"] == CORPUS_ASSET
    assert row["parent"] == "/Game/ElysiumGenerated/Materials/V2/M_V2_LitTranslucent"
    # One report per unit, owned by the first slot that named it in sorted order -- stable, so a
    # unit that fails is reported under the same surface every run.
    assert owner == "surface glass/glass01"
    assert units["foliage/fern"][1] == "fern.mtl/foliage/fern"
    # A surface whose instance the material lane never staged is named, not dropped.
    _, unstaged = module.v2_bound_units(
        {"glass/gone": {"asset": "/ElysiumBaked/Materials/glass/MI_gone"}}, {"no/unit": "p.mtl/no"},
        by_asset, by_unit)
    assert unstaged == ["surface glass/gone", "p.mtl/no"]


def test_the_v2_alpha_set_is_the_two_blend_modes_that_read_a_texel_alpha(module):
    by_asset, by_unit = _indexes(module)
    units, _ = module.v2_bound_units(
        MAP_MATERIALS, {"foliage/fern": "fern.mtl/foliage/fern"}, by_asset, by_unit)
    rows = module.v2_alpha_units(units)
    # `$translucent` -> Translucent and `$alphatest` -> Masked read the base texture's alpha, so
    # the baked texture must have one; the opaque wall is not asked about. The texture checked is
    # the corpus instance's -- the patched override carries none of its own.
    assert [(key, blend, base) for key, _owner, blend, base in rows] == [
        ("foliage/fern", "Masked", "/ElysiumBaked/Textures/foliage/T_fern"),
        ("glass/glass01", "Translucent", "/ElysiumBaked/Textures/glass/T_glass01"),
    ]
    assert module.V2_ALPHA_BLEND_MODES == ("Translucent", "Masked")


def test_the_alpha_answer_comes_from_the_staged_payload_not_the_engine_tag(module, tmp_path):
    """`HasAlphaChannel` cannot answer inside a commandlet -- it is derived from built platform
    data (never built there), and the registry overwrites its own cached disk row from the loaded
    object as it ticks, so the same batch reports different findings per map position. The lane's
    own documents answer the same question and answer it identically on every run: the material
    provenance names the texture unit its `BaseTexture` resolved to, and that unit's staged DDS is
    the payload Unreal imported."""
    import struct

    def _dds(alpha):
        header = bytearray(124)
        struct.pack_into("<IIII", header, 0, 124, 0x1007, 1, len(alpha))
        struct.pack_into("<II4s", header, 72, 32, 0x4, b"DX10")
        body = b"DDS " + bytes(header) + struct.pack("<IIIII", 87, 3, 0, 1, 0)
        return body + b"".join(bytes([1, 2, 3, a]) for a in alpha)

    materials = tmp_path / "import" / "materials"
    textures = tmp_path / "import" / "textures" / "wood"
    (materials / "wood").mkdir(parents=True)
    textures.mkdir(parents=True)
    (materials / "wood" / "fencea.provenance.json").write_text(json.dumps({
        "textureBindings": [{"parameter": "BaseTexture", "asset": "vtmb:texture:wood/fencea"}],
    }), encoding="utf-8")
    (textures / "fencea.dds").write_bytes(_dds([255, 0, 255]))
    (textures / "planks.dds").write_bytes(_dds([255, 255]))

    with mock.patch.object(module, "work_root", lambda: tmp_path):
        assert module._authored_alpha_minimum({"provenance": "wood/fencea.provenance.json"}) == 0
        # An opaque payload is the corpus's own fact -- 255, and the caller records rather than
        # fails it. A row with no provenance, or a binding the sidecar does not carry, is `None`:
        # the check cannot answer, and says so instead of guessing either way.
        (materials / "wood" / "planks.provenance.json").write_text(json.dumps({
            "textureBindings": [{"parameter": "BaseTexture", "asset": "vtmb:texture:wood/planks"}],
        }), encoding="utf-8")
        assert module._authored_alpha_minimum({"provenance": "wood/planks.provenance.json"}) == 255
        assert module._authored_alpha_minimum({}) is None
        (materials / "wood" / "bare.provenance.json").write_text(
            json.dumps({"textureBindings": []}), encoding="utf-8")
        assert module._authored_alpha_minimum({"provenance": "wood/bare.provenance.json"}) is None


def test_the_unit_index_answers_with_the_surface_row_not_the_decal_projector(module, tmp_path):
    """R7.2 stages two rows under ONE unit id for a `$decal` material -- the surface `MI_<stem>`
    a model binds and the `MI_<stem>_Decal` projector a `UDecalComponent` lays -- and the second
    is written after the first. A last-wins unit index hands every prop-material join the
    projector: it is parented to `M_V2_Decal` and Translucent by construction, so the unit joins
    the alpha set on the projector's say-so and the glass/refract parent assertion checks the
    projector against its own recorded parent instead of checking the instance the model binds.
    Measured on this install: 588 units carry both rows, 126 of them disagree on blend mode."""
    surface = {
        "assetPath": "/ElysiumBaked/Materials/art/MI_librarysign1",
        "unit": "vtmb:material:art/librarysign1",
        "parent": "/Game/ElysiumGenerated/Materials/V2/M_V2_Lit",
        "decalAsset": "/ElysiumBaked/Materials/art/MI_librarysign1_Decal",
        "basePropertyOverrides": {"blendMode": "Opaque"},
        "textures": {"BaseTexture": "/ElysiumBaked/Textures/art/T_librarysign1"},
    }
    projector = {
        "assetPath": surface["decalAsset"],
        "unit": "vtmb:material:art/librarysign1",
        "parent": "/Game/ElysiumGenerated/Materials/V2/M_V2_Decal",
        "decalAsset": None,
        "basePropertyOverrides": {"blendMode": "Translucent"},
        "textures": {"BaseTexture": "/ElysiumBaked/Textures/art/T_librarysign1"},
    }
    materials = tmp_path / "import" / "materials"
    materials.mkdir(parents=True)
    (materials / "manifest.json").write_text(
        json.dumps({"assets": [surface, projector]}), encoding="utf-8")

    module._STAGED_MATERIALS = None
    try:
        with mock.patch.object(module, "work_root", lambda: tmp_path):
            by_asset, by_unit = module._staged_materials()
        # Both rows are reachable by asset path -- the projector is bound by `decalAsset`, which
        # is how the decal lane names it; only the unit index is the surface's.
        assert sorted(by_asset) == [surface["assetPath"], projector["assetPath"]]
        assert by_unit["art/librarysign1"] == surface

        units, missing = module.v2_bound_units(
            {}, {"art/librarysign1": "sign.mtl/librarysign1"}, by_asset, by_unit)
        assert missing == []
        row, owner = units["art/librarysign1"]
        assert row["parent"] == "/Game/ElysiumGenerated/Materials/V2/M_V2_Lit"
        assert owner == "sign.mtl/librarysign1"
        # The surface is opaque, so the unit is not asked for an alpha channel it never had.
        assert module.v2_alpha_units(units) == []
    finally:
        module._STAGED_MATERIALS = None
