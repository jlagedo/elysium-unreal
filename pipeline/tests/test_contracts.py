from __future__ import annotations

import os
import ast
import importlib.util
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

from PIL import Image

import pytest

from elysium_pipeline.formats import bsp, mdl, tex_to_png, vmt
from elysium_pipeline.formats.glass import derive_normal, is_glass
from elysium_pipeline import paths, shared_corpus, unreal as unreal_driver
from elysium_pipeline.validation.png_alpha import alpha_range

REPO = Path(__file__).resolve().parents[2]
POLICY_SPEC = importlib.util.spec_from_file_location(
    "elysium_repo_policy", REPO / "dev/check_repo_policy.py"
)
assert POLICY_SPEC and POLICY_SPEC.loader
POLICY = importlib.util.module_from_spec(POLICY_SPEC)
POLICY_SPEC.loader.exec_module(POLICY)


def test_source_to_unreal_contract() -> None:
    assert bsp.source_to_unreal(1.0, 2.0, 3.0) == (2.54, -5.08, 7.62)
    assert bsp.source_dir_to_unreal(1.0, 2.0, 3.0) == (1.0, -2.0, 3.0)


def test_coordinate_owners_are_unique() -> None:
    definitions: list[Path] = []
    for root in (REPO / "pipeline/src", REPO / "pipeline/unreal"):
        for source in root.rglob("*.py"):
            text = source.read_text(encoding="utf-8")
            names = {
                node.name for node in ast.walk(ast.parse(text))
                if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
            }
            if {"source_to_unreal", "source_dir_to_unreal"} & names:
                definitions.append(source)
    assert definitions == [REPO / "pipeline/src/elysium_pipeline/formats/bsp.py"]


def test_model_obj_writer_is_unreal_only() -> None:
    mesh = mdl.Mesh("test")
    mesh.verts = [
        (1.0, 2.0, 3.0, 0.0, 0.0),
        (2.0, 2.0, 3.0, 1.0, 0.0),
        (1.0, 3.0, 3.0, 0.0, 1.0),
    ]
    mesh.tris = [(0, 1, 2)]
    with tempfile.TemporaryDirectory() as out:
        mdl.write_obj_scene(
            [mesh],
            "test",
            out,
            [],
            lambda _key: None,
            {},
        )
        obj = (Path(out) / "test.obj").read_text(encoding="utf-8")
    assert "v 2.540000 -5.080000 7.620000" in obj
    assert "f 1/1 3/3 2/2" in obj


SEARCH = ["models/props/"]


def _triangle(material: str) -> mdl.Mesh:
    mesh = mdl.Mesh(material)
    mesh.verts = [
        (1.0, 2.0, 3.0, 0.0, 0.0),
        (2.0, 2.0, 3.0, 1.0, 0.0),
        (1.0, 3.0, 3.0, 0.0, 1.0),
    ]
    mesh.tris = [(0, 1, 2)]
    return mesh


def _record_for_vmt(vmt_body: str, read_bytes=None) -> dict:
    """One material's corpus definition. The `.mtl` names the material and the map's own
    facts; every channel and flag is stated once here."""
    def default_read(path):
        return vmt_body.encode("ascii") if path == "materials/models/props/glasswin.vmt" else None

    channels = mdl.material_channels("glasswin", SEARCH, read_bytes or default_read)
    return shared_corpus.material_record(channels) if channels else {}


def _mtl_for_vmt(vmt_body: str) -> str:
    """The `.mtl` a model writes: its slot names and the material key each draws."""
    mesh = _triangle("glasswin")

    def read_bytes(path):
        return vmt_body.encode("ascii") if path == "materials/models/props/glasswin.vmt" else None

    with tempfile.TemporaryDirectory() as out:
        mdl.write_obj_scene([mesh], "test", out, SEARCH, read_bytes, {})
        return (Path(out) / "test.mtl").read_text(encoding="utf-8")


def test_translucent_prop_material_carries_blend_flag() -> None:
    body = '"VertexLitGeneric"\n{\n"$basetexture" "props/glasswin"\n"$translucent" "1"\n}\n'
    assert _record_for_vmt(body)["blend"]
    assert "mat models/props/glasswin" in _mtl_for_vmt(body)


def test_alphatest_prop_material_carries_illum_flag() -> None:
    body = '"VertexLitGeneric"\n{\n"$basetexture" "props/glasswin"\n"$alphatest" "1"\n}\n'
    record = _record_for_vmt(body)
    assert record["scissor"]
    assert not record["blend"]


def test_semantic_glass_prop_carries_glass_and_derived_normal() -> None:
    vmt_body = (
        '"VertexLitGeneric"\n{\n"$basetexture" "props/glasswin"\n'
        '"$translucent" "1"\n"$envmap" "env_cubemap"\n}\n'
    )

    def read_bytes(path):
        if path == "materials/models/props/glasswin.vmt":
            return vmt_body.encode("ascii")
        if path in ("materials/props/glasswin.tth", "materials/props/glasswin.ttz"):
            return b"synthetic"
        return None

    source = Image.new("RGBA", (5, 5), (110, 140, 160, 80))
    source.putpixel((2, 2), (180, 200, 210, 80))
    with tempfile.TemporaryDirectory() as out, mock.patch(
        "elysium_pipeline.formats.tex_to_png.decode", return_value=source
    ):
        mdl.write_obj_scene(
            [_triangle("glasswin")], "test", out, SEARCH, read_bytes, {})
        mtl = (Path(out) / "test.mtl").read_text(encoding="utf-8")
        normal = Path(out) / "tex" / "props_glasswin_glass_n.png"
        assert normal.is_file()
    assert "mat models/props/glasswin" in mtl
    record = _record_for_vmt(vmt_body, read_bytes)
    assert record["blend"]
    assert record["glass"]
    assert record["bump"] == "tex/props_glasswin_glass_n.png"


