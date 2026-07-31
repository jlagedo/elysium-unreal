"""Locate the lightmap fields inside the v17 104-byte dface_t and confirm the
LIGHTING lump sample format.

Modern dface_t (56B) lightmap fields, relative to struct start:
    byte  styles[4]                       @16
    int   lightofs                        @20   (-1 = no lightmap)
    int   LightmapTextureMinsInLuxels[2]  @28
    int   LightmapTextureSizeInLuxels[2]  @36
v17 shifts the early fields by +32 (firstedge is @36 not @4), so we scan for the
real offsets instead of trusting the modern ones.
"""
import struct, sys

def main():
    def read_lump(data, i):
        o, l = struct.unpack_from("<ii", data, 8 + i * 16)
        return data[o:o + l], l

    data = open(sys.argv[1], "rb").read()
    faces, flen = read_lump(data, 7)
    lighting, llen = read_lump(data, 8)
    FS = 104
    nf = flen // FS
    print(f"faces={nf}  LIGHTING lump={llen:,} bytes\n")

    # --- find lightofs: an int32 that is -1 or in [0, llen) for ~all faces ---
    print("== lightofs candidates (int32 offsets, value=-1 or in [0,llen)) ==")
    lightofs_off = None
    for off in range(0, FS - 4, 4):
        ok = neg1 = 0
        for fi in range(nf):
            v = struct.unpack_from("<i", faces, fi * FS + off)[0]
            if v == -1:
                neg1 += 1; ok += 1
            elif 0 <= v < llen:
                ok += 1
        if ok == nf and neg1 < nf:  # all valid, at least some real lightmaps
            print(f"  offset {off:3}: valid for all faces  ({neg1} have -1)")
            if lightofs_off is None:
                lightofs_off = off

    # --- find lightmap size: two consecutive int32 in [0,256] for all faces ---
    print("\n== lightmap-size candidates (two int32 in [0,255]) ==")
    size_off = None
    for off in range(0, FS - 8, 4):
        ok = 0
        smax = 0
        for fi in range(nf):
            a, b = struct.unpack_from("<ii", faces, fi * FS + off)
            if 0 <= a <= 255 and 0 <= b <= 255:
                ok += 1; smax = max(smax, a, b)
        if ok == nf and smax >= 8:  # plausible lightmap dims, not all zero
            print(f"  offset {off:3}: two small ints for all faces (max seen {smax})")
            if size_off is None and off != lightofs_off:
                size_off = off

    # --- verify RGBE (4 bytes/luxel): sum (sx+1)(sy+1) over faces * 4 ?= llen ---
    if lightofs_off is not None and size_off is not None:
        print(f"\nassuming lightofs@{lightofs_off}, lightmap-size@{size_off}")
        total_luxels = 0
        lit = 0
        for fi in range(nf):
            lo = struct.unpack_from("<i", faces, fi * FS + lightofs_off)[0]
            sx, sy = struct.unpack_from("<ii", faces, fi * FS + size_off)
            if lo != -1:
                total_luxels += (sx + 1) * (sy + 1)
                lit += 1
        print(f"  lit faces={lit}  total luxels={total_luxels:,}")
        for bpp, name in [(4, "RGBE8888"), (3, "RGB888")]:
            print(f"  {name}: {total_luxels*bpp:,} bytes  vs lump {llen:,}  "
                  f"({'MATCH' if abs(total_luxels*bpp-llen) < total_luxels else 'no'})")

if __name__ == '__main__':
    main()
