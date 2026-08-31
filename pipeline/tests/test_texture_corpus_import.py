"""Contract tests for `uv run elysium import textures` (the offline phases).

Every fixture is a synthetic texture unit exported into a temporary `export_v2` root from
hand-built TTH/TTZ bytes -- never the real install, never a real staging tree. The one test that
reads the machine's own trees is the legacy cubemap parity check at the bottom, and it skips
itself when either tree is absent.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import struct
import zlib

import numpy as np
import pytest
from typer.testing import CliRunner

from elysium_pipeline import cli
from elysium_pipeline.config import ProjectConfig
from elysium_pipeline.exporters import texture_glb as exporter
from elysium_pipeline.formats.texture_glb.decode import (
    ENVMAP,
    FORMATS,
    SOURCE_TO_GLTF_FACE_ORDER,
    image_size,
)
from elysium_pipeline.formats.unit_contract.container import encode_glb
from elysium_pipeline.importers import texture_cube, texture_roles
from elysium_pipeline.importers import textures as importer
from elysium_pipeline.importers.texture_dds import decode_image, parse_dds, parse_ktx2

RUNNER = CliRunner()

FMT_RGBA8888, FMT_BGR888, FMT_BGRA8888, FMT_DXT1, FMT_DXT5 = 0, 3, 12, 13, 15
FMT_DXT3, FMT_UVWQ8888 = 14, 23
FLAG_POINT, FLAG_TRILINEAR, FLAG_CLAMPS, FLAG_CLAMPT = 0x1, 0x2, 0x4, 0x8
FLAG_NOMIP, FLAG_NOLOD, FLAG_ALLMIPS = 0x100, 0x200, 0x400


# --- fixtures ------------------------------------------------------------------------------------


def _pair(fmt=FMT_DXT1, *, width=4, height=4, mips=1, inline=1, cubemap=False, frames=1,
          flags=0, images=None):
    """A synthetic TTH/TTZ pair. `images(source_mip, frame, face, w, h) -> bytes` overrides the
    default constant fill; a cubemap stores seven VTF faces, the seventh a spheremap."""

    info = FORMATS[fmt]
    faces = 7 if cubemap else 1
    levels = []
    for source_mip in range(mips):
        shift = mips - source_mip - 1
        w, h = max(1, width >> shift), max(1, height >> shift)
        size = image_size(w, h, info)
        parts = []
        for frame in range(frames):
            for face in range(faces):
                if images is not None:
                    data = images(source_mip, frame, face, w, h)
                    assert len(data) == size
                else:
                    data = bytes([(source_mip * 31 + frame * 11 + face + 1) & 0xFF]) * size
                parts.append(data)
        levels.append(b"".join(parts))
    low = bytes([0x5A]) * 128
    vtf = bytearray(64)
    struct.pack_into("<4sIII", vtf, 0, b"VTF\0", 7, 1, 64)
    struct.pack_into("<HHIHH", vtf, 16, width, height, (ENVMAP if cubemap else 0) | flags,
                     frames, 0)
    struct.pack_into("<3f", vtf, 32, 0.1, 0.2, 0.3)
    struct.pack_into("<f", vtf, 48, 1.0)
    struct.pack_into("<IB", vtf, 52, fmt, mips)
    struct.pack_into("<IBB", vtf, 57, 13, 16, 16)
    inline_data = b"".join(levels[:inline])
    external = b"".join(levels[inline:])
    ttz = zlib.compress(external) if external else None
    table = bytearray()
    raw_offset = 64 + len(low)
    for level in levels:
        table.extend(struct.pack("<II", raw_offset, 0))
        raw_offset += len(level)
    table.extend(struct.pack("<II", raw_offset, len(ttz or b"")))
    blob = bytes(vtf) + low + inline_data
    tth = struct.pack("<4sHBBI", b"TTH\0", 1, mips, inline, len(blob)) + bytes(table) + blob
    return tth, ttz


def _publish(export_v2_root: Path, key: str, fmt=FMT_DXT1, **kwargs) -> Path:
    """Export one synthetic unit as `<export_v2_root>/textures/<key>.glb`."""

    tth, ttz = _pair(fmt, **kwargs)
    base = f"materials/{key}"
    files = {base + ".tth": tth}
    if ttz is not None:
        files[base + ".ttz"] = ttz
    index = {path: ("loose", Path("synthetic") / path) for path in files}
    return exporter.export(index, base + ".tth", Path(export_v2_root) / "textures",
                           read_bytes=lambda _index, path: files.get(path))


def _material(export_v2_root: Path, name: str, bindings: dict[str, str]) -> Path:
    """A minimal material unit binding `{parameter: texture key}`."""

    document = {
        "asset": {"version": "2.0"},
        "extensionsUsed": [texture_roles.MATERIAL_EXTENSION],
        "extensions": {texture_roles.MATERIAL_EXTENSION: {
            "identity": {"asset": f"vtmb:material:{name}"},
            "dependencies": [
                {"role": "texture", "parameter": parameter, "asset": f"vtmb:texture:{key}",
                 "sourcePath": f"materials/{key}.tth", "resolved": True}
                for parameter, key in bindings.items()
            ],
        }},
    }
    destination = Path(export_v2_root) / "materials" / (name + ".glb")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(encode_glb(document))
    return destination


def _manifest(root: Path) -> dict:
    return json.loads((root / importer.MANIFEST_NAME).read_text(encoding="utf-8"))


def _entries(root: Path) -> dict[str, dict]:
    return {entry["assetPath"]: entry for entry in _manifest(root)["assets"]}


# --- the DDS rewrap --------------------------------------------------------------------------------


def test_stage_writes_a_dx10_dds_carrying_every_ktx2_level(tmp_path):
    unit = _publish(tmp_path / "v2", "syn/wall", FMT_DXT1, width=8, height=8, mips=4, inline=2)
    result = importer.stage_textures(tmp_path / "v2", tmp_path / "stage")

    assert result.failures == []
    assert result.staged == 1 and result.assets == 1
    dds = parse_dds((tmp_path / "stage" / "syn" / "wall.dds").read_bytes())
    _, binary = __import__("elysium_pipeline.formats.unit_contract.container",
                           fromlist=["read_glb"]).read_glb(unit)
    ktx = parse_ktx2(binary)
    # Unreal 5.8 refuses a BC DDS, so the staged file is the lane's own decode of every level.
    assert (dds.kind, dds.dxgi, dds.width, dds.height, dds.mip_count, dds.slices) == \
        ("bgra8", 87, 8, 8, 4, 1)
    for level, blocks in enumerate(ktx.levels):
        w, h = max(1, 8 >> level), max(1, 8 >> level)
        assert np.array_equal(dds.decode(0, level), decode_image("bc1", blocks, w, h))
        assert len(dds.images[0][level]) == w * h * 4
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Textures/syn/T_wall"]
    assert entry["class"] == "Texture2D" and entry["dds"] == "syn/wall.dds"
    assert entry["stagedFormat"] == "bgra8"
    assert entry["compression"] == "default" and entry["mipGen"] == "leave-existing"
    assert entry["expected"] == {"width": 8, "height": 8, "mips": 4, "faces": 1, "slices": 1}
    assert entry["recipe"] == {"unitSha256": entry["unitSha256"],
                               "settingsVersion": importer.SETTINGS_VERSION,
                               "role": "colour", "srgb": True, "compression": "default",
                               "twin": False}
    manifest = _manifest(tmp_path / "stage")
    assert manifest["schemaVersion"] == importer.MANIFEST_SCHEMA
    assert manifest["packageRoot"] == importer.PACKAGE_ROOT and manifest["select"] is None


def test_rgb8_widens_to_rgba_with_opaque_alpha(tmp_path):
    def images(_mip, _frame, _face, w, h):
        return bytes(range(w * h * 3 % 256)) * 0 + bytes((i * 7) & 0xFF for i in range(w * h * 3))

    _publish(tmp_path / "v2", "syn/flat", FMT_BGR888, width=4, height=2, images=images)
    importer.stage_textures(tmp_path / "v2", tmp_path / "stage")

    dds = parse_dds((tmp_path / "stage" / "syn" / "flat.dds").read_bytes())
    assert (dds.kind, dds.dxgi) == ("rgba8", 28)
    pixels = dds.decode()
    assert pixels.shape == (2, 4, 4) and (pixels[:, :, 3] == 255).all()
    sidecar = json.loads((tmp_path / "stage" / "syn" / "flat.provenance.json").read_text())
    assert sidecar["expandedFromRgb8"] is True and sidecar["vkFormat"] == 23
    assert _entries(tmp_path / "stage")["/ElysiumBaked/Textures/syn/T_flat"]["compression"] == "uncompressed"


def test_frames_stage_as_an_array_slice_per_frame(tmp_path):
    _publish(tmp_path / "v2", "syn/anim", FMT_DXT5, width=8, height=8, mips=2, frames=3)
    importer.stage_textures(tmp_path / "v2", tmp_path / "stage")

    dds = parse_dds((tmp_path / "stage" / "syn" / "anim.dds").read_bytes())
    assert (dds.kind, dds.slices, dds.mip_count, dds.is_cube) == ("bgra8", 3, 2, False)
    # The default fill is `frame * 11` apart, so each slice must be its own frame at every level:
    # the staged pixels are the lane's decode of that frame's constant-byte BC3 blocks.
    for frame in range(3):
        for level in range(2):
            fill = ((1 - level) * 31 + frame * 11 + 1) & 0xFF
            w = h = 8 >> level
            blocks = bytes([fill]) * image_size(w, h, FORMATS[FMT_DXT5])
            assert np.array_equal(dds.decode(frame, level), decode_image("bc3", blocks, w, h))
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Textures/syn/TA_anim"]
    assert entry["class"] == "Texture2DArray" and entry["expected"]["slices"] == 3


def test_partial_and_single_mip_chains_are_recorded(tmp_path):
    _publish(tmp_path / "v2", "syn/short", FMT_DXT1, width=16, height=16, mips=2, inline=2)
    _publish(tmp_path / "v2", "syn/single", FMT_DXT1, width=16, height=16, mips=1)
    importer.stage_textures(tmp_path / "v2", tmp_path / "stage")

    short = json.loads((tmp_path / "stage" / "syn" / "short.provenance.json").read_text())
    single = json.loads((tmp_path / "stage" / "syn" / "single.provenance.json").read_text())
    assert short["partialMipChain"] is True and short["mipCount"] == 2
    assert single["partialMipChain"] is False
    entries = _entries(tmp_path / "stage")
    assert entries["/ElysiumBaked/Textures/syn/T_short"]["mipGen"] == "leave-existing"
    assert entries["/ElysiumBaked/Textures/syn/T_single"]["mipGen"] == "no-mipmaps"


# --- cubemaps ---------------------------------------------------------------------------------------


def _bc1_face_blocks(seed: int, w: int, h: int) -> bytes:
    rng = np.random.default_rng(seed)
    blocks = image_size(w, h, FORMATS[FMT_DXT1]) // 8
    out = bytearray()
    for _ in range(blocks):
        c0, c1 = sorted(rng.integers(0, 0xFFFF, size=2, dtype=np.uint32), reverse=True)
        out += struct.pack("<HHI", int(c0), int(c1), int(rng.integers(0, 2**32, dtype=np.uint64)))
    return bytes(out)


def test_cube_faces_return_to_source_order_and_orientation(tmp_path):
    originals: dict[tuple[int, int], bytes] = {}

    def images(mip, _frame, face, w, h):
        data = _bc1_face_blocks(mip * 10 + face, w, h)
        originals[(mip, face)] = data
        return data

    _publish(tmp_path / "v2", "syn/env", FMT_DXT1, width=8, height=8, mips=2, inline=2,
             cubemap=True, images=images)
    result = importer.stage_textures(tmp_path / "v2", tmp_path / "stage")
    assert result.failures == []

    dds = parse_dds((tmp_path / "stage" / "syn" / "env.dds").read_bytes())
    assert dds.is_cube and dds.slices == 6 and dds.mip_count == 2 and dds.kind == "bgra8"
    # DDS face `s` is source face `s`, texel for texel, at every level (source mip 1 is level
    # 0): the glTF rotation is undone on the blocks and only then decoded, so the staged face is
    # exactly the decode of the original blocks.
    for source_face in range(6):
        for level in range(2):
            w = h = 8 >> level
            assert np.array_equal(dds.decode(source_face, level),
                                  decode_image("bc1", originals[(1 - level, source_face)], w, h))
    sidecar = json.loads((tmp_path / "stage" / "syn" / "env.provenance.json").read_text())
    mapping = sidecar["faceMapping"]
    assert [row["unrealFace"] for row in mapping] == ["+X", "-X", "+Y", "-Y", "+Z", "-Z"]
    assert [row["ktxFace"] for row in mapping] == [SOURCE_TO_GLTF_FACE_ORDER.index(s) for s in range(6)]
    assert [row["transform"] for row in mapping] == \
        ["rotate-ccw", "rotate-cw", "rotate-180", "identity", "identity", "rotate-180"]
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Textures/syn/TC_env"]
    assert entry["class"] == "TextureCube" and entry["expected"]["faces"] == 6


def test_emitted_cube_is_seam_continuous_and_every_other_rotation_is_not(tmp_path):
    size = 16
    faces = texture_cube.rasterize_cube(size, lambda d: np.concatenate(
        [(d + 1.0) / 2.0, np.ones(d.shape[:-1] + (1,))], axis=-1))

    def images(_mip, _frame, face, w, h):
        assert (w, h) == (size, size)
        if face == 6:
            return bytes(w * h * 4)
        return faces[face][:, :, [2, 1, 0, 3]].tobytes()   # BGRA8888 stores B first

    _publish(tmp_path / "v2", "syn/cont", FMT_BGRA8888, width=size, height=size, cubemap=True,
             images=images)
    result = importer.stage_textures(tmp_path / "v2", tmp_path / "stage")
    assert result.failures == []

    dds = parse_dds((tmp_path / "stage" / "syn" / "cont.dds").read_bytes())
    emitted = [dds.decode(face, 0) for face in range(6)]
    for face in range(6):
        assert np.array_equal(emitted[face], faces[face])
    baseline = texture_cube.seam_error(emitted)
    assert baseline < 6.0
    for face in range(6):
        for turns in (1, 2, 3):
            variant = list(emitted)
            variant[face] = np.rot90(emitted[face], turns)
            assert texture_cube.seam_error(variant) > baseline * 3


# --- roles, sRGB and twins -----------------------------------------------------------------------


@pytest.mark.parametrize("parameters, expected", [
    (set(), ("colour", True, False)),
    ({"$basetexture"}, ("colour", True, False)),
    ({"$BaseTexture", "$detail"}, ("colour", True, False)),
    ({"$bumpmap"}, ("data-normal", False, False)),
    ({"$normalmap", "$bumpmap2"}, ("data-normal", False, False)),
    ({"$envmapmask"}, ("data-mask", False, False)),
    ({"$burntexture"}, ("data-mask", False, False)),
    ({"$BumpMap"}, ("data-normal", False, False)),
    # A mixed data-only set is a mask: `data-normal` needs every data binding normal-class.
    ({"$bumpmap", "$envmapmask"}, ("data-mask", False, False)),
    ({"$normalmap", "$burntexture"}, ("data-mask", False, False)),
    ({"$basetexture", "$envmapmask"}, ("colour", True, True)),
    ({"$basetexture", "$bumpmap"}, ("colour", True, True)),
])
def test_role_rule(parameters, expected):
    assert texture_roles.role_for(parameters) == expected


@pytest.mark.parametrize("parameters, expected", [
    ({"$bumpmap"}, "data-normal"),
    ({"$BUMPMAP", "$NormalMap"}, "data-normal"),
    ({"$basetexture", "$bumpmap"}, "data-normal"),          # the twin of a colour+bump conflict
    ({"$basetexture", "$bumpmap", "$envmapmask"}, "data-mask"),
    ({"$bumpmap", "$EnvMapMask"}, "data-mask"),
    ({"$envmapmask"}, "data-mask"),
    (set(), "data-mask"),
])
def test_data_role_needs_every_data_binding_normal_class_for_a_normal(parameters, expected):
    assert texture_roles.data_role(parameters) == expected


def test_bindings_decide_srgb_and_an_unbound_texture_is_colour(tmp_path):
    for key in ("syn/albedo", "syn/bump", "syn/mask", "syn/orphan"):
        _publish(tmp_path / "v2", key)
    _material(tmp_path / "v2", "m/a", {"$basetexture": "syn/albedo", "$bumpmap": "syn/bump"})
    _material(tmp_path / "v2", "m/b", {"$envmapmask": "syn/mask", "$basetexture": "syn/albedo"})
    importer.stage_textures(tmp_path / "v2", tmp_path / "stage")

    entries = _entries(tmp_path / "stage")
    albedo = entries["/ElysiumBaked/Textures/syn/T_albedo"]
    assert (albedo["role"], albedo["srgb"], albedo["roleEvidence"]) == ("colour", True, ["$basetexture"])
    bump = entries["/ElysiumBaked/Textures/syn/T_bump"]
    assert (bump["role"], bump["srgb"], bump["roleConflict"]) == ("data-normal", False, False)
    mask = entries["/ElysiumBaked/Textures/syn/T_mask"]
    assert (mask["role"], mask["srgb"]) == ("data-mask", False)
    orphan = entries["/ElysiumBaked/Textures/syn/T_orphan"]
    assert (orphan["role"], orphan["srgb"], orphan["roleEvidence"]) == ("colour", True, [])
    assert len(entries) == 4


def test_a_conflict_texture_gets_a_linear_twin(tmp_path):
    _publish(tmp_path / "v2", "syn/galply")
    _material(tmp_path / "v2", "m/wood", {"$basetexture": "syn/galply"})
    _material(tmp_path / "v2", "m/shiny", {"$envmapmask": "syn/galply"})
    result = importer.stage_textures(tmp_path / "v2", tmp_path / "stage")

    assert result.twins == 1 and result.assets == 2
    entries = _entries(tmp_path / "stage")
    colour = entries["/ElysiumBaked/Textures/syn/T_galply"]
    twin = entries["/ElysiumBaked/Textures/syn/T_galply_linear"]
    assert colour["roleConflict"] is True and colour["srgb"] is True and colour["twinOf"] is None
    assert (twin["role"], twin["srgb"], twin["twinOf"]) == \
        ("data-mask", False, "/ElysiumBaked/Textures/syn/T_galply")
    assert twin["dds"] == colour["dds"] == "syn/galply.dds"
    assert twin["recipe"]["twin"] is True and colour["recipe"]["twin"] is False
    assert twin["provenance"] == "syn/galply_linear.provenance.json"
    twin_sidecar = json.loads((tmp_path / "stage" / "syn" / "galply_linear.provenance.json").read_text())
    assert twin_sidecar["assetPath"] == twin["assetPath"] and twin_sidecar["role"] == "data-mask"
    assert twin_sidecar["roleEvidence"] == ["$basetexture", "$envmapmask"]
    assert twin_sidecar["assetId"] == "vtmb:texture:syn/galply"


def test_a_conflict_with_a_bump_binding_twins_as_a_normal(tmp_path):
    _publish(tmp_path / "v2", "syn/dudv")
    _material(tmp_path / "v2", "m/a", {"$basetexture": "syn/dudv"})
    _material(tmp_path / "v2", "m/b", {"$bumpmap": "syn/dudv"})
    importer.stage_textures(tmp_path / "v2", tmp_path / "stage")
    assert _entries(tmp_path / "stage")["/ElysiumBaked/Textures/syn/T_dudv_linear"]["role"] == "data-normal"


# --- sampling, naming and guards ------------------------------------------------------------------


def test_sampling_flags_map_onto_asset_settings(tmp_path):
    _publish(tmp_path / "v2", "syn/ui", flags=FLAG_POINT | FLAG_CLAMPS | FLAG_NOLOD)
    _publish(tmp_path / "v2", "syn/tri", flags=FLAG_TRILINEAR | FLAG_CLAMPT | FLAG_ALLMIPS)
    _publish(tmp_path / "v2", "syn/plain", flags=FLAG_NOMIP)
    importer.stage_textures(tmp_path / "v2", tmp_path / "stage")

    entries = _entries(tmp_path / "stage")
    ui = entries["/ElysiumBaked/Textures/syn/T_ui"]
    assert (ui["addressX"], ui["addressY"], ui["filter"], ui["neverStream"]) == ("clamp", "wrap", "nearest", True)
    tri = entries["/ElysiumBaked/Textures/syn/T_tri"]
    assert (tri["addressX"], tri["addressY"], tri["filter"], tri["neverStream"]) == ("wrap", "clamp", "trilinear", True)
    plain = entries["/ElysiumBaked/Textures/syn/T_plain"]
    assert (plain["filter"], plain["neverStream"]) == ("default", False)
    sidecar = json.loads((tmp_path / "stage" / "syn" / "plain.provenance.json").read_text())
    assert sidecar["sampling"]["noMip"] is True and sidecar["flags"] == FLAG_NOMIP


def test_sidecar_carries_every_contracted_key(tmp_path):
    _publish(tmp_path / "v2", "syn/keys", FMT_DXT5, width=8, height=4, mips=2, inline=1)
    importer.stage_textures(tmp_path / "v2", tmp_path / "stage")
    sidecar = json.loads((tmp_path / "stage" / "syn" / "keys.provenance.json").read_text())
    assert set(sidecar) >= {
        "assetId", "texturePath", "unitSchemaVersion", "unitSha256", "payloadSha256",
        "sourceFormat", "sourceFormatEnum", "vkFormat", "vkFormatName", "vtfVersion",
        "tthVersion", "flags", "reflectivity", "bumpScale", "startFrame", "width", "height",
        "frames", "faces", "mipCount", "sourceMipCount", "sampling", "members", "role",
        "roleEvidence", "roleConflict", "twinOf", "faceMapping", "expandedFromRgb8",
        "partialMipChain", "assetPath", "unitGlb", "stagedFormat", "ddsSha256",
    }
    assert sidecar["stagedFormat"] == "bgra8"
    assert sidecar["assetId"] == "vtmb:texture:syn/keys"
    assert sidecar["sourceFormat"] == "DXT5" and sidecar["sourceFormatEnum"] == 15
    assert sidecar["vkFormat"] == 137 and sidecar["vkFormatName"] == "VK_FORMAT_BC3_UNORM_BLOCK"
    assert sidecar["reflectivity"] == pytest.approx([0.1, 0.2, 0.3])
    assert sidecar["bumpScale"] == 1.0 and sidecar["vtfVersion"] == "7.1" and sidecar["tthVersion"] == 1
    assert (sidecar["width"], sidecar["height"], sidecar["mipCount"]) == (8, 4, 2)
    assert [m["role"] for m in sidecar["members"]] == ["tth", "ttz"]
    assert all(m["sha256"] and m["byteLength"] > 0 for m in sidecar["members"])
    assert sidecar["unitGlb"] == "textures/syn/keys.glb"


def test_asset_names_fold_illegal_characters_and_refuse_collisions(tmp_path):
    _publish(tmp_path / "v2", "syn/odd name-1")
    _publish(tmp_path / "v2", "syn/twin-a")
    _publish(tmp_path / "v2", "syn/twin_a")
    result = importer.stage_textures(tmp_path / "v2", tmp_path / "stage")

    entries = _entries(tmp_path / "stage")
    assert "/ElysiumBaked/Textures/syn/T_odd_name_1" in entries
    assert len(entries) == 1
    reasons = dict(result.failures)
    assert "asset path collides with syn/twin_a" in reasons["syn/twin-a"]
    assert "asset path collides with syn/twin-a" in reasons["syn/twin_a"]
    assert not (tmp_path / "stage" / "syn" / "twin-a.dds").exists()


def test_the_key_guard_refuses_anything_that_could_leave_the_roots():
    for bad in ("../x", "a/../b", "a/b:c", "a\\b", ""):
        with pytest.raises(importer.TextureImportError):
            importer.asset_path_for(bad, "Texture2D")


# --- idempotency, pruning and selection -----------------------------------------------------------


def test_staging_twice_rewrites_nothing_and_prunes_orphans(tmp_path):
    _publish(tmp_path / "v2", "syn/a")
    _publish(tmp_path / "v2", "syn/b")
    root = tmp_path / "stage"
    first = importer.stage_textures(tmp_path / "v2", root)
    assert (first.staged, first.unchanged) == (2, 0)
    stamp = (root / "syn" / "a.dds").stat().st_mtime_ns

    (root / "syn" / "a.built.dds").write_bytes(b"built")            # a phase-2 product: kept
    (root / "syn" / "gone.dds").write_bytes(b"stale")               # an orphan: pruned
    (root / "syn" / "gone.built.dds").write_bytes(b"stale")         # its phase-2 product: pruned
    (root / "old" / "deep").mkdir(parents=True)
    (root / "old" / "deep" / "x.provenance.json").write_text("{}")  # a retired tree: pruned
    (root / importer.IMPORT_REPORT_NAME).write_text("{}")           # a root report: kept
    second = importer.stage_textures(tmp_path / "v2", root)

    assert (second.staged, second.unchanged, second.pruned) == (0, 2, 3)
    assert (root / "syn" / "a.dds").stat().st_mtime_ns == stamp
    assert (root / "syn" / "a.built.dds").exists()
    assert not (root / "syn" / "gone.dds").exists() and not (root / "old").exists()
    assert (root / importer.IMPORT_REPORT_NAME).exists()


def test_a_changed_unit_restages_and_a_settings_bump_would_too(tmp_path, monkeypatch):
    _publish(tmp_path / "v2", "syn/a")
    root = tmp_path / "stage"
    importer.stage_textures(tmp_path / "v2", root)
    _publish(tmp_path / "v2", "syn/a", FMT_DXT5)          # same key, new bytes
    result = importer.stage_textures(tmp_path / "v2", root)
    assert (result.staged, result.unchanged) == (1, 0)
    assert json.loads((root / "syn" / "a.provenance.json").read_text())["vkFormat"] == 137

    monkeypatch.setattr(importer, "SETTINGS_VERSION", "elysium-texture-import-vNext")
    result = importer.stage_textures(tmp_path / "v2", root)
    assert (result.staged, result.unchanged) == (1, 0)


def test_select_narrows_the_units_and_the_prune_scope(tmp_path):
    _publish(tmp_path / "v2", "hud/signs/note")
    _publish(tmp_path / "v2", "hud/other")
    _publish(tmp_path / "v2", "wood/plank")
    root = tmp_path / "stage"
    importer.stage_textures(tmp_path / "v2", root)
    assert _manifest(root)["pruneScope"] == "/ElysiumBaked/Textures/"
    (root / "hud" / "signs" / "stale.dds").write_bytes(b"x")
    (root / "wood" / "stale.dds").write_bytes(b"x")

    result = importer.stage_textures(tmp_path / "v2", root, select="HUD/Signs/")
    assert result.assets == 1 and result.pruned == 1
    assert list(_entries(root)) == ["/ElysiumBaked/Textures/hud/signs/T_note"]
    manifest = _manifest(root)
    assert manifest["select"] == "hud/signs"
    assert manifest["pruneScope"] == "/ElysiumBaked/Textures/hud/signs/"
    assert (root / "wood" / "stale.dds").exists() and (root / "hud" / "other.dds").exists()
    assert not (root / "hud" / "signs" / "stale.dds").exists()

    with pytest.raises(importer.TextureImportError, match="matching"):
        importer.stage_textures(tmp_path / "v2", root, select="nothing/here")


def test_select_is_a_directory_so_hud_never_reaches_hudson(tmp_path):
    _publish(tmp_path / "v2", "hud/note")
    _publish(tmp_path / "v2", "hudson/river")
    _publish(tmp_path / "v2", "odd dir/x")
    root = tmp_path / "stage"
    importer.stage_textures(tmp_path / "v2", root)
    (root / "hudson" / "stale.dds").write_bytes(b"x")
    (root / "hud" / "stale.dds").write_bytes(b"x")

    result = importer.stage_textures(tmp_path / "v2", root, select="hud")

    assert list(_entries(root)) == ["/ElysiumBaked/Textures/hud/T_note"]
    assert result.pruned == 1
    assert (root / "hudson" / "stale.dds").exists() and (root / "hudson" / "river.dds").exists()
    assert not (root / "hud" / "stale.dds").exists()
    assert _manifest(root)["pruneScope"] == "/ElysiumBaked/Textures/hud/"
    assert importer.in_selection("hudson/river", "hud") is False
    assert importer.in_selection("hud/deep/er", "hud") is True
    assert importer.in_selection("hud/note", "hud/note") is True
    # The scope folds the directory exactly as the asset paths are folded.
    assert importer.prune_scope_for("odd dir") == "/ElysiumBaked/Textures/odd_dir/"
    importer.stage_textures(tmp_path / "v2", root, select="odd dir")
    assert list(_entries(root)) == ["/ElysiumBaked/Textures/odd_dir/T_x"]
    # A selector that names no directory of the staging tree prunes nothing.
    assert result.pruned == 1


# --- measure -------------------------------------------------------------------------------------


def test_measure_reports_the_delta_between_staged_and_built(tmp_path):
    _publish(tmp_path / "v2", "syn/same", FMT_DXT1, width=8, height=8)
    _publish(tmp_path / "v2", "syn/drift", FMT_DXT1, width=8, height=8)
    _publish(tmp_path / "v2", "syn/nobuild", FMT_DXT1, width=8, height=8)
    root = tmp_path / "stage"
    importer.stage_textures(tmp_path / "v2", root)

    same = (root / "syn" / "same.dds").read_bytes()
    (root / "syn" / "same.built.dds").write_bytes(same)
    drift = bytearray((root / "syn" / "drift.dds").read_bytes())
    drift[-16:] = bytes(255 - b for b in drift[-16:])       # last four texels inverted
    (root / "syn" / "drift.built.dds").write_bytes(bytes(drift))

    result = importer.measure_textures(root)
    assert (result.measured, result.unmeasured, result.failures) == (2, 1, [])
    assert result.max_of_max > 0 and result.worst[0]["unit"] == "vtmb:texture:syn/drift"
    report = json.loads((root / importer.MEASURE_NAME).read_text())
    by_unit = {row["unit"]: row for row in report["units"]}
    assert by_unit["vtmb:texture:syn/same"] == {"unit": "vtmb:texture:syn/same",
                                                "dds": "syn/same.dds", "maxDelta": 0,
                                                "meanDelta": 0.0}
    assert by_unit["vtmb:texture:syn/drift"]["maxDelta"] > 0


def test_measure_accepts_a_legacy_fourcc_built_dds(tmp_path):
    """Unreal writes the built mip back as BC (legacy fourCC); the staged side is BGRA8. The
    same decoder reads both, so a built file holding the unit's own blocks measures zero."""

    unit = _publish(tmp_path / "v2", "syn/legacy", FMT_DXT5, width=4, height=4)
    root = tmp_path / "stage"
    importer.stage_textures(tmp_path / "v2", root)
    from elysium_pipeline.formats.unit_contract.container import read_glb
    _, binary = read_glb(unit)
    blocks = parse_ktx2(binary).levels[0]
    header = bytearray(128)
    struct.pack_into("<4sIIIIIII", header, 0, b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000,
                     4, 4, 16, 0, 1)
    struct.pack_into("<II4s", header, 76, 32, 0x4, b"DXT5")
    struct.pack_into("<I", header, 108, 0x1000)
    (root / "syn" / "legacy.built.dds").write_bytes(bytes(header) + blocks)
    result = importer.measure_textures(root)
    assert result.measured == 1 and result.max_of_max == 0