def test_authored_glass_bumpmap_takes_precedence() -> None:
    vmt_body = (
        '"VertexLitGeneric"\n{\n"$basetexture" "props/glasswin"\n'
        '"$translucent" "1"\n"$envmap" "env_cubemap"\n'
        '"$bumpmap" "props/authored"\n}\n'
    )

    def read_bytes(path):
        if path == "materials/models/props/glasswin.vmt":
            return vmt_body.encode("ascii")
        if path.endswith((".tth", ".ttz")):
            return path.encode("ascii")
        return None

    def decode(tth, _ttz):
        color = (128, 128, 255, 255) if b"authored" in tth else (100, 130, 150, 80)
        return Image.new("RGBA", (4, 4), color)

    with tempfile.TemporaryDirectory() as out, mock.patch(
        "elysium_pipeline.formats.tex_to_png.decode", side_effect=decode
    ):
        mdl.write_obj_scene(
            [_triangle("glasswin")], "test", out, SEARCH, read_bytes, {})
        mtl = (Path(out) / "test.mtl").read_text(encoding="utf-8")
        assert not (Path(out) / "tex" / "props_glasswin_glass_n.png").exists()
    record = _record_for_vmt(vmt_body, read_bytes)
    assert record["bump"] == "tex/props_authored_n.png"


def test_one_fold_names_the_mtl_slot_the_skins_slot_and_the_manifest_slot() -> None:
    """A model's material slot has one spelling in three places, and it is `mdl.sanitize`.

    `bl.safe_name` is the other fold in this repo: it collapses runs and drops `.` and `-`.
    The two agree on ordinary names and disagree on exactly the names that carry those
    characters, so a slot folded the wrong way joins on most models and silently misses the
    rest -- which shows up as a prop that ignores its alternate skin.
    """
    awkward = "Panel-A.2 x"
    mesh = _triangle(awkward)
    mesh.skinref = 0

    def read_bytes(path):
        return None

    with tempfile.TemporaryDirectory() as out:
        mdl.write_obj_scene(
            [mesh], "test", out, SEARCH, read_bytes, {},
            skins=[[awkward], ["Panel-B.2 x"]])
        mtl = (Path(out) / "test.mtl").read_text(encoding="utf-8")
        skins = (Path(out) / "test.skins").read_text(encoding="utf-8")

    folded = mdl.sanitize(awkward)
    assert folded == "panel-a.2_x"
    assert f"newmtl {folded}" in mtl
    assert folded in skins
    # The other fold would have produced a different name in each place.
    assert folded != shared_corpus.material_asset(awkward)[len("MI_"):]


def test_source_refract_prop_exports_dudv_as_distortion_not_albedo() -> None:
    vmt_body = (
        '"Refract"\n{\n"$dudvmap" "props/rain_dudv"\n'
        '"$refractamount" ".01"\n"$model" "1"\n}\n'
    )

    def read_bytes(path):
        if path == "materials/models/props/glasswin.vmt":
            return vmt_body.encode("ascii")
        if path in ("materials/props/rain_dudv.tth", "materials/props/rain_dudv.ttz"):
            return b"synthetic"
        return None

    signed = Image.new("RGBA", (2, 1))
    signed.putdata([(0, 0, 127, 255), (255, 1, 127, 255)])
    with tempfile.TemporaryDirectory() as out, mock.patch(
        "elysium_pipeline.formats.tex_to_png.decode", return_value=signed
    ):
        mdl.write_obj_scene(
            [_triangle("glasswin")], "test", out, SEARCH, read_bytes, {})
        mtl = (Path(out) / "test.mtl").read_text(encoding="utf-8")
        normal_path = Path(out) / "tex" / "props_rain_dudv_refract_n.png"
        with Image.open(normal_path) as normal:
            assert list(normal.get_flattened_data()) == [
                (128, 128, 255), (127, 129, 255)]

    record = _record_for_vmt(vmt_body, read_bytes)
    assert record["refract"]
    assert record["refract_amount"] == 0.01
    assert record["refract_map"] == "tex/props_rain_dudv_refract_n.png"
    assert not record["blend"]
    assert not record["glass"]
    assert record["albedo"] == ""


def test_source_refract_prefers_authored_normal_over_dudv_fallback() -> None:
    vmt_body = (
        '"Refract"\n{\n"$dudvmap" "props/old_dudv"\n'
        '"$normalmap" "props/authored_normal"\n"$refractamount" "2"\n}\n'
    )
    decoded = []

    def read_bytes(path):
        if path == "materials/models/props/glasswin.vmt":
            return vmt_body.encode("ascii")
        if path in (
                "materials/props/authored_normal.tth",
                "materials/props/authored_normal.ttz"):
            return path.encode("ascii")
        return None

    def decode(tth, _ttz):
        decoded.append(tth)
        return Image.new("RGBA", (1, 1), (128, 128, 255, 255))

    with tempfile.TemporaryDirectory() as out, mock.patch(
        "elysium_pipeline.formats.tex_to_png.decode", side_effect=decode
    ):
        mdl.write_obj_scene(
            [_triangle("glasswin")], "test", out, SEARCH, read_bytes, {})
        mtl = (Path(out) / "test.mtl").read_text(encoding="utf-8")

    assert len(decoded) == 1
    assert b"authored_normal" in decoded[0]
    assert "mat models/props/glasswin" in mtl
    record = _record_for_vmt(vmt_body, read_bytes)
    assert record["refract_map"] == "tex/props_authored_normal_refract_n.png"


