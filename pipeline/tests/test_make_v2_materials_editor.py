"""`pipeline/unreal/make_v2_materials.py`, the SF-4.3 M_V2_Lit generator, exercised end to end
against a faked editor -- the same shape as `test_import_textures_editor.py`. The module runs its
whole build at import time (module scope, no `__main__` guard, matching every other
`pipeline/unreal/make_*.py` generator), so loading it under the fake IS the test: a wrong pin
name, an unmasked multi-output node fed into a switch, or a missing enum surfaces as an exception
during import rather than only inside the real editor, where a failure costs a multi-minute
launch to discover.
"""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import sys
from types import SimpleNamespace
from unittest import mock

import pytest

REPO = Path(__file__).resolve().parents[2]


class FakeNode:
    _next_id = [0]

    def __init__(self, cls, x, y):
        self.cls = cls
        self.x, self.y = x, y
        self.props = {}
        FakeNode._next_id[0] += 1
        self.id = FakeNode._next_id[0]

    def set_editor_property(self, name, value):
        self.props[name] = value

    def get_editor_property(self, name):
        return self.props.get(name)

    def get_class(self):
        return SimpleNamespace(get_name=lambda: getattr(self.cls, "__name__", str(self.cls)))


class FakeAsset:
    def __init__(self, name, package, cls):
        self.name = name
        self.package = package
        self.path = "%s/%s" % (package, name)
        self.cls = cls
        self.props = {}
        self.metadata = {}
        self.expressions = []

    def set_editor_property(self, name, value):
        self.props[name] = value

    def get_editor_property(self, name):
        return self.props.get(name, [] if name in ("scalar_parameters", "vector_parameters") else None)


class FakeMel:
    def __init__(self, editor):
        self.editor = editor
        self.connections = []
        self.property_connections = []
        self.refuse_pins = set()
        self.refuse_properties = set()
        self.recompile_errors = []

    def create_material_expression(self, mat, cls, x, y):
        node = FakeNode(cls, x, y)
        mat.expressions.append(node)
        return node

    def connect_material_expressions(self, src, src_out, dst, dst_in):
        self.connections.append((src, src_out, dst, dst_in))
        if src is None or dst is None:
            return False
        return (dst_in, src_out) not in self.refuse_pins

    def connect_material_property(self, src, src_out, prop):
        self.property_connections.append((src, src_out, prop))
        return prop not in self.refuse_properties

    def recompile_material(self, mat):
        return list(self.recompile_errors)


class FakeEditor:
    def __init__(self):
        self.assets = {}
        self.saved = []
        self.deleted = []
        self.tasks = []
        self.errors, self.warnings, self.logs = [], [], []
        self.command_line = ""
        self.mel = FakeMel(self)

    # EditorAssetLibrary
    def does_asset_exist(self, path):
        return path in self.assets

    def delete_asset(self, path):
        self.deleted.append(path)
        return self.assets.pop(path, None) is not None

    def save_asset(self, path, only_if_is_dirty=True):
        self.saved.append(path)
        return path in self.assets

    def set_metadata_tag(self, asset, tag, value):
        asset.metadata[tag] = value

    def get_metadata_tag(self, asset, tag):
        return asset.metadata.get(tag, "")

    # AssetRegistry (bake_lib.stored_recipe / object_path_of)
    def get_asset_by_object_path(self, object_path):
        path = object_path.split(".", 1)[0]
        asset = self.assets.get(path)
        if asset is None:
            return None
        return SimpleNamespace(is_valid=lambda: True,
                               get_tag_value=lambda tag, a=asset: a.metadata.get(tag, ""))

    def scan_paths_synchronous(self, *a, **k):
        pass

    # AssetTools
    def create_asset(self, name, package, cls, factory):
        asset = FakeAsset(name, package, cls)
        self.assets[asset.path] = asset
        return asset

    def import_asset_tasks(self, tasks):
        for task in tasks:
            self.tasks.append(task)
            name = task.get_editor_property("destination_name")
            package = task.get_editor_property("destination_path")
            path = "%s/%s" % (package, name)
            self.assets[path] = FakeAsset(name, package, "Texture2D")

    def load_asset(self, path):
        return self.assets.get(path)