def test_bc_decoders_agree_with_a_hand_decoded_block():
    # BC1: c0 = pure red (0xF800), c1 = pure blue (0x001F), every texel index 0 -> red, opaque.
    block = struct.pack("<HHI", 0xF800, 0x001F, 0)
    pixels = decode_image("bc1", block, 4, 4)
    assert pixels.shape == (4, 4, 4) and (pixels == [255, 0, 0, 255]).all()
    # BC1 three-colour mode with index 3 -> transparent black.
    block = struct.pack("<HHI", 0x001F, 0xF800, 0xFFFFFFFF)
    assert (decode_image("bc1", block, 4, 4) == [0, 0, 0, 0]).all()
    # BC3: alpha table a0=255 > a1=0, all codes 0 -> alpha 255; colours as above.
    block = bytes([255, 0, 0, 0, 0, 0, 0, 0]) + struct.pack("<HHI", 0xF800, 0x001F, 0)
    assert (decode_image("bc3", block, 4, 4) == [255, 0, 0, 255]).all()


# --- the command -----------------------------------------------------------------------------------


def _configured(monkeypatch, tmp_path) -> ProjectConfig:
    work = tmp_path / "work"
    (work / "logs").mkdir(parents=True)
    repo = tmp_path / "repo"
    repo.mkdir()
    config = ProjectConfig(
        repo_root=repo,
        project=repo / "ElysiumUE.uproject",
        game_root=None,
        work_root=work,
        export_root=work / "exports",
        export_v2_root=tmp_path / "v2",
        ue_root=None,
        unreal_zen_data_path=None,
        unreal_local_data_cache_path=None,
        unreal_shader_work_root=None,
        temp_root=None,
    )
    monkeypatch.setattr(cli.CliState, "resolve", lambda self, **kwargs: config)
    return config


