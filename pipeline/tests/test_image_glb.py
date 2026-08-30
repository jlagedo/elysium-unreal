"""Synthetic contract tests for the isolated Image GLB exporter.

Every fixture here is bytes this test builds itself -- never the real install -- so the suite
proves the seam's own rules: the ledger is gapless, the key keeps its extension, every KTX2
`vkFormat` table row round-trips, row/column orientation is exact and reversible, and a tampered
published unit is caught by both the export-time and the standalone validator.
"""

from __future__ import annotations

import hashlib
import struct

import pytest

from elysium_pipeline.exporters import image_glb as exporter
from elysium_pipeline.formats.image_glb import (
    IMAGE_EXTENSION,
    BmpDecodeError,
    ImageModelError,
    ImageSourceClosure,
    ImageSourceError,
    TgaDecodeError,
    asset_id,
    container_of,
    decode_bmp,
    decode_image,
    decode_tga,
    ktx2 as image_ktx2,
    load_source_closure,
    normalize_image_path,
    output_relative_path,
)
from elysium_pipeline.formats.image_glb.coverage import (
    claim_uncovered_gaps,
    claim_uncovered_gaps_graded,
    palette_usage,
)
from elysium_pipeline.formats.image_glb.tga import FOOTER_LENGTH, FOOTER_SIGNATURE
from elysium_pipeline.formats.unit_contract import ByteLedger
from elysium_pipeline.formats.unit_contract.coverage import ROOT_KEYS
from elysium_pipeline.formats.unit_contract.origin import Origin, SourceMember
from elysium_pipeline.validation import image_glb as validation
from elysium_pipeline.validation.image_glb import ImageGlbValidationError


# --- byte builders -----------------------------------------------------------------------------


def _tga_header(
    *,
    id_length: int = 0,
    color_map_type: int = 0,
    image_type: int,
    color_map_first: int = 0,
    color_map_length: int = 0,
    color_map_depth: int = 0,
    x_origin: int = 0,
    y_origin: int = 0,
    width: int,
    height: int,
    pixel_depth: int,
    descriptor: int = 0,
) -> bytes:
    return struct.pack(
        "<BBBHHBHHHHBB",
        id_length, color_map_type, image_type, color_map_first, color_map_length,
        color_map_depth, x_origin, y_origin, width, height, pixel_depth, descriptor,
    )


def _rle_encode(pixels: list[bytes]) -> bytes:
    """One raw packet per call site's pixel list -- enough to exercise the RLE reader."""

    out = bytearray()
    index = 0
    while index < len(pixels):
        run = 1
        while index + run < len(pixels) and pixels[index + run] == pixels[index] and run < 128:
            run += 1
        if run > 1:
            out.append(0x80 | (run - 1))
            out.extend(pixels[index])
            index += run
        else:
            span = [pixels[index]]
            index += 1
            while index < len(pixels) and len(span) < 128 and (
                index + 1 >= len(pixels) or pixels[index] != pixels[index + 1]
            ):
                span.append(pixels[index])
                index += 1
            out.append(len(span) - 1)
            for pixel in span:
                out.extend(pixel)
    return bytes(out)


