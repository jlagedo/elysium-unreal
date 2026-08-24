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
import os, struct, zlib, io, sys, tempfile
from PIL import Image
from elysium_pipeline.paths import export_root

# VTF IMAGE_FORMAT enum values we care about.
FMT_RGBA8888, FMT_BGR888, FMT_BGRA8888 = 0, 3, 12
FMT_DXT1, FMT_DXT3, FMT_DXT5 = 13, 14, 15
# Signed U/V/W displacement plus Q. VtMB's Source Refract cards use this for
# screen-space distortion vectors rather than colour (for example
# `models/scenery/structural/santamonica/rain_refract_dudv`).
FMT_UVWQ8888 = 23
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
    material it hit (`../docs/vtmb/sky-ambience.md` -> "K3 / K5"). So it is the missing input for
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
    if fmt in (FMT_RGBA8888, FMT_BGRA8888, FMT_UVWQ8888):
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
    if fmt == FMT_UVWQ8888:
        # Preserve the bytes here. UVW are signed two's-complement values, not
        # display colour; dudv_to_normal performs the semantic conversion.
        return Image.frombytes("RGBA", (w, h), raw)
    raise ValueError(f"unsupported format {fmt}")


def dudv_to_normal(image: Image.Image) -> Image.Image:
    """Convert Source's signed UVWQ8888 DUDV payload to a UE tangent normal.

    UVWQ stores U/V/W as signed bytes centred on zero. A conventional tangent
    normal stores the same signed range biased by 128, so toggling the high bit
    maps byte 0 (zero displacement) to 128, 127 (+1) to 255, and 255 (-1/127)
    to 127. UE normal compression reconstructs Z from R/G; emit blue=255 so the
    loose PNG is also a valid flat-forward normal before import.
    """
    rgba = image.convert("RGBA")
    u, v, _w, _q = rgba.split()
    signed_to_biased = [value ^ 0x80 for value in range(256)]
    return Image.merge("RGB", (
        u.point(signed_to_biased),
        v.point(signed_to_biased),
        Image.new("L", rgba.size, 255),
    ))

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