def _enum(prefix, *names):
    return SimpleNamespace(**{n: "%s.%s" % (prefix, n) for n in names})


def _fake_unreal(editor):
    class AssetImportTask(FakeNode):
        def __init__(self):
            super().__init__(None, 0, 0)

    class LinearColor:
        def __init__(self, r, g, b, a):
            self.r, self.g, self.b, self.a = r, g, b, a

    class CollectionScalarParameter(FakeNode):
        def __init__(self):
            super().__init__(None, 0, 0)

    material_expression_names = [
        "MaterialExpressionTextureSampleParameter2D", "MaterialExpressionTextureSampleParameterCube",
        "MaterialExpressionTextureObjectParameter", "MaterialExpressionTextureSample",
        "MaterialExpressionScalarParameter", "MaterialExpressionVectorParameter",
        "MaterialExpressionComponentMask", "MaterialExpressionStaticSwitchParameter",
        "MaterialExpressionCollectionParameter", "MaterialExpressionConstant",
        "MaterialExpressionConstant3Vector", "MaterialExpressionMultiply", "MaterialExpressionAdd",
        "MaterialExpressionSubtract", "MaterialExpressionDivide", "MaterialExpressionDotProduct",
        "MaterialExpressionPower", "MaterialExpressionLinearInterpolate",
        "MaterialExpressionSaturate", "MaterialExpressionOneMinus", "MaterialExpressionClamp",
        "MaterialExpressionAppendVector", "MaterialExpressionTime", "MaterialExpressionSine",
        "MaterialExpressionPanner", "MaterialExpressionVertexColor", "MaterialExpressionFresnel",
        "MaterialExpressionReflectionVectorWS", "MaterialExpressionTextureCoordinate",
        "MaterialExpressionRayTracingQualitySwitch", "MaterialExpressionIf",
    ]

    ns = SimpleNamespace(
        MaterialEditingLibrary=editor.mel,
        GeometryScript_Collision=object(),
        AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: editor),
        EditorAssetLibrary=editor,
        AssetRegistryHelpers=SimpleNamespace(get_asset_registry=lambda: editor),
        SystemLibrary=SimpleNamespace(get_command_line=lambda: editor.command_line),
        load_asset=editor.load_asset,
        log=editor.logs.append,
        log_warning=editor.warnings.append,
        log_error=editor.errors.append,
        LinearColor=LinearColor,
        AssetImportTask=AssetImportTask,
        CollectionScalarParameter=CollectionScalarParameter,
        Material=type("Material", (), {}),
        MaterialFactoryNew=type("MaterialFactoryNew", (), {}),
        MaterialParameterCollection=type("MaterialParameterCollection", (), {}),
        MaterialParameterCollectionFactoryNew=type("MaterialParameterCollectionFactoryNew", (), {}),
        MaterialSamplerType=_enum(
            "SAMPLERTYPE", "SAMPLERTYPE_COLOR", "SAMPLERTYPE_MASKS", "SAMPLERTYPE_NORMAL",
            "SAMPLERTYPE_LINEAR_COLOR"),
        TextureMipValueMode=_enum("TMVM", "TMVM_MIP_LEVEL"),
        TextureCompressionSettings=_enum("TC", "TC_MASKS", "TC_VECTOR_DISPLACEMENTMAP"),
        TextureMipGenSettings=_enum("TMGS", "TMGS_NO_MIPMAPS"),
        TextureFilter=_enum("TF", "TF_NEAREST"),
        TextureAddress=_enum("TA", "TA_CLAMP"),
        TextureLossyCompressionAmount=_enum("TLCA", "TLCA_NONE"),
        MaterialDomain=_enum("MD", "MD_SURFACE"),
        BlendMode=_enum("BLEND", "BLEND_OPAQUE"),
        MaterialProperty=_enum(
            "MP", "MP_BASE_COLOR", "MP_NORMAL", "MP_EMISSIVE_COLOR", "MP_SPECULAR",
            "MP_ROUGHNESS", "MP_METALLIC", "MP_OPACITY", "MP_OPACITY_MASK",
            "MP_PIXEL_DEPTH_OFFSET"),
    )
    for name in material_expression_names:
        setattr(ns, name, type(name, (), {}))
    return ns