def _bmp(
    *,
    width: int,
    height: int,
    bit_count: int,
    rows: list[bytes],
    palette: bytes = b"",
    compression: int = 0,
    clr_used: int = 0,
    size_image: int = 0,
) -> bytes:
    info = struct.pack(
        "<IiiHHIiiIII", 40, width, height, 1, bit_count, compression, size_image, 0, 0, clr_used, 0
    )
    row_bytes = width * (bit_count // 8)
    stride = (row_bytes + 3) // 4 * 4
    padded_rows = b"".join(row + b"\0" * (stride - len(row)) for row in rows)
    off_bits = 14 + 40 + len(palette)
    file_header = struct.pack("<2sIHHI", b"BM", off_bits + len(padded_rows), 0, 0, off_bits)
    return file_header + info + palette + padded_rows


def _closure(path: str, data: bytes) -> ImageSourceClosure:
    container = container_of(path)
    member = SourceMember(role="image", path=path, data=data, origin=Origin(kind="loose", root="Vampire"))
    return ImageSourceClosure(path, asset_id(path), container, member)


# --- identity ------------------------------------------------------------------------------


def test_normalize_image_path_folds_case_and_keeps_the_extension():
    assert normalize_image_path(r"Particles\Fire01.TGA") == "particles/fire01.tga"
    assert normalize_image_path("GFX/HLFACEPOSER/Icon.BMP") == "gfx/hlfaceposer/icon.bmp"


def test_normalize_image_path_rejects_traversal_and_the_wrong_extension():
    with pytest.raises(ImageModelError):
        normalize_image_path("../particles/fire.tga")
    with pytest.raises(ImageModelError):
        normalize_image_path("materials/skin.vmt")


def test_asset_id_and_output_path_keep_the_whole_installed_relative_path():
    assert asset_id("Materials/UI/Icon.tga") == "vtmb:image:materials/ui/icon.tga"
    assert output_relative_path("Materials/UI/Icon.tga") == output_relative_path(
        "materials/ui/icon.tga"
    )
    assert str(output_relative_path("particles/fire01.tga")) == "particles/fire01.tga.glb"


def test_container_of_reads_the_keys_own_extension():
    assert container_of("particles/fire01.tga") == "tga"
    assert container_of("gfx/hlfaceposer/icon.bmp") == "bmp"


# --- source resolution -----------------------------------------------------------------------


def test_load_source_closure_resolves_through_the_injected_reader():
    index = {"particles/fire01.tga": ("loose", "C:/x")}
    closure = load_source_closure(
        index, "Particles/Fire01.TGA", read_bytes=lambda idx, key: b"payload"
    )
    assert closure.image_path == "particles/fire01.tga"
    assert closure.container == "tga"
    assert closure.member.data == b"payload"
    assert closure.asset_id == "vtmb:image:particles/fire01.tga"


def test_load_source_closure_raises_when_the_install_has_no_member():
    with pytest.raises(ImageSourceError):
        load_source_closure({}, "particles/missing.tga", read_bytes=lambda idx, key: None)


def test_source_keys_enumerates_every_tga_and_bmp_member_and_nothing_else():
    index = {
        "particles/fire01.tga": ("loose", "a"),
        "gfx/hlfaceposer/icon.bmp": ("loose", "b"),
        "materials/skin.vmt": ("loose", "c"),
        "models/a.mdl": ("loose", "d"),
    }
    assert exporter.source_keys(index) == ["gfx/hlfaceposer/icon.bmp", "particles/fire01.tga"]


# --- TGA: uncompressed formats -----------------------------------------------------------------


def test_a_24_bit_uncompressed_tga_reverses_rows_to_top_down():
    # Default descriptor (0): bottom-up, left-to-right -- the ordinary TGA convention.
    bottom_row = bytes([0, 0, 255]) + bytes([0, 255, 0])     # BGR: red, green
    top_row = bytes([255, 0, 0]) + bytes([255, 255, 255])    # blue, white
    header = _tga_header(image_type=2, width=2, height=2, pixel_depth=24)
    data = header + bottom_row + top_row
    model = decode_tga(_closure("particles/rows.tga", data))
    assert model.vk_format_name == "VK_FORMAT_B8G8R8_UNORM"
    assert model.pixel_data == top_row + bottom_row
    assert model.orientation == {
        "sourceOrigin": "bottom-left", "rowsReversed": True, "columnsReversed": False,
    }
    row = model.ledger_row
    assert row["byteLength"] == len(data) == row["accountedBytes"]
    assert row["coveragePercent"] == 100.0
    assert [entry["state"] for entry in row["ranges"]] == ["mapped", "mapped"]


def test_a_top_left_origin_tga_is_not_row_reversed():
    header = _tga_header(image_type=2, width=1, height=2, pixel_depth=24, descriptor=0x20)
    rows = bytes([1, 2, 3]) + bytes([4, 5, 6])   # already top-down in the file
    model = decode_tga(_closure("particles/topleft.tga", header + rows))
    assert model.pixel_data == rows
    assert model.orientation["rowsReversed"] is False
    assert model.orientation["sourceOrigin"] == "top-left"


def test_a_right_to_left_tga_is_column_reversed_per_row():
    header = _tga_header(image_type=2, width=2, height=1, pixel_depth=24, descriptor=0x20 | 0x10)
    row = bytes([1, 1, 1]) + bytes([2, 2, 2])   # file order is right-to-left
    model = decode_tga(_closure("particles/rl.tga", header + row))
    assert model.orientation == {
        "sourceOrigin": "top-right", "rowsReversed": False, "columnsReversed": True,
    }
    assert model.pixel_data == bytes([2, 2, 2]) + bytes([1, 1, 1])


def test_a_32_bit_tga_keeps_bgra_verbatim():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=32, descriptor=0x20)
    pixel = bytes([10, 20, 30, 255])
    model = decode_tga(_closure("particles/rgba.tga", header + pixel))
    assert model.vk_format_name == "VK_FORMAT_B8G8R8A8_UNORM"
    assert model.pixel_data == pixel


def test_a_16_bit_tga_uses_the_packed_vk_format_verbatim():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=16, descriptor=0x20)
    pixel = struct.pack("<H", (1 << 15) | (5 << 10) | (9 << 5) | 3)
    model = decode_tga(_closure("particles/16bit.tga", header + pixel))
    assert model.vk_format_name == "VK_FORMAT_A1R5G5B5_UNORM_PACK16"
    assert model.pixel_data == pixel


def test_an_8_bit_greyscale_tga_uses_r8_unorm():
    header = _tga_header(image_type=3, width=2, height=1, pixel_depth=8, descriptor=0x20)
    pixel = bytes([12, 200])
    model = decode_tga(_closure("particles/grey.tga", header + pixel))
    assert model.vk_format_name == "VK_FORMAT_R8_UNORM"
    assert model.pixel_data == pixel


# --- TGA: RLE ------------------------------------------------------------------------------


def test_an_rle_true_color_tga_decodes_to_the_same_pixels_as_uncompressed():
    pixels = [bytes([1, 1, 1]), bytes([1, 1, 1]), bytes([9, 9, 9]), bytes([1, 1, 1])]
    header = _tga_header(image_type=10, width=2, height=2, pixel_depth=24, descriptor=0x20)
    rle_body = _rle_encode(pixels)
    model = decode_tga(_closure("particles/rle.tga", header + rle_body))
    assert model.pixel_data == b"".join(pixels)
    owners = {entry["owner"]: entry["state"] for entry in model.ledger_row["ranges"]}
    assert owners["tga.image"] == "derived"


