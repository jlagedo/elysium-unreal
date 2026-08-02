"""VtMB `.mdl` v2531 + `.dx80.vtx` decoder → geometry + materials.

Decodes a VtMB static-model into per-material meshes (LOD0). Struct layout and the
three embedded-vertex formats are documented in `docs/vtmb/mdl_v2531.md` and validated by
`research/tooling/probes/probe_mdl.py`. Pure Python, no Bloodlines SDK. Positions are returned in
**Source** coordinates (inches); the Unreal OBJ writer applies the shared Source→Unreal transform.

API:
  decode(mdl_bytes, vtx_bytes) -> list[Mesh]
    Mesh.material : str        material name, resolved through skin family 0
    Mesh.skinref  : int        the skin-table row index an alternate family remaps
    Mesh.verts    : [(x,y,z,u,v), ...]   deduped, Source coords
    Mesh.tris     : [(i,j,k), ...]       indices into verts
  skin_families(mdl_bytes) -> [[material name per skinref], ...]   index 0 = the authored set

"""
import struct, os, re
from elysium_pipeline.formats import bsp

STATIC_PROP_FLAG = 0x10
VSTRIDE = {0: 44, 1: 12, 2: 8}

# `.dx80.vtx` StripGroupHeader stride (numVerts@0, numStrips@4, vertTable@8,
# indexTable@12, stripTable@16 — VtMB's compact 20-byte form). Meshes with a
# single stripgroup (most props) never expose the stride; multi-stripgroup
# skinned meshes do.
STRIPGROUP_STRIDE = 20

# .vtx vertex-table forms, as (stride, offset of the u16 mesh-vertex id).
# VtMB writes two and no header flag distinguishes them: a flat u16 id, or a
# 12-byte record carrying the id at +10 (the leading bytes are bone data).
# Retail's static props are flat; every non-static model and the Unofficial
# Patch's 22 recompiled static models use the 12-byte record - so
# STUDIOHDR_FLAGS_STATIC_PROP does not predict the form.
VTABLE_FORMS = ((2, 0), (12, 10))


def _norm(key):
    """Normalize a model asset key to the index's form.

    Separators come both ways — retail models write texture search paths with `/`,
    the patch's recompiled ones with `\\` — and a path already ending in a separator
    doubles it when joined. Fold both to a single `/`, or the join misses."""
    return re.sub(r"/+", "/", key.replace("\\", "/"))


def _u16(b, o): return struct.unpack_from("<H", b, o)[0]
def _i32(b, o): return struct.unpack_from("<i", b, o)[0]
def _vec3(b, o): return struct.unpack_from("<3f", b, o)
def _cstr(b, o): return b[o:b.index(b"\0", o)].decode("ascii", "replace")


class Mesh:
    __slots__ = ("material", "skinref", "verts", "tris")

    def __init__(self, material, skinref=None):
        self.material = material
        self.skinref = skinref      # StudioMesh.Material -- the row index a skin family remaps
        self.verts = []
        self.tris = []


def _read_vertex(d, sv, vlist, hmin, hmax):
    """(x,y,z,u,v) in Source coords. Skinned exact; compressed hull-interpolate.

    UNSKINNED and COMPRESSED share one layout: quantized position, one packed
    normal (unused; regenerated from geometry), then u16 u, u16 v. Reading the
    normal slot as U leaves U constant across every flat face and smears the
    texture into vertical streaks -- see the degeneracy/anisotropy sweep in
    docs/vtmb/mdl_v2531.md."""
    if vlist == 0:                                  # StudioVertex 44B
        x, y, z = _vec3(d, sv + 12)
        u, v = struct.unpack_from("<2f", d, sv + 36)
    elif vlist == 1:                                # StudioVertex2 12B
        r = [_u16(d, sv + 2 * i) for i in range(6)]
        x, y, z = (hmin[c] + (r[c] / 65535.0) * (hmax[c] - hmin[c]) for c in range(3))
        u, v = r[4] / 65535.0, r[5] / 65535.0
    else:                                           # StudioVertex3 8B
        b = d[sv:sv + 8]
        x, y, z = (hmin[c] + (b[c] / 255.0) * (hmax[c] - hmin[c]) for c in range(3))
        u, v = _u16(d, sv + 4) / 65535.0, _u16(d, sv + 6) / 65535.0
    return (x, y, z, u, v)


