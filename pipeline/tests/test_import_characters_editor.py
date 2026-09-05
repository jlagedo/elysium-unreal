"""Native character orchestration: precise owner inventory, hash guards and partial reuse."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace as NS
from unittest import mock

import pytest

from elysium_pipeline.asset_paths import baked_unit


@pytest.fixture
def editor(monkeypatch, tmp_path):
    assets, calls = {}, []
    asset_id = "vtmb:model:test/body"
    def author_clips(source, package, skeleton, role, **kwargs):
        calls.append("clips")
        for name in ("left", "right"):
            path = baked_unit(asset_id, "A", label=name, role=role or None)
            assets[path] = ""
        return "", 2, 0, 0, []
    def author_blends(table, package, skeleton, role, **kwargs):
        calls.append("blends")
        assets[baked_unit(asset_id, "BS", label="walk", role=role or None)] = ""
        return "", 1, 0, 0
    def author_skeleton(sources, path, *args, **kwargs):
        calls.append("skeleton")
        assets[path] = ""
        return "", 1
    lib = NS(build_anim_sequences_from_stage=author_clips, build_blend_spaces_from_stage=author_blends,
             build_family_skeleton=author_skeleton, declare_compatible_skeletons=lambda *a, **k: "")
    fake = NS(ElysiumSkeletalBuildLibrary=lib,
              ElysiumMapBakeLibrary=NS(finish_asset_compilation=lambda: None, unload_baked_packages=lambda *a: None),
              Paths=NS(project_dir=lambda: str(tmp_path)),
              AssetRegistryHelpers=NS(get_asset_registry=lambda: NS(scan_paths_synchronous=lambda *a, **k: None)),
              ElysiumCharacterProvenance=NS(apply_json=lambda *a: (NS(), "")),
              ElysiumClipData=NS(apply_json=lambda *a: (NS(), "")),
              load_asset=lambda p: p if p in assets else None, log=lambda *a: None, log_warning=lambda *a: None,
              SystemLibrary=NS(get_command_line=lambda: 'editor "-ImportCharacters=C:/space here/manifest.json" -ImportForce=1'))
    bl = NS(recipe_fingerprint=lambda *a: hashlib.sha256(json.dumps(a, sort_keys=True).encode()).hexdigest(),
            stored_recipe=lambda p, **k: assets.get(p, ""),
            stamp_recipe=lambda p, digest, **k: assets.__setitem__(p, digest), save=lambda p: True,
            prune_owned=lambda *a: 0)
    path = Path(__file__).resolve().parents[1] / "unreal/import_characters.py"
    spec = importlib.util.spec_from_file_location("character_editor_test", path)
    module = importlib.util.module_from_spec(spec)
    # Keep the editor package isolated too: leaving it in sys.modules would make later
    # fake-editor tests reuse its bake_lib attribute and write into a different fake registry.
    with mock.patch.dict(sys.modules, {"unreal": fake, "pipeline.unreal": NS(_bootstrap=NS(), bake_lib=bl)}):
        spec.loader.exec_module(module)
    monkeypatch.setattr(module.eskm, "clip_payloads", lambda _: [NS(name=n, flags=0, base="") for n in ("left", "right")])
    monkeypatch.setattr(module.eskm, "bones", lambda _: [("root", -1)])
    dll = tmp_path / "Binaries/Win64/UnrealEditor-ElysiumUE.dll"
    dll.parent.mkdir(parents=True)
    dll.write_bytes(b"test-tool")
    return NS(module=module, root=tmp_path, id=asset_id, assets=assets, calls=calls)


def body():
    return {"grids": {"walk": {"cells": [{"axis": [0, 0], "clip": "left"}, {"axis": [1, 0], "clip": "right"}]}},
            "sequences": [], "sourceSemantics": {"mdl": {"poseParameters": [], "includeModels": []},
                                                  "facial": None, "physics": None, "cloth": None, "procedural": None}}


def stage(editor):
    payload = editor.root / "body.skel"
    payload.write_bytes(b"fixture")
    content = json.dumps(body()).encode()
    (editor.root / "body.json").write_bytes(content)
    entry = {"assetId": editor.id, "payload": payload.name, "body": "body.json", "meshAsset": None,
             "skeletonAsset": baked_unit(editor.id, "SKEL"), "animationSkeletonAsset": baked_unit(editor.id, "SKEL"),
             "recipe": {"unitSha256": "unit", "payloadSha256": hashlib.sha256(b"fixture").hexdigest(),
                        "bodySha256": hashlib.sha256(content).hexdigest()}}
    metadata = json.dumps({"clips": {name: {} for name in ("left", "right")}, "blendSpaces": {"walk": {}}}).encode()
    (editor.root / "clips.json").write_bytes(metadata)
    entry.update(clipData="clips.json", clipDataSha256=hashlib.sha256(metadata).hexdigest())
    manifest = {"producer": "characters", "packageRoot": "/ElysiumBaked/Models", "stageFailures": [],
                "selectedUnits": [editor.id], "assets": [entry], "bankPartition": {"families": []}}
    path = editor.root / "manifest.json"
    path.write_text(json.dumps(manifest))
    return path


def test_missing_blend_product_rebuilds_even_when_all_clips_are_current(editor):
    manifest = stage(editor)
    result = editor.module.run(manifest, editor.root)
    assert result["failed"] == []
    assert editor.calls == ["skeleton", "clips", "blends"]
    editor.calls.clear()
    del editor.assets[baked_unit(editor.id, "BS", label="walk")]
    result = editor.module.run(manifest, editor.root)
    assert result["failed"] == []
    assert editor.calls == ["blends"]


def test_tampered_payload_is_refused_before_native_build(editor):
    manifest = stage(editor)
    (editor.root / "body.skel").write_bytes(b"altered")
    result = editor.module.run(manifest, editor.root)
    assert "digest mismatch" in result["failed"][0]["reason"]
    assert editor.calls == []


def test_actor_products_keep_their_role_and_report_missing_cells(editor):
    b = body()
    b["sequences"] = [{"label": "host", "autolayers": ["@walk"]}]
    clips = [NS(name="left@host"), NS(name="right@host")]
    _, expected, omissions = editor.module.blend_plan(editor.id, b, clips, "actor")
    assert expected == [baked_unit(editor.id, "BS", label="walk@host", role="actor")]
    assert omissions == []
    _, expected, omissions = editor.module.blend_plan(editor.id, b, clips[:1], "actor")
    assert expected == []
    assert omissions[0]["skippedGrid"]
    assert omissions[0]["missingCells"][0]["clip"] == "right"


def test_stage_paths_cannot_escape_and_command_line_preserves_spaces(editor):
    with pytest.raises(RuntimeError, match="escapes"):
        editor.module._check_file(editor.root, "../elsewhere", "digest")
    assert editor.module.argument("ImportCharacters") == "C:/space here/manifest.json"
    assert editor.module.argument("ImportForce") == "1"


def test_reference_quaternion_bypasses_the_float32_make_struct_constructor(editor, monkeypatch):
    class Quat:
        def __init__(self, *args):
            assert not args, "MakeQuat narrows constructor arguments to float32"
            self.fields = {}

        def set_editor_property(self, field, value):
            self.fields[field] = value

    monkeypatch.setattr(editor.module.unreal, "Quat", Quat, raising=False)
    monkeypatch.setattr(editor.module.unreal, "Transform", NS, raising=False)
    monkeypatch.setattr(editor.module.unreal, "Vector", lambda *v: v, raising=False)
    values = [.123456789012345, -.234567890123456, .345678901234567, .901234567890123]
    transform = editor.module._transform({"position": [1., 2., 3.], "rotation": values})
    assert list(transform.rotation.fields.values()) == values
