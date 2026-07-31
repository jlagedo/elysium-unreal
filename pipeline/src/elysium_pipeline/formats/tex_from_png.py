"""Encode an image into a VtMB .tth/.ttz texture pair — the inverse of `tex_to_png`.

Written for RE probes that need the *original game* to draw an authored image (RE-A2,
the labelled-sky probe): VtMB has no loose `.vtf` path, so a probe texture has to ship
in the engine's own container.

Container (the decoder's notes plus the fields a writer needs):

    .tth  "TTH\\0"                              4
          uint16 version         = 1
          uint8  mip_count                      full chain, 512x512 -> 10
          uint8  inline_mips                    smallest N mips kept in the .tth
          uint32 vtf_blob_len                   bytes from the "VTF\\0" marker to EOF
          (mip_count + 1) x { uint32 raw_offset; uint32 ttz_prefix }
          <embedded standard VTF 7.1 header, 64 bytes>
          <low-res thumbnail, DXT1 16x16, 128 bytes>
          <the `inline_mips` smallest mips>

    .ttz  one zlib stream: the mips the .tth does not carry, smallest -> largest,
          with a Z_SYNC_FLUSH between each.

`raw_offset` locates a mip in the reconstructed `[header][thumbnail][mips]` image,
counted from the "VTF\\0" marker, so the first mip sits at `header_size + thumbnail`
(192). `ttz_prefix` is how many *compressed* bytes precede that mip, which is why the
stream carries flush points: `zlib.decompressobj().decompress(ttz[:prefix])` yields
exactly the mips before it. Entry `mip_count` is the pair of totals — the end offset
and the whole `.ttz` length. Inline mips carry prefix 0.

Both shipped arrangements round-trip: DXT5 with three inline mips (`skybox/la*`) and
uncompressed BGR888 with none (`skybox/pier*`).
"""
import io
import struct
import zlib

from PIL import Image

from elysium_pipeline.formats import tex_to_png as t2p

VTF_HEADER_SIZE = 64
LOWRES_W = LOWRES_H = 16
LOWRES_FORMAT = t2p.FMT_DXT1
LOWRES_SIZE = t2p.mip_byte_size(LOWRES_W, LOWRES_H, LOWRES_FORMAT)   # 128


def _encode_mip(img: Image.Image, fmt: int) -> bytes:
    """One mip level's raw bytes in `fmt` — the exact inverse of `t2p._decode_mip`."""
    if fmt in t2p.DXT_FOURCC:
        buf = io.BytesIO()
        img.convert("RGBA").save(buf, "DDS", pixel_format=t2p.DXT_FOURCC[fmt].decode())
        return buf.getvalue()[128:]                      # strip the DDS header
    if fmt == t2p.FMT_BGR888:
        return img.convert("RGB").tobytes("raw", "BGR")
    if fmt == t2p.FMT_BGRA8888:
        return img.convert("RGBA").tobytes("raw", "BGRA")
    if fmt == t2p.FMT_RGBA8888:
        return img.convert("RGBA").tobytes("raw")
    raise ValueError(f"unsupported format {fmt}")


def _mip_chain(img: Image.Image, count: int) -> list:
    """`count` mips, largest first, box-filtered down from `img`."""
    w, h = img.size
    return [img if i == 0 else img.resize((max(1, w >> i), max(1, h >> i)), Image.BOX)
            for i in range(count)]


def _reflectivity(img: Image.Image) -> tuple:
    px = img.convert("RGB").resize((16, 16), Image.BOX).tobytes()
    return tuple(sum(px[c::3]) / (256.0 * 255.0) for c in range(3))


def _vtf_header(w, h, flags, fmt, mip_count, reflectivity) -> bytes:
    hdr = bytearray(VTF_HEADER_SIZE)
    struct.pack_into("<4sIII", hdr, 0, b"VTF\x00", 7, 1, VTF_HEADER_SIZE)
    struct.pack_into("<HHIHH", hdr, 16, w, h, flags, 1, 0)       # size, flags, frames
    struct.pack_into("<3f", hdr, 32, *reflectivity)
    struct.pack_into("<f", hdr, 48, 1.0)                         # bumpmapScale
    struct.pack_into("<IB", hdr, 52, fmt, mip_count)
    struct.pack_into("<IBB", hdr, 57, LOWRES_FORMAT, LOWRES_W, LOWRES_H)
    return bytes(hdr)