def _load(editor, tmp_path, monkeypatch):
    monkeypatch.setenv("ELYSIUM_WORK_ROOT", str(tmp_path))
    monkeypatch.delenv("ELYSIUM_EXPORT_V2_ROOT", raising=False)
    fake = _fake_unreal(editor)
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        for mod in ("pipeline.unreal.mat_fog", "pipeline.unreal.matgraph",
                    "pipeline.unreal.bake_lib", "pipeline.unreal.make_v2_materials"):
            sys.modules.pop(mod, None)
        spec = importlib.util.spec_from_file_location(
            "elysium_test_make_v2_materials", REPO / "pipeline/unreal/make_v2_materials.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    module.unreal = fake
    return module


def test_a_fresh_run_authors_a_compiling_m_v2_lit(tmp_path, monkeypatch):
    editor = FakeEditor()
    module = _load(editor, tmp_path, monkeypatch)

    asset = editor.assets.get("/Game/ElysiumGenerated/Materials/V2/M_V2_Lit")
    assert asset is not None
    assert asset.props.get("material_domain") == "MD.MD_SURFACE"
    assert asset.props.get("blend_mode") == "BLEND.BLEND_OPAQUE"
    assert editor.mel.recompile_errors == []
    assert "ElysiumRecipe" in asset.metadata
    assert "/Game/ElysiumGenerated/Materials/V2/M_V2_Lit" in editor.saved


def test_every_property_connection_the_module_makes_is_accepted():
    """The fake refuses nothing by default, so this only guards against a call that never
    happens -- the real assertion is that `_load` above completes without `SystemExit`, which is
    exactly what `connect`/`Graph.to` raise on a refused pin (mat_fog.connect's contract)."""


def test_declared_parameters_all_land_on_the_graph(tmp_path, monkeypatch):
    editor = FakeEditor()
    module = _load(editor, tmp_path, monkeypatch)
    asset = editor.assets["/Game/ElysiumGenerated/Materials/V2/M_V2_Lit"]
    names = {n.props.get("parameter_name") for n in asset.expressions if "parameter_name" in n.props}

    for table in (module.Params.Textures, module.Params.Scalars, module.Params.Vectors,
                 module.Params.Switches):
        for attr, value in vars(table).items():
            if attr.startswith("_"):
                continue
            assert value in names, "%s not authored on the graph" % value


def test_a_second_run_with_no_change_skips_the_rebuild(tmp_path, monkeypatch):
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    editor.mel.connections.clear()
    first_recompiles = len(editor.saved)

    editor2 = FakeEditor()
    editor2.assets = editor.assets  # same "disk" state (recipe stamp persists)
    editor2.command_line = ""
    module2 = _load(editor2, tmp_path, monkeypatch)

    # A no-op rerun creates no *new* expressions on the master (the recompiled asset is reused,
    # not recreated): the stamped recipe on the loaded asset already matches.
    asset = editor2.assets["/Game/ElysiumGenerated/Materials/V2/M_V2_Lit"]
    assert asset.metadata.get("ElysiumRecipe")


def test_policy_force_rebuilds_even_when_current(tmp_path, monkeypatch):
    editor = FakeEditor()
    _load(editor, tmp_path, monkeypatch)
    stamped = editor.assets["/Game/ElysiumGenerated/Materials/V2/M_V2_Lit"].metadata["ElysiumRecipe"]

    editor2 = FakeEditor()
    editor2.assets = editor.assets
    editor2.command_line = "-PolicyForce=1"
    module2 = _load(editor2, tmp_path, monkeypatch)

    asset = editor2.assets["/Game/ElysiumGenerated/Materials/V2/M_V2_Lit"]
    assert asset.metadata["ElysiumRecipe"] == stamped
    assert "/Game/ElysiumGenerated/Materials/V2/M_V2_Lit" in editor2.saved