def cubemap_dds(faces) -> bytes:
    """Encode six equally-sized PIL faces as one uncompressed BGRA8 DDS cubemap.

    ``faces`` stays in VTF/D3D cubemap order (+X, -X, +Y, -Y, +Z, -Z).  No
    rotation or mirroring belongs here: the material applies the one established
    Source-to-Unreal handedness correction to its reflection vector instead.
    """
    if len(faces) != 6:
        raise ValueError("a cubemap requires exactly six faces")
    sizes = {face.size for face in faces}
    if len(sizes) != 1:
        raise ValueError("cubemap faces must have equal dimensions")
    width, height = next(iter(sizes))
    if width <= 0 or height <= 0 or width != height:
        raise ValueError("cubemap faces must be non-empty squares")

    # DDS_HEADER + DDS_PIXELFORMAT. Unreal imports this legacy DX9 form directly
    # as UTextureCube and preserves the canonical D3D face order above.
    header = bytearray(128)
    struct.pack_into("<4sIIIIIII", header, 0,
                     b"DDS ", 124, 0x0000100F, height, width, width * 4, 0, 0)
    struct.pack_into("<IIIIIIII", header, 76,
                     32, 0x41, 0, 32,
                     0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    struct.pack_into("<II", header, 108, 0x1008, 0xFE00)
    payload = b"".join(
        face.convert("RGBA").tobytes("raw", "BGRA") for face in faces
    )
    return bytes(header) + payload

def _dxt1_blocks_opaque(raw: bytes) -> bool:
    """Whether every BC1 block selects the four-colour mode (color0 > color1).

    BC1's other mode (color0 <= color1) decodes index-3 texels transparent, so a plain DXT1
    can still carry punch-through alpha. When no block uses that mode the decoded alpha plane
    is provably all-255, which lets `save_png` skip its per-pixel scan."""
    import numpy as np
    colors = np.frombuffer(raw, dtype="<u2")
    return bool((colors[0::4] > colors[1::4]).all())


def decode(tth: bytes, ttz: bytes) -> Image.Image:
    w, h, fmt, mips = parse_tth(tth)
    data = zlib.decompress(ttz)
    base = mip_byte_size(w, h, fmt)          # size of the full-res mip
    largest = data[len(data) - base:]        # it's stored LAST

    # Formats whose decoded alpha plane is provably all-255 are annotated so `save_png` can
    # answer its fold-to-RGB question from the source format instead of scanning every pixel.
    # `Image.info` survives `convert`, and PNG save writes no arbitrary info keys, so the
    # annotation never reaches the file.
    if fmt in DXT_FOURCC:
        image = Image.open(io.BytesIO(make_dds(largest, w, h, DXT_FOURCC[fmt]))).convert("RGBA")
        if fmt == FMT_DXT1 and _dxt1_blocks_opaque(largest):
            image.info["opaque_alpha"] = True
        return image
    if fmt == FMT_BGR888:
        image = Image.frombytes("RGB", (w, h), largest, "raw", "BGR").convert("RGBA")
        image.info["opaque_alpha"] = True    # no alpha plane in the source at all
        return image
    if fmt == FMT_BGRA8888:
        return Image.frombytes("RGBA", (w, h), largest, "raw", "BGRA")
    if fmt == FMT_RGBA8888:
        return Image.frombytes("RGBA", (w, h), largest)
    if fmt == FMT_UVWQ8888:
        return Image.frombytes("RGBA", (w, h), largest)
    raise ValueError(f"unsupported format {fmt}")


def save_png(image: Image.Image, path) -> None:
    """Write a decoded texture: alpha as the source stores it, published atomically.

    Alpha is a per-file fact, never a decision. A texture keeps whatever alpha plane its source
    format carries; the one normalization is lossless -- a plane that is uniformly opaque encodes
    nothing, so it folds to RGB. No material's semantics reach this function: whether a material
    *renders* with alpha lives in `materials.json`, and the bake keys compression and blend mode
    off that document, not off the PNG's channel count.

    Atomic because a sharded corpus decode reaches the same texture from more than one worker:
    two materials that share a basetexture can land in different shards. What they write is
    byte-identical -- name and pixels are both a function of the source texture and its role --
    so the duplicate is wasted work rather than a conflict, and `os.replace` keeps a reader (or
    the other writer) from ever observing a half-written PNG.

    The opaqueness question is answered from the source format when `decode` proved it
    (``image.info["opaque_alpha"]``); otherwise the alpha plane is scanned. Encoded at zlib
    level 1: the PNGs are gitignored intermediates whose read-back cost is identical, so encode
    speed outranks disk size.
    """
    if image.mode == "RGBA" and (
            image.info.get("opaque_alpha")
            or image.getchannel("A").getextrema()[0] == 255):
        image = image.convert("RGB")
    path = os.fspath(path)
    directory = os.path.dirname(path) or "."
    handle, temporary = tempfile.mkstemp(
        dir=directory, prefix=".tmp-", suffix=os.path.splitext(path)[1] or ".png")
    os.close(handle)
    try:
        image.save(temporary, compress_level=1)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


if __name__ == "__main__":
    from elysium_pipeline.formats import install, vpk
    GAME = install.GAME
    idx = vpk.index_all(GAME)
    name = sys.argv[1] if len(sys.argv) > 1 else "ground/streetb"
    out = sys.argv[2] if len(sys.argv) > 2 else os.fspath(
        export_root() / "_decode" / f"{name.replace('/','_')}.png"
    )
    os.makedirs(os.path.dirname(out), exist_ok=True)
    tth = vpk.extract(idx[f"materials/{name}.tth"])
    ttz = vpk.extract(idx[f"materials/{name}.ttz"])
    w, h, fmt, mips = parse_tth(tth)
    img = decode(tth, ttz)
    img.save(out)
    print(f"{name}: {w}x{h} fmt={fmt} mips={mips} -> {out} ({img.size})")