def test_a_truncated_rle_stream_fails_the_export():
    header = _tga_header(image_type=10, width=4, height=1, pixel_depth=24, descriptor=0x20)
    # A run of 2 identical pixels is 6 pixel-bytes short of the 4 pixels (12 bytes) declared, and
    # nothing follows in the file to make up the difference.
    with pytest.raises(TgaDecodeError):
        decode_tga(_closure("particles/broken.tga", header + bytes([0x81, 1, 2, 3])))


# --- TGA: colour-mapped ----------------------------------------------------------------------


def test_an_8_bit_colormapped_tga_carries_indices_and_a_palette():
    palette = bytes([0, 0, 0]) + bytes([255, 255, 255]) + bytes([0, 0, 255])   # 3 BGR entries
    header = _tga_header(
        image_type=1, color_map_type=1, color_map_length=3, color_map_depth=24,
        width=2, height=1, pixel_depth=8, descriptor=0x20,
    )
    indices = bytes([0, 1])   # never touches index 2 -- an unused palette entry
    model = decode_tga(_closure("particles/indexed.tga", header + palette + indices))
    assert model.vk_format_name == "VK_FORMAT_R8_UINT"
    assert model.pixel_data == indices
    assert model.palette == {
        "entries": 3, "bitsPerEntry": 24, "firstIndex": 0,
        "colors": [
            {"blue": 0, "green": 0, "red": 0},
            {"blue": 255, "green": 255, "red": 255},
            {"blue": 0, "green": 0, "red": 255},
        ],
    }
    assert model.omissions == [{
        "role": "unused-palette-entries",
        "reason": "palette entries beyond the largest index the pixels use",
        "count": 1,
    }]


def test_an_unsupported_colormap_pixel_depth_fails_the_export():
    header = _tga_header(
        image_type=1, color_map_type=1, color_map_length=1, color_map_depth=24,
        width=1, height=1, pixel_depth=16, descriptor=0x20,
    )
    with pytest.raises(TgaDecodeError):
        decode_tga(_closure("particles/bad-index.tga", header + bytes(3) + bytes(2)))


def test_an_unsupported_tga_bit_depth_fails_the_export():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=15, descriptor=0x20)
    with pytest.raises(TgaDecodeError):
        decode_tga(_closure("particles/badbits.tga", header + bytes(2)))


def test_an_empty_tga_member_fails_the_export():
    with pytest.raises(TgaDecodeError):
        decode_tga(_closure("particles/empty.tga", b""))


# --- TGA: 2.0 footer -------------------------------------------------------------------------


def _extension_area(**overrides) -> bytes:
    area = bytearray(495)
    struct.pack_into("<H", area, 0, overrides.get("size", 495))
    struct.pack_into("<I", area, 482, overrides.get("color_correction_offset", 0))
    struct.pack_into("<I", area, 486, overrides.get("postage_stamp_offset", 0))
    struct.pack_into("<I", area, 490, overrides.get("scan_line_offset", 0))
    return bytes(area)


def test_a_tga_2_0_footer_decodes_the_extension_area_and_types_the_developer_tags():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    pixel = bytes([1, 2, 3])
    extension_area_offset = len(header) + len(pixel)
    extension_area = _extension_area()
    developer_directory_offset = extension_area_offset + len(extension_area)
    developer_area = b"tool-private"
    developer_area_offset = developer_directory_offset + 2 + 10
    directory = struct.pack("<H", 1) + struct.pack(
        "<HII", 999, developer_area_offset, len(developer_area)
    )
    footer = struct.pack(
        "<II", extension_area_offset, developer_directory_offset
    ) + FOOTER_SIGNATURE
    data = header + pixel + extension_area + directory + developer_area + footer
    model = decode_tga(_closure("particles/footer.tga", data))
    assert model.footer["extensionAreaOffset"] == extension_area_offset
    assert model.footer["developerDirectoryOffset"] == developer_directory_offset
    assert model.footer["extensionArea"]["size"] == 495
    assert model.footer["developerDirectory"]["tagCount"] == 1
    tag_offset = developer_directory_offset + 2
    assert model.typed_unidentified == [{
        "field": "developerDirectory.tag[0]",
        "reason": "a developer-area tag's meaning belongs to the authoring tool",
        "tag": 999,
        "sourceOffset": tag_offset,
        "areaOffset": developer_area_offset,
        "byteLength": len(developer_area),
        "sha256": hashlib.sha256(developer_area).hexdigest(),
    }]
    row = model.ledger_row
    assert row["accountedBytes"] == row["byteLength"] == len(data)


def test_bytes_between_the_image_and_the_footer_are_swept_into_a_trailing_omission():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    pixel = bytes([1, 2, 3])
    filler = b"\x01\x02\x03\x04"
    footer = struct.pack("<II", 0, 0) + FOOTER_SIGNATURE
    data = header + pixel + filler + footer
    model = decode_tga(_closure("particles/trailing.tga", data))
    filler_offset = len(header) + len(pixel)
    assert model.omissions == [{
        "role": "trailing-bytes",
        "reason": "bytes between decoded structures that no record claims",
        "byteLength": len(filler),
        "ranges": [{"sourceOffset": filler_offset, "byteLength": len(filler)}],
    }]
    assert model.ledger_row["accountedBytes"] == len(data)