def _vtable_form(v, vtable, numverts, mesh_numverts):
    """Pick the vertex-table form: the one whose ids all index the mesh.

    A stripgroup's table maps each of its `numverts` entries to a mesh vertex, so
    the wrong form reads bone bytes as ids and overshoots the mesh's vertex range.
    The right form is the one whose every id lands in `[0, mesh_numverts)`; a
    reading of bone/strip bytes runs out to `0xffff` or collides. Skinned meshes
    reuse a vertex across strips, so ids are **not** all distinct (a stripgroup can
    hold more entries than the mesh has vertices) — coverage, not distinctness, is
    the discriminator, so on the rare tie the widest-covering form wins. Returns
    (stride, offset), or None if no form indexes the mesh."""
    best, best_cover = None, -1
    for stride, off in VTABLE_FORMS:
        try:
            ids = [_u16(v, vtable + k * stride + off) for k in range(numverts)]
        except struct.error:
            continue
        if ids and min(ids) >= 0 and max(ids) < mesh_numverts:
            cover = len(set(ids))
            if cover > best_cover:
                best, best_cover = (stride, off), cover
    return best


def materials(d):
    """The model's texture list -- material names, indexed by texture index.
    (StudioTexture stride 20; NameIndex rel. to the struct base.)"""
    num_textures, texture_index = _i32(d, 292), _i32(d, 296)
    out = []
    for i in range(num_textures):
        to = texture_index + i * 20
        out.append(_cstr(d, to + _i32(d, to)))
    return out


def skin_table(d):
    """The skin table: `skinTable[family][skinref]` -> texture index.

    `StudioMesh.Material` is a **skinref**, not a texture index -- the family row remaps it,
    which is how one model draws several skins (a traffic light's red/walk/flashing/yellow, a
    doorknob's locked/unlocked, a laser emitter's armed/idle). Family 0 is the identity row on
    every readable model in the install, which is why reading a mesh's material index directly
    yielded the right skin-0 material and nothing else.

    `NumSkinRefs`@308, `NumSkinFamilies`@312, `SkinIndex`@316; the table is
    `short skinref[families][refs]`. A model whose table is absent or unreadable (12 in the
    install) yields a single identity row, so callers need no special case."""
    n_refs, n_fams, base = _i32(d, 308), _i32(d, 312), _i32(d, 316)
    n_tex = _i32(d, 292)
    if (n_refs < 1 or n_fams < 1 or base <= 0
            or base + n_fams * n_refs * 2 > len(d)):
        return [list(range(max(n_refs, n_tex, 1)))]
    return [list(struct.unpack_from(f"<{n_refs}h", d, base + f * n_refs * 2))
            for f in range(n_fams)]


def skin_families(d):
    """`skin_table` resolved through the texture list: per family, the material name for each
    skinref. Index 0 is the authored set -- what the OBJ's own `usemtl` groups are named."""
    mats = materials(d)
    return [[mats[t] if 0 <= t < len(mats) else f"mat{t}" for t in row]
            for row in skin_table(d)]