def _export_texture(flag: str) -> Image.Image:
    vmt_body = (
        '"VertexLitGeneric"\n{\n"$basetexture" "props/shared"\n'
        + (f'"${flag}" "1"\n' if flag else "")
        + "}\n"
    )

    def read_bytes(path):
        if path == "materials/models/props/glasswin.vmt":
            return vmt_body.encode("ascii")
        if path in ("materials/props/shared.tth", "materials/props/shared.ttz"):
            return b"synthetic"
        return None

    source = Image.new("RGBA", (2, 1))
    source.putdata([(10, 20, 30, 0), (40, 50, 60, 191)])
    with tempfile.TemporaryDirectory() as out, mock.patch(
        "elysium_pipeline.formats.tex_to_png.decode", return_value=source
    ):
        mdl.write_obj_scene(
            [_triangle("glasswin")], "test", out, SEARCH, read_bytes, {})
        with Image.open(Path(out) / "tex" / "props_shared.png") as exported:
            return exported.copy()


def test_translucent_prop_preserves_source_alpha() -> None:
    image = _export_texture("translucent")
    assert image.mode == "RGBA"
    assert [image.getpixel((x, 0))[3] for x in range(2)] == [0, 191]


def test_alphatest_prop_preserves_source_alpha() -> None:
    image = _export_texture("alphatest")
    assert image.mode == "RGBA"
    assert [image.getpixel((x, 0))[3] for x in range(2)] == [0, 191]


def test_opaque_prop_keeps_stored_alpha() -> None:
    # Alpha is a fact of the source file, not of the material drawing it: an opaque VMT does
    # not flatten a texture whose source stores a real alpha plane.
    image = _export_texture("")
    assert image.mode == "RGBA"
    assert [image.getpixel((x, 0))[3] for x in range(2)] == [0, 191]


def test_blank_alpha_folds_to_rgb() -> None:
    # A uniformly opaque plane encodes nothing, so the write folds it away -- losslessly.
    vmt_body = '"VertexLitGeneric"\n{\n"$basetexture" "props/shared"\n}\n'

    def read_bytes(path):
        if path == "materials/models/props/glasswin.vmt":
            return vmt_body.encode("ascii")
        if path in ("materials/props/shared.tth", "materials/props/shared.ttz"):
            return b"synthetic"
        return None

    source = Image.new("RGBA", (2, 1), (10, 20, 30, 255))
    with tempfile.TemporaryDirectory() as out, mock.patch(
        "elysium_pipeline.formats.tex_to_png.decode", return_value=source
    ):
        mdl.write_obj_scene(
            [_triangle("glasswin")], "test", out, SEARCH, read_bytes, {})
        with Image.open(Path(out) / "tex" / "props_shared.png") as exported:
            assert exported.mode == "RGB"


def test_shared_basetexture_writes_one_file_whatever_the_order() -> None:
    vmts = {
        "materials/models/props/opaque.vmt": (
            '"VertexLitGeneric"\n{\n"$basetexture" "props/shared"\n}\n'
        ),
        "materials/models/props/glass.vmt": (
            '"VertexLitGeneric"\n{\n"$basetexture" "props/shared"\n'
            '"$translucent" "1"\n}\n'
        ),
    }

    def read_bytes(path):
        if path in vmts:
            return vmts[path].encode("ascii")
        if path in ("materials/props/shared.tth", "materials/props/shared.ttz"):
            return b"synthetic"
        return None

    source = Image.new("RGBA", (1, 1), (10, 20, 30, 73))
    for order in (("opaque", "glass"), ("glass", "opaque")):
        with tempfile.TemporaryDirectory() as out, mock.patch(
            "elysium_pipeline.formats.tex_to_png.decode", return_value=source
        ):
            mdl.write_obj_scene(
                [_triangle(name) for name in order],
                "test", out, SEARCH, read_bytes, {})
            with Image.open(Path(out) / "tex" / "props_shared.png") as exported:
                assert exported.mode == "RGBA"
                assert exported.getchannel("A").getpixel((0, 0)) == 73


TEXTURE_DECODE_MEMO_SEARCH = ["models/props/"]


# TextureDecodeMemoTests
# `mdl._png_memo`: one decode+write per output path across per-model caches.

def _read_bytes_for(vmts):
    def read_bytes(path):
        if path in vmts:
            return vmts[path].encode("ascii")
        if path in ("materials/props/shared.tth", "materials/props/shared.ttz"):
            return b"synthetic"
        return None
    return read_bytes


def test_a_shared_texture_decodes_once_across_per_model_caches() -> None:
    read_bytes = _read_bytes_for({
        "materials/models/props/glasswin.vmt":
            '"VertexLitGeneric"\n{\n"$basetexture" "props/shared"\n}\n',
    })
    source = Image.new("RGBA", (2, 1))
    source.putdata([(10, 20, 30, 0), (40, 50, 60, 191)])
    with tempfile.TemporaryDirectory() as out, mock.patch(
        "elysium_pipeline.formats.tex_to_png.decode", return_value=source
    ) as decode:
        (Path(out) / "tex").mkdir()          # write_obj_scene's own makedirs
        first = mdl._resolve_material("glasswin", TEXTURE_DECODE_MEMO_SEARCH, read_bytes, out, {})
        second = mdl._resolve_material("glasswin", TEXTURE_DECODE_MEMO_SEARCH, read_bytes, out, {})
    assert first["albedo"] == "props_shared.png"
    assert second["albedo"] == first["albedo"]
    assert decode.call_count == 1


def test_selfillum_arriving_on_a_later_model_still_writes_the_emission_mask() -> None:
    # The memo keeps filenames, not images, so a derived product first requested by a
    # later model re-decodes its base once rather than losing the mask.
    read_bytes = _read_bytes_for({
        "materials/models/props/plain.vmt":
            '"VertexLitGeneric"\n{\n"$basetexture" "props/shared"\n}\n',
        "materials/models/props/glow.vmt":
            '"VertexLitGeneric"\n{\n"$basetexture" "props/shared"\n"$selfillum" "1"\n}\n',
    })
    source = Image.new("RGBA", (2, 1))
    source.putdata([(10, 20, 30, 0), (40, 50, 60, 191)])
    with tempfile.TemporaryDirectory() as out, mock.patch(
        "elysium_pipeline.formats.tex_to_png.decode", return_value=source
    ) as decode:
        (Path(out) / "tex").mkdir()          # write_obj_scene's own makedirs
        first = mdl._resolve_material("plain", TEXTURE_DECODE_MEMO_SEARCH, read_bytes, out, {})
        second = mdl._resolve_material("glow", TEXTURE_DECODE_MEMO_SEARCH, read_bytes, out, {})
        assert (Path(out) / "tex" / "props_shared_ke.png").is_file()
    assert first["emis"] is None
    assert second["emis"] == "props_shared_ke.png"
    assert decode.call_count == 2   # the albedo, then the mask's re-decode


