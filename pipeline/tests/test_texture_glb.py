"""Synthetic contract tests for the isolated Texture GLB exporter."""

from __future__ import annotations

import struct
import tempfile
from pathlib import Path
import zlib

import pytest

from elysium_pipeline.exporters import texture_glb
from elysium_pipeline.formats.texture_glb import decode_texture, source as texture_source
from elysium_pipeline.formats.texture_glb.decode import (
    ENVMAP,
    FORMATS,
    TextureDecodeError,
    image_size,
    transform_cubemap_face,
)
from elysium_pipeline.formats.texture_glb.source import SourceMember, TextureSourceClosure
from elysium_pipeline.validation import texture_glb as validation

_BSP_HEADER_BYTES = 8 + 64 * 16 + 4


def _zip_member(name: str, payload: bytes) -> tuple[bytes, bytes, int]:
    """One stored ZIP member: its local record, its encoded name, and its length."""

    raw = name.encode("latin-1")
    local = struct.pack(
        "<IHHHHHIIIHH", 0x04034B50, 20, 0, 0, 0, 0, zlib.crc32(payload), len(payload),
        len(payload), len(raw), 0,
    ) + raw + payload
    return local, raw, len(payload)


def _pakfile_zip(members: dict[str, bytes]) -> bytes:
    """A minimal stored-only ZIP, the same layout the compiler writes into BSP lump 40."""

    body = bytearray()
    central = bytearray()
    for name, payload in members.items():
        offset = len(body)
        local, raw, size = _zip_member(name, payload)
        body.extend(local)
        central.extend(
            struct.pack(
                "<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, 0, 0, 0, 0, zlib.crc32(payload),
                size, size, len(raw), 0, 0, 0, 0, 0, offset,
            )
            + raw
        )
    central_offset = len(body)
    body.extend(central)
    body.extend(
        struct.pack(
            "<IHHHHIIH", 0x06054B50, 0, 0, len(members), len(members), len(central),
            central_offset, 0,
        )
    )
    return bytes(body)


def _bsp_with_pakfile(members: dict[str, bytes]) -> bytes:
    """A synthetic BSP whose only populated lump is PAKFILE (lump 40)."""

    payload = _pakfile_zip(members)
    header = bytearray(struct.pack("<4si", b"VBSP", 17))
    for index in range(64):
        if index == 40:
            header.extend(struct.pack("<iii4s", _BSP_HEADER_BYTES, len(payload), 0, b"\0\0\0\0"))
        else:
            header.extend(struct.pack("<iii4s", 0, 0, 0, b"\0\0\0\0"))
    header.extend(struct.pack("<i", 7))
    assert len(header) == _BSP_HEADER_BYTES
    return bytes(header) + payload


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


def _closure_with_superfluous_ttz(raw_ttz: bytes):
    """A texture whose declared inline chain is complete on its own, plus a superfluous `.ttz`.

    `_pair` leaves `closure.ttz` `None` once every mip is inline, so this builds the TTH the same
    way and then attaches a `.ttz` by hand -- the shape SF-1.2 found in 2 of the shipped
    reflection probes: a pyramid that fits wholly inline yet still ships a TTZ neither the mip
    table nor the image walk needs.
    """

    tth, _ttz = _pair(15, width=16, height=16, mips=3, inline=3)
    base = "materials/synthetic/texture"
    return TextureSourceClosure(
        "synthetic/texture",
        "vtmb:texture:synthetic/texture",
        _source("tth", base + ".tth", tth),
        _source("ttz", base + ".ttz", raw_ttz),
    )


def test_a_declared_inline_chain_superfluous_ttz_exports_and_validates():
    """Finding 1: a TTH whose inline chain is already complete plus a superfluous `.ttz` must
    export a unit that validates -- not one `admittedFullResolutionImageOnly` mislabels as
    single-level when it actually admits the whole declared chain."""
    raw_external = bytes([0xAB]) * 300
    closure = _closure_with_superfluous_ttz(zlib.compress(raw_external))
    model = decode_texture(closure)
    assert model.mip_count == 3
    row = _omission(model, "unexplained-leading-image-storage")
    assert row is not None
    assert row["admittedDeclaredInlineChain"] is True
    assert row["admittedLevels"] == 3
    assert row["sourceMips"] == [0, 1, 2]
    assert not row["admittedFullResolutionImageOnly"]
    # The `.ttz` span contributed nothing to the emitted image: it is proven-redundant storage,
    # not `derived` content.
    ttz_ledger = model.byte_coverage[1]
    assert ttz_ledger["stateBytes"].get("derived", 0) == 0
    assert ttz_ledger["stateBytes"]["omitted-proven"] == ttz_ledger["byteLength"]

    from elysium_pipeline.exporters import texture_glb as texture_exporter

    document, binary = texture_exporter.build_document(model)
    summary = validation.validate_document(document, binary, source_members=closure.members())
    assert summary["mips"] == 3


