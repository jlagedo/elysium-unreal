"""Probe the real dface_t layout for this BSP by trying candidate strides and
checking which one yields sane firstedge/numedges values."""
import struct, sys

def main():
    def read_lump(data, index):
        fileofs, filelen = struct.unpack_from("<ii", data, 8 + index * 16)
        return data[fileofs:fileofs + filelen], filelen

    data = open(sys.argv[1], "rb").read()
    face_raw, face_len = read_lump(data, 7)
    se_raw, se_len = read_lump(data, 13)
    edge_raw, edge_len = read_lump(data, 12)
    n_surfedges = se_len // 4
    n_edges = edge_len // 4
    print(f"faces lump   : {face_len} bytes")
    print(f"surfedges    : {n_surfedges}")
    print(f"edges        : {n_edges}")
    print()

    # Candidate face struct sizes that divide the lump evenly.
    candidates = [s for s in range(16, 121, 2) if face_len % s == 0]
    print("strides dividing faces lump evenly:", candidates)
    print()

    # For each candidate stride, assume firstedge is int32 at some offset and
    # numedges int16 right after. Try offset 0..(stride-6). Score by how many of
    # the first 200 faces have firstedge in [0,n_surfedges) and numedges in [3,64]
    # and firstedge+numedges <= n_surfedges.
    best = None
    for stride in candidates:
        nf = face_len // stride
        for off in range(0, stride - 6, 2):
            good = 0
            checked = min(nf, 400)
            for fi in range(checked):
                b = fi * stride
                fe = struct.unpack_from("<i", face_raw, b + off)[0]
                ne = struct.unpack_from("<h", face_raw, b + off + 4)[0]
                if 0 <= fe < n_surfedges and 3 <= ne <= 64 and fe + ne <= n_surfedges:
                    good += 1
            score = good / checked
            if score > 0.9 and (best is None or score > best[0]):
                best = (score, stride, off, nf)

    if best:
        score, stride, off, nf = best
        print(f"BEST GUESS -> stride={stride} bytes, firstedge@offset {off}, "
              f"numedges@offset {off+4}  ({nf} faces, {score:.0%} sane)")
        print("\nfirst 12 faces (firstedge, numedges, next int16=texinfo?):")
        for fi in range(12):
            b = fi * stride
            fe = struct.unpack_from("<i", face_raw, b + off)[0]
            ne = struct.unpack_from("<h", face_raw, b + off + 4)[0]
            tx = struct.unpack_from("<h", face_raw, b + off + 6)[0]
            print(f"  face {fi:2d}: firstedge={fe:6d} numedges={ne:3d} texinfo={tx}")
    else:
        print("no candidate scored >90% - deeper inspection needed")
        # Dump first 64 bytes so we can eyeball the layout.
        print("\nfirst 64 bytes of faces lump:")
        print(" ".join(f"{x:02x}" for x in face_raw[:64]))

if __name__ == '__main__':
    main()
