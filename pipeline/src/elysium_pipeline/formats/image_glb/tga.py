"""TGA (Truevision) decode: the 18-byte header, the image data, and the optional 2.0 footer.

Every field this module cannot resolve to a named record still owns a byte range: the developer
directory's tags are carried as `typedUnidentified` because their meaning belongs to the
authoring tool; a footer-referenced structure (extension area, developer directory, colour
correction table, postage stamp, scan-line table) whose offset a malformed file makes
unreachable or overlapping is recorded as `unresolved` rather than silently swept into
`omitted-proven`, so an incomplete unit is never mistaken for a complete one. Bytes no named
record ever addresses at all (padding between structures) still fall to the trailing-bytes sweep
in `coverage.claim_uncovered_gaps`.
"""

from __future__ import annotations

import hashlib
import struct
from typing import Any

from elysium_pipeline.formats.image_glb import ktx2
from elysium_pipeline.formats.image_glb.coverage import claim_uncovered_gaps, palette_usage
from elysium_pipeline.formats.image_glb.model import ImageModel
from elysium_pipeline.formats.unit_contract import ByteLedger, ByteLedgerError

FOOTER_SIGNATURE = b"TRUEVISION-XFILE.\x00"
FOOTER_LENGTH = 26
EXTENSION_AREA_LENGTH = 495


class TgaDecodeError(ValueError):
    """The selected member cannot be read as a TGA image."""


def _descriptor_bits(descriptor: int) -> tuple[int, bool, bool, int]:
    alpha_bits = descriptor & 0x0F
    origin_right = bool(descriptor & 0x10)
    origin_top = bool(descriptor & 0x20)
    interleave = (descriptor >> 6) & 0x03
    return alpha_bits, origin_right, origin_top, interleave


def _format_for(image_type: int, color_map_type: int, pixel_depth: int) -> tuple[int, bool]:
    """The KTX2 `vkFormat` for one TGA image-type/pixel-depth pair, and whether it is indexed."""

    if image_type in (1, 9):
        if color_map_type != 1 or pixel_depth != 8:
            raise TgaDecodeError(
                f"unsupported colour-mapped TGA: colorMapType={color_map_type} "
                f"pixelDepth={pixel_depth}"
            )
        return ktx2.VK_FORMAT_R8_UINT, True
    if image_type in (2, 10):
        if pixel_depth == 32:
            return ktx2.VK_FORMAT_B8G8R8A8_UNORM, False
        if pixel_depth == 24:
            return ktx2.VK_FORMAT_B8G8R8_UNORM, False
        if pixel_depth == 16:
            return ktx2.VK_FORMAT_A1R5G5B5_UNORM_PACK16, False
        raise TgaDecodeError(f"unsupported true-colour TGA pixel depth {pixel_depth}")
    if image_type in (3, 11):
        if pixel_depth == 8:
            return ktx2.VK_FORMAT_R8_UNORM, False
        raise TgaDecodeError(f"unsupported greyscale TGA pixel depth {pixel_depth}")
    raise TgaDecodeError(f"unsupported TGA image type {image_type}")


def _decode_rle(data: bytes, offset: int, pixel_size: int, pixel_count: int) -> tuple[bytes, int]:
    out = bytearray()
    index = offset
    needed = pixel_count * pixel_size
    while len(out) < needed:
        if index >= len(data):
            raise TgaDecodeError("RLE image data ends before the declared pixel count")
        header = data[index]
        index += 1
        count = (header & 0x7F) + 1
        if header & 0x80:
            pixel = data[index:index + pixel_size]
            if len(pixel) < pixel_size:
                raise TgaDecodeError("RLE packet pixel is truncated")
            index += pixel_size
            out.extend(pixel * count)
        else:
            span = pixel_size * count
            chunk = data[index:index + span]
            if len(chunk) < span:
                raise TgaDecodeError("RLE raw packet is truncated")
            index += span
            out.extend(chunk)
    if len(out) != needed:
        raise TgaDecodeError("RLE image data decodes to the wrong pixel count")
    return bytes(out[:needed]), index


