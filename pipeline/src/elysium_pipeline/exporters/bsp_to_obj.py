"""
Extract world geometry from a Source BSP (v17, VtMB) into a Wavefront .obj.

No textures yet - this is the "does the mesh come out geometrically correct?"
proof. OBJ is plain text and loads in Godot and Blender.

Face reconstruction (the indirection chain):

    FACE.firstedge/numedges -> SURFEDGES[] -> (signed) EDGES[] -> VERTEXES[]

  * A surfedge is a SIGNED int indexing EDGES.
      surfedge >= 0 : use edge as (v0 -> v1)
      surfedge <  0 : use edge reversed, i.e. vertex edge[|surfedge|][1]
    The sign keeps every face's winding consistent.
  * Collect the face's corner vertices in order -> an N-gon.
  * Fan-triangulate the N-gon: (0,1,2), (0,2,3), ...

Coordinate systems:
  * Source: Z-up, right-handed, 1 unit = 1 inch.
  * Godot : Y-up. We remap (x, y, z)_source -> (x, z, -y)_godot so the map
    stands upright, and scale inches -> meters (*0.0254) so physics/camera
    speeds feel sane later. Scale is cosmetic for this test.
"""
import struct
import sys

INCH_TO_M = 0.0254

def read_lump(data, index):
    """Return the raw bytes of lump `index` using the header directory."""
    entry_ofs = 8 + index * 16
    fileofs, filelen = struct.unpack_from("<ii", data, entry_ofs)
    return data[fileofs:fileofs + filelen]

def source_to_godot(x, y, z):
    # Z-up -> Y-up, inches -> meters.
    return (x * INCH_TO_M, z * INCH_TO_M, -y * INCH_TO_M)

def extract(bsp_path, obj_path):
    with open(bsp_path, "rb") as f:
        data = f.read()

    version = struct.unpack_from("<i", data, 4)[0]

    # --- VERTEXES: array of 3 floats (12 bytes) ---
    vtx_raw = read_lump(data, 3)
    vertexes = [struct.unpack_from("<fff", vtx_raw, i)
                for i in range(0, len(vtx_raw), 12)]

    # --- EDGES: array of 2 uint16 (4 bytes) ---
    edge_raw = read_lump(data, 12)
    edges = [struct.unpack_from("<HH", edge_raw, i)
             for i in range(0, len(edge_raw), 4)]

    # --- SURFEDGES: array of signed int32 (4 bytes) ---
    se_raw = read_lump(data, 13)
    surfedges = list(struct.unpack_from("<%di" % (len(se_raw) // 4), se_raw, 0))

    # --- FACES: VtMB v17 dface_t is 104 bytes (NOT the modern 56). Layout
    #     reverse-engineered from the file (see probe_face.py):
    #   offset 36 int   firstedge
    #   offset 40 int16 numedges
    #   offset 42 int16 texinfo
    face_raw = read_lump(data, 7)
    FACE_SIZE = 104
    FE_OFS, NE_OFS = 36, 40
    num_faces = len(face_raw) // FACE_SIZE

    obj_verts = []      # deduped-ish: we just append per face corner
    tris = []           # list of (i0, i1, i2) into obj_verts (1-based written later)
    degenerate = 0

    for fi in range(num_faces):
        base = fi * FACE_SIZE
        firstedge = struct.unpack_from("<i", face_raw, base + FE_OFS)[0]
        numedges = struct.unpack_from("<h", face_raw, base + NE_OFS)[0]

        if numedges < 3:
            degenerate += 1
            continue

        # Walk the surfedges to collect this face's corner vertex indices.
        corner_vidx = []
        for k in range(numedges):
            surfedge = surfedges[firstedge + k]
            if surfedge >= 0:
                v = edges[surfedge][0]
            else:
                v = edges[-surfedge][1]
            corner_vidx.append(v)

        # Emit corners as OBJ vertices (converted to Godot space), remember
        # their new 0-based indices for triangulation.
        local = []
        for v in corner_vidx:
            gx, gy, gz = source_to_godot(*vertexes[v])
            obj_verts.append((gx, gy, gz))
            local.append(len(obj_verts) - 1)

        # Fan triangulation of the N-gon.
        for k in range(1, len(local) - 1):
            tris.append((local[0], local[k], local[k + 1]))

    # --- write OBJ ---
    xs = [v[0] for v in obj_verts]
    ys = [v[1] for v in obj_verts]
    zs = [v[2] for v in obj_verts]
    with open(obj_path, "w") as out:
        out.write(f"# ch_hub_1 world geometry, BSP v{version}\n")
        out.write(f"# {len(obj_verts)} verts, {len(tris)} tris\n")
        out.write("o ch_hub_1_world\n")
        for (x, y, z) in obj_verts:
            out.write(f"v {x:.4f} {y:.4f} {z:.4f}\n")
        for (a, b, c) in tris:
            out.write(f"f {a+1} {b+1} {c+1}\n")  # OBJ is 1-indexed

    print(f"BSP version         : {version}")
    print(f"source vertices     : {len(vertexes):,}")
    print(f"edges / surfedges   : {len(edges):,} / {len(surfedges):,}")
    print(f"faces               : {num_faces:,}  (skipped {degenerate} degenerate)")
    print(f"emitted OBJ verts   : {len(obj_verts):,}")
    print(f"emitted triangles   : {len(tris):,}")
    print(f"bounding box (m)    : "
          f"X[{min(xs):.1f}, {max(xs):.1f}]  "
          f"Y[{min(ys):.1f}, {max(ys):.1f}]  "
          f"Z[{min(zs):.1f}, {max(zs):.1f}]")
    span = (max(xs)-min(xs), max(ys)-min(ys), max(zs)-min(zs))
    print(f"map span (m)        : {span[0]:.1f} x {span[1]:.1f} x {span[2]:.1f}")
    print(f"wrote               : {obj_path}")

if __name__ == "__main__":
    bsp = sys.argv[1]
    obj = sys.argv[2] if len(sys.argv) > 2 else "ch_hub_1.obj"
    extract(bsp, obj)
