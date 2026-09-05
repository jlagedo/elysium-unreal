"""Complete TTH/TTZ decode into the Texture GLB semantic model."""

from __future__ import annotations

import hashlib
import struct
import zlib

from elysium_pipeline.formats.texture_glb.coverage import ByteLedger
from elysium_pipeline.formats.texture_glb.model import FormatInfo, TextureLevel, TextureModel


class TextureDecodeError(ValueError):
    pass


FMT_RGBA8888 = 0
FMT_BGR888 = 3
FMT_BGRA8888 = 12
FMT_DXT1 = 13
FMT_DXT3 = 14
FMT_DXT5 = 15
FMT_UVWQ8888 = 23

FORMATS = {
    FMT_RGBA8888: FormatInfo(0, "RGBA8888", 37, "VK_FORMAT_R8G8B8A8_UNORM", 1, 1, 4,
                             ("R", "G", "B", "A"), False),
    FMT_BGR888: FormatInfo(3, "BGR888", 23, "VK_FORMAT_R8G8B8_UNORM", 1, 1, 3,
                          ("R", "G", "B"), False),
    FMT_BGRA8888: FormatInfo(12, "BGRA8888", 44, "VK_FORMAT_B8G8R8A8_UNORM", 1, 1, 4,
                            ("B", "G", "R", "A"), False),
    FMT_DXT1: FormatInfo(13, "DXT1", 133, "VK_FORMAT_BC1_RGBA_UNORM_BLOCK", 4, 4, 8,
                        ("COLOR", "ALPHA"), True),
    FMT_DXT3: FormatInfo(14, "DXT3", 135, "VK_FORMAT_BC2_UNORM_BLOCK", 4, 4, 16,
                        ("ALPHA", "COLOR"), True),
    FMT_DXT5: FormatInfo(15, "DXT5", 137, "VK_FORMAT_BC3_UNORM_BLOCK", 4, 4, 16,
                        ("ALPHA", "COLOR"), True),
    FMT_UVWQ8888: FormatInfo(23, "UVWQ8888", 41, "VK_FORMAT_R8G8B8A8_UINT", 1, 1, 4,
                            ("U", "V", "W", "Q"), False),
}

ENVMAP = 0x00004000
SOURCE_TO_GLTF_FACE_ORDER = (0, 1, 4, 5, 3, 2)
SOURCE_TO_GLTF_FACE_TRANSFORMS = ("rotate-cw", "rotate-ccw", "identity", "rotate-180", "identity", "rotate-180")
FACE_NAMES = ("+X", "-X", "+Y", "-Y", "+Z", "-Z")


