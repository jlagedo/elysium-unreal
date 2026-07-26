"""RE-A5: what did VRAD do with the `light_environment` sky pair at bake time? (K6)

VtMB ships no map compiler, so there is no rad binary to decompile -- the bake is
only observable through its output. This probe reads that output directly: it
reconstructs every lightmap luxel's world position, measures how much sky each one
can actually see, and asks what lump 8 does with it.

Three measurements:

  1. **The transfer, algebraically.** Every `light`/`light_spot`/`light_environment`
     keyvalue against the `dworldlight_t` row at the same origin. No tracing, and it
     comes out exact game-wide (see the comment on `vrad_intensity`), which fixes
     what a compiled intensity means and -- with the x255 storage scale -- converts
     any worldlight into the lump-8 luxel value it can contribute.

  2. **The sun, as the scale and the control.** Its rule is not in doubt: trace
     toward it, add `intensity*cos` on a sky hit. Dividing a sun-visible luxel by
     cos removes the geometry, so the population must pile up on `255*|intensity|`
     AND its bright end must carry the SUN's chromaticity. The colour half is what
     no confound can fake -- if "sky-visible luxels are brighter" were only the
     author's outdoor lamps, the bright end would be the colour of those lamps.

  3. **The skyambient, by the same two signatures.** Fit baked RGB against measured
     sky visibility over sun-shadowed, fill-free luxels; read the slope's magnitude
     against `255*intensity` and its direction against `_ambient` vs the fill. The
     colour comes back positive; the magnitude is confounded, and the write-up says
     so rather than reporting a number the data does not support.

The luxel positions are reconstructed from the face's `lightmapVecs` and plane and
kept only where they fall inside the face polygon; sky visibility is a point trace
against the map's own solid brushes, classified by `SURF_SKY` (0x4) on the
brushside's texinfo -- the same surface flag the engine's light cache tests.

Provenance matters and is enforced: the Unofficial Patch **recompiles 20 maps and
adds 7 of its own**, so their lump 8 is a modern VRAD's output, not Troika's. This
probe reads the *retail* BSP by default and refuses a patch-recompiled bake unless
asked (`--patch`). `--provenance` prints the full split.

Usage:
    python tools/probe_skyambient.py                     # ch_fishmarket_1
    python tools/probe_skyambient.py ch_temple_1 sp_taxiride
    python tools/probe_skyambient.py --all               # every retail sky-pair map
    python tools/probe_skyambient.py --provenance        # which bakes are Troika's
"""
import argparse
import json
import math
import os
import re
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bsp as B
import install

SURF_SKY = 0x4
CONTENTS_SOLID = 0x1
LUM = np.array([0.2126, 0.7152, 0.0722])

OUT_DIR = Path(__file__).resolve().parent / "out" / "_skyambient"


# --------------------------------------------------------------------------- #
# provenance: whose bake is this?
# --------------------------------------------------------------------------- #

def bsp_paths(name):
    """(patch_path or None, retail_path or None) for a map name."""
    p = os.path.join(install.GAME_ROOT, "Unofficial_Patch", "maps", name + ".bsp")
    r = os.path.join(install.GAME_ROOT, "Vampire", "maps", name + ".bsp")
    return (p if os.path.exists(p) else None, r if os.path.exists(r) else None)


def bake_fingerprint(path):
    """(nfaces, len(lump8), maprevision) -- enough to tell two bakes apart."""
    data = open(path, "rb").read()
    return (len(B.read_lump(data, 7)) // B.FACE_SIZE, len(B.read_lump(data, 8)),
            struct.unpack_from("<i", data, 8 + 64 * 16)[0])


def provenance(name):
    """'retail' | 'patch-recompiled' | 'patch-only'."""
    p, r = bsp_paths(name)
    if r is None:
        return "patch-only"
    if p is None:
        return "retail"
    return "retail" if bake_fingerprint(p) == bake_fingerprint(r) else "patch-recompiled"


def load(name, prefer_patch=False):
    """BSP bytes + which build they came from."""
    p, r = bsp_paths(name)
    prov = provenance(name)
    path = p if (prefer_patch or r is None) else r
    return open(path, "rb").read(), prov, path


# --------------------------------------------------------------------------- #
# entity / worldlight side
# --------------------------------------------------------------------------- #

def entities(data):
    text = B.read_lump(data, 0).split(b"\x00")[0].decode("latin-1")
    return [dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', b))
            for b in re.findall(r"\{([^{}]*)\}", text, re.S)]


def light_key(value):
    """`_light` / `_ambient` "R G B brightness" -> (rgb255[3], brightness) or None."""
    if value is None:
        return None
    parts = value.replace(",", " ").split()
    try:
        nums = [float(x) for x in parts[:4]]
    except ValueError:
        return None
    if len(nums) < 3:
        return None
    return np.array(nums[:3]), (nums[3] if len(nums) > 3 else 255.0)


# VRAD's keyvalue -> dworldlight_t.intensity transfer. Verified with zero exceptions
# on all 16,378 origin-matched lights of all 108 maps (worst relative error 2.25e-7,
# i.e. float32):
#
#   intensity = (colour/255)^2.2 * (brightness/255) * (const + 100*linear + 10000*quad)
#
# Gamma 2.2 on the colour, linear on the brightness, and the third factor is the
# light's own falloff denominator evaluated at d = 100 units. Since the engine divides
# by that same denominator, the compiled intensity is the light's radiance at 100 units
# (2.54 m), whatever its falloff order.
#
# Lump 8 stores that radiance scaled by 255 (the RGBE mantissa is a 0..255 byte), so
#
#   stored luxel = LUXEL_SCALE * intensity / (const + lin*d + quad*d^2)
#
# and a light of brightness B lands a stored luxel of `(colour/255)^2.2 * B` at 100
# units -- the authored brightness, back out of the bake. The sky pair carries no
# falloff (attn is 0,0,0), so its ceiling in luxel units is exactly that. LUXEL_SCALE
# is confirmed by the sun: on the maps whose sun dominates, sun-visible luxels divided
# by cos pile up at 255*|intensity| (sp_endsequences_b: p50/p90/p99 = 170.3/170.4/170.6
# against a prediction of 168.2) and carry the sun's own chromaticity, not the fill's.
LUXEL_SCALE = 255.0