def test_the_command_stages_only_and_reports_one_line(monkeypatch, tmp_path):
    config = _configured(monkeypatch, tmp_path)
    _publish(tmp_path / "v2", "syn/a")
    _publish(tmp_path / "v2", "other/b")

    result = RUNNER.invoke(cli.app, ["import", "textures", "--stage-only", "--select", "syn"])
    assert result.exit_code == 0, result.output
    assert "texture staging: 1 assets" in result.output
    root = importer.staging_root(config.work_root)
    assert (root / "syn" / "a.dds").is_file() and (root / importer.MANIFEST_NAME).is_file()
    assert not (root / "other").exists()


def test_the_command_fails_when_a_unit_cannot_be_staged(monkeypatch, tmp_path):
    _configured(monkeypatch, tmp_path)
    unit = _publish(tmp_path / "v2", "syn/a")
    unit.write_bytes(b"not a glb at all")

    result = RUNNER.invoke(cli.app, ["import", "textures", "--stage-only"])
    assert result.exit_code == int(cli.ExitCode.OFFLINE_EXPORT), result.output
    assert "1 texture unit(s) could not be staged" in result.output


# --- parity with the legacy env cubemaps ---------------------------------------------------------


def _legacy_cube_pairs() -> list[tuple[Path, Path]]:
    """`(legacy tex/cube/envmap_<name>.dds, v2 textures/envmap/<name>.glb)` pairs on this machine."""

    export_root = os.environ.get("ELYSIUM_EXPORT_ROOT", "").strip()
    v2_root = os.environ.get("ELYSIUM_EXPORT_V2_ROOT", "").strip()
    if not export_root or not v2_root:
        return []
    pairs: dict[str, tuple[Path, Path]] = {}
    for legacy in sorted(Path(export_root).glob("*/tex/cube/envmap_*.dds")):
        name = legacy.stem[len("envmap_"):]
        unit = Path(v2_root) / "textures" / "envmap" / f"{name}.glb"
        if unit.is_file() and name not in pairs:
            pairs[name] = (legacy, unit)
    return list(pairs.values())


