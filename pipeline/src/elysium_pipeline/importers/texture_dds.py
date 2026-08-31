"""KTX2 → DDS staging and BC block decoding for the texture import lane.

The texture unit's BIN chunk is one KTX 2.0 file (`seam_map_texture.md` → "KTX 2.0 payload").
Unreal opens DDS, not KTX2, and -- the fact that shapes this module -- Unreal 5.8 opens only an
**uncompressed** DDS: both import paths (`InterchangeDDSTranslator.cpp` and the legacy factory in
`EditorFactories.cpp`) map the DXGI format through `UE::DDS::DXGIFormatGetClosestRawFormat`,
whose table has no BC entry, and refuse a BC1/BC2/BC3 file with "DDS DXGIFormat not supported".
So the lane decodes every block-compressed level itself and stages a DX10-header DDS of 8-bit
BGRA pixels (`B8G8R8A8_UNORM`, DXGI 87) -- every authored level, slice-major, mips largest first.
An already-uncompressed source is staged as it is: `B8G8R8A8_UNORM` stays DXGI 87,
`R8G8B8A8_UNORM` is DXGI 28, the three-byte `R8G8B8_UNORM` (no DXGI counterpart) is widened to
RGBA with an opaque alpha, and the four-byte UINT source (`R8G8B8A8_UINT`, the UVWQ8888 refraction
fields) keeps its bytes but is labelled `R8G8B8A8_UNORM` (DXGI 28): Unreal's DDS reader leaves
`R8G8B8A8_UINT` out of its channel-order table, so a DXGI 30 label would land in the BGRA8 source
with R and B swapped.

The BC1/BC2/BC3 decoders below are the lane's own and are the only decode a block ever gets on the
way into Unreal: the staged pixels, and the measure phase's reading of the staged and the built
DDS, all come from this code, so a measured delta is Unreal's encoder, never a decoder
disagreement. BC1 honours the three-colour (punch-through) mode when `c0 <= c1`; BC3 alpha uses
the spec's eight-value table when `a0 > a1` and the six-value table with 0 and 255 otherwise.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct

import numpy as np

from elysium_pipeline.formats.texture_glb.ktx2 import IDENTIFIER as KTX_IDENTIFIER


class TextureDdsError(ValueError):
    """The payload is not a KTX2 the lane admits, or a DDS it can read."""


# --- formats ------------------------------------------------------------------------------------

#: vkFormat -> (DXGI format, block width, block height, bytes per block, kind)
#: `kind` names the decoder: `bc1`, `bc2`, `bc3`, `rgba8`, `bgra8`, `rgb8`.
VK_FORMATS = {
    23: (28, 1, 1, 3, "rgb8"),      # VK_FORMAT_R8G8B8_UNORM -> widened to DXGI R8G8B8A8_UNORM
    37: (28, 1, 1, 4, "rgba8"),     # VK_FORMAT_R8G8B8A8_UNORM
    41: (28, 1, 1, 4, "rgba8"),     # VK_FORMAT_R8G8B8A8_UINT  (UVWQ8888) -> labelled R8G8B8A8_UNORM
    44: (87, 1, 1, 4, "bgra8"),     # VK_FORMAT_B8G8R8A8_UNORM
    133: (71, 4, 4, 8, "bc1"),      # VK_FORMAT_BC1_RGBA_UNORM_BLOCK
    135: (74, 4, 4, 16, "bc2"),     # VK_FORMAT_BC2_UNORM_BLOCK
    137: (77, 4, 4, 16, "bc3"),     # VK_FORMAT_BC3_UNORM_BLOCK
}

#: DXGI format -> (block width, block height, bytes per block, kind), for reading a DDS back.
DXGI_FORMATS = {
    28: (1, 1, 4, "rgba8"),
    29: (1, 1, 4, "rgba8"),   # R8G8B8A8_UNORM_SRGB
    30: (1, 1, 4, "rgba8"),
    71: (4, 4, 8, "bc1"),
    72: (4, 4, 8, "bc1"),     # BC1_UNORM_SRGB
    74: (4, 4, 16, "bc2"),
    75: (4, 4, 16, "bc2"),
    77: (4, 4, 16, "bc3"),
    78: (4, 4, 16, "bc3"),
    87: (1, 1, 4, "bgra8"),
    91: (1, 1, 4, "bgra8"),   # B8G8R8A8_UNORM_SRGB
}

#: Legacy fourCC -> kind, for a DDS written without a DX10 header.
FOURCC_KINDS = {b"DXT1": "bc1", b"DXT3": "bc2", b"DXT5": "bc3"}
KIND_BLOCKS = {"bc1": (4, 4, 8), "bc2": (4, 4, 16), "bc3": (4, 4, 16),
               "rgba8": (1, 1, 4), "bgra8": (1, 1, 4), "rgb8": (1, 1, 3)}

DDS_MAGIC = b"DDS "
DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PITCH, DDSD_PIXELFORMAT = 0x1, 0x2, 0x4, 0x8, 0x1000
DDSD_MIPMAPCOUNT, DDSD_LINEARSIZE = 0x20000, 0x80000
DDSCAPS_COMPLEX, DDSCAPS_TEXTURE, DDSCAPS_MIPMAP = 0x8, 0x1000, 0x400000
DDSCAPS2_CUBEMAP_ALL = 0xFE00
DDPF_FOURCC = 0x4
DX10_FOURCC = b"DX10"
RESOURCE_DIMENSION_TEXTURE2D = 3
RESOURCE_MISC_TEXTURECUBE = 0x4
HEADER_BYTES = 4 + 124
DX10_HEADER_BYTES = 20


def image_bytes(width: int, height: int, block_w: int, block_h: int, block_bytes: int) -> int:
    return max(1, -(-width // block_w)) * max(1, -(-height // block_h)) * block_bytes


# --- KTX2 ---------------------------------------------------------------------------------------


@dataclass(slots=True)
class Ktx2:
    vk_format: int
    width: int
    height: int
    layers: int          # 0 for a non-array texture, as the KTX2 header spells it
    faces: int           # 1 or 6
    levels: list[bytes]  # level 0 first; each is `images` concatenated layer-major, face-minor

    @property
    def images(self) -> int:
        return max(1, self.layers) * self.faces

    def image(self, level: int, layer: int, face: int) -> bytes:
        """One image out of a level, by the KTX2 layer-major/face-minor layout."""
        _, block_w, block_h, block_bytes, _ = VK_FORMATS[self.vk_format]
        size = image_bytes(max(1, self.width >> level), max(1, self.height >> level),
                           block_w, block_h, block_bytes)
        index = layer * self.faces + face
        data = self.levels[level]
        return data[index * size:(index + 1) * size]


def parse_ktx2(payload: bytes) -> Ktx2:
    """The header and level table of an admitted KTX2 payload, levels sliced out.

    A structural read for the rewrap: it checks what the DDS needs to be right (formats, extents,
    level sizes) and leaves the DFD/ledger scrutiny to `validation.texture_glb`, which the export
    already ran.
    """

    if len(payload) < 80 or payload[:12] != KTX_IDENTIFIER:
        raise TextureDdsError("payload is not KTX 2.0")
    (vk_format, type_size, width, height, depth, layers, faces, level_count,
     supercompression) = struct.unpack_from("<9I", payload, 12)
    if vk_format not in VK_FORMATS:
        raise TextureDdsError(f"KTX2 vkFormat {vk_format} is not one the lane admits")
    if type_size != 1 or depth != 0 or supercompression != 0:
        raise TextureDdsError("KTX2 header is not a plain 2D/cube/array texture")
    if faces not in (1, 6) or level_count < 1 or not width or not height:
        raise TextureDdsError("KTX2 header declares an invalid texture shape")
    if faces == 6 and layers > 1:
        raise TextureDdsError("cubemap arrays are not admitted")
    _, block_w, block_h, block_bytes, _ = VK_FORMATS[vk_format]
    images = max(1, layers) * faces
    levels: list[bytes] = []
    for level in range(level_count):
        offset, size, uncompressed = struct.unpack_from("<3Q", payload, 80 + level * 24)
        expected = image_bytes(max(1, width >> level), max(1, height >> level),
                               block_w, block_h, block_bytes) * images
        if size != expected or uncompressed != expected or offset + size > len(payload):
            raise TextureDdsError(f"KTX2 level {level} has an invalid extent")
        levels.append(payload[offset:offset + size])
    return Ktx2(vk_format, width, height, layers, faces, levels)


# --- DDS write ----------------------------------------------------------------------------------


def _widen_rgb8(data: bytes) -> bytes:
    pixels = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3)
    widened = np.empty((pixels.shape[0], 4), dtype=np.uint8)
    widened[:, :3] = pixels
    widened[:, 3] = 255
    return widened.tobytes()


def staged_format(vk_format: int) -> str:
    """The pixel kind the staged DDS carries for a source format: BC sources decode to `bgra8`."""

    kind = VK_FORMATS[vk_format][4]
    if kind.startswith("bc"):
        return "bgra8"
    return "rgba8" if kind == "rgb8" else kind


def _rgba_to_bgra_bytes(rgba: np.ndarray) -> bytes:
    return np.ascontiguousarray(rgba[:, :, [2, 1, 0, 3]]).tobytes()


def build_dds(ktx: Ktx2, *, faces: list[list[bytes]] | None = None) -> bytes:
    """One DX10 DDS over the KTX2's levels, block-compressed levels decoded to BGRA8.

    `faces`, when given, replaces a cubemap's images: six lists (DDS face order +X, -X, +Y, -Y,
    +Z, -Z), each with one image per level, largest first, still in the source's block format --
    the lane's cube reorder rotates blocks exactly and feeds this, and the decode happens here,
    after that rotation.
    """

    dxgi, block_w, block_h, block_bytes, kind = VK_FORMATS[ktx.vk_format]
    level_count = len(ktx.levels)
    is_cube = ktx.faces == 6
    slices = 6 if is_cube else max(1, ktx.layers)
    out_kind = staged_format(ktx.vk_format)
    if out_kind == "bgra8":
        dxgi = 87

    def image_for(slice_index: int, level: int) -> bytes:
        if is_cube:
            data = faces[slice_index][level] if faces is not None else ktx.image(level, 0, slice_index)
        else:
            data = ktx.image(level, slice_index, 0)
        if kind.startswith("bc"):
            width, height = max(1, ktx.width >> level), max(1, ktx.height >> level)
            return _rgba_to_bgra_bytes(decode_image(kind, data, width, height))
        return _widen_rgb8(data) if kind == "rgb8" else data

    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_PITCH
    pitch_or_size = ktx.width * 4
    caps = DDSCAPS_TEXTURE
    if level_count > 1:
        flags |= DDSD_MIPMAPCOUNT
        caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP
    if is_cube or slices > 1:
        caps |= DDSCAPS_COMPLEX
    header = bytearray(HEADER_BYTES)
    struct.pack_into("<4sIIIIIII", header, 0, DDS_MAGIC, 124, flags, ktx.height, ktx.width,
                     pitch_or_size, 0, level_count)
    struct.pack_into("<II4sIIIII", header, 76, 32, DDPF_FOURCC, DX10_FOURCC, 0, 0, 0, 0, 0)
    struct.pack_into("<IIII", header, 108, caps, DDSCAPS2_CUBEMAP_ALL if is_cube else 0, 0, 0)
    dx10 = struct.pack("<IIIII", dxgi, RESOURCE_DIMENSION_TEXTURE2D,
                       RESOURCE_MISC_TEXTURECUBE if is_cube else 0,
                       1 if is_cube else slices, 0)
    parts = [bytes(header), dx10]
    for slice_index in range(slices):
        for level in range(level_count):
            parts.append(image_for(slice_index, level))
    return b"".join(parts)


# --- DDS read -----------------------------------------------------------------------------------


@dataclass(slots=True)
class Dds:
    kind: str
    width: int
    height: int
    mip_count: int
    slices: int          # array slices, or 6 for a cubemap
    is_cube: bool
    dxgi: int | None
    images: list[list[bytes]]   # [slice][level]

    def decode(self, slice_index: int = 0, level: int = 0) -> np.ndarray:
        """RGBA8 pixels of one image as an `(h, w, 4)` array."""
        return decode_image(self.kind, self.images[slice_index][level],
                            max(1, self.width >> level), max(1, self.height >> level))


def parse_dds(data: bytes) -> Dds:
    """Read a DDS the lane wrote, or the built one Unreal wrote back (legacy fourCC or DX10)."""

    if len(data) < HEADER_BYTES or data[:4] != DDS_MAGIC:
        raise TextureDdsError("not a DDS file")
    size, flags, height, width, _pitch, _depth, mip_count = struct.unpack_from("<IIIIIII", data, 4)
    if size != 124:
        raise TextureDdsError("DDS header size is not 124")
    pf_flags, fourcc = struct.unpack_from("<I4s", data, 80)
    rgb_bits, r_mask, g_mask, b_mask = struct.unpack_from("<IIII", data, 88)
    caps2 = struct.unpack_from("<I", data, 112)[0]
    mip_count = max(1, mip_count) if flags & DDSD_MIPMAPCOUNT else 1
    offset = HEADER_BYTES
    dxgi: int | None = None
    slices = 1
    is_cube = bool(caps2 & 0x200)
    if pf_flags & DDPF_FOURCC and fourcc == DX10_FOURCC:
        dxgi, dimension, misc, array_size, _misc2 = struct.unpack_from("<IIIII", data, offset)
        offset += DX10_HEADER_BYTES
        if dimension != RESOURCE_DIMENSION_TEXTURE2D:
            raise TextureDdsError(f"DDS resource dimension {dimension} is not 2D")
        if dxgi not in DXGI_FORMATS:
            raise TextureDdsError(f"DDS DXGI format {dxgi} is not one the lane reads")
        block_w, block_h, block_bytes, kind = DXGI_FORMATS[dxgi]
        is_cube = is_cube or bool(misc & RESOURCE_MISC_TEXTURECUBE)
        slices = 6 * max(1, array_size) if is_cube else max(1, array_size)
    elif pf_flags & DDPF_FOURCC and fourcc in FOURCC_KINDS:
        kind = FOURCC_KINDS[fourcc]
        block_w, block_h, block_bytes = KIND_BLOCKS[kind]
        slices = 6 if is_cube else 1
    elif rgb_bits == 32 and r_mask == 0x00FF0000 and g_mask == 0x0000FF00 and b_mask == 0xFF:
        kind, (block_w, block_h, block_bytes) = "bgra8", KIND_BLOCKS["bgra8"]
        slices = 6 if is_cube else 1
    elif rgb_bits == 32 and r_mask == 0xFF and g_mask == 0x0000FF00 and b_mask == 0x00FF0000:
        kind, (block_w, block_h, block_bytes) = "rgba8", KIND_BLOCKS["rgba8"]
        slices = 6 if is_cube else 1
    else:
        raise TextureDdsError("DDS pixel format is not one the lane reads")
    images: list[list[bytes]] = []
    for _ in range(slices):
        chain: list[bytes] = []
        for level in range(mip_count):
            n = image_bytes(max(1, width >> level), max(1, height >> level),
                            block_w, block_h, block_bytes)
            if offset + n > len(data):
                raise TextureDdsError("DDS data ends before its declared images")
            chain.append(data[offset:offset + n])
            offset += n
        images.append(chain)
    return Dds(kind, width, height, mip_count, slices, is_cube, dxgi, images)


# --- BC decoding --------------------------------------------------------------------------------


def _expand_565(colours: np.ndarray) -> np.ndarray:
    """`(n,)` uint16 565 -> `(n, 3)` uint8, with bit replication."""
    r = ((colours >> 11) & 0x1F).astype(np.uint16)
    g = ((colours >> 5) & 0x3F).astype(np.uint16)
    b = (colours & 0x1F).astype(np.uint16)
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)],
                    axis=1).astype(np.uint8)


def _bc1_colours(block_words: np.ndarray, force_four: bool) -> tuple[np.ndarray, np.ndarray]:
    """Per block: the 4-entry RGBA palette `(n, 4, 4)` and the 16 2-bit indices `(n, 16)`."""
    c0, c1 = block_words[:, 0], block_words[:, 1]
    rgb0 = _expand_565(c0).astype(np.int32)
    rgb1 = _expand_565(c1).astype(np.int32)
    four = (c0 > c1) | force_four
    palette = np.zeros((len(c0), 4, 4), dtype=np.int32)
    palette[:, :, 3] = 255
    palette[:, 0, :3] = rgb0
    palette[:, 1, :3] = rgb1
    palette[:, 2, :3] = np.where(four[:, None], (2 * rgb0 + rgb1 + 1) // 3, (rgb0 + rgb1) // 2)
    palette[:, 3, :3] = np.where(four[:, None], (rgb0 + 2 * rgb1 + 1) // 3, 0)
    palette[:, 3, 3] = np.where(four, 255, 0)
    bits = block_words[:, 2].astype(np.uint32) | (block_words[:, 3].astype(np.uint32) << 16)
    indices = np.stack([(bits >> (2 * i)) & 0x3 for i in range(16)], axis=1)
    return palette.astype(np.uint8), indices


def _blocks_to_image(texels: np.ndarray, width: int, height: int) -> np.ndarray:
    """`(blocks, 16, 4)` block texels -> `(height, width, 4)` cropped image."""
    bw, bh = -(-width // 4), -(-height // 4)
    grid = texels.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return np.ascontiguousarray(grid[:height, :width])


def _decode_bc1(data: bytes, width: int, height: int) -> np.ndarray:
    words = np.frombuffer(data, dtype="<u2").reshape(-1, 4)
    palette, indices = _bc1_colours(words, force_four=False)
    texels = np.take_along_axis(palette, indices[:, :, None].repeat(4, axis=2), axis=1)
    return _blocks_to_image(texels, width, height)


def _decode_bc2(data: bytes, width: int, height: int) -> np.ndarray:
    raw = np.frombuffer(data, dtype=np.uint8).reshape(-1, 16)
    alpha_words = np.frombuffer(raw[:, :8].tobytes(), dtype="<u2").reshape(-1, 4)
    alpha_bits = (alpha_words[:, 0].astype(np.uint64) | (alpha_words[:, 1].astype(np.uint64) << 16)
                  | (alpha_words[:, 2].astype(np.uint64) << 32)
                  | (alpha_words[:, 3].astype(np.uint64) << 48))
    alpha = np.stack([((alpha_bits >> np.uint64(4 * i)) & np.uint64(0xF)) for i in range(16)],
                     axis=1).astype(np.uint16)
    alpha = ((alpha << 4) | alpha).astype(np.uint8)
    words = np.frombuffer(raw[:, 8:].tobytes(), dtype="<u2").reshape(-1, 4)
    palette, indices = _bc1_colours(words, force_four=True)
    texels = np.take_along_axis(palette, indices[:, :, None].repeat(4, axis=2), axis=1)
    texels[:, :, 3] = alpha
    return _blocks_to_image(texels, width, height)


def _decode_bc3(data: bytes, width: int, height: int) -> np.ndarray:
    raw = np.frombuffer(data, dtype=np.uint8).reshape(-1, 16)
    a0 = raw[:, 0].astype(np.int32)
    a1 = raw[:, 1].astype(np.int32)
    bits = np.zeros(len(raw), dtype=np.uint64)
    for i in range(6):
        bits |= raw[:, 2 + i].astype(np.uint64) << np.uint64(8 * i)
    codes = np.stack([((bits >> np.uint64(3 * i)) & np.uint64(0x7)) for i in range(16)],
                     axis=1).astype(np.int64)
    eight = a0 > a1
    table = np.zeros((len(raw), 8), dtype=np.int32)
    table[:, 0], table[:, 1] = a0, a1
    for i in range(1, 7):
        table[:, i + 1] = np.where(eight, ((7 - i) * a0 + i * a1 + 3) // 7, 0)
    for i in range(1, 5):
        table[:, i + 1] = np.where(eight, table[:, i + 1], ((5 - i) * a0 + i * a1 + 2) // 5)
    table[:, 6] = np.where(eight, table[:, 6], 0)
    table[:, 7] = np.where(eight, table[:, 7], 255)
    alpha = np.take_along_axis(table, codes, axis=1).astype(np.uint8)
    words = np.frombuffer(raw[:, 8:].tobytes(), dtype="<u2").reshape(-1, 4)
    palette, indices = _bc1_colours(words, force_four=True)
    texels = np.take_along_axis(palette, indices[:, :, None].repeat(4, axis=2), axis=1)
    texels[:, :, 3] = alpha
    return _blocks_to_image(texels, width, height)


def decode_image(kind: str, data: bytes, width: int, height: int) -> np.ndarray:
    """RGBA8 `(height, width, 4)` for one image of the given kind."""

    if kind == "bc1":
        return _decode_bc1(data, width, height)
    if kind == "bc2":
        return _decode_bc2(data, width, height)
    if kind == "bc3":
        return _decode_bc3(data, width, height)
    if kind == "rgba8":
        return np.frombuffer(data, dtype=np.uint8).reshape(height, width, 4).copy()
    if kind == "bgra8":
        return np.frombuffer(data, dtype=np.uint8).reshape(height, width, 4)[:, :, [2, 1, 0, 3]].copy()
    if kind == "rgb8":
        rgb = np.frombuffer(data, dtype=np.uint8).reshape(height, width, 3)
        out = np.full((height, width, 4), 255, dtype=np.uint8)
        out[:, :, :3] = rgb
        return out
    raise TextureDdsError(f"no decoder for {kind}")