def vrad_intensity(rgb255, brightness, attn=(0.0, 0.0, 0.0), gamma=2.2):
    denom = attn[0] + 100.0 * attn[1] + 10000.0 * attn[2]
    return (np.power(np.clip(rgb255, 0, None) / 255.0, gamma)
            * (brightness / 255.0) * (denom or 1.0))


def chroma(v):
    """Normalise an RGB triple to chromaticity, so magnitude drops out."""
    v = np.asarray(v, dtype=np.float64)
    total = v.sum()
    return v / total if total > 0 else v


def luxel_ceiling(intensity):
    """A sky-pair worldlight's intensity as the stored-luxel value it can produce."""
    return np.asarray(intensity, dtype=np.float64) * LUXEL_SCALE


# --------------------------------------------------------------------------- #
# geometry
# --------------------------------------------------------------------------- #

def read_planes(data):
    return np.frombuffer(B.read_lump(data, 1), dtype=np.float32).reshape(-1, 5).astype(np.float64)


def world_leaves(data):
    """Leaf indices reachable from model 0's headnode (i.e. the world, not brush models)."""
    nodes = B.read_lump(data, 5)
    out, stack = [], [0]
    while stack:
        n = stack.pop()
        if n < 0:
            out.append(-n - 1)
            continue
        c0, c1 = struct.unpack_from("<ii", nodes, n * B.NODE_SIZE + B.ND_CHILDREN)
        stack.append(c0)
        stack.append(c1)
    return out


def leaf_of_points(data, pts):
    """Leaf index of every point in (K,3), by one vectorised descent of model 0."""
    nd = np.frombuffer(B.read_lump(data, 5), dtype=np.int32).reshape(-1, 8)
    planenum, ch = nd[:, 0], nd[:, 1:3]
    planes = read_planes(data)
    cur = np.zeros(len(pts), dtype=np.int64)
    live = np.ones(len(pts), dtype=bool)
    while live.any():
        n = cur[live]
        pn = planenum[n]
        d = (pts[live] * planes[pn, :3]).sum(axis=1) - planes[pn, 3]
        cur[live] = np.where(d > 0, ch[n, 0], ch[n, 1])
        live = cur >= 0
    return (-cur - 1).astype(np.int64)


def in_real_space(data, pts):
    """True where a point sits in open, PVS-clustered space.

    A luxel can sit on the back of a wall or on the outside of the sealed world,
    facing the void between the map and its skybox shell. Those faces bake to zero
    and see 'sky' in most of their hemisphere, so they read as a lower-bound
    violation on every additive rule while telling us nothing about the bake. The
    engine's own test for "is this real space" is the leaf: not CONTENTS_SOLID, and
    carrying a vis cluster.
    """
    leafs = B.read_lump(data, 10)
    li = leaf_of_points(data, pts)
    raw = np.frombuffer(leafs, dtype=np.int32).reshape(-1, 8)
    contents = raw[:, 0]
    cluster = np.frombuffer(leafs, dtype=np.int16).reshape(-1, 16)[:, 2]
    return ((contents[li] & CONTENTS_SOLID) == 0) & (cluster[li] >= 0)