# SourceFormatAlphaTests
# `decode` answers the fold-to-RGB question from the source format where provable.

def _tth(w, h, fmt):
    import struct
    vtf = bytearray(64)
    vtf[0:4] = b"VTF\x00"
    struct.pack_into("<HH", vtf, 16, w, h)
    struct.pack_into("<I", vtf, 52, fmt)
    vtf[56] = 1
    return b"TTH\x00" + b"\x00" * 12 + bytes(vtf)


def _roundtrip(tth, ttz):
    image = tex_to_png.decode(tth, ttz)
    with tempfile.TemporaryDirectory() as out:
        path = Path(out) / "t.png"
        tex_to_png.save_png(image, path)
        with Image.open(path) as saved:
            return image, saved.mode


def test_bgr888_is_format_answered_opaque_and_folds() -> None:
    import zlib
    tth = _tth(2, 1, tex_to_png.FMT_BGR888)
    image, mode = _roundtrip(tth, zlib.compress(bytes((30, 20, 10, 60, 50, 40))))
    assert image.info.get("opaque_alpha") is True
    assert mode == "RGB"


def test_dxt1_without_punch_through_blocks_is_format_answered_opaque() -> None:
    import struct
    import zlib
    block = struct.pack("<HH4B", 0xF800, 0x001F, 0, 0, 0, 0)   # color0 > color1
    tth = _tth(4, 4, tex_to_png.FMT_DXT1)
    image, mode = _roundtrip(tth, zlib.compress(block))
    assert image.info.get("opaque_alpha") is True
    assert mode == "RGB"


def test_dxt1_punch_through_blocks_keep_their_alpha() -> None:
    # color0 <= color1 selects BC1's three-colour mode; index 3 decodes transparent, so
    # the format cannot claim opacity and the scan (and the alpha plane) must survive.
    import struct
    import zlib
    block = struct.pack("<HH4B", 0x001F, 0xF800, 0xFF, 0xFF, 0xFF, 0xFF)
    tth = _tth(4, 4, tex_to_png.FMT_DXT1)
    image, mode = _roundtrip(tth, zlib.compress(block))
    assert image.info.get("opaque_alpha") is None
    assert image.getchannel("A").getextrema() == (0, 0)
    assert mode == "RGBA"


def test_reads_rgba_alpha_range_and_rejects_rgb_as_alpha() -> None:
    with tempfile.TemporaryDirectory() as out:
        rgba = Path(out) / "rgba.png"
        rgb = Path(out) / "rgb.png"
        image = Image.new("RGBA", (2, 1))
        image.putdata([(0, 0, 0, 17), (0, 0, 0, 239)])
        image.save(rgba)
        image.convert("RGB").save(rgb)
        assert alpha_range(rgba) == (17, 239)
        assert alpha_range(rgb) is None


def test_vmt_parser_keeps_refract_shader_inputs_without_basetexture() -> None:
    info = vmt.parse(
        '"Refract"\n{\n"$dudvmap" "Props\\Rain_DUDV"\n'
        '"$refractamount" ".01"\n}\n')
    assert info["refract"]
    assert info["basetexture"] is None
    assert info["dudvmap"] == "props/rain_dudv"
    assert info["refractamount"] == 0.01


def test_uvwq_signed_vectors_convert_deterministically_to_tangent_normal() -> None:
    raw = bytes((0, 0, 127, 255, 127, 128, 127, 255, 255, 1, 127, 255))
    decoded = tex_to_png._decode_mip(
        raw, 3, 1, tex_to_png.FMT_UVWQ8888)
    first = tex_to_png.dudv_to_normal(decoded)
    second = tex_to_png.dudv_to_normal(decoded)
    assert first.tobytes() == second.tobytes()
    assert list(first.get_flattened_data()) == [
        (128, 128, 255), (255, 0, 255), (127, 129, 255)]
    assert tex_to_png.mip_byte_size(3, 1, tex_to_png.FMT_UVWQ8888) == 12


def _info(shader="lightmappedgeneric", **overrides):
    info = {
        "shader": shader,
        "basetexture": "glass/window",
        "translucent": True,
        "envmap": "env_cubemap",
        "additive": False,
        "decal": False,
        "water": False,
    }
    info.update(overrides)
    return info


def test_world_and_prop_lit_reflective_glass_classify() -> None:
    assert is_glass(_info(), "glass/pawnwndwglass")
    assert is_glass(
        _info(shader="vertexlitgeneric",
                   basetexture="models/scenery/misc/wall_clock/clockglass"),
        "models/scenery/misc/wall_clock/clockglass")


def test_non_glass_transparency_combinations_stay_generic() -> None:
    assert not is_glass(
        _info(envmap=None, basetexture="models/scenery/theater/neta"),
        "models/scenery/theater/neta")
    assert not is_glass(_info(shader="unlitgeneric"), "effects/glass_fleck")
    assert not is_glass(_info(additive=True), "models/light/glass")
    assert not is_glass(_info(decal=True), "glass/poster")
    assert not is_glass(_info(water=True), "glass/water")
    assert not is_glass(
        _info(basetexture="models/scenery/theater/curtains"),
        "models/scenery/theater/curtains")


