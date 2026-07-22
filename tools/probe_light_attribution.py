"""Rebalance the dynamic light rig per map-area against VtMB's baked lightmap (experiment).

Not a physical solve and not "perfect" -- a fine-tuner. Some areas of a map read too
bright or too dark under the dynamic rig relative to how VtMB actually lit them. VRAD's
baked lightmap (lump 8) is the record of the intended per-area brightness, so we use it as
a spatial reference and nudge the lights area-by-area to match its RELATIVE structure.

(An earlier version tried to deconvolve the bake onto individual light sources by NNLS. It
collapsed -- direct kernels are decorrelated from a bounce-dominated bake, so the solver
dumped everything into a flat ambient and railed the per-light weights to the clamp. Per-
*light* attribution is ill-posed. Per-*area* balancing is well-posed, and is what "areas
too bright/dark" actually asks for.)

Method:
  1. Reconstruct the bake as a dense per-luxel 3D cloud (world pos + baked luminance Y_j).
  2. Predict the rig's direct irradiance E_j at each luxel (same kernel the LightRig uses:
     mag * max(0, n.Lhat) / d^P, radius cutoff + spot cone).
  3. Bin luxels into coarse spatial cells. Per cell: ratio = median(Y) / median(E) -- "how
     far off is the rig here". Median-normalise ratios across cells (the Y/E unit + global
     scale cancels; absolute level stays with elysium.LightScale) and clamp gently.
  4. Assign each light its cell's multiplier -> `<map>.lightfit` (one value per `.lights`
     line). The runtime applies it (elysium.LightFit 1): cells the rig over-lights get
     dimmed, cells it under-lights get boosted -- a per-area rebalance, not a per-light one.

Honest caveats: the ratio folds bounce + occlusion into "too dark/bright" (an area dark in
the bake from shadowing will ask for a boost the runtime's Lumen may not want), so keep the
clamp tight and A/B in engine. It rebalances relative areas; it does not set absolute level.

Usage:  python probe_light_attribution.py [map_name] [--exp P] [--cell IN] [--budget N]
"""
import os
import sys
import struct
import numpy as np

import install
import bsp as B
import lightmap as LM
from UE_bsp_to_scene import base_material

FS = 104
FE, NE, TI = 36, 40, 42
FACE_LM_MINS, FACE_LM_SIZE = 80, 88
TI_SIZE, TI_LMVEC_S, TI_LMVEC_T = 72, 32, 48
LUM = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
IN_TO_M = 0.0254
UNCAPPED_REACH_IN = 1500.0
CLAMP = (0.5, 2.0)
MIN_LUXELS_PER_CELL = 30


def face_plane(faces, verts, edges, surfedges, fi):
    """(centroid, unit normal, plane_d = n.centroid) in source inches, or None."""
    b = fi * FS
    fe = struct.unpack_from("<i", faces, b + FE)[0]
    ne = struct.unpack_from("<h", faces, b + NE)[0]
    if ne < 3:
        return None
    pts = []
    for k in range(ne):
        se = surfedges[fe + k]
        v = edges[se][0] if se >= 0 else edges[-se][1]
        pts.append(verts[v])
    pts = np.array(pts, dtype=np.float64)
    c = pts.mean(axis=0)
    n = np.zeros(3)                                    # Newell's method
    for k in range(len(pts)):
        a, bb = pts[k], pts[(k + 1) % len(pts)]
        n[0] += (a[1] - bb[1]) * (a[2] + bb[2])
        n[1] += (a[2] - bb[2]) * (a[0] + bb[0])
        n[2] += (a[0] - bb[0]) * (a[1] + bb[1])
    ln = np.linalg.norm(n)
    if ln < 1e-6:
        return None
    n /= ln
    return c, n, float(n @ c)


def luxel_world_positions(lm_s, lm_t, n, plane_d, mins, size):
    """Invert the Source luxel->world mapping onto the face plane. Returns (pos (w*h,3) in
    col-major order, (w,h)) or None if the 3x3 solve is degenerate."""
    M = np.array([lm_s[:3], lm_t[:3], n], dtype=np.float64)
    if abs(np.linalg.det(M)) < 1e-9:
        return None
    Minv = np.linalg.inv(M)
    w, h = size[0] + 1, size[1] + 1
    cols = np.arange(w) + mins[0]
    rows = np.arange(h) + mins[1]
    ls = np.repeat(cols, h) - lm_s[3]
    lt = np.tile(rows, w) - lm_t[3]
    rhs = np.stack([ls, lt, np.full(ls.shape, plane_d)], axis=1)
    return rhs @ Minv.T, (w, h)


