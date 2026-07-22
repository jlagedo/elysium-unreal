"""Phase F recon: settle VtMB's baked-lighting reality before extending the atlas.

Dumps, per map:
  * the LIGHTING / HDR / ambient lump sizes (HDR + ambient are empty on VtMB);
  * the dface_t lighting fields (avgLightColor[8], styles[8], day[8], night[8]);
  * how many luxel SETS each face stores vs its non-FF style count -- 4 sets/style
    is the HL2 bumped-lightmap signature (NUM_BUMP_VECTS+1); byte-closure against
    the LIGHTING lump length confirms the interpretation;
  * which materials carry the 4-set bumped lightmaps (and whether their VMTs
    declare $bumpmap -- the patch recompiles bake bumped lightmaps on every face,
    but the engine only USES the basis sets where the material has a normal map).

Findings (ch_hub_1 / sm_hub_1 / sp_tutorial_1 / hw_hub_1 / la_hub_1):
  HDR lumps 51-56,58 empty everywhere; day[]==night[]==0 everywhere (the night-
  time lightmapping system was never baked). Lightstyles: hubs ~0-44 multi-style
  faces, sp_tutorial 1473 (styles 0,1,6,32-34). Bumped lightmaps: hubs 31-1820
  faces on genuine $bumpmap materials; sp_tutorial all 10702 lit faces (VRAD
  -bumpall on the Hammer recompile). Byte-closure exact (residual 0-4 B).
"""
import struct, sys, collections
sys.path.insert(0, "tools")
import bsp, install
import UE_bsp_to_scene as B

FS = 104
LUMPS = {8: "LIGHTING", 15: "WORLDLIGHTS", 51: "LEAF_AMBIENT_HDR",
         52: "LEAF_AMBIENT", 53: "LIGHTING_HDR", 54: "WORLDLIGHTS_HDR",
         55: "LEAF_AMBIENT_INDEX", 56: "reserved56", 58: "LEAF_AMBIENT_INDEX_HDR"}


def lump(data, i):
    fofs, flen, ver = struct.unpack_from("<iii", data, 8 + i * 16)
    return fofs, flen, ver


def main(name):
    idx = install.build_index()
    data = install.read(idx, "maps/%s.bsp" % name)
    faces = bsp.read_lump(data, 7)
    lighting = bsp.read_lump(data, 8)
    texinfo = bsp.read_lump(data, 6); texdata = bsp.read_lump(data, 2)
    names = bsp.strings_from_blob(bsp.read_lump(data, 43))
    table = list(struct.unpack_from("<%di" % (len(bsp.read_lump(data, 44)) // 4),
                                    bsp.read_lump(data, 44), 0))
    nf = len(faces) // FS
    print("=== %s ===  faces=%d" % (name, nf))
    for i in sorted(LUMPS):
        print("  lump %2d %-22s len=%d" % (i, LUMPS[i], lump(data, i)[1]))

    def face_mat(fi):
        ti = struct.unpack_from("<h", faces, fi * FS + 42)[0]
        if ti < 0:
            return ""
        tb = struct.unpack_from("<i", texinfo, ti * 72 + 68)[0]
        nid = struct.unpack_from("<i", texdata, tb * 32 + 12)[0]
        return B.base_material(names.get(table[nid], ""))

    style_idx = collections.Counter()
    daynight_nonzero = 0
    lit = []
    for fi in range(nf):
        b = fi * FS
        lo = struct.unpack_from("<i", faces, b + 72)[0]
        if any(x for x in faces[b + 56:b + 72]):   # day[8]+night[8] all zero?
            daynight_nonzero += 1
        if lo == -1:
            continue
        sx, sy = struct.unpack_from("<ii", faces, b + 88)
        one = (sx + 1) * (sy + 1) * 4
        styles = [x for x in faces[b + 48:b + 56] if x != 0xFF]
        for s in styles:
            style_idx[s] += 1
        lit.append((lo, one, len(styles), fi))
    lit.sort()

    total = 0
    pair = collections.Counter()
    bumped_mat = collections.Counter()
    for k in range(len(lit)):
        lo, one, ns, fi = lit[k]
        nxt = lit[k + 1][0] if k + 1 < len(lit) else len(lighting)
        if nxt == lo:
            continue
        total += nxt - lo
        sets = round((nxt - lo) / one) if one else 0
        pair[(ns, sets)] += 1
        if ns and sets / ns >= 3.5:
            bumped_mat[face_mat(fi)] += 1

    print("  lightstyle indices:", dict(sorted(style_idx.items())))
    print("  day/night non-zero faces:", daynight_nonzero)
    print("  (styles, sets) pairs:", dict(sorted(pair.items())))
    print("  byte-closure: lump=%d summed=%d residual=%d"
          % (len(lighting), total, len(lighting) - total))
    print("  bumped faces=%d over %d materials; top:"
          % (sum(bumped_mat.values()), len(bumped_mat)))
    for m, c in bumped_mat.most_common(6):
        print("     %5d  %s" % (c, m))
    print()


if __name__ == "__main__":
    for nm in (sys.argv[1:] or ["ch_hub_1"]):
        main(nm)
