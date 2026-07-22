"""Measure map geometry to pick SDFGI cell size / cascade0 distance.

SDFGI leaks indirect light through any wall thinner than ~1 voxel, so the cell
size must beat the thinnest *room-separating* wall. Cascade 0 is a fixed 128-voxel
grid, so SdfgiCascade0Distance = 64 * SdfgiMinCellSize -- picking the cell size
also picks how far the fine cascade reaches. This reads the exported `.hulls`
(convex world brushes, Godot metres) and reports the wall-thickness distribution
and map extent so the trade-off can be made from data.
"""
import glob
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")

# A "wall-like" slab is thin on one axis and broad on the other two -- a trim
# detail or tiny clip brush is not a room divider and may safely leak.
BROAD_RATIO = 3.0     # the two large dims must each be >= 3x the thin one
BROAD_MIN_M = 1.0     # ...and at least this big (metres), i.e. a real surface


def brush_dims(line):
    v = np.array(line.split(), np.float64).reshape(-1, 3)
    return np.sort(v.max(0) - v.min(0))   # [thin, mid, broad]


def analyse(path):
    thin_all, thin_wall, verts_min, verts_max = [], [], None, None
    with open(path) as f:
        for line in f:
            if not line.strip():
                continue
            v = np.array(line.split(), np.float64).reshape(-1, 3)
            lo, hi = v.min(0), v.max(0)
            verts_min = lo if verts_min is None else np.minimum(verts_min, lo)
            verts_max = hi if verts_max is None else np.maximum(verts_max, hi)
            a, b, c = np.sort(hi - lo)
            thin_all.append(a)
            if b >= BROAD_RATIO * a and c >= BROAD_RATIO * a and b >= BROAD_MIN_M:
                thin_wall.append(a)
    return np.array(thin_all), np.array(thin_wall), verts_min, verts_max


def pct(a, p):
    return float(np.percentile(a, p)) if len(a) else float("nan")


def main():
    hulls = sorted(glob.glob(os.path.join(OUT, "*", "*.hulls")))
    print(f"{'map':<16} {'brushes':>7} {'walls':>6}  "
          f"{'wall thickness (m): p1':>22} {'p5':>6} {'p25':>6} {'p50':>6}   "
          f"{'extent (m)':>18}")
    for h in hulls:
        name = os.path.basename(h)[:-6]
        all_t, wall_t, lo, hi = analyse(h)
        ext = hi - lo
        print(f"{name:<16} {len(all_t):>7} {len(wall_t):>6}  "
              f"{pct(wall_t,1):>22.3f} {pct(wall_t,5):>6.3f} "
              f"{pct(wall_t,25):>6.3f} {pct(wall_t,50):>6.3f}   "
              f"{ext[0]:>5.0f}x{ext[1]:>3.0f}x{ext[2]:>3.0f}")


if __name__ == "__main__":
    main()
