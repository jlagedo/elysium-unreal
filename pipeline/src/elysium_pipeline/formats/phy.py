"""Decoder for VtMB's VPhysics collision files (`.phy`) -> the `props/<stem>.phys` sidecar.

A VtMB `.mdl` ships with a sibling `.phy` holding the collision model the original game
simulates against: a set of **convex** hulls authored alongside the render mesh. 2,854 of them
are in the retail VPKs. `prop_physics` collides with these, so the runtime reproduces the
original's physics by using them rather than by approximating the render mesh.

Layout (VtMB is Source 2003, so the *legacy* surface header -- no `VPHY` magic; `IVPS` sits at
+0x2C instead):

    phyheader_t            { int size; int id; int solidCount; int checksum; }
    per solid: int size; then at `body`:
      legacysurfaceheader_t{ float mass_center[3]; float rotation_inertia[3];
                             float upper_limit_radius;
                             int max_deviation:8, byte_size:24;      // byte_size == solid size
                             int offset_ledgetree_root; int dummy[2]; char magic[4]; }
    compactledgenode_t (28 B)  { int offset_right_node; int offset_compact_ledge; ... }
        offset_right_node == 0 marks a leaf; the left child follows the node inline.
    compactledge_t (16 B)      { int c_point_offset; int node_offset;
                                 uint flags:8, size_div_16:24; short n_triangles; short pad; }
    compacttriangle_t (16 B)   { uint packed; compactedge_t edge[3]; }
    compactedge_t (4 B)        { uint start_point_index:16; int opposite_index:15; uint virt:1; }
    points                     float[4] each, IVP metres.

After the solids comes a plain-text keyvalues block carrying the authored per-solid `mass`
and an `editparams` `totalmass`.

`offset_ledgetree_root` and a ledge's `c_point_offset` are relative to the surface header and
to the ledge respectively, not to the file.

**Coordinates.** IVP stores metres in a frame that is neither Source's nor Valve's documented
`ConvertPositionToHL` -- the phy frame is mirrored with respect to it. The mapping here was
settled empirically, by scoring all 48 axis-permutation/sign combinations against the already
verified render-mesh bounds over every `prop_physics` model: `(x, -z, -y) * 100` wins at
0.85 cm/model/axis, and the nearest *distinct* mapping is 4.6x worse. Negating two axes is a
rotation (determinant +1), not a reflection, so triangle winding carries through unchanged --
unlike `bsp.source_to_unreal`, which negates one axis and forces a winding reversal.
Evidence and method: `docs/vtmb/phy_vphysics.md`.
"""
import os
import struct

from elysium_pipeline.formats import install

# IVP metres -> Unreal centimetres.
IVP_TO_CM = 100.0

_HEADER = struct.Struct("<iiii")
_LEDGE = struct.Struct("<iiIhh")

LEDGE_NODE_SIZE = 28
LEDGE_SIZE = 16
TRIANGLE_SIZE = 16
POINT_SIZE = 16


class PhyError(Exception):
    """The file is not a `.phy` we understand. Raised rather than guessed around: a
    mis-parsed ledge yields a plausible-looking but wrong collider."""


def _leaf_ledges(data, node_off, out, guard):
    """Depth-first over the ledge tree. `offset_right_node == 0` marks a leaf, whose ledge
    sits at `offset_compact_ledge` from the node; otherwise the left child follows the node
    inline and the right child is `offset_right_node` away."""
    if node_off in guard:
        raise PhyError("ledge tree revisits node at %d" % node_off)
    guard.add(node_off)
    right, ledge_off = struct.unpack_from("<ii", data, node_off)
    if right == 0:
        out.append(node_off + ledge_off)
        return
    _leaf_ledges(data, node_off + LEDGE_NODE_SIZE, out, guard)
    _leaf_ledges(data, node_off + right, out, guard)


def _read_ledge(data, off):
    """One convex ledge -> (verts, tris), verts in Unreal cm, tris indexing into verts.

    The ledge's points are a shared pool indexed by every triangle corner; only the ones this
    ledge actually references are kept, renumbered densely."""
    c_point_offset, _node, _packed, n_tri, _pad = _LEDGE.unpack_from(data, off)
    if n_tri < 1:
        raise PhyError("ledge at %d has %d triangles" % (off, n_tri))

    corners = []
    for t in range(n_tri):
        base = off + LEDGE_SIZE + t * TRIANGLE_SIZE
        corners.append(tuple(
            struct.unpack_from("<I", data, base + 4 + e * 4)[0] & 0xFFFF for e in range(3)))

    remap = {}
    verts = []
    point_base = off + c_point_offset
    for tri in corners:
        for src in tri:
            if src in remap:
                continue
            remap[src] = len(verts)
            x, y, z, _w = struct.unpack_from("<4f", data, point_base + src * POINT_SIZE)
            verts.append((x * IVP_TO_CM, -z * IVP_TO_CM, -y * IVP_TO_CM))

    tris = [tuple(remap[c] for c in tri) for tri in corners]
    return verts, tris