def test_derived_normal_is_deterministic_and_flat_outside_mask() -> None:
    source = Image.new("RGBA", (7, 7), (80, 100, 120, 90))
    for y in range(7):
        for x in range(7):
            source.putpixel((x, y), (60 + x * 20, 70 + y * 15, 100, 90))
    mask = Image.new("L", (7, 7), 255)
    for y in range(7):
        mask.putpixel((3, y), 0)
    first = derive_normal(source, mask)
    second = derive_normal(source, mask)
    assert first.tobytes() == second.tobytes()
    assert first.getpixel((3, 3)) == (128, 128, 255)
    assert first.getpixel((1, 3)) != (128, 128, 255)


def test_derived_normal_resamples_independently_sized_mask() -> None:
    source = Image.new("RGBA", (8, 8), (0, 0, 0, 255))
    for y in range(8):
        for x in range(8):
            value = x * 30
            source.putpixel((x, y), (value, value, value, 255))
    mask = Image.new("L", (2, 2), 0)
    mask.putpixel((0, 0), 255)
    mask.putpixel((0, 1), 255)

    normal = derive_normal(source, mask, blur_radius=0.0)

    assert normal.size == source.size
    assert normal.getpixel((1, 3)) != (128, 128, 255)
    assert normal.getpixel((6, 3)) == (128, 128, 255)


def test_uniform_glass_normal_is_neutral() -> None:
    normal = derive_normal(Image.new("RGBA", (5, 5), (100, 120, 140, 80)))
    assert set(normal.get_flattened_data()) == {(128, 128, 255)}


