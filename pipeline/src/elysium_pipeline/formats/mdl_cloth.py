"""VtMB's authored renderer-cloth payload, decoded into a simulation mesh.

`docs/vtmb/secondary_motion.md` owns the format and the retail solve. This module
turns that payload into the four things a host cloth solver needs and the file
does not state directly:

  rest positions   The payload stores particle *indices*, never positions. The
                   anchored prefix names model-global render vertices and
                   `StudioMesh` +52 names, per render vertex, the particle that
                   supplies its position. Reading the first and inverting the
                   second places every particle.
  topology         The authored collision-triangle array is a per-particle
                   collision proxy, not a surface -- on `jeanette_skirt` 870 of
                   its 930 edges are used once. The render mesh IS a surface, so
                   its own triangulation pushed through the +52 map induces one
                   (604 triangles, no non-manifold edge).
  binding          Anchored particles are skinned through the bone palette, so
                   their render vertices' skin weights are the simulation mesh's
                   weights.
  colliders        Bind-space capsules and spheres carried into each owning
                   bone's local frame through `StudioBone.pose_to_bone`.

Everything here stays in the model's own Source frame -- inches, Z-up,
right-handed. This is a format parser, not a coordinate exporter: the basis
change belongs to the writer that emits a sidecar beside a `.glb`, and it uses
the same conversion that `.glb` was written with.

Two decoded facts that a caller cannot rediscover from the numbers alone:

  * The distance set's `rest_length_squared` reproduces the reconstructed rest
    pose to a median relative error near 1e-07 on every garment measured, which
    is what makes the reconstruction self-checking at all.
  * The compression-only set does **not** share one scale across the corpus.
    `jeanette_skirt` authors it at 1/1000 of the squared rest distance while
    `sheriffbody2` and `nosferatu_female_armor_2` author it at 1/1. So no scale
    constant is applied here: the file's value is carried verbatim and
    `compression_rest_ratio` reports the ratio **measured** against the
    reconstructed rest pose for that garment. A consumer that needs a common
    scale multiplies by it; one that needs the authored number already has it.
  * A zero primitive count can retain a nonzero table offset, so the count is
    the validity gate and the offset must not be dereferenced without it.
"""

from __future__ import annotations

import struct
from collections import defaultdict

from elysium_pipeline.formats import mdl, mdl_skel

MODEL_STRIDE = 224
MESH_STRIDE = 60

#: `MDLHeader.Flags` bit that gates the whole system, and the header pair the
#: bodypart walk starts from.
H_FLAGS = 228
H_NUM_BODYPARTS, H_BODYPART_INDEX = 320, 324
CLOTH_FLAG = 0x400

#: Per-`StudioModel` cloth fields.
M_NUM_DEFS, M_DEF_TABLE = 200, 204
M_NUM_CAPSULES, M_CAPSULE_TABLE = 208, 212
M_NUM_SPHERES, M_SPHERE_TABLE = 216, 220
CAPSULE_STRIDE, SPHERE_STRIDE = 36, 20

#: Per-`StudioMesh` substitution maps.
MESH_SELECTOR, MESH_POSITION, MESH_TANGENT = 48, 52, 56
SELECTOR_SKINNED = 0xFF
POSITION_INDEX_MASK = 0x7FFF
POSITION_FLIP_NORMAL = 0x8000



def _i32(d, o):
    return struct.unpack_from("<i", d, o)[0]


def _u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def _f32(d, o):
    return struct.unpack_from("<f", d, o)[0]


def _f3(d, o):
    return struct.unpack_from("<3f", d, o)


def _cstr(d, o):
    end = d.find(b"\0", o)
    return d[o:end if end >= 0 else len(d)].decode("ascii", "replace")


def has_cloth(d):
    """Whether this model declares the garment-cloth payload at all."""
    return bool(_i32(d, H_FLAGS) & CLOTH_FLAG)


def _relative(d, owner, rel, size, label):
    absolute = owner + rel
    if rel <= 0 or absolute < 0 or absolute + size > len(d):
        raise ValueError(f"{label}: relative {rel} from {owner} leaves the {len(d)}-byte image")
    return absolute


