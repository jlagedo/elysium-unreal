"""Synthetic contract tests for the isolated Texture GLB exporter."""

from __future__ import annotations

import struct
import tempfile
from pathlib import Path
import unittest
import zlib

import pytest

from elysium_pipeline.exporters import texture_glb
from elysium_pipeline.formats.texture_glb import decode_texture
from elysium_pipeline.formats.texture_glb.decode import (
    ENVMAP,
    FORMATS,
    image_size,
    transform_cubemap_face,
)
from elysium_pipeline.formats.texture_glb.source import SourceMember, TextureSourceClosure
from elysium_pipeline.validation import texture_glb as validation


def _source(role, path, data):
    return SourceMember(role, path, data, {"kind": "synthetic"})


def _pair(fmt=13, *, width=4, height=4, mips=1, inline=1, cubemap=False, frames=1):
    info = FORMATS[fmt]
    faces = 7 if cubemap else 1
    levels = []
    for source_mip in range(mips):
        shift = mips - source_mip - 1
        w, h = max(1, width >> shift), max(1, height >> shift)
        size = image_size(w, h, info)
        images = []
        for frame in range(frames):
            for face in range(faces):
                images.append(bytes([(source_mip * 31 + frame * 11 + face + 1) & 0xFF]) * size)
        levels.append(b"".join(images))
    low = bytes([0x5A]) * 128
    vtf = bytearray(64)
    struct.pack_into("<4sIII", vtf, 0, b"VTF\0", 7, 1, 64)
    struct.pack_into("<HHIHH", vtf, 16, width, height, ENVMAP if cubemap else 0, frames, 0)
    struct.pack_into("<3f", vtf, 32, 0.1, 0.2, 0.3)
    struct.pack_into("<f", vtf, 48, 1.0)
    struct.pack_into("<IB", vtf, 52, fmt, mips)
    struct.pack_into("<IBB", vtf, 57, 13, 16, 16)
    inline_data = b"".join(levels[:inline])
    external = b"".join(levels[inline:])
    ttz = zlib.compress(external) if external else None
    table = bytearray()
    raw_offset = 64 + len(low)
    for index, level in enumerate(levels):
        table.extend(struct.pack("<II", raw_offset, 0))
        raw_offset += len(level)
    table.extend(struct.pack("<II", raw_offset, len(ttz or b"")))
    blob = bytes(vtf) + low + inline_data
    tth = struct.pack("<4sHBBI", b"TTH\0", 1, mips, inline, len(blob)) + bytes(table) + blob
    return tth, ttz


def _closure(fmt=13, **kwargs):
    tth, ttz = _pair(fmt, **kwargs)
    base = "materials/synthetic/texture"
    return TextureSourceClosure(
        "synthetic/texture",
        "vtmb:texture:synthetic/texture",
        _source("tth", base + ".tth", tth),
        _source("ttz", base + ".ttz", ttz) if ttz is not None else None,
    )


def _with_external(closure, raw_external):
    """Replace a closure's external payload, keeping the TTH's declared TTZ length honest."""
    ttz = zlib.compress(raw_external)
    tth = bytearray(closure.tth.data)
    struct.pack_into("<I", tth, 12 + tth[6] * 8 + 4, len(ttz))
    return TextureSourceClosure(
        closure.texture_path,
        closure.asset_id,
        _source("tth", closure.tth.path, bytes(tth)),
        _source("ttz", closure.ttz.path, ttz),
    )


def _omission(model_or_extension, role):
    rows = getattr(model_or_extension, "omissions", None)
    if rows is None:
        rows = model_or_extension["omissions"]
    return next((row for row in rows if row["role"] == role), None)


@pytest.mark.parametrize("fmt", FORMATS)
def test_every_admitted_format_builds_one_complete_model(fmt):
    model = decode_texture(_closure(fmt))
    assert len(model.levels) == 1
    assert model.byte_coverage[0]["coveragePercent"] == 100.0
    assert model.omissions[0]["role"] == "low-res-cpu-sample"
    assert "sourceSrgb" not in model.sampling
    assert "normal" not in model.sampling
    assert "pointSample" in model.sampling


def test_inline_and_external_mips_reconstruct_largest_first():
    model = decode_texture(_closure(15, width=8, height=8, mips=3, inline=1))
    assert [(level.width, level.height) for level in model.levels] == [(8, 8), (4, 4), (2, 2)]
    assert len(model.byte_coverage) == 2


def test_cubemap_keeps_six_faces_and_ledgers_the_low_end_spheremap():
    model = decode_texture(_closure(13, cubemap=True))
    assert len(model.levels[0].images) == 6
    assert [face["sourceFace"] for face in model.faces] == [0, 1, 4, 5, 3, 2]
    assert model.omissions[-1]["role"] == "low-end-spheremap"
    states = model.byte_coverage[0]["stateBytes"]
    assert states["omitted-proven"] > 128


