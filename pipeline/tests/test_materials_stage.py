"""Contract tests for `uv run elysium import materials` (the offline stage phase, SF-4.4).

Every fixture is a synthetic material unit built directly as an `ELYSIUM_vtmb_material` extension
body and encoded with `encode_glb` -- never the real install, never a real staging tree -- the way
`test_texture_corpus_import.py` fabricates texture units.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

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
) -> dict:
    family = family if family is not None else shader
    return {
        "schemaVersion": "1.1.0",
        "identity": {"asset": f"vtmb:material:{key}", "materialPath": key},
        "shader": shader,
        "sourceShader": shader,
        "shaderResolution": {"family": family, "resolved": resolved, "programs": [], "inputs": [],
                             "reason": ""},
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
    assert entry["basePropertyOverrides"] == {"blendMode": "Opaque"}


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
    assert entry["basePropertyOverrides"] == {"blendMode": "Translucent"}


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
    assert entry["basePropertyOverrides"] == {"blendMode": "Masked", "opacityMaskClipValue": 0.5}


# --- envmap shapes ----------------------------------------------------------------------------------


def test_env_cubemap_symbol_binds_no_texture_and_is_recorded_as_runtime(tmp_path):
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
    assert provenance["environment"]["runtimeBind"] == "env_cubemap"
    assert any(row["kind"] == "envmap" for row in provenance["runtime"])


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


def test_unrecognised_surfaceprop_value_falls_back_to_default(tmp_path):
    export = tmp_path / "v2"
    _publish(export, "misc3/cloth", _unit(
        "misc3/cloth",
        parameters=[
            _param(0, "$basetexture", "misc3/cloth"),
            _param(1, "$surfaceprop", "cloth"),  # not a class-table entry
        ],
        dependencies=[_texture_dep("$basetexture", "misc3/cloth")],
    ))
    result = importer.stage_materials(export, tmp_path / "stage")
    assert result.failures == []
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Materials/misc3/MI_cloth"]
    assert entry["surfaceClass"] == "default"


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
    assert entry["scalars"]["WetnessScale"] == 0.56
    provenance = _provenance(tmp_path / "stage", entry)
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
    # Every install $surfaceprop entry this corpus stages against is in the table.
    for name in ("brick", "metal", "wood", "water", "glass", "carpet"):
        assert name in importer.SURFACE_CLASS_INDEX