def decode(d, v):
    """Decode mdl bytes `d` + vtx bytes `v` → list[Mesh] (LOD0, Source coords)."""
    assert d[0:4] == b"IDST", f"bad ident {d[0:4]!r}"
    assert v is not None, "missing .dx80.vtx"
    hmin, hmax = _vec3(d, 180), _vec3(d, 192)
    mats = materials(d)
    # A mesh names a skinref; family 0 maps it to the texture it draws with (identity on every
    # readable model, so this is a no-op there -- but it is the correct lookup, not a coincidence).
    fam0 = skin_table(d)[0]
    num_bodyparts = _i32(d, 320)
    bodypart_index = _i32(d, 324)
    vtx_bp_off = _i32(v, 32)

    meshes = []
    for bp in range(num_bodyparts):
        mbp = bodypart_index + bp * 16
        num_models = _i32(d, mbp + 4)
        model_index = _i32(d, mbp + 12)             # rel to bodypart
        vbp = vtx_bp_off + bp * 8
        vtx_model_off = _i32(v, vbp + 4)            # rel to vtx bodypart
        for m in range(num_models):
            model_base = mbp + model_index + m * 160
            num_meshes = _i32(d, model_base + 136)
            mesh_index = _i32(d, model_base + 140)  # rel to model
            vertex_index = _i32(d, model_base + 148)  # rel to model
            vlist = _i32(d, model_base + 156)
            vstride = VSTRIDE.get(vlist, 44)
            vmodel = vbp + vtx_model_off + m * 8
            vtx_lod_off = _i32(v, vmodel + 4)       # rel to vtx model -> LOD0
            vlod = vmodel + vtx_lod_off
            vtx_mesh_off = _i32(v, vlod + 4)        # rel to lod
            for mi in range(num_meshes):
                mesh_base = model_base + mesh_index + mi * 60
                material = _i32(d, mesh_base + 0)
                mesh_numverts = _i32(d, mesh_base + 8)
                vertex_offset = _i32(d, mesh_base + 12)
                vmesh = vlod + vtx_mesh_off + mi * 8
                num_sg = _u16(v, vmesh + 0)
                sg_off = _i32(v, vmesh + 4)         # rel to mesh

                tex = fam0[material] if 0 <= material < len(fam0) else material
                out = Mesh(mats[tex] if 0 <= tex < len(mats) else f"mat{tex}", material)
                remap = {}                          # global vert id -> local index
                for sg in range(num_sg):
                    sgb = vmesh + sg_off + sg * STRIPGROUP_STRIDE
                    sg_numverts = _u16(v, sgb + 0)
                    numstrips = _u16(v, sgb + 4)
                    vtable = sgb + _i32(v, sgb + 8)
                    itable = sgb + _i32(v, sgb + 12)
                    strip_off = _i32(v, sgb + 16)
                    form = _vtable_form(v, vtable, sg_numverts, mesh_numverts)
                    if form is None:
                        raise ValueError(
                            f"unrecognized .vtx vertex-table form "
                            f"(mesh {mi}, stripgroup {sg}, {sg_numverts} verts)")
                    vt_stride, vt_off = form
                    for s in range(numstrips):
                        sh = sgb + strip_off + s * 16
                        num_indices = _u16(v, sh + 0)
                        strip_index_off = _u16(v, sh + 2)
                        for k in range(0, num_indices, 3):
                            tri = []
                            for j in range(3):
                                local = _u16(v, itable + (strip_index_off + k + j) * 2)
                                gvid = vertex_offset + _u16(v, vtable + local * vt_stride + vt_off)
                                if gvid not in remap:
                                    remap[gvid] = len(out.verts)
                                    sv = model_base + vertex_index + gvid * vstride
                                    out.verts.append(_read_vertex(d, sv, vlist, hmin, hmax))
                                tri.append(remap[gvid])
                            out.tris.append(tuple(tri))
                if out.tris:
                    meshes.append(out)
    return meshes


def search_paths(d):
    """Header texture search paths (header-relative offset table)."""
    n = _i32(d, 300); base = _i32(d, 304)
    return [_cstr(d, _i32(d, base + i * 4)) for i in range(n)]


def sanitize(name):
    return "".join(c if c.isalnum() or c in "._-" else "_" for c in name.lower())


# --- reusable Unreal OBJ-scene writer (shared texture pipeline, world-compatible) ---