def test_a_truncated_superfluous_ttz_is_not_admitted_silently():
    """Finding 3: a truncated `.ttz` behind an already-complete inline chain must raise, not
    publish a unit that silently treats the corrupt stream as proven-redundant."""
    raw_external = bytes([0xAB]) * 300
    complete = zlib.compress(raw_external)
    # Drop only the trailer, the same "corrupt tail" shape `_inflate_with_recovery` is written to
    # tolerate: almost every byte still decodes, but the stream never reaches `eof`.
    closure = _closure_with_superfluous_ttz(complete[:-2])
    with pytest.raises(TextureDecodeError, match="truncated"):
        decode_texture(closure)


def test_a_blob_larger_than_the_declared_colour_sample_is_not_the_colour_sample():
    closure = _closure(15, width=16, height=16, mips=3, inline=1)
    full = bytes([0x7E]) * image_size(16, 16, FORMATS[15])
    model = decode_texture(_with_external(closure, bytes(len(full)) + full))
    assert _omission(model, "low-res-cpu-sample")["externalByteLength"] == 0


def _probe_pair(fmt=3, *, width=32, height=32, mips=6, low=b"", declared_inline=3):
    """A compiler-written reflection probe: the outer TTH mip table declares ONE level while the
    VTF header inside it declares the whole chain and the blob carries every level inline.
    `maps/sm_pier_1/cubemapdefault`'s exact shape (`U1b_textures.md` section 1: `tthMipTableCount`
    1 against `vtfMipCount` 6). `low` adds a declared low-resolution image ahead of the images."""

    info = FORMATS[fmt]
    levels = []
    for source_mip in range(mips):
        shift = mips - source_mip - 1
        w, h = max(1, width >> shift), max(1, height >> shift)
        size = image_size(w, h, info)
        levels.append(b"".join(
            bytes([(source_mip * 31 + face + 1) & 0xFF]) * size for face in range(7)
        ))
    vtf = bytearray(64)
    struct.pack_into("<4sIII", vtf, 0, b"VTF\0", 7, 1, 64)
    struct.pack_into("<HHIHH", vtf, 16, width, height, ENVMAP, 1, 0)
    struct.pack_into("<3f", vtf, 32, 0.1, 0.2, 0.3)
    struct.pack_into("<f", vtf, 48, 1.0)
    struct.pack_into("<IB", vtf, 52, fmt, mips)
    if low:
        struct.pack_into("<IBB", vtf, 57, 13, 16, 16)
    else:
        struct.pack_into("<IBB", vtf, 57, 0, 0, 0)
    blob = bytes(vtf) + low + b"".join(levels)
    table = struct.pack("<II", 64, 0) + struct.pack("<II", 64 + len(blob), 0)
    return struct.pack("<4sHBBI", b"TTH\0", 1, 1, declared_inline, len(blob)) + table + blob


def _probe_closure(**kwargs):
    base = "materials/maps/synthetic/cubemapdefault"
    return TextureSourceClosure(
        "maps/synthetic/cubemapdefault",
        "vtmb:texture:maps/synthetic/cubemapdefault",
        _source("tth", base + ".tth", _probe_pair(**kwargs)),
        None,
    )


def test_a_probe_whose_mip_table_under_declares_keeps_the_chain_the_vtf_header_names():
    """G5b: the outer TTH table says one level, the VTF header says six and the blob is exactly
    those six. Read against the table alone the five lower levels fall ahead of the image start
    and are claimed as the header's CPU colour sample, so the probe imports flat -- both
    `sm_pier_1` probes did (`PHASE0_VERDICT.md` A5)."""

    model = decode_texture(_probe_closure())
    assert model.mip_count == 6
    assert [(level.width, level.height) for level in model.levels] == [
        (32, 32), (16, 16), (8, 8), (4, 4), (2, 2), (1, 1)]
    assert model.header["tthMipTableCount"] == 1 and model.header["vtfMipCount"] == 6
    assert model.header["sourceMipCount"] == 6 and model.header["resolvedInlineMips"] == 6
    assert _omission(model, "low-res-cpu-sample") is None
    assert model.byte_coverage[0]["coveragePercent"] == 100.0


def test_the_under_declared_chain_is_admitted_only_when_the_blob_is_exactly_that_chain():
    """The gate is arithmetic, not a guess: a probe that really does carry a colour sample ahead
    of its images no longer matches the pyramid's byte total, so it keeps the old reading."""

    model = decode_texture(_probe_closure(low=bytes([0x5A]) * 128))
    assert model.mip_count == 1 and model.header["sourceMipCount"] == 1
    assert _omission(model, "low-res-cpu-sample")["declaredByteLength"] == 128


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