class BrushTracer:
    """Point-segment trace against the map's convex solid brushes.

    Brushes are the collision description VRAD's own visibility tests ran against,
    and every brushside carries a texinfo, so one trace answers both "is the ray
    blocked" and "was it blocked *by sky*". Bevel sides are dropped: they are axial
    planes tangent to the hull added for box sweeps, so they never cut a point trace
    and their texinfo is not a real surface.
    """

    def __init__(self, data, world_only=True):
        planes = read_planes(data)
        brushes = np.frombuffer(B.read_lump(data, 18), dtype=np.int32).reshape(-1, 3)
        sides = np.frombuffer(B.read_lump(data, 19), dtype=np.int16).reshape(-1, 4)
        side_plane = sides[:, 0].astype(np.uint16).astype(np.int32)
        side_ti = sides[:, 1].astype(np.int32)
        side_bevel = sides[:, 3].astype(np.int32)
        ti_lump = B.read_lump(data, 6)
        ti_flags = np.frombuffer(ti_lump, dtype=np.uint8).reshape(-1, 72)[:, 64:68]
        ti_flags = ti_flags.copy().view(np.int32).reshape(-1)

        leafs = B.read_lump(data, 10)
        leafbrushes = np.frombuffer(B.read_lump(data, 17), dtype=np.uint16)
        keep = np.zeros(len(brushes), dtype=bool)
        lo = np.full((len(brushes), 3), np.inf)
        hi = np.full((len(brushes), 3), -np.inf)
        leaf_ids = world_leaves(data) if world_only else range(len(leafs) // B.LEAF_SIZE)
        for li in leaf_ids:
            base = li * B.LEAF_SIZE
            fb, nb = struct.unpack_from("<HH", leafs, base + B.LF_FIRSTBRUSH)
            if not nb:
                continue
            mins = np.array(struct.unpack_from("<3h", leafs, base + 8), dtype=np.float64)
            maxs = np.array(struct.unpack_from("<3h", leafs, base + 14), dtype=np.float64)
            for k in range(fb, fb + nb):
                b = int(leafbrushes[k])
                keep[b] = True
                lo[b] = np.minimum(lo[b], mins)
                hi[b] = np.maximum(hi[b], maxs)
        keep &= (brushes[:, 2] & CONTENTS_SOLID) != 0

        idx = np.nonzero(keep)[0]
        counts = []
        for b in idx:
            fs, ns = int(brushes[b, 0]), int(brushes[b, 1])
            counts.append(int((side_bevel[fs:fs + ns] == 0).sum()))
        jmax = max(counts) if counts else 1
        nb = len(idx)
        self.N = np.zeros((nb, jmax, 3))
        self.D = np.zeros((nb, jmax))
        self.V = np.zeros((nb, jmax), dtype=bool)
        self.SKY = np.zeros((nb, jmax), dtype=bool)
        for i, b in enumerate(idx):
            fs, ns = int(brushes[b, 0]), int(brushes[b, 1])
            sel = [s for s in range(fs, fs + ns) if side_bevel[s] == 0]
            for j, s in enumerate(sel):
                pn = side_plane[s]
                self.N[i, j] = planes[pn, :3]
                self.D[i, j] = planes[pn, 3]
                self.V[i, j] = True
                t = side_ti[s]
                self.SKY[i, j] = t >= 0 and bool(ti_flags[t] & SURF_SKY)
        self.lo = lo[idx].astype(np.float32)
        self.hi = hi[idx].astype(np.float32)
        self.n_brushes = nb
        self.n_sky_brushes = int(self.SKY.any(axis=1).sum())

    def trace(self, origins, dirs, tmax, batch=256):
        """(R,3) origins + unit dirs -> (blocked bool[R], sky bool[R]).

        `tmax` is a scalar or a per-ray array, so the same call serves an open ray
        (sky visibility) and a bounded segment (luxel -> light occlusion).

        `sky` is True when the nearest solid the ray meets inside `tmax` is a sky
        surface, i.e. the ray escapes the map the way VRAD's sky test wanted.
        """
        R = len(origins)
        blocked = np.zeros(R, dtype=bool)
        sky = np.zeros(R, dtype=bool)
        if self.n_brushes == 0:
            return blocked, sky
        tmax_arr = np.broadcast_to(np.asarray(tmax, dtype=np.float64), (R,))
        for s in range(0, R, batch):
            e = min(R, s + batch)
            o = origins[s:e]
            u = dirs[s:e]
            tm = tmax_arr[s:e]
            # --- slab cull against the per-brush AABB -------------------------
            inv = 1.0 / np.where(np.abs(u) < 1e-12, 1e-12, u)
            t1 = (self.lo[None, :, :] - o[:, None, :].astype(np.float32)) * inv[:, None, :].astype(np.float32)
            t2 = (self.hi[None, :, :] - o[:, None, :].astype(np.float32)) * inv[:, None, :].astype(np.float32)
            tnear = np.minimum(t1, t2).max(axis=2)
            tfar = np.maximum(t1, t2).min(axis=2)
            cand = (tnear <= tfar) & (tfar > 0.0) & (tnear < tm[:, None])
            ri, bi = np.nonzero(cand)
            if len(ri) == 0:
                continue
            # --- exact convex clip on the surviving (ray, brush) pairs ---------
            Np, Dp, Vp, Sp = self.N[bi], self.D[bi], self.V[bi], self.SKY[bi]
            a = np.einsum("kjc,kc->kj", Np, u[ri])
            c = np.einsum("kjc,kc->kj", Np, o[ri]) - Dp
            eps = 1e-9
            ent = np.where(Vp & (a < -eps), -c / np.where(a == 0, 1, a), -np.inf)
            ext = np.where(Vp & (a > eps), -c / np.where(a == 0, 1, a), np.inf)
            tenter = ent.max(axis=1)
            jenter = ent.argmax(axis=1)
            texit = ext.min(axis=1)
            parallel_out = (Vp & (np.abs(a) <= eps) & (c > eps)).any(axis=1)
            hit = (~parallel_out) & (tenter <= texit) & (texit > 0.0) & (tenter < tm[ri])
            t = np.where(tenter > 0.0, tenter, 0.0)
            t = np.where(hit, t, np.inf)
            if not np.isfinite(t).any():
                continue
            order = np.lexsort((t, ri))
            first = np.ones(len(order), dtype=bool)
            first[1:] = ri[order][1:] != ri[order][:-1]
            win = order[first]
            good = np.isfinite(t[win])
            win = win[good]
            rr = ri[win] + s
            blocked[rr] = True
            sky[rr] = Sp[win, jenter[win]]
        return blocked, sky


def hemisphere(n_dirs):
    """Directions uniform over solid angle on the +Z hemisphere, plus their cosines."""
    i = np.arange(n_dirs) + 0.5
    cz = 1.0 - i / n_dirs                       # uniform in cos(theta) -> uniform solid angle
    sz = np.sqrt(np.clip(1.0 - cz * cz, 0, 1))
    phi = i * math.pi * (3.0 - math.sqrt(5.0))
    return np.stack([sz * np.cos(phi), sz * np.sin(phi), cz], axis=1), cz


def basis_for(n):
    """Orthonormal frame with +Z along n, for each normal in (K,3)."""
    a = np.tile(np.array([0.0, 0.0, 1.0]), (len(n), 1))
    flip = np.abs(n[:, 2]) > 0.9
    a[flip] = np.array([1.0, 0.0, 0.0])
    t = np.cross(a, n)
    t /= np.linalg.norm(t, axis=1, keepdims=True)
    b = np.cross(n, t)
    return t, b


# --------------------------------------------------------------------------- #
# faces + luxels
# --------------------------------------------------------------------------- #

def sky_area(data):
    """BSP area holding the 3D-skybox miniature (RE-A8), or None."""
    sc = [e for e in entities(data) if e.get("classname") == "sky_camera"]
    if not sc:
        return None
    o = [float(x) for x in sc[0].get("origin", "0 0 0").split()]
    leafs = B.read_lump(data, 10)
    li = B.point_leaf(data, o)
    return struct.unpack_from("<H", leafs, li * B.LEAF_SIZE + 6)[0] & 0x1FF


def area_faces(data, area):
    """Model-0 face indices whose leaf carries `area` (the miniature's faces)."""
    if area is None:
        return set()
    leafs = B.read_lump(data, 10)
    leaffaces = np.frombuffer(B.read_lump(data, 16), dtype=np.uint16)
    out = set()
    for li in world_leaves(data):
        base = li * B.LEAF_SIZE
        if (struct.unpack_from("<H", leafs, base + 6)[0] & 0x1FF) != area:
            continue
        ff, nf = struct.unpack_from("<HH", leafs, base + B.LF_FIRSTFACE)
        out.update(int(leaffaces[k]) for k in range(ff, ff + nf))
    return out


def collect_luxels(data, rng, max_luxels, per_face):
    """Sample luxels across the map's lit world faces.

    Returns dict of arrays: pos (K,3) source inches, nrm (K,3), rgb (K,3) linear
    baked light, face (K,), plus the face bookkeeping the report needs.
    """
    faces = B.read_lump(data, 7)
    lighting = B.read_lump(data, 8)
    ti_lump = B.read_lump(data, 6)
    td_lump = B.read_lump(data, 2)
    names = B.strings_from_blob(B.read_lump(data, 43))
    table = list(struct.unpack_from("<%di" % (len(B.read_lump(data, 44)) // 4),
                                    B.read_lump(data, 44), 0))
    planes = read_planes(data)
    verts = np.frombuffer(B.read_lump(data, 3), dtype=np.float32).reshape(-1, 3).astype(np.float64)
    edges = np.frombuffer(B.read_lump(data, 12), dtype=np.uint16).reshape(-1, 2)
    surfedges = np.frombuffer(B.read_lump(data, 13), dtype=np.int32)

    skip = area_faces(data, sky_area(data))
    nf = len(faces) // B.FACE_SIZE

    P, N, C, F = [], [], [], []
    n_faces_used = 0
    for fi in range(nf):
        if fi in skip:
            continue
        b = fi * B.FACE_SIZE
        ti = struct.unpack_from("<h", faces, b + B.TI_OFS)[0]
        if ti < 0:
            continue
        if struct.unpack_from("<h", faces, b + B.DISP_OFS)[0] >= 0:
            continue                                    # displacement luxels are off-plane
        flags = struct.unpack_from("<i", ti_lump, ti * 72 + 64)[0]
        if flags & (SURF_SKY | 0x80 | 0x400):           # sky / nodraw / trigger-ish
            continue
        tb = struct.unpack_from("<i", ti_lump, ti * 72 + 68)[0]
        nid = struct.unpack_from("<i", td_lump, tb * 32 + 12)[0]
        if names.get(table[nid], "").upper().startswith("TOOLS/"):
            continue
        lo = struct.unpack_from("<i", faces, b + B.FACE_LIGHTOFS)[0]
        if lo < 0:
            continue
        sx, sy = struct.unpack_from("<ii", faces, b + B.FACE_LM_SIZE)
        mn0, mn1 = struct.unpack_from("<ii", faces, b + B.FACE_LM_MINS)
        w, h = sx + 1, sy + 1
        if w < 1 or h < 1:
            continue
        need = w * h * 4
        if lo + need > len(lighting):
            continue

        # --- face polygon + outward normal --------------------------------
        fe = struct.unpack_from("<i", faces, b + B.FE_OFS)[0]
        ne = struct.unpack_from("<h", faces, b + B.NE_OFS)[0]
        if ne < 3:
            continue
        se = surfedges[fe:fe + ne]
        vi = np.where(se >= 0, edges[np.abs(se), 0], edges[np.abs(se), 1])
        poly = verts[vi]
        pn = struct.unpack_from("<H", faces, b + 32)[0]
        side = faces[b + 34]
        nrm = planes[pn, :3] * (-1.0 if side else 1.0)

        # --- luxel world positions from the lightmap basis -----------------
        lmS = np.frombuffer(ti_lump[ti * 72 + B.TI_LMVEC_S:ti * 72 + B.TI_LMVEC_S + 16],
                            dtype=np.float32).astype(np.float64)
        lmT = np.frombuffer(ti_lump[ti * 72 + B.TI_LMVEC_T:ti * 72 + B.TI_LMVEC_T + 16],
                            dtype=np.float32).astype(np.float64)
        A = np.array([lmS[:3], lmT[:3], planes[pn, :3]])
        if abs(np.linalg.det(A)) < 1e-9:
            continue
        Ainv = np.linalg.inv(A)

        # sample up to `per_face` luxels, spread over the grid
        k = min(per_face, w * h)
        pick = np.unique(np.linspace(0, w * h - 1, k).astype(int))
        gy, gx = np.divmod(pick, w)
        rhs = np.stack([gx + mn0 - lmS[3], gy + mn1 - lmT[3],
                        np.full(len(pick), planes[pn, 3])], axis=1)
        pts = rhs @ Ainv.T

        # keep only luxels that actually land inside the face polygon
        t_ax, b_ax = basis_for(nrm[None, :])
        t_ax, b_ax = t_ax[0], b_ax[0]
        p2 = np.stack([(poly - poly[0]) @ t_ax, (poly - poly[0]) @ b_ax], axis=1)
        q2 = np.stack([(pts - poly[0]) @ t_ax, (pts - poly[0]) @ b_ax], axis=1)
        inside = point_in_poly(q2, p2)
        if not inside.any():
            continue

        lm = np.frombuffer(lighting[lo:lo + need], dtype=np.uint8).reshape(-1, 4).astype(np.float64)
        rgb = lm[:, :3] * np.power(2.0, lm[:, 3].astype(np.int8).astype(np.float64))[:, None]
        P.append(pts[inside])
        N.append(np.tile(nrm, (int(inside.sum()), 1)))
        C.append(rgb[pick][inside])
        F.append(np.full(int(inside.sum()), fi))
        n_faces_used += 1

    if not P:
        return None
    P = np.concatenate(P); N = np.concatenate(N)
    C = np.concatenate(C); F = np.concatenate(F)
    if len(P) > max_luxels:
        sel = rng.choice(len(P), max_luxels, replace=False)
        P, N, C, F = P[sel], N[sel], C[sel], F[sel]
    return {"pos": P, "nrm": N, "rgb": C, "face": F, "n_faces": n_faces_used}


def point_in_poly(q, poly):
    """(K,2) points vs one convex-or-not (M,2) polygon -- even-odd crossing test."""
    x, y = q[:, 0], q[:, 1]
    inside = np.zeros(len(q), dtype=bool)
    m = len(poly)
    for i in range(m):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % m]
        cond = ((y1 > y) != (y2 > y))
        with np.errstate(divide="ignore", invalid="ignore"):
            xint = (x2 - x1) * (y - y1) / np.where(y2 == y1, 1e-30, y2 - y1) + x1
        inside ^= cond & (x < xint)
    return inside


# --------------------------------------------------------------------------- #
# the measurement
# --------------------------------------------------------------------------- #

def measure(name, args):
    data, prov, path = load(name, args.patch)
    if prov != "retail" and not args.patch and not args.allow_patch:
        print("  %-20s SKIPPED: bake is %s (not Troika's VRAD). --allow-patch to force."
              % (name, prov))
        return None

    ents = entities(data)
    envs = [e for e in ents if e.get("classname") == "light_environment"]
    wl = B.read_worldlights(data)
    suns = [w for w in wl if w["type"] == 3]
    ambs = [w for w in wl if w["type"] == 5]
    if not suns and not ambs:
        print("  %-20s SKIPPED: no sky pair" % name)
        return None

    rep = {"map": name, "provenance": prov, "bsp": os.path.basename(os.path.dirname(path)),
           "n_light_environment": len(envs), "n_sun": len(suns), "n_skyambient": len(ambs)}

    print("=" * 78)
    print("%s   (%s bake, %d light_environment, %d sun / %d skyambient worldlights)"
          % (name, prov, len(envs), len(suns), len(ambs)))
    print("=" * 78)

    # ---- 1a. keyvalue -> intensity transfer (algebraic, no tracing) ---------
    print("\n-- VRAD's keyvalue -> dworldlight_t.intensity transfer --")
    xfer = []
    for kind, key, rows in (("sun", "_light", suns), ("skyambient", "_ambient", ambs)):
        if not rows or not envs:
            continue
        parsed = light_key(envs[0].get(key))
        if parsed is None:
            continue
        rgb, br = parsed
        pred = vrad_intensity(rgb, br, rows[0]["attn"])
        got = np.array(rows[0]["intensity"])
        err = np.abs(pred - got).max()
        print("   %-11s %-18s -> pred (%.6f, %.6f, %.6f)  lump15 (%.6f, %.6f, %.6f)  max err %.2e"
              % (kind, '"%s"' % envs[0].get(key, ""), *pred, *got, err))
        xfer.append({"kind": kind, "key": envs[0].get(key), "pred": pred.tolist(),
                     "actual": got.tolist(), "max_err": float(err)})
    # the same law on this map's point/spot lights, whose falloff denominator is not 1
    worst, n_chk = 0.0, 0
    wl_all = B.read_worldlights(data)
    W = np.array([w["origin"] for w in wl_all]) if wl_all else np.zeros((0, 3))
    for e in ents:
        if e.get("classname") not in ("light", "light_spot") or "_light" not in e:
            continue
        parsed = light_key(e.get("_light"))
        if parsed is None or "origin" not in e:
            continue
        o = np.array([float(x) for x in e["origin"].split()[:3]])
        d = np.linalg.norm(W - o, axis=1)
        j = int(np.argmin(d))
        if d[j] > 1.0 or wl_all[j]["type"] not in (0, 1, 2):
            continue
        pred = vrad_intensity(parsed[0], parsed[1], wl_all[j]["attn"])
        got = np.array(wl_all[j]["intensity"])
        scale = max(float(np.abs(pred).max()), float(np.abs(got).max()))
        if scale < 1e-9:
            continue
        worst = max(worst, float(np.abs(pred - got).max() / scale))
        n_chk += 1
    if n_chk:
        print("   point/spot  same law, falloff denominator (const + 100*lin + 10000*quad)"
              " in place of 1: %d lights, worst relative error %.2e" % (n_chk, worst))
        rep["point_transfer_check"] = {"n": n_chk, "worst_rel_err": worst}
    rep["transfer"] = xfer

    sun_I = np.array(suns[0]["intensity"]) if suns else np.zeros(3)
    amb_I = np.array(ambs[0]["intensity"]) if ambs else np.zeros(3)
    sun_dir = np.array(suns[0]["normal"]) if suns else np.array([0.0, 0.0, -1.0])
    sun_dir = sun_dir / (np.linalg.norm(sun_dir) or 1.0)
    rep["sun_intensity"] = sun_I.tolist()
    rep["skyambient_intensity"] = amb_I.tolist()

    # ---- geometry + tracing -------------------------------------------------
    tracer = BrushTracer(data, world_only=not args.all_brushes)
    rng = np.random.default_rng(args.seed)
    lux = collect_luxels(data, rng, args.luxels, args.per_face)
    if lux is None:
        print("   no usable luxels")
        return rep
    P, N, C = lux["pos"], lux["nrm"], lux["rgb"]
    real = in_real_space(data, P + N * args.lift)
    n_dropped = int((~real).sum())
    P, N, C = P[real], N[real], C[real]
    lux["face"] = lux["face"][real]
    K = len(P)
    if K < 50:
        print("   too few luxels in real space (%d)" % K)
        return rep
    mins = P.min(axis=0); maxs = P.max(axis=0)
    tmax = float(np.linalg.norm(maxs - mins)) * 3.0 + 4096.0
    print("\n-- geometry --")
    print("   %d solid brushes (%d carry a sky surface), %d faces sampled, %d luxels"
          " (%d dropped: solid or unclustered leaf -- back faces and the void)"
          % (tracer.n_brushes, tracer.n_sky_brushes, lux["n_faces"], K, n_dropped))

    lift = P + N * args.lift

    # sun visibility: one ray toward the sun (normal is the direction light travels)
    toward_sun = np.tile(-sun_dir, (K, 1))
    ndl = np.einsum("kc,kc->k", N, toward_sun)
    blocked, hit_sky = tracer.trace(lift, toward_sun, tmax)
    sun_vis = (ndl > 0.0) & blocked & hit_sky
    rep["sun_ndotl_positive"] = int((ndl > 0).sum())
    rep["sun_visible"] = int(sun_vis.sum())

    # sky visibility over the hemisphere
    dirs_local, cz = hemisphere(args.rays)
    t_ax, b_ax = basis_for(N)
    D = args.rays
    O = np.repeat(lift, D, axis=0)
    W = (t_ax[:, None, :] * dirs_local[None, :, 0, None]
         + b_ax[:, None, :] * dirs_local[None, :, 1, None]
         + N[:, None, :] * dirs_local[None, :, 2, None]).reshape(-1, 3)
    print("   tracing %d sky rays (%d luxels x %d directions)..." % (len(O), K, D))
    bl, sk = tracer.trace(O, W, tmax)
    reach = (bl & sk).reshape(K, D)
    escaped = (~bl).reshape(K, D)               # left the map without hitting anything
    f_uni = reach.mean(axis=1)
    f_cos = (reach * cz[None, :]).sum(axis=1) / cz.sum()
    rep["escaped_rays_frac"] = float(escaped.mean())

    Y = C @ LUM
    print("   sky-visibility: %d luxels fully enclosed (f=0), %d partly open, "
          "%d wide open (f>0.5);  %.2f%% of rays left the map without a hit"
          % (int((f_uni == 0).sum()), int(((f_uni > 0) & (f_uni <= 0.5)).sum()),
             int((f_uni > 0.5).sum()), 100.0 * escaped.mean()))

    # ---- direct-light upper bound: which luxels no point light can reach -----
    # Unoccluded irradiance from every type-0/1/2 worldlight. It is an upper bound
    # (no shadowing), so a luxel with E_max ~ 0 provably received nothing from the
    # author's fill. Those are the only luxels where a sky term could be read off
    # the floor without the fill drowning it.
    O, MAG, DIRW, TYP, RAD, ST2 = [], [], [], [], [], []
    for w in wl:
        if w["type"] not in (0, 1, 2):
            continue
        m = float(np.array(w["intensity"]) @ LUM)
        if m <= 0:
            continue
        O.append(w["origin"]); MAG.append(m); DIRW.append(w["normal"])
        TYP.append(w["type"]); RAD.append(w["radius"]); ST2.append(w["stopdot2"])
    Emax = np.zeros(K)
    if O:
        O = np.array(O); MAG = np.array(MAG); DIRW = np.array(DIRW)
        TYP = np.array(TYP); RAD = np.array(RAD); ST2 = np.array(ST2)
        for i in range(K):
            L = O - P[i]
            d = np.linalg.norm(L, axis=1) + 1e-6
            Lh = L / d[:, None]
            nl = np.maximum(0.0, Lh @ N[i])
            ok = ((RAD <= 0) | (d <= RAD))
            ok &= ~((TYP == 2) & (np.einsum("mj,mj->m", -Lh, DIRW) < ST2))
            Emax[i] = float((MAG * nl / (d * d) * ok).sum())
    dark = Emax <= np.percentile(Emax, args.fill_pct)
    rep["n_luxels"] = K
    rep["fill_free_luxels"] = int(dark.sum())

    # ---- 2. the sun: is the sky pair baked, and at what scale? --------------
    # The sun is the term whose rule is not in doubt (trace toward it, add
    # intensity*cos on a sky hit), so it is what fixes LUXEL_SCALE and proves the
    # bake reads the sky at all. Dividing a sun-visible luxel by cos removes the
    # geometry, so the population should pile up against 255*|intensity| -- and its
    # bright end should carry the SUN's chromaticity. That second half is what no
    # confound can fake: if "sky-visible luxels are brighter" were only the author's
    # outdoor lamps, the bright end would be the colour of those lamps instead.
    sun_c = luxel_ceiling(sun_I)
    amb_c = luxel_ceiling(amb_I)
    sun_y, amb_y = float(sun_c @ LUM), float(amb_c @ LUM)
    fill_rgb = np.array([w["intensity"] for w in wl if w["type"] in (0, 1, 2)])
    fill_chroma = chroma(fill_rgb.sum(axis=0)) if len(fill_rgb) else None
    rep["sun_lum"], rep["ambient_lum"] = sun_y, amb_y

    print("\n-- the sun: is the sky pair in the bake at all? --")
    print("   ceilings in stored-luxel units: sun %.2f, skyambient %.2f  "
          "(intensity x %g)" % (sun_y, amb_y, LUXEL_SCALE))
    print("   %d luxels face the sun (n.L > 0.3); %d of those reach sky along it"
          % (int((ndl > 0.3).sum()), int((sun_vis & (ndl > 0.3)).sum())))
    m = sun_vis & (ndl > 0.3)
    if m.sum() >= 30 and sun_y > 0:
        norm = Y[m] / ndl[m]
        q = np.percentile(norm, [50, 90, 99])
        top = np.nonzero(m)[0][np.argsort(norm)[-max(20, int(m.sum()) // 10):]]
        got = chroma(np.median(C[top], axis=0))
        d_sun = float(np.abs(got - chroma(sun_I)).sum())
        d_fill = float(np.abs(got - fill_chroma).sum()) if fill_chroma is not None else None
        print("   baked/cos over them:  p50 %8.2f   p90 %8.2f   p99 %8.2f"
              % (q[0], q[1], q[2]))
        print("   predicted ceiling     %8.2f            -> p90/pred %.2f"
              % (sun_y, q[1] / sun_y))
        print("   bright-end colour (%.3f, %.3f, %.3f)  vs sun (%.3f, %.3f, %.3f) d=%.3f%s"
              % (*got, *chroma(sun_I), d_sun,
                 ("  vs fill (%.3f, %.3f, %.3f) d=%.3f"
                  % (*fill_chroma, d_fill)) if fill_chroma is not None else ""))
        verdict = (d_fill is None or d_sun < d_fill) and 0.5 <= q[1] / sun_y <= 2.0
        print("   -> the sun %s" % ("IS baked, at intensity x %g, gated by a sky test"
                                    % LUXEL_SCALE if verdict else
                                    "is too dim here for the fill to be separated from it"))
        rep["sun_fit"] = {"n": int(m.sum()), "p50": float(q[0]), "p90": float(q[1]),
                          "p99": float(q[2]), "pred": sun_y, "ratio": float(q[1] / sun_y),
                          "chroma": got.tolist(), "d_sun": d_sun, "d_fill": d_fill,
                          "conclusive": bool(verdict)}
    else:
        print("   too few sun-visible luxels (or the sun is authored black)")
        rep["sun_fit"] = None

    # ---- 3. the skyambient: same two signatures, against sky visibility -----
    # A hemisphere ambient adds amb * w(f). Fit the baked RGB against f over
    # sun-shadowed, fill-free luxels: the slope vector's magnitude tests the
    # weighting and its direction tests the source, exactly as above.
    print("\n-- the skyambient --")
    noSun = (~sun_vis) & dark
    rep["skyambient_fit"] = None
    if amb_y > 0 and noSun.sum() >= 40 and (f_cos[noSun].max() - f_cos[noSun].min()) > 0.2:
        f = f_cos[noSun]
        slope = np.array([np.polyfit(f, C[noSun][:, k], 1)[0] for k in range(3)])
        got = chroma(slope) if slope.sum() > 0 else None
        mag = float(slope @ LUM)
        print("   dRGB/df over %d sun-shadowed, fill-free luxels: (%.2f, %.2f, %.2f)"
              "  luminance %.2f" % (int(noSun.sum()), *slope, mag))
        print("   predicted at full sky:                        (%.2f, %.2f, %.2f)"
              "  luminance %.2f  -> x%.2f"
              % (*amb_c, amb_y, mag / amb_y))
        if got is not None:
            d_amb = float(np.abs(got - chroma(amb_I)).sum())
            d_fill = float(np.abs(got - fill_chroma).sum()) if fill_chroma is not None else None
            print("   slope colour (%.3f, %.3f, %.3f)  vs _ambient (%.3f, %.3f, %.3f) d=%.3f%s"
                  % (*got, *chroma(amb_I), d_amb,
                     ("  vs fill (%.3f, %.3f, %.3f) d=%.3f"
                      % (*fill_chroma, d_fill)) if fill_chroma is not None else ""))
            rep["skyambient_fit"] = {"slope": slope.tolist(), "lum": mag, "pred": amb_y,
                                     "ratio": mag / amb_y, "chroma": got.tolist(),
                                     "d_ambient": d_amb, "d_fill": d_fill,
                                     "n": int(noSun.sum())}
    elif amb_y <= 0:
        print("   _ambient is authored black on this map -- the negative control")
    else:
        print("   not enough sun-shadowed, fill-free luxels spanning sky visibility")

    # per-bin table, for reading the shape rather than the fitted slope
    bins = [(0.0, 1e-9), (1e-9, 0.05), (0.05, 0.15), (0.15, 0.35), (0.35, 1.01)]
    print("   sky visibility | luxels | exact 0 |     min |     p10 |  median |    mean")
    table = []
    for a, b in bins:
        mm = noSun & (f_cos >= a) & (f_cos < b)
        if mm.sum() < 8:
            continue
        y = Y[mm]
        print("   %5.2f .. %-5.2f  | %6d | %7d | %7.3f | %7.3f | %7.3f | %7.3f"
              % (a, min(b, 1.0), int(mm.sum()), int((y <= 0).sum()), y.min(),
                 np.percentile(y, 10), np.median(y), y.mean()))
        table.append({"lo": a, "hi": min(b, 1.0), "n": int(mm.sum()),
                      "n_zero": int((y <= 0).sum()), "min": float(y.min()),
                      "p10": float(np.percentile(y, 10)), "median": float(np.median(y)),
                      "mean": float(y.mean())})
    rep["sky_bins"] = table

    # ---- 4. multiple light_environments ------------------------------------
    if len(suns) > 1 and rep.get("sun_fit") and rep["sun_fit"]["conclusive"]:
        r = rep["sun_fit"]["ratio"]
        print("\n-- %d light_environments: summed or not? --" % len(suns))
        print("   measured/predicted at 1x = %.2f, at %dx = %.2f  -> %s"
              % (r, len(suns), r / len(suns),
                 "one sun" if abs(r - 1) < abs(r / len(suns) - 1) else "summed"))
        rep["multi_env"] = {"n": len(suns), "ratio_1x": r, "ratio_nx": r / len(suns)}

    return rep


def print_provenance():
    names = install.all_map_names()
    groups = {"retail": [], "patch-recompiled": [], "patch-only": []}
    for nm in names:
        groups[provenance(nm)].append(nm)
    print("bake provenance over %d maps" % len(names))
    for k in ("retail", "patch-recompiled", "patch-only"):
        print("\n  %-18s %d maps" % (k, len(groups[k])))
        if k != "retail":
            for nm in groups[k]:
                print("      %s" % nm)
    print("\n  Only the 'retail' group carries Troika's own VRAD bake; the other %d were"
          % (len(groups["patch-recompiled"]) + len(groups["patch-only"])))
    print("  compiled by the Unofficial Patch with a later Source VRAD.")


def inventory():
    """The tracing-free half: how big is the sky pair next to the bake it lands in?

    The transfer above puts `dworldlight_t.intensity` in the same units as a lump-8
    luxel, so the sun's ceiling (normal incidence, full sky) and the skyambient's
    ceiling (full hemisphere) can be quoted directly against that map's own luxel
    distribution. No ray tracing and no modelling -- just the two numbers side by side.
    """
    print("The sky pair's authored ceiling vs the bake it lands in, both in stored-luxel")
    print("units (intensity x %g). `sun` is its value at normal incidence with the sky in"
          % LUXEL_SCALE)
    print("view; `amb` is the skyambient's over a fully open hemisphere. The lump-8")
    print("percentiles are over lit world faces' mean luxel luminance.\n")
    print("%-20s %4s %8s %8s | %7s %7s %7s | %8s %8s" %
          ("map", "env", "sun", "amb", "p50", "p90", "max", "sun/p50", "amb/p50"))
    rows = []
    for nm in install.all_map_names():
        if provenance(nm) != "retail":
            continue
        data, _, _ = load(nm)
        wl = B.read_worldlights(data)
        suns = [w for w in wl if w["type"] == 3]
        ambs = [w for w in wl if w["type"] == 5]
        if not suns and not ambs:
            continue
        envs = len([e for e in entities(data) if e.get("classname") == "light_environment"])
        sun_y = float(luxel_ceiling(suns[0]["intensity"]) @ LUM) if suns else 0.0
        amb_y = float(luxel_ceiling(ambs[0]["intensity"]) @ LUM) if ambs else 0.0

        faces = B.read_lump(data, 7)
        lighting = B.read_lump(data, 8)
        ti_lump = B.read_lump(data, 6)
        vals = []
        for fi in range(len(faces) // B.FACE_SIZE):
            b = fi * B.FACE_SIZE
            ti = struct.unpack_from("<h", faces, b + B.TI_OFS)[0]
            if ti < 0 or (struct.unpack_from("<i", ti_lump, ti * 72 + 64)[0] & (SURF_SKY | 0x400)):
                continue
            lo = struct.unpack_from("<i", faces, b + B.FACE_LIGHTOFS)[0]
            if lo < 0:
                continue
            sx, sy = struct.unpack_from("<ii", faces, b + B.FACE_LM_SIZE)
            n = (sx + 1) * (sy + 1)
            if n <= 0 or lo + n * 4 > len(lighting):
                continue
            a = np.frombuffer(lighting[lo:lo + n * 4], dtype=np.uint8).reshape(-1, 4)
            rgb = a[:, :3].astype(np.float64) * np.power(
                2.0, a[:, 3].astype(np.int8).astype(np.float64))[:, None]
            vals.append(float(rgb.mean(axis=0) @ LUM))
        if not vals:
            continue
        v = np.array(vals)
        p50, p90, mx = np.percentile(v, 50), np.percentile(v, 90), v.max()
        print("%-20s %4d %8.4f %8.4f | %7.2f %7.2f %7.1f | %7.1f%% %7.1f%%" %
              (nm, envs, sun_y, amb_y, p50, p90, mx,
               100 * sun_y / p50 if p50 else 0, 100 * amb_y / p50 if p50 else 0))
        rows.append((nm, envs, sun_y, amb_y, p50, p90, mx))
    if rows:
        a = np.array([[r[2], r[3], r[4]] for r in rows])
        print("\n  %d retail maps carry a sky pair. Sun luminance runs %.4f .. %.4f,"
              % (len(rows), a[:, 0].min(), a[:, 0].max()))
        print("  skyambient %.4f .. %.4f; the median lit face runs %.2f .. %.1f."
              % (a[:, 1].min(), a[:, 1].max(), a[:, 2].min(), a[:, 2].max()))
        ok = a[:, 2] > 0                      # sp_endsequences_b bakes >half its faces black
        for lab, col in (("sun", 0), ("skyambient", 1)):
            sh = 100 * a[ok, col] / a[ok, 2]
            print("  %-11s as a share of that map's median lit face: median %.1f%%,"
                  " max %.1f%% (%d maps)" % (lab, np.median(sh), sh.max(), int(ok.sum())))
        print("  authored to exactly zero: sun on %d maps, skyambient on %d"
              % (int((a[:, 0] == 0).sum()), int((a[:, 1] == 0).sum())))


def sky_pair_maps():
    out = []
    for nm in install.all_map_names():
        if provenance(nm) != "retail":
            continue
        data, _, _ = load(nm)
        wl = B.read_worldlights(data)
        if any(w["type"] in (3, 5) for w in wl):
            out.append(nm)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("maps", nargs="*", default=None)
    ap.add_argument("--all", action="store_true", help="every retail map carrying a sky pair")
    ap.add_argument("--provenance", action="store_true", help="which bakes are Troika's")
    ap.add_argument("--inventory", action="store_true",
                    help="sky-pair magnitude vs the bake, all retail maps (no tracing)")
    ap.add_argument("--patch", action="store_true", help="read the patch BSP instead")
    ap.add_argument("--allow-patch", action="store_true",
                    help="do not skip maps whose bake is not Troika's")
    ap.add_argument("--rays", type=int, default=64, help="hemisphere rays per luxel")
    ap.add_argument("--luxels", type=int, default=30000, help="luxel budget per map")
    ap.add_argument("--per-face", type=int, default=40, help="luxels sampled per face")
    ap.add_argument("--fill-pct", type=float, default=50.0,
                    help="keep luxels below this percentile of unoccluded direct irradiance")
    ap.add_argument("--all-brushes", action="store_true",
                    help="occlude with brush entities too, not just model 0 (conservative)")
    ap.add_argument("--lift", type=float, default=1.0, help="ray start offset along the normal")
    ap.add_argument("--seed", type=int, default=17)
    args = ap.parse_args(argv)

    if args.provenance:
        print_provenance()
        return
    if args.inventory:
        inventory()
        return

    names = args.maps or (sky_pair_maps() if args.all else ["ch_fishmarket_1"])
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    reps = []
    for nm in names:
        rep = measure(nm, args)
        if rep:
            reps.append(rep)
            (OUT_DIR / ("%s.json" % nm)).write_text(json.dumps(rep, indent=2))
            print("\n   -> %s" % (OUT_DIR / ("%s.json" % nm)))
        print()

    if len(reps) > 1:
        summary(reps)


def summary(reps):
    """Cross-map roll-up.

    Two questions, one table. For the sun: does `baked/cos` over sun-visible luxels
    land on 255x its intensity, and does its bright end carry the sun's colour rather
    than the map's fill? For the skyambient: does dRGB/df land on 255x its intensity
    with the `_ambient` colour? A map whose sky term is dimmer than its own fill
    cannot answer either -- the d(sun)/d(fill) columns say which maps are load-bearing.
    """
    print("=" * 104)
    print("CROSS-MAP: is the sky pair in the bake, and at what scale?")
    print("=" * 104)
    print("%-20s %4s | %8s %9s %6s %6s %6s | %8s %9s %6s %6s %6s" %
          ("map", "env", "sun pred", "sun p90", "x/prd", "d(sun)", "d(fil)",
           "amb pred", "amb dY/df", "x/prd", "d(amb)", "d(fil)"))

    def f(v, spec="%.2f"):
        return spec % v if v is not None else "-"

    sun_ok, amb_ok = [], []
    for r in sorted(reps, key=lambda r: -(r.get("sun_lum") or 0)):
        sf = r.get("sun_fit") or {}
        af = r.get("skyambient_fit") or {}
        print("%-20s %4d | %8s %9s %6s %6s %6s | %8s %9s %6s %6s %6s" %
              (r["map"], r["n_light_environment"],
               f(r.get("sun_lum")), f(sf.get("p90")), f(sf.get("ratio")),
               f(sf.get("d_sun"), "%.3f"), f(sf.get("d_fill"), "%.3f"),
               f(r.get("ambient_lum")), f(af.get("lum")), f(af.get("ratio")),
               f(af.get("d_ambient"), "%.3f"), f(af.get("d_fill"), "%.3f")))
        if sf.get("conclusive"):
            sun_ok.append((r["map"], sf["ratio"], r["n_light_environment"]))
        # "separable" needs the slope to land NEAR _ambient, not merely nearer to it
        # than to the fill -- sm_warehouse_1's slope is far from both (1.79 / 2.40)
        # and says nothing about either.
        if (af.get("d_ambient") is not None and af.get("d_fill") is not None
                and af["d_ambient"] < af["d_fill"] and af["d_ambient"] < 0.5):
            amb_ok.append((r["map"], af["ratio"]))

    print("")
    print("  Maps where the sun outshines the fill enough to be identified by its own")
    print("  colour (d(sun) < d(fill)) AND lands within 2x of its predicted ceiling:")
    if sun_ok:
        for nm, r, n in sun_ok:
            print("     %-20s x%.2f  (%d light_environment%s)"
                  % (nm, r, n, "s" if n > 1 else ""))
        rs = np.array([r for _, r, _ in sun_ok])
        print("     -> median %.2f of the 1x prediction. The sun is baked at"
              " intensity x %g," % (np.median(rs), LUXEL_SCALE))
        print("        gated by a sky-visibility test along the sun direction.")
        multi = [(nm, r, n) for nm, r, n in sun_ok if n > 1]
        if multi:
            print("     multi-light_environment maps among them: %s"
                  % ", ".join("%s x%.2f (summing would predict x%.2f)" % (nm, r, r / n)
                              for nm, r, n in multi))
    else:
        print("     none -- on every map measured, the fill outshines the sun")

    print("")
    print("  Maps where the skyambient is separable from the fill by colour"
          " (d(_ambient) < d(fill)):")
    if amb_ok:
        for nm, r in amb_ok:
            print("     %-20s dRGB/df = x%.2f of the predicted ceiling" % (nm, r))
        ra = np.array([r for _, r in amb_ok])
        print("     -> median %.2f. A gather weighting the full hemisphere reads 1.00;"
              " below that is a narrower effective aperture." % np.median(ra))
    else:
        print("     none")


if __name__ == "__main__":
    main()
