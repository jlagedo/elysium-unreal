"""GLB-only D1 sky conservation; no Unreal, PNGs, or installed source fixtures."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

import numpy as np
import pytest

from elysium_pipeline.importers import sky_composites as sky
from elysium_pipeline.importers import textures
from elysium_pipeline.importers.texture_cube import rasterize_cube
from elysium_pipeline.importers.texture_dds import parse_dds
from test_texture_corpus_import import _publish, FMT_RGBA8888, FMT_DXT5


def _sources(root, name="hav", *, mips=3, size=8, fmt=FMT_RGBA8888):
    def shade(d):
        rgb = (d + 1) / 2
        return np.concatenate([rgb, np.full((*d.shape[:-1], 1), 0.37)], axis=-1)

    paths = {}
    # Retail sky texcoords after Source's Y reflection (K1), independently of the
    # projector's image rotations. A smooth direction field exercises ALL pixels.
    for face in ("rt", "lf", "bk", "ft", "up", "dn"):
        def pixels(mip, frame, index, w, h, suffix=face):
            v, u = np.meshgrid((np.arange(w) + .5)/w, (np.arange(w) + .5)/w, indexing="ij")
            s, t, one = 2*u-1, 1-2*v, np.ones_like(u)
            x, y, z = {"rt": (one, s, t), "lf": (-one, -s, t),
                       "bk": (s, -one, t), "ft": (-s, one, t),
                       "up": (-t, s, one), "dn": (t, s, -one)}[suffix]
            d = np.stack((x, y, z), axis=-1)
            d /= np.linalg.norm(d, axis=-1, keepdims=True)
            return np.rint(shade(d)*255).astype(np.uint8).tobytes()
        paths[face] = _publish(root, f"skybox/{name}{face}", fmt, width=size, height=size,
                               mips=mips, images=pixels if fmt == FMT_RGBA8888 else None)
    return {face: path.read_bytes() for face, path in paths.items()}, shade


def test_all_faces_all_mips_orientation_alpha_and_provenance(tmp_path):
    sources, shade = _sources(tmp_path)
    plan = sky.plan_sky("hav", sources)
    dds = parse_dds(plan.dds_bytes)
    assert (dds.width, dds.height, dds.slices, dds.mip_count, dds.is_cube) == (8, 8, 6, 3, True)
    for level in range(3):
        expected = rasterize_cube(8 >> level, shade)
        for face in range(6):
            np.testing.assert_array_equal(dds.decode(face, level), sky.linear_rgba(expected[face]))
    provenance = next(iter(plan.sidecars.values()))
    assert len(provenance["sourceUnits"]) == 6
    for row in provenance["sourceUnits"]:
        assert row["unitSha256"] == hashlib.sha256(sources[row["assetId"][-2:]]).hexdigest()
        assert row["texture"]["sourceResolution"]["members"]
        assert row["texture"]["coverage"]["byteLedger"]
    for mip in range(3):
        assert provenance["sourceMipMd5"][mip] == hashlib.md5(
            b"".join(dds.images[face][mip] for face in range(6))).hexdigest()
    assert plan.entries[0]["compression"] == "hdr-f32"
    assert plan.entries[0]["mipGen"] == "leave-existing"


def test_mean_matches_previous_scalar_formula_and_ignores_ground_alpha():
    faces = [np.full((4, 4, 4), 128, dtype=np.uint8) for _ in range(6)]
    expected = (128 / 255)**2.2
    assert sky.upper_hemisphere_mean(faces) == pytest.approx(expected, abs=1e-12)
    faces[5][...] = 255
    for face in faces:
        face[..., 3] = 0
    assert sky.upper_hemisphere_mean(faces) == pytest.approx(expected, abs=1e-12)
    for face in faces[:5]:
        face[..., :3] = 0
    faces[4][..., :3] = 255
    # A cube face subtends one third of the upper hemisphere on an even grid.
    assert sky.upper_hemisphere_mean(faces) == pytest.approx(1 / 3, abs=1e-12)


def test_linear_transfer_is_reversible_for_every_byte_and_hdr_is_unclipped():
    pixels = np.repeat(np.arange(256, dtype=np.uint8)[:, None], 4, axis=1).reshape(16, 16, 4)
    linear = sky.linear_rgba(pixels).astype(np.float64)
    srgb = np.where(linear[..., :3] <= 0.0031308, linear[..., :3] * 12.92,
                    1.055 * linear[..., :3]**(1/2.4) - 0.055)
    np.testing.assert_array_equal(np.rint(srgb * 255).astype(np.uint8), pixels[..., :3])
    np.testing.assert_array_equal(np.rint(linear[..., 3] * 255).astype(np.uint8), pixels[..., 3])
    hdr = np.full((2, 2, 4), 64.5, dtype=np.float32)
    np.testing.assert_array_equal(sky.linear_rgba(hdr), hdr)
    assert sky.upper_hemisphere_mean([hdr] * 6) == pytest.approx(64.5)


def test_native_hdr_glb_faces_and_float_dds_keep_every_sample(tmp_path):
    import struct
    from elysium_pipeline.formats.texture_glb.ktx2 import IDENTIFIER
    from elysium_pipeline.formats.texture_glb.model import TEXTURE_EXTENSION
    from elysium_pipeline.formats.unit_contract.container import encode_glb

    sources = {}
    for suffix, _ in sky.SLICES:
        header = bytearray(80 + 2 * 24)
        header[:12] = IDENTIFIER
        struct.pack_into("<9I", header, 12, 109, 4, 2, 2, 0, 0, 1, 2, 0)
        levels = [np.full((size, size, 4), 32.25 + level, dtype="<f4").tobytes()
                  for level, size in enumerate((2, 1))]
        offset = len(header)
        for level, data in enumerate(levels):
            struct.pack_into("<3Q", header, 80 + level * 24, offset, len(data), len(data))
            offset += len(data)
        binary = bytes(header) + b"".join(levels)
        doc = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}],
               "extensions": {TEXTURE_EXTENSION: {"identity": {"asset": f"vtmb:texture:skybox/hav{suffix}"}}}}
        sources[suffix] = encode_glb(doc, binary)
    plan = sky.plan_sky("hav", sources)
    dds = parse_dds(plan.dds_bytes)
    assert (dds.kind, dds.dxgi, dds.slices, dds.mip_count) == ("rgba32f", 2, 6, 2)
    for face in range(6):
        for mip in range(2):
            assert (dds.decode(face, mip) == 32.25 + mip).all()


def test_true_cube_hav_and_composite_have_distinct_addresses_and_storage(tmp_path):
    export, stage = tmp_path / "export", tmp_path / "stage"
    _sources(export)
    _publish(export, "skybox/hav", FMT_RGBA8888, cubemap=True)
    result = textures.stage_textures(export, stage, select="skybox", bindings={})
    assert not result.failures
    manifest = json.loads(result.manifest_path.read_text())
    assert len(manifest["assets"]) == 8
    paths = {entry["assetPath"] for entry in manifest["assets"]}
    assert sky.sky_cube_path("hav") in paths
    assert "/ElysiumBaked/Textures/skybox/TC_hav" in paths
    assert (stage / "skybox/hav.dds").is_file()
    assert (stage / "skybox/_composites/hav.dds").is_file()
    assert sky.sky_material_path("hav") == "/ElysiumBaked/Materials/skybox/MI_hav_Sky"


def test_reuse_hash_change_and_missing_face_protection(tmp_path):
    export, stage = tmp_path / "export", tmp_path / "stage"
    _sources(export)
    first = textures.stage_textures(export, stage, bindings={})
    assert first.staged == 7
    second = textures.stage_textures(export, stage, bindings={})
    assert (second.staged, second.unchanged) == (0, 7)
    target = stage / "skybox/_composites/hav.dds"
    original = target.read_bytes()
    target.write_bytes(b"corrupt")
    third = textures.stage_textures(export, stage, bindings={})
    assert (third.staged, third.unchanged) == (1, 6)
    assert target.read_bytes() == original
    (export / "textures/skybox/havup.glb").unlink()
    failed = textures.stage_textures(export, stage, bindings={})
    assert len(failed.failures) == 1
    assert target.read_bytes() == original
    manifest = json.loads(failed.manifest_path.read_text())
    assert sky.sky_cube_path("hav") in manifest["keep"]


def test_unrelated_selection_leaves_sky_files_untouched(tmp_path):
    export, stage = tmp_path / "export", tmp_path / "stage"
    _sources(export)
    _publish(export, "hud/a")
    textures.stage_textures(export, stage, bindings={})
    before = {p: p.read_bytes() for p in (stage / "skybox").rglob("*") if p.is_file()}
    result = textures.stage_textures(export, stage, select="hud", bindings={})
    assert result.assets == 1
    assert before == {p: p.read_bytes() for p in before}


def test_actual_cube_unit_collision_fails_both_before_writing(tmp_path):
    export, stage = tmp_path / "export", tmp_path / "stage"
    _sources(export)
    _publish(export, "skybox/hav_sky", FMT_RGBA8888, cubemap=True)
    result = textures.stage_textures(export, stage, bindings={})
    assert len(result.failures) == 2
    assert all("collides" in reason for _, reason in result.failures)
    assert not (stage / "skybox/_composites/hav.dds").exists()
    assert not (stage / "skybox/hav_sky.dds").exists()


def test_unreadable_cube_unit_still_protects_its_address_from_composite(tmp_path):
    export, stage = tmp_path / "export", tmp_path / "stage"
    _sources(export)
    unit = _publish(export, "skybox/hav_sky", FMT_RGBA8888, cubemap=True)
    unit.write_bytes(b"bad GLB")
    result = textures.stage_textures(export, stage, bindings={})
    assert len(result.failures) == 2
    assert not (stage / "skybox/_composites/hav.dds").exists()
    assert sky.sky_cube_path("hav") in json.loads(result.manifest_path.read_text())["keep"]


def test_float_measure_does_not_truncate_small_radiance_deltas(tmp_path):
    import struct
    sources, _ = _sources(tmp_path / "export")
    plan = sky.plan_sky("hav", sources)
    root = tmp_path / "stage"
    target = root / plan.dds_relative
    target.parent.mkdir(parents=True)
    target.write_bytes(plan.dds_bytes)
    built = bytearray(plan.dds_bytes)
    value = struct.unpack_from("<f", built, 148)[0]
    struct.pack_into("<f", built, 148, value + 0.001)
    target.with_suffix(".built.dds").write_bytes(built)
    (root / "manifest.json").write_text(json.dumps({"assets": plan.entries}))
    measured = textures.measure_textures(root)
    assert measured.measured == 1
    assert measured.max_of_max == pytest.approx(0.001, abs=1e-7)


@pytest.mark.parametrize("kwargs", [{"width": 4, "height": 4, "mips": 2},
                                   {"width": 8, "height": 8, "mips": 2},
                                   {"width": 8, "height": 4, "mips": 3},
                                   {"width": 8, "height": 8, "frames": 2, "mips": 3}])
def test_incompatible_face_cannot_drop_data(tmp_path, kwargs):
    sources, _ = _sources(tmp_path)
    sources["up"] = _publish(tmp_path, "skybox/havup", FMT_RGBA8888, **kwargs).read_bytes()
    with pytest.raises(sky.SkyCompositeError):
        sky.plan_sky("hav", sources)


def test_mixed_bc_and_rgb_units_keep_their_source_evidence(tmp_path):
    sources, _ = _sources(tmp_path, fmt=FMT_DXT5)
    sources["up"] = _publish(tmp_path, "skybox/havup", FMT_RGBA8888,
                              width=8, height=8, mips=3).read_bytes()
    plan = sky.plan_sky("hav", sources)
    dds = parse_dds(plan.dds_bytes)
    assert dds.mip_count == 3
    assert len(next(iter(plan.sidecars.values()))["sourceUnits"]) == 6


def test_a_face_change_invalidates_composite_recipe(tmp_path):
    sources, _ = _sources(tmp_path)
    first = sky.plan_sky("hav", sources)
    prior = next(iter(first.sidecars.values()))
    sources["dn"] = _publish(tmp_path, "skybox/havdn", FMT_RGBA8888,
                              width=8, height=8, mips=3).read_bytes()
    changed = sky.plan_sky("hav", sources, existing_sidecar=prior,
                           existing_dds_sha256=prior["ddsSha256"])
    assert changed.dds_bytes is not None
    assert changed.entries[0]["recipe"] != first.entries[0]["recipe"]


def test_reject_wrong_face_identity(tmp_path):
    sources, _ = _sources(tmp_path)
    sources["up"] = sources["dn"]
    with pytest.raises(sky.SkyCompositeError, match="identity"):
        sky.plan_sky("hav", sources)


@pytest.mark.parametrize("name", ["../hav", "/hav", "hav/up", "", "a.b", "a:b"])
def test_path_escape_refused(name):
    with pytest.raises(sky.SkyCompositeError):
        sky.sky_cube_path(name)