def _envmask_png(info, bt, img, read_bytes, out_dir, tex_cache):
    """The $envmap reflectivity mask under tex/ as ``(filename, L image)``.

    Both forms are retained because semantic glass uses the same authored mask to keep
    its derived refraction normal off mullions and frames. Two sources, and they are not
    the same channel:

      $envmapmask <tex>        a separate mask texture, used as-is
      $basealphaenvmapmask 1   the BASE texture's alpha, INVERTED

    The inversion is the shipped shader's, not a guess: lightmappedgeneric_basealphamaskedenvmap
    computes `mul r1, t2, 1-t3.a` (docs/vtmb/reflections.md). Cached under a prefixed key so a mask
    shared by several models decodes once, in the same dict the albedos use."""
    from elysium_pipeline.formats.tex_to_png import decode as decode_texture
    from PIL import Image, ImageChops

    em = info.get("envmapmask")
    if em:
        key = "#envmask:" + _norm(em.replace("\\", "/").lstrip("/"))
        if key not in tex_cache:
            tex_cache[key] = (None, None)
            src = key[len("#envmask:"):]
            tth, ttz = read_bytes(f"materials/{src}.tth"), read_bytes(f"materials/{src}.ttz")
            if tth and ttz:
                try:
                    fn = sanitize(src) + "_envmask.png"
                    mask = decode_texture(tth, ttz).convert("L")
                    mask.save(os.path.join(out_dir, "tex", fn))
                    tex_cache[key] = (fn, mask)
                except Exception:
                    pass
        return tex_cache[key]

    if info.get("basealphaenvmapmask") and img is not None:
        key = "#envmask:" + bt + "#a"
        if key not in tex_cache:
            tex_cache[key] = (None, None)
            try:
                fn = sanitize(bt) + "_envmask.png"
                alpha = img.convert("RGBA").getchannel("A")
                mask = ImageChops.invert(alpha)
                mask.save(os.path.join(out_dir, "tex", fn))
                tex_cache[key] = (fn, mask)
            except Exception:
                pass
        return tex_cache[key]
    return (None, None)