def test_incomplete_external_base_falls_back_to_the_complete_inline_chain():
    closure = _closure(15, width=8, height=8, mips=4, inline=3)
    partial = zlib.compress(bytes(range(32)))
    changed_tth = bytearray(closure.tth.data)
    struct.pack_into("<I", changed_tth, 12 + 4 * 8 + 4, len(partial))
    changed = TextureSourceClosure(
        closure.texture_path,
        closure.asset_id,
        _source("tth", closure.tth.path, bytes(changed_tth)),
        _source("ttz", closure.ttz.path, partial),
    )
    model = decode_texture(changed)
    assert (model.width, model.height, model.mip_count) == (4, 4, 3)
    assert model.omissions[-1]["role"] == "incomplete-lower-mips"


def test_a_stale_inline_mip_count_is_restated_as_the_source_wrote_it():
    """`declaredInlineMips` is the header's claim; `resolvedInlineMips` is what fits.

    No shipped unit carries a stale count, so only a synthetic one exercises the split. The
    clamped value drives the extent arithmetic, but publishing it would erase the evidence
    that the source over-declared.
    """
    closure = _closure(13, width=4, height=4, mips=1, inline=1)
    stale = bytearray(closure.tth.data)
    stale[7] = 9                                  # the TTH's declared inline mip count
    changed = TextureSourceClosure(
        closure.texture_path,
        closure.asset_id,
        _source("tth", closure.tth.path, bytes(stale)),
        None,
    )
    model = decode_texture(changed)
    assert model.header["declaredInlineMips"] == 9
    assert model.header["resolvedInlineMips"] == 1
    assert model.byte_coverage[0]["coveragePercent"] == 100.0


def test_declared_lengths_exclude_arbitrary_compiler_allocation_tails():
    closure = _closure(13, width=8, height=8, mips=2, inline=1)
    changed = TextureSourceClosure(
        closure.texture_path,
        closure.asset_id,
        _source("tth", closure.tth.path, closure.tth.data + b"\x13\x37"),
        _source("ttz", closure.ttz.path, closure.ttz.data + b"\x99\x42"),
    )
    model = decode_texture(changed)
    assert model.byte_coverage[0]["stateBytes"]["omitted-proven"] == 130
    assert model.byte_coverage[1]["stateBytes"]["omitted-proven"] == 2


def test_a_repeated_full_resolution_stream_admits_one_level():
    """A stream the declared chain cannot explain must not be sliced into levels.

    Retail units of this shape store the full-resolution image twice and no pyramid. Sliding
    the chain onto the tail would compose every level below the first from unrelated bytes.
    """
    closure = _closure(15, width=16, height=16, mips=3, inline=1)
    full = bytes([0x7E]) * image_size(16, 16, FORMATS[15])
    changed = _with_external(closure, bytes([0x11]) * len(full) + full)
    model = decode_texture(changed)
    assert model.mip_count == 1
    assert (model.width, model.height) == (16, 16)
    assert model.levels[0].images[0] == full
    row = _omission(model, "unexplained-leading-image-storage")
    assert row is not None
    assert row["byteLength"] == len(full)
    assert row["streamFullResolutionImages"] == 2.0
    assert row["admittedFullResolutionImageOnly"]


def test_a_blob_larger_than_the_declared_colour_sample_is_not_the_colour_sample():
    closure = _closure(15, width=16, height=16, mips=3, inline=1)
    full = bytes([0x7E]) * image_size(16, 16, FORMATS[15])
    model = decode_texture(_with_external(closure, bytes(len(full)) + full))
    assert _omission(model, "low-res-cpu-sample")["externalByteLength"] == 0


def test_a_small_leading_blob_stays_the_colour_sample():
    model = decode_texture(_closure(15, width=8, height=8, mips=3, inline=1))
    assert model.mip_count == 3
    assert _omission(model, "unexplained-leading-image-storage") is None


def test_a_lost_primary_image_is_recorded_rather_than_published_quietly():
    closure = _closure(15, width=8, height=8, mips=4, inline=3)
    model = decode_texture(_with_external(closure, bytes(range(32))))
    assert (model.width, model.height) == (4, 4)
    assert (model.declared_width, model.declared_height) == (8, 8)
    row = _omission(model, "primary-image-not-recoverable")
    assert row is not None
    assert (row["declaredWidth"], row["declaredHeight"]) == (8, 8)
    assert (row["recoveredWidth"], row["recoveredHeight"]) == (4, 4)


def test_cubemap_rotation_preserves_uncompressed_texels():
    source = bytes((0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3))
    rotated = transform_cubemap_face(source, 2, 2, FORMATS[3], "rotate-cw")
    assert rotated == bytes((2, 2, 2, 0, 0, 0, 3, 3, 3, 1, 1, 1))