def image_size(width: int, height: int, info: FormatInfo) -> int:
    blocks_x = max(1, (width + info.block_width - 1) // info.block_width)
    blocks_y = max(1, (height + info.block_height - 1) // info.block_height)
    return blocks_x * blocks_y * info.block_bytes


def _normalize_image(data: bytes, info: FormatInfo) -> bytes:
    if info.source_enum != FMT_BGR888:
        return data
    result = bytearray(len(data))
    for offset in range(0, len(data), 3):
        result[offset:offset + 3] = data[offset:offset + 3][::-1]
    return bytes(result)


def _rotate_grid(values, width: int, operation: str):
    if operation == "identity":
        return list(values)
    if operation == "rotate-cw":
        return [values[(width - 1 - x) * width + y] for y in range(width) for x in range(width)]
    if operation == "rotate-ccw":
        return [values[x * width + (width - 1 - y)] for y in range(width) for x in range(width)]
    if operation == "rotate-180":
        return [values[(width - 1 - y) * width + (width - 1 - x)] for y in range(width) for x in range(width)]
    raise TextureDecodeError(f"unknown cubemap transform {operation}")


def _rotate_bits(data: bytes, bits_per_value: int, operation: str) -> bytes:
    packed = int.from_bytes(data, "little")
    mask = (1 << bits_per_value) - 1
    values = [(packed >> (bits_per_value * index)) & mask for index in range(16)]
    rotated = _rotate_grid(values, 4, operation)
    output = sum(value << (bits_per_value * index) for index, value in enumerate(rotated))
    return output.to_bytes(len(data), "little")


def _rotate_bc_block(block: bytes, source_format: int, operation: str) -> bytes:
    if operation == "identity":
        return block
    if source_format == FMT_DXT1:
        return block[:4] + _rotate_bits(block[4:8], 2, operation)
    if source_format == FMT_DXT3:
        return _rotate_bits(block[:8], 4, operation) + block[8:12] + _rotate_bits(
            block[12:16], 2, operation
        )
    if source_format == FMT_DXT5:
        return block[:2] + _rotate_bits(block[2:8], 3, operation) + block[8:12] + _rotate_bits(
            block[12:16], 2, operation
        )
    raise TextureDecodeError(f"format {source_format} is not BC-compressed")


def transform_cubemap_face(
    data: bytes, width: int, height: int, info: FormatInfo, operation: str
) -> bytes:
    if operation == "identity":
        return data
    if width != height:
        raise TextureDecodeError(f"cubemap face is not square: {width}x{height}")
    if info.compressed:
        blocks = max(1, (width + info.block_width - 1) // info.block_width)
        source = [
            data[offset:offset + info.block_bytes]
            for offset in range(0, len(data), info.block_bytes)
        ]
        rotated_blocks = _rotate_grid(source, blocks, operation)
        return b"".join(
            _rotate_bc_block(block, info.source_enum, operation) for block in rotated_blocks
        )
    pixels = [
        data[offset:offset + info.block_bytes]
        for offset in range(0, len(data), info.block_bytes)
    ]
    return b"".join(_rotate_grid(pixels, width, operation))


def _sampling(flags: int) -> dict[str, bool]:
    return {
        "pointSample": bool(flags & 0x00000001),
        "trilinear": bool(flags & 0x00000002),
        "clampS": bool(flags & 0x00000004),
        "clampT": bool(flags & 0x00000008),
        "anisotropic": bool(flags & 0x00000010),
        "noMip": bool(flags & 0x00000100),
        "noLod": bool(flags & 0x00000200),
        "allMips": bool(flags & 0x00000400),
    }


def _inflate_with_recovery(data: bytes) -> tuple[bytes, bool]:
    """Inflate a retail stream, retaining decoded bytes when only its trailer is corrupt."""
    try:
        return zlib.decompress(data), True
    except zlib.error:
        decoder = zlib.decompressobj()
        output = bytearray()
        for value in data:
            try:
                output.extend(decoder.decompress(bytes((value,))))
            except zlib.error:
                return bytes(output), False
        try:
            output.extend(decoder.flush())
        except zlib.error:
            return bytes(output), False
        return bytes(output), bool(decoder.eof)


def _decode_sprite(closure) -> TextureModel:
    """R7.3: one raw `particles/<stem>.tga` member as a one-level BGRA8 texture unit
    (`seam_map_texture.md` -> "Texture unit" -> "Particle sprites").

    The pixels are the image seam's own TGA decode (`formats.image_glb.tga.decode_tga`: header,
    optional colour map, uncompressed or RLE image, orientation put top-down), widened from
    24-bit BGR to BGRA with an opaque alpha where the file stores no alpha -- the corpus is 279
    BGRA8 uncompressed, 36 BGR8 uncompressed, 3 BGRA8 RLE. No VTF header exists, so the
    `sourceFormat` block states the TGA's own fields under `tga` and reports the pixel format as
    `BGRA8888` (VTF enum 12, the format the payload actually carries); the sampling flags are all
    off (Source's particle atlas sampled with the defaults); there is one mip level.
    """

    from types import SimpleNamespace

    from elysium_pipeline.formats.image_glb import ktx2 as image_ktx2
    from elysium_pipeline.formats.image_glb.tga import TgaDecodeError, decode_tga

    member = closure.tth
    try:
        image = decode_tga(SimpleNamespace(
            member=SimpleNamespace(path=member.path, data=member.data),
            image_path=member.path, asset_id=closure.asset_id, container="tga"))
    except TgaDecodeError as error:
        raise TextureDecodeError(str(error)) from error
    pixels = image.pixel_data
    if image.vk_format_value == image_ktx2.VK_FORMAT_B8G8R8_UNORM:
        widened = bytearray(len(pixels) // 3 * 4)
        widened[0::4] = pixels[0::3]
        widened[1::4] = pixels[1::3]
        widened[2::4] = pixels[2::3]
        widened[3::4] = b"\xff" * (len(pixels) // 3)
        pixels = bytes(widened)
    elif image.vk_format_value != image_ktx2.VK_FORMAT_B8G8R8A8_UNORM:
        raise TextureDecodeError(
            f"{member.path}: TGA pixel format {image.vk_format_name} is not admitted as a texture "
            f"unit (BGRA8 and BGR8 only)")
    info = FORMATS[FMT_BGRA8888]
    width, height = image.width, image.height

    # The ledger: the image seam claims the same spans under its own owners; restated here under
    # this seam's states so the unit's byte accountability is checked by this seam's validator.
    data = member.data
    ledger = ByteLedger(member.path, data)
    ledger.claim(0, 18, "mapped", "tga.header")
    cursor = 18
    id_length = data[0]
    if id_length:
        ledger.claim(cursor, id_length, "mapped", "tga.imageId")
        cursor += id_length
    source = image.source_format
    if int(source.get("colorMapType") or 0) == 1:
        map_bytes = int(source.get("colorMapLength") or 0) * ((int(source.get("colorMapDepth") or 0) + 7) // 8)
        if map_bytes:
            ledger.claim(cursor, map_bytes, "mapped", "tga.colorMap")
            cursor += map_bytes
    pixel_size = int(source.get("pixelDepth") or 32) // 8
    if int(source.get("imageType") or 2) in (9, 10, 11):
        from elysium_pipeline.formats.image_glb.tga import _decode_rle

        _pixels, consumed_to = _decode_rle(data, cursor, pixel_size, width * height)
        ledger.claim(cursor, consumed_to - cursor, "derived", "tga.image")
        cursor = consumed_to
    else:
        ledger.claim(cursor, width * height * pixel_size, "mapped", "tga.image")
        cursor += width * height * pixel_size
    if cursor < len(data):
        ledger.claim(cursor, len(data) - cursor, "omitted-proven", "tga.trailing")

    identities = [m.identity() for m in closure.members()]
    return TextureModel(
        texture_path=closure.texture_path,
        asset_id=closure.asset_id,
        sources=identities,
        format=info,
        width=width,
        height=height,
        declared_width=width,
        declared_height=height,
        frames=1,
        cubemap=False,
        mip_count=1,
        levels=[TextureLevel(0, 0, width, height, (pixels,))],
        header={
            "tthVersion": 0,
            "vtfVersion": "",
            "flags": 0,
            "startFrame": 0,
            "reflectivity": [0.0, 0.0, 0.0],
            "bumpScale": 0.0,
            "declaredInlineMips": 1,
            "resolvedInlineMips": 1,
            "sourceFormat": info.source_name,
            "sourceFormatEnum": info.source_enum,
            "vtfMipCount": 1,
            "sourceWidth": width,
            "sourceHeight": height,
            "sourceMipCount": 1,
            "tthMipTableCount": 1,
            "zlibStreamComplete": True,
            "sourceContainer": "tga",
            "tga": dict(source),
            "tgaOrientation": dict(image.orientation),
        },
        sampling=_sampling(0),
        faces=[],
        omissions=[],
        byte_coverage=[ledger.finish()],
    )


def decode_texture(closure) -> TextureModel:
    if getattr(closure.tth, "role", "") == "tga":
        return _decode_sprite(closure)
    tth = closure.tth.data
    if len(tth) < 20 or tth[:4] != b"TTH\0":
        raise TextureDecodeError(f"{closure.tth.path}: invalid TTH header")
    outer_version, mip_count, declared_inline, vtf_blob_length = struct.unpack_from(
        "<HBBI", tth, 4
    )
    if outer_version != 1 or mip_count < 1:
        raise TextureDecodeError(
            f"{closure.tth.path}: unsupported TTH version/mips {outer_version}/{mip_count}"
        )
    table_end = 12 + (mip_count + 1) * 8
    if table_end + 64 > len(tth) or tth[table_end:table_end + 4] != b"VTF\0":
        raise TextureDecodeError(f"{closure.tth.path}: invalid mip table or VTF offset")
    meaningful_end = table_end + vtf_blob_length
    if vtf_blob_length < 64 or meaningful_end > len(tth):
        raise TextureDecodeError(
            f"{closure.tth.path}: VTF blob range {table_end}+{vtf_blob_length} "
            f"exceeds {len(tth)}"
        )
    table = [struct.unpack_from("<II", tth, 12 + index * 8) for index in range(mip_count + 1)]
    vtf = table_end
    major, minor, header_size = struct.unpack_from("<III", tth, vtf + 4)
    if major != 7 or minor not in (0, 1) or header_size != 64:
        raise TextureDecodeError(
            f"{closure.tth.path}: unsupported VTF {major}.{minor} header {header_size}"
        )
    width, height, flags, frames, start_frame = struct.unpack_from("<HHIHH", tth, vtf + 16)
    reflectivity = struct.unpack_from("<3f", tth, vtf + 32)
    bump_scale = struct.unpack_from("<f", tth, vtf + 48)[0]
    source_format = struct.unpack_from("<I", tth, vtf + 52)[0]
    vtf_mips = tth[vtf + 56]
    low_format = struct.unpack_from("<I", tth, vtf + 57)[0]
    low_width, low_height = tth[vtf + 61], tth[vtf + 62]
    if not width or not height or not frames or not vtf_mips:
        raise TextureDecodeError(
            f"{closure.tth.path}: invalid dimensions/frames/mips "
            f"{width}x{height}/{frames}/{vtf_mips}/{mip_count}"
        )
    info = FORMATS.get(source_format)
    if info is None:
        raise TextureDecodeError(f"{closure.tth.path}: unsupported image format {source_format}")
    declared_low_size = 0
    if low_width or low_height:
        if low_format not in FORMATS:
            raise TextureDecodeError(
                f"{closure.tth.path}: unsupported low-resolution image "
                f"{low_width}x{low_height} format {low_format}"
            )
        if low_width and low_height:
            declared_low_size = image_size(low_width, low_height, FORMATS[low_format])
    cubemap = bool(flags & ENVMAP)
    source_faces = 7 if cubemap else 1
    natural_mip_count = max(width, height).bit_length()

    def _mip_chain(levels: int) -> list[tuple[int, int, int, int]]:
        """`(width, height, bytes per image, bytes per level)` smallest level first, as VTF stores them."""

        chain = []
        for source_mip in range(levels):
            shift = levels - source_mip - 1
            mip_width, mip_height = max(1, width >> shift), max(1, height >> shift)
            per_image = image_size(mip_width, mip_height, info)
            chain.append((mip_width, mip_height, per_image, per_image * frames * source_faces))
        return chain

    image_mip_count = min(mip_count, natural_mip_count)
    source_level_sizes = _mip_chain(image_mip_count)
    # G5b: the outer TTH mip table under-declares its own chain on every compiler-written
    # reflection probe -- `mip_count` is 1 while the VTF header inside it says 6, 7 or 8 and the
    # blob carries every one of those levels inline. Read against the outer table alone the
    # levels below the top one fall ahead of `image_start` and are claimed as the header's CPU
    # colour sample, so a probe imports with a single mip and no roughness chain (measured on
    # both `sm_pier_1` probes; `maps/sm_pier_1/c-1241_22_4950` loses exactly the 114,681 bytes
    # of its seven lower levels). The VTF header's count wins where the blob is exactly the
    # pyramid that count describes -- an arithmetic proof, not a guess; a leading blob that is
    # not a whole chain still reads as the CPU sample below.
    vtf_chain_count = min(vtf_mips, natural_mip_count)
    if vtf_chain_count > image_mip_count:
        candidate = _mip_chain(vtf_chain_count)
        if vtf_blob_length - header_size == sum(row[3] for row in candidate):
            image_mip_count, source_level_sizes = vtf_chain_count, candidate
    expected_total = sum(row[3] for row in source_level_sizes)
    # The header's own claim survives into `sourceFormat.declaredInlineMips` even where it is
    # stale, so the unit restates what the source wrote; the clamped count is what the following
    # extent arithmetic can actually index.
    admitted_inline = min(declared_inline, image_mip_count)
    declared_inline_bytes = sum(row[3] for row in source_level_sizes[:admitted_inline])
    meaningful_trailing = vtf_blob_length - header_size
    has_external_file = closure.ttz is not None and bool(closure.ttz.data)
    if not has_external_file and meaningful_trailing >= expected_total:
        actual_inline = image_mip_count
    else:
        actual_inline = admitted_inline
        while actual_inline and sum(
            row[3] for row in source_level_sizes[:actual_inline]
        ) > meaningful_trailing:
            actual_inline -= 1
    actual_inline_bytes = sum(row[3] for row in source_level_sizes[:actual_inline])
    auxiliary_size = meaningful_trailing - actual_inline_bytes
    if auxiliary_size < 0:
        raise TextureDecodeError(
            f"{closure.tth.path}: inline image bytes exceed the meaningful VTF blob"
        )
    image_start = vtf + header_size + auxiliary_size
    trailing = tth[image_start:meaningful_end]
    if len(trailing) != actual_inline_bytes:
        raise TextureDecodeError(f"{closure.tth.path}: inline image range is inconsistent")

    compressed = b""
    compressed_complete = True
    ttz_meaningful_length = 0
    if closure.ttz is not None:
        ttz_meaningful_length = min(
            table[-1][1] or len(closure.ttz.data), len(closure.ttz.data)
        )
        ttz_fill = closure.ttz.data[ttz_meaningful_length:]
        if ttz_meaningful_length:
            compressed, compressed_complete = _inflate_with_recovery(
                closure.ttz.data[:ttz_meaningful_length]
            )
    expected_external = sum(row[3] for row in source_level_sizes[actual_inline:])
    smallest_external = (
        source_level_sizes[actual_inline][3] if actual_inline < image_mip_count else 0
    )

    def _is_low_res_blob(size: int, floor: int) -> bool:
        """Whether a blob ahead of the image stream is the header's CPU colour sample.

        A leading blob is the low-resolution image, so it is either no larger than the size the
        header declares for it or too small to be a level at all. Anything larger is image storage
        the admitted chain does not explain. ``floor`` is the smallest level the test measures
        against: the smallest external level while the chain is still being chosen, and the
        smallest emitted level once it has been, which is the same quantity a published unit
        states and therefore the one an independent reader can re-check.
        """
        return size <= declared_low_size or size < floor

    selected_start = 0
    selected_end = image_mip_count
    incomplete_mips = []
    unexplained_stream = False
    admitted_declared_inline_chain = False
    if len(compressed) < expected_external:
        tail_size = 0
        tail_start = image_mip_count
        for source_mip in reversed(range(actual_inline, image_mip_count)):
            size = source_level_sizes[source_mip][3]
            if tail_size + size > len(compressed):
                break
            tail_size += size
            tail_start = source_mip
        if tail_start < image_mip_count:
            selected_start, selected_end = tail_start, image_mip_count
            external_auxiliary = len(compressed) - tail_size
            stream = compressed[external_auxiliary:]
            incomplete_mips = list(range(tail_start))
        elif actual_inline:
            selected_start, selected_end = 0, actual_inline
            external_auxiliary = len(compressed)
            stream = trailing
            incomplete_mips = list(range(actual_inline, image_mip_count))
        else:
            source = closure.ttz.path if closure.ttz else f"materials/{closure.texture_path}.ttz"
            raise TextureDecodeError(f"{source}: no complete primary mip survives")
    elif not _is_low_res_blob(len(compressed) - expected_external, smallest_external):
        # The stream carries more than the declared chain plus a colour sample, so the chain does
        # not describe it. Retail units of this shape store repeated full-resolution images and no
        # pyramid, and sliding the chain onto the tail would compose every level below the first
        # out of unrelated bytes. Admit the full-resolution image alone.
        top = source_level_sizes[-1][3]
        if actual_inline >= image_mip_count and not compressed_complete:
            # The inline blob alone would already complete the chain, but the stream that makes
            # it superfluous is itself truncated (`_inflate_with_recovery` returned a partial
            # buffer). A partial stream cannot be proven redundant -- it might be a corrupt
            # *primary* stream this shape coincidentally resembles -- so this is not the silent
            # inline-chain shortcut below; raise like every other unexplained-shape failure.
            raise TextureDecodeError(
                f"{closure.texture_path}: a superfluous external stream of "
                f"{len(compressed)} bytes is truncated and cannot be proven redundant"
            )
        elif actual_inline >= image_mip_count:
            # The admitted chain is already complete from the inline blob alone -- the shape
            # SF-1.2 found in 2 of the shipped reflection probes, whose pyramid fits wholly
            # inline (`image_mip_count`, the outer TTH table's own mip count, not `vtf_mips`)
            # yet still ships a `.ttz`. There is no level left for the external stream to
            # supply, so none of it is selected; it is unexplained storage in full below, kept
            # distinct from the single-full-resolution-image shape so a reader can tell a
            # multi-level admitted chain from a one-level one.
            selected_start, selected_end = 0, actual_inline
            external_auxiliary = len(compressed)
            stream = trailing
            unexplained_stream = True
            admitted_declared_inline_chain = True
        elif len(compressed) < top:
            raise TextureDecodeError(
                f"{closure.texture_path}: a TTZ stream of {len(compressed)} bytes matches neither "
                f"the declared mip chain nor one full-resolution image"
            )
        else:
            selected_start, selected_end = image_mip_count - 1, image_mip_count
            external_auxiliary = len(compressed) - top
            stream = compressed[external_auxiliary:]
            unexplained_stream = True
    else:
        external_auxiliary = len(compressed) - expected_external
        primary_external = compressed[external_auxiliary:]
        stream = trailing + primary_external
    selected_sizes = source_level_sizes[selected_start:selected_end]
    if len(stream) != sum(row[3] for row in selected_sizes):
        raise TextureDecodeError(f"{closure.texture_path}: incomplete reconstructed image stream")
    unexplained_external = 0
    if external_auxiliary and (
        unexplained_stream
        or not _is_low_res_blob(external_auxiliary, min(row[3] for row in selected_sizes))
    ):
        unexplained_external, external_auxiliary = external_auxiliary, 0

    tth_ledger = ByteLedger(closure.tth.path, tth)
    tth_ledger.claim(0, 12, "mapped", "tth.header")
    tth_ledger.claim(12, table_end - 12, "mapped", "tth.mip-table")
    tth_ledger.claim(vtf, header_size, "mapped", "vtf.header")
    if auxiliary_size:
        tth_ledger.claim(
            image_start - auxiliary_size,
            auxiliary_size,
            "omitted-proven",
            "vtf.low-res-cpu-sample",
        )

    tth_cursor = image_start
    for source_mip, (_mip_width, _mip_height, per_image, level_size) in enumerate(
        source_level_sizes[:actual_inline]
    ):
        for frame in range(frames):
            for face in range(source_faces):
                at = (frame * source_faces + face) * per_image
                omitted = not (selected_start <= source_mip < selected_end) or (
                    cubemap and face == 6
                )
                state = "omitted-proven" if omitted else "mapped"
                owner = (
                    f"vtf.mip[{source_mip}].frame[{frame}].omitted-image[{face}]"
                    if omitted
                    else f"vtf.mip[{source_mip}].frame[{frame}].face[{face}]"
                )
                tth_ledger.claim(tth_cursor + at, per_image, state, owner)
        tth_cursor += level_size
    if tth_cursor != meaningful_end:
        raise TextureDecodeError(
            f"{closure.tth.path}: TTH image walk ends at {tth_cursor}/{meaningful_end}"
        )

    source_levels = []
    cursor = 0
    for relative_mip, (mip_width, mip_height, per_image, level_size) in enumerate(selected_sizes):
        source_mip = selected_start + relative_mip
        level_data = stream[cursor:cursor + level_size]
        cursor += level_size
        images = []
        for frame in range(frames):
            source_images = []
            for face in range(source_faces):
                at = (frame * source_faces + face) * per_image
                image = level_data[at:at + per_image]
                source_images.append(image)
            if cubemap:
                images.extend(
                    transform_cubemap_face(
                        source_images[source_face],
                        mip_width,
                        mip_height,
                        info,
                        SOURCE_TO_GLTF_FACE_TRANSFORMS[target_face],
                    )
                    for target_face, source_face in enumerate(SOURCE_TO_GLTF_FACE_ORDER)
                )
            else:
                images.append(source_images[0])
        source_levels.append((source_mip, mip_width, mip_height, tuple(images)))
    omitted_spheremap_bytes = (
        sum(row[2] * frames for row in source_level_sizes) if cubemap else 0
    )
    compiler_fill = tth[meaningful_end:]
    if compiler_fill:
        state = "padding-zero" if not any(compiler_fill) else "omitted-proven"
        tth_ledger.claim(meaningful_end, len(compiler_fill), state, "tth.trailing-compiler-fill")

    ledgers = [tth_ledger.finish()]
    if closure.ttz is not None:
        ttz_ledger = ByteLedger(closure.ttz.path, closure.ttz.data)
        prefixes = [entry[1] for entry in table]
        bounds = [prefixes[index] if index < len(prefixes) else 0
                  for index in range(actual_inline, image_mip_count + 1)]
        if bounds and not bounds[-1]:
            bounds[-1] = ttz_meaningful_length
        use_mip_spans = (
            ttz_meaningful_length
            and len(bounds) >= 2
            and bounds[0] == 0
            and bounds[-1] == ttz_meaningful_length
            and all(bounds[index] < bounds[index + 1] for index in range(len(bounds) - 1))
        )
        if use_mip_spans:
            for relative, source_mip in enumerate(range(actual_inline, image_mip_count)):
                start, end = bounds[relative], bounds[relative + 1]
                omitted = not (selected_start <= source_mip < selected_end)
                ttz_ledger.claim(
                    start,
                    end - start,
                    "omitted-proven" if omitted else "derived",
                    (
                        f"ttz.mip[{source_mip}].omitted-zlib"
                        if omitted
                        else f"ttz.mip[{source_mip}].zlib"
                    ),
                )
        elif ttz_meaningful_length and admitted_declared_inline_chain:
            # The declared chain is already complete from the inline blob alone, so no emitted
            # level draws a byte from this stream: it is proven-redundant storage, not something
            # `derived` represents. Its evidence is the `unexplained-leading-image-storage`
            # omission row this shape always publishes.
            ttz_ledger.claim(
                0,
                ttz_meaningful_length,
                "omitted-proven",
                "ttz.superfluous-zlib-stream (see omissions.unexplained-leading-image-storage)",
            )
        elif ttz_meaningful_length:
            ttz_ledger.claim(0, ttz_meaningful_length, "derived", "zlib-primary-mip-stream")
        ttz_fill = closure.ttz.data[ttz_meaningful_length:]
        if ttz_fill:
            state = "padding-zero" if not any(ttz_fill) else "omitted-proven"
            ttz_ledger.claim(
                ttz_meaningful_length, len(ttz_fill), state, "ttz.trailing-compiler-fill"
            )
        ledgers.append(ttz_ledger.finish())

    levels = []
    for ktx_level, (source_mip, mip_width, mip_height, images) in enumerate(reversed(source_levels)):
        levels.append(
            TextureLevel(
                ktx_level,
                source_mip,
                mip_width,
                mip_height,
                tuple(_normalize_image(image, info) for image in images),
            )
        )
    omissions = []
    if auxiliary_size or external_auxiliary:
        omissions.append({
            "role": "low-res-cpu-sample",
            "reason": "compiler-generated Source CPU colour-sampling optimization",
            "byteLength": auxiliary_size,
            "declaredByteLength": declared_low_size,
            "externalByteLength": external_auxiliary,
            "format": FORMATS[low_format].source_name if low_format in FORMATS else "none",
            "width": low_width,
            "height": low_height,
        })
    if unexplained_external:
        top = source_level_sizes[-1][3]
        row = {
            "role": "unexplained-leading-image-storage",
            "reason": "source image storage the declared mip chain does not account for",
            "byteLength": unexplained_external,
            "sourceMips": list(range(actual_inline)) if admitted_declared_inline_chain else [],
            "streamFullResolutionImages": round(len(compressed) / top, 4),
            "admittedFullResolutionImageOnly": (
                unexplained_stream and not admitted_declared_inline_chain
            ),
        }
        if admitted_declared_inline_chain:
            row["admittedDeclaredInlineChain"] = True
            row["admittedLevels"] = actual_inline
        omissions.append(row)
    if levels[0].width != width or levels[0].height != height:
        omissions.append({
            "role": "primary-image-not-recoverable",
            "reason": "no complete image at the declared resolution survives in the selected source",
            "declaredWidth": width,
            "declaredHeight": height,
            "recoveredWidth": levels[0].width,
            "recoveredHeight": levels[0].height,
            "zlibStreamComplete": compressed_complete,
        })
    if incomplete_mips:
        omissions.append({
            "role": "incomplete-lower-mips",
            "reason": "retail source does not contain a complete contiguous KTX2 mip level",
            "sourceMips": incomplete_mips,
            "representedOnlyByLedger": True,
            "zlibStreamComplete": compressed_complete,
        })
    if cubemap:
        omissions.append({
            "role": "low-end-spheremap",
            "reason": "obsolete Source low-end cubemap fallback",
            "byteLength": omitted_spheremap_bytes,
        })
    faces = []
    if cubemap:
        faces = [
            {
                "ktxFace": index,
                "name": name,
                "sourceFace": SOURCE_TO_GLTF_FACE_ORDER[index],
                "transform": SOURCE_TO_GLTF_FACE_TRANSFORMS[index],
            }
            for index, name in enumerate(FACE_NAMES)
        ]
    identities = [member.identity() for member in closure.members()]
    return TextureModel(
        texture_path=closure.texture_path,
        asset_id=closure.asset_id,
        sources=identities,
        format=info,
        width=levels[0].width,
        height=levels[0].height,
        declared_width=width,
        declared_height=height,
        frames=frames,
        cubemap=cubemap,
        mip_count=len(levels),
        levels=levels,
        header={
            "tthVersion": outer_version,
            "vtfVersion": f"{major}.{minor}",
            "flags": flags,
            "startFrame": start_frame,
            "reflectivity": list(reflectivity),
            "bumpScale": bump_scale,
            "declaredInlineMips": declared_inline,
            "resolvedInlineMips": actual_inline,
            "sourceFormat": info.source_name,
            "sourceFormatEnum": source_format,
            "vtfMipCount": vtf_mips,
            "sourceWidth": width,
            "sourceHeight": height,
            "sourceMipCount": image_mip_count,
            "tthMipTableCount": mip_count,
            "zlibStreamComplete": compressed_complete,
        },
        sampling=_sampling(flags),
        faces=faces,
        omissions=omissions,
        byte_coverage=ledgers,
    )