def _resolve_material(mat, search, read_bytes, out_dir, tex_cache):
    """material name -> decoded channels plus the VMT's render semantics.

    The albedo PNG (and the self-illum emission mask derived from its alpha) is
    decoded once per basetexture and cached; selfillum/additive/translucent/alphatest/envmap
    are per-material (per-VMT), so two materials that share a basetexture but differ in those
    flags do not inherit each other's. Source ``Refract`` is deliberately independent of
    albedo: its authored DUDV/normal map distorts the framebuffer and many such VMTs declare no
    ``$basetexture`` at all."""
    from elysium_pipeline.formats import vmt
    from elysium_pipeline.formats.glass import derive_normal, is_glass
    from elysium_pipeline.formats.tex_to_png import decode as decode_texture, dudv_to_normal
    none = {"albedo": None, "emis": None, "additive": False,
            "translucent": False, "alphatest": False,
            "envmap": None, "envmask": None, "envtint": None,
            "glass": False, "bump": None,
            "refract": False, "refract_amount": 0.0, "refract_map": None}
    vmt_txt = vmt_path = None
    for sp in search:
        candidate = _norm(f"{sp}/{mat}").strip("/")
        b = read_bytes(_norm(f"materials/{candidate}.vmt"))
        if b:
            vmt_txt = b.decode("ascii", "replace")
            vmt_path = candidate
            break
    if vmt_txt is None:                       # last resort: flat materials/<mat>.vmt
        b = read_bytes(f"materials/{mat}.vmt")
        if b:
            vmt_txt = b.decode("ascii", "replace")
            vmt_path = mat
    if not vmt_txt:
        return dict(none)
    info = vmt.parse(vmt_txt, resolve_include=lambda p: (
        lambda bb: bb.decode("ascii", "replace") if bb else None)(
        read_bytes(_norm(p if p.lower().endswith(".vmt") else p + ".vmt"))))
    bt = info.get("basetexture")
    refract = bool(info.get("refract"))
    if not bt and not refract:
        return dict(none)
    if bt:
        bt = _norm(bt.replace("\\", "/").lstrip("/"))  # prop VMTs carry leading/doubled slashes
    additive = bool(info.get("additive"))
    translucent = bool(info.get("translucent"))
    alphatest = bool(info.get("alphatest"))
    glass = is_glass(info, vmt_path)
    needs_alpha = additive or translucent or alphatest

    # Cache per basetexture:
    # [albedo_png, emis_png_or_None, decoded_rgba_or_None, alpha_preserved].
    # emis is generated lazily the first time a selfillum material references this
    # texture; the decoded image is held so that generation needs no re-decode. Alpha
    # preservation is promoted: if an opaque material resolves a shared basetexture first,
    # a later translucent/masked/additive material rewrites that same PNG as RGBA.
    albedo = emis = img = None
    if bt:
        ent = tex_cache.get(bt)
        if ent is None:
            tth, ttz = read_bytes(f"materials/{bt}.tth"), read_bytes(f"materials/{bt}.ttz")
            if tth and ttz:
                try:
                    img = decode_texture(tth, ttz).convert("RGBA")
                    fn = sanitize(bt) + ".png"
                    (img if needs_alpha else img.convert("RGB")).save(
                        os.path.join(out_dir, "tex", fn))
                    albedo = fn
                except Exception as e:
                    print(f"    texture decode failed for {mat} ({bt}): {e}")
                    img = None
            ent = tex_cache[bt] = [albedo, None, img, bool(albedo and needs_alpha)]

        albedo, emis, img, alpha_preserved = ent
        if needs_alpha and albedo and img is not None and not alpha_preserved:
            img.save(os.path.join(out_dir, "tex", albedo))
            ent[3] = True
    if info.get("selfillum") and albedo and emis is None and img is not None:
        import numpy as np
        from PIL import Image
        arr = np.asarray(img.convert("RGBA"), dtype=np.float32)
        a = arr[:, :, 3:4] / 255.0
        masked = (arr[:, :, :3] * a).clip(0, 255).astype("uint8")
        efn = sanitize(bt) + "_ke.png"
        Image.fromarray(masked, "RGB").save(os.path.join(out_dir, "tex", efn))
        ent[1] = emis = efn

    # $envmap. VtMB's models are the LARGER half of the reflective set (1,419 of 2,610
    # $envmap VMTs are vertexlitgeneric), and vertexlitgeneric_maskedenvmap composites it
    # exactly as the world's lightmappedgeneric does -- `base + cube*mask*tint`, then times
    # the lighting -- so props take the same channel the world surfaces do
    # (docs/vtmb/reflections.md). The cube ID is carried for the record only; the Lumen path
    # never samples it, and a prop's $envmap is `env_cubemap` (runtime-resolved) far more
    # often than a named cube.
    envmap = envmask = envmask_img = envtint = None
    if info.get("envmap"):
        envmap = sanitize(info["envmap"])
        if bt:
            envmask, envmask_img = _envmask_png(
                info, bt, img, read_bytes, out_dir, tex_cache)
        envtint = info.get("envmaptint")

    # Source Refract is an explicit framebuffer-distortion layer. Prefer the authored tangent
    # normal on materials that carry both hardware paths; the old $dudvmap fallback is signed
    # UVWQ8888 and must be biased into a conventional tangent normal before UE imports it.
    refract_png = None
    if refract:
        refract_src = info.get("normalmap") or info.get("dudvmap")
        if refract_src:
            refract_src = _norm(refract_src.replace("\\", "/").lstrip("/"))
            is_dudv = not info.get("normalmap")
            key = "#refract:%s:%s" % ("dudv" if is_dudv else "normal", refract_src)
            if key not in tex_cache:
                tex_cache[key] = None
                tth = read_bytes(f"materials/{refract_src}.tth")
                ttz = read_bytes(f"materials/{refract_src}.ttz")
                if tth and ttz:
                    try:
                        decoded = decode_texture(tth, ttz)
                        normal = dudv_to_normal(decoded) if is_dudv else decoded.convert("RGB")
                        refract_png = sanitize(refract_src) + "_refract_n.png"
                        normal.save(os.path.join(out_dir, "tex", refract_png))
                        tex_cache[key] = refract_png
                    except Exception as e:
                        print(f"    refract decode failed for {mat} ({refract_src}): {e}")
            refract_png = tex_cache[key]

    # Props use the same bumpmap channel as world surfaces. Real authored normals win;
    # otherwise semantic glass derives a mild ripple from its retained RGBA albedo and
    # confines that ripple with the same reflectivity mask exported above.
    bump_png = None
    bump = info.get("bumpmap")
    if bump:
        bump = _norm(bump.replace("\\", "/").lstrip("/"))
        key = "#normal:" + bump
        if key not in tex_cache:
            tex_cache[key] = None
            tth, ttz = read_bytes(f"materials/{bump}.tth"), read_bytes(f"materials/{bump}.ttz")
            if tth and ttz:
                try:
                    bump_png = sanitize(bump) + "_n.png"
                    decode_texture(tth, ttz).convert("RGB").save(
                        os.path.join(out_dir, "tex", bump_png))
                    tex_cache[key] = bump_png
                except Exception:
                    pass
        bump_png = tex_cache[key]
    elif glass and img is not None and bt:
        mask_id = info.get("envmapmask") or ("#basealpha" if info.get("basealphaenvmapmask") else "#alpha")
        key = "#glassnormal:%s|%s" % (bt, mask_id)
        if key not in tex_cache:
            bump_png = sanitize(bt) + "_glass_n.png"
            derive_normal(img, envmask_img).save(os.path.join(out_dir, "tex", bump_png))
            tex_cache[key] = bump_png
        bump_png = tex_cache[key]

    return {
        "albedo": albedo,
        "emis": emis if info.get("selfillum") else None,
        "additive": additive,
        "translucent": translucent,
        "alphatest": alphatest,
        "envmap": envmap,
        "envmask": envmask,
        "envtint": envtint,
        "glass": glass,
        "bump": bump_png,
        "refract": refract,
        "refract_amount": float(info.get("refractamount") or 0.0),
        "refract_map": refract_png,
    }


