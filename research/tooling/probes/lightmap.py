"""Decode VtMB baked lightmaps (LIGHTING lump 8) and visualize them.

v17 dface_t lightmap fields:  styles[8] @48, lightofs int32 @72 (-1=unlit),
LightmapMins[2] @80, LightmapSize[2] @88.  Samples are RGBE8888 (4 bytes/luxel):
per channel  linear = mantissa * 2^(signed_exponent).  A face's lightmap is
(sizeX+1) x (sizeY+1) luxels; it stores numstyles x setsPerStyle grids (setsPerStyle
is 1 flat / 4 bumped, numstyles = styles[8] non-FF count). This reads only the first
(style-0 flat) set; the atlas + F1/F2 in bsp_to_scene read the rest. See
docs/vtmb/lighting.md.
"""
import os, struct, sys
import numpy as np
from elysium_pipeline.exporters import UE_bsp_to_scene as B
from elysium_pipeline.paths import research_root

FS = 104
LOFS, MINS, SIZE, STYLES = 72, 80, 88, 48

def decode_rgbe(buf):
    """buf: bytes of RGBE luxels -> (n,3) float array of linear light (0..255+)."""
    a = np.frombuffer(buf, dtype=np.uint8).reshape(-1, 4).astype(np.float32)
    exp = a[:, 3].astype(np.int8).astype(np.float32)      # signed exponent
    scale = np.power(2.0, exp)[:, None]
    return a[:, :3] * scale                                # linear, may exceed 255

def face_lightmap(faces, lighting, fi):
    b = fi * FS
    lo = struct.unpack_from("<i", faces, b + LOFS)[0]
    if lo == -1:
        return None
    sx, sy = struct.unpack_from("<ii", faces, b + SIZE)
    w, h = sx + 1, sy + 1
    n = w * h
    lm = decode_rgbe(lighting[lo:lo + n * 4])             # first (style-0) set
    return lm.reshape(h, w, 3), (w, h)

def main(bsp_path, out_png):
    data = open(bsp_path, "rb").read()
    faces = B.read_lump(data, 7)
    lighting = B.read_lump(data, 8)
    verts_l = B.read_lump(data, 3); edges_l = B.read_lump(data, 12); surf_l = B.read_lump(data, 13)
    texinfo_l = B.read_lump(data, 6); texdata_l = B.read_lump(data, 2)
    names = B.strings_from_blob(B.read_lump(data, 43))
    table = list(struct.unpack_from("<%di" % (len(B.read_lump(data,44))//4), B.read_lump(data,44), 0))
    vertexes = [struct.unpack_from("<fff", verts_l, i) for i in range(0, len(verts_l), 12)]
    edges = [struct.unpack_from("<HH", edges_l, i) for i in range(0, len(edges_l), 4)]
    surfedges = list(struct.unpack_from("<%di" % (len(surf_l)//4), surf_l, 0))

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection

    polys, colors = [], []
    n_faces = len(faces) // FS
    lit = 0
    for fi in range(n_faces):
        b = fi * FS
        fe = struct.unpack_from("<i", faces, b + 36)[0]
        ne = struct.unpack_from("<h", faces, b + 40)[0]
        ti = struct.unpack_from("<h", faces, b + 42)[0]
        if ne < 3 or ti < 0:
            continue
        tb = struct.unpack_from("<i", texinfo_l, ti*72 + 68)[0]
        nid = struct.unpack_from("<i", texdata_l, tb*32 + 12)[0]
        if B.base_material(names.get(table[nid], "")).startswith("tools/"):
            continue
        lm = face_lightmap(faces, lighting, fi)
        if lm is None:
            continue
        avg = lm[0].reshape(-1, 3).mean(axis=0)           # avg linear light
        col = np.clip(avg / 255.0, 0, 1)
        pts = []
        for k in range(ne):
            se = surfedges[fe + k]
            v = edges[se][0] if se >= 0 else edges[-se][1]
            x, y, z = vertexes[v]
            pts.append((x, y))                             # top-down (source XY)
        polys.append(pts); colors.append(col); lit += 1

    fig, ax = plt.subplots(figsize=(14, 12), facecolor="black")
    pc = PolyCollection(polys, facecolors=colors, edgecolors="none")
    ax.add_collection(pc); ax.autoscale(); ax.set_aspect("equal")
    ax.set_facecolor("black"); ax.axis("off")
    ax.set_title(f"ch_hub_1 baked lighting (style 0)  -  {lit} lit faces",
                 color="#888")
    fig.savefig(out_png, dpi=90, facecolor="black", bbox_inches="tight")
    print(f"lit faces drawn: {lit}  ->  {out_png}")

if __name__ == "__main__":
    bsp = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else str(
        research_root() / "lighting" / "ch_hub_1_lighting.png"
    )
    os.makedirs(os.path.dirname(out), exist_ok=True)
    main(bsp, out)