def _reverse_pixels(row: bytes, pixel_size: int) -> bytes:
    return b"".join(
        row[offset:offset + pixel_size]
        for offset in range(len(row) - pixel_size, -1, -pixel_size)
    )


def _apply_orientation(
    data: bytes,
    width: int,
    height: int,
    pixel_size: int,
    rows_reversed: bool,
    columns_reversed: bool,
) -> bytes:
    row_size = width * pixel_size
    rows = [data[index:index + row_size] for index in range(0, len(data), row_size)]
    if rows_reversed:
        rows = rows[::-1]
    if columns_reversed:
        rows = [_reverse_pixels(row, pixel_size) for row in rows]
    return b"".join(rows)


def _palette_entry(depth: int, raw: bytes) -> dict[str, Any]:
    if depth == 32:
        blue, green, red, alpha = raw
        return {"blue": blue, "green": green, "red": red, "alpha": alpha}
    if depth == 24:
        blue, green, red = raw
        return {"blue": blue, "green": green, "red": red}
    if depth in (15, 16):
        value = int.from_bytes(raw, "little")
        entry = {
            "blue": value & 0x1F,
            "green": (value >> 5) & 0x1F,
            "red": (value >> 10) & 0x1F,
        }
        if depth == 16:
            entry["attribute"] = (value >> 15) & 0x1
        return entry
    if depth == 8:
        return {"intensity": raw[0]}
    raise TgaDecodeError(f"unsupported colour-map depth {depth}")


def _cstr(data: bytes, offset: int, length: int) -> str:
    raw = data[offset:offset + length]
    end = raw.find(b"\x00")
    if end >= 0:
        raw = raw[:end]
    return raw.decode("latin-1")


def _decode_color_correction_table(
    data: bytes, offset: int, ledger: ByteLedger, unresolved: list[dict[str, Any]]
) -> dict | None:
    length = 256 * 4 * 2
    if offset < 0 or offset + length > len(data):
        unresolved.append({
            "field": "extensionArea.colorCorrectionTable", "offset": offset, "length": length,
            "reason": "the colour-correction offset does not fit within the file",
        })
        return None
    try:
        ledger.claim(offset, length, "mapped", "tga.colorCorrectionTable")
    except ByteLedgerError as error:
        unresolved.append({
            "field": "extensionArea.colorCorrectionTable", "offset": offset, "length": length,
            "reason": f"the colour-correction table overlaps another claimed range: {error}",
        })
        return None
    values = struct.unpack_from(f"<{256 * 4}H", data, offset)
    return {
        "sourceOffset": offset,
        "entries": [list(values[index * 4:index * 4 + 4]) for index in range(256)],
    }


def _decode_postage_stamp(
    data: bytes, offset: int, pixel_size: int, ledger: ByteLedger, unresolved: list[dict[str, Any]]
) -> dict | None:
    if offset < 0 or offset + 2 > len(data):
        unresolved.append({
            "field": "extensionArea.postageStamp", "offset": offset,
            "reason": "the postage-stamp offset does not fit within the file",
        })
        return None
    width, height = data[offset], data[offset + 1]
    length = 2 + width * height * pixel_size
    if offset + length > len(data):
        unresolved.append({
            "field": "extensionArea.postageStamp", "offset": offset, "length": length,
            "reason": "the postage-stamp image runs past the end of the file",
        })
        return None
    try:
        ledger.claim(offset, length, "mapped", "tga.postageStamp")
    except ByteLedgerError as error:
        unresolved.append({
            "field": "extensionArea.postageStamp", "offset": offset, "length": length,
            "reason": f"the postage stamp overlaps another claimed range: {error}",
        })
        return None
    pixels = data[offset + 2:offset + length]
    return {
        "sourceOffset": offset,
        "width": width,
        "height": height,
        "byteLength": len(pixels),
        "sha256": hashlib.sha256(pixels).hexdigest(),
    }


