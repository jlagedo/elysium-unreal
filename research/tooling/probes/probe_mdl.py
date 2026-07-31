"""Probe a VtMB `.mdl` (v2531) + `.dx80.vtx` against the Spike-0 struct map.

Validates the layout in `docs/vtmb/mdl_v2531.md` on a real model: parses the MDLHeader,
walks bodyparts -> models -> meshes in lockstep with the VTX strip tree, reconstructs
LOD0 triangles, looks up the embedded StudioVertex array, and sanity-checks positions
against the model hull. Prints everything so offsets can be eyeballed/corrected.

Usage: python research/tooling/probes/probe_mdl.py [models/scenery/.../foo.mdl]
Default model: models/scenery/structural/diner/pillars.mdl
"""
import struct, sys, os
from elysium_pipeline.formats import install, vpk

GAME = install.GAME
DEFAULT_MDL = "models/scenery/structural/diner/pillars.mdl"

STATIC_PROP_FLAG = 0x10


def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def i16(b, o): return struct.unpack_from("<h", b, o)[0]
def i32(b, o): return struct.unpack_from("<i", b, o)[0]
def f32(b, o): return struct.unpack_from("<f", b, o)[0]
def vec3(b, o): return struct.unpack_from("<3f", b, o)
def cstr(b, o):
    e = b.index(b"\0", o)
    return b[o:e].decode("ascii", "replace")


def parse_header(d):
    assert d[0:4] == b"IDST", f"bad ident {d[0:4]!r}"
    h = {
        "version": i32(d, 4), "checksum": i32(d, 8),
        "name": cstr(d, 12), "length": i32(d, 140),
        "hull_min": vec3(d, 180), "hull_max": vec3(d, 192),
        "flags": i32(d, 228),
        "num_bones": i32(d, 240),
        "num_textures": i32(d, 292), "texture_index": i32(d, 296),
        "num_tex_search": i32(d, 300), "tex_search_index": i32(d, 304),
        "num_skinref": i32(d, 308), "num_skinfam": i32(d, 312), "skin_index": i32(d, 316),
        "num_bodyparts": i32(d, 320), "bodypart_index": i32(d, 324),
    }
    return h


def probe_texture_stride(d, h):
    """Find the StudioTexture stride by testing which yields all-sane material names."""
    base = h["texture_index"]
    n = h["num_textures"]
    for stride in (20, 24, 64):
        ok, names = True, []
        for i in range(n):
            to = base + i * stride
            ni = i32(d, to)
            try:
                name = cstr(d, to + ni)
            except (ValueError, IndexError):
                ok = False; break
            if not name or not all(32 <= ord(c) < 127 for c in name):
                ok = False; break
            names.append(name)
        if ok:
            return stride, names
    return None, []


def parse_vtx_header(v):
    return {
        "version": i32(v, 0), "num_lods": i32(v, 20),
        "num_bodyparts": i32(v, 28), "bodypart_offset": i32(v, 32),
    }


# Vertex stride by VertexListType (empirically confirmed via TangentsIndex-VertexIndex).
VSTRIDE = {0: 44, 1: 12, 2: 8}
VTYPE_NAME = {0: "SKINNED", 1: "UNSKINNED", 2: "COMPRESSED"}


def read_vertex(d, sv, vlist, hmin, hmax):
    """Return (pos, uv, raw_hex) for one vertex. Skinned is exact; compressed
    formats interpolate HullMin->HullMax (candidate decode to validate)."""
    if vlist == 0:  # StudioVertex 44B: BoneWeight[12], pos@12, normal@24, uv@36
        pos = vec3(d, sv + 12)
        uv = struct.unpack_from("<2f", d, sv + 36)
        raw = d[sv:sv + 44]
    elif vlist == 1:  # StudioVertex2 12B (6x u16) — layout uncertain, best guess
        r = [u16(d, sv + 2 * i) for i in range(6)]
        pos = tuple(hmin[c] + (r[c] / 65535.0) * (hmax[c] - hmin[c]) for c in range(3))
        uv = (r[3] / 65535.0, r[4] / 65535.0)  # guess: shorts 3,4
        raw = d[sv:sv + 12]
    else:  # StudioVertex3 8B: pos bytes @0-2, TexX@5, TexY@7
        b = d[sv:sv + 8]
        pos = tuple(hmin[c] + (b[c] / 255.0) * (hmax[c] - hmin[c]) for c in range(3))
        uv = (b[5] / 255.0, b[7] / 255.0)
        raw = b
    return pos, uv, raw.hex()