def _skin_remaps(meshes, skins):
    """Per alternate family, [(authored material, this family's material)] for the slots it
    actually repaints. Only skinrefs the model's meshes draw are considered -- a skin table row
    covers every skinref, including ones no LOD0 mesh uses. Keyed by the authored material name,
    because that is the OBJ group (and so the mesh material slot) the swap targets; on the rare
    model where two skinrefs share one authored material, the first wins."""
    if not skins or len(skins) < 2:
        return {}
    refs = sorted({m.skinref for m in meshes if m.skinref is not None})
    base = skins[0]
    out = {}
    for f in range(1, len(skins)):
        row, pairs, seen = skins[f], [], set()
        for r in refs:
            if r >= len(base) or r >= len(row) or base[r] == row[r]:
                continue
            if base[r] in seen:
                continue
            seen.add(base[r])
            pairs.append((base[r], row[r]))
        if pairs:
            out[f] = pairs
    return out


def _skin_materials(meshes, skins):
    """Every material an alternate family draws -- these need a .mtl entry (and so a decoded
    texture and a baked material instance) even though no triangle references them at skin 0."""
    return {rep for pairs in _skin_remaps(meshes, skins).values() for _, rep in pairs}


def _write_skins(meshes, skins, name, out_dir):
    """out_dir/<name>.skins -- one line per alternate family that repaints something:

        <family> <authored material>=<family material> ...

    Names are sanitized, so they match the `usemtl`/`newmtl` keys and the baked mesh's material
    slot names 1:1. Families that repaint nothing are simply absent (the leading token carries
    the family number, so gaps are fine). No file is written for a single-family model."""
    remaps = _skin_remaps(meshes, skins)
    path = os.path.join(out_dir, name + ".skins")
    if not remaps:
        if os.path.exists(path):
            os.remove(path)   # a model that lost its families must not keep a stale sidecar
        return
    with open(path, "w") as f:
        for fam in sorted(remaps):
            pairs = " ".join(f"{sanitize(a)}={sanitize(b)}" for a, b in remaps[fam])
            f.write(f"{fam} {pairs}\n")


