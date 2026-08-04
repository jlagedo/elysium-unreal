"""Synthesised garment lattice for VtMB's rigidly-weighted skirts and coats.

VtMB has no cloth. A skirt or coat is skinned rigidly to one bone -- `Bip01 Pelvis` on
every garment in the corpus -- and swings as a single shell while the legs move inside
it. The only per-bone stage that does move is the authored angular limit in
`docs/vtmb/secondary_motion.md`, and it names hair, ponytail, mane and breast bones
only: no garment bone appears in it, and no garment vertex is driven by anything but
its one rigid parent.

This module segments that shell out of the decoded skin, hangs a bone lattice on the
garment's own surface, and re-weights the shell onto the lattice, so a host animation
system can simulate what the original engine could not.

Everything it emits is derived rather than authored. The shell is the `root`-dominant
geometry below the hip joint; the lattice rows and columns are centroids of that
geometry; the weights are a bilinear blend over the lattice. No painted weights, no
per-model authoring, and nothing that has to be re-done when a model changes.

Row 0 is an anchor row that is never simulated. Because a rigid parent-child pair with
no relative motion skins identically to the parent alone, every vertex at the waistband
transforms exactly as it does today -- the enhanced rig reproduces the original
silhouette at the attachment line by construction rather than by tuning.

The Unreal side that consumes this is `docs/architecture/animation-architecture.md`.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

#: The models the spike covers. Frozen rather than discovered: this is an experiment on
#: two garments, and every other model must keep loading its faithful mesh untouched.
ALLOWLIST = ("tremere_female_armor_0", "sheriff")

#: The bone every shipped garment is rigidly weighted to.
DEFAULT_ROOT = "Bip01 Pelvis"

#: The joint whose height splits "hangs free" from "wraps the torso". A coat reaching
#: the chest keeps its upper half rigid; only what falls below the hip can swing. The
#: same two bones drive the collision spheres that keep the legs inside the garment.
DEFAULT_HIP = ("Bip01 L Thigh", "Bip01 R Thigh")

#: A garment worth simulating has to be a shell, not a stray weight. Below these it is
#: cheaper and safer to leave the model exactly as it shipped.
MIN_SHELL_VERTS = 120
MIN_SHELL_DROP = 8.0        # Source units below the hip joint

#: Source units are inches; the glb carries metres, and glTFRuntime scales by 100 at
#: load. Lengths in the sidecar are metres in the glb's own space for that reason —
#: the consuming rig applies the import scale exactly as the composition rig does.
SCALE = 0.0254

# --- solver ramp ---------------------------------------------------------------------
#
# The cone widens down the chain: a waistband that barely moves, a hem that swings. The
# engine clamps ConeAngle to 0..90, so the top of the ramp is a real ceiling rather than
# a soft preference. Damping is deliberately high — under-damped cloth oscillates like a
# spring, which reads as rubber rather than fabric, and retail damped its own secondary
# motion hard too (the hair table's presets carry 0.9 and 0.97 in the damping slot).
CONE_MIN_DEG = 12.0
CONE_MAX_DEG = 55.0
CONE_CEILING_DEG = 90.0

LINEAR_DAMPING = 0.78
ANGULAR_DAMPING = 0.82
GRAVITY_SCALE = 1.0

#: A chain is stiffer to solve than a single body; the engine's default 4/1 leaves a long
#: skirt panel visibly rubbery at the hem.
SOLVER_ITERATIONS_PRE = 8
SOLVER_ITERATIONS_POST = 2

#: `FAnimNode_AnimDynamics` zero-initialises both of these, which makes a body react only
#: to its bound bone *rotating* and not at all to the character translating through the
#: world — a skirt on a walking character then barely moves. Non-zero values here are the
#: difference between a garment that flows and one that looks welded on.
COMPONENT_LINEAR_VEL_SCALE = 0.6
COMPONENT_LINEAR_ACC_SCALE = 0.25

#: Fraction of a collider bone's own skinned radius to use for its sphere. Below 1 so the
#: garment sits just off the leg rather than exactly on it.
COLLIDER_RADIUS_SCALE = 0.85


@dataclass
class LatticeBone:
    """One synthesised bone. `pos`/`quat` are parent-relative in Source units, which is
    what `StudioBone` carries and what the glTF exporter converts."""
    name: str
    parent: str
    pos: tuple[float, float, float]
    quat: tuple[float, float, float, float]
    world: tuple[float, float, float]
    row: int
    column: int


@dataclass
class GarmentPlan:
    root: str
    rows: int
    columns: int
    bones: list[LatticeBone]
    #: material -> list of (joint name, weight) per surface vertex, only for reweighted
    #: vertices; a vertex absent from the map keeps its decoded skin.
    weights: dict[str, dict[int, list[tuple[str, float]]]]
    #: hip joint height and shell extent, in Source units, for the runtime's own report.
    hip_z: float
    hem_z: float
    #: sphere limits keeping the legs inside the garment: one per collider bone, each
    #: `(bone name, offset from the bone's bind position, radius)` in Source units.
    colliders: list[tuple[str, tuple[float, float, float], float]] = field(
        default_factory=list)
    stats: dict = field(default_factory=dict)

    def chains(self) -> list[list[str]]:
        """One simulated chain per column: rows 1..N, anchor row excluded."""
        out = []
        for c in range(self.columns):
            chain = [b.name for b in self.bones if b.column == c and b.row >= 1]
            if chain:
                out.append(chain)
        return out


# --- bind-pose composition (Source space: X forward, Y left, Z up) --------------------

def _quat_mat(q):
    """xyzw -> 3x3. `StudioBone` stores the bind rotation in this order."""
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ], dtype=np.float64)


def _mat_quat(m):
    """3x3 -> xyzw."""
    t = m[0, 0] + m[1, 1] + m[2, 2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        w, x = 0.25 * s, (m[2, 1] - m[1, 2]) / s
        y, z = (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        w, x = (m[2, 1] - m[1, 2]) / s, 0.25 * s
        y, z = (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        w, x = (m[0, 2] - m[2, 0]) / s, (m[0, 1] + m[1, 0]) / s
        y, z = 0.25 * s, (m[1, 2] + m[2, 1]) / s
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        w, x = (m[1, 0] - m[0, 1]) / s, (m[0, 2] + m[2, 0]) / s
        y, z = (m[1, 2] + m[2, 1]) / s, 0.25 * s
    return (x, y, z, w)


def bind_world(bones):
    """FK the bind pose. Returns index -> 4x4 in Source space."""
    out = {}
    for b in bones:
        local = np.eye(4)
        local[:3, :3] = _quat_mat(b.quat)
        local[:3, 3] = b.pos
        out[b.index] = local if b.parent < 0 else out[b.parent] @ local
    return out


# --- segmentation --------------------------------------------------------------------

def segment(bones, surfaces, *, root=DEFAULT_ROOT, hip=DEFAULT_HIP):
    """Isolate the free-hanging shell: `root`-dominant vertices below the hip joint.

    Returns `(shell, hip_z)` where shell is material -> list of (vertex index, pos).
    `hip_z` is the mean world bind height of the hip joints, which is where a garment
    stops following the torso and starts hanging.
    """
    by_name = {b.name: b for b in bones}
    if root not in by_name:
        return {}, 0.0
    root_index = by_name[root].index

    world = bind_world(bones)
    hips = [world[by_name[h].index][2, 3] for h in hip if h in by_name]
    if not hips:
        return {}, 0.0
    hip_z = float(sum(hips) / len(hips))

    shell = {}
    for mat, s in surfaces.items():
        picked = []
        for i, (p, js, ws) in enumerate(zip(s["pos"], s["joints"], s["weights"])):
            dominant = max(zip(ws, js))[1]
            if dominant == root_index and p[2] < hip_z:
                picked.append((i, p))
        if picked:
            shell[mat] = picked
    return shell, hip_z


def collider_spheres(bones, surfaces, names=DEFAULT_HIP):
    """Sphere limits sized from the collider bones' own skinned geometry.

    A garment that swings has to be stopped by the legs rather than pass through them,
    and `FAnimPhysSphericalLimit` is the only collision `AnimDynamics` offers. The radius
    is measured off the bone's own dominant vertices instead of guessed, so a heavy NPC
    and a slim one get different legs without either being authored.

    Returns `[(bone, offset from the bone's bind position, radius)]` in Source units.
    """
    by_name = {b.name: b for b in bones}
    world = bind_world(bones)
    out = []
    for name in names:
        bone = by_name.get(name)
        if bone is None:
            continue
        origin = world[bone.index][:3, 3]
        owned = [p for s in surfaces.values()
                 for p, js, ws in zip(s["pos"], s["joints"], s["weights"])
                 if max(zip(ws, js))[1] == bone.index]
        if len(owned) < 8:
            continue
        pts = np.array(owned, dtype=np.float64)
        centre = pts.mean(axis=0)
        # 75th percentile rather than the max: a single stray vertex on the hip seam
        # would otherwise inflate the sphere until it pushed the whole skirt outward.
        radius = float(np.percentile(np.linalg.norm(pts - centre, axis=1), 75))
        out.append((name, tuple(float(x) for x in centre - origin),
                    radius * COLLIDER_RADIUS_SCALE))
    return out


def parameters(plan):
    """The solver setup, as `cloth/<stem>.json` states it.

    Lengths are metres in the glb's own space, because the mesh beside them is; the
    consuming rig applies glTFRuntime's import scale to both. Angles are degrees and the
    scales are dimensionless, so neither is converted.

    One chain per column, rows 1 upward — row 0 is the anchor and is never simulated,
    which is what makes the garment skin identically to the original at the waistband.
    """
    from elysium_pipeline.formats.mdl_gltf import conv_pos

    rows_simulated = plan.rows - 1
    by_cell = {(b.row, b.column): b for b in plan.bones}

    def cone(row):
        if rows_simulated <= 1:
            return CONE_MAX_DEG
        t = (row - 1) / (rows_simulated - 1)
        return min(CONE_MIN_DEG + (CONE_MAX_DEG - CONE_MIN_DEG) * t, CONE_CEILING_DEG)

    # Cell size drives the body size. Deliberately isotropic: box extents are stated in
    # the body's own frame, and an axis mistake there is invisible in the data and
    # baffling in motion, whereas an isotropic body is merely approximate.
    circumference_step = 2 * math.pi / max(plan.columns, 1)
    drop = plan.hip_z - plan.hem_z
    row_spacing = drop / max(rows_simulated, 1)

    chains = []
    for c in range(plan.columns):
        bodies = []
        for r in range(1, plan.rows):
            bone = by_cell.get((r, c))
            if bone is None:
                continue
            radius = math.hypot(bone.world[0] - plan.stats["centre"][0],
                                bone.world[1] - plan.stats["centre"][1])
            cell = min(radius * circumference_step, row_spacing)
            bodies.append({
                "bone": bone.name,
                "row": r,
                "cone_deg": round(cone(r), 2),
                "box_extent": round(max(cell, 1.0) * 0.5 * SCALE, 5),
            })
        if bodies:
            chains.append({"column": c, "root": bodies[0]["bone"],
                           "end": bodies[-1]["bone"], "bodies": bodies})

    return {
        "chains": chains,
        "anchor_row": [by_cell[(0, c)].name for c in range(plan.columns)
                       if (0, c) in by_cell],
        "solver": {
            "linear_damping": LINEAR_DAMPING,
            "angular_damping": ANGULAR_DAMPING,
            "gravity_scale": GRAVITY_SCALE,
            "iterations_pre": SOLVER_ITERATIONS_PRE,
            "iterations_post": SOLVER_ITERATIONS_POST,
            "component_linear_vel_scale": COMPONENT_LINEAR_VEL_SCALE,
            "component_linear_acc_scale": COMPONENT_LINEAR_ACC_SCALE,
        },
        "colliders": [
            {"bone": name,
             "offset": [round(v, 6) for v in conv_pos(offset)],
             "radius": round(radius * SCALE, 5)}
            for name, offset, radius in plan.colliders
        ],
    }


# --- lattice -------------------------------------------------------------------------

def plan(bones, surfaces, *, root=DEFAULT_ROOT, hip=DEFAULT_HIP,
         rows=5, columns=10, prefix="Cloth"):
    """Build a garment plan, or None when the model carries no shell worth simulating.

    `rows` counts lattice rows including the rigid anchor row 0, so `rows=4` simulates
    three. `columns` is the number of vertical panels around the axis; a vertex blends
    between its two neighbouring rows and two neighbouring columns, which is exactly
    the four influences glTF carries.
    """
    shell, hip_z = segment(bones, surfaces, root=root, hip=hip)
    if not shell:
        return None

    pts = np.array([p for m in shell for _, p in shell[m]], dtype=np.float64)
    if len(pts) < MIN_SHELL_VERTS:
        return None

    hem_z = float(pts[:, 2].min())
    drop = hip_z - hem_z
    if drop < MIN_SHELL_DROP:
        return None

    # The axis the panels wrap: the shell's own horizontal centre, not the bone's, so a
    # coat whose volume sits behind the hips is still divided evenly.
    cx, cy = float(pts[:, 0].mean()), float(pts[:, 1].mean())

    def coords(p):
        """(row float, column float) for a shell point."""
        t = (hip_z - p[2]) / drop                      # 0 at the hip line, 1 at the hem
        rf = min(max(t, 0.0), 1.0) * (rows - 1)
        ang = math.atan2(p[1] - cy, p[0] - cx)
        cf = (ang + math.pi) / (2 * math.pi) * columns
        return rf, cf % columns

    # Row/column cell centroids give the lattice the garment's own silhouette -- a
    # flared skirt gets a flared lattice, a straight coat a straight one.
    acc = np.zeros((rows, columns, 3))
    cnt = np.zeros((rows, columns))
    for mat in shell:
        for _, p in shell[mat]:
            rf, cf = coords(p)
            acc[int(round(rf)), int(cf) % columns] += p
            cnt[int(round(rf)), int(cf) % columns] += 1

    # Rows with no geometry in a cell fall back to that row's mean ring, and a row with
    # no geometry at all to a cylinder -- a garment with a slit or an asymmetric hem
    # must not produce a bone at the origin.
    radii = []
    for r in range(rows):
        m = cnt[r] > 0
        if m.any():
            ring = acc[r][m] / cnt[r][m][:, None]
            radii.append(float(np.hypot(ring[:, 0] - cx, ring[:, 1] - cy).mean()))
        else:
            radii.append(float(np.hypot(pts[:, 0] - cx, pts[:, 1] - cy).mean()))

    centres = np.zeros((rows, columns, 3))
    for r in range(rows):
        z = hip_z - (r / (rows - 1)) * drop
        for c in range(columns):
            if cnt[r, c] > 0:
                centres[r, c] = acc[r, c] / cnt[r, c]
                centres[r, c, 2] = z
            else:
                a = (c + 0.5) / columns * 2 * math.pi - math.pi
                centres[r, c] = (cx + radii[r] * math.cos(a),
                                 cy + radii[r] * math.sin(a), z)

    # Lattice bones carry identity world rotation: the runtime works in component space
    # off bind offsets, so a rest orientation would only be one more thing to keep in
    # sync with the exporter's basis change.
    world = bind_world(bones)
    by_name = {b.name: b for b in bones}
    root_world = world[by_name[root].index]

    lattice: list[LatticeBone] = []
    name_of = {}
    for c in range(columns):
        for r in range(rows):
            name = f"{prefix}_{c:02d}_{r:02d}"
            name_of[(r, c)] = name
            wpos = centres[r, c]
            if r == 0:
                parent_name, parent_world = root, root_world
            else:
                parent_name = name_of[(r - 1, c)]
                pw = np.eye(4)
                pw[:3, 3] = centres[r - 1, c]
                parent_world = pw
            gw = np.eye(4)
            gw[:3, 3] = wpos
            local = np.linalg.inv(parent_world) @ gw
            lattice.append(LatticeBone(
                name=name, parent=parent_name,
                pos=tuple(float(x) for x in local[:3, 3]),
                quat=_mat_quat(local[:3, :3]),
                world=tuple(float(x) for x in wpos),
                row=r, column=c,
            ))

    # --- reweight ---
    weights: dict[str, dict[int, list[tuple[str, float]]]] = {}
    for mat in shell:
        per_vert = {}
        for vi, p in shell[mat]:
            rf, cf = coords(p)
            r0 = min(int(rf), rows - 1)
            r1 = min(r0 + 1, rows - 1)
            fr = rf - r0
            c0 = int(cf) % columns
            c1 = (c0 + 1) % columns
            fc = cf - int(cf)
            terms = [
                (name_of[(r0, c0)], (1 - fr) * (1 - fc)),
                (name_of[(r0, c1)], (1 - fr) * fc),
                (name_of[(r1, c0)], fr * (1 - fc)),
                (name_of[(r1, c1)], fr * fc),
            ]
            merged: dict[str, float] = {}
            for n, w in terms:
                if w > 1e-6:
                    merged[n] = merged.get(n, 0.0) + w
            total = sum(merged.values()) or 1.0
            per_vert[vi] = [(n, w / total) for n, w in
                            sorted(merged.items(), key=lambda kv: -kv[1])[:4]]
        weights[mat] = per_vert

    return GarmentPlan(
        root=root, rows=rows, columns=columns, bones=lattice, weights=weights,
        hip_z=hip_z, hem_z=hem_z,
        colliders=collider_spheres(bones, surfaces, names=hip),
        stats={
            "shell_verts": int(sum(len(v) for v in shell.values())),
            "shell_materials": sorted(shell),
            "drop": round(drop, 2),
            "lattice_bones": len(lattice),
            "empty_cells": int((cnt == 0).sum()),
            "centre": (round(cx, 2), round(cy, 2)),
        },
    )