def read_definition(d, model_base, index):
    """One cloth definition: particles, constraints and the authored collision proxy."""
    count = _i32(d, model_base + M_NUM_DEFS)
    table = _relative(d, model_base, _i32(d, model_base + M_DEF_TABLE), count * 4, "cloth table")
    record = _relative(d, model_base, _i32(d, table + index * 4), 92, f"cloth definition {index}")

    particles = _i32(d, record + 4)
    anchored = _i32(d, record + 8)
    dynamic = _i32(d, record + 12)
    total = _i32(d, record + 20)
    distance = _i32(d, record + 24)
    compression = _i32(d, record + 28)
    if anchored + dynamic != particles or distance + compression != total:
        raise ValueError(f"cloth definition {index}: counts disagree")

    anchors = []
    if anchored:
        base = _relative(d, record, _i32(d, record + 16), anchored * 2, "anchor indices")
        anchors = [_u16(d, base + i * 2) for i in range(anchored)]

    constraints = []
    if total:
        base = _relative(d, record, _i32(d, record + 32), total * 16, "constraints")
        for i in range(total):
            rec = base + i * 16
            constraints.append(
                {
                    "a": _u16(d, rec),
                    "b": _u16(d, rec + 2),
                    "move_a": _f32(d, rec + 4),
                    "move_b": _f32(d, rec + 8),
                    "rest_length_squared": _f32(d, rec + 12),
                    "compression_only": i >= distance,
                }
            )

    proxy = []
    ntri = _i32(d, record + 48)
    if ntri:
        base = _relative(d, record, _i32(d, record + 52), ntri * 6, "collision triangles")
        proxy = [[_u16(d, base + i * 6), _u16(d, base + i * 6 + 2), _u16(d, base + i * 6 + 4)]
                 for i in range(ntri)]

    return {
        "gravity_scale": _f32(d, record),
        "particles": particles,
        "anchored": anchored,
        "dynamic": dynamic,
        "anchor_vertex_indices": anchors,
        "constraints": constraints,
        "distance_constraints": distance,
        "compression_constraints": compression,
        "collision_proxy_triangles": proxy,
    }


def read_colliders(d, model_base, bones):
    """Authored capsules and spheres, carried into their owning bone's local frame.

    Both tables store bind-space points beside a bone index, so a consumer that
    attaches them to a moving bone needs the point in that bone's own frame.
    `StudioBone.pose_to_bone` is the 3x4 world-to-bone inverse bind that does it.
    """
    def to_local(bone_index, point):
        if not (0 <= bone_index < len(bones)):
            return point
        m = bones[bone_index].pose_to_bone
        x, y, z = point
        return (
            m[0] * x + m[1] * y + m[2] * z + m[3],
            m[4] * x + m[5] * y + m[6] * z + m[7],
            m[8] * x + m[9] * y + m[10] * z + m[11],
        )

    capsules = []
    count = _i32(d, model_base + M_NUM_CAPSULES)
    if count:
        base = _relative(d, model_base, _i32(d, model_base + M_CAPSULE_TABLE),
                         count * CAPSULE_STRIDE, "cloth capsules")
        for i in range(count):
            rec = base + i * CAPSULE_STRIDE
            b0, b1 = _i32(d, rec), _i32(d, rec + 4)
            a, b = _f3(d, rec + 12), _f3(d, rec + 24)
            capsules.append(
                {
                    "bone_a": b0, "bone_b": b1,
                    "bone_a_name": bones[b0].name if 0 <= b0 < len(bones) else None,
                    "bone_b_name": bones[b1].name if 0 <= b1 < len(bones) else None,
                    "radius": _f32(d, rec + 8),
                    "a_bind": list(a), "b_bind": list(b),
                    "a_local": list(to_local(b0, a)), "b_local": list(to_local(b1, b)),
                }
            )

    spheres = []
    count = _i32(d, model_base + M_NUM_SPHERES)
    if count:
        base = _relative(d, model_base, _i32(d, model_base + M_SPHERE_TABLE),
                         count * SPHERE_STRIDE, "cloth spheres")
        for i in range(count):
            rec = base + i * SPHERE_STRIDE
            bone = _i32(d, rec)
            centre = _f3(d, rec + 8)
            spheres.append(
                {
                    "bone": bone,
                    "bone_name": bones[bone].name if 0 <= bone < len(bones) else None,
                    "radius": _f32(d, rec + 4),
                    "centre_bind": list(centre),
                    "centre_local": list(to_local(bone, centre)),
                }
            )
    return capsules, spheres