def test_a_postage_stamp_offset_past_the_end_of_file_is_unresolved_not_silently_dropped():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    pixel = bytes([1, 2, 3])
    extension_area_offset = len(header) + len(pixel)
    bogus_offset = 999_999
    area = _extension_area(postage_stamp_offset=bogus_offset)
    footer = struct.pack("<II", extension_area_offset, 0) + FOOTER_SIGNATURE
    data = header + pixel + area + footer
    model = decode_tga(_closure("particles/badstamp.tga", data))
    assert model.footer["extensionArea"]["postageStampOffset"] == bogus_offset
    assert model.footer["extensionArea"]["postageStamp"] is None
    assert model.unresolved == [{
        "field": "extensionArea.postageStamp",
        "offset": bogus_offset,
        "reason": "the postage-stamp offset does not fit within the file",
    }]


def test_extension_area_offsets_are_published_even_when_every_sub_structure_is_absent():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    pixel = bytes([1, 2, 3])
    extension_area_offset = len(header) + len(pixel)
    area = _extension_area()
    footer = struct.pack("<II", extension_area_offset, 0) + FOOTER_SIGNATURE
    data = header + pixel + area + footer
    model = decode_tga(_closure("particles/nosubareas.tga", data))
    extension_area = model.footer["extensionArea"]
    assert extension_area["colorCorrectionTableOffset"] == 0
    assert extension_area["postageStampOffset"] == 0
    assert extension_area["scanLineTableOffset"] == 0
    assert model.unresolved == []


def test_an_extension_area_size_field_that_disagrees_with_the_bytes_read_is_an_anomaly():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    pixel = bytes([1, 2, 3])
    extension_area_offset = len(header) + len(pixel)
    area = _extension_area(size=123)
    footer = struct.pack("<II", extension_area_offset, 0) + FOOTER_SIGNATURE
    data = header + pixel + area + footer
    model = decode_tga(_closure("particles/badsize.tga", data))
    assert model.anomalies == [{
        "role": "extension-area-size-mismatch", "offset": extension_area_offset,
        "declaredSize": 123, "consumedSize": 495,
        "reason": "the extension area's own size field disagrees with the fixed 495 bytes read",
    }]


def test_an_interleaved_tga_fails_the_export_instead_of_publishing_a_wrong_pixel_order():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x40)
    with pytest.raises(TgaDecodeError):
        decode_tga(_closure("particles/interleaved.tga", header + bytes(3)))


def test_unused_palette_entries_are_counted_from_the_colour_maps_own_first_index():
    palette = bytes([0, 0, 0]) + bytes([1, 1, 1]) + bytes([2, 2, 2]) + bytes([3, 3, 3])
    header = _tga_header(
        image_type=1, color_map_type=1, color_map_first=10, color_map_length=4,
        color_map_depth=24, width=1, height=1, pixel_depth=8, descriptor=0x20,
    )
    # The one pixel indexes absolute palette slot 11 -- used-within-map index 1 -- leaving 2 of
    # the 4 declared entries (indices 12 and 13) unused.
    index = bytes([11])
    model = decode_tga(_closure("particles/offsetpalette.tga", header + palette + index))
    assert model.omissions == [{
        "role": "unused-palette-entries",
        "reason": "palette entries beyond the largest index the pixels use",
        "count": 2,
    }]


def test_a_pixel_index_below_the_colour_maps_first_index_is_an_anomaly_not_a_negative_count():
    palette = bytes([0, 0, 0]) + bytes([1, 1, 1])
    header = _tga_header(
        image_type=1, color_map_type=1, color_map_first=10, color_map_length=2,
        color_map_depth=24, width=1, height=1, pixel_depth=8, descriptor=0x20,
    )
    # Absolute pixel index 0 is below `colorMapFirst=10`, so it addresses a slot the stored map
    # does not have; this must not fold into an inflated (or negative) unused-entries count.
    index = bytes([0])
    model = decode_tga(_closure("particles/belowmap.tga", header + palette + index))
    assert model.omissions == []
    assert model.anomalies == [{
        "role": "palette-index-out-of-range",
        "reason": "a pixel index falls outside the colour map the source declares",
        "maxIndex": 0,
        "colorMapFirst": 10,
        "colorMapEntries": 2,
    }]


def test_a_colour_map_on_a_non_indexed_tga_is_published_not_silently_dropped():
    # imageType 2 (true-colour) never indexes a colour map, but a genuine file can still carry
    # one; its bytes must reach `palette`, not be claimed `mapped` and then dropped from every
    # extension and payload.
    palette = bytes([0, 0, 0]) + bytes([255, 255, 255])
    header = _tga_header(
        image_type=2, color_map_type=1, color_map_length=2, color_map_depth=24,
        width=1, height=1, pixel_depth=24, descriptor=0x20,
    )
    pixel = bytes([1, 2, 3])
    data = header + palette + pixel
    model = decode_tga(_closure("particles/unusedmap.tga", data))
    assert model.palette == {
        "entries": 2,
        "bitsPerEntry": 24,
        "firstIndex": 0,
        "colors": [
            {"blue": 0, "green": 0, "red": 0},
            {"blue": 255, "green": 255, "red": 255},
        ],
    }
    assert model.omissions == []
    assert model.anomalies == []
    owners = {entry["owner"] for entry in model.ledger_row["ranges"]}
    assert "tga.colorMap" in owners
    assert model.ledger_row["accountedBytes"] == model.ledger_row["byteLength"] == len(data)