# --------------------------------------------------------------------------------------------
# SF-1.3: PAKFILE reflection probes as texture units.
# --------------------------------------------------------------------------------------------


def _pakfile_probe_install():
    """A synthetic install: one loose texture and one map BSP with two PAKFILE probes.

    `c0_0_0` carries a `.ttz` twin (mips=2, inline=1 spills the second mip external); the
    `cubemapdefault` probe is `.tth`-only (mips=1, inline=1 keeps the whole pyramid inline), the
    same case SF-1.2 found for 575 of the 1,325 shipped probes.
    """

    tth_with_ttz, ttz = _pair(13, width=4, height=4, mips=2, inline=1)
    tth_only, none_ttz = _pair(13, width=4, height=4, mips=1, inline=1)
    assert none_ttz is None
    bsp = _bsp_with_pakfile(
        {
            "materials/maps/testmap/c0_0_0.tth": tth_with_ttz,
            "materials/maps/testmap/c0_0_0.ttz": ttz,
            "materials/maps/testmap/cubemapdefault.tth": tth_only,
        }
    )
    index = {
        "maps/testmap.bsp": ("loose", "synthetic/maps/testmap.bsp"),
        "materials/synthetic/texture.tth": ("loose", "synthetic/texture.tth"),
    }
    files = {
        "maps/testmap.bsp": bsp,
        "materials/synthetic/texture.tth": b"install-tth-bytes",
    }
    return index, files


def test_source_keys_lists_install_textures_and_pakfile_probes():
    index, files = _pakfile_probe_install()
    keys = texture_source.source_keys(index, read_bytes=lambda _index, key: files.get(key))
    assert "synthetic/texture" in keys
    assert "maps/testmap/c0_0_0" in keys
    assert "maps/testmap/cubemapdefault" in keys
    assert keys == sorted(keys)


def test_pakfile_probe_with_ttz_round_trips_with_bsp_pakfile_origin(tmp_path):
    index, files = _pakfile_probe_install()
    destination = texture_glb.export(
        index, "maps/testmap/c0_0_0", tmp_path, read_bytes=lambda _index, key: files.get(key)
    )
    summary = validation.validate(destination)
    assert summary["asset"] == "vtmb:texture:maps/testmap/c0_0_0"
    document, _binary = validation.read_glb(destination)
    extension = document["extensions"]["ELYSIUM_vtmb_texture"]
    members = {member["role"]: member for member in extension["sourceResolution"]["members"]}
    assert set(members) == {"tth", "ttz"}
    for member in members.values():
        origin = member["origin"]
        assert origin["kind"] == "bsp-pakfile"
        assert origin["map"] == "testmap"
        assert origin["member"].startswith("materials/maps/testmap/c0_0_0.")
        assert origin["origin"] == {"kind": "loose", "root": "loose"}


def test_pakfile_probe_without_ttz_round_trips_and_validates(tmp_path):
    index, files = _pakfile_probe_install()
    destination = texture_glb.export(
        index,
        "maps/testmap/cubemapdefault",
        tmp_path,
        read_bytes=lambda _index, key: files.get(key),
    )
    summary = validation.validate(destination)
    assert summary["asset"] == "vtmb:texture:maps/testmap/cubemapdefault"
    document, _binary = validation.read_glb(destination)
    extension = document["extensions"]["ELYSIUM_vtmb_texture"]
    members = extension["sourceResolution"]["members"]
    assert {member["role"] for member in members} == {"tth"}
    assert members[0]["origin"]["kind"] == "bsp-pakfile"


def test_pakfile_members_are_parsed_once_per_index():
    """`pakfile_members` memoises per install index identity, so repeated closures over the same
    index do not re-open and re-unzip the BSP."""

    from elysium_pipeline.formats.map_glb import pakfile_index

    index, files = _pakfile_probe_install()
    read_bytes = lambda _index, key: files.get(key)
    reads = []

    def counting_read(idx, key):
        if key == "maps/testmap.bsp":
            reads.append(key)
        return read_bytes(idx, key)

    first = pakfile_index.pakfile_members(index, read_bytes=counting_read)
    second = pakfile_index.pakfile_members(index, read_bytes=counting_read)
    assert first is second
    assert reads == ["maps/testmap.bsp"]


def test_pakfile_members_returns_an_immutable_mapping():
    """Finding 2: a caller must not be able to mutate the shared per-process memo."""

    from types import MappingProxyType

    from elysium_pipeline.formats.map_glb import pakfile_index

    index, files = _pakfile_probe_install()
    members = pakfile_index.pakfile_members(index, read_bytes=lambda _i, k: files.get(k))
    assert isinstance(members, MappingProxyType)
    with pytest.raises(TypeError):
        members["testmap"] = ()