def main(map_name, p_exp=2.0, cell_in=128.0, budget=60000):
    bsp_path = install.map_path(map_name)
    data = open(bsp_path, "rb").read()
    faces = B.read_lump(data, 7)
    lighting = B.read_lump(data, 8)
    verts_l = B.read_lump(data, 3)
    edges_l = B.read_lump(data, 12)
    surf_l = B.read_lump(data, 13)
    texinfo_l = B.read_lump(data, 6)
    texdata_l = B.read_lump(data, 2)
    names = B.strings_from_blob(B.read_lump(data, 43))
    table = list(struct.unpack_from("<%di" % (len(B.read_lump(data, 44)) // 4), B.read_lump(data, 44), 0))
    verts = [struct.unpack_from("<fff", verts_l, i) for i in range(0, len(verts_l), 12)]
    edges = [struct.unpack_from("<HH", edges_l, i) for i in range(0, len(edges_l), 4)]
    surfedges = list(struct.unpack_from("<%di" % (len(surf_l) // 4), surf_l, 0))

    # ---- ground truth: per-luxel world position, face normal, baked luminance ----
    P, Npos, Y = [], [], []
    n_faces = len(faces) // FS
    for fi in range(n_faces):
        b = fi * FS
        ti = struct.unpack_from("<h", faces, b + TI)[0]
        if ti < 0:
            continue
        tb = struct.unpack_from("<i", texinfo_l, ti * TI_SIZE + 68)[0]
        nid = struct.unpack_from("<i", texdata_l, tb * 32 + 12)[0]
        if base_material(names.get(table[nid], "")).startswith("tools/"):
            continue
        lm = LM.face_lightmap(faces, lighting, fi)
        if lm is None:
            continue
        grid, (gw, gh) = lm
        pl = face_plane(faces, verts, edges, surfedges, fi)
        if pl is None:
            continue
        c, n, plane_d = pl
        lm_s = struct.unpack_from("<4f", texinfo_l, ti * TI_SIZE + TI_LMVEC_S)
        lm_t = struct.unpack_from("<4f", texinfo_l, ti * TI_SIZE + TI_LMVEC_T)
        mins = struct.unpack_from("<ii", faces, b + FACE_LM_MINS)
        size = struct.unpack_from("<ii", faces, b + FACE_LM_SIZE)
        if (size[0] + 1) != gw or (size[1] + 1) != gh:
            continue
        res = luxel_world_positions(lm_s, lm_t, n, plane_d, mins, size)
        if res is None:
            continue
        pos, (w, h) = res
        lumin = (grid.reshape(-1, 3) @ LUM)            # (h*w,) row-major (row,col)
        pos = pos.reshape(w, h, 3).transpose(1, 0, 2).reshape(-1, 3)   # -> row-major
        m = lumin > 0
        if not m.any():
            continue
        P.append(pos[m]); Npos.append(np.broadcast_to(n, (m.sum(), 3))); Y.append(lumin[m])
    P = np.concatenate(P); Npos = np.concatenate(Npos); Y = np.concatenate(Y)
    print(f"map {map_name}: {len(Y)} lit luxels from {n_faces} faces (dense 3D ground truth)")
    pc = np.percentile(Y, [5, 50, 95])
    print(f"  baked luminance: 5th {pc[0]:.1f}  median {pc[1]:.1f}  95th {pc[2]:.1f}")

    if len(Y) > budget:
        rng = np.random.default_rng(0)
        idx = rng.choice(len(Y), size=budget, replace=False)
        P, Npos, Y = P[idx], Npos[idx], Y[idx]
        print(f"  subsampled to {budget} luxels")

    # ---- light sources (lump 15), keeping their original .lights line index ----
    wl = B.read_worldlights(data)
    lights = []
    for j, w in enumerate(wl):
        if w["type"] not in (0, 1, 2):
            continue
        mag = float(np.array(w["intensity"], dtype=np.float64) @ LUM)
        if mag <= 0:
            continue
        lights.append((j, np.array(w["origin"], float), mag,
                       np.array(w["normal"], float), w["type"], w["radius"], w["stopdot2"]))
    L = len(lights)
    O = np.array([l[1] for l in lights])
    MAG = np.array([l[2] for l in lights])
    DIR = np.array([l[3] for l in lights])
    TYPE = np.array([l[4] for l in lights])
    RAD = np.array([l[5] for l in lights])
    STOP2 = np.array([l[6] for l in lights])
    print(f"  {L} point/spot/texlight sources")

    # ---- predicted rig direct irradiance E_j at each luxel (LightRig's own kernel) ----
    E = np.zeros(len(Y))
    for i in range(L):
        Lv = O[i] - P
        d_in = np.linalg.norm(Lv, axis=1) + 1e-6
        Lhat = Lv / d_in[:, None]
        ndotl = np.maximum(0.0, np.einsum("kj,kj->k", Lhat, Npos))
        contrib = MAG[i] * ndotl / np.power(d_in * IN_TO_M, p_exp)
        reach = RAD[i] if RAD[i] > 0 else UNCAPPED_REACH_IN
        contrib = np.where(d_in <= reach, contrib, 0.0)
        if TYPE[i] == 2:
            cosang = -np.einsum("kj,j->k", Lhat, DIR[i])
            contrib = np.where(cosang < STOP2[i], 0.0, contrib)
        E += contrib

    # ---- per-cell brightness ratio (baked vs predicted), median-normalised + clamped ----
    cell = np.floor(P / cell_in).astype(np.int64)
    keys = cell[:, 0].astype(np.int64) * 73856093 ^ cell[:, 1] * 19349663 ^ cell[:, 2] * 83492791
    order = np.argsort(keys)
    keys_s, Y_s, E_s = keys[order], Y[order], E[order]
    bounds = np.concatenate([[0], np.where(np.diff(keys_s) != 0)[0] + 1, [len(keys_s)]])
    cell_ratio = {}          # cell-key -> raw ratio  (only for cells the rig actually lights)
    Emed_global = np.median(E[E > 0]) if (E > 0).any() else 1.0
    Efloor = 1e-3 * Emed_global
    for a, bnd in zip(bounds[:-1], bounds[1:]):
        if bnd - a < MIN_LUXELS_PER_CELL:
            continue
        ymed = np.median(Y_s[a:bnd])
        emed = np.median(E_s[a:bnd])
        if emed <= Efloor:                             # bounce-only cell: no direct light to tune
            continue
        cell_ratio[int(keys_s[a])] = ymed / emed
    if not cell_ratio:
        print("  no rig-lit cells with enough luxels -- nothing to tune.")
        return
    ratios = np.array(list(cell_ratio.values()))
    med = np.median(ratios)
    rel = {k: float(np.clip(v / med, *CLAMP)) for k, v in cell_ratio.items()}

    rvals = np.array(list(rel.values()))
    n_dim = int((rvals < 0.8).sum())
    n_boost = int((rvals > 1.25).sum())
    print(f"\nper-area balance over {len(rel)} rig-lit cells (~{cell_in*IN_TO_M:.1f} m each):")
    print(f"  multiplier distribution: 10th {np.percentile(rvals,10):.2f}  "
          f"median {np.median(rvals):.2f}  90th {np.percentile(rvals,90):.2f}")
    print(f"  {n_dim} cells too bright (dimmed <0.8x)   {n_boost} cells too dark (boosted >1.25x)   "
          f"{len(rel)-n_dim-n_boost} ~balanced")

    # ---- assign each light its cell's multiplier ----
    fit = np.ones(L)
    hit = 0
    for i in range(L):
        ck = np.floor(O[i] / cell_in).astype(np.int64)
        key = int(ck[0] * 73856093 ^ ck[1] * 19349663 ^ ck[2] * 83492791)
        if key in rel:
            fit[i] = rel[key]; hit += 1
    print(f"  {hit}/{L} lights fall in a tuned cell (rest keep 1.0)")
    print(f"  per-light fit: {int((fit<0.8).sum())} dimmed, {int((fit>1.25).sum())} boosted, "
          f"{int(((fit>=0.8)&(fit<=1.25)).sum())} unchanged")

    # ---- write <map>.lightfit: one multiplier per .lights line, in order ----
    out_dir = os.path.join("out", map_name)
    if os.path.isdir(out_dir):
        fit_by_line = {lights[i][0]: float(fit[i]) for i in range(L)}
        path = os.path.join(out_dir, map_name + ".lightfit")
        with open(path, "w") as f:
            f.write("# per-light area-balance multiplier, one per .lights line.\n")
            f.write("# from VtMB baked lighting (lump 8) via probe_light_attribution.py.\n")
            for j in range(len(wl)):
                f.write(f"{fit_by_line.get(j, 1.0):.4f}\n")
        print(f"\nwrote {path}  ({len(wl)} lines)")
        print("  runtime: set  elysium.LightFit 1  then re-travel to apply it.")
    else:
        print(f"\n(no out/{map_name}/ -- export the map first to write the .lightfit sidecar)")

    print("\n--- read it as ---")
    print("  * a gentle per-area rebalance clamped to [0.5, 2.0]: over-lit cells dimmed,")
    print("    under-lit cells boosted, toward VtMB's own relative area brightness.")
    print("  * it does NOT set absolute level (elysium.LightScale still does) and folds")
    print("    bounce+occlusion into the ratio -- so A/B it in engine, don't trust it blind.")


if __name__ == "__main__":
    args = sys.argv[1:]
    name = next((a for a in args if not a.startswith("--")), "sp_tutorial_1")
    def opt(flag, default, cast):
        return cast(args[args.index(flag) + 1]) if flag in args else default
    main(name, opt("--exp", 2.0, float), opt("--cell", 128.0, float), opt("--budget", 60000, int))