def test_a_footer_that_overlaps_its_own_developer_directory_is_unresolved_not_raised():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    pixel = bytes([1, 2, 3])
    # A zero-tag developer directory table is 2 bytes; positioning it one byte before the
    # footer's own 26 bytes makes the footer's first byte (extensionAreaOffset's low byte, kept
    # 0 so no extension area is referenced) overlap the table's own second byte (also 0, keeping
    # `numTags` zero) -- one shared byte, claimed twice.
    developer_directory_offset = len(header) + len(pixel)
    footer = struct.pack("<II", 0, developer_directory_offset) + FOOTER_SIGNATURE
    data = header + pixel + bytes([0]) + footer
    footer_offset = len(data) - FOOTER_LENGTH
    model = decode_tga(_closure("particles/footeroverlap.tga", data))
    assert model.footer is not None
    assert model.footer["sourceOffset"] == footer_offset
    assert model.footer["extensionArea"] is None
    assert model.footer["developerDirectory"] == {
        "sourceOffset": developer_directory_offset, "tagCount": 0, "tags": [],
    }
    assert model.unresolved == [{
        "field": "footer", "offset": footer_offset, "length": FOOTER_LENGTH,
        "reason": (
            "the footer overlaps another claimed range: "
            f"particles/footeroverlap.tga: tga.footer range {footer_offset}+{FOOTER_LENGTH} "
            f"overlaps tga.developerDirectory range {developer_directory_offset}+2"
        ),
    }]
    assert model.ledger_row["accountedBytes"] == model.ledger_row["byteLength"] == len(data)


# --- BMP ------------------------------------------------------------------------------------


def test_a_24_bit_bmp_reverses_rows_and_removes_padding():
    # width 1 forces a 3-byte row and a 1-byte pad -- enough to exercise `bmp.row[i].pad`.
    bottom = bytes([9, 9, 9])
    top = bytes([1, 2, 3])
    data = _bmp(width=1, height=2, bit_count=24, rows=[bottom, top])
    model = decode_bmp(_closure("gfx/hlfaceposer/a.bmp", data))
    assert model.vk_format_name == "VK_FORMAT_B8G8R8_UNORM"
    assert model.pixel_data == top + bottom
    assert model.orientation == {
        "sourceOrigin": "bottom-left", "rowsReversed": True, "columnsReversed": False,
    }
    owners = {entry["owner"]: entry["state"] for entry in model.ledger_row["ranges"]}
    assert owners["bmp.row[0].pad"] == "padding-zero"
    assert model.ledger_row["accountedBytes"] == model.ledger_row["byteLength"] == len(data)


def test_a_negative_height_bmp_is_already_top_down():
    top = bytes([1, 2, 3])
    bottom = bytes([9, 9, 9])
    data = _bmp(width=1, height=-2, bit_count=24, rows=[top, bottom])
    model = decode_bmp(_closure("gfx/hlfaceposer/b.bmp", data))
    assert model.pixel_data == top + bottom
    assert model.orientation["rowsReversed"] is False
    assert model.orientation["sourceOrigin"] == "top-left"


def test_a_non_zero_row_pad_is_an_omission_not_a_zero_claim():
    row = bytes([5, 6, 7])
    data = bytearray(_bmp(width=1, height=1, bit_count=24, rows=[row]))
    data[-1] = 0xFF   # the one alignment pad byte, made non-zero
    model = decode_bmp(_closure("gfx/hlfaceposer/pad.bmp", bytes(data)))
    owners = {entry["owner"]: entry["state"] for entry in model.ledger_row["ranges"]}
    assert owners["bmp.row[0].pad"] == "omitted-proven"
    assert {"role": "row-pad", "reason": "BMP row alignment bytes the source stores non-zero"} \
        in model.omissions


def test_an_8_bit_palette_bmp_carries_indices_and_a_palette():
    palette = bytes([0, 0, 0, 0]) + bytes([0, 0, 255, 0])   # black, red (BGR + reserved)
    data = _bmp(
        width=4, height=1, bit_count=8, rows=[bytes([0, 1, 0, 0])],
        palette=palette, clr_used=2,
    )
    model = decode_bmp(_closure("gfx/hlfaceposer/pal.bmp", data))
    assert model.vk_format_name == "VK_FORMAT_R8_UINT"
    assert model.palette["entries"] == 2
    assert model.palette["colors"] == [
        {"blue": 0, "green": 0, "red": 0, "reserved": 0},
        {"blue": 0, "green": 0, "red": 255, "reserved": 0},
    ]
    assert model.pixel_data == bytes([0, 1, 0, 0])