def test_cubemap_bc_selector_rotation_is_lossless():
    source = bytes.fromhex("00112233e4e4e4e4")
    clockwise = transform_cubemap_face(source, 4, 4, FORMATS[13], "rotate-cw")
    restored = transform_cubemap_face(clockwise, 4, 4, FORMATS[13], "rotate-ccw")
    assert restored == source


def test_complete_texture_writes_one_ktx2_payload_and_validates():
    model = decode_texture(_closure(15, width=8, height=8, mips=3, inline=1))
    document, binary = texture_glb.build_document(model)
    assert "images" not in document
    assert "textures" not in document
    assert len(document["bufferViews"]) == 1
    summary = validation.validate_document(document, binary)
    assert summary["mips"] == 3
    assert summary["sourceBytes"] == summary["accountedBytes"]


def test_cubemap_ktx2_is_six_faced():
    model = decode_texture(_closure(13, cubemap=True))
    document, binary = texture_glb.build_document(model)
    summary = validation.validate_document(document, binary)
    assert summary["faces"] == 6


def test_validator_refuses_payload_corruption():
    model = decode_texture(_closure(13))
    document, binary = texture_glb.build_document(model)
    changed = bytearray(binary)
    changed[-1] ^= 1
    with pytest.raises(validation.TextureGlbValidationError, match="identity"):
        validation.validate_document(document, bytes(changed))


def test_validation_reports_a_degraded_unit_and_refuses_to_lose_the_record():
    closure = _closure(15, width=8, height=8, mips=4, inline=3)
    model = decode_texture(_with_external(closure, bytes(range(32))))
    document, binary = texture_glb.build_document(model)
    summary = validation.validate_document(document, binary)
    assert summary["degraded"]
    assert (summary["declaredWidth"], summary["declaredHeight"]) == (8, 8)
    extension = document["extensions"][validation.TEXTURE_EXTENSION]
    extension["omissions"] = [
        row for row in extension["omissions"]
        if row["role"] != "primary-image-not-recoverable"
    ]
    with pytest.raises(validation.TextureGlbValidationError, match="must be recorded"):
        validation.validate_document(document, binary)


def test_validation_refuses_levels_sliced_from_an_unexplained_stream():
    model = decode_texture(_closure(15, width=8, height=8, mips=3, inline=1))
    document, binary = texture_glb.build_document(model)
    extension = document["extensions"][validation.TEXTURE_EXTENSION]
    extension["omissions"].append({
        "role": "unexplained-leading-image-storage",
        "byteLength": 64,
        "admittedFullResolutionImageOnly": True,
    })
    with pytest.raises(validation.TextureGlbValidationError, match="one level only"):
        validation.validate_document(document, binary)


def test_validation_refuses_an_oversized_colour_sample_claim():
    model = decode_texture(_closure(15, width=8, height=8, mips=3, inline=1))
    document, binary = texture_glb.build_document(model)
    row = _omission(document["extensions"][validation.TEXTURE_EXTENSION], "low-res-cpu-sample")
    row["externalByteLength"] = row["declaredByteLength"] + 4096
    with pytest.raises(validation.TextureGlbValidationError, match="image storage"):
        validation.validate_document(document, binary)


def test_public_exporter_publishes_expected_relative_path():
    closure = _closure(13)
    files = {member.path: member.data for member in closure.members()}
    index = {path: ("loose", Path("synthetic") / path) for path in files}
    with tempfile.TemporaryDirectory() as temporary:
        destination = texture_glb.export(
            index,
            "materials/synthetic/texture.ttz",
            Path(temporary),
            read_bytes=lambda _index, path: files.get(path),
        )
        summary = validation.validate(destination)
        relative = destination.relative_to(temporary).as_posix()
    assert relative == "synthetic/texture.glb"
    assert summary["asset"] == "vtmb:texture:synthetic/texture"


def test_prepublication_validation_rechecks_source_hashes():
    closure = _closure(13)
    model = decode_texture(closure)
    document, binary = texture_glb.build_document(model)
    changed = _source("tth", closure.tth.path, closure.tth.data + b"x")
    with pytest.raises(validation.TextureGlbValidationError, match="source bytes disagree"):
        validation.validate_document(document, binary, source_members=(changed,))


def test_prepublication_validation_compares_decoded_pixels():
    closure = _closure(15, width=8, height=8, mips=3, inline=1)
    model = decode_texture(closure)
    document, binary = texture_glb.build_document(model)
    summary = validation.validate_document(
        document, binary, source_members=closure.members()
    )
    assert summary["mips"] == 3
    assert summary["sourceBytes"] == summary["accountedBytes"]
