"""Calibrate the dynamic LightRig against VtMB's baked lightmaps (NOT a bake).

VRAD produced the baked lighting in lump 8 from the light sources in lump 15. That
baked result is the ground truth for how VtMB actually lit the scene. This tool reads
both and *fits the dynamic-light model's free parameters* to it, so the real-time rig
reproduces the baked look mathematically instead of by eye:

  falloff exponent p, intensity scale a, ambient floor b, effective reach.

Per lit face f we take the mean baked luminance y_f (ground truth) at its centroid
c_f with outward normal n_f. For a trial exponent p the *unscaled* predicted direct
irradiance is

    E_f(p) = sum_i  mag_i * max(0, n_f . Lhat_if) / d_if(m)^p       (over worldlights i)

with a radius cutoff and a spot cone. We linear-fit  y_f ~ a*E_f(p) + b  for a grid of
p and pick the best R^2: a is the direct scale, b the ambient/indirect floor. We also
do a model-free log-log fit of brightness vs distance on faces dominated by a single
nearby light — that measures the falloff exponent directly, with no model assumed.

Caveats (honest): the fit ignores shadow visibility (occluded faces read too bright ->
residual noise) and folds indirect bounce into the flat term b. Both are exactly the
things a flat-ambient dynamic rig can't reproduce, so the residual is itself the signal
for "how much does this map need real GI". Absolute brightness still needs one on-screen
anchor (VtMB overbright + gamma + Unreal's tonemap); the *relative* structure (exponent,
ambient ratio, per-light relative brightness) transfers straight to the LightRig knobs.

Usage:  python probe_light_calibration.py [map_name]   (default sp_tutorial_1)
"""
import sys
import struct
import numpy as np

import install
import bsp as B
import lightmap as LM
from UE_bsp_to_scene import base_material

FS = 104
FE, NE, TI = 36, 40, 42
LUM = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)


def face_geometry(faces, verts, edges, surfedges, fi):
    """Return (centroid, normal, ncorners) in Source inches, or None if degenerate."""
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
    # Newell's method: robust normal for a planar polygon.
    n = np.zeros(3)
    for k in range(len(pts)):
        a, bb = pts[k], pts[(k + 1) % len(pts)]
        n[0] += (a[1] - bb[1]) * (a[2] + bb[2])
        n[1] += (a[2] - bb[2]) * (a[0] + bb[0])
        n[2] += (a[0] - bb[0]) * (a[1] + bb[1])
    ln = np.linalg.norm(n)
    if ln < 1e-6:
        return None
    return c, n / ln, ne