def test_a_non_zero_gap_between_the_headers_and_the_pixel_data_is_an_omission_not_a_zero_claim():
    gap_bytes = bytes([0xAA, 0xBB, 0xCC, 0xDD])
    info = struct.pack("<IiiHHIiiIII", 40, 1, 1, 1, 24, 0, 0, 0, 0, 0, 0)
    pixel_row = bytes([5, 6, 7]) + bytes([0])   # 3 pixel bytes + 1 zero alignment pad byte
    off_bits = 14 + 40 + len(gap_bytes)
    file_size = off_bits + len(pixel_row)
    file_header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, off_bits)
    data = file_header + info + gap_bytes + pixel_row
    model = decode_bmp(_closure("gfx/hlfaceposer/gap.bmp", data))
    owners = {entry["owner"]: entry["state"] for entry in model.ledger_row["ranges"]}
    assert owners["bmp.gap"] == "omitted-proven"
    assert {
        "role": "gap-non-zero",
        "reason": "bytes between the colour table and the pixel data the source stores non-zero",
        "byteLength": len(gap_bytes),
    } in model.omissions
    assert model.ledger_row["accountedBytes"] == model.ledger_row["byteLength"] == len(data)


def test_a_tampered_file_size_field_is_an_anomaly_not_a_silent_republish():
    data = bytearray(_bmp(width=1, height=1, bit_count=24, rows=[bytes([1, 2, 3])]))
    struct.pack_into("<I", data, 2, len(data) + 100)
    model = decode_bmp(_closure("gfx/hlfaceposer/wrongsize.bmp", bytes(data)))
    assert model.anomalies == [{
        "role": "file-size-mismatch",
        "reason": "the file header's declared size disagrees with the member's actual length",
        "declaredSize": len(data) + 100,
        "actualSize": len(data),
    }]


def test_a_sizeimage_field_that_disagrees_with_stride_times_height_is_an_anomaly():
    data = _bmp(width=1, height=1, bit_count=24, rows=[bytes([1, 2, 3])], size_image=999)
    model = decode_bmp(_closure("gfx/hlfaceposer/wrongsizeimage.bmp", data))
    assert model.anomalies == [{
        "role": "size-image-mismatch",
        "reason": "the info header's sizeImage disagrees with stride * height",
        "declaredSizeImage": 999,
        "computedSizeImage": 4,
    }]


def test_a_32_bit_bmp_uses_bgra_verbatim():
    pixel = bytes([1, 2, 3, 255])
    data = _bmp(width=1, height=1, bit_count=32, rows=[pixel])
    model = decode_bmp(_closure("gfx/hlfaceposer/rgba.bmp", data))
    assert model.vk_format_name == "VK_FORMAT_B8G8R8A8_UNORM"
    assert model.pixel_data == pixel


def test_a_compressed_bmp_fails_the_export():
    data = _bmp(width=1, height=1, bit_count=24, rows=[bytes(3)], compression=1)
    with pytest.raises(BmpDecodeError):
        decode_bmp(_closure("gfx/hlfaceposer/bad.bmp", data))


def test_an_empty_bmp_member_fails_the_export():
    with pytest.raises(BmpDecodeError):
        decode_bmp(_closure("gfx/hlfaceposer/empty.bmp", b""))


def test_a_pixel_data_offset_past_the_member_fails_closed_as_a_bmp_decode_error():
    # A truncated source whose `bfOffBits` points past the file's own length must fail as this
    # seam's documented error, not escape as a ledger-internal `ByteLedgerError` for what is
    # really a malformed/truncated BMP.
    data = bytearray(_bmp(width=2, height=2, bit_count=24, rows=[bytes(6), bytes(6)]))
    bogus_off_bits = len(data) + 1000
    data[10:14] = struct.pack("<I", bogus_off_bits)
    with pytest.raises(BmpDecodeError, match="past the"):
        decode_bmp(_closure("gfx/hlfaceposer/truncated.bmp", bytes(data)))


def test_zero_filled_bmp_trailing_bytes_are_padding_zero_not_an_omission():
    data = _bmp(width=1, height=1, bit_count=24, rows=[bytes([1, 2, 3])]) + bytes(2)
    model = decode_bmp(_closure("gfx/hlfaceposer/ziptrail.bmp", data))
    owners = {entry["owner"]: entry["state"] for entry in model.ledger_row["ranges"]}
    assert owners["bmp.trailing"] == "padding-zero"
    assert model.omissions == []
    assert model.ledger_row["accountedBytes"] == model.ledger_row["byteLength"] == len(data)


def test_non_zero_bmp_trailing_bytes_are_an_omitted_proven_omission_with_their_offset():
    trailer_offset = len(_bmp(width=1, height=1, bit_count=24, rows=[bytes([1, 2, 3])]))
    data = _bmp(width=1, height=1, bit_count=24, rows=[bytes([1, 2, 3])]) + bytes([9, 9])
    model = decode_bmp(_closure("gfx/hlfaceposer/nonzerotrail.bmp", data))
    owners = {entry["owner"]: entry["state"] for entry in model.ledger_row["ranges"]}
    assert owners["bmp.trailing"] == "omitted-proven"
    assert model.omissions == [{
        "role": "trailing-bytes",
        "reason": "bytes after the pixel data that no record claims",
        "byteLength": 2,
        "ranges": [{"sourceOffset": trailer_offset, "byteLength": 2}],
    }]


def test_a_bmp_pixel_index_beyond_the_declared_palette_is_an_anomaly_not_a_negative_count():
    palette = bytes([0, 0, 0, 0])   # one declared entry
    data = _bmp(width=1, height=1, bit_count=8, rows=[bytes([5])], palette=palette, clr_used=1)
    model = decode_bmp(_closure("gfx/hlfaceposer/badidx.bmp", data))
    assert model.omissions == []
    assert model.anomalies == [{
        "role": "palette-index-out-of-range",
        "reason": "a pixel index falls outside the colour map the source declares",
        "maxIndex": 5,
        "colorMapFirst": 0,
        "colorMapEntries": 1,
    }]


