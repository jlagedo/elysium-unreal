"""Fresh content generation must not depend on imported skies or delete a live master."""
import runpy
import sys
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import pytest

from test_sky_composites_editor import editor_sky

SCRIPT = Path(__file__).resolve().parents[2] / "pipeline/unreal/make_sky_material.py"
MASTER = "/Game/ElysiumGenerated/Materials/M_Sky"


class Asset:
    def __init__(self, kind):
        self.kind = kind
        self.properties = {}

    def get_class(self):
        return SimpleNamespace(get_name=lambda: self.kind)

    def set_editor_properties(self, properties):
        self.properties.update(properties)

    def set_editor_property(self, name, value):
        self.properties[name] = value


class Editor:
    def __init__(self):
        self.engine_cube = Asset("TextureCube")
        self.engine_cube.properties["srgb"] = True
        self.assets = {editor_sky.ENGINE_DEFAULT_CUBE: self.engine_cube}
        self.duplicated, self.saved, self.cleared, self.expressions = [], [], [], []
        self.fail_save = None
        self.fake = SimpleNamespace(
            EditorAssetLibrary=SimpleNamespace(
                load_asset=self.assets.get, does_asset_exist=lambda path: path in self.assets,
                does_directory_exist=lambda path: True,
                duplicate_asset=self.duplicate, save_asset=self.save),
            TextureCompressionSettings=SimpleNamespace(TC_HDR_F32="HDR_F32"),
            TextureMipGenSettings=SimpleNamespace(TMGS_NO_MIPMAPS="NO_MIPS"),
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: SimpleNamespace(create_asset=self.create)),
            MaterialEditingLibrary=SimpleNamespace(
                delete_all_material_expressions=self.cleared.append,
                create_material_expression=self.expression, recompile_material=lambda mat: None,
                connect_material_expressions=lambda *args: True, connect_material_property=lambda *args: True),
            Material=object, MaterialFactoryNew=object,
            MaterialExpressionTextureSampleParameterCube=object,
            MaterialExpressionCameraVectorWS=object, MaterialExpressionMultiply=object,
            MaterialExpressionScalarParameter=object,
            MaterialSamplerType=SimpleNamespace(SAMPLERTYPE_LINEAR_COLOR="LINEAR"),
            MaterialShadingModel=SimpleNamespace(MSM_UNLIT="UNLIT"),
            MaterialProperty=SimpleNamespace(MP_EMISSIVE_COLOR="EMISSIVE"),
            log=lambda text: None, log_error=lambda text: None,
        )

    def duplicate(self, source, destination):
        self.duplicated.append((source, destination))
        self.assets[destination] = Asset("TextureCube")
        return self.assets[destination]

    def create(self, name, package, *args):
        self.assets[package + "/" + name] = Asset("Material")
        return self.assets[package + "/" + name]

    def expression(self, *args):
        result = Asset("Expression")
        self.expressions.append(result)
        return result

    def save(self, path):
        self.saved.append(path)
        return path != self.fail_save

    def run(self):
        with mock.patch.dict(sys.modules, {"unreal": self.fake, "pipeline.unreal.sky_composites": editor_sky}):
            return runpy.run_path(str(SCRIPT))


def test_fresh_bootstrap_without_imported_textures_then_reuses_master():
    editor = Editor()
    editor.run()
    master = editor.assets[MASTER]
    default = editor.assets[editor_sky.DEFAULT_SKY_CUBE_PATH]
    assert default.properties["srgb"] is False
    assert editor.engine_cube.properties == {"srgb": True}
    assert editor.expressions[0].properties["sampler_type"] == "LINEAR"
    assert editor.expressions[0].properties["texture"] is default
    editor.run()
    assert editor.assets[MASTER] is master
    assert editor.cleared == [master]
    assert len(editor.duplicated) == 1
    assert not any(path.startswith("/ElysiumBaked") for path in editor.assets)


def test_missing_engine_default_keeps_existing_material():
    editor = Editor()
    del editor.assets[editor_sky.ENGINE_DEFAULT_CUBE]
    master = editor.assets[MASTER] = Asset("Material")
    with pytest.raises(RuntimeError, match="DefaultTextureCube"):
        editor.run()
    assert editor.assets[MASTER] is master
    assert not editor.cleared


def test_wrong_class_at_master_address_is_not_deleted():
    editor = Editor()
    foreign = editor.assets[MASTER] = Asset("Texture2D")
    with pytest.raises(RuntimeError, match="refusing to replace"):
        editor.run()
    assert editor.assets[MASTER] is foreign
    assert not editor.cleared


@pytest.mark.parametrize("path", [MASTER, editor_sky.DEFAULT_SKY_CUBE_PATH])
def test_save_failure_fails_generator(path):
    editor = Editor()
    editor.fail_save = path
    with pytest.raises(RuntimeError, match="save"):
        editor.run()