def main(map_name):
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

    # ---- ground truth: per lit face, centroid / normal / mean baked luminance ----
    C, N, Y = [], [], []
    n_faces = len(faces) // FS
    for fi in range(n_faces):
        b = fi * FS
        ti = struct.unpack_from("<h", faces, b + TI)[0]
        if ti < 0:
            continue
        tb = struct.unpack_from("<i", texinfo_l, ti * 72 + 68)[0]
        nid = struct.unpack_from("<i", texdata_l, tb * 32 + 12)[0]
        if base_material(names.get(table[nid], "")).startswith("tools/"):
            continue
        lm = LM.face_lightmap(faces, lighting, fi)
        if lm is None:
            continue
        geo = face_geometry(faces, verts, edges, surfedges, fi)
        if geo is None:
            continue
        c, n, _ = geo
        y = float(lm[0].reshape(-1, 3).mean(axis=0) @ LUM)   # mean baked luminance
        if y <= 0:
            continue
        C.append(c); N.append(n); Y.append(y)
    C = np.array(C); N = np.array(N); Y = np.array(Y)
    print(f"map {map_name}: {len(Y)} lit world faces (ground truth)")
    pc = np.percentile(Y, [5, 25, 50, 75, 95])
    print(f"  baked luminance distribution: 5th {pc[0]:.1f}  25th {pc[1]:.1f}  "
          f"median {pc[2]:.1f}  75th {pc[3]:.1f}  95th {pc[4]:.1f}  "
          f"(spread 95/5 = {pc[4]/max(pc[0],1e-6):.1f}x)")

    # ---- light sources (lump 15) ----
    # read_worldlights applies the engine's own load-time fixups (zero attenuation -> quadratic 1,
    # zero spot exponent -> 1, radius < 1 -> no cutoff), so these are the values the game lit
    # with rather than the bytes on disk (RE-A3).
    wl = B.read_worldlights(data)
    O, MAG, DIR, TYPE, RAD, STOP2 = [], [], [], [], [], []
    for w in wl:
        if w["type"] not in (0, 1, 2):        # point / spot / texlight only (drop sky terms)
            continue
        # De-normalise before fitting (RE-A5). VRAD divides a light's radiance by its own falloff
        # denominator at d = 100 units on the way into lump 15, so `intensity` is a *normalised*
        # number, not an emission: two lights of equal brightness but different attenuation
        # keyvalues land at different intensities. Multiplying it back out gives a quantity that
        # is comparable across lights, which is what a fit against the bake needs. The factor is
        # built from the attenuations the fixups above just rewrote, which is why the two land
        # together.
        mag = float(np.array(w["intensity"], dtype=np.float64) @ LUM) * w["falloff"]
        if mag <= 0:
            continue
        O.append(w["origin"]); MAG.append(mag); DIR.append(w["normal"])
        TYPE.append(w["type"]); RAD.append(w["radius"]); STOP2.append(w["stopdot2"])
    O = np.array(O); MAG = np.array(MAG); DIR = np.array(DIR)
    TYPE = np.array(TYPE); RAD = np.array(RAD); STOP2 = np.array(STOP2)
    print(f"        {len(MAG)} point/spot/texlight sources  "
          f"(mag: min {MAG.min():.0f}  median {np.median(MAG):.0f}  max {MAG.max():.0f})")

    IN_TO_M = 0.0254

    def irradiance(p):
        """E_f(p) for every face, and the nearest-light distance / dominance."""
        E = np.zeros(len(C))
        nearest_d = np.full(len(C), np.inf)
        dominance = np.zeros(len(C))
        for i in range(len(C)):
            L = O - C[i]                                  # (M,3) inches
            d_in = np.linalg.norm(L, axis=1) + 1e-6
            Lhat = L / d_in[:, None]
            ndotl = np.maximum(0.0, Lhat @ N[i])
            d_m = d_in * IN_TO_M
            contrib = MAG * ndotl / np.power(d_m, p)
            # radius cutoff
            contrib = np.where((RAD <= 0) | (d_in <= RAD), contrib, 0.0)
            # spot cone (type 2): drop luxels outside the outer cone
            spot = TYPE == 2
            cos = np.einsum("mj,mj->m", -Lhat, DIR)
            cone_bad = spot & (cos < STOP2)
            contrib = np.where(cone_bad, 0.0, contrib)
            E[i] = contrib.sum()
            j = np.argmax(contrib)
            if contrib[j] > 0:
                nearest_d[i] = d_m[j]
                dominance[i] = contrib[j] / (E[i] + 1e-12)
        return E, nearest_d, dominance

    def fit(y, e):
        """Least-squares y ~ a*e + b; return a, b, R^2."""
        A = np.vstack([e, np.ones_like(e)]).T
        (a, b), *_ = np.linalg.lstsq(A, y, rcond=None)
        pred = a * e + b
        ss_res = float(((y - pred) ** 2).sum())
        ss_tot = float(((y - y.mean()) ** 2).sum())
        return a, b, 1.0 - ss_res / (ss_tot + 1e-12)

    def spearman(y, e):
        """Rank correlation — robust to the huge mag/E outlier range."""
        ry = np.argsort(np.argsort(y)).astype(np.float64)
        re = np.argsort(np.argsort(e)).astype(np.float64)
        ry -= ry.mean(); re -= re.mean()
        return float((ry @ re) / (np.linalg.norm(ry) * np.linalg.norm(re) + 1e-12))

    print("\nexponent sweep  (y_baked ~ a * E(p) + b):")
    print("   p     scale a        ambient b     R^2     Spearman")
    best = None
    for p in (1.0, 1.5, 2.0, 2.5, 3.0):
        E, nd, dom = irradiance(p)
        a, b, r2 = fit(Y, E)
        rho = spearman(Y, E)
        if best is None or abs(rho) > abs(best[4]):
            best = (p, r2, a, b, rho)
        print(f"  {p:4.1f}  {a:12.5g}  {b:12.5g}  {r2:6.3f}   {rho:6.3f}")
    bp, br2, ba, bb, brho = best
    print(f"\nbest model fit: p={bp}  a={ba:.5g}  b={bb:.5g}  R^2={br2:.3f}")

    # ---- model-free: log-log brightness vs distance on single-light-dominated faces ----
    E, nd, dom = irradiance(2.0)
    ythr = np.percentile(Y, 60)                          # brighter half ~ likely unshadowed
    sel = (dom > 0.6) & (Y > ythr) & np.isfinite(nd) & (nd > 0.3)
    if sel.sum() > 20:
        ld = np.log(nd[sel]); ly = np.log(Y[sel])
        slope, icpt = np.polyfit(ld, ly, 1)
        print(f"\nmodel-free falloff (log-luminance vs log-distance, "
              f"{int(sel.sum())} single-light faces):")
        print(f"  slope = {slope:.2f}  ->  empirical exponent p ~ {-slope:.2f}")
    else:
        print(f"\nmodel-free falloff: too few single-light faces ({int(sel.sum())})")

    # ---- visibility test: does adding shadowing explain the contrast? ----
    # If a shadowless model has R^2~0 but the same model *with occlusion* fits well, then
    # the contrast is driven by visibility, not falloff. Occlusion = trace face->light and
    # test whether any sample sits in a solid leaf (CONTENTS_SOLID bit).
    nodes = B.read_lump(data, 5)
    planes = np.frombuffer(B.read_lump(data, 1), dtype=np.float32).reshape(-1, 5)
    leafs = B.read_lump(data, 10)
    LEAF_SZ = 32

    def in_solid(pt):
        li = B.point_leaf(data, pt, nodes, planes)
        return (struct.unpack_from("<i", leafs, li * LEAF_SZ)[0] & 1) != 0

    def visible(c, o, n, samples=8):
        start = c + n * 4.0                      # lift off the surface (avoid self-hit)
        for t in np.linspace(0.06, 0.96, samples):
            if in_solid(start + (o - start) * t):
                return False
        return True

    NS = min(1500, len(C))
    sel_idx = np.linspace(0, len(C) - 1, NS).astype(int)
    Ys = Y[sel_idx]
    ps = (1.0, 2.0)
    Eall = {p: np.zeros(NS) for p in ps}
    Evis = {p: np.zeros(NS) for p in ps}
    print(f"\ntracing occlusion for {NS} sampled faces (top contributors each)...")
    n_traced, n_occluded = 0, 0
    for si, fi in enumerate(sel_idx):
        c, n = C[fi], N[fi]
        L = O - c
        d_in = np.linalg.norm(L, axis=1) + 1e-6
        Lhat = L / d_in[:, None]
        ndotl = np.maximum(0.0, Lhat @ n)
        cos = np.einsum("mj,mj->m", -Lhat, DIR)
        ok = (ndotl > 0) & ((RAD <= 0) | (d_in <= RAD)) & ~((TYPE == 2) & (cos < STOP2))
        idxs = np.where(ok)[0]
        if len(idxs) == 0:
            continue
        d_m = d_in * IN_TO_M
        # cap to the 12 strongest contributors (keeps the trace count bounded)
        strength = MAG[idxs] * ndotl[idxs] / np.power(d_m[idxs], 2.0)
        idxs = idxs[np.argsort(strength)[::-1][:12]]
        for li in idxs:
            vis = visible(c, O[li], n)
            n_traced += 1
            n_occluded += not vis
            for p in ps:
                w = MAG[li] * ndotl[li] / d_m[li] ** p
                Eall[p][si] += w
                if vis:
                    Evis[p][si] += w
    print(f"  traced {n_traced} face->light rays; {n_occluded} occluded "
          f"({100.0 * n_occluded / max(n_traced, 1):.0f}%)  "
          f"mean E(p=2): all {Eall[2.0].mean():.3g} / visible {Evis[2.0].mean():.3g}")

    print("\n  model              p     R^2     Spearman")
    for p in ps:
        a, b, r2n = fit(Ys, Eall[p]); rn = spearman(Ys, Eall[p])
        a, b, r2v = fit(Ys, Evis[p]); rv = spearman(Ys, Evis[p])
        print(f"  no shadow         {p:3.1f}  {r2n:6.3f}   {rn:6.3f}")
        print(f"  + occlusion       {p:3.1f}  {r2v:6.3f}   {rv:6.3f}")
    # verdict on the strongest exponent
    rv_best = max(abs(spearman(Ys, Evis[p])) for p in ps)
    rn_best = max(abs(spearman(Ys, Eall[p])) for p in ps)
    print(f"\n  shadowing lifts rank-correlation {rn_best:.2f} -> {rv_best:.2f}"
          f"  ({'CONFIRMED: shadowing drives the contrast' if rv_best > rn_best + 0.15 else 'inconclusive / indirect bounce also matters'})")

    # ---- what the baked data says (use robust stats; the linear a/b fit above is
    #      dragged by the huge mag/E outliers, so read the distribution + rank, not b) ----
    lo25, hi95 = np.percentile(Y, [25, 95])
    spread = hi95 / max(lo25, 1e-6)
    reach = np.percentile(RAD[RAD > 0] * IN_TO_M, [50, 90]) if (RAD > 0).any() else [0, 0]
    print("\n--- what the baked data says ---")
    print(f"  contrast    : 25th/95th baked luminance = {lo25:.1f} / {hi95:.1f}  ({spread:.0f}x)")
    print(f"  dark median : {np.median(Y):.1f}  (shadows are genuinely dark, not a bright fill)")
    print(f"  distance cor: Spearman |rho| = {abs(brho):.2f}   (~0 => falloff is NOT the driver)")
    print(f"  reach       : authored radius median {reach[0]:.1f} m, 90th {reach[1]:.1f} m "
          f"({int((RAD<=0).sum())}/{len(RAD)} uncapped)")

    shadow_helps = rv_best > rn_best + 0.15
    direct_weak = max(rn_best, rv_best) < 0.2
    print("\n--- implied LightRig direction ---")
    if shadow_helps:
        print("  Occlusion sharply improves the fit -> the contrast is driven by SHADOWING.")
        print("   * Keep real-time shadows on the lights that matter; falloff is secondary.")
    elif direct_weak and spread > 20:
        print("  The contrast is large but a direct-light model explains ~nothing EVEN WITH")
        print(f"  correct occlusion ({100.0*n_occluded/max(n_traced,1):.0f}% of rays blocked). So it is not falloff and not")
        print("  direct shadowing -> it is INDIRECT BOUNCE (VRAD radiosity), which is spatially")
        print("  smooth and decorrelated from the point/spot source positions. Therefore:")
        print("   * The look fundamentally needs global illumination (bounce) -> Lumen is")
        print("     load-bearing here, NOT overkill. The dynamic lights feed it; the bounce")
        print("     carries the mood.")
        print("   * FalloffExponent -> flat/gentle (~1); it barely matters (confirmed).")
        print("   * Direct lights should be a modest contributor over the GI base; let Lumen")
        print("     do the fill instead of a big flat ambient.")
    else:
        print("  Direct-light structure is present -> per-light falloff/scale is worth fitting.")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "sp_tutorial_1")