class BakeTextureImportContractTests(unittest.TestCase):
    @staticmethod
    def _load_bake_lib(fake_unreal):
        spec = importlib.util.spec_from_file_location(
            "elysium_test_bake_lib", REPO / "pipeline/unreal/bake_lib.py")
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {"unreal": fake_unreal}):
            spec.loader.exec_module(module)
        return module

    def test_existing_texture_is_submitted_for_in_place_replacement(self) -> None:
        tasks = []
        asset = object()

        class FakeTask:
            pass

        tools = SimpleNamespace(import_asset_tasks=lambda submitted: tasks.extend(submitted))
        editor = SimpleNamespace(
            does_directory_exist=lambda _target: True,
            make_directory=lambda _target: True,
            does_asset_exist=lambda _target: True,
            load_asset=lambda _target: asset,
        )
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: tools),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
            AssetImportTask=FakeTask,
            EditorAssetLibrary=editor,
        )
        module = self._load_bake_lib(fake_unreal)

        result = module.import_textures([("updated.png", "T_existing")], "/Test")
        assert result == {"T_existing": asset}
        assert len(tasks) == 1
        assert tasks[0].replace_existing
        assert tasks[0].replace_existing_settings

    def test_stored_recipe_reads_the_registry_by_object_path(self) -> None:
        # Callers name assets by package path; the registry answers only the object path.
        class FakeData:
            def __init__(self, tag):
                self.tag = tag

            def is_valid(self):
                return True

            def get_tag_value(self, name):
                return self.tag if name == "ElysiumRecipe" else ""

        registry = SimpleNamespace(get_asset_by_object_path=lambda path: (
            FakeData("abc123") if path == "/ElysiumBaked/Shared/Textures/T_x.T_x" else None))
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
            AssetRegistryHelpers=SimpleNamespace(get_asset_registry=lambda: registry),
        )
        module = self._load_bake_lib(fake_unreal)
        assert module.stored_recipe("/ElysiumBaked/Shared/Textures/T_x") == "abc123"
        assert module.stored_recipe("/ElysiumBaked/Shared/Textures/T_x.T_x") == "abc123"
        assert module.stored_recipe("/ElysiumBaked/Shared/Textures/T_other") == ""

    def test_bake_mtl_parser_keeps_semantic_glass_flag(self) -> None:
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
        )
        module = self._load_bake_lib(fake_unreal)
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "glass.mtl"
            path.write_text("newmtl pane\nmat glass/pane\n", encoding="utf-8")
            mat = module.read_mtl(path, corpus={"glass/pane": {
                "albedo": "tex/pane.png", "blend": True, "glass": True,
                "bump": "tex/pane_glass_n.png"}})["pane"]
        assert mat.blend
        assert mat.glass
        assert mat.bump == "tex/pane_glass_n.png"

    def test_bake_mtl_parser_keeps_source_refract_contract(self) -> None:
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
        )
        module = self._load_bake_lib(fake_unreal)
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "refract.mtl"
            path.write_text("newmtl rain\nmat effects/rain\n", encoding="utf-8")
            mat = module.read_mtl(path, corpus={"effects/rain": {
                "refract": True, "refract_amount": 0.01,
                "refract_map": "tex/rain_refract_n.png"}})["rain"]
        assert mat.refract
        assert not mat.opaque
        assert mat.refract_amount == 0.01
        assert mat.refract_map == "tex/rain_refract_n.png"

    def test_bake_mtl_parser_keeps_exact_env_cube_identifier(self) -> None:
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
        )
        module = self._load_bake_lib(fake_unreal)
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "wet.mtl"
            path.write_text("newmtl wet\nmat concrete/wet\ncube cubemapdefault\n",
                            encoding="utf-8")
            mat = module.read_mtl(path, corpus={"concrete/wet": {
                "env_cube": "env_cubemap", "wetness": 0.6}})["wet"]
        # The material names `env_cubemap`; the map's own `cube` line says which baked cube that
        # resolved to here, and that is the one the bake must bind.
        assert mat.env_cube == "cubemapdefault"
        assert mat.wetness_driven
        assert mat.wetness_scale == 0.6

    def test_two_maps_naming_one_material_read_one_definition(self) -> None:
        """The regression the corpus exists for.

        Each map's `.mtl` carries only its own facts -- which baked cubemap VBSP patched in, and
        whether the surface is water or a decal here. Both join the same definition, so they cannot
        disagree about the material however far apart the two exports were run.
        """
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
        )
        module = self._load_bake_lib(fake_unreal)
        corpus = {"brick/brickwall001a": {
            "albedo": "tex/brick_brickwall001a.png", "scissor": True,
            "env_cube": "env_cubemap"}}
        with tempfile.TemporaryDirectory() as out:
            first = Path(out) / "a.mtl"
            second = Path(out) / "b.mtl"
            first.write_text(
                "newmtl brick/brickwall001a@cubemapdefault\nmat brick/brickwall001a\n"
                "cube cubemapdefault\n", encoding="utf-8")
            second.write_text(
                "newmtl brick/brickwall001a@c12_34_56\nmat brick/brickwall001a\n"
                "cube c12_34_56\n", encoding="utf-8")
            a = module.read_mtl(first, corpus=corpus)["brick/brickwall001a@cubemapdefault"]
            b = module.read_mtl(second, corpus=corpus)["brick/brickwall001a@c12_34_56"]

        assert a.albedo == b.albedo
        assert a.scissor == b.scissor
        assert a.material_key == b.material_key
        # ...and differ in exactly the one thing their maps own.
        assert a.env_cube == "cubemapdefault"
        assert b.env_cube == "c12_34_56"

    def test_a_surface_whose_material_no_document_names_is_dropped(self) -> None:
        # Silently binding the master's placeholder would render a grey wall with nothing logged.
        # Every caller passes the whole document set its `.mtl` can draw from, so the key resolving
        # in none of them is a defect in the export that wrote it, named where it is noticed.
        warnings: list[str] = []
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
            log_warning=warnings.append,
        )
        module = self._load_bake_lib(fake_unreal)
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "gap.mtl"
            path.write_text("newmtl wall\nmat brick/absent\n", encoding="utf-8")
            assert module.read_mtl(path, corpus={}) == {}
        assert len(warnings) == 1
        assert "brick/absent" in warnings[0]
        assert "wall" in warnings[0]

    def test_a_slot_with_no_mat_line_is_absent_and_unnamed(self) -> None:
        """The exporter writes `newmtl` with no `mat` line when the slot's material name resolved
        no `.vmt`. That absence is the bake's signal to bind the error material, not a defect in
        the export, so the warning for an unknown key must not fire for it."""
        warnings: list[str] = []
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
            log_warning=warnings.append,
        )
        module = self._load_bake_lib(fake_unreal)
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "miss.mtl"
            path.write_text("newmtl gone\n\nnewmtl lid\nmat props/lid\n", encoding="utf-8")
            mats = module.read_mtl(path, corpus={"props/lid": {"albedo": "tex/lid.png"}})
        assert sorted(mats) == ["lid"]
        assert warnings == []

    def test_a_material_key_is_matched_exactly_and_a_case_mismatch_is_named(self) -> None:
        """The corpus is keyed by `shared_corpus.material_key`, which is lower case, and this
        lookup is a plain dict hit. A `.mtl` naming the model header's own mixed-case spelling
        therefore resolves nothing -- which must be said, not appended as None."""
        warnings: list[str] = []
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
            log_warning=warnings.append,
        )
        module = self._load_bake_lib(fake_unreal)
        corpus = {"models/scenery/furniture/milkcrate/milkcrate": {"albedo": "tex/crate.png"}}
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "crate.mtl"
            path.write_text(
                "newmtl milkcrate\nmat models/scenery/furniture/MilkCrate/MilkCrate\n",
                encoding="utf-8")
            assert module.read_mtl(path, corpus=corpus) == {}
            path.write_text(
                "newmtl milkcrate\nmat models/scenery/furniture/milkcrate/milkcrate\n",
                encoding="utf-8")
            assert module.read_mtl(path, corpus=corpus)["milkcrate"].albedo == "tex/crate.png"
        assert len(warnings) == 1
        assert "MilkCrate" in warnings[0]

    def test_a_map_local_definition_outranks_the_corpus(self) -> None:
        # VBSP writes per-water-volume depth-blend instances into a map's own PAKFILE and nowhere
        # else, so a map's local document wins for the keys it carries.
        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: object()),
            MaterialEditingLibrary=object(),
            GeometryScript_Collision=object(),
        )
        module = self._load_bake_lib(fake_unreal)
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "local.mtl"
            path.write_text("newmtl pool\nmat dev/pool_water\nwater 1\n", encoding="utf-8")
            mat = module.read_mtl(
                path,
                corpus={"dev/pool_water": {"albedo": "tex/shared.png"}},
                local={"dev/pool_water": {"albedo": "tex/local.png"}})["pool"]
        assert mat.albedo == "tex/local.png"
        assert mat.water


