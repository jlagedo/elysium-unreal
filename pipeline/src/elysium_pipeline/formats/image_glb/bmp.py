"""BMP (Windows bitmap) decode: `BITMAPFILEHEADER` + `BITMAPINFOHEADER`, `BI_RGB` only.

Only the Faceposer tool icons below `gfx/hlfaceposer/` are BMPs in this corpus, all written by
ordinary Windows tooling, so the 40-byte `BITMAPINFOHEADER` and uncompressed storage are the only
shapes this module admits; anything else fails the export per the seam's own rule.
"""

from __future__ import annotations

import struct
from typing import Any

from elysium_pipeline.formats.image_glb import ktx2
from elysium_pipeline.formats.image_glb.coverage import claim_uncovered_gaps_graded, palette_usage
from elysium_pipeline.formats.image_glb.model import ImageModel
from elysium_pipeline.formats.unit_contract import ByteLedger

FILE_HEADER_LENGTH = 14
INFO_HEADER_LENGTH = 40
BI_RGB = 0


class BmpDecodeError(ValueError):
    """The selected member cannot be read as a BMP image."""


def _format_for(bit_count: int) -> int:
    if bit_count == 32:
        return ktx2.VK_FORMAT_B8G8R8A8_UNORM
    if bit_count == 24:
        return ktx2.VK_FORMAT_B8G8R8_UNORM
    if bit_count == 8:
        return ktx2.VK_FORMAT_R8_UINT
    raise BmpDecodeError(f"unsupported BMP bit depth {bit_count}")