def test_staged_cubes_match_the_legacy_env_cubemap_faces(tmp_path):
    """The legacy bake imports env cubes in VTF order with no rotation (`tex_to_png.cubemap_dds`)
    and the game draws them right today; the staged DDS must show the same face in each slot."""

    pairs = _legacy_cube_pairs()
    if not pairs:
        pytest.skip("no legacy env cubemap beside a v2 cubemap unit on this machine")
    v2_root = Path(os.environ["ELYSIUM_EXPORT_V2_ROOT"])
    compared = 0
    for legacy_path, unit in pairs[:8]:
        key = importer.unit_key(v2_root, unit)
        root = tmp_path / "stage" / key.replace("/", "_")
        importer.stage_textures(v2_root, root, select=key, bindings={})
        staged = parse_dds((root / f"{key}.dds").read_bytes())
        legacy = parse_dds(legacy_path.read_bytes())
        assert legacy.is_cube and staged.is_cube
        for face in range(6):
            ours = staged.decode(face, 0).astype(np.int16)
            theirs = legacy.decode(face, 0).astype(np.int16)
            assert ours.shape == theirs.shape, (key, face)
            delta = np.abs(ours[:, :, :3] - theirs[:, :, :3])
            # Two BC1 decoders may round the 5:6:5 expansion a step apart; a wrong face or a
            # rotation is tens of levels off across the whole image.
            assert delta.max() <= 4 and delta.mean() < 1.0, (key, face, delta.max(), delta.mean())
        compared += 1
    assert compared > 0