class BakeErrorMaterialContractTests(unittest.TestCase):
    """A material name that resolves no `.vmt` binds a reproduction of the engine's own error
    material rather than failing the surface.

    VtMB substitutes a synthetic `___error` checkerboard for exactly this case and reports it only
    through a developer-level warning deduped per name, so the shipped install misses 1,152 model
    slots in silence (research case `material-resolution`). The bake substitutes too, and says so
    once per distinct missing name.
    """

    ERROR_PATH = "/ElysiumBaked/Shared/Error/M_ElysiumError"
    LID_PATH = "/ElysiumBaked/Shared/Materials/MI_props_lid"

    class _Tracker:
        """The recipe sink `_emit` registers against."""

        def __init__(self) -> None:
            self.recipes: dict = {}
            self.build_count = 0

        def register(self, stage, object_path, recipe, expected_class="", fresh=True):
            self.recipes[object_path] = recipe
            return True

        def built(self, stage, count=1):
            self.build_count += count

        def pruned(self, stage, count):
            pass

        def stamp(self, asset, object_path):
            pass

        def summary(self, stage):
            return ""

    @staticmethod
    def _fake_unreal(logs, warnings):
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
            AssetRegistryHelpers=SimpleNamespace(
                get_asset_registry=mock.Mock(side_effect=SystemExit(0))),
            LinearColor=lambda *values: values,
            log=logs.append,
            log_warning=warnings.append,
            log_error=logs.append,
        )

    @staticmethod
    def _load_bake_map(fake_unreal, export_root):
        spec = importlib.util.spec_from_file_location(
            "elysium_test_bake_map", REPO / "pipeline/unreal/bake_map.py")
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        with mock.patch.dict(sys.modules, {"unreal": fake_unreal}), \
                mock.patch.dict(os.environ, {"ELYSIUM_EXPORT_ROOT": export_root}):
            # A copy loaded against the fake editor, so the real one cannot leak in from an
            # earlier import; patch.dict restores whatever was there.
            sys.modules.pop("pipeline.unreal.bake_lib", None)
            try:
                # bake_map is an editor entry point, so importing it runs main(). The faked
                # registry scan exits it at once, with every definition made.
                spec.loader.exec_module(module)
            except SystemExit:
                pass
        return module

    def _prop_bake(self, module, tracker, stems):
        """A corpus-shaped prop bake over `stems`, each carrying one resolved slot (`lid`) and one
        whose material name resolved nothing (`gone`)."""
        error_asset = SimpleNamespace(
            get_path_name=lambda: self.ERROR_PATH + ".M_ElysiumError")
        lid_asset = SimpleNamespace(get_path_name=lambda: self.LID_PATH + ".MI_props_lid")
        module.bl.ensure_error_material = lambda: error_asset
        module.bl.build_dynamic_mesh = lambda sections: SimpleNamespace(sections=sections)
        module.bl.mesh_triangle_count = lambda mesh: sum(
            len(section[4]) for section in mesh.sections) // 3
        module.bl.create_static_mesh = lambda *args, **kwargs: SimpleNamespace()
        module.bl.set_complex_collision = lambda mesh: None
        module.bl.prune_package_prefix = lambda *args, **kwargs: 0

        bake = module.Bake("sp_test", tracker, None)
        for stem in stems:
            model = module.bl.ObjModel()
            model.positions = [(0.0, 0.0, 0.0), (10.0, 0.0, 0.0), (0.0, 10.0, 0.0),
                               (0.0, 0.0, 10.0), (10.0, 0.0, 10.0), (0.0, 10.0, 10.0)]
            model.uvs = [(0.0, 0.0)] * 6
            model.groups = {"lid": [0, 1, 2], "gone": [3, 4, 5]}
            bake.prop_models[stem] = model
            lid = module.bl.MatDef("lid")
            lid.material_key = "props/lid"
            bake.prop_mats[stem] = {"lid": lid}
        bake.materials[(bake.shared_mat_pkg, "props/lid")] = lid_asset
        return bake

    def test_a_missed_slot_is_error_bound_receipted_and_named_once(self) -> None:
        logs: list[str] = []
        warnings: list[str] = []
        with tempfile.TemporaryDirectory() as out:
            module = self._load_bake_map(self._fake_unreal(logs, warnings), out)
            tracker = self._Tracker()
            bake = self._prop_bake(module, tracker, ["crate", "barrel"])
            bake.stage_props()

        # Built and receipted, with the error material on exactly the slot that missed. Slots are
        # the mesh's own sorted material groups, so `gone` precedes `lid`.
        recipe = tracker.recipes["%s/%s" % (bake.shared_mesh_pkg, shared_corpus.mesh_asset("crate"))]
        assert recipe["slot_names"] == ["gone", "lid"]
        assert recipe["materials"] == [self.ERROR_PATH, self.LID_PATH]
        assert tracker.build_count == 2
        # One warning for the one distinct missing name, naming the key and the first stem that
        # hit it -- not one per slot instance.
        assert len(warnings) == 1
        assert "'gone'" in warnings[0]
        assert "barrel" in warnings[0]
        # ...and both slot instances counted in the stage summary.
        assert bake.error_keys == {"gone": 2}
        assert any("2 slots error-bound across 2 props" in line for line in logs)

    def test_two_runs_over_the_same_inputs_produce_the_same_recipes(self) -> None:
        recipes = []
        for _ in range(2):
            logs: list[str] = []
            warnings: list[str] = []
            with tempfile.TemporaryDirectory() as out:
                module = self._load_bake_map(self._fake_unreal(logs, warnings), out)
                tracker = self._Tracker()
                self._prop_bake(module, tracker, ["crate"]).stage_props()
            recipes.append(tracker.recipes)
        assert recipes[0] == recipes[1]
        assert self.ERROR_PATH in str(recipes[0])

    def test_a_world_surface_with_no_definition_binds_the_error_material(self) -> None:
        logs: list[str] = []
        warnings: list[str] = []
        with tempfile.TemporaryDirectory() as out:
            module = self._load_bake_map(self._fake_unreal(logs, warnings), out)
            error_asset = SimpleNamespace(get_path_name=lambda: self.ERROR_PATH)
            module.bl.ensure_error_material = lambda: error_asset
            bake = module.Bake("sp_test", self._Tracker(), None)
            assert bake.material_for("brick/absent") is error_asset
            assert bake.material_for("brick/absent") is error_asset
        assert len(warnings) == 1
        assert bake.error_keys == {"brick/absent": 2}

    def test_the_error_material_is_reused_and_authored_unlit(self) -> None:
        """Authored once from constants, so every run leaves the same asset and a recipe naming it
        stays stable. Unlit and emissive-driven, so the checker reads flat like the original."""
        created: list[str] = []
        expressions: list[str] = []
        connected: list[str] = []
        existing = object()

        class _Expression:
            def set_editor_property(self, name, value):
                pass

        mel = SimpleNamespace(
            create_material_expression=lambda material, kind, x, y: (
                expressions.append(kind.__name__), _Expression())[1],
            connect_material_expressions=lambda a, ao, b, bi: connected.append(bi),
            connect_material_property=lambda a, ao, prop: connected.append(str(prop)),
            recompile_material=lambda material: None,
        )
        properties: dict = {}
        material = SimpleNamespace(
            set_editor_property=lambda name, value: properties.__setitem__(name, value))

        def create_asset(name, package, cls, factory):
            created.append("%s/%s" % (package, name))
            return material

        assets = [existing, None]
        editor = SimpleNamespace(
            does_directory_exist=lambda target: True,
            make_directory=lambda target: True,
            save_asset=lambda target, only_if_is_dirty=True: True,
        )

        def named(name):
            return type(name, (object,), {})

        fake_unreal = SimpleNamespace(
            AssetToolsHelpers=SimpleNamespace(
                get_asset_tools=lambda: SimpleNamespace(create_asset=create_asset)),
            MaterialEditingLibrary=mel,
            GeometryScript_Collision=object(),
            EditorAssetLibrary=editor,
            LinearColor=lambda *values: values,
            Material=object,
            MaterialFactoryNew=lambda: object(),
            MaterialShadingModel=SimpleNamespace(MSM_UNLIT="unlit"),
            MaterialProperty=SimpleNamespace(MP_EMISSIVE_COLOR="emissive"),
            load_asset=lambda target: assets.pop(0),
            log_error=lambda message: None,
        )
        for name in ("MaterialExpressionTextureCoordinate", "MaterialExpressionFloor",
                     "MaterialExpressionComponentMask", "MaterialExpressionAdd",
                     "MaterialExpressionMultiply", "MaterialExpressionFrac",
                     "MaterialExpressionCeil", "MaterialExpressionConstant3Vector",
                     "MaterialExpressionLinearInterpolate"):
            setattr(fake_unreal, name, named(name))
        module = BakeTextureImportContractTests._load_bake_lib(fake_unreal)

        assert module.ensure_error_material() is existing
        assert created == []
        assert module.ensure_error_material() is material
        assert created == [module.ERROR_MATERIAL_PATH]
        assert properties["shading_model"] == "unlit"
        assert "MaterialExpressionCeil" in expressions
        assert "emissive" in connected


