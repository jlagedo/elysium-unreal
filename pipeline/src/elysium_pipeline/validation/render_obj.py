"""Render an OBJ mesh to a PNG as three orthographic views with simple
per-triangle depth shading, so we can eyeball that the geometry is a real level."""
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection

def load_obj(path):
    verts, faces = [], []
    with open(path) as f:
        for line in f:
            if line.startswith("v "):
                _, x, y, z = line.split()
                verts.append((float(x), float(y), float(z)))
            elif line.startswith("f "):
                idx = [int(p.split("/")[0]) - 1 for p in line.split()[1:]]
                faces.append(idx[:3])
    return np.array(verts), np.array(faces, dtype=np.int64)

def view(ax, V, F, a, b, depth, title):
    tris = V[F]                          # (N,3,3)
    poly = tris[:, :, [a, b]]            # project onto axes a,b -> (N,3,2)
    d = tris[:, :, depth].mean(axis=1)   # mean depth per tri for shading
    order = np.argsort(d)                # painter's algorithm: far -> near
    poly, d = poly[order], d[order]
    dn = (d - d.min()) / (np.ptp(d) + 1e-9)
    shade = 0.25 + 0.6 * dn              # nearer = lighter
    colors = np.stack([shade * 0.5, shade * 0.55, shade * 0.7, np.ones_like(shade)], 1)
    pc = PolyCollection(poly, facecolors=colors, edgecolors=(1, 1, 1, 0.05), linewidths=0.15)
    ax.add_collection(pc)
    ax.autoscale()
    ax.set_aspect("equal")
    ax.set_facecolor("#0a0a12")
    ax.set_title(title, color="#ccc", fontsize=10)
    ax.tick_params(colors="#555", labelsize=6)

def main(obj_path, png_path):
    V, F = load_obj(obj_path)
    fig, axes = plt.subplots(1, 3, figsize=(21, 7), facecolor="#0a0a12")
    # axes indices: 0=X, 1=Y(up), 2=Z
    view(axes[0], V, F, 0, 2, 1, "TOP  (X-Z, looking down)")
    view(axes[1], V, F, 0, 1, 2, "FRONT  (X-Y)")
    view(axes[2], V, F, 2, 1, 0, "SIDE  (Z-Y)")
    fig.suptitle(f"{obj_path}   |   {len(V):,} verts  {len(F):,} tris",
                 color="#aaa", fontsize=12)
    fig.tight_layout()
    fig.savefig(png_path, dpi=90, facecolor="#0a0a12")
    print("wrote", png_path)

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