def write_obj_scene(meshes, name, out_dir, search, read_bytes, tex_cache, *, skins=None):
    """Write out_dir/<name>.obj + .mtl, decoding textures into out_dir/tex/
    (shared across models via tex_cache). Vertices are Unreal cm/Z-up/left-handed via
    bsp.source_to_unreal and triangle winding is reversed because the Y negation is a
    reflection. UVs are unchanged and the runtime reads the result verbatim.

    skins: `skin_families(d)` -- when the model has alternate families, every material any of
    them names is resolved into the .mtl too (so the bake authors a material instance for it),
    and out_dir/<name>.skins records the remap. A single-family model writes exactly what it
    always did, byte for byte."""
    os.makedirs(os.path.join(out_dir, "tex"), exist_ok=True)

    # The .mtl carries the authored set first, in the order the meshes name it (so a model with
    # no alternate families is unchanged), then any material only an alternate family draws.
    names = list(dict.fromkeys(m.material for m in meshes))
    extra = _skin_materials(meshes, skins) - set(names)
    names += sorted(extra)

    mat_png = {n: _resolve_material(n, search, read_bytes, out_dir, tex_cache) for n in names}
    _write_skins(meshes, skins, name, out_dir)
    with open(os.path.join(out_dir, name + ".mtl"), "w") as f:
        for mat, m in mat_png.items():
            f.write(f"newmtl {sanitize(mat)}\n")
            if m["albedo"]:
                f.write(f"map_Kd tex/{m['albedo']}\n")
            else:
                f.write("Kd 0.6 0.6 0.62\n")
            if m["emis"]:
                f.write(f"map_Ke tex/{m['emis']}\n")
            if m["additive"]:
                f.write("additive 1\n")             # our flag: additive glow overlay (unlit)
            elif m["translucent"]:
                f.write("blend 1\n")                # our flag: alpha-blended
            elif m["alphatest"]:
                f.write("illum 4\n")                # our flag: alpha-tested (scissor)
            # Same envmap lines UE_bsp_to_scene writes for a reflective world surface, so one
            # MTL contract covers world and props. $envmapcontrast/$envmapsaturation are not
            # emitted: VtMB's shipped shaders carry no term for either (docs/vtmb/reflections.md).
            if m["envmap"]:
                f.write(f"envmap {m['envmap']}\n")
                if m["envmask"]:
                    f.write(f"envmapmask tex/{m['envmask']}\n")
                t = m["envtint"] or [1.0, 1.0, 1.0]
                f.write(f"envtint {t[0]:.4f} {t[1]:.4f} {t[2]:.4f}\n")
            if m["glass"]:
                f.write("glass 1\n")                # our semantic: UE thin refractive glass
            if m["bump"]:
                f.write(f"bumpmap tex/{m['bump']}\n")
            if m["refract"]:
                # Source Refract is a separate transparent framebuffer-distortion card, not
                # the underlying glass albedo. Preserve its authored amount and vector field.
                f.write(f"refract {m['refract_amount']:.6f}\n")
                if m["refract_map"]:
                    f.write(f"refractmap tex/{m['refract_map']}\n")
    with open(os.path.join(out_dir, name + ".obj"), "w") as f:
        f.write(f"mtllib {name}.mtl\n")
        vbase = 1
        for mesh in meshes:
            for (x, y, z, u, vv) in mesh.verts:
                ux, uy, uz = bsp.source_to_unreal(x, y, z)
                f.write(f"v {ux:.6f} {uy:.6f} {uz:.6f}\n")
            for (_, _, _, u, vv) in mesh.verts:
                f.write(f"vt {u:.6f} {vv:.6f}\n")
            f.write(f"usemtl {sanitize(mesh.material)}\n")
            for (a, b, c) in mesh.tris:
                a, b, c = a + vbase, b + vbase, c + vbase
                # Unreal space is reflected (det -1), so reverse winding to stay front-facing.
                f.write(f"f {a}/{a} {c}/{c} {b}/{b}\n")
            vbase += len(mesh.verts)
    return sum(len(m.verts) for m in meshes), sum(len(m.tris) for m in meshes)


def load(idx, model_path):
    """Read mdl+dx80.vtx bytes for an install model path. Returns (d, v) or None."""
    from elysium_pipeline.formats import install
    stem = model_path[:-4] if model_path.lower().endswith(".mdl") else model_path
    d, v = install.read(idx, stem + ".mdl"), install.read(idx, stem + ".dx80.vtx")
    return (d, v) if d and v else None
