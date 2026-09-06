"""Import-safe sky editor adapters and the texture worker's HDR dispatch."""
import importlib.util
import sys
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import pytest

from test_import_textures_editor import FakeEditor, _load

# Loading the helper directly avoids keeping pipeline.unreal alive across the existing
# fake editor's scoped sys.modules patch (its bake_lib binds the fake at import time).
_spec = importlib.util.spec_from_file_location("sky_test_editor", Path(__file__).resolve().parents[2]
                                               / "pipeline/unreal/sky_composites.py")
editor_sky = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(editor_sky)


def _cube(path="/ElysiumBaked/Textures/skybox/TC_hav_Sky"):
    return SimpleNamespace(get_class=lambda: SimpleNamespace(get_name=lambda: "TextureCube"),
                           get_path_name=lambda: path)


def test_load_uses_only_canonical_asset_and_serialized_mean():
    loaded = []
    cube = _cube()
    data = {"sky_name": "hav", "upper_hemisphere_mean": 0.125}
    record = SimpleNamespace(get_editor_property=data.__getitem__)
    fake = SimpleNamespace(EditorAssetLibrary=SimpleNamespace(
        load_asset=lambda path: loaded.append(path) or cube),
        ElysiumSkyProvenance=SimpleNamespace(find=lambda texture: record))
    with mock.patch.dict(sys.modules, {"unreal": fake}):
        assert editor_sky.load_sky_cube("HAV") == (cube, .125)
    assert loaded == ["/ElysiumBaked/Textures/skybox/TC_hav_Sky"]
    assert editor_sky.material_address("hav") == ("/ElysiumBaked/Materials/skybox", "MI_hav_Sky")
    assert editor_sky.SKY_DOME_PATH == "/Game/ElysiumGenerated/Sky/SM_SkyDome"
    assert editor_sky.LOOKDEV_MAP_PATH == "/Game/ElysiumGenerated/Lookdev/Materials"


@pytest.mark.parametrize("mode", ["missing", "provenance", "wrong-sky", "invalid-mean"])
def test_missing_or_bad_sky_fails_without_png_fallback(mode):
    props = {"sky_name": "la" if mode == "wrong-sky" else "hav",
             "upper_hemisphere_mean": float("nan") if mode == "invalid-mean" else .1}
    fake = SimpleNamespace(EditorAssetLibrary=SimpleNamespace(
        load_asset=lambda path: None if mode == "missing" else _cube()),
        ElysiumSkyProvenance=SimpleNamespace(find=lambda cube: None if mode == "provenance"
                                             else SimpleNamespace(get_editor_property=props.__getitem__)))
    with mock.patch.dict(sys.modules, {"unreal": fake}), pytest.raises(RuntimeError):
        editor_sky.load_sky_cube("hav")


def test_texture_worker_requests_linear_float_hdr_and_dispatches_full_provenance(tmp_path):
    fake_editor = FakeEditor()
    worker = _load(fake_editor)
    worker.unreal.TextureCompressionSettings.TC_HDR_F32 = "hdr32"
    entry = {"assetPath": "/ElysiumBaked/Textures/skybox/TC_hav_Sky", "class": "TextureCube",
             "product": "sky-composite", "stagedFormat": "rgba32f", "srgb": False,
             "compression": "hdr-f32", "mipGen": "leave-existing", "neverStream": True,
             "provenance": "sky.json", "recipe": {}, "unit": "elysium:sky-composite:hav"}
    settings = worker.settings_for(entry)
    assert settings["compression_settings"] == "hdr32"
    assert settings["srgb"] is False
    assert settings["never_stream"] is True
    assert "address_x" not in settings
    sidecar = '{"sourceUnits": [1, 2, 3, 4, 5, 6], "upperHemisphereMean": 0.125}'
    (tmp_path / "sky.json").write_text(sidecar)
    cube = _cube()
    attached = []
    fake = SimpleNamespace(ElysiumSkyProvenance=SimpleNamespace(
        apply_json=lambda obj, text: (attached.append((obj, text)) or object(), "")))
    worker.apply_settings = lambda obj, row: None
    worker.verify_built = lambda obj, row: (None, None)
    worker.unreal.ElysiumTextureImportLibrary.built_pixel_format = lambda obj: "PF_A32B32G32R32F"
    report = worker.Report("manifest", "/ElysiumBaked/Textures")
    with mock.patch.dict(sys.modules, {"unreal": fake}), \
            mock.patch.object(worker.bl, "stamp_recipe"), mock.patch.object(worker.bl, "save", return_value=True):
        worker._finish_entry(cube, entry, str(tmp_path), SimpleNamespace(fingerprint=lambda row: "fp"), report, False)
    assert attached == [(cube, sidecar)]
    assert report.built == 1


def test_native_provenance_refusal_is_a_worker_failure():
    fake = SimpleNamespace(ElysiumSkyProvenance=SimpleNamespace(
        apply_json=lambda obj, text: (None, "source mip 2 mismatch")))
    with mock.patch.dict(sys.modules, {"unreal": fake}), pytest.raises(RuntimeError, match="mip 2"):
        editor_sky.attach_provenance(_cube(), "{}")


@pytest.mark.parametrize("mips", [1, 3, 4])
def test_usable_native_sky_does_not_need_strict_platform_precision(mips):
    worker = _load(FakeEditor())
    entry = {"class": "TextureCube", "product": "sky-composite",
             "expected": {"width": 8, "height": 8, "mips": 3, "faces": 6}}
    worker.unreal.ElysiumTextureImportLibrary.built_extent = lambda obj: (8, 8, 0, mips)
    worker.unreal.ElysiumTextureImportLibrary.built_pixel_format = mock.Mock(
        side_effect=AssertionError("a platform precision gate is outside milestone scope"))
    problem, difference = worker.verify_built(_cube(), entry)
    assert problem is None
    assert difference == (None if mips == 3 else {"authoredMips": 3, "builtMips": mips})


def test_native_sky_still_needs_a_usable_mip_and_original_extent():
    worker = _load(FakeEditor())
    entry = {"class": "TextureCube", "product": "sky-composite",
             "expected": {"width": 8, "height": 8, "mips": 3, "faces": 6}}
    worker.unreal.ElysiumTextureImportLibrary.built_extent = lambda obj: (8, 8, 0, 0)
    assert "no usable" in worker.verify_built(_cube(), entry)[0]
    worker.unreal.ElysiumTextureImportLibrary.built_extent = lambda obj: (4, 4, 0, 1)
    assert "expected 8x8" in worker.verify_built(_cube(), entry)[0]