def encode(img: Image.Image, fmt=t2p.FMT_DXT5, flags=0x0120, inline_mips=3,
           mip_count=None) -> tuple:
    """Encode a square power-of-two image -> (tth_bytes, ttz_bytes)."""
    w, h = img.size
    if mip_count is None:
        mip_count = max(w, h).bit_length()                       # 512 -> 10
    inline_mips = min(inline_mips, mip_count)

    # Standard VTF image order is smallest -> largest.
    mips = [_encode_mip(m, fmt) for m in reversed(_mip_chain(img, mip_count))]
    thumb = _encode_mip(img.resize((LOWRES_W, LOWRES_H), Image.BOX), LOWRES_FORMAT)

    co = zlib.compressobj(9)
    packed, prefixes, total = [], [], 0
    for i, mip in enumerate(mips):
        prefixes.append(0 if i < inline_mips else total)
        if i < inline_mips:
            continue
        chunk = co.compress(mip) + co.flush(zlib.Z_SYNC_FLUSH)
        packed.append(chunk)
        total += len(chunk)
    tail = co.flush()
    packed.append(tail)
    ttz = b"".join(packed)

    tth = bytearray()
    tth += struct.pack("<4sHBBI", b"TTH\x00", 1, mip_count, inline_mips,
                       VTF_HEADER_SIZE + LOWRES_SIZE + sum(len(m) for m in mips[:inline_mips]))
    offset = VTF_HEADER_SIZE + LOWRES_SIZE
    for i, mip in enumerate(mips):
        tth += struct.pack("<II", offset, prefixes[i])
        offset += len(mip)
    tth += struct.pack("<II", offset, len(ttz))                  # the pair of totals
    tth += _vtf_header(w, h, flags, fmt, mip_count, _reflectivity(img))
    tth += thumb
    tth += b"".join(mips[:inline_mips])
    return bytes(tth), ttz


def template_params(tth: bytes) -> dict:
    """The encoding policy of a shipped `.tth`, for `encode(**params)`."""
    mip_count, inline_mips = tth[6], tth[7]
    v = tth.find(b"VTF\x00")
    if v < 0:
        raise ValueError("no embedded VTF header in .tth")
    return dict(fmt=struct.unpack_from("<I", tth, v + 52)[0],
                flags=struct.unpack_from("<I", tth, v + 20)[0],
                inline_mips=inline_mips, mip_count=mip_count)


def encode_like(template_tth: bytes, img: Image.Image) -> tuple:
    """Encode `img` with the format, flags and mip policy of a shipped texture."""
    return encode(img, **template_params(template_tth))


if __name__ == "__main__":
    # Round-trip check against the install: decode a shipped face, re-encode it with
    # its own policy, and compare what the decoder sees.
    import sys

    from elysium_pipeline.formats import install

    idx = install.build_index(verbose=False)
    names = sys.argv[1:] or ["skybox/lart", "skybox/pierrt", "skybox/hollyup"]
    for name in names:
        tth = install.read(idx, f"materials/{name}.tth")
        ttz = install.read(idx, f"materials/{name}.ttz")
        src = t2p.decode(tth, ttz)
        params = template_params(tth)
        tth2, ttz2 = encode(src, **params)
        back = t2p.decode(tth2, ttz2)
        diff = max(abs(a - b) for a, b in zip(src.convert("RGB").tobytes(),
                                              back.convert("RGB").tobytes()))
        head_ok = tth[:12] == tth2[:12]
        print(f"{name:20} fmt={params['fmt']:<3} inline={params['inline_mips']} "
              f"tth {len(tth)}->{len(tth2)} ttz {len(ttz)}->{len(ttz2)}  "
              f"header={'same' if head_ok else 'DIFFERS'}  max channel delta={diff}")