def _keyvalues(text):
    """The trailing text block: a `solid { }` per solid plus one `editparams { }`. Returns
    (list of per-solid dicts, editparams dict)."""
    solids, params, current, name = [], {}, None, None
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.endswith("{"):
            name = line[:-1].strip() or name
            current = {}
            continue
        if line.startswith("}"):
            if current is not None:
                (solids.append(current) if name == "solid" else params.update(current))
            current, name = None, None
            continue
        parts = line.split('"')
        if current is not None and len(parts) >= 5:
            current[parts[1]] = parts[3]
    return solids, params


def decode(data):
    """Decode a `.phy` -> {"hulls": [(verts, tris), ...], "mass": float}.

    Every hull is convex by construction; `write_prop_phys` is where that gets asserted."""
    if len(data) < _HEADER.size:
        raise PhyError("file is %d bytes" % len(data))
    size, _ident, solid_count, _checksum = _HEADER.unpack_from(data, 0)
    if size < _HEADER.size or solid_count < 1:
        raise PhyError("header size=%d solidCount=%d" % (size, solid_count))

    pos = size
    hulls = []
    for index in range(solid_count):
        solid_size = struct.unpack_from("<i", data, pos)[0]
        body = pos + 4
        if solid_size < 48 or body + solid_size > len(data):
            raise PhyError("solid %d size %d overruns the file" % (index, solid_size))
        # The magic reads `IVPS` on most files and is zeroed on 42 of them, which are otherwise
        # the same layout (all 42 decode, 100 hulls, every one convex). `byte_size` is the real
        # integrity gate, so the magic only has to not be garbage.
        magic = data[body + 44:body + 48]
        if magic not in (b"IVPS", b"\0\0\0\0"):
            raise PhyError("solid %d magic %r, expected IVPS" % (index, magic))
        byte_size = struct.unpack_from("<I", data, body + 28)[0] >> 8
        if byte_size != solid_size:
            raise PhyError("solid %d byte_size %d != size %d" % (index, byte_size, solid_size))
        root = struct.unpack_from("<i", data, body + 32)[0]

        offsets = []
        _leaf_ledges(data, body + root, offsets, set())
        for off in offsets:
            hulls.append(_read_ledge(data, off))
        pos = body + solid_size

    solids, params = _keyvalues(data[pos:].decode("ascii", "replace"))
    mass = params.get("totalmass")
    if mass is None:
        mass = sum(float(s.get("mass", 0.0)) for s in solids)
    return {"hulls": hulls, "mass": float(mass)}


def is_convex(verts, tris):
    """Euler's identity for a triangulated convex polyhedron: F == 2V - 4. Holds on every
    ledge in the retail set, so a failure means the ledge was mis-parsed (or is not the convex
    shape the format promises) and must not become a collider."""
    return len(tris) == 2 * len(verts) - 4


def write_prop_phys(data, out_path):
    """Write one model's `.phys` sidecar. Returns the hull count.

    Format -- two lines per convex hull, after a single `mass` line:
        mass <kg>
        hull <x y z x y z ...>          flat Unreal-cm verts
        tris <i j k i j k ...>          flat triangle corners, indexing this hull's verts
    """
    phy = decode(data)
    for verts, tris in phy["hulls"]:
        if not is_convex(verts, tris):
            raise PhyError("ledge is not convex: %d verts, %d tris (want %d)"
                           % (len(verts), len(tris), 2 * len(verts) - 4))
    with open(out_path, "w") as handle:
        handle.write("mass %.6f\n" % phy["mass"])
        for verts, tris in phy["hulls"]:
            handle.write("hull " + " ".join("%.4f" % c for v in verts for c in v) + "\n")
            handle.write("tris " + " ".join(str(i) for t in tris for i in t) + "\n")
    return len(phy["hulls"])


def write_physics_phys(idx, propdir, stem_to_model):
    """Emit `<stem>.phys` for every `prop_physics` model that ships a `.phy`.

    `idx` is the install index, so a `.phy` the Unofficial Patch overrides shadows the VPK's
    exactly as the engine would resolve it.

    `stem_to_model` maps the decoded OBJ stem to its original `models/...mdl` key. A model
    without a `.phy` gets no sidecar and no collision -- which is what VtMB does with it:
    `CPhysicsProp::CreateVPhysics` (vampire.dll @10191510) demotes such a prop to
    SOLID_NONE + MOVETYPE_NONE and returns, leaving it visible but inert. Any stale `.hulls`
    from the retired CoACD pass is removed so it cannot be mistaken for live collision.

    Prints one summary line."""
    n_models = n_hulls = n_missing = n_failed = 0
    for stem in sorted(stem_to_model):
        stale = os.path.join(propdir, stem + ".hulls")
        if os.path.exists(stale):
            os.remove(stale)
        key = stem_to_model[stem].lower()
        key = key[:-4] + ".phy" if key.endswith(".mdl") else key + ".phy"
        data = install.read(idx, key)
        if data is None:
            n_missing += 1
            continue
        try:
            count = write_prop_phys(data, os.path.join(propdir, stem + ".phys"))
        except PhyError as exc:
            n_failed += 1
            print(f"  phy decode failed {stem}: {exc}")
            continue
        n_models += 1
        n_hulls += count
    note = f", {n_missing} without a .phy (inert, as VtMB leaves them)" if n_missing else ""
    note += f", {n_failed} failed" if n_failed else ""
    print(f"physics prop collision: {n_models} models -> {n_hulls} convex hulls{note}")