def _mesh_maps(d, mesh_base, numverts):
    """The three per-render-vertex maps, or None where this mesh carries none."""
    sel = _i32(d, mesh_base + MESH_SELECTOR)
    pos = _i32(d, mesh_base + MESH_POSITION)
    tan = _i32(d, mesh_base + MESH_TANGENT)
    if not (sel or pos or tan):
        return None
    if not (sel and pos and tan):
        raise ValueError("partial cloth map triple")
    sb = _relative(d, mesh_base, sel, numverts, "selector map")
    pb = _relative(d, mesh_base, pos, numverts * 2, "position map")
    tb = _relative(d, mesh_base, tan, numverts * 2, "tangent map")
    return (
        list(d[sb:sb + numverts]),
        [_u16(d, pb + i * 2) for i in range(numverts)],
        [_u16(d, tb + i * 2) for i in range(numverts)],
    )


def build(d, v):
    """Every garment this model declares, as simulation meshes in Source space.

    `v` is the model's `.dx80.vtx`. Returns one entry per cloth definition, each
    naming the render surfaces it substitutes into so a writer can join it to the
    glb's own vertex order rather than to mesh-local indices.
    """
    if not has_cloth(d):
        return []

    bones = mdl_skel.read_bones(d)
    names = mdl_skel.bone_names(d)
    for i, bone in enumerate(bones):
        bone.name = names[i] if i < len(names) else str(i)

    mesh_map = []
    surfaces = mdl_skel.decode_skinned(d, v, mesh_map)

    # One model-global vertex table: an anchor index and a per-mesh map index
    # resolve through the same door.
    global_pos, global_skin, global_uv = {}, {}, {}
    for rec in mesh_map:
        surf = surfaces[rec["material"]]
        for gvid, si in rec["remap"].items():
            if si < len(surf["pos"]):
                global_pos[gvid] = surf["pos"][si]
                global_uv[gvid] = surf["uv"][si]
                # By NAME, not by index. These weights cross into a host skeleton that
                # renumbers -- the baked family skeleton unions several models' bones --
                # and an index carried over that boundary attaches a hem to whatever bone
                # happens to land on the slot.
                global_skin[gvid] = [
                    (names[j] if j < len(names) else str(j), float(w))
                    for j, w in zip(surf["joints"][si], surf["weights"][si]) if w > 0
                ]

    garments = []
    for bp in range(_i32(d, H_NUM_BODYPARTS)):
        bpr = _i32(d, H_BODYPART_INDEX) + bp * 16
        for m in range(_i32(d, bpr + 4)):
            model_base = bpr + _i32(d, bpr + 12) + m * MODEL_STRIDE
            ndefs = _i32(d, model_base + M_NUM_DEFS)
            if ndefs <= 0:
                continue
            capsules, spheres = read_colliders(d, model_base, bones)
            model_name = _cstr(d, model_base)

            # The model's own vertex pool, addressed by the same model-global id the
            # anchored prefix uses. A render vertex no LOD0 strip references never
            # enters `decode_skinned`'s remap yet still carries its authored position,
            # uv and skin here — which is where the engine reads anchors from.
            pool_verts = _i32(d, model_base + 144)
            pool_index = _i32(d, model_base + 148)
            pool_vlist = _i32(d, model_base + 156)
            pool_stride = mdl.VSTRIDE.get(pool_vlist, 44)
            pool_qoff = _f3(d, model_base + 160)
            pool_qscale = _f3(d, model_base + 172)
            pool_skin = None

            for di in range(ndefs):
                defn = read_definition(d, model_base, di)
                samples = defaultdict(list)
                # The FIRST render vertex to name each particle, not an average of
                # them all: the ones that disagree do so because they sit on a UV
                # seam, where the two coordinates are a texture width apart and
                # their midpoint is a place on the page the garment never uses.
                uv_first = {}
                skin_first = {}
                render_maps = []

                for rec in mesh_map:
                    if rec["model_base"] != model_base:
                        continue
                    mesh_base = model_base + _i32(d, model_base + 140) \
                        + rec["mesh_index"] * MESH_STRIDE
                    numverts = _i32(d, mesh_base + 8)
                    maps = _mesh_maps(d, mesh_base, numverts)
                    if maps is None:
                        continue
                    selectors, position, tangent = maps
                    surf = surfaces[rec["material"]]
                    # Keyed by the surface's own vertex order, which is what the
                    # glb beside this sidecar carries.
                    rows = [None] * len(surf["pos"])
                    for local in range(numverts):
                        if selectors[local] != di:
                            continue
                        si = rec["remap"].get(rec["vertex_offset"] + local)
                        if si is None:
                            continue
                        particle = position[local] & POSITION_INDEX_MASK
                        samples[particle].append(surf["pos"][si])
                        uv_first.setdefault(particle, surf["uv"][si])
                        # The skin of a render vertex that names this particle. VtMB skins only
                        # its pinned prefix, but a Chaos particle needs an animation position
                        # whether or not it is pinned -- max distance and the tethers are both
                        # measured from it. Binding the free ones to the root instead leaves the
                        # garment measuring itself against a rest pose the root drags around.
                        skin_first.setdefault(particle, [
                            (names[j] if j < len(names) else str(j), float(w))
                            for j, w in zip(surf["joints"][si], surf["weights"][si]) if w > 0
                        ])
                        rows[si] = {
                            "particle": particle,
                            "tangent": tangent[local],
                            "flip_normal": bool(position[local] & POSITION_FLIP_NORMAL),
                        }
                    if any(r is not None for r in rows):
                        # The material's OWN geometry travels with the map, because the garment
                        # has to be drawn as the surface it is part of rather than as a surface
                        # of its own. A substituted vertex takes its position from a particle; a
                        # vertex beside it -- the waistband, the collar, the torso the coat is
                        # sewn into -- stays skinned, and it can only stay skinned if it is still
                        # here to be skinned.
                        render_maps.append(
                            {
                                "material": rec["material"],
                                "vertices": rows,
                                "positions": [list(p) for p in surf["pos"]],
                                "uvs": [list(t) for t in surf["uv"]],
                                "skin": [
                                    [[names[j] if j < len(names) else str(j), float(w)]
                                     for j, w in zip(js, ws) if w > 0]
                                    for js, ws in zip(surf["joints"], surf["weights"])
                                ],
                                "triangles": [list(t) for t in surf["tris"]],
                                "_rec": rec,
                            })

                rest = {}
                for particle, pts in samples.items():
                    n = len(pts)
                    rest[particle] = (
                        sum(p[0] for p in pts) / n,
                        sum(p[1] for p in pts) / n,
                        sum(p[2] for p in pts) / n,
                    )
                # The anchored prefix is authoritative wherever both name one.
                anchor_skin = {}
                for k, gvid in enumerate(defn["anchor_vertex_indices"]):
                    if gvid in global_pos:
                        rest[k] = global_pos[gvid]
                        anchor_skin[k] = global_skin.get(gvid, [])
                        skin_first[k] = anchor_skin[k]
                        if gvid in global_uv:
                            uv_first[k] = global_uv[gvid]
                    elif 0 <= gvid < pool_verts:
                        sv = model_base + pool_index + gvid * pool_stride
                        px, py, pz, u, vv = mdl._read_vertex(
                            d, sv, pool_vlist, pool_qoff, pool_qscale)
                        if pool_skin is None:
                            pool_skin = mdl_skel.read_skin(
                                d, model_base, pool_index, pool_verts, pool_vlist)
                        anchor_skin[k] = [
                            (names[j] if j < len(names) else str(j), float(w))
                            for j, w in zip(*pool_skin[gvid]) if w > 0
                        ]
                        rest[k] = (px, py, pz)
                        skin_first[k] = anchor_skin[k]
                        uv_first[k] = (u, vv)

                # Some of the anchored ring is not substituted. The vertices a pinned particle
                # is SKINNED FROM keep an ordinary-skinning selector, so the loop above never
                # claims them -- though a substituted vertex elsewhere may still read that same
                # pinned particle, which is why this only fills rows the loop left empty.
                #
                # That is correct for VtMB, where anchors are simulation-only attachment points
                # joined to the cloth by constraints, and wrong for a surface Chaos has to solve:
                # a particle no triangle references is dropped by the mesh compaction, taking
                # part of the waistband with it and leaving the garment hanging from whichever
                # anchors happened to also be substituted.
                #
                # Claiming those render vertices for their anchor particles is what closes the
                # ring. `anchor` marks them so the substitution map can still tell the two apart:
                # here the garment is its own surface and needs them, in VtMB they were never
                # substituted at all.
                for entry in render_maps:
                    remap = entry["_rec"]["remap"]
                    rows = entry["vertices"]
                    for k, gvid in enumerate(defn["anchor_vertex_indices"]):
                        si = remap.get(gvid)
                        if si is not None and si < len(rows) and rows[si] is None:
                            rows[si] = {
                                "particle": k,
                                "tangent": None,
                                "flip_normal": False,
                                "anchor": True,
                            }
                for entry in render_maps:
                    del entry["_rec"]
                    # How much of this material's surface the garment accounts for. A consumer
                    # that draws the garment separately has to know whether hiding the body's
                    # own section removes the garment or removes the character: a skirt owns
                    # nearly all of its material, a trenchcoat shares one with the torso wearing
                    # it, and a scalp patch is a rounding error on a head.
                    rows = entry["vertices"]
                    entry["claimed"] = sum(1 for r in rows if r)
                    entry["total"] = len(rows)

                missing = [p for p in range(defn["particles"]) if p not in rest]
                if missing:
                    raise ValueError(
                        f"{model_name} cloth {di}: {len(missing)} particles have no rest position"
                    )

                # Topology induced from the render surface, deduplicated: the
                # authored proxy array is not a manifold and cannot serve.
                windings = {}
                for entry in render_maps:
                    surf = surfaces[entry["material"]]
                    rows = entry["vertices"]
                    for tri in surf["tris"]:
                        mapped = [rows[t]["particle"] if t < len(rows) and rows[t] else None
                                  for t in tri]
                        if any(x is None for x in mapped) or len(set(mapped)) < 3:
                            continue
                        # Rotated to start at the lowest index, NOT sorted. Sorting three
                        # vertices is a fine way to deduplicate a triangle and a silent way to
                        # destroy which side of it is the outside -- ascending order is an
                        # arbitrary orientation, so about half the surface ends up wound
                        # backwards. That is invisible against an unlit debug material and is
                        # black triangles and see-through gaps against a real one.
                        i = mapped.index(min(mapped))
                        wound = (mapped[i], mapped[(i + 1) % 3], mapped[(i + 2) % 3])
                        counts = windings.setdefault(tuple(sorted(wound)), {})
                        counts[wound] = counts.get(wound, 0) + 1

                # A garment with a front and a back -- a coat, a robe -- collapses both onto one
                # particle sheet, so the same three particles arrive wound both ways. Keeping
                # both faces makes their normals cancel exactly, and a particle whose normal is
                # zero shades every render vertex it drives black. The sheet has one side; which
                # render vertices read it inverted is what the per-vertex flip bit is for. The
                # winding more render triangles agreed on wins, and first seen breaks a tie.
                triangles = {max(counts.items(), key=lambda kv: kv[1])[0]
                             for counts in windings.values()}

                # What the two constraint sets measure against the rest pose the
                # lines above reconstructed. The distance ratio is the parser's
                # own self-check; the compression ratio is the authored scale,
                # which is not the same number on every garment.
                ratios = {"distance": [], "compression": []}
                for c in defn["constraints"]:
                    authored = c["rest_length_squared"]
                    if authored <= 1e-9:
                        continue
                    pa, pb = rest[c["a"]], rest[c["b"]]
                    actual = sum((pa[i] - pb[i]) ** 2 for i in range(3))
                    ratios["compression" if c["compression_only"] else "distance"].append(
                        actual / authored
                    )
                measured = {}
                for kind, seq in ratios.items():
                    seq.sort()
                    measured[kind] = seq[len(seq) // 2] if seq else None

                garments.append(
                    {
                        "model_name": model_name,
                        "definition": di,
                        "gravity_scale": defn["gravity_scale"],
                        "distance_rest_ratio": measured["distance"],
                        "compression_rest_ratio": measured["compression"],
                        "particle_count": defn["particles"],
                        "anchored_count": defn["anchored"],
                        "rest_positions": [list(rest[i]) for i in range(defn["particles"])],
                        "rest_uvs": [
                            list(uv_first.get(i, (0.0, 0.0)))
                            for i in range(defn["particles"])
                        ],
                        "anchored": [i < defn["anchored"] for i in range(defn["particles"])],
                        "anchor_skin": {str(k): v for k, v in anchor_skin.items()},
                        "particle_skin": [
                            [list(t) for t in skin_first.get(i, [])]
                            for i in range(defn["particles"])
                        ],
                        "triangles": sorted(triangles),
                        "constraints": defn["constraints"],
                        "distance_constraints": defn["distance_constraints"],
                        "compression_constraints": defn["compression_constraints"],
                        "capsules": capsules,
                        "spheres": spheres,
                        "render_maps": render_maps,
                    }
                )
    return garments
