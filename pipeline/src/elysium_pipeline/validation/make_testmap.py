"""Generate a blank self-test map for the Godot viewer: an evenly-lit enclosed room
with a checker floor and colour-coded walls, plus a synthetic light rig and a spawn.

Not part of the asset pipeline — a scratch stage for iterating on characters,
animations, and transforms without the tutorial map's dark lighting and clutter.
Drop a skeletal .glb in with `--map testbox --gltf <glb> --gltf-at 0,0,0 ...`.

Writes $ELYSIUM_EXPORT_ROOT/testbox/ (obj + mtl + tex/checker.png + .lights + .spawn), which
the viewer loads like any exported map (ContentPaths anchors to $ELYSIUM_EXPORT_ROOT). The OBJ
is authored directly in Godot space (Y-up, metres) — the viewer applies no further
transform to world OBJs.

  python pipeline/src/elysium_pipeline/validation/make_testmap.py
"""
import os
from PIL import Image
from elysium_pipeline.paths import export_root

HALF = 12.0      # room half-width (24 m square)
H = 6.0          # wall height
OUT = os.path.join(os.fspath(export_root()), "testbox")

# Colour-coded walls give orientation at a glance while debugging character facing:
# +X red, -X blue, +Z green, -Z yellow (all high-contrast against skin/dark clothes).
WALLS = {
    "wall_px": ((0.85, 0.15, 0.15), +1, 0),   # (colour, axis sign, axis: 0=x 1=z)
    "wall_nx": ((0.15, 0.45, 0.85), -1, 0),
    "wall_pz": ((0.20, 0.75, 0.28), +1, 1),
    "wall_nz": ((0.85, 0.78, 0.15), -1, 1),
}