# --- shared byte-ledger sweep ------------------------------------------------------------------


def test_claim_uncovered_gaps_sweeps_every_untouched_byte_under_one_owner():
    ledger = ByteLedger("x", bytes(10))
    ledger.claim(2, 3, "mapped", "middle")
    swept, ranges = claim_uncovered_gaps(ledger, "gap")
    assert swept == 7
    assert ranges == [
        {"sourceOffset": 0, "byteLength": 2}, {"sourceOffset": 5, "byteLength": 5},
    ]
    row = ledger.finish()
    assert row["stateBytes"] == {"mapped": 3, "omitted-proven": 7}


def test_claim_uncovered_gaps_graded_grades_an_all_zero_sweep_padding_zero():
    ledger = ByteLedger("x", bytes(10))
    ledger.claim(2, 3, "mapped", "middle")
    claimed, non_zero = claim_uncovered_gaps_graded(ledger, "gap", bytes(10))
    assert claimed == 0
    assert non_zero == []
    row = ledger.finish()
    assert row["stateBytes"] == {"mapped": 3, "padding-zero": 7}


def test_claim_uncovered_gaps_graded_grades_a_non_zero_sweep_omitted_proven():
    data = bytes([0, 0, 9, 9, 9, 0, 0, 0, 7, 0])
    ledger = ByteLedger("x", data)
    ledger.claim(2, 3, "mapped", "middle")
    claimed, non_zero = claim_uncovered_gaps_graded(ledger, "gap", data)
    assert claimed == 5
    assert non_zero == [{"sourceOffset": 5, "byteLength": 5}]
    row = ledger.finish()
    assert row["stateBytes"] == {"mapped": 3, "padding-zero": 2, "omitted-proven": 5}


# --- KTX2 payload ----------------------------------------------------------------------------


@pytest.mark.parametrize(
    "vk_format,width,height",
    [
        (image_ktx2.VK_FORMAT_B8G8R8A8_UNORM, 2, 2),
        (image_ktx2.VK_FORMAT_B8G8R8_UNORM, 2, 2),
        (image_ktx2.VK_FORMAT_A1R5G5B5_UNORM_PACK16, 2, 2),
        (image_ktx2.VK_FORMAT_R8_UNORM, 2, 2),
        (image_ktx2.VK_FORMAT_R8_UINT, 2, 2),
    ],
)
def test_every_admitted_ktx2_format_declares_one_level_one_face_and_the_right_dfd(
    vk_format, width, height
):
    plane = image_ktx2.bytes_per_pixel(vk_format)
    pixels = bytes(range(width * height * plane))
    payload, meta = image_ktx2.build(width, height, vk_format, pixels)
    assert payload[:12] == image_ktx2.IDENTIFIER
    fields = struct.unpack_from("<9I", payload, 12)
    assert fields == (
        vk_format, image_ktx2.type_size_for(vk_format), width, height, 0, 0, 1, 1, 0,
    )
    dfd_offset, dfd_length = struct.unpack_from("<2I", payload, 48)
    assert payload[dfd_offset:dfd_offset + dfd_length] == image_ktx2.dfd_bytes(vk_format)
    level_offset, level_length, level_uncompressed = struct.unpack_from("<3Q", payload, 80)
    assert level_length == level_uncompressed == len(pixels)
    assert payload[level_offset:level_offset + level_length] == pixels
    assert meta["byteLength"] == len(payload)


def test_ktx2_build_rejects_pixel_data_of_the_wrong_length():
    with pytest.raises(ValueError):
        image_ktx2.build(2, 2, image_ktx2.VK_FORMAT_R8_UNORM, bytes(3))


def test_the_packed_16_bit_format_declares_a_two_byte_type_size_and_the_rest_declare_one():
    assert image_ktx2.type_size_for(image_ktx2.VK_FORMAT_A1R5G5B5_UNORM_PACK16) == 2
    for vk_format in (
        image_ktx2.VK_FORMAT_B8G8R8A8_UNORM,
        image_ktx2.VK_FORMAT_B8G8R8_UNORM,
        image_ktx2.VK_FORMAT_R8_UNORM,
        image_ktx2.VK_FORMAT_R8_UINT,
    ):
        assert image_ktx2.type_size_for(vk_format) == 1


def test_a_ktx2_header_with_the_wrong_type_size_for_its_vk_format_fails_validation(tmp_path):
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=16, descriptor=0x20)
    pixel = struct.pack("<H", (1 << 15) | (5 << 10) | (9 << 5) | 3)
    index = {"particles/packed16.tga": ("loose", "C:/x")}
    destination = exporter.export(
        index, "particles/packed16.tga", tmp_path,
        read_bytes=lambda idx, key: header + pixel,
    )
    document, binary = validation.read_glb(destination)
    corrupted = bytearray(binary)
    # typeSize is the 9-int header's second field, right after `vkFormat`, at byte offset 16.
    struct.pack_into("<I", corrupted, 16, 1)
    with pytest.raises(ImageGlbValidationError):
        validation.validate_document(document, bytes(corrupted))


# --- extension root shape --------------------------------------------------------------------


