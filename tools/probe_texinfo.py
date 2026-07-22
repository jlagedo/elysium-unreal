"""Probe v17 texinfo + texdata layouts.

texinfo (modern 72B): float textureVecs[2][4]; float lightmapVecs[2][4];
                      int flags; int texdata;
texdata (modern 32B): float reflectivity[3]; int nameStringTableID;
                      int width,height; int view_width,view_height;
We confirm strides/offsets against sane index ranges from THIS map.
"""
import struct, sys, math

def read_lump(data, i):
    o, l = struct.unpack_from("<ii", data, 8 + i * 16)
    return data[o:o + l], l

data = open(sys.argv[1], "rb").read()
texinfo, ti_len = read_lump(data, 6)
texdata, td_len = read_lump(data, 2)
table, tbl_len = read_lump(data, 44)
n_texdata_guess = td_len // 32
n_table = tbl_len // 4
print(f"texinfo lump {ti_len}  |  texdata lump {td_len} (~{n_texdata_guess} @32B)  "
      f"|  string-table {n_table} entries\n")

# ---- texdata: find stride + nameStringTableID offset in [0, n_table) ----
print("== texdata ==")
best_td = None
for stride in [s for s in range(16, 49, 4) if td_len % s == 0]:
    n = td_len // stride
    for off in range(0, stride - 3, 4):
        ok = 0
        for k in range(n):
            v = struct.unpack_from("<i", texdata, k * stride + off)[0]
            if 0 <= v < n_table:
                ok += 1
        if ok == n and (best_td is None or stride < best_td[0]):
            best_td = (stride, off, n)
if best_td:
    stride, off, n = best_td
    print(f"  stride={stride}  nameStringTableID@{off}  ({n} entries)")
    # width/height usually two ints after the name id
    for k in range(3):
        vals = struct.unpack_from("<6i", texdata, k * stride)
        print(f"  entry {k}: {vals}")
    TD_STRIDE, TD_NAMEOFS = stride, off
else:
    print("  texdata layout not found"); TD_STRIDE = TD_NAMEOFS = None

# ---- texinfo: find stride + texdata-index offset in [0, n_texdata) ----
print("\n== texinfo ==")
n_texdata = best_td[2] if best_td else n_texdata_guess
best_ti = None
for stride in [s for s in range(48, 121, 4) if ti_len % s == 0]:
    n = ti_len // stride
    for off in range(stride - 4, 0, -4):  # texdata index is near the end
        ok = 0
        for k in range(min(n, 500)):
            v = struct.unpack_from("<i", texinfo, k * stride + off)[0]
            if 0 <= v < n_texdata:
                ok += 1
        # also require first 8 floats (texture vecs) to be finite/reasonable
        vecs_ok = True
        for k in range(min(n, 50)):
            fs = struct.unpack_from("<8f", texinfo, k * stride)
            if any(math.isnan(f) or math.isinf(f) or abs(f) > 1e6 for f in fs):
                vecs_ok = False; break
        frac = ok / min(n, 500)
        if frac > 0.98 and vecs_ok and (best_ti is None or stride < best_ti[0]):
            best_ti = (stride, off, n)
if best_ti:
    stride, off, n = best_ti
    print(f"  stride={stride}  texdata-index@{off}  ({n} entries)")
    for k in range(2):
        s0 = struct.unpack_from("<4f", texinfo, k*stride)      # s axis
        t0 = struct.unpack_from("<4f", texinfo, k*stride+16)   # t axis
        td = struct.unpack_from("<i", texinfo, k*stride+off)[0]
        print(f"  entry {k}: sAxis={tuple(round(x,4) for x in s0)}")
        print(f"           tAxis={tuple(round(x,4) for x in t0)}  texdata={td}")
else:
    print("  texinfo layout not found")