def decode_bmp(closure) -> ImageModel:
    member = closure.member
    data = member.data
    path = member.path
    if not data:
        raise BmpDecodeError(f"{path}: empty BMP member")
    if len(data) < FILE_HEADER_LENGTH + INFO_HEADER_LENGTH:
        raise BmpDecodeError(f"{path}: BMP header is truncated ({len(data)} bytes)")

    ledger = ByteLedger(path, data)
    signature = data[0:2]
    if signature != b"BM":
        raise BmpDecodeError(f"{path}: not a BMP file (signature {signature!r})")
    file_size, reserved1, reserved2, off_bits = struct.unpack_from("<IHHI", data, 2)
    ledger.claim(0, FILE_HEADER_LENGTH, "mapped", "bmp.fileHeader")

    (
        header_size, width, height, planes, bit_count, compression, size_image,
        x_pels, y_pels, clr_used, clr_important,
    ) = struct.unpack_from("<IiiHHIiiIII", data, FILE_HEADER_LENGTH)
    if header_size != INFO_HEADER_LENGTH:
        raise BmpDecodeError(f"{path}: unsupported BMP info header size {header_size}")
    ledger.claim(FILE_HEADER_LENGTH, INFO_HEADER_LENGTH, "mapped", "bmp.infoHeader")
    cursor = FILE_HEADER_LENGTH + INFO_HEADER_LENGTH

    if compression != BI_RGB:
        raise BmpDecodeError(f"{path}: unsupported BMP compression {compression}")
    if width <= 0 or height == 0:
        raise BmpDecodeError(f"{path}: image extent {width}x{height} carries no pixels")

    vk_format = _format_for(bit_count)
    pixel_size = ktx2.bytes_per_pixel(vk_format)
    is_index = bit_count == 8

    palette = None
    if is_index:
        color_count = clr_used if clr_used else (1 << bit_count)
        palette_length = color_count * 4
        palette_data = data[cursor:cursor + palette_length]
        if len(palette_data) != palette_length:
            raise BmpDecodeError(f"{path}: colour table is truncated")
        if palette_length:
            ledger.claim(cursor, palette_length, "mapped", "bmp.palette")
        cursor += palette_length
        colors = [
            {"blue": palette_data[index], "green": palette_data[index + 1],
             "red": palette_data[index + 2], "reserved": palette_data[index + 3]}
            for index in range(0, palette_length, 4)
        ]
        palette = {"entries": len(colors), "bitsPerEntry": 32, "firstIndex": 0, "colors": colors}

    if off_bits < cursor:
        raise BmpDecodeError(f"{path}: pixel data offset {off_bits} overlaps the headers")
    if off_bits > len(data):
        raise BmpDecodeError(
            f"{path}: pixel data offset {off_bits} is past the {len(data)}-byte member"
        )
    gap = off_bits - cursor
    gap_non_zero = False
    if gap:
        gap_bytes = data[cursor:cursor + gap]
        gap_non_zero = any(gap_bytes)
        ledger.claim(cursor, gap, "padding-zero" if not gap_non_zero else "omitted-proven", "bmp.gap")
    cursor = off_bits

    absolute_height = abs(height)
    row_bytes = width * pixel_size
    stride = (row_bytes + 3) // 4 * 4
    total_pixel_bytes = stride * absolute_height
    pixel_region = data[cursor:cursor + total_pixel_bytes]
    if len(pixel_region) != total_pixel_bytes:
        raise BmpDecodeError(f"{path}: pixel data is truncated")

    rows: list[bytes] = []
    non_zero_pad = False
    for row_index in range(absolute_height):
        row_start = row_index * stride
        rows.append(pixel_region[row_start:row_start + row_bytes])
        ledger.claim(cursor + row_start, row_bytes, "mapped", f"bmp.row[{row_index}]")
        pad = stride - row_bytes
        if pad:
            pad_bytes = pixel_region[row_start + row_bytes:row_start + stride]
            state = "padding-zero" if not any(pad_bytes) else "omitted-proven"
            ledger.claim(cursor + row_start + row_bytes, pad, state, f"bmp.row[{row_index}].pad")
            non_zero_pad = non_zero_pad or any(pad_bytes)
    cursor += total_pixel_bytes

    trailing, trailing_ranges = claim_uncovered_gaps_graded(ledger, "bmp.trailing", data)

    omissions: list[dict[str, Any]] = []
    if non_zero_pad:
        omissions.append({
            "role": "row-pad",
            "reason": "BMP row alignment bytes the source stores non-zero",
        })
    if gap_non_zero:
        omissions.append({
            "role": "gap-non-zero",
            "reason": "bytes between the colour table and the pixel data the source stores non-zero",
            "byteLength": gap,
        })
    if trailing:
        omissions.append({
            "role": "trailing-bytes",
            "reason": "bytes after the pixel data that no record claims",
            "byteLength": trailing,
            "ranges": trailing_ranges,
        })

    anomalies: list[dict[str, Any]] = []
    if file_size != len(data):
        anomalies.append({
            "role": "file-size-mismatch",
            "reason": "the file header's declared size disagrees with the member's actual length",
            "declaredSize": file_size,
            "actualSize": len(data),
        })
    if size_image and size_image != total_pixel_bytes:
        anomalies.append({
            "role": "size-image-mismatch",
            "reason": "the info header's sizeImage disagrees with stride * height",
            "declaredSizeImage": size_image,
            "computedSizeImage": total_pixel_bytes,
        })

    # A positive height stores rows bottom-up; KTX2 wants top-down. A negative height is already
    # top-down, so `rows` is not touched and the pixel byte order the file spells reaches the
    # payload unchanged, per the seam's own rule.
    rows_reversed = height > 0
    if rows_reversed:
        rows = rows[::-1]
    oriented = b"".join(rows)
    source_origin = "bottom-left" if height > 0 else "top-left"

    if is_index:
        max_index = max(oriented) if oriented else 0
        colors_declared = len(palette["colors"]) if palette else 0
        # A BMP colour map always starts at index 0 -- unlike a TGA's `colorMapFirst` -- so the
        # palette's own index space is the pixel's absolute space.
        omission, anomaly = palette_usage(max_index, 0, colors_declared)
        if omission is not None:
            omissions.append(omission)
        if anomaly is not None:
            anomalies.append(anomaly)

    source_format = {
        "fileHeader": {
            "type": signature.decode("latin-1"),
            "size": file_size,
            "reserved1": reserved1,
            "reserved2": reserved2,
            "offBits": off_bits,
        },
        "infoHeader": {
            "size": header_size,
            "width": width,
            "height": height,
            "planes": planes,
            "bitCount": bit_count,
            "compression": compression,
            "sizeImage": size_image,
            "xPelsPerMeter": x_pels,
            "yPelsPerMeter": y_pels,
            "clrUsed": clr_used,
            "clrImportant": clr_important,
        },
    }
    orientation = {
        "sourceOrigin": source_origin,
        "rowsReversed": rows_reversed,
        "columnsReversed": False,
    }

    return ImageModel(
        image_path=closure.image_path,
        asset_id=closure.asset_id,
        container="bmp",
        member=member,
        width=width,
        height=absolute_height,
        bits_per_pixel=bit_count,
        vk_format_name=ktx2.vk_format_name(vk_format),
        vk_format_value=vk_format,
        pixel_data=oriented,
        source_format=source_format,
        palette=palette,
        footer=None,
        orientation=orientation,
        omissions=omissions,
        anomalies=anomalies,
        typed_unidentified=[],
        ledger_row=ledger.finish(),
    )
