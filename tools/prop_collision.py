"""Physics-prop collision: convex decomposition of a decoded prop mesh.

Consumes a prop's already-decoded `props/<stem>.obj` (Unreal cm / Z-up / LH, winding
reversed) and emits `props/<stem>.hulls` in the **same format the world collider uses**
(`UE_bsp_to_scene.write_collision`): one convex hull per line, flat space-separated
Unreal-cm vertex floats (`x y z x y z ...`, >= 4 verts, order irrelevant — the runtime
builds the hull from the point cloud). So a physics prop reads N convex parts exactly the
way the world reads its brush hulls.

Roadmap 8.4 (`prop_physics` Chaos bodies). The acceptance is "convex from render mesh";
a single whole-model hull is a coarse proxy for concave props (chairs, furniture), so this
runs an offline approximate-convex decomposition (CoACD) — deterministic, free at runtime,
the project's exporter-side-merge pattern. CoACD is **optional**: if it is not installed or
fails on a model, we fall back to one whole-model hull line, which is exactly the baseline
spec, so the pipeline never hard-fails on a missing dependency.
"""

import os

# CoACD (https://github.com/SarahWeiii/CoACD, MIT) is an optional dependency — pip wheels
# ship for Windows/Linux/macOS. Absent -> single-hull fallback (see module docstring).
try:
    import coacd
    _HAVE_COACD = True
except Exception:
    _HAVE_COACD = False

# Decomposition tuning, chosen for **pipeline speed** — a collision proxy for clutter, not a
# surgical decomposition. CoACD's default threshold (0.05) with full-res MCTS costs ~150 s on a
# single 2.5k-vert chair (far too slow for a per-map, ~100-map export); these coarser knobs cut
# that ~12× (~10 s) for an 8-hull proxy that is still a big improvement on a single whole-model
# hull. `threshold` is the concavity stop (higher = fewer/looser hulls, faster). The MCTS/voxel
# resolutions are lowered together — the dominant cost. Cap the part count so a pathological model
# can't emit dozens of hulls (each cooks a runtime convex).
_COACD_PARAMS = dict(
    threshold=0.2,
    preprocess_resolution=30,
    resolution=1000,
    mcts_iterations=60,
    mcts_max_depth=2,
    mcts_nodes=15,
    max_convex_hull=12,
)


def _read_obj(obj_path):
    """Read an OBJ's vertex positions and triangle indices (0-based). Positions are taken
    verbatim (already Unreal cm); polygon faces are fan-triangulated; only the position
    index of each `v/vt/vn` corner is used."""
    verts, tris = [], []
    with open(obj_path, "r") as f:
        for line in f:
            if line.startswith("v "):
                p = line.split()
                verts.append((float(p[1]), float(p[2]), float(p[3])))
            elif line.startswith("f "):
                idx = [int(c.split("/")[0]) - 1 for c in line.split()[1:]]
                for k in range(1, len(idx) - 1):     # fan-triangulate
                    tris.append((idx[0], idx[k], idx[k + 1]))
    return verts, tris


def _write_hulls(hulls_path, hulls):
    """Write one hull per line: flat Unreal-cm verts. `hulls` = list of vertex lists."""
    with open(hulls_path, "w") as f:
        for verts in hulls:
            f.write(" ".join(f"{c:.4f}" for v in verts for c in v) + "\n")


def write_prop_hulls(obj_path, hulls_path):
    """Decompose `obj_path` into convex hulls and write `hulls_path`. Returns the hull
    count (1 for the single-hull fallback, 0 if the OBJ has no geometry)."""
    verts, tris = _read_obj(obj_path)
    if len(verts) < 4 or not tris:
        return 0

    if _HAVE_COACD:
        try:
            import numpy as np
            coacd.set_log_level("error")
            mesh = coacd.Mesh(np.array(verts, dtype=np.float64), np.array(tris, dtype=np.int32))
            parts = coacd.run_coacd(mesh, **_COACD_PARAMS)
            hulls = [pv.tolist() for pv, _pf in parts if len(pv) >= 4]
            if hulls:
                _write_hulls(hulls_path, hulls)
                return len(hulls)
        except Exception as e:
            print(f"  prop decompose failed {os.path.basename(obj_path)}: {e} (single-hull fallback)")

    # Fallback: one whole-model hull (the runtime builds the convex hull of all points).
    _write_hulls(hulls_path, [verts])
    return 1


def write_physics_hulls(propdir, stems):
    """Emit `<stem>.hulls` for each physics-referenced prop stem. `stems` is the set of
    decoded OBJ stems standing under `propdir`. A `.hulls` at least as new as its `.obj` is
    reused (decomposition is expensive; the OBJ is deterministic, so a fresh sidecar is still
    valid) — this makes a re-export instant and keeps `export_all` from re-paying per map for a
    model it already decomposed. Prints one summary line."""
    n_models = n_hulls = n_cached = 0
    for stem in sorted(stems):
        obj = os.path.join(propdir, stem + ".obj")
        if not os.path.exists(obj):
            continue
        hulls_path = os.path.join(propdir, stem + ".hulls")
        if os.path.exists(hulls_path) and os.path.getmtime(hulls_path) >= os.path.getmtime(obj):
            n_cached += 1
            continue
        c = write_prop_hulls(obj, hulls_path)
        if c:
            n_models += 1
            n_hulls += c
    mode = "coacd" if _HAVE_COACD else "single-hull (coacd absent)"
    print(f"physics prop collision: {n_models} decomposed -> {n_hulls} hulls, {n_cached} cached [{mode}]")
