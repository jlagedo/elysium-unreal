"""Locate animated-lightstyle faces in a map and rank them by how visible the
animation is, so we can name a spot to stand and watch.

For each face with >1 non-FF style, compute its world-space centroid (Godot
coords) and its baked brightness, and tag each style with the amplitude of its
Quake pattern (how far it swings). A face is "watchable" when a dramatic style
(big swing) sits on a bright face.
"""
import struct, sys
import numpy as np
import bsp_to_scene as B
from install import build_index, read, map_path

FS = 104

# Quake lightstyle patterns (index 0-11); 'a'=0 .. 'm'=1.0 .. 'z'=2.083
PATTERNS = [
    "m",                                                    # 0 normal (constant)
    "mmnmmommommnonmmonqnmmo",                              # 1 fluorescent flicker
    "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba",  # 2 slow strong pulse
    "mmmmmaaaaammmmmaaaaaabcdefgabcdefg",                   # 3 candle 1
    "mamamamamamamamamamamamamamamama",                     # 4 fast strobe
    "jklmnopqrstuvwxyzyxwvutsrqponmlkj",                    # 5 gentle pulse
    "nmonqnmomnmomomno",                                    # 6 flicker 2
    "mmmaaaabcdefgmmmmaaaammmaamm",                         # 7 candle 2
    "mmmaaaammmaaammmabcdefaaaammmmabcdefmmmaaaa",          # 8 candle 3
    "aaaaaaaazzzzzzzz",                                     # 9 slow strobe
    "mmamammmmammamamaaamammma",                            # 10 fluorescent 2
    "abcdefghijklmnopqrrqponmlkjihgfedcba",                # 11 slow pulse
]

def amp(idx):
    if idx >= len(PATTERNS):
        return 0.0
    vals = [(ord(c) - ord('a')) / 12.0 for c in PATTERNS[idx]]
    return max(vals) - min(vals)

def main(name):
    dirs = None
    data = open(map_path(name), "rb").read()
    faces = B.read_lump(data, 7)
    lighting = B.read_lump(data, 8)
    verts_l = B.read_lump(data, 3); edges_l = B.read_lump(data, 12); surf_l = B.read_lump(data, 13)
    texinfo_l = B.read_lump(data, 6); texdata_l = B.read_lump(data, 2)
    names = B.strings_from_blob(B.read_lump(data, 43))
    table = list(struct.unpack_from("<%di" % (len(B.read_lump(data,44))//4), B.read_lump(data,44), 0))
    vertexes = [struct.unpack_from("<fff", verts_l, i) for i in range(0, len(verts_l), 12)]
    edges = [struct.unpack_from("<HH", edges_l, i) for i in range(0, len(edges_l), 4)]
    surfedges = list(struct.unpack_from("<%di" % (len(surf_l)//4), surf_l, 0))

    from lightmap import decode_rgbe
    rows = []
    nf = len(faces) // FS
    for fi in range(nf):
        b = fi * FS
        styles = faces[b+48:b+56]
        nsty = sum(1 for s in styles if s != 0xFF)
        if nsty < 2:
            continue
        lo = struct.unpack_from("<i", faces, b+72)[0]
        if lo == -1:
            continue
        ti = struct.unpack_from("<h", faces, b+42)[0]
        fe = struct.unpack_from("<i", faces, b+36)[0]
        ne = struct.unpack_from("<h", faces, b+40)[0]
        if ne < 3 or ti < 0:
            continue
        tb = struct.unpack_from("<i", texinfo_l, ti*72 + 68)[0]
        nid = struct.unpack_from("<i", texdata_l, tb*32 + 12)[0]
        mat = B.base_material(names.get(table[nid], ""))
        if mat.startswith("tools/"):
            continue
        sx, sy = struct.unpack_from("<ii", faces, b+88)
        n = (sx+1)*(sy+1)
        # style 0 flat set brightness, and the brightest animated style's set
        s0 = decode_rgbe(lighting[lo:lo+n*4]).mean()
        # centroid (Godot coords, metres)
        pts = []
        for k in range(ne):
            se = surfedges[fe+k]
            v = edges[se][0] if se >= 0 else edges[-se][1]
            pts.append(B.source_to_godot(*vertexes[v]))
        c = np.mean(pts, axis=0)
        active = [s for s in styles if s != 0xFF]
        best = max(active, key=amp)
        rows.append((amp(best), best, active, s0, c, mat, ne))

    # Rank: watchable = big swing AND bright enough to see
    rows.sort(key=lambda r: r[0] * min(r[3], 40), reverse=True)
    print(f"{name}: {len(rows)} animated (non-tools) faces")
    print(f"  {'styles':<14} {'amp':>5} {'s0':>6}  centroid (godot x y z)        material")
    for amp_v, best, active, s0, c, mat, ne in rows[:16]:
        print(f"  {str(active):<14} {amp_v:>5.2f} {s0:>6.1f}  "
              f"({c[0]:7.2f},{c[1]:6.2f},{c[2]:7.2f})   {mat}")

if __name__ == "__main__":
    for m in sys.argv[1:]:
        main(m); print()