def _decode_scan_line_table(
    data: bytes, offset: int, height: int, ledger: ByteLedger, unresolved: list[dict[str, Any]]
) -> dict | None:
    length = height * 4
    if offset < 0 or offset + length > len(data):
        unresolved.append({
            "field": "extensionArea.scanLineTable", "offset": offset, "length": length,
            "reason": "the scan-line table offset does not fit within the file",
        })
        return None
    try:
        ledger.claim(offset, length, "mapped", "tga.scanLineTable")
    except ByteLedgerError as error:
        unresolved.append({
            "field": "extensionArea.scanLineTable", "offset": offset, "length": length,
            "reason": f"the scan-line table overlaps another claimed range: {error}",
        })
        return None
    offsets = list(struct.unpack_from(f"<{height}I", data, offset))
    return {"sourceOffset": offset, "entries": len(offsets), "offsets": offsets}


def _decode_extension_area(
    data: bytes,
    offset: int,
    pixel_size: int,
    height: int,
    ledger: ByteLedger,
    unresolved: list[dict[str, Any]],
    anomalies: list[dict[str, Any]],
) -> dict | None:
    if offset < 0 or offset + EXTENSION_AREA_LENGTH > len(data):
        unresolved.append({
            "field": "footer.extensionArea", "offset": offset, "length": EXTENSION_AREA_LENGTH,
            "reason": "the extension-area offset does not fit within the file",
        })
        return None
    try:
        ledger.claim(offset, EXTENSION_AREA_LENGTH, "mapped", "tga.extensionArea")
    except ByteLedgerError as error:
        unresolved.append({
            "field": "footer.extensionArea", "offset": offset, "length": EXTENSION_AREA_LENGTH,
            "reason": f"the extension area overlaps another claimed range: {error}",
        })
        return None
    size = struct.unpack_from("<H", data, offset)[0]
    if size != EXTENSION_AREA_LENGTH:
        anomalies.append({
            "role": "extension-area-size-mismatch", "offset": offset,
            "declaredSize": size, "consumedSize": EXTENSION_AREA_LENGTH,
            "reason": "the extension area's own size field disagrees with the fixed 495 bytes read",
        })
    author_name = _cstr(data, offset + 2, 41)
    comments = [_cstr(data, offset + 43 + 81 * index, 81) for index in range(4)]
    date_time = struct.unpack_from("<6H", data, offset + 367)
    job_name = _cstr(data, offset + 379, 41)
    job_time = struct.unpack_from("<3H", data, offset + 420)
    software_id = _cstr(data, offset + 426, 41)
    version_number = struct.unpack_from("<H", data, offset + 467)[0]
    version_letter_byte = data[offset + 469]
    key_color = list(data[offset + 470:offset + 474])
    pixel_aspect = struct.unpack_from("<2H", data, offset + 474)
    gamma = struct.unpack_from("<2H", data, offset + 478)
    color_correction_offset = struct.unpack_from("<I", data, offset + 482)[0]
    postage_stamp_offset = struct.unpack_from("<I", data, offset + 486)[0]
    scan_line_offset = struct.unpack_from("<I", data, offset + 490)[0]
    attributes_type = data[offset + 494]

    area: dict[str, Any] = {
        "sourceOffset": offset,
        "size": size,
        "authorName": author_name,
        "authorComments": comments,
        "dateTimeStamp": {
            "month": date_time[0], "day": date_time[1], "year": date_time[2],
            "hour": date_time[3], "minute": date_time[4], "second": date_time[5],
        },
        "jobName": job_name,
        "jobTime": {"hours": job_time[0], "minutes": job_time[1], "seconds": job_time[2]},
        "softwareId": software_id,
        "softwareVersion": {
            "number": version_number / 100,
            "letter": chr(version_letter_byte) if version_letter_byte else "",
        },
        "keyColor": {"alpha": key_color[0], "red": key_color[1], "green": key_color[2],
                     "blue": key_color[3]},
        "pixelAspectRatio": {"numerator": pixel_aspect[0], "denominator": pixel_aspect[1]},
        "gamma": {"numerator": gamma[0], "denominator": gamma[1]},
        "attributesType": attributes_type,
        "colorCorrectionTableOffset": color_correction_offset,
        "postageStampOffset": postage_stamp_offset,
        "scanLineTableOffset": scan_line_offset,
        "colorCorrectionTable": None,
        "postageStamp": None,
        "scanLineTable": None,
    }
    if color_correction_offset:
        area["colorCorrectionTable"] = _decode_color_correction_table(
            data, color_correction_offset, ledger, unresolved
        )
    if postage_stamp_offset:
        area["postageStamp"] = _decode_postage_stamp(
            data, postage_stamp_offset, pixel_size, ledger, unresolved
        )
    if scan_line_offset:
        area["scanLineTable"] = _decode_scan_line_table(
            data, scan_line_offset, height, ledger, unresolved
        )
    return area