# --- review follow-ups: formats, failure protection, truncated products ------------------------


def test_uvwq_stages_as_unorm_rgba_and_dxt3_as_bc2(tmp_path):
    """`R8G8B8A8_UINT` is labelled DXGI 28: Unreal's DDS reader does not swizzle DXGI 30 into
    its BGRA8 source and would swap U and W. The bytes are untouched; the sidecar keeps the
    truth (`vkFormat` 41) and the asset is always a linear data texture."""

    def images(_mip, _frame, _face, w, h):
        return bytes((i * 13) & 0xFF for i in range(w * h * 4))

    uvwq = _publish(tmp_path / "v2", "syn/water_dudv", FMT_UVWQ8888, width=4, height=4,
                    images=images)
    dxt3 = _publish(tmp_path / "v2", "syn/glass", FMT_DXT3, width=8, height=8, mips=2, inline=2)
    _material(tmp_path / "v2", "m/a", {"$basetexture": "syn/water_dudv", "$bumpmap": "syn/water_dudv"})
    result = importer.stage_textures(tmp_path / "v2", tmp_path / "stage")
    assert result.failures == [] and result.twins == 0

    from elysium_pipeline.formats.unit_contract.container import read_glb

    dds = parse_dds((tmp_path / "stage" / "syn" / "water_dudv.dds").read_bytes())
    _, binary = read_glb(uvwq)
    assert (dds.kind, dds.dxgi) == ("rgba8", 28)
    assert dds.images[0] == parse_ktx2(binary).levels
    sidecar = json.loads((tmp_path / "stage" / "syn" / "water_dudv.provenance.json").read_text())
    assert sidecar["stagedFormat"] == "rgba8"
    assert sidecar["vkFormat"] == 41 and sidecar["vkFormatName"] == "VK_FORMAT_R8G8B8A8_UINT"
    assert sidecar["sourceFormat"] == "UVWQ8888" and sidecar["expandedFromRgb8"] is False
    entry = _entries(tmp_path / "stage")["/ElysiumBaked/Textures/syn/T_water_dudv"]
    assert (entry["srgb"], entry["compression"], entry["role"]) == (False, "uncompressed", "data-normal")
    assert entry["roleConflict"] is False and entry["roleEvidence"] == ["$basetexture", "$bumpmap"]

    dds = parse_dds((tmp_path / "stage" / "syn" / "glass.dds").read_bytes())
    _, binary = read_glb(dxt3)
    assert (dds.kind, dds.dxgi, dds.mip_count) == ("bgra8", 87, 2)
    for level, blocks in enumerate(parse_ktx2(binary).levels):
        w = h = 8 >> level
        assert np.array_equal(dds.decode(0, level), decode_image("bc2", blocks, w, h))
    glass = _entries(tmp_path / "stage")["/ElysiumBaked/Textures/syn/T_glass"]
    assert glass["compression"] == "default" and glass["stagedFormat"] == "bgra8"
    # The BC2 decoder reads the explicit 4-bit alpha the encoder wrote.
    block = bytes([0xFF] * 8) + struct.pack("<HHI", 0xF800, 0x001F, 0)
    assert (decode_image("bc2", block, 4, 4) == [255, 0, 0, 255]).all()
    block = bytes([0x00] * 8) + struct.pack("<HHI", 0xF800, 0x001F, 0)
    assert (decode_image("bc2", block, 4, 4) == [255, 0, 0, 0]).all()


