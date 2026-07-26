"""Decode a VtMB .tth/.ttz texture pair to PNG.

Format (reverse-engineered):
  .tth : "TTH\\0" + a mip offset/size table + an EMBEDDED standard VTF header
         (starts at the "VTF\\0" marker). The VTF header gives width/height,
         high-res image format, and mip count.
  .ttz : zlib-compressed raw image data = the DXT mip pyramid, ordered
         smallest -> largest (standard VTF order), so the full-res mip is LAST.

DXT blocks are decoded by wrapping the raw bytes in a minimal DDS container and
letting PIL do the BC1/BC3 decode.
"""
import struct, zlib, io, sys
from PIL import Image

# VTF IMAGE_FORMAT enum values we care about.
FMT_RGBA8888, FMT_BGR888, FMT_BGRA8888 = 0, 3, 12
FMT_DXT1, FMT_DXT3, FMT_DXT5 = 13, 14, 15
DXT_FOURCC = {FMT_DXT1: b"DXT1", FMT_DXT3: b"DXT3", FMT_DXT5: b"DXT5"}

def parse_tth(tth: bytes):
    """Return (width, height, hi_format, mip_count) from the embedded VTF header."""
    v = tth.find(b"VTF\x00")
    if v < 0:
        raise ValueError("no embedded VTF header in .tth")
    width, height = struct.unpack_from("<HH", tth, v + 16)
    hi_format = struct.unpack_from("<I", tth, v + 52)[0]
    mip_count = tth[v + 56]
    return width, height, hi_format, mip_count


def reflectivity(tth: bytes):
    """The texture's average albedo (float[3] at +32 from the `VTF\\0` marker), or None.

    `vtex` computes this at build time and VtMB's engine reads it back: when the light cache
    builds a model's ambient cube it multiplies every bounce ray by the reflectivity of the
    material it hit (`../docs/sky-ambience.md` -> "K3 / K5"). So it is the missing input for
    any reproduction of VtMB's bounce term -- decodable header data, not dead space."""
    v = tth.find(b"VTF\x00")
    if v < 0 or len(tth) < v + 44:
        return None
    r, g, b = struct.unpack_from("<3f", tth, v + 32)
    # vtex writes a 0..1 average; anything outside that is a header we have misread.
    if not all(0.0 <= c <= 1.0 for c in (r, g, b)):
        return None
    return (r, g, b)

def mip_byte_size(w, h, fmt):
    if fmt == FMT_DXT1:
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 8
    if fmt in (FMT_DXT3, FMT_DXT5):
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 16
    if fmt == FMT_BGR888:
        return w * h * 3
    if fmt in (FMT_RGBA8888, FMT_BGRA8888):
        return w * h * 4
    raise ValueError(f"unsupported format {fmt}")

def make_dds(raw, w, h, fourcc):
    """Wrap raw DXT block data in a minimal DDS file PIL can decode."""
    header = bytearray(128)
    struct.pack_into("<4sIIIIII", header, 0,
                     b"DDS ", 124, 0x0000100F, h, w, len(raw), 0)
    # pixelformat block at offset 76: size, flags(FOURCC=0x4), fourcc
    struct.pack_into("<II4s", header, 76, 32, 0x4, fourcc)
    struct.pack_into("<I", header, 108, 0x1000)  # caps = TEXTURE
    return bytes(header) + raw

def _decode_mip(raw, w, h, fmt) -> Image.Image:
    """Decode one full-res mip's raw bytes (single face) -> RGBA image."""
    if fmt in DXT_FOURCC:
        return Image.open(io.BytesIO(make_dds(raw, w, h, DXT_FOURCC[fmt]))).convert("RGBA")
    if fmt == FMT_BGR888:
        return Image.frombytes("RGB", (w, h), raw, "raw", "BGR").convert("RGBA")
    if fmt == FMT_BGRA8888:
        return Image.frombytes("RGBA", (w, h), raw, "raw", "BGRA")
    if fmt == FMT_RGBA8888:
        return Image.frombytes("RGBA", (w, h), raw)
    raise ValueError(f"unsupported format {fmt}")

def decode_cubemap(tth: bytes, ttz) -> list:
    """Decode a baked env cubemap (.tth has the ENVMAP flag) -> the six RGBA cube
    faces in VTF order [+x,-x,+y,-y,+z,-z].

    VtMB cubemaps are VTF 7.1, which stores **seven** faces (the six axes plus a
    legacy spheremap dropped in 7.5); the spheremap is face 6 and discarded here.
    Two on-disk forms occur across the maps: DXT cubes ship the large mips zlib'd in
    a `.ttz` (the small mips live in the `.tth`), while uncompressed cubes (BGR888,
    seen on recompiled/patched maps) carry the whole image inline in the `.tth` with
    no `.ttz`. Either way the image data is standard VTF order (mips smallest->
    largest; within a mip, one image per face), so the full-res mip is the final
    7*facesize bytes and its first six faces are the cube."""
    w, h, fmt, mips = parse_tth(tth)
    if ttz:
        data = zlib.decompress(ttz)          # DXT: .ttz holds the largest mips
    else:
        v = tth.find(b"VTF\x00")             # uncompressed: image data trails the header
        hdr = struct.unpack_from("<I", tth, v + 12)[0]
        data = tth[v + hdr:]
    fs = mip_byte_size(w, h, fmt)            # one full-res face
    hi = data[len(data) - fs * 7:]           # the full-res mip's seven faces
    return [_decode_mip(hi[i*fs:(i+1)*fs], w, h, fmt) for i in range(6)]  # drop spheremap

def decode(tth: bytes, ttz: bytes) -> Image.Image:
    w, h, fmt, mips = parse_tth(tth)
    data = zlib.decompress(ttz)
    base = mip_byte_size(w, h, fmt)          # size of the full-res mip
    largest = data[len(data) - base:]        # it's stored LAST

    if fmt in DXT_FOURCC:
        return Image.open(io.BytesIO(make_dds(largest, w, h, DXT_FOURCC[fmt]))).convert("RGBA")
    if fmt == FMT_BGR888:
        return Image.frombytes("RGB", (w, h), largest, "raw", "BGR").convert("RGBA")
    if fmt == FMT_BGRA8888:
        return Image.frombytes("RGBA", (w, h), largest, "raw", "BGRA")
    if fmt == FMT_RGBA8888:
        return Image.frombytes("RGBA", (w, h), largest)
    raise ValueError(f"unsupported format {fmt}")

if __name__ == "__main__":
    import vpk
    GAME = r"E:/dev_game/Vampire The Masquerade - Bloodlines/Vampire"
    idx = vpk.index_all(GAME)
    name = sys.argv[1] if len(sys.argv) > 1 else "ground/streetb"
    out = sys.argv[2] if len(sys.argv) > 2 else f"out/{name.replace('/','_')}.png"
    tth = vpk.extract(idx[f"materials/{name}.tth"])
    ttz = vpk.extract(idx[f"materials/{name}.ttz"])
    w, h, fmt, mips = parse_tth(tth)
    img = decode(tth, ttz)
    img.save(out)
    print(f"{name}: {w}x{h} fmt={fmt} mips={mips} -> {out} ({img.size})")
