"""Contract tests for `uv run elysium import materials` (the offline stage phase, SF-4.4).

Every fixture is a synthetic material unit built directly as an `ELYSIUM_vtmb_material` extension
body and encoded with `encode_glb` -- never the real install, never a real staging tree -- the way
`test_texture_corpus_import.py` fabricates texture units.
"""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

import pytest

from elysium_pipeline import paths
from elysium_pipeline.formats.unit_contract.container import encode_glb
from elysium_pipeline.importers import materials as importer

MATERIAL_EXTENSION = importer.MATERIAL_EXTENSION


def _param(index, key, value, *, block="", value_type="string"):
    return {"index": index, "block": block, "key": key, "sourceKey": key, "value": value,
            "valueType": value_type}


def _unit(
    key: str,
    *,
    shader: str = "lightmappedgeneric",
    family: str | None = None,
    resolved: bool = True,
    parameters=(),
    proxies=(),
    dependencies=(),
    patch: dict | None = None,
    programs=(),
) -> dict:
    family = family if family is not None else shader
    return {
        "schemaVersion": "1.1.0",
        "identity": {"asset": f"vtmb:material:{key}", "materialPath": key},
        "shader": shader,
        "sourceShader": shader,
        "shaderResolution": {"family": family, "resolved": resolved, "programs": list(programs),
                             "inputs": [], "reason": ""},
        "parameters": list(parameters),
        "blocks": [],
        "proxies": list(proxies),
        "textureBindings": [],
        "patch": patch,
        "patchOf": None,
        "surfaceProperty": None,
        "environment": None,
        "dependencies": list(dependencies),
        "comments": [],
        "anomalies": [],
        "omissions": [],
        "coverage": {"mapped": [], "byteLedger": [], "unresolved": [], "unsupported": []},
    }


def _texture_dep(parameter: str, texture_key: str, *, resolved: bool = True) -> dict:
    return {"role": "texture", "parameter": parameter, "asset": f"vtmb:texture:{texture_key}",
            "sourcePath": f"materials/{texture_key}.tth", "resolved": resolved}


def _publish(export_v2_root: Path, key: str, extension: dict) -> Path:
    document = {
        "asset": {"version": "2.0"},
        "extensionsUsed": [MATERIAL_EXTENSION],
        "extensions": {MATERIAL_EXTENSION: extension},
    }
    destination = Path(export_v2_root) / "materials" / (key + ".glb")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(encode_glb(document))
    return destination


def _manifest(root: Path) -> dict:
    return json.loads((root / importer.MANIFEST_NAME).read_text(encoding="utf-8"))


def _entries(root: Path) -> dict[str, dict]:
    return {entry["assetPath"]: entry for entry in _manifest(root)["assets"]}


def _provenance(root: Path, entry: dict) -> dict:
    return json.loads((root / entry["provenance"]).read_text(encoding="utf-8"))


# --- basic Lit unit, grey and chromatic tint ------------------------------------------------------