def _decode_developer_directory(
    data: bytes,
    offset: int,
    ledger: ByteLedger,
    typed_unidentified: list[dict[str, Any]],
    unresolved: list[dict[str, Any]],
) -> dict | None:
    if offset < 0 or offset + 2 > len(data):
        unresolved.append({
            "field": "footer.developerDirectory", "offset": offset,
            "reason": "the developer-directory offset does not fit within the file",
        })
        return None
    num_tags = struct.unpack_from("<H", data, offset)[0]
    table_length = 2 + num_tags * 10
    if offset + table_length > len(data):
        unresolved.append({
            "field": "footer.developerDirectory", "offset": offset, "length": table_length,
            "reason": "the developer-directory table runs past the end of the file",
        })
        return None
    try:
        ledger.claim(offset, table_length, "mapped", "tga.developerDirectory")
    except ByteLedgerError as error:
        unresolved.append({
            "field": "footer.developerDirectory", "offset": offset, "length": table_length,
            "reason": f"the developer-directory table overlaps another claimed range: {error}",
        })
        return None
    tags = []
    for index in range(num_tags):
        tag_offset = offset + 2 + index * 10
        tag_id, area_offset, area_size = struct.unpack_from("<HII", data, tag_offset)
        record: dict[str, Any] = {
            "tag": tag_id, "sourceOffset": tag_offset, "areaOffset": area_offset,
            "byteLength": area_size,
        }
        if area_size and 0 <= area_offset and area_offset + area_size <= len(data):
            try:
                ledger.claim(area_offset, area_size, "mapped", f"tga.developerArea[{index}]")
            except ByteLedgerError as error:
                unresolved.append({
                    "field": f"developerDirectory.tag[{index}].area", "offset": area_offset,
                    "length": area_size,
                    "reason": f"the developer area overlaps another claimed range: {error}",
                })
            else:
                record["sha256"] = hashlib.sha256(
                    data[area_offset:area_offset + area_size]
                ).hexdigest()
        elif area_size:
            unresolved.append({
                "field": f"developerDirectory.tag[{index}].area", "offset": area_offset,
                "length": area_size,
                "reason": "the developer area does not fit within the file",
            })
        typed_unidentified.append({
            "field": f"developerDirectory.tag[{index}]",
            "reason": "a developer-area tag's meaning belongs to the authoring tool",
            **record,
        })
        tags.append(record)
    return {"sourceOffset": offset, "tagCount": num_tags, "tags": tags}