def checker(path, n=512, cells=8):
    """A grey/white checker so scale and foot-sliding read clearly on the floor."""
    img = Image.new("RGB", (n, n))
    s = n // cells
    a, b = (150, 150, 155), (225, 225, 230)
    px = img.load()
    for y in range(n):
        for x in range(n):
            px[x, y] = a if ((x // s) + (y // s)) & 1 else b
    img.save(path)


def main():
    os.makedirs(os.path.join(OUT, "tex"), exist_ok=True)
    checker(os.path.join(OUT, "tex", "checker.png"))

    v, vt, vn, faces = [], [], [], []   # faces: (material, [(vi,ti,ni)])

    def quad(mat, p, uv, normal):
        base_v = len(v) + 1
        base_t = len(vt) + 1
        ni = len(vn) + 1
        vn.append(normal)
        for p_i in p:
            v.append(p_i)
        for uv_i in uv:
            vt.append(uv_i)
        # BuildWorld ignores OBJ vn and derives normals from winding (GenerateNormals);
        # Godot's front face is clockwise, so emit the loop reversed to make each face's
        # normal point the way `normal` intends (floor up, walls/ceiling inward).
        order = [0, 3, 2, 1]
        faces.append((mat, [(base_v + k, base_t + k, ni) for k in order]))

    # Floor (y=0, normal up). UV = (x,z)/2 -> 1 m checker cells.
    fc = [(-HALF, 0, -HALF), (HALF, 0, -HALF), (HALF, 0, HALF), (-HALF, 0, HALF)]
    quad("floor", fc, [(x / 2.0, z / 2.0) for (x, _, z) in fc], (0, 1, 0))
    # Ceiling (y=H, normal down), wound the other way.
    cc = [(-HALF, H, -HALF), (-HALF, H, HALF), (HALF, H, HALF), (HALF, H, -HALF)]
    quad("ceil", cc, [(0, 0)] * 4, (0, -1, 0))

    # Four walls, inward-facing normals.
    for mat, (_col, sgn, axis) in WALLS.items():
        if axis == 0:  # x = ±HALF plane, spans z
            x = sgn * HALF
            if sgn > 0:   # +X wall, inward normal -X
                p = [(x, 0, -HALF), (x, 0, HALF), (x, H, HALF), (x, H, -HALF)]
                nrm = (-1, 0, 0)
            else:         # -X wall, inward normal +X
                p = [(x, 0, HALF), (x, 0, -HALF), (x, H, -HALF), (x, H, HALF)]
                nrm = (1, 0, 0)
        else:          # z = ±HALF plane, spans x
            z = sgn * HALF
            if sgn > 0:   # +Z wall, inward normal -Z
                p = [(HALF, 0, z), (-HALF, 0, z), (-HALF, H, z), (HALF, H, z)]
                nrm = (0, 0, -1)
            else:         # -Z wall, inward normal +Z
                p = [(-HALF, 0, z), (HALF, 0, z), (HALF, H, z), (-HALF, H, z)]
                nrm = (0, 0, 1)
        quad(mat, p, [(0, 0)] * 4, nrm)

    # --- write MTL ---
    with open(os.path.join(OUT, "testbox.mtl"), "w") as f:
        f.write("newmtl floor\nmap_Kd tex/checker.png\nKd 0.8 0.8 0.8\n\n")
        f.write("newmtl ceil\nKd 0.55 0.55 0.60\n\n")
        for mat, (col, _s, _a) in WALLS.items():
            f.write(f"newmtl {mat}\nKd {col[0]} {col[1]} {col[2]}\n\n")

    # --- write OBJ ---
    with open(os.path.join(OUT, "testbox.obj"), "w") as f:
        f.write("mtllib testbox.mtl\n")
        for x, y, z in v:
            f.write(f"v {x} {y} {z}\n")
        for u, w in vt:
            f.write(f"vt {u} {w}\n")
        for x, y, z in vn:
            f.write(f"vn {x} {y} {z}\n")
        last = None
        for mat, idx in faces:
            if mat != last:
                f.write(f"usemtl {mat}\n")
                last = mat
            f.write("f " + " ".join(f"{a}/{b}/{c}" for a, b, c in idx) + "\n")

    # --- synthetic light rig. The shaded world shader is driven by the LightRig's real
    # lights; point lights (type 1) are the map-proven path (VtMB WORLDLIGHTS are almost
    # all point/spot), and their energy is min(intensity*RIG_SCALE, RIG_MAXE) so a big raw
    # intensity pins each omni to the max-energy cap for bright, even fill. A ceiling grid
    # of omnis + a white skyambient floods the room flatly (good for reading a pose).
    # Format: type ox oy oz  dx dy dz  ir ig ib  radius  stopdot stopdot2 exp  style
    with open(os.path.join(OUT, "testbox.lights"), "w") as f:
        for lx in (-6, 0, 6):
            for lz in (-6, 0, 6):
                f.write(f"1 {lx} {H-0.5} {lz}  0 0 0  3000 3000 3000  40  0 0 0  0\n")
        f.write("5 0 0 0  0 0 0  1 1 1  0  0 0 0  0\n")

    # --- collision: convex-brush hulls (Godot space, metres), the same sidecar real
    # maps use. One box per surface (floor slab + four walls) so the player stands and
    # can't walk out. Each line is a hull's corner points flattened (x y z ...).
    def box(lo, hi):
        xs, ys, zs = zip(lo, hi)
        return [(x, y, z) for x in xs for y in ys for z in zs]

    T = 0.5  # slab / wall thickness
    hulls = [box((-HALF, -T, -HALF), (HALF, 0.0, HALF))]            # floor
    hulls.append(box((HALF, 0, -HALF), (HALF + T, H, HALF)))        # +X wall
    hulls.append(box((-HALF - T, 0, -HALF), (-HALF, H, HALF)))      # -X wall
    hulls.append(box((-HALF, 0, HALF), (HALF, H, HALF + T)))        # +Z wall
    hulls.append(box((-HALF, 0, -HALF - T), (HALF, H, -HALF)))      # -Z wall
    with open(os.path.join(OUT, "testbox.hulls"), "w") as f:
        for h in hulls:
            f.write(" ".join(f"{c:.3f}" for pt in h for c in pt) + "\n")

    # --- spawn: source coords (viewer converts (sx,sz,-sy)*0.0254, yaw-90). Place the
    # player at Godot (0,0.05,5) facing -Z (toward the origin where test models sit).
    # source = (gx/0.0254, -gz/0.0254, gy/0.0254); yaw_src = yaw_godot(0)+90 = 90.
    with open(os.path.join(OUT, "testbox.spawn"), "w") as f:
        f.write("origin 0 -196.85 1.97\nyaw 90\n")

    print(f"wrote {OUT}: {len(faces)} faces, checker floor, 4 colour walls, omni grid")


if __name__ == "__main__":
    main()