def test_lit_unit_binds_base_mask_and_grey_tint(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "brick/wall", _unit(
        "brick/wall",
        parameters=[
            _param(0, "$basetexture", "brick/wall"),
            _param(1, "$surfaceprop", "brick"),
            _param(2, "$envmap", "env_cubemap"),
            _param(3, "$envmapmask", "brick/wall_ref"),
            _param(4, "$envmaptint", "[0.5 0.5 0.5]"),
        ],
        dependencies=[_texture_dep("$basetexture", "brick/wall"),
                      _texture_dep("$envmapmask", "brick/wall_ref")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/brick/MI_wall"]
    assert entry["parent"] == f"{importer.MASTER_ROOT}/M_V2_Lit"
    assert entry["textures"] == {
        "BaseTexture": "/ElysiumBaked/Textures/brick/T_wall",
        "EnvMapMask": "/ElysiumBaked/Textures/brick/T_wall_ref",
    }
    assert entry["vectors"]["EnvMapTint"] == [0.5, 0.5, 0.5, 1.0]
    assert entry["surfaceClass"] == "brick"
    assert entry["surfaceClassIndex"] == importer.SURFACE_CLASS_INDEX["brick"]
    assert entry["physMaterial"] == "/ElysiumBaked/SurfaceProperties/PM_brick"
    assert entry["switches"]["UseEnvMap"] is True
    assert entry["switches"]["UseEnvMapMask"] is True
    assert entry["basePropertyOverrides"] == {
        "blendMode": "Opaque", "twoSided": False, "opacityMaskClipValue": None,
    }


def test_chromatic_tint_is_recorded_the_same_way_as_grey(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "metal/gold", _unit(
        "metal/gold",
        parameters=[
            _param(0, "$basetexture", "metal/gold"),
            _param(1, "$envmap", "env_cubemap"),
            _param(2, "$envmaptint", "[1.0 0.7 0.0]"),
        ],
        dependencies=[_texture_dep("$basetexture", "metal/gold")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/metal/MI_gold"]
    assert entry["vectors"]["EnvMapTint"] == [1.0, 0.7, 0.0, 1.0]


# --- blend mode overrides --------------------------------------------------------------------------


def test_translucent_flag_sets_blend_mode(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "glass/pane", _unit(
        "glass/pane", shader="vertexlitgeneric",
        parameters=[
            _param(0, "$basetexture", "glass/pane"),
            _param(1, "$translucent", "1"),
        ],
        dependencies=[_texture_dep("$basetexture", "glass/pane")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/glass/MI_pane"]
    assert entry["parent"] == f"{importer.MASTER_ROOT}/M_V2_LitTranslucent"
    assert entry["basePropertyOverrides"] == {
        "blendMode": "Translucent", "twoSided": False, "opacityMaskClipValue": None,
    }


def test_alphatest_flag_sets_masked_and_clip_value(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "grates/gate", _unit(
        "grates/gate", shader="vertexlitgeneric",
        parameters=[
            _param(0, "$basetexture", "grates/gate"),
            _param(1, "$alphatest", "1"),
        ],
        dependencies=[_texture_dep("$basetexture", "grates/gate")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/grates/MI_gate"]
    assert entry["basePropertyOverrides"] == {
        "blendMode": "Masked", "opacityMaskClipValue": 0.5, "twoSided": False,
    }


# --- envmap shapes ----------------------------------------------------------------------------------


def test_env_cubemap_symbol_binds_no_texture_and_no_runtime_bind(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "wood/floor", _unit(
        "wood/floor",
        parameters=[
            _param(0, "$basetexture", "wood/floor"),
            _param(1, "$envmap", "env_cubemap"),
        ],
        dependencies=[_texture_dep("$basetexture", "wood/floor")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/wood/MI_floor"]
    assert "EnvMap" not in entry["textures"]
    assert entry["switches"]["UseEnvMap"] is True
    provenance = _provenance(tmp_path / "stage", entry)
    # "Nothing binds it at runtime": no `runtimeBind` field, and no `envmap` runtime row.
    assert "runtimeBind" not in provenance["environment"]
    assert provenance["environment"]["envMapSymbol"] == "env_cubemap"
    assert all(row["kind"] != "envmap" for row in provenance["runtime"])


def test_authored_fixed_cube_binds_a_texturecube_and_sets_the_switch(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "hall/mirror", _unit(
        "hall/mirror",
        parameters=[
            _param(0, "$basetexture", "hall/mirror"),
            _param(1, "$envmap", "envmap/blood"),
        ],
        dependencies=[_texture_dep("$basetexture", "hall/mirror"),
                      _texture_dep("$envmap", "envmap/blood")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/hall/MI_mirror"]
    assert entry["textures"]["EnvMap"] == "/ElysiumBaked/Textures/envmap/TC_blood"
    assert entry["switches"]["UseFixedCube"] is True
    assert entry["switches"]["UseEnvMap"] is True


# --- patched map materials --------------------------------------------------------------------------


def test_patched_unit_with_only_envmap_overrides_nothing_and_parents_to_base(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "glass/pane", _unit(
        "glass/pane", shader="vertexlitgeneric",
        parameters=[_param(0, "$basetexture", "glass/pane")],
        dependencies=[_texture_dep("$basetexture", "glass/pane")],
    ))
    _publish(export, "maps/ch_cloud_1/glass/pane", _unit(
        "maps/ch_cloud_1/glass/pane", shader="patch", family="patch", resolved=False,
        parameters=[
            _param(0, "include", "glass/pane"),
            _param(1, "$envmap", "maps/ch_cloud_1/cubemapdefault", block="replace#1"),
        ],
        dependencies=[_texture_dep("$envmap", "maps/ch_cloud_1/cubemapdefault")],
        patch={"include": "glass/pane", "asset": "vtmb:material:glass/pane", "operations": ["replace"]},
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    assert result.patched == 1
    entries = _entries(tmp_path / "stage")
    patched = entries["/ElysiumBaked/Materials/maps/ch_cloud_1/glass/MI_pane"]
    assert patched["patched"] is True
    assert patched["parent"] == "/ElysiumBaked/Materials/glass/MI_pane"
    assert patched["textures"] == {} and patched["scalars"] == {} and patched["switches"] == {}
    assert patched["surfaceClass"] is None and patched["physMaterial"] is None
    provenance = _provenance(tmp_path / "stage", patched)
    assert provenance["environment"]["patchedProbe"] is True


def test_patched_unit_with_waterdepth_carries_the_override_scalar(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "water/pool", _unit(
        "water/pool", shader="water",
        parameters=[_param(0, "$basetexture", "water/pool")],
        dependencies=[_texture_dep("$basetexture", "water/pool")],
    ))
    _publish(export, "maps/ch_cloud_1/water/pool", _unit(
        "maps/ch_cloud_1/water/pool", shader="patch", family="patch", resolved=False,
        parameters=[
            _param(0, "include", "water/pool"),
            _param(1, "$waterdepth", "12.5", block="insert#1"),
        ],
        patch={"include": "water/pool", "asset": "vtmb:material:water/pool", "operations": ["insert"]},
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entries = _entries(tmp_path / "stage")
    patched = entries["/ElysiumBaked/Materials/maps/ch_cloud_1/water/MI_pool"]
    assert patched["scalars"] == {"WaterDepth": 12.5}
    assert patched["parent"] == "/ElysiumBaked/Materials/water/MI_pool"


# --- debug/tool provenance-only families ------------------------------------------------------------


def test_debug_family_stages_provenance_only_parented_to_unlit(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "debug/wire", _unit(
        "debug/wire", shader="wireframe",
        parameters=[
            _param(0, "$basetexture", "debug/wire"),
            _param(1, "$color", "[1 0 0]"),
        ],
        dependencies=[_texture_dep("$basetexture", "debug/wire")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    assert result.provenance_only == 1
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/debug/MI_wire"]
    assert entry["provenanceOnly"] is True
    assert entry["parent"] == f"{importer.MASTER_ROOT}/M_V2_Unlit"
    assert entry["textures"] == {"BaseTexture": "/ElysiumBaked/Textures/debug/T_wire"}
    assert entry["vectors"] == {}  # $color is not reproduced on a provenance-only instance
    manifest = _manifest(tmp_path / "stage")
    assert "debug/wire" in manifest["provenanceOnly"]


# --- unmapped key ------------------------------------------------------------------------------------


def test_unknown_key_fails_the_unit_and_names_it(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "brick/bad", _unit(
        "brick/bad",
        parameters=[
            _param(0, "$basetexture", "brick/bad"),
            _param(1, "$totallymadeupkey", "1"),
        ],
        dependencies=[_texture_dep("$basetexture", "brick/bad")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert len(result.failures) == 1
    key, reason = result.failures[0]
    assert key == "brick/bad"
    assert "$totallymadeupkey" in reason
    manifest = _manifest(tmp_path / "stage")
    assert "/ElysiumBaked/Materials/brick/MI_bad" in manifest["keep"]
    assert manifest["assets"] == []


# --- class-index resolution tiers ---------------------------------------------------------------------


def test_class_index_resolves_from_surfaceprop_first(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "misc/thing", _unit(
        "misc/thing",
        parameters=[
            _param(0, "$basetexture", "misc/thing"),
            _param(1, "$surfaceprop", "METAL"),  # case-folded
        ],
        dependencies=[_texture_dep("$basetexture", "misc/thing")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/misc/MI_thing"]
    assert entry["surfaceClass"] == "metal"


def test_class_index_falls_back_to_vmt_top_directory(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "carpet/rug", _unit(
        "carpet/rug",
        parameters=[_param(0, "$basetexture", "carpet/rug")],
        dependencies=[_texture_dep("$basetexture", "carpet/rug")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/carpet/MI_rug"]
    assert entry["surfaceClass"] == "carpet"


def test_class_index_falls_back_to_default_when_nothing_names_a_class(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "misc2/whatsit", _unit(
        "misc2/whatsit",
        parameters=[_param(0, "$basetexture", "misc2/whatsit")],
        dependencies=[_texture_dep("$basetexture", "misc2/whatsit")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/misc2/MI_whatsit"]
    assert entry["surfaceClass"] == "default"
    assert entry["surfaceClassIndex"] == 0


def test_tier1_addition_surfaceprop_gets_its_own_class_row_but_falls_back_on_physmaterial(tmp_path):
    # `cloth`, `bone`, `asphalt`, `leather` are not in the 63-entry `surfaceproperties.txt` table,
    # but the revision gives each its own class row; the physical material still falls back to
    # `PM_default` (`physMaterialFallback`), because no `PM_cloth` asset exists.
    export = tmp_path / "v2"
    _publish(export, "misc3/cloth", _unit(
        "misc3/cloth",
        parameters=[
            _param(0, "$basetexture", "misc3/cloth"),
            _param(1, "$surfaceprop", "cloth"),
        ],
        dependencies=[_texture_dep("$basetexture", "misc3/cloth")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/misc3/MI_cloth"]
    assert entry["surfaceClass"] == "cloth"
    assert entry["surfaceClassIndex"] == importer.SURFACE_CLASS_INDEX["cloth"]
    assert entry["surfaceClassSource"] == "surfaceprop"
    assert entry["physMaterialFallback"] is True
    assert entry["physMaterial"] == "/ElysiumBaked/SurfaceProperties/PM_default"


def test_unrecognised_surfaceprop_value_falls_through_to_topdir(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "carpet/whatsit", _unit(
        "carpet/whatsit",
        parameters=[
            _param(0, "$basetexture", "carpet/whatsit"),
            _param(1, "$surfaceprop", "totallymadeup"),
        ],
        dependencies=[_texture_dep("$basetexture", "carpet/whatsit")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/carpet/MI_whatsit"]
    assert entry["surfaceClass"] == "carpet"
    assert entry["surfaceClassSource"] == "topdir"


# --- proxies -----------------------------------------------------------------------------------------


def test_sine_proxy_writes_named_scalars(tmp_path):
    export = tmp_path / "v2"
    parameters = [
        _param(0, "$basetexture", "brick/glow"),
        _param(1, "sinemin", "0.1", block="proxies#1/sine#0"),
        _param(2, "sinemax", "0.9", block="proxies#1/sine#0"),
        _param(3, "sineperiod", "2.0", block="proxies#1/sine#0"),
        _param(4, "resultvar", "$alpha", block="proxies#1/sine#0"),
    ]
    _publish(export, "brick/glow", _unit(
        "brick/glow", parameters=parameters,
        proxies=[{"index": 0, "name": "sine", "sourceName": "Sine", "parameters": [1, 2, 3, 4]}],
        dependencies=[_texture_dep("$basetexture", "brick/glow")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/brick/MI_glow"]
    assert entry["scalars"]["SineMin"] == 0.1
    assert entry["scalars"]["SineMax"] == 0.9
    assert entry["scalars"]["SinePeriod"] == 2.0
    provenance = _provenance(tmp_path / "stage", entry)
    assert provenance["proxies"][0]["kind"] == "sine"
    assert provenance["proxies"][0]["destination"] == "scalar"


def test_globalwetness_proxy_writes_wetness_scale_and_runtime_row(tmp_path):
    export = tmp_path / "v2"
    parameters = [
        _param(0, "$basetexture", "asphalt/wet"),
        _param(1, "resultvar", "$envmaptint[0]", block="proxies#1/globalwetness#0"),
        _param(2, "scale", "0.56", block="proxies#1/globalwetness#0"),
    ]
    _publish(export, "asphalt/wet", _unit(
        "asphalt/wet", parameters=parameters,
        proxies=[{"index": 0, "name": "globalwetness", "sourceName": "GlobalWetness",
                 "parameters": [1, 2]}],
        dependencies=[_texture_dep("$basetexture", "asphalt/wet")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/asphalt/MI_wet"]
    # R5.3: a real per-instance scalar pair on M_V2_Lit (the only master with a wetness lane),
    # not provenance-only any more -- see "Decal fog and wetness homes" in seam_map_material.md.
    assert entry["scalars"]["WetnessScale"] == 0.56
    assert entry["scalars"]["WetnessDriven"] == 1.0
    provenance = _provenance(tmp_path / "stage", entry)
    assert provenance["wetnessScale"] == 0.56
    assert any(row["kind"] == "globalwetness" for row in provenance["runtime"])


# --- determinism ---------------------------------------------------------------------------------------


def test_two_runs_produce_byte_identical_manifest_and_sidecars(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "brick/wall", _unit(
        "brick/wall",
        parameters=[
            _param(0, "$basetexture", "brick/wall"),
            _param(1, "$surfaceprop", "brick"),
            _param(2, "$envmap", "env_cubemap"),
        ],
        dependencies=[_texture_dep("$basetexture", "brick/wall")],
    ))
    _publish(export, "maps/m1/brick/wall", _unit(
        "maps/m1/brick/wall", shader="patch", family="patch", resolved=False,
        parameters=[
            _param(0, "include", "brick/wall"),
            _param(1, "$envmap", "maps/m1/cubemapdefault", block="replace#1"),
        ],
        patch={"include": "brick/wall", "asset": "vtmb:material:brick/wall", "operations": ["replace"]},
    ))
    stage = tmp_path / "stage"
    importer.stage_materials(export, stage)
    first = {p: p.read_bytes() for p in sorted(stage.rglob("*")) if p.is_file()}
    importer.stage_materials(export, stage)
    second = {p: p.read_bytes() for p in sorted(stage.rglob("*")) if p.is_file()}
    assert first.keys() == second.keys()
    assert first == second


# --- surface class table -------------------------------------------------------------------------------


def test_surface_classes_table_is_pinned(tmp_path):
    assert importer.SURFACE_CLASSES[0] == "default"
    assert len(importer.SURFACE_CLASSES) == len(set(importer.SURFACE_CLASSES))
    assert importer.SURFACE_CLASS_INDEX["default"] == 0
    # 72 rows exactly: 63 `surfaceproperties.txt` entries (`default` included) + 6
    # top-directory-only names + 3 tier-1 additions (`asphalt` counted once, via the topdir set).
    assert len(importer.SURFACE_CLASSES) == 72
    # Every install $surfaceprop entry this corpus stages against is in the table.
    for name in ("brick", "metal", "wood", "water", "glass", "carpet"):
        assert name in importer.SURFACE_CLASS_INDEX
    # Tier-1 additions: real surfaces with no `surfaceproperties.txt` row of their own.
    for name in ("bone", "cloth", "leather", "asphalt"):
        assert name in importer.SURFACE_CLASS_INDEX
    # The curated 16-name top-directory allowlist, `grass` included per the revision.
    assert importer.TOP_DIRECTORY_CLASSES == frozenset({
        "plaster", "wood", "stone", "blends", "brick", "ground", "metal", "tile", "drapery",
        "carpet", "cable", "glass", "grates", "asphalt", "water", "grass",
    })


# --- patched-unit identity: asset paths preserve subdirectories, patchOf is filename-derived ------


def test_patched_asset_path_preserves_subdirectories(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "brick/wall", _unit(
        "brick/wall",
        parameters=[_param(0, "$basetexture", "brick/wall")],
        dependencies=[_texture_dep("$basetexture", "brick/wall")],
    ))
    _publish(export, "maps/ch1/sub/dir/brick/wall", _unit(
        "maps/ch1/sub/dir/brick/wall", shader="patch", family="patch", resolved=False,
        parameters=[
            _param(0, "include", "brick/wall"),
            _param(1, "$envmap", "maps/ch1/c1_2_3", block="replace#1"),
        ],
        dependencies=[_texture_dep("$envmap", "maps/ch1/c1_2_3")],
        patch={"include": "brick/wall", "asset": "vtmb:material:brick/wall", "operations": ["replace"]},
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entries = _entries(tmp_path / "stage")
    assert "/ElysiumBaked/Materials/maps/ch1/sub/dir/brick/MI_wall" in entries


def test_patch_of_is_filename_derived_from_coordinate_suffix(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "brick/wall", _unit(
        "brick/wall",
        parameters=[_param(0, "$basetexture", "brick/wall")],
        dependencies=[_texture_dep("$basetexture", "brick/wall")],
    ))
    _publish(export, "maps/ch1/brick/wall_1_2_3", _unit(
        "maps/ch1/brick/wall_1_2_3", shader="patch", family="patch", resolved=False,
        parameters=[
            _param(0, "include", "brick/wall"),
            _param(1, "$envmap", "maps/ch1/c1_2_3", block="replace#1"),
        ],
        dependencies=[_texture_dep("$envmap", "maps/ch1/c1_2_3")],
        patch={"include": "brick/wall", "asset": "vtmb:material:brick/wall", "operations": ["replace"]},
    ))
    _publish(export, "maps/ch1/brick/wall", _unit(
        "maps/ch1/brick/wall", shader="patch", family="patch", resolved=False,
        parameters=[
            _param(0, "include", "brick/wall"),
            _param(1, "$envmap", "maps/ch1/cubemapdefault", block="replace#1"),
        ],
        dependencies=[_texture_dep("$envmap", "maps/ch1/cubemapdefault")],
        patch={"include": "brick/wall", "asset": "vtmb:material:brick/wall", "operations": ["replace"]},
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entries = _entries(tmp_path / "stage")
    coord = entries["/ElysiumBaked/Materials/maps/ch1/brick/MI_wall_1_2_3"]
    default = entries["/ElysiumBaked/Materials/maps/ch1/brick/MI_wall"]
    coord_prov = _provenance(tmp_path / "stage", coord)
    default_prov = _provenance(tmp_path / "stage", default)
    assert coord_prov["patchOf"] == {"x": 1, "y": 2, "z": 3}
    assert default_prov["patchOf"] is None
    assert coord_prov["patchBase"] == "vtmb:material:brick/wall"
    assert default_prov["patchBase"] == "vtmb:material:brick/wall"


def test_patched_unit_fails_when_base_did_not_stage(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "maps/ch1/brick/wall", _unit(
        "maps/ch1/brick/wall", shader="patch", family="patch", resolved=False,
        parameters=[
            _param(0, "include", "brick/wall"),
            _param(1, "$envmap", "maps/ch1/cubemapdefault", block="replace#1"),
        ],
        dependencies=[_texture_dep("$envmap", "maps/ch1/cubemapdefault")],
        patch={"include": "brick/wall", "asset": "vtmb:material:brick/wall", "operations": ["replace"]},
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert len(result.failures) == 1
    key, reason = result.failures[0]
    assert key == "maps/ch1/brick/wall"
    assert "brick/MI_wall" in reason


def test_standalone_sm_tattoo_unit_with_no_patch_block_takes_ordinary_master(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "maps/sm_tattoo/glass/glass01", _unit(
        "maps/sm_tattoo/glass/glass01", shader="lightmappedgeneric",
        parameters=[_param(0, "$basetexture", "maps/sm_tattoo/glass/glass01")],
        dependencies=[_texture_dep("$basetexture", "maps/sm_tattoo/glass/glass01")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/maps/sm_tattoo/glass/MI_glass01"]
    assert entry["patched"] is False
    assert entry["parent"] == f"{importer.MASTER_ROOT}/M_V2_Lit"


# --- reflection contract: chromatic split, fixed cube precedence ----------------------------------


def test_authored_fixed_cube_beats_the_chromatic_branch(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "metal/gold2", _unit(
        "metal/gold2",
        parameters=[
            _param(0, "$basetexture", "metal/gold2"),
            _param(1, "$envmap", "envmap/blood"),
            _param(2, "$envmaptint", "[1.0 0.7 0.0]"),  # chromatic spread
        ],
        dependencies=[_texture_dep("$basetexture", "metal/gold2"),
                      _texture_dep("$envmap", "envmap/blood")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/metal/MI_gold2"]
    assert entry["switches"]["UseFixedCube"] is True
    assert entry["switches"].get("MetallicTint") is not True


def test_env_cubemap_with_chromatic_tint_sets_metallic_tint(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "metal/copper", _unit(
        "metal/copper",
        parameters=[
            _param(0, "$basetexture", "metal/copper"),
            _param(1, "$envmap", "env_cubemap"),
            _param(2, "$envmaptint", "[0.74 0.57 0.31]"),
        ],
        dependencies=[_texture_dep("$basetexture", "metal/copper")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/metal/MI_copper"]
    assert entry["switches"]["MetallicTint"] is True
    provenance = _provenance(tmp_path / "stage", entry)
    assert provenance["environment"]["envMapTintChromatic"] is True


def test_chroma_threshold_is_read_from_argument_not_a_literal(tmp_path):
    # A tint whose spread sits between a low and a high threshold flips classification, proving
    # the split reads the passed-in knob rather than a hardcoded constant.
    export = tmp_path / "v2"
    _publish(export, "metal/edge", _unit(
        "metal/edge",
        parameters=[
            _param(0, "$basetexture", "metal/edge"),
            _param(1, "$envmap", "env_cubemap"),
            _param(2, "$envmaptint", "[0.5 0.45 0.5]"),  # spread 0.05
        ],
        dependencies=[_texture_dep("$basetexture", "metal/edge")],
    ))
    loose = importer.stage_materials(export, tmp_path / "stage_loose", chroma_threshold=0.1)
    assert loose.failures == []
    loose_entry = _entries(tmp_path / "stage_loose")["/ElysiumBaked/Materials/metal/MI_edge"]
    assert loose_entry["switches"].get("MetallicTint") is not True

    tight = importer.stage_materials(export, tmp_path / "stage_tight", chroma_threshold=0.01)
    assert tight.failures == []
    tight_entry = _entries(tmp_path / "stage_tight")["/ElysiumBaked/Materials/metal/MI_edge"]
    assert tight_entry["switches"]["MetallicTint"] is True


# --- $ignorez re-routing --------------------------------------------------------------------------


def test_ignorez_world_unit_reroutes_to_sprite(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "engine/lightsprite", _unit(
        "engine/lightsprite", shader="unlitgeneric",
        parameters=[
            _param(0, "$basetexture", "engine/lightsprite"),
            _param(1, "$ignorez", "1"),
        ],
        dependencies=[_texture_dep("$basetexture", "engine/lightsprite")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/engine/MI_lightsprite"]
    assert entry["parent"] == f"{importer.MASTER_ROOT}/M_V2_Sprite"
    provenance = _provenance(tmp_path / "stage", entry)
    assert provenance["ignoreZ"] is True


def test_ignorez_named_divergence_unit_keeps_lit(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "models/scenery/furniture/displaytable/floating", _unit(
        "models/scenery/furniture/displaytable/floating", shader="vertexlitgeneric",
        parameters=[
            _param(0, "$basetexture", "models/scenery/furniture/displaytable/floating"),
            _param(1, "$ignorez", "1"),
        ],
        dependencies=[_texture_dep("$basetexture", "models/scenery/furniture/displaytable/floating")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")[
        "/ElysiumBaked/Materials/models/scenery/furniture/displaytable/MI_floating"]
    assert entry["parent"] == f"{importer.MASTER_ROOT}/M_V2_Lit"
    provenance = _provenance(tmp_path / "stage", entry)
    assert provenance["ignoreZNamedDivergence"] is True


# --- $spriterendermode blend table ------------------------------------------------------------------


def test_sprite_rendermode_8_sets_additive_blend(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "sprites/muzzleflash", _unit(
        "sprites/muzzleflash", shader="sprite",
        parameters=[
            _param(0, "$basetexture", "sprites/muzzleflash"),
            _param(1, "$spriterendermode", "8"),
        ],
        dependencies=[_texture_dep("$basetexture", "sprites/muzzleflash")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/sprites/MI_muzzleflash"]
    assert entry["basePropertyOverrides"] == {
        "blendMode": "Additive", "twoSided": False, "opacityMaskClipValue": None,
    }


def test_sprite_rendermode_6_is_a_stage_failure(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "sprites/bad", _unit(
        "sprites/bad", shader="sprite",
        parameters=[
            _param(0, "$basetexture", "sprites/bad"),
            _param(1, "$spriterendermode", "6"),
        ],
        dependencies=[_texture_dep("$basetexture", "sprites/bad")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert len(result.failures) == 1
    key, reason = result.failures[0]
    assert key == "sprites/bad"
    assert "spriterendermode" in reason


# --- $alphatestreference latent key -----------------------------------------------------------------


def test_alphatestreference_overrides_the_default_clip_value(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "grates/gate2", _unit(
        "grates/gate2", shader="vertexlitgeneric",
        parameters=[
            _param(0, "$basetexture", "grates/gate2"),
            _param(1, "$alphatest", "1"),
            _param(2, "$alphatestreference", "0.75"),
        ],
        dependencies=[_texture_dep("$basetexture", "grates/gate2")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/grates/MI_gate2"]
    assert entry["basePropertyOverrides"] == {
        "blendMode": "Masked", "opacityMaskClipValue": 0.75, "twoSided": False,
    }


# --- decal surfaces: $decal is a provenance flag, not a switch -------------------------------------


def test_decal_flag_is_provenance_only_not_a_switch(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "decals/blood", _unit(
        "decals/blood", shader="lightmappedgeneric",
        parameters=[
            _param(0, "$basetexture", "decals/blood"),
            _param(1, "$decal", "1"),
        ],
        dependencies=[_texture_dep("$basetexture", "decals/blood")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/decals/MI_blood"]
    assert "IsDecalSurface" not in entry["switches"]
    provenance = _provenance(tmp_path / "stage", entry)
    assert provenance["isDecalSurface"] is True


def test_decalmodulate_family_takes_decal_master_and_modulate_blend(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "decals/splat", _unit(
        "decals/splat", shader="decalmodulate",
        parameters=[_param(0, "$basetexture", "decals/splat")],
        dependencies=[_texture_dep("$basetexture", "decals/splat")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/decals/MI_splat"]
    assert entry["parent"] == f"{importer.MASTER_ROOT}/M_V2_Decal"
    assert entry["basePropertyOverrides"] == {
        "blendMode": "Modulate", "twoSided": False, "opacityMaskClipValue": None,
    }


# --- exposed-parameter refusal ----------------------------------------------------------------------


def test_parameter_not_on_selected_masters_exposed_list_fails_the_unit(tmp_path):
    # `Iris` is on M_V2_Eyes, not on M_V2_Lit; author $iris on a lightmappedgeneric unit.
    export = tmp_path / "v2"
    _publish(export, "brick/bad_iris", _unit(
        "brick/bad_iris",
        parameters=[
            _param(0, "$basetexture", "brick/bad_iris"),
            _param(1, "$iris", "brick/bad_iris"),
        ],
        dependencies=[_texture_dep("$basetexture", "brick/bad_iris"),
                      _texture_dep("$iris", "brick/bad_iris")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert len(result.failures) == 1
    key, reason = result.failures[0]
    assert key == "brick/bad_iris"
    assert "Iris" in reason and "M_V2_Lit" in reason


def test_every_master_exposed_dict_is_internally_consistent(tmp_path):
    for master, exposed in importer.EXPOSED_PARAMS.items():
        assert master.startswith("M_V2_")
        assert set(exposed.values()) <= {"T", "S", "V", "#"}
        for shared_name in ("SurfaceClassIndex", "SurfaceClassLUT", "Alpha", "Color"):
            assert shared_name in exposed, f"{master} is missing shared param {shared_name}"


# --- TwoTexture: TexScaleOffset / Texture2ScaleOffset merges ---------------------------------------


def test_two_texture_second_layer_scale_and_offset_merge_into_one_vector(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "world/transition", _unit(
        "world/transition", shader="worldvertextransition",
        parameters=[
            _param(0, "$basetexture", "world/transition"),
            _param(1, "$basetexture2", "world/transition2"),
            _param(2, "$tex2scale", "0.5"),
            _param(3, "$tex2offset", "[0.1 0.2]"),
        ],
        dependencies=[_texture_dep("$basetexture", "world/transition"),
                      _texture_dep("$basetexture2", "world/transition2")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/world/MI_transition"]
    assert entry["parent"] == f"{importer.MASTER_ROOT}/M_V2_TwoTexture"
    assert entry["vectors"]["Texture2ScaleOffset"] == [0.5, 0.5, 0.1, 0.2]


def test_alpha_bias_is_a_real_scalar_on_two_texture(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "world/blend2", _unit(
        "world/blend2", shader="worldtwotextureblend",
        parameters=[
            _param(0, "$basetexture", "world/blend2"),
            _param(1, "$basetexture2", "world/blend2b"),
            _param(2, "$alpha_bias", "0.2"),
        ],
        dependencies=[_texture_dep("$basetexture", "world/blend2"),
                      _texture_dep("$basetexture2", "world/blend2b")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/world/MI_blend2"]
    assert entry["scalars"]["AlphaBias"] == 0.2


# --- texturescroll: two independent lanes, summed on collision -------------------------------------


def test_texturescroll_base_and_bump_lanes_are_independent(tmp_path):
    # Lit hosts both lanes: $basetexturetransform -> BaseScrollRateU/V, $bumptransform ->
    # BumpScrollRateU/V.
    export = tmp_path / "v2"
    parameters = [
        _param(0, "$basetexture", "brick/scroll"),
        _param(1, "$bumpmap", "brick/scroll_bump"),
        _param(2, "texturescrollvar", "$basetexturetransform", block="proxies#1/texturescroll#0"),
        _param(3, "texturescrollrate", "1.0", block="proxies#1/texturescroll#0"),
        _param(4, "texturescrollangle", "0", block="proxies#1/texturescroll#0"),
        _param(5, "texturescrollvar", "$bumptransform", block="proxies#2/texturescroll#0"),
        _param(6, "texturescrollrate", "2.0", block="proxies#2/texturescroll#0"),
        _param(7, "texturescrollangle", "90", block="proxies#2/texturescroll#0"),
    ]
    _publish(export, "brick/scroll", _unit(
        "brick/scroll", shader="lightmappedgeneric", parameters=parameters,
        proxies=[
            {"index": 0, "name": "texturescroll", "sourceName": "TextureScroll", "parameters": [2, 3, 4]},
            {"index": 1, "name": "texturescroll", "sourceName": "TextureScroll", "parameters": [5, 6, 7]},
        ],
        dependencies=[_texture_dep("$basetexture", "brick/scroll"),
                      _texture_dep("$bumpmap", "brick/scroll_bump")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/brick/MI_scroll"]
    assert entry["scalars"]["BaseScrollRateU"] == 1.0
    assert entry["scalars"]["BaseScrollRateV"] == 0.0
    assert entry["scalars"]["BumpScrollRateU"] == pytest.approx(0.0, abs=1e-6)
    assert entry["scalars"]["BumpScrollRateV"] == pytest.approx(2.0, abs=1e-6)


def test_texturescroll_two_proxies_on_the_same_lane_sum_rates(tmp_path):
    export = tmp_path / "v2"
    parameters = [
        _param(0, "$basetexture", "water/dual"),
        _param(1, "$bumpmap", "water/dual_bump"),
        _param(2, "texturescrollvar", "$bumpoffset", block="proxies#1/texturescroll#0"),
        _param(3, "texturescrollrate", "1.0", block="proxies#1/texturescroll#0"),
        _param(4, "texturescrollangle", "0", block="proxies#1/texturescroll#0"),
        _param(5, "texturescrollvar", "$bumptransform", block="proxies#2/texturescroll#0"),
        _param(6, "texturescrollrate", "1.5", block="proxies#2/texturescroll#0"),
        _param(7, "texturescrollangle", "0", block="proxies#2/texturescroll#0"),
    ]
    _publish(export, "water/dual", _unit(
        "water/dual", shader="water", parameters=parameters,
        proxies=[
            {"index": 0, "name": "texturescroll", "sourceName": "TextureScroll", "parameters": [2, 3, 4]},
            {"index": 1, "name": "texturescroll", "sourceName": "TextureScroll", "parameters": [5, 6, 7]},
        ],
        dependencies=[_texture_dep("$basetexture", "water/dual"),
                      _texture_dep("$bumpmap", "water/dual_bump")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/water/MI_dual"]
    assert entry["scalars"]["BumpScrollRateU"] == pytest.approx(2.5, abs=1e-6)


# --- animatedtexture: base vs normal lane -----------------------------------------------------------


def _animatedtexture_unit_kwargs():
    parameters = [
        _param(0, "$basetexture", "water/anim"),
        _param(1, "$bumpmap", "water/anim_bump"),
        _param(2, "animatedtexturevar", "$bumpmap", block="proxies#1/animatedtexture#0"),
        _param(3, "animatedtextureframenumvar", "$bumpframe", block="proxies#1/animatedtexture#0"),
        _param(4, "animatedtextureframerate", "10", block="proxies#1/animatedtexture#0"),
    ]
    return dict(
        shader="water", parameters=parameters,
        proxies=[{"index": 0, "name": "animatedtexture", "sourceName": "AnimatedTexture",
                 "parameters": [2, 3, 4]}],
        dependencies=[_texture_dep("$basetexture", "water/anim"),
                      _texture_dep("$bumpmap", "water/anim_bump")],
    )


def test_animatedtexture_binds_frames_array_when_the_texture_staged_as_one(tmp_path):
    """The `TA_` sibling `textures.py` stages for a `frames > 1` unit is bound to
    `NormalMapFrames`, `NormalFrameCount` is read from the same sidecar, and the switch only then
    turns on (review fix, finding 2)."""
    export = tmp_path / "v2"
    _publish(export, "water/anim", _unit("water/anim", **_animatedtexture_unit_kwargs()))
    texture_staging = tmp_path / "texture-stage"
    sidecar = texture_staging / "water" / ("anim_bump" + importer.TEXTURE_PROVENANCE_SUFFIX)
    sidecar.parent.mkdir(parents=True, exist_ok=True)
    sidecar.write_text(json.dumps({"frames": 4}), encoding="utf-8")

    result = importer.stage_materials(export, tmp_path / "stage", texture_staging_root=texture_staging)

    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/water/MI_anim"]
    assert entry["scalars"]["NormalFrameRate"] == 10.0
    assert entry["scalars"]["NormalFrameCount"] == 4.0
    assert entry["switches"]["UseAnimatedNormalFrames"] is True
    assert entry["textures"]["NormalMapFrames"] == "/ElysiumBaked/Textures/water/TA_anim_bump"
    assert "FrameRate" not in entry["scalars"]


def test_animatedtexture_leaves_the_switch_off_without_a_staged_frames_array(tmp_path):
    """No texture-staging root (or a texture that never staged as a `Texture2DArray`) means
    `UseAnimatedNormalFrames` stays off rather than sampling the master's inert default frames
    array -- named in `omissions`, not silently dropped (review fix, finding 2)."""
    export = tmp_path / "v2"
    _publish(export, "water/anim", _unit("water/anim", **_animatedtexture_unit_kwargs()))

    result = importer.stage_materials(export, tmp_path / "stage")

    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/water/MI_anim"]
    assert entry["scalars"]["NormalFrameRate"] == 10.0
    # Review finding 2: full-state switches now stage every one of the master's switch names
    # explicitly, so the omitted-array case lands `False`, not an absent key.
    assert entry["switches"]["UseAnimatedNormalFrames"] is False
    assert "NormalMapFrames" not in entry["textures"]
    provenance = _provenance(tmp_path / "stage", entry)
    assert any(row.get("kind") == "animatedFramesArrayUnavailable" for row in provenance["omissions"])


# --- sine proxy: SineTargetMask/SineChannelMask, case-fold, provenance-only targets -----------------


def test_sine_proxy_color_component_target_sets_mask_and_channel(tmp_path):
    export = tmp_path / "v2"
    parameters = [
        _param(0, "$basetexture", "brick/pulse"),
        _param(1, "sinemin", "0.1", block="proxies#1/sine#0"),
        _param(2, "sinemax", "0.9", block="proxies#1/sine#0"),
        _param(3, "sineperiod", "2.0", block="proxies#1/sine#0"),
        _param(4, "resultvar", "$COLOR[1]", block="proxies#1/sine#0"),  # mixed case, on purpose
    ]
    _publish(export, "brick/pulse", _unit(
        "brick/pulse", parameters=parameters,
        proxies=[{"index": 0, "name": "sine", "sourceName": "Sine", "parameters": [1, 2, 3, 4]}],
        dependencies=[_texture_dep("$basetexture", "brick/pulse")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/brick/MI_pulse"]
    assert entry["vectors"]["SineTargetMask"] == [0.0, 1.0, 0.0, 0.0]  # .y = Color
    assert entry["vectors"]["SineChannelMask"] == [0.0, 1.0, 0.0, 0.0]  # component 1


def test_sine_proxy_detailscale_target_is_provenance_only_and_named(tmp_path):
    export = tmp_path / "v2"
    parameters = [
        _param(0, "$basetexture", "shadertest/thing"),
        _param(1, "sinemin", "0.1", block="proxies#1/sine#0"),
        _param(2, "sinemax", "0.9", block="proxies#1/sine#0"),
        _param(3, "sineperiod", "2.0", block="proxies#1/sine#0"),
        _param(4, "resultvar", "$detailscale", block="proxies#1/sine#0"),
    ]
    _publish(export, "shadertest/thing", _unit(
        "shadertest/thing", parameters=parameters,
        proxies=[{"index": 0, "name": "sine", "sourceName": "Sine", "parameters": [1, 2, 3, 4]}],
        dependencies=[_texture_dep("$basetexture", "shadertest/thing")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/shadertest/MI_thing"]
    assert "SineTargetMask" not in entry["vectors"]
    provenance = _provenance(tmp_path / "stage", entry)
    assert any(row["kind"] == "proxyTargetProvenanceOnly" for row in provenance["omissions"])


# --- provenance-only families: SurfaceClassIndex still written -------------------------------------


def test_provenance_only_entry_still_gets_surface_class_index(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "debug/wire2", _unit(
        "debug/wire2", shader="wireframe",
        parameters=[_param(0, "$basetexture", "debug/wire2")],
        dependencies=[_texture_dep("$basetexture", "debug/wire2")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/debug/MI_wire2"]
    assert entry["scalars"]["SurfaceClassIndex"] == 0


# --- unrecognised keys still fail loudly for a proxy resultvar too ---------------------------------


def test_sine_resultvar_with_no_destination_fails_the_unit(tmp_path):
    export = tmp_path / "v2"
    parameters = [
        _param(0, "$basetexture", "brick/badsine"),
        _param(1, "sinemin", "0.1", block="proxies#1/sine#0"),
        _param(2, "sinemax", "0.9", block="proxies#1/sine#0"),
        _param(3, "sineperiod", "2.0", block="proxies#1/sine#0"),
        _param(4, "resultvar", "$notarealtarget", block="proxies#1/sine#0"),
    ]
    _publish(export, "brick/badsine", _unit(
        "brick/badsine", parameters=parameters,
        proxies=[{"index": 0, "name": "sine", "sourceName": "Sine", "parameters": [1, 2, 3, 4]}],
        dependencies=[_texture_dep("$basetexture", "brick/badsine")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert len(result.failures) == 1
    key, reason = result.failures[0]
    assert key == "brick/badsine"
    assert "notarealtarget" in reason


# --- binding-contract pin: M_V2_Lit against the C++ header (SF-4.3) --------------------------------


def _parse_surface_params_header(path: Path) -> dict[str, dict[str, str]]:
    """A light, line-based parser over `ElysiumSurfaceParams.h`'s `inline const FName NAME(...)`
    declarations, grouped by their *top-level* namespace (`ElysiumSurfaceParamsShared`,
    `ElysiumSurfaceParamsLit`, ...) and, within that, by kind
    (`Textures`/`Scalars`/`Vectors`/`Switches`). Deliberately not a C++ parser -- the header's own
    docstring asks for exactly this: "parse the header text -- do not hand-duplicate a second copy
    that can drift". Grouping by top-level namespace (rather than flattening every declaration into
    one dict, as an earlier version of this parser did) is what lets the header hold more than one
    master's parameter set without a name from one master's namespace leaking into another
    master's comparison."""

    namespace_re = re.compile(r"namespace\s+(\w+)")
    fname_re = re.compile(r"inline const FName (\w+)\(")
    kind_by_leaf = {"Textures": "T", "Scalars": "S", "Vectors": "V", "Switches": "#"}
    stack: list[str] = []
    out: dict[str, dict[str, str]] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = re.sub(r"//.*", "", raw_line)
        match = namespace_re.search(line)
        if match:
            stack.append(match.group(1))
            continue
        for name_match in fname_re.finditer(line):
            leaf = stack[-1] if stack else ""
            kind = kind_by_leaf.get(leaf, "?")
            top = stack[0] if stack else ""
            out.setdefault(top, {})[name_match.group(1)] = kind
        if line.strip() == "}" and stack:
            stack.pop()
    return out


def _header_params_for(master_namespace: str, by_namespace: dict[str, dict[str, str]]) -> dict[str, str]:
    """The four shared parameters (`ElysiumSurfaceParamsShared`) plus `master_namespace`'s own --
    the same shape `importers.materials._merged(_SHARED_PARAMS, ...)` builds for each
    `EXPOSED_PARAMS` entry."""
    merged = dict(by_namespace.get("ElysiumSurfaceParamsShared", {}))
    merged.update(by_namespace.get(master_namespace, {}))
    return merged


_HEADER_PATH = Path(__file__).resolve().parents[2] / "Source/ElysiumUE/Public/ElysiumSurfaceParams.h"


def test_lit_master_exposed_params_pinned_against_cpp_header():
    """`Source/ElysiumUE/Public/ElysiumSurfaceParams.h` is the other half of the binding contract:
    the stage writes exactly the names the header declares for `M_V2_Lit`, or a build error results
    (never a silent default). This test parses the header rather than hand-duplicating its name
    list, so the two cannot drift without this test catching it."""
    parsed = _header_params_for("ElysiumSurfaceParamsLit", _parse_surface_params_header(_HEADER_PATH))
    assert parsed == importer.EXPOSED_PARAMS["M_V2_Lit"]
    assert parsed == importer.EXPOSED_PARAMS["M_V2_LitTranslucent"]



def test_unlit_master_exposed_params_pinned_against_cpp_header():
    parsed = _header_params_for("ElysiumSurfaceParamsUnlit", _parse_surface_params_header(_HEADER_PATH))
    assert parsed == importer.EXPOSED_PARAMS["M_V2_Unlit"]


# --- provenance sidecar <-> UElysiumMaterialProvenance::FromJson (C2) -----------------------------

_PROVENANCE_CPP = (
    Path(__file__).resolve().parents[2] / "Source/ElysiumUE/Private/ElysiumMaterialProvenance.cpp"
)

#: Top-level sidecar keys `FromJson` intentionally does not read: `patched` and `patchOf` (the
#: `{x, y, z}` map-patch coordinate, distinct from `PatchOf`/`patchBase`) are redundant with data
#: the record already carries or does not need; `runtime` restates the scalar/vector side effects
#: `proxies[]` already carries the resolved arguments for; `ignoreZNamedDivergence` is a named
#: deliberate-divergence flag for the placement lane's own bookkeeping, not part of the record a
#: packaged game or the Content Browser needs. Named here so this test states the omission rather
#: than silently passing it (mirrors the C++-side Substrate test's own list).
_KNOWINGLY_UNCOVERED = frozenset({"patched", "patchOf", "runtime", "ignoreZNamedDivergence"})


def _cpp_top_level_keys() -> set[str]:
    """Every `TEXT("...")` literal `ElysiumMaterialProvenance.cpp` reads directly off the sidecar's
    top-level JSON object `O` (via `Str`/`Bool`/`Int`/`Float`/`Arr`/`Obj`), parsed as text rather
    than introspected from C++ -- the same approach `test_make_surface_knobs.py` uses against the
    Unreal headers it pins."""
    text = _PROVENANCE_CPP.read_text(encoding="utf-8")
    return set(re.findall(r'\(O, TEXT\("(\w+)"\)', text))


def test_material_provenance_sidecar_keys_are_covered_by_fromjson(tmp_path):
    """Stage one synthetic unit exercising most of the sidecar's top-level shape (a proxy, a
    material reference, the placement/map fields, an $envmap), merge in the four manifest-entry
    keys `import_materials.py` adds, and assert every top-level key in the resulting sidecar object
    is one `UElysiumMaterialProvenance::FromJson` actually reads (its literal `TEXT("...")` keys) --
    a key the stage writes that the C++ reader silently ignores is exactly C2's defect."""
    export = tmp_path / "v2"
    parameters = [
        _param(0, "$basetexture", "brick/floora"),
        _param(1, "$surfaceprop", "brick"),
        _param(2, "$envmap", "env_cubemap"),
        _param(3, "$crackmaterial", "crack/basic"),
        _param(4, "$spriteorigin", "[1 2]"),
        _param(5, "$minlight", "0.1"),
        _param(6, "$decal", "1"),
        _param(7, "resultvar", "$envmaptint[0]", block="proxies#1/globalwetness#0"),
        _param(8, "scale", "0.5", block="proxies#1/globalwetness#0"),
    ]
    _publish(export, "brick/floora", _unit(
        "brick/floora", parameters=parameters,
        proxies=[{"index": 0, "name": "globalwetness", "sourceName": "GlobalWetness", "parameters": [7, 8]}],
        dependencies=[
            _texture_dep("$basetexture", "brick/floora"),
            {"role": "material", "parameter": "$crackmaterial", "asset": "vtmb:material:crack/basic",
             "sourcePath": "materials/crack/basic.vmt", "resolved": True},
        ],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/brick/MI_floora"]
    sidecar = _provenance(tmp_path / "stage", entry)

    # The four fields import_materials.py merges into the sidecar object as top-level keys before
    # calling ApplyJson (see FromJson's "--- identity ---" comment).
    for key in ("assetPath", "unitGlb", "sourceMembersSha256", "physMaterial"):
        sidecar[key] = entry.get(key, "")

    sidecar_keys = set(sidecar.keys())
    cpp_keys = _cpp_top_level_keys()
    uncovered = sidecar_keys - cpp_keys - _KNOWINGLY_UNCOVERED
    assert not uncovered, (
        "sidecar top-level key(s) %r are not read by UElysiumMaterialProvenance::FromJson "
        "(ElysiumMaterialProvenance.cpp)" % sorted(uncovered)
    )


def test_two_texture_master_exposed_params_pinned_against_cpp_header():
    parsed = _header_params_for(
        "ElysiumSurfaceParamsTwoTexture", _parse_surface_params_header(_HEADER_PATH))
    assert parsed == importer.EXPOSED_PARAMS["M_V2_TwoTexture"]


def test_eyes_master_exposed_params_pinned_against_cpp_header():
    parsed = _header_params_for("ElysiumSurfaceParamsEyes", _parse_surface_params_header(_HEADER_PATH))
    assert parsed == importer.EXPOSED_PARAMS["M_V2_Eyes"]


def test_water_master_exposed_params_pinned_against_cpp_header():
    parsed = _header_params_for("ElysiumSurfaceParamsWater", _parse_surface_params_header(_HEADER_PATH))
    assert parsed == importer.EXPOSED_PARAMS["M_V2_Water"]


def test_sprite_master_exposed_params_pinned_against_cpp_header():
    parsed = _header_params_for("ElysiumSurfaceParamsSprite", _parse_surface_params_header(_HEADER_PATH))
    assert parsed == importer.EXPOSED_PARAMS["M_V2_Sprite"]


def test_refract_master_exposed_params_pinned_against_cpp_header():
    parsed = _header_params_for("ElysiumSurfaceParamsRefract", _parse_surface_params_header(_HEADER_PATH))
    assert parsed == importer.EXPOSED_PARAMS["M_V2_Refract"]


def test_decal_master_exposed_params_pinned_against_cpp_header():
    parsed = _header_params_for("ElysiumSurfaceParamsDecal", _parse_surface_params_header(_HEADER_PATH))
    assert parsed == importer.EXPOSED_PARAMS["M_V2_Decal"]


# --- UNIT_DIVERGENCES stays honest against the real corpus -----------------------------------------


def test_unit_divergences_key_every_real_unit_and_are_actually_used():
    """Every `UNIT_DIVERGENCES` key names a real corpus unit, and every one of its per-unit VMT
    keys was actually consumed (recorded in that unit's own `omissions`) the last time the real
    corpus was staged -- an entry that no longer matches a shipped unit, or whose key the unit no
    longer authors, is a stale allowlist row hiding a real regression instead of a real divergence.
    Skipped outright when the export corpus (or its staged manifest) is not present locally."""

    try:
        export_v2_root = paths.export_v2_root()
    except Exception:
        pytest.skip("no export_v2 root configured")
    if not importer.unit_root(export_v2_root).is_dir():
        pytest.skip("material corpus not exported locally")

    corpus_keys = {
        importer.unit_key(export_v2_root, unit) for unit in importer.units(export_v2_root)
    }
    missing_units = sorted(set(importer.UNIT_DIVERGENCES) - corpus_keys)
    assert not missing_units, (
        "UNIT_DIVERGENCES names unit(s) not in the real corpus: %r" % missing_units
    )

    staging_root = importer.staging_root(paths.work_root())
    manifest_path = staging_root / importer.MANIFEST_NAME
    if not manifest_path.is_file():
        pytest.skip("material corpus not staged locally -- run `uv run elysium import materials`")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    by_asset_path = {entry["assetPath"]: entry for entry in manifest["assets"]}

    for key, divergent_keys in importer.UNIT_DIVERGENCES.items():
        try:
            asset_path = importer.asset_path_for(key)
        except importer.MaterialImportError:
            continue
        entry = by_asset_path.get(asset_path)
        if entry is None:
            continue  # covered by the missing_units assertion above
        provenance_path = staging_root / entry["provenance"]
        if not provenance_path.is_file():
            continue
        provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
        used_keys = {
            str(row.get("key") or "").lower()
            for row in provenance.get("omissions") or ()
            if row.get("kind") == "unitDivergenceProvenanceOnly"
        }
        stale = sorted(set(divergent_keys) - used_keys)
        assert not stale, (
            "UNIT_DIVERGENCES[%r] names key(s) not recorded as unitDivergenceProvenanceOnly in "
            "the real staged run: %r" % (key, stale)
        )


# --- UseBaseTexture: NoTexture programs and no-$basetexture units get it explicitly false ----------


def test_use_base_texture_is_true_when_basetexture_is_bound_and_no_notexture_program(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "brick/plain", _unit(
        "brick/plain",
        parameters=[_param(0, "$basetexture", "brick/plain")],
        dependencies=[_texture_dep("$basetexture", "brick/plain")],
        programs=[{"pixelShader": "lightmappedgeneric_envmap_ps11", "vertexShader": "v",
                  "condition": "", "drawPass": 0}],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/brick/MI_plain"]
    assert entry["switches"]["UseBaseTexture"] is True


def test_use_base_texture_is_false_with_no_basetexture_authored(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "brick/notex", _unit("brick/notex", parameters=[]))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/brick/MI_notex"]
    assert entry["switches"]["UseBaseTexture"] is False


def test_use_base_texture_is_false_when_the_resolved_program_is_a_notexture_variant(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "brick/degenerate", _unit(
        "brick/degenerate",
        parameters=[_param(0, "$basetexture", "brick/degenerate")],
        dependencies=[_texture_dep("$basetexture", "brick/degenerate")],
        programs=[{"pixelShader": "lightmappedgeneric_notexture", "vertexShader": "v",
                  "condition": "", "drawPass": 0}],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/brick/MI_degenerate"]
    assert entry["switches"]["UseBaseTexture"] is False
    # BaseTexture is still recorded (provenance/reproduction of the binding), only the switch
    # that would sample it stays off.
    assert entry["textures"]["BaseTexture"] == "/ElysiumBaked/Textures/brick/T_degenerate"


def test_use_base_texture_defaults_false_on_water_and_refract_masters(tmp_path):
    """Water and Refract expose `UseBaseTexture` too, and it resolves the same way as Lit/Unlit
    (review finding 4's second half): most water/refract units bind no `$basetexture` at all, so
    the switch should read `False`, not silently vanish from the switches dict."""
    export = tmp_path / "v2"
    _publish(export, "water/plain", _unit("water/plain", shader="water", parameters=[]))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/water/MI_plain"]
    assert entry["parent"] == f"{importer.MASTER_ROOT}/M_V2_Water"
    assert entry["switches"]["UseBaseTexture"] is False


# --- table-driven texture-slot / paired-switch audit (review finding 6) ----------------------------


def test_every_bound_texture_slot_with_a_paired_switch_sets_it(tmp_path):
    """`NormalMap`/`BaseTexture2`/`CloudAlphaTexture`, each bound on a master that also exposes
    its paired switch, must set that switch -- the same "bound texture with nothing gating it"
    defect finding 6 named for `NormalMap` (294 unsampled normal maps). One unit per pair,
    exercised through the real stage rather than calling the private helper directly."""
    export = tmp_path / "v2"
    _publish(export, "brick/bumped", _unit(
        "brick/bumped",
        parameters=[_param(0, "$basetexture", "brick/bumped"),
                    _param(1, "$bumpmap", "brick/bumped_bump")],
        dependencies=[_texture_dep("$basetexture", "brick/bumped"),
                      _texture_dep("$bumpmap", "brick/bumped_bump")],
    ))
    _publish(export, "world/blend", _unit(
        "world/blend", shader="worldvertextransition",
        parameters=[_param(0, "$basetexture", "world/blend"),
                    _param(1, "$basetexture2", "world/blend2")],
        dependencies=[_texture_dep("$basetexture", "world/blend"),
                      _texture_dep("$basetexture2", "world/blend2")],
    ))
    _publish(export, "sky/cloudy", _unit(
        "sky/cloudy", shader="cloud",
        parameters=[_param(0, "$basetexture", "sky/cloudy"),
                    _param(1, "$cloudalphatexture", "sky/cloudy_alpha")],
        dependencies=[_texture_dep("$basetexture", "sky/cloudy"),
                      _texture_dep("$cloudalphatexture", "sky/cloudy_alpha")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entries = _entries(tmp_path / "stage")

    for asset_path, texture_name, switch_name in (
        ("/ElysiumBaked/Materials/brick/MI_bumped", "NormalMap", "UseNormalMap"),
        ("/ElysiumBaked/Materials/world/MI_blend", "BaseTexture2", "UseBaseTexture2"),
        ("/ElysiumBaked/Materials/sky/MI_cloudy", "CloudAlphaTexture", "UseCloudAlpha"),
    ):
        entry = entries[asset_path]
        assert texture_name in entry["textures"], asset_path
        assert entry["switches"].get(switch_name) is True, (asset_path, switch_name)


def test_texture_switch_pairs_table_only_names_exposed_switches():
    """Every switch `_TEXTURE_SWITCH_PAIRS` names is a real switch on at least one master's
    `EXPOSED_PARAMS`, and every texture it pairs one with is real too -- a typo in the table would
    otherwise silently never fire (`_apply_texture_switch_pairs` only acts when both the texture is
    bound *and* the switch is in that master's own exposed set)."""
    all_switches = {name for exposed in importer.EXPOSED_PARAMS.values()
                    for name, kind in exposed.items() if kind == "#"}
    all_textures = {name for exposed in importer.EXPOSED_PARAMS.values()
                    for name, kind in exposed.items() if kind == "T"}
    for texture_name, switch_name in importer._TEXTURE_SWITCH_PAIRS.items():
        assert texture_name in all_textures, texture_name
        assert switch_name in all_switches, switch_name


# --- review finding 5: required texture slots -------------------------------------------------------


def test_basetexture_class_mismatch_on_a_real_master_fails_the_unit(tmp_path):
    """A `$basetexture` that resolves but staged as the wrong class (a cubemap where a plain
    `Texture2D` is wanted) leaves `BaseTexture` unbound on every real master -- the master's only
    colour source -- so this is a per-unit stage failure, not merely a `textureClassMismatch`
    anomaly (review finding 5)."""
    export = tmp_path / "v2"
    texture_staging = tmp_path / "texture-stage"
    sidecar = texture_staging / "sprites" / ("muzzleflash" + importer.TEXTURE_PROVENANCE_SUFFIX)
    sidecar.parent.mkdir(parents=True, exist_ok=True)
    sidecar.write_text(json.dumps({"faces": 6}), encoding="utf-8")  # staged as a TextureCube
    _publish(export, "sprites/muzzleflash", _unit(
        "sprites/muzzleflash", shader="sprite",
        parameters=[_param(0, "$basetexture", "sprites/muzzleflash")],
        dependencies=[_texture_dep("$basetexture", "sprites/muzzleflash")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage", texture_staging_root=texture_staging)
    assert len(result.failures) == 1
    key, reason = result.failures[0]
    assert key == "sprites/muzzleflash"
    assert "BaseTexture" in reason
    assert "/ElysiumBaked/Materials/sprites/MI_muzzleflash" not in _entries(tmp_path / "stage")


def test_basetexture_class_mismatch_on_an_optional_slot_stays_an_anomaly(tmp_path):
    """The same mismatch on an optional slot (`EnvMapMask`) is recorded and the unit still
    stages -- only a *required* slot escalates to a failure."""
    export = tmp_path / "v2"
    texture_staging = tmp_path / "texture-stage"
    sidecar = texture_staging / "brick" / ("wall_ref" + importer.TEXTURE_PROVENANCE_SUFFIX)
    sidecar.parent.mkdir(parents=True, exist_ok=True)
    sidecar.write_text(json.dumps({"faces": 6}), encoding="utf-8")
    _publish(export, "brick/wall", _unit(
        "brick/wall",
        parameters=[_param(0, "$basetexture", "brick/wall"),
                    _param(1, "$envmapmask", "brick/wall_ref")],
        dependencies=[_texture_dep("$basetexture", "brick/wall"),
                      _texture_dep("$envmapmask", "brick/wall_ref")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage", texture_staging_root=texture_staging)
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/brick/MI_wall"]
    assert "EnvMapMask" not in entry["textures"]
    provenance = _provenance(tmp_path / "stage", entry)
    assert any(row["kind"] == "textureClassMismatch" and row["parameter"] == "EnvMapMask"
              for row in provenance["anomalies"])


# --- static frame-0 fallback for a multi-frame BaseTexture/NormalMap --------------------------------


def test_basetexture_multiframe_array_binds_as_static_frame_zero_on_lit(tmp_path):
    """`$basetexture` resolving to a unit that staged as a `Texture2DArray` (`frames > 1`) is not a
    dead end on a master that exposes the `BaseTextureFrames` lane (Lit here): Source itself draws
    frame 0 of a multi-frame texture when nothing animates it, so the stage binds the array
    statically -- `FrameRate=0`, `FrameCount` from the sidecar, `UseAnimatedFrames`/`UseBaseTexture`
    both on -- instead of failing the unit."""
    export = tmp_path / "v2"
    texture_staging = tmp_path / "texture-stage"
    sidecar = texture_staging / "sprites" / ("mflash_colt" + importer.TEXTURE_PROVENANCE_SUFFIX)
    sidecar.parent.mkdir(parents=True, exist_ok=True)
    sidecar.write_text(json.dumps({"frames": 6}), encoding="utf-8")  # staged as a Texture2DArray
    _publish(export, "sprites/mflash_colt", _unit(
        "sprites/mflash_colt",
        parameters=[_param(0, "$basetexture", "sprites/mflash_colt")],
        dependencies=[_texture_dep("$basetexture", "sprites/mflash_colt")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage", texture_staging_root=texture_staging)
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/sprites/MI_mflash_colt"]
    assert entry["textures"]["BaseTextureFrames"] == "/ElysiumBaked/Textures/sprites/TA_mflash_colt"
    assert "BaseTexture" not in entry["textures"]
    assert entry["scalars"]["FrameCount"] == 6.0
    assert entry["scalars"]["FrameRate"] == 0.0
    assert entry["switches"]["UseAnimatedFrames"] is True
    assert entry["switches"]["UseBaseTexture"] is True
    provenance = _provenance(tmp_path / "stage", entry)
    assert any(row["kind"] == "textureClassMismatch" and row["parameter"] == "BaseTexture"
              for row in provenance["anomalies"])
    assert not any(row.get("kind") == "staticFrameOffsetUnsupported" for row in provenance["omissions"])


def test_basetexture_multiframe_array_records_divergence_when_frame_is_authored_nonzero(tmp_path):
    """An authored `$frame` other than the default (0) is a real divergence from the frame the
    static fallback samples -- recorded as `staticFrameOffsetUnsupported`, never silently honoured
    or silently dropped."""
    export = tmp_path / "v2"
    texture_staging = tmp_path / "texture-stage"
    sidecar = texture_staging / "sprites" / ("mflash_colt" + importer.TEXTURE_PROVENANCE_SUFFIX)
    sidecar.parent.mkdir(parents=True, exist_ok=True)
    sidecar.write_text(json.dumps({"frames": 6}), encoding="utf-8")
    _publish(export, "sprites/mflash_colt", _unit(
        "sprites/mflash_colt",
        parameters=[_param(0, "$basetexture", "sprites/mflash_colt"), _param(1, "$frame", "2")],
        dependencies=[_texture_dep("$basetexture", "sprites/mflash_colt")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage", texture_staging_root=texture_staging)
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/sprites/MI_mflash_colt"]
    assert entry["switches"]["UseAnimatedFrames"] is True
    provenance = _provenance(tmp_path / "stage", entry)
    assert any(
        row.get("kind") == "staticFrameOffsetUnsupported" and row.get("parameter") == "BaseTexture"
        and row.get("value") == "2"
        for row in provenance["omissions"]
    )


def test_basetexture_multiframe_array_stays_a_failure_on_water(tmp_path):
    """Water exposes no `BaseTextureFrames` lane, so the same `frames > 1` mismatch on its
    `BaseTexture` (a required slot) keeps failing the unit exactly as before -- the static frame-0
    fallback never applies where the master has no lane to bind onto."""
    export = tmp_path / "v2"
    texture_staging = tmp_path / "texture-stage"
    sidecar = texture_staging / "dev" / ("ocean" + importer.TEXTURE_PROVENANCE_SUFFIX)
    sidecar.parent.mkdir(parents=True, exist_ok=True)
    sidecar.write_text(json.dumps({"frames": 4}), encoding="utf-8")
    _publish(export, "dev/ocean", _unit(
        "dev/ocean", shader="water",
        parameters=[_param(0, "$basetexture", "dev/ocean")],
        dependencies=[_texture_dep("$basetexture", "dev/ocean")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage", texture_staging_root=texture_staging)
    assert len(result.failures) == 1
    key, reason = result.failures[0]
    assert key == "dev/ocean"
    assert "BaseTexture" in reason


def test_normalmap_multiframe_array_binds_as_static_frame_zero_on_lit(tmp_path):
    """The same fallback applies to `NormalMap` -> `NormalMapFrames` on a master that exposes that
    lane (Lit); `NormalMap` is only an optional slot, so before this fallback the mismatch would
    already have staged (as an anomaly, `BaseTexture` still bound) -- this asserts the array itself
    is now bound rather than the parameter left at the master's inert default."""
    export = tmp_path / "v2"
    texture_staging = tmp_path / "texture-stage"
    sidecar = texture_staging / "brick" / ("wall_bump" + importer.TEXTURE_PROVENANCE_SUFFIX)
    sidecar.parent.mkdir(parents=True, exist_ok=True)
    sidecar.write_text(json.dumps({"frames": 3}), encoding="utf-8")
    _publish(export, "brick/wall", _unit(
        "brick/wall",
        parameters=[_param(0, "$basetexture", "brick/wall"), _param(1, "$bumpmap", "brick/wall_bump")],
        dependencies=[_texture_dep("$basetexture", "brick/wall"),
                      _texture_dep("$bumpmap", "brick/wall_bump")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage", texture_staging_root=texture_staging)
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/brick/MI_wall"]
    assert entry["textures"]["NormalMapFrames"] == "/ElysiumBaked/Textures/brick/TA_wall_bump"
    assert "NormalMap" not in entry["textures"]
    assert entry["scalars"]["NormalFrameCount"] == 3.0
    assert entry["scalars"]["NormalFrameRate"] == 0.0
    assert entry["switches"]["UseAnimatedNormalFrames"] is True
    assert entry["switches"]["UseNormalMap"] is True


# --- review finding 2: full-state switches ------------------------------------------------------------


def test_all_switches_lists_every_switch_name_the_master_exposes(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "brick/wall", _unit(
        "brick/wall",
        parameters=[_param(0, "$basetexture", "brick/wall")],
        dependencies=[_texture_dep("$basetexture", "brick/wall")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/brick/MI_wall"]
    expected = sorted(name for name, kind in importer.EXPOSED_PARAMS["M_V2_Lit"].items() if kind == "#")
    assert entry["allSwitches"] == expected
    assert sorted(entry["switches"]) == expected
    assert entry["recipe"]["params"]["allSwitches"] == expected


def test_patched_instance_carries_no_switches_or_all_switches(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "glass/pane", _unit(
        "glass/pane", shader="vertexlitgeneric",
        parameters=[_param(0, "$basetexture", "glass/pane")],
        dependencies=[_texture_dep("$basetexture", "glass/pane")],
    ))
    _publish(export, "maps/ch_cloud_1/glass/pane", _unit(
        "maps/ch_cloud_1/glass/pane", shader="patch", family="patch", resolved=False,
        parameters=[_param(0, "include", "glass/pane")],
        patch={"include": "glass/pane", "asset": "vtmb:material:glass/pane", "operations": []},
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    patched = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/maps/ch_cloud_1/glass/MI_pane"]
    assert patched["allSwitches"] == []


# --- review finding 7: patched instances stamp the root master ---------------------------------------


def test_patched_instance_provenance_master_walks_to_the_root_base(tmp_path):
    """A patched unit's own provenance `master` used to be `null` (`ElysiumMaterialProvenance`'s
    `Master`/`ElysiumMaster` registry tag then stamps empty) -- walked to the base unit's own
    master instead, so the Content Browser filter covers a patched instance too."""
    export = tmp_path / "v2"
    _publish(export, "glass/pane", _unit(
        "glass/pane", shader="vertexlitgeneric",
        parameters=[_param(0, "$basetexture", "glass/pane")],
        dependencies=[_texture_dep("$basetexture", "glass/pane")],
    ))
    _publish(export, "maps/ch_cloud_1/glass/pane", _unit(
        "maps/ch_cloud_1/glass/pane", shader="patch", family="patch", resolved=False,
        parameters=[_param(0, "include", "glass/pane")],
        patch={"include": "glass/pane", "asset": "vtmb:material:glass/pane", "operations": []},
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entries = _entries(tmp_path / "stage")
    patched = entries["/ElysiumBaked/Materials/maps/ch_cloud_1/glass/MI_pane"]
    provenance = _provenance(tmp_path / "stage", patched)
    assert provenance["master"] == f"{importer.MASTER_ROOT}/M_V2_Lit"


# --- review finding 9: sourceMembersSha256 --------------------------------------------------------


def test_source_members_sha256_is_role_and_order_sensitive(tmp_path):
    export = tmp_path / "v2"

    def _document_with_members(members):
        document = _unit(
            "brick/wall",
            parameters=[_param(0, "$basetexture", "brick/wall")],
            dependencies=[_texture_dep("$basetexture", "brick/wall")],
        )
        document["sourceResolution"] = {"policy": "up-first", "members": members}
        return document

    a = [{"role": "vmt", "sha256": "aa"}, {"role": "psh", "sha256": "bb"}]
    b = [{"role": "psh", "sha256": "bb"}, {"role": "vmt", "sha256": "aa"}]  # reordered
    c = [{"role": "vmt", "sha256": "bb"}, {"role": "psh", "sha256": "aa"}]  # roles swapped

    _publish(export, "brick/wall", _document_with_members(a))
    result_a = importer.stage_materials(export, tmp_path / "stage-a")
    assert result_a.failures == []
    entry_a = _entries(tmp_path / "stage-a")["/ElysiumBaked/Materials/brick/MI_wall"]

    _publish(export, "brick/wall", _document_with_members(b))
    result_b = importer.stage_materials(export, tmp_path / "stage-b")
    entry_b = _entries(tmp_path / "stage-b")["/ElysiumBaked/Materials/brick/MI_wall"]

    _publish(export, "brick/wall", _document_with_members(c))
    result_c = importer.stage_materials(export, tmp_path / "stage-c")
    entry_c = _entries(tmp_path / "stage-c")["/ElysiumBaked/Materials/brick/MI_wall"]

    assert entry_a["sourceMembersSha256"] == hashlib.sha256(b"vmt:aa\npsh:bb").hexdigest()
    assert entry_a["sourceMembersSha256"] != entry_b["sourceMembersSha256"]
    assert entry_a["sourceMembersSha256"] != entry_c["sourceMembersSha256"]


# --- review finding 4: recipe covers the sidecar and the physical material -------------------------


def test_recipe_covers_provenance_bytes_and_phys_material(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "brick/wall", _unit(
        "brick/wall",
        parameters=[_param(0, "$basetexture", "brick/wall"), _param(1, "$surfaceprop", "brick")],
        dependencies=[_texture_dep("$basetexture", "brick/wall")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/brick/MI_wall"]
    provenance = _provenance(tmp_path / "stage", entry)
    expected_bytes = (json.dumps(provenance, indent=1, sort_keys=True) + "\n").encode("utf-8")
    assert entry["recipe"]["provenanceSha256"] == hashlib.sha256(expected_bytes).hexdigest()
    assert entry["recipe"]["physMaterial"] == entry["physMaterial"] == "/ElysiumBaked/SurfaceProperties/PM_brick"


def test_a_provenance_only_change_bumps_the_recipe_fingerprint():
    """The policy sentence: any change to what the instance or its provenance carries bumps the
    recipe hash, even when not one bound parameter changed. `$curve` lands only in provenance
    (`misc_provenance`), never in `params`, so this isolates the sidecar-hash coverage."""
    without_curve = _unit(
        "brick/wall", parameters=[_param(0, "$basetexture", "brick/wall")],
        dependencies=[_texture_dep("$basetexture", "brick/wall")],
    )
    with_curve = _unit(
        "brick/wall",
        parameters=[_param(0, "$basetexture", "brick/wall"), _param(1, "$curve", "1.5")],
        dependencies=[_texture_dep("$basetexture", "brick/wall")],
    )
    entry_without, _ = importer.stage_unit("brick/wall", without_curve, "0" * 64)
    entry_with, _ = importer.stage_unit("brick/wall", with_curve, "0" * 64)
    assert entry_without["recipe"]["provenanceSha256"] != entry_with["recipe"]["provenanceSha256"]
    # And nothing bound actually changed:
    assert entry_without["recipe"]["params"] == entry_with["recipe"]["params"]


# --- review finding 5: anomaly/omission rollup --------------------------------------------------------


def test_anomaly_rollup_counts_by_kind_in_manifest_and_summary(tmp_path):
    export = tmp_path / "v2"
    texture_staging = tmp_path / "texture-stage"
    sidecar = texture_staging / "brick" / ("wall_ref" + importer.TEXTURE_PROVENANCE_SUFFIX)
    sidecar.parent.mkdir(parents=True, exist_ok=True)
    sidecar.write_text(json.dumps({"faces": 6}), encoding="utf-8")
    _publish(export, "brick/wall", _unit(
        "brick/wall",
        parameters=[_param(0, "$basetexture", "brick/wall"),
                    _param(1, "$envmapmask", "brick/wall_ref")],
        dependencies=[_texture_dep("$basetexture", "brick/wall"),
                      _texture_dep("$envmapmask", "brick/wall_ref")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage", texture_staging_root=texture_staging)
    assert result.failures == []
    assert result.anomaly_counts.get("textureClassMismatch") == 1
    assert "textureClassMismatch=1" in result.summary()
    manifest = _manifest(tmp_path / "stage")
    assert manifest["anomalyCounts"]["textureClassMismatch"] == 1