def test_pakfile_members_key_on_read_bytes_identity_too():
    """Finding 2: two different `read_bytes` injections over the same index do not share a memo,
    even though nothing about the index itself changed."""

    from elysium_pipeline.formats.map_glb import pakfile_index

    index, files = _pakfile_probe_install()
    read_one = lambda _index, key: files.get(key)
    read_two = lambda _index, key: files.get(key)
    first = pakfile_index.pakfile_members(index, read_bytes=read_one)
    second = pakfile_index.pakfile_members(index, read_bytes=read_two)
    assert first is not second
    assert dict(first) == dict(second)


def test_pakfile_members_do_not_collide_across_index_identity_reuse():
    """Finding 2: a memo keyed on `id(index)` alone answers for the wrong index once the first
    one is freed and CPython reuses its address. Two sequential, unrelated indexes -- the second
    built only after the first is dropped -- must each get their own members regardless of
    whether the allocator actually reused the address."""

    import gc

    from elysium_pipeline.formats.map_glb import pakfile_index

    bsp_a = _bsp_with_pakfile({"materials/maps/mapa/stem.tth": b"aaaa"})
    index_a = {"maps/mapa.bsp": ("loose", "synthetic/maps/mapa.bsp")}
    files_a = {"maps/mapa.bsp": bsp_a}
    members_a = pakfile_index.pakfile_members(
        index_a, read_bytes=lambda _index, key: files_a.get(key)
    )
    assert set(members_a) == {"mapa"}
    del index_a
    gc.collect()

    bsp_b = _bsp_with_pakfile({"materials/maps/mapb/stem.tth": b"bbbb"})
    index_b = {"maps/mapb.bsp": ("loose", "synthetic/maps/mapb.bsp")}
    files_b = {"maps/mapb.bsp": bsp_b}
    members_b = pakfile_index.pakfile_members(
        index_b, read_bytes=lambda _index, key: files_b.get(key)
    )
    assert set(members_b) == {"mapb"}


def test_pakfile_failures_names_a_map_whose_pakfile_could_not_be_parsed():
    """Finding 4: an unparseable BSP is not silent -- it is named by `pakfile_failures`."""

    from elysium_pipeline.formats.map_glb import pakfile_index

    index = {"maps/brokenmap.bsp": ("loose", "synthetic/maps/brokenmap.bsp")}
    files = {"maps/brokenmap.bsp": b"too-short-to-be-a-bsp"}
    read = lambda _index, key: files.get(key)
    members = pakfile_index.pakfile_members(index, read_bytes=read)
    assert members.get("brokenmap") is None
    failures = pakfile_index.pakfile_failures(index, read_bytes=read)
    assert "brokenmap" in failures
    assert failures["brokenmap"]


def test_pakfile_members_skip_a_member_outside_its_own_maps_prefix():
    """Finding 5: a member whose key does not belong to the BSP it came from is a per-member
    warning and a skip, not a silent cross-map leak."""

    from elysium_pipeline.formats.map_glb import pakfile_index

    bsp = _bsp_with_pakfile(
        {
            "materials/maps/testmap/good.tth": b"1234",
            "materials/maps/othermap/bad.tth": b"5678",
        }
    )
    index = {"maps/testmap.bsp": ("loose", "synthetic/maps/testmap.bsp")}
    files = {"maps/testmap.bsp": bsp}
    members = pakfile_index.pakfile_members(
        index, read_bytes=lambda _index, key: files.get(key)
    )
    names = {member.name.lower() for member in members["testmap"]}
    assert names == {"materials/maps/testmap/good.tth"}


def test_load_source_closure_prefers_an_install_member_over_the_pakfile_probe():
    """Finding 5: an install member under `materials/maps/**` wins over the PAKFILE probe of the
    same spelling."""

    index, files = _pakfile_probe_install()
    index = dict(index)
    files = dict(files)
    index["materials/maps/testmap/c0_0_0.tth"] = ("loose", "synthetic/install-probe.tth")
    files["materials/maps/testmap/c0_0_0.tth"] = b"install-wins-bytes"
    closure = texture_source.load_source_closure(
        index, "maps/testmap/c0_0_0", read_bytes=lambda _index, key: files.get(key)
    )
    assert closure.tth.data == b"install-wins-bytes"
    assert closure.tth.path == "materials/maps/testmap/c0_0_0.tth"
    assert closure.tth.origin["kind"] == "loose"


def test_load_source_closure_rejects_a_flat_maps_key_with_no_stem():
    """Finding 5: `maps/<name>` with nothing after it names no map-relative stem to resolve."""

    index, files = _pakfile_probe_install()
    with pytest.raises(texture_source.TextureSourceError, match="stem"):
        texture_source.load_source_closure(
            index, "maps/overview", read_bytes=lambda _index, key: files.get(key)
        )