def _publish(path: str, data: bytes):
    model = decode_image(_closure(path, data))
    document, binary = exporter.build_document(model)
    return model, document, binary


def test_build_document_orders_the_extension_root_per_the_shared_contract():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    _, document, binary = _publish("particles/order.tga", header + bytes(3))
    root = document["extensions"][IMAGE_EXTENSION]
    assert list(root)[: len(ROOT_KEYS)] == list(ROOT_KEYS)
    assert list(root)[len(ROOT_KEYS):] == [
        "payload", "dimensions", "sourceFormat", "palette", "footer", "orientation",
        "anomalies", "omissions",
    ]
    assert root["dependencies"] == []
    assert document["extensionsUsed"] == document["extensionsRequired"] == [IMAGE_EXTENSION]
    assert document["asset"]["generator"] == "Elysium Image GLB Exporter"


def test_a_published_unit_declares_no_scene_and_exactly_one_buffer_view():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    _, document, binary = _publish("particles/scene.tga", header + bytes(3))
    for forbidden in ("scenes", "nodes", "meshes", "images", "textures", "samplers"):
        assert forbidden not in document
    assert len(document["bufferViews"]) == 1
    assert document["bufferViews"][0]["byteOffset"] == 0
    assert len(document["buffers"]) == 1


# --- export + validation ---------------------------------------------------------------------


def test_export_writes_a_unit_that_standalone_validation_accepts(tmp_path):
    header = _tga_header(image_type=2, width=2, height=1, pixel_depth=24, descriptor=0x20)
    data = header + bytes([1, 2, 3]) + bytes([4, 5, 6])
    index = {"particles/roundtrip.tga": ("loose", "C:/x")}
    destination = exporter.export(
        index, "Particles/RoundTrip.TGA", tmp_path, read_bytes=lambda idx, key: data
    )
    assert destination == tmp_path / "particles" / "roundtrip.tga.glb"
    summary = validation.validate(destination)
    assert summary["asset"] == "vtmb:image:particles/roundtrip.tga"
    assert summary["byteCoveragePercent"] == 100.0
    assert validation.warnings_for(summary) == []


def test_export_time_validation_catches_a_tampered_ledger_range():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    data = header + bytes([1, 2, 3])
    model, document, binary = _publish("particles/tamper.tga", data)
    document["extensions"][IMAGE_EXTENSION]["coverage"]["byteLedger"][0]["ranges"][0]["length"] = 5
    member = SourceMember(role="image", path="particles/tamper.tga", data=data,
                           origin=Origin(kind="loose", root="Vampire"))
    with pytest.raises(ImageGlbValidationError):
        validation.validate_document(document, binary, source_members=[member])


def test_export_time_validation_notices_a_re_decode_that_disagrees_with_the_payload():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    data = header + bytes([1, 2, 3])
    model, document, binary = _publish("particles/swap.tga", data)
    other = header + bytes([9, 9, 9])
    member = SourceMember(role="image", path="particles/swap.tga", data=other,
                           origin=Origin(kind="loose", root="Vampire"))
    with pytest.raises(ImageGlbValidationError):
        validation.validate_document(document, binary, source_members=[member])


def test_standalone_validation_catches_a_corrupted_ktx2_hash():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    data = header + bytes([1, 2, 3])
    model, document, binary = _publish("particles/corrupt.tga", data)
    corrupted = bytearray(binary)
    corrupted[-1] ^= 0xFF
    with pytest.raises(ImageGlbValidationError):
        validation.validate_document(document, bytes(corrupted))


def test_a_dependency_declared_on_an_image_unit_is_refused():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    data = header + bytes([1, 2, 3])
    model, document, binary = _publish("particles/dep.tga", data)
    document["extensions"][IMAGE_EXTENSION]["dependencies"] = [
        {"role": "material", "asset": "vtmb:material:x", "sourcePath": "x.vmt", "resolved": True}
    ]
    with pytest.raises(ImageGlbValidationError):
        validation.validate_document(document, binary)


def test_standalone_validation_rejects_a_second_buffer_view():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    data = header + bytes([1, 2, 3])
    model, document, binary = _publish("particles/secondview.tga", data)
    document["bufferViews"].append({"buffer": 0, "byteOffset": 0, "byteLength": 1})
    with pytest.raises(ImageGlbValidationError):
        validation.validate_document(document, binary)


def test_standalone_validation_rejects_a_buffer_view_not_at_offset_zero():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    data = header + bytes([1, 2, 3])
    model, document, binary = _publish("particles/nonzerooffset.tga", data)
    document["bufferViews"][0]["byteOffset"] = 4
    with pytest.raises(ImageGlbValidationError):
        validation.validate_document(document, binary)


def test_export_time_validation_catches_a_tampered_orientation_record():
    header = _tga_header(image_type=2, width=1, height=1, pixel_depth=24, descriptor=0x20)
    data = header + bytes([1, 2, 3])
    model, document, binary = _publish("particles/orientation.tga", data)
    root = document["extensions"][IMAGE_EXTENSION]
    root["orientation"]["rowsReversed"] = not root["orientation"]["rowsReversed"]
    member = SourceMember(role="image", path="particles/orientation.tga", data=data,
                           origin=Origin(kind="loose", root="Vampire"))
    with pytest.raises(ImageGlbValidationError):
        validation.validate_document(document, binary, source_members=[member])