def test_play_opens_unreals_live_log_console_without_stdout_redirection() -> None:
    submitted = []
    runner = SimpleNamespace(
        run=lambda command, cwd, tail_lines=None, timeout=None: submitted.append((command, cwd))
        or SimpleNamespace(returncode=0)
    )
    config = SimpleNamespace(
        repo_root=REPO,
        project=REPO / "ElysiumUE.uproject",
        export_root=REPO / "exports",
    )
    with mock.patch.object(
            unreal_driver, "editor_executable", return_value=Path("UnrealEditor.exe")):
        unreal_driver.run_play(config, runner)

    assert len(submitted) == 1
    arguments = submitted[0][0]
    assert "-log" in arguments
    assert "-NewConsole" in arguments
    assert "-stdout" not in arguments
    assert "-FullStdOutLogOutput" not in arguments
    assert "-LogCmds=LogElysiumWorld Verbose, LogElysiumIO Verbose" in arguments


def test_texture_bake_enables_commandlet_rendering() -> None:
    submitted = []
    runner = SimpleNamespace(
        run=lambda command, cwd, tail_lines=None, timeout=None: submitted.append((command, cwd))
        or SimpleNamespace(returncode=0)
    )
    config = SimpleNamespace(
        repo_root=REPO,
        project=REPO / "ElysiumUE.uproject",
        unreal_shader_work_root=Path("D:/UnrealCache/ShaderWorking"),
    )
    with mock.patch.object(
            unreal_driver, "editor_executable", return_value=Path("UnrealEditor-Cmd.exe")):
        unreal_driver.bake_maps(config, runner, ["sp_theatre"])

    assert len(submitted) == 1
    assert "-AllowCommandletRendering" in submitted[0][0]
    assert "-shaderworkingdir=D:\\UnrealCache\\ShaderWorking" in submitted[0][0]


def test_work_root_derivations() -> None:
    with tempfile.TemporaryDirectory() as work:
        with mock.patch.dict(
            os.environ,
            {"ELYSIUM_WORK_ROOT": work},
            clear=False,
        ):
            os.environ.pop("ELYSIUM_EXPORT_ROOT", None)
            root = Path(work).resolve()
            assert paths.export_root() == root / "exports"
            assert paths.research_root() == root / "research"
            assert paths.cache_root() == root / "cache"
            assert paths.log_root() == root / "logs"
            assert paths.scratch_root() == root / "scratch"


def test_missing_work_root_has_no_repository_fallback() -> None:
    with mock.patch.dict(os.environ, {}, clear=True):
        with pytest.raises(RuntimeError, match="ELYSIUM_WORK_ROOT"):
            paths.work_root()


@pytest.mark.parametrize(
    "path",
    [
        "Content/Test.uasset",
        "Plugins/ElysiumBaked/Content/Test.umap",
        "scratch/note.py",
        "Source/Fix.cpp.orig",
        "logs/build.log",
        "extracted/maps/sp_theatre.bsp",
        "research/evidence/vampire.gpr",
    ],
)
def test_forbidden_assets_and_backups_are_detected(path: str) -> None:
    assert POLICY.prohibited(path) is not None


def test_authored_source_is_not_prohibited() -> None:
    assert POLICY.prohibited("pipeline/src/elysium_pipeline/formats/bsp.py") is None


@pytest.mark.parametrize(
    "path",
    [
        "Content/ElysiumAuthored/Camera/Profiles/DA_Default.uasset",
        "Content/ElysiumAuthored/Cinematics/Sequences/LS_Test.uasset",
        "Content/ElysiumAuthored/Cinematics/Maps/CameraLab.umap",
    ],
)
def test_project_authored_unreal_packages_have_one_namespace(path: str) -> None:
    assert POLICY.prohibited(path) is None


def test_a_copied_game_file_under_the_authored_namespace_is_still_prohibited() -> None:
    assert POLICY.prohibited("Content/ElysiumAuthored/Camera/copied_game_data.vpk") is not None