def main(mdl_name):
    idx = vpk.index_all(GAME)
    stem = mdl_name[:-4] if mdl_name.endswith(".mdl") else mdl_name
    mdl_name = stem + ".mdl"
    vtx_name = stem + ".dx80.vtx"
    if mdl_name.lower() not in idx:
        print(f"NOT FOUND: {mdl_name}"); return
    d = vpk.extract(idx[mdl_name.lower()])
    v = vpk.extract(idx[vtx_name.lower()]) if vtx_name.lower() in idx else None

    print(f"=== {mdl_name}  ({len(d)}B mdl, {len(v) if v else 0}B vtx) ===")
    h = parse_header(d)
    for k in ("version", "checksum", "name", "length", "flags",
              "num_bones", "num_textures", "num_tex_search",
              "num_skinref", "num_skinfam", "num_bodyparts"):
        print(f"  {k:16} = {h[k]}")
    print(f"  length matches file: {h['length'] == len(d)}")
    print(f"  hull_min = {tuple(round(x,2) for x in h['hull_min'])}")
    print(f"  hull_max = {tuple(round(x,2) for x in h['hull_max'])}")
    static = bool(h["flags"] & STATIC_PROP_FLAG)
    print(f"  STATIC_PROP flag: {static}")

    # Textures / materials.
    stride, names = probe_texture_stride(d, h)
    print(f"\n  StudioTexture stride = {stride}")
    for i, nm in enumerate(names):
        print(f"    material[{i}] = {nm!r}")
    print("  texture search paths:")
    for i in range(h["num_tex_search"]):
        off = i32(d, h["tex_search_index"] + i * 4)
        print(f"    [{i}] {cstr(d, off)!r}")

    if not v:
        print("\n  (no .dx80.vtx — cannot walk geometry)"); return
    vh = parse_vtx_header(v)
    print(f"\n  VTX version={vh['version']} num_lods={vh['num_lods']} "
          f"num_bodyparts={vh['num_bodyparts']}")
    print(f"  bodypart counts match: {vh['num_bodyparts'] == h['num_bodyparts']}")

    # Walk both trees in lockstep (LOD0).
    hmin, hmax = h["hull_min"], h["hull_max"]
    total_tris = 0
    total_verts = 0
    pos_ok = True
    uv_lo = [9e9, 9e9]; uv_hi = [-9e9, -9e9]

    for bp in range(h["num_bodyparts"]):
        # MDL bodypart (16B)
        mbp = h["bodypart_index"] + bp * 16
        num_models = i32(d, mbp + 4)
        model_index = i32(d, mbp + 12)          # rel to bodypart
        # VTX bodypart (8B)
        vbp = vh["bodypart_offset"] + bp * 8
        vtx_num_models = i32(v, vbp + 0)
        vtx_model_off = i32(v, vbp + 4)         # rel to vtx bodypart
        print(f"\n  bodypart[{bp}]: models mdl={num_models} vtx={vtx_num_models}")

        for m in range(num_models):
            model_base = mbp + model_index + m * 160
            num_meshes = i32(d, model_base + 136)
            mesh_index = i32(d, model_base + 140)   # rel to model
            num_vertices = i32(d, model_base + 144)
            vertex_index = i32(d, model_base + 148)  # rel to model
            tangents_index = i32(d, model_base + 152)
            vlist_type = i32(d, model_base + 156)
            total_verts += num_vertices
            emp_stride = ((tangents_index - vertex_index) / num_vertices
                          if num_vertices and tangents_index > vertex_index else 0)
            vstride = VSTRIDE.get(vlist_type, 44)

            # VTX model (8B) -> LOD0 (12B)
            vmodel = vbp + vtx_model_off + m * 8
            vtx_num_lods = i32(v, vmodel + 0)
            vtx_lod_off = i32(v, vmodel + 4)        # rel to vtx model
            vlod = vmodel + vtx_lod_off             # LOD0
            vtx_num_meshes = i32(v, vlod + 0)
            vtx_mesh_off = i32(v, vlod + 4)         # rel to lod

            print(f"    model[{m}]: meshes mdl={num_meshes} vtx={vtx_num_meshes}  "
                  f"verts={num_vertices}  vlist_type={vlist_type}"
                  f"({VTYPE_NAME.get(vlist_type,'?')})  "
                  f"vstride={vstride} empirical={emp_stride:.1f}  VertexIndex={vertex_index}")
            # Dump the first 3 raw verts for layout inspection.
            for vi in range(min(3, num_vertices)):
                p, uv, raw = read_vertex(d, model_base + vertex_index + vi * vstride,
                                         vlist_type, hmin, hmax)
                print(f"      vert[{vi}] raw={raw}  pos={tuple(round(x,2) for x in p)}"
                      f"  uv={tuple(round(x,3) for x in uv)}")

            for mi in range(num_meshes):
                mesh_base = model_base + mesh_index + mi * 60
                material = i32(d, mesh_base + 0)
                mesh_numverts = i32(d, mesh_base + 8)
                vertex_offset = i32(d, mesh_base + 12)

                vmesh = vlod + vtx_mesh_off + mi * 8
                num_sg = u16(v, vmesh + 0)
                sg_off = i32(v, vmesh + 4)          # rel to mesh

                mat_name = names[material] if material < len(names) else "?"
                tris_here = 0
                for sg in range(num_sg):
                    sgb = vmesh + sg_off + sg * 24   # try 24B stride
                    numv = u16(v, sgb + 0)
                    numstrips = u16(v, sgb + 4)
                    vtable_off = i32(v, sgb + 8)
                    index_off = i32(v, sgb + 12)
                    strip_off = i32(v, sgb + 16)
                    vtable = sgb + vtable_off
                    itable = sgb + index_off
                    for s in range(numstrips):
                        sh = sgb + strip_off + s * 16
                        num_indices = u16(v, sh + 0)
                        strip_index_off = u16(v, sh + 2)
                        for k in range(0, num_indices, 3):
                            tri = []
                            for j in range(3):
                                local = u16(v, itable + (strip_index_off + k + j) * 2)
                                mesh_vid = u16(v, vtable + local * 2)
                                gvid = vertex_offset + mesh_vid
                                sv = model_base + vertex_index + gvid * vstride
                                pos, uv, _ = read_vertex(d, sv, vlist_type, hmin, hmax)
                                tri.append((pos, uv))
                                # validate
                                for c in range(3):
                                    if not (hmin[c] - 1 <= pos[c] <= hmax[c] + 1):
                                        pos_ok = False
                                uv_lo[0] = min(uv_lo[0], uv[0]); uv_hi[0] = max(uv_hi[0], uv[0])
                                uv_lo[1] = min(uv_lo[1], uv[1]); uv_hi[1] = max(uv_hi[1], uv[1])
                            tris_here += 1
                total_tris += tris_here
                print(f"      mesh[{mi}]: mat[{material}]={mat_name!r}  "
                      f"numverts={mesh_numverts}  voff={vertex_offset}  "
                      f"stripgroups={num_sg}  tris={tris_here}")

    print(f"\n  TOTAL: {total_tris} triangles, {total_verts} model verts")
    print(f"  positions within hull (+-1): {pos_ok}")
    print(f"  UV range: u[{uv_lo[0]:.2f},{uv_hi[0]:.2f}] v[{uv_lo[1]:.2f},{uv_hi[1]:.2f}]")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_MDL)