def decode_tga(closure) -> ImageModel:
    member = closure.member
    data = member.data
    path = member.path
    if not data:
        raise TgaDecodeError(f"{path}: empty TGA member")
    if len(data) < 18:
        raise TgaDecodeError(f"{path}: TGA header is truncated ({len(data)} bytes)")

    ledger = ByteLedger(path, data)
    id_length = data[0]
    color_map_type = data[1]
    image_type = data[2]
    color_map_first, color_map_length = struct.unpack_from("<HH", data, 3)
    color_map_depth = data[7]
    x_origin, y_origin = struct.unpack_from("<HH", data, 8)
    width, height = struct.unpack_from("<HH", data, 12)
    pixel_depth = data[16]
    descriptor = data[17]
    alpha_bits, origin_right, origin_top, interleave = _descriptor_bits(descriptor)
    if interleave:
        # An interleaved storage order would silently misplace pixels: `_apply_orientation` only
        # ever reverses whole rows or columns, so publishing pixels as though they were laid out
        # sequentially would be a byte-for-byte lie rather than a lossy read. No corpus TGA sets
        # this, so failing the export costs nothing today and never risks a wrong picture later.
        raise TgaDecodeError(f"{path}: interleaved TGA storage is not supported (interleave={interleave})")
    ledger.claim(0, 18, "mapped", "tga.header")
    cursor = 18

    image_id_bytes = data[cursor:cursor + id_length]
    if len(image_id_bytes) != id_length:
        raise TgaDecodeError(f"{path}: image ID field is truncated")
    if id_length:
        ledger.claim(cursor, id_length, "mapped-string", "tga.imageId")
    cursor += id_length

    if color_map_type not in (0, 1):
        raise TgaDecodeError(f"{path}: unsupported colour-map type {color_map_type}")
    color_map_entry_bytes = 0
    color_map_data = b""
    if color_map_type == 1:
        if color_map_depth not in (8, 15, 16, 24, 32):
            raise TgaDecodeError(f"{path}: unsupported colour-map depth {color_map_depth}")
        color_map_entry_bytes = (color_map_depth + 7) // 8
        color_map_bytes_len = color_map_length * color_map_entry_bytes
        color_map_data = data[cursor:cursor + color_map_bytes_len]
        if len(color_map_data) != color_map_bytes_len:
            raise TgaDecodeError(f"{path}: colour map is truncated")
        if color_map_bytes_len:
            ledger.claim(cursor, color_map_bytes_len, "mapped", "tga.colorMap")
        cursor += color_map_bytes_len

    vk_format, is_index = _format_for(image_type, color_map_type, pixel_depth)
    pixel_size = ktx2.bytes_per_pixel(vk_format)
    if not width or not height:
        raise TgaDecodeError(f"{path}: image extent {width}x{height} carries no pixels")
    pixel_count = width * height
    image_start = cursor
    if image_type in (9, 10, 11):
        pixels, consumed_to = _decode_rle(data, cursor, pixel_size, pixel_count)
        ledger.claim(image_start, consumed_to - image_start, "derived", "tga.image")
        cursor = consumed_to
    else:
        needed = pixel_count * pixel_size
        pixels = data[cursor:cursor + needed]
        if len(pixels) != needed:
            raise TgaDecodeError(f"{path}: uncompressed image data is truncated")
        ledger.claim(cursor, needed, "mapped", "tga.image")
        cursor += needed

    rows_reversed = not origin_top
    columns_reversed = origin_right
    oriented = _apply_orientation(pixels, width, height, pixel_size, rows_reversed, columns_reversed)
    source_origin = ("top" if origin_top else "bottom") + "-" + ("right" if origin_right else "left")

    omissions: list[dict[str, Any]] = []
    anomalies: list[dict[str, Any]] = []
    typed_unidentified: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    footer: dict[str, Any] | None = None

    if len(data) >= FOOTER_LENGTH:
        footer_offset = len(data) - FOOTER_LENGTH
        if footer_offset >= cursor and data[footer_offset + 8:] == FOOTER_SIGNATURE:
            extension_area_offset, developer_directory_offset = struct.unpack_from(
                "<II", data, footer_offset
            )
            extension_area = None
            developer_directory = None
            if extension_area_offset:
                extension_area = _decode_extension_area(
                    data, extension_area_offset, pixel_size, height, ledger, unresolved, anomalies
                )
            if developer_directory_offset:
                developer_directory = _decode_developer_directory(
                    data, developer_directory_offset, ledger, typed_unidentified, unresolved
                )
            try:
                ledger.claim(footer_offset, FOOTER_LENGTH, "mapped", "tga.footer")
            except ByteLedgerError as error:
                # The footer's own 26 bytes are already accounted for under whichever structure
                # its own offsets pointed into (e.g. an extension area declared to run past end
                # of file) -- the ledger stays gapless, and the footer is still published with
                # everything decoded from it, but the overlap itself is recorded rather than
                # raised, matching every other footer-referenced structure's own treatment.
                unresolved.append({
                    "field": "footer", "offset": footer_offset, "length": FOOTER_LENGTH,
                    "reason": f"the footer overlaps another claimed range: {error}",
                })
            footer = {
                "sourceOffset": footer_offset,
                "extensionAreaOffset": extension_area_offset,
                "developerDirectoryOffset": developer_directory_offset,
                "extensionArea": extension_area,
                "developerDirectory": developer_directory,
            }

    trailing, trailing_ranges = claim_uncovered_gaps(ledger, "tga.trailing")
    if trailing:
        omissions.append({
            "role": "trailing-bytes",
            "reason": "bytes between decoded structures that no record claims",
            "byteLength": trailing,
            "ranges": trailing_ranges,
        })

    palette = None
    if color_map_type == 1:
        # A colour map is published whenever the source stores one, independently of whether
        # this image type actually indexes it (`is_index`) -- a true-colour or greyscale TGA can
        # still carry `colorMapType=1` bytes on disk, and dropping them after claiming them
        # `mapped` would publish 100% byte accountability over data that reaches neither the
        # extension nor the payload.
        colors = [
            _palette_entry(color_map_depth, color_map_data[index:index + color_map_entry_bytes])
            for index in range(0, len(color_map_data), color_map_entry_bytes)
        ]
        if is_index:
            max_index = max(oriented) if oriented else 0
            omission, anomaly = palette_usage(max_index, color_map_first, len(colors))
            if omission is not None:
                omissions.append(omission)
            if anomaly is not None:
                anomalies.append(anomaly)
        palette = {
            "entries": len(colors),
            "bitsPerEntry": color_map_depth,
            "firstIndex": color_map_first,
            "colors": colors,
        }

    source_format = {
        "idLength": id_length,
        "colorMapType": color_map_type,
        "imageType": image_type,
        "colorMapFirst": color_map_first,
        "colorMapLength": color_map_length,
        "colorMapDepth": color_map_depth,
        "xOrigin": x_origin,
        "yOrigin": y_origin,
        "width": width,
        "height": height,
        "pixelDepth": pixel_depth,
        "descriptor": descriptor,
        "alphaBits": alpha_bits,
        "originRight": origin_right,
        "originTop": origin_top,
        "interleave": interleave,
        "imageId": {"bytes": list(image_id_bytes), "text": image_id_bytes.decode("latin-1")},
    }
    orientation = {
        "sourceOrigin": source_origin,
        "rowsReversed": rows_reversed,
        "columnsReversed": columns_reversed,
    }

    return ImageModel(
        image_path=closure.image_path,
        asset_id=closure.asset_id,
        container="tga",
        member=member,
        width=width,
        height=height,
        bits_per_pixel=pixel_depth,
        vk_format_name=ktx2.vk_format_name(vk_format),
        vk_format_value=vk_format,
        pixel_data=oriented,
        source_format=source_format,
        palette=palette,
        footer=footer,
        orientation=orientation,
        omissions=omissions,
        anomalies=anomalies,
        typed_unidentified=typed_unidentified,
        unresolved=unresolved,
        ledger_row=ledger.finish(),
    )
