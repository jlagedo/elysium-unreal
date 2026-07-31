"""Probe OVERLAYS (lump 45) + neighbours for a VtMB BSP.

Phase E reconnaissance: does the map carry overlays at all, what struct size
divides the lump cleanly, and do the first entries decode into sane texinfo /
face-count / render-order / UV / origin / normal values.

Modern Source `doverlay_t` (OVERLAY_BSP_FACE_COUNT = 64):
    int   nId;                               @0
    short nTexInfo;                          @4
    ushort m_nFaceCountAndRenderOrder;       @6   (low 14 bits count, high 2 order)
    int   aFaces[64];                        @8   -> 256
    float u[2];                              @264
    float v[2];                              @272
    Vector vecUVPoints[4];                   @280 -> 48
    Vector origin;                           @328
    Vector vecBasisNormal;                   @340
                                             = 352 bytes
Early Source may use a different OVERLAY_BSP_FACE_COUNT; the divisibility scan
below reports which candidate sizes fit so we don't assume 352.

Usage: uv run elysium research probe_overlay <bsp> [<bsp> ...]
"""
import struct, sys, os
from elysium_pipeline.formats import bsp as B

L_OVERLAYS = 45
# neighbouring lumps that sometimes carry overlay data across Source versions
NEIGHBOURS = {
    45: "OVERLAYS",
    46: "LEAFMINDISTTOWATER",
    60: "OVERLAY_FADES",
    61: "OVERLAY_SYSTEM_LEVELS",
}

FACE_COUNT_CANDIDATES = [64, 32, 16, 8]  # OVERLAY_BSP_FACE_COUNT guesses


def overlay_size(face_count):
    # id4 + texinfo2 + fcao2 + faces(4*fc) + u8 + v8 + uvpts48 + origin12 + normal12
    return 4 + 2 + 2 + 4 * face_count + 8 + 8 + 48 + 12 + 12


def texinfo_material(data, ti):
    """texinfo index -> material name via texdata string table (like the exporter)."""
    if ti < 0:
        return "(none)"
    texinfo = B.read_lump(data, B.L_TEXINFO)
    texdata = B.read_lump(data, B.L_TEXDATA)
    names = B.strings_from_blob(B.read_lump(data, B.L_TEXDATA_STR_DATA))
    tbl_raw = B.read_lump(data, B.L_TEXDATA_STR_TABLE)
    table = list(struct.unpack_from("<%di" % (len(tbl_raw) // 4), tbl_raw, 0))
    td = struct.unpack_from("<i", texinfo, ti * 72 + 68)[0]
    name_id = struct.unpack_from("<i", texdata, td * 32 + 12)[0]
    return names.get(table[name_id], "?")


def main(path):
    data = open(path, "rb").read()
    name = os.path.basename(path)
    ident, ver = struct.unpack_from("<4si", data, 0)
    print(f"\n=== {name}  ident={ident!r} version={ver} ===")

    for li, lname in NEIGHBOURS.items():
        ofs, length = B.lump_ptr(data, li)
        lver = struct.unpack_from("<i", data, 8 + li * 16 + 8)[0]
        print(f"  lump {li:2d} {lname:22s} len={length:8d}  ofs={ofs:9d}  ver={lver}")

    raw = B.read_lump(data, L_OVERLAYS)
    if not raw:
        print("  (no OVERLAYS data)")
        return
    print(f"\n  OVERLAYS lump: {len(raw)} bytes")
    for fc in FACE_COUNT_CANDIDATES:
        sz = overlay_size(fc)
        fits = len(raw) % sz == 0
        print(f"    OVERLAY_BSP_FACE_COUNT={fc:2d} -> struct {sz:4d}B : "
              f"{'FITS ' if fits else '     '} {len(raw)}/{sz} = {len(raw)/sz:.3f}")

    # decode the first few entries with the best-fitting candidate
    for fc in FACE_COUNT_CANDIDATES:
        sz = overlay_size(fc)
        if len(raw) % sz == 0:
            count = len(raw) // sz
            print(f"\n  Decoding with face_count={fc} (struct {sz}B): {count} overlays")
            for i in range(min(count, 6)):
                o = i * sz
                oid = struct.unpack_from("<i", raw, o)[0]
                ti = struct.unpack_from("<h", raw, o + 4)[0]
                fcao = struct.unpack_from("<H", raw, o + 6)[0]
                nfaces = fcao & 0x3FFF
                order = fcao >> 14
                faces = struct.unpack_from("<%di" % fc, raw, o + 8)
                after = o + 8 + 4 * fc
                u = struct.unpack_from("<2f", raw, after)
                v = struct.unpack_from("<2f", raw, after + 8)
                uvpts = struct.unpack_from("<12f", raw, after + 16)
                origin = struct.unpack_from("<3f", raw, after + 64)
                normal = struct.unpack_from("<3f", raw, after + 76)
                mat = texinfo_material(data, ti)
                print(f"    [{i}] id={oid} texinfo={ti} ({mat})")
                print(f"        faceCount={nfaces} order={order} "
                      f"faces[0:{nfaces}]={list(faces[:max(nfaces,1)])}")
                print(f"        u={u} v={v}")
                print(f"        origin={tuple(round(c,1) for c in origin)} "
                      f"normal={tuple(round(c,3) for c in normal)}")
            break
    else:
        print("  ! no candidate struct size divides the lump cleanly")


if __name__ == "__main__":
    from elysium_pipeline.formats import install
    args = sys.argv[1:] or ["ch_hub_1", "la_hub_1", "sp_tutorial_1"]
    for a in args:
        p = a if os.path.sep in a or a.endswith(".bsp") else install.map_path(a)
        main(p)