def test_a_unit_that_fails_to_stage_protects_its_assets_and_staged_files(tmp_path):
    unit = _publish(tmp_path / "v2", "syn/a")
    _publish(tmp_path / "v2", "syn/b")
    root = tmp_path / "stage"
    importer.stage_textures(tmp_path / "v2", root)
    (root / "syn" / "a.built.dds").write_bytes(b"built")
    unit.write_bytes(b"not a glb any more")

    result = importer.stage_textures(tmp_path / "v2", root)

    assert [key for key, _ in result.failures] == ["syn/a"]
    assert result.protected == 6
    manifest = _manifest(root)
    assert list(_entries(root)) == ["/ElysiumBaked/Textures/syn/T_b"]
    assert manifest["keep"] == sorted([
        "/ElysiumBaked/Textures/syn/T_a", "/ElysiumBaked/Textures/syn/T_a_linear",
        "/ElysiumBaked/Textures/syn/TC_a", "/ElysiumBaked/Textures/syn/TC_a_linear",
        "/ElysiumBaked/Textures/syn/TA_a", "/ElysiumBaked/Textures/syn/TA_a_linear",
    ])
    assert manifest["stageFailures"][0]["unit"] == "syn/a" and manifest["stageFailures"][0]["reason"]
    # Its staged products survive the prune, so the editor phase can still read them if asked.
    assert (root / "syn" / "a.dds").exists() and (root / "syn" / "a.provenance.json").exists()
    assert (root / "syn" / "a.built.dds").exists()
    assert result.pruned == 0

    # A collision is a failure too: both units' paths are protected, neither is named.
    _publish(tmp_path / "v2", "syn/c-d")
    _publish(tmp_path / "v2", "syn/c_d")
    unit.unlink()
    result = importer.stage_textures(tmp_path / "v2", root)
    manifest = _manifest(root)
    assert "/ElysiumBaked/Textures/syn/T_c_d" in manifest["keep"]
    assert "/ElysiumBaked/Textures/syn/T_c_d" not in _entries(root)
    assert sorted(key for key, _ in result.failures) == ["syn/c-d", "syn/c_d"]


def test_a_truncated_staged_dds_is_rewritten_not_trusted(tmp_path):
    _publish(tmp_path / "v2", "syn/a", FMT_DXT1, width=8, height=8, mips=2, inline=2)
    root = tmp_path / "stage"
    importer.stage_textures(tmp_path / "v2", root)
    dds_path = root / "syn" / "a.dds"
    whole = dds_path.read_bytes()
    sidecar = json.loads((root / "syn" / "a.provenance.json").read_text())
    import hashlib
    assert sidecar["ddsSha256"] == hashlib.sha256(whole).hexdigest()

    dds_path.write_bytes(whole[:-16])          # a run killed mid-write, sidecar already current
    result = importer.stage_textures(tmp_path / "v2", root)
    assert (result.staged, result.unchanged) == (1, 0)
    assert dds_path.read_bytes() == whole
    assert not list((root / "syn").glob("*.tmp"))

    result = importer.stage_textures(tmp_path / "v2", root)
    assert (result.staged, result.unchanged) == (0, 1)
