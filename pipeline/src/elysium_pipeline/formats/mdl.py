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

# .vtx vertex-table forms, as (stride, offset of the u16 mesh-vertex id). VtMB writes
# two - a flat u16 id, or a 12-byte record carrying the id at +10 (the leading bytes are
# bone data) - and the StripGroupHeader's own flags byte says which. Retail's static props
# are flat; every non-static model and the Unofficial Patch's 22 recompiled static models
# use the 12-byte record, so STUDIOHDR_FLAGS_STATIC_PROP does not predict the form.
SG_FLAGS = 6                # StripGroupHeader_t.flags, a byte
SG_VERTS_ARE_BONED = 0x08   # 12-byte record, mesh-vertex id at +10
SG_VERTS_ARE_PLAIN = 0x10   # bare u16 array


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


def _read_vertex(d, sv, vlist, offset, scale):
    """(x,y,z,u,v) in Source coords. Skinned exact; the packed formats de-quantize.

    `offset`/`scale` are the owning `StudioModel`'s `PositionOffset`@160 and
    `PositionScale`@172 -- the basis StudioRender's own vertex accessor uses, and the only
    correct one. UNSKINNED multiplies the raw u16 by the scale; COMPRESSED normalizes its
    byte through the same 0..1 table the skin weights use, so the two differ by more than
    their stride. The header hull is a separate authored box that is not the quantization
    fit: on 326 models it is wider than the mesh, and reading positions off it stretches
    them by up to 900 units.

    UNSKINNED and COMPRESSED share one layout: quantized position, one packed normal
    (unused here; regenerated from geometry), then u16 u, u16 v. Reading the normal slot
    as U leaves U constant across every flat face and smears the texture into vertical
    streaks -- see the degeneracy/anisotropy sweep in docs/vtmb/mdl_v2531.md."""
    if vlist == 0:                                  # StudioVertex 44B
        x, y, z = _vec3(d, sv + 12)
        u, v = struct.unpack_from("<2f", d, sv + 36)
    elif vlist == 1:                                # StudioVertex2 12B
        r = [_u16(d, sv + 2 * i) for i in range(6)]
        x, y, z = (offset[c] + r[c] * scale[c] for c in range(3))
        u, v = r[4] / 65535.0, r[5] / 65535.0
    else:                                           # StudioVertex3 8B
        b = d[sv:sv + 8]
        x, y, z = (offset[c] + (b[c] / 255.0) * scale[c] for c in range(3))
        u, v = _u16(d, sv + 4) / 65535.0, _u16(d, sv + 6) / 65535.0
    return (x, y, z, u, v)


def _vtable_form(v, sgb):
    """The stripgroup's vertex-table form as (stride, offset of the u16 mesh-vertex id).

    The flags byte at `StripGroupHeader_t`+6 states it: bit 0x08 selects the 12-byte bone
    record whose id sits at +10, and bit 0x10 the bare u16 array. This is the engine's own
    branch -- StudioRender's stripgroup walk tests that bit to choose between stepping a
    record pointer and indexing at stride 2 -- so the form is read, never inferred.

    A stripgroup setting neither bit names no format and is refused rather than guessed
    around: the wrong form reads bone bytes as vertex ids and yields plausible garbage."""
    flags = v[sgb + SG_FLAGS]
    if flags & SG_VERTS_ARE_BONED:
        return (12, 10)
    if flags & SG_VERTS_ARE_PLAIN:
        return (2, 0)
    raise ValueError(
        f"StripGroupHeader flags 0x{flags:02x} at +{sgb} names no vertex-table form "
        f"(neither 0x{SG_VERTS_ARE_BONED:02x} nor 0x{SG_VERTS_ARE_PLAIN:02x})")


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
            model_base = mbp + model_index + m * 224   # StudioModel stride (mdl_skel.MODEL_STRIDE)
            num_meshes = _i32(d, model_base + 136)
            mesh_index = _i32(d, model_base + 140)  # rel to model
            vertex_index = _i32(d, model_base + 148)  # rel to model
            vlist = _i32(d, model_base + 156)
            vstride = VSTRIDE.get(vlist, 44)
            # The packed formats' de-quantization basis is per model, not the header hull.
            qoff, qscale = _vec3(d, model_base + 160), _vec3(d, model_base + 172)
            vmodel = vbp + vtx_model_off + m * 8
            vtx_lod_off = _i32(v, vmodel + 4)       # rel to vtx model -> LOD0
            vlod = vmodel + vtx_lod_off
            vtx_mesh_off = _i32(v, vlod + 4)        # rel to lod
            for mi in range(num_meshes):
                mesh_base = model_base + mesh_index + mi * 60
                material = _i32(d, mesh_base + 0)
                vertex_offset = _i32(d, mesh_base + 12)
                vmesh = vlod + vtx_mesh_off + mi * 8
                num_sg = _u16(v, vmesh + 0)
                sg_off = _i32(v, vmesh + 4)         # rel to mesh

                tex = fam0[material] if 0 <= material < len(fam0) else material
                out = Mesh(mats[tex] if 0 <= tex < len(mats) else f"mat{tex}", material)
                remap = {}                          # global vert id -> local index
                for sg in range(num_sg):
                    sgb = vmesh + sg_off + sg * STRIPGROUP_STRIDE
                    numstrips = _u16(v, sgb + 4)
                    vtable = sgb + _i32(v, sgb + 8)
                    itable = sgb + _i32(v, sgb + 12)
                    strip_off = _i32(v, sgb + 16)
                    vt_stride, vt_off = _vtable_form(v, sgb)
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
                                    out.verts.append(
                                        _read_vertex(d, sv, vlist, qoff, qscale))
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


#: Process-level memo of texture PNGs already decoded and written, keyed by the absolute output
#: path (so two output roots never collide). A corpus pass resolves the same shared texture from
#: many models, each carrying its own per-model ``tex_cache``; a PNG's name and pixels are a pure
#: function of the source texture and its role, so after the first successful write the decode is
#: pure repetition. The memo holds filenames only, never decoded images -- a derived product
#: first requested by a later model re-decodes its base once -- and it never outlives the
#: process, so a changed install is always re-read on the next run. Task threads may race a key:
#: single-key dict operations are atomic, the loser only repeats one decode, and
#: `tex_to_png.save_png` publishes identical bytes atomically, so no lock is needed.
_png_memo = {}


def _memo_key(tex_out, fn):
    return os.path.normcase(os.path.abspath(os.path.join(tex_out, fn)))


def _redecode(bt, read_bytes):
    """Pixels for a texture whose PNG this process already wrote (`_png_memo` holds filenames,
    not images). The pair decoded once before, so a miss here is a real fault and warns."""
    from elysium_pipeline.formats.tex_to_png import decode as decode_texture
    tth, ttz = read_bytes(f"materials/{bt}.tth"), read_bytes(f"materials/{bt}.ttz")
    if tth and ttz:
        try:
            return decode_texture(tth, ttz).convert("RGBA")
        except Exception as e:
            print(f"    texture re-decode failed ({bt}): {e}")
            return None
    print(f"    texture re-decode failed ({bt}): missing .tth/.ttz pair")
    return None


def _envmask_image(info, bt, img, read_bytes):
    """The L mask for an `_envmask_png` entry that hit `_png_memo` (filename without pixels),
    re-derived without re-writing -- same two sources, same semantics."""
    from elysium_pipeline.formats.tex_to_png import decode as decode_texture
    from PIL import ImageChops
    em = info.get("envmapmask")
    if em:
        src = _norm(em.replace("\\", "/").lstrip("/"))
        tth, ttz = read_bytes(f"materials/{src}.tth"), read_bytes(f"materials/{src}.ttz")
        if tth and ttz:
            try:
                return decode_texture(tth, ttz).convert("L")
            except Exception as e:
                print(f"    envmask re-decode failed ({src}): {e}")
                return None
        print(f"    envmask re-decode failed ({src}): missing .tth/.ttz pair")
        return None
    if info.get("basealphaenvmapmask") and img is not None:
        return ImageChops.invert(img.convert("RGBA").getchannel("A"))
    return None


def _envmask_png(info, bt, img, read_bytes, tex_out, tex_cache):
    """The $envmap reflectivity mask under tex/ as ``(filename, L image)``.

    Both forms are retained because semantic glass uses the same authored mask to keep
    its derived refraction normal off mullions and frames. Two sources, and they are not
    the same channel:

      $envmapmask <tex>        a separate mask texture, used as-is
      $basealphaenvmapmask 1   the BASE texture's alpha, INVERTED

    The inversion is the shipped shader's, not a guess: lightmappedgeneric_basealphamaskedenvmap
    computes `mul r1, t2, 1-t3.a` (docs/vtmb/reflections.md). Cached under a prefixed key so a mask
    shared by several models decodes once, in the same dict the albedos use."""
    from elysium_pipeline.formats.tex_to_png import decode as decode_texture, save_png
    from PIL import Image, ImageChops

    em = info.get("envmapmask")
    if em:
        key = "#envmask:" + _norm(em.replace("\\", "/").lstrip("/"))
        if key not in tex_cache:
            tex_cache[key] = (None, None)
            src = key[len("#envmask:"):]
            fn = sanitize(src) + "_envmask.png"
            if _memo_key(tex_out, fn) in _png_memo:
                # Written earlier this process; the mask image is re-derived on demand.
                tex_cache[key] = (fn, None)
            else:
                tth, ttz = read_bytes(f"materials/{src}.tth"), read_bytes(f"materials/{src}.ttz")
                if tth and ttz:
                    try:
                        mask = decode_texture(tth, ttz).convert("L")
                        save_png(mask, os.path.join(tex_out, fn))
                        _png_memo[_memo_key(tex_out, fn)] = fn
                        tex_cache[key] = (fn, mask)
                    except Exception:
                        pass
        return tex_cache[key]

    if info.get("basealphaenvmapmask") and bt:
        key = "#envmask:" + bt + "#a"
        if key not in tex_cache:
            fn = sanitize(bt) + "_envmask.png"
            if _memo_key(tex_out, fn) in _png_memo:
                # Written earlier this process; the mask image is re-derived on demand.
                tex_cache[key] = (fn, None)
            else:
                if img is None:
                    # The albedo's PNG was memoised earlier this process; the mask needs
                    # its alpha, so the pixels come back through the same re-decode the
                    # selfillum and glass branches use.
                    img = _redecode(bt, read_bytes)
                if img is None:
                    return (None, None)      # base decode failed; there is no alpha to invert
                tex_cache[key] = (None, None)
                try:
                    alpha = img.convert("RGBA").getchannel("A")
                    mask = ImageChops.invert(alpha)
                    save_png(mask, os.path.join(tex_out, fn))
                    _png_memo[_memo_key(tex_out, fn)] = fn
                    tex_cache[key] = (fn, mask)
                except Exception:
                    pass
        return tex_cache[key]
    return (None, None)


#: What `_resolve_material` answers for a material the install does not carry, or one whose VMT
#: names neither a base texture nor a refraction layer.
NO_MATERIAL = {"vmt": "", "albedo": None, "emis": None, "additive": False,
               "translucent": False, "alphatest": False,
               "envmap": None, "envmask": None, "envtint": None,
               "globalwetness": None,
               "glass": False, "bump": None,
               "refract": False, "refract_amount": 0.0, "refract_map": None,
               "iris": None, "vampire": False}


#: The search-path list for a name that is already a full materials-relative path. A world or
#: decal material is resolved by the engine's brush path, which composes `materials/<name>.vmt`
#: and nothing else; only a *model* material goes through the header walk below. Stating the
#: materials root as the one search path is that same single candidate.
WORLD_SEARCH = ("",)


def resolve_vmt(mat, search, read_bytes):
    """``(install path, parsed VMT)`` for a material name, or ``(None, None)``.

    The model's own header search paths, in header order, and nothing after them. That is the
    whole of the engine's resolution: the material system composes exactly
    ``materials/<search path><name>.vmt`` and carries no flat, ``models/``-prefixed or otherwise
    global last resort (research case `material-resolution`). It is also the reason a material
    name alone does not identify a material: two models can name ``spike`` and mean different
    files.

    A total miss is routine rather than a defect -- the engine substitutes its own ``___error``
    checkerboard for the slot and reports it only at a developer level nobody ships -- so the
    caller answers `NO_MATERIAL` and the bake binds a reproduction of that material.

    The returned path carries the install's own spelling, which is what the read used. A model
    header states its search paths and material names in mixed case, so the *corpus key* for the
    resolved material is `shared_corpus.material_key` of this path, not the path itself --
    `material_channels` applies that fold.
    """
    from elysium_pipeline.formats import vmt

    for sp in search:
        candidate = _norm(f"{sp}/{mat}").strip("/")
        raw = read_bytes(_norm(f"materials/{candidate}.vmt"))
        if raw:
            return candidate, vmt.parse(
                raw.decode("ascii", "replace"),
                resolve_include=lambda p: (
                    lambda bb: bb.decode("ascii", "replace") if bb else None)(
                    read_bytes(_norm(p if p.lower().endswith(".vmt") else p + ".vmt"))))
    return None, None


def material_channels(mat, search, read_bytes):
    """A material's render semantics and the **source keys** of every texture it draws.

    Pure resolution: it reads VMTs and nothing else, decodes nothing, and writes nothing. That is
    what lets a corpus decide, over every material at once, which base textures have to keep their
    alpha channel -- a question that has one answer per texture, not one per decode order.

    Returns ``None`` when no VMT resolves, or when the VMT names neither a base texture nor a
    refraction layer. Keys are install-relative and normalized (`shared_corpus.texture_key`'s

    ``vmt`` is the resolved material's **corpus key**, folded by `shared_corpus.material_key` --
    the one form ``shared/materials.json`` is keyed by and the one form a `.mtl`'s ``mat`` line
    names. The fold matters because a model header spells its search paths and material names in
    mixed case while the corpus document is lower case, and every lookup against that document is
    case-sensitive.
    """
    from elysium_pipeline import shared_corpus
    from elysium_pipeline.formats.glass import is_glass

    vmt_path, info = resolve_vmt(mat, search, read_bytes)
    if info is None:
        return None
    bt = info.get("basetexture")
    refract = bool(info.get("refract"))
    # A material with none of these three draws nothing. Water belongs in the test: the `Water`
    # shader carries no `$basetexture` at all -- what it draws is its own normal map plus the fog
    # and reflection tint the surface looks through -- so testing only the first two drops every
    # canal, sewer and pool in the game.
    if not bt and not refract and not info.get("water"):
        return None

    def key(value):
        return _norm(str(value).replace("\\", "/").lstrip("/")) if value else ""

    additive = bool(info.get("additive"))
    translucent = bool(info.get("translucent"))
    alphatest = bool(info.get("alphatest"))
    refract_src = (info.get("normalmap") or info.get("dudvmap")) if refract else None
    return {
        "material": mat,
        "vmt": shared_corpus.material_key(vmt_path),
        "albedo": key(bt),
        # An additive, blended or masked surface reads its own alpha, so the base texture cannot
        # be flattened to RGB. One material asking is enough for the whole corpus.
        "selfillum": bool(info.get("selfillum")),
        "additive": additive,
        "translucent": translucent,
        "alphatest": alphatest,
        "glass": is_glass(info, vmt_path),
        "envmap": sanitize(info["envmap"]) if info.get("envmap") else "",
        "envmap_path": key(info.get("envmap")),
        "envmask": key(info.get("envmapmask")),
        # The base texture's own alpha, inverted, standing in for a mask texture.
        "envmask_from_alpha": bool(
            info.get("basealphaenvmapmask") and not info.get("envmapmask")),
        "envtint": info.get("envmaptint"),
        "globalwetness": info.get("globalwetness"),
        "bump": key(info.get("bumpmap")),
        # WorldVertexTransition's second base texture, mixed against the first by the
        # displacement's own DISPVERT alpha (the map's `.blend` sidecar carries the weights).
        "base_tex2": key(info.get("basetexture2")),
        "refract": refract,
        "refract_amount": float(info.get("refractamount") or 0.0),
        "refract_map": key(refract_src),
        # A DUDV source is signed UVWQ8888 and must be biased into a conventional tangent normal;
        # an authored $normalmap is already one.
        "refract_is_dudv": bool(refract_src and not info.get("normalmap")),
        "iris": key(info.get("iris")),
        "vampire": bool(info.get("vampire")),
        # `$decalscale` and an `unlit*` shader are what an `infodecal` projector needs to size and
        # light itself. Both are VMT facts, so they belong to the material rather than to the map
        # that happens to stick the decal on a wall.
        "decal_scale": float(info.get("decalscale") or 0.0),
        "unlit": str(info.get("shader") or "").lower().startswith("unlit"),
        # The "Water" shader carries no base texture: what it draws is its own normal map plus
        # the fog and reflection tint the surface looks through. The plane it sits at is the one
        # thing a map owns, because it is the median height of that material's own world faces.
        "water": bool(info.get("water")),
        "water_normal": key(info.get("normalmap")) if info.get("water") else "",
        "water_fog_color": info.get("fogcolor"),
        "water_fog_start": info.get("fogstart"),
        "water_fog_end": info.get("fogend"),
        "water_reflect_tint": info.get("reflecttint"),
        "info": info,
    }


def _resolve_material(mat, search, read_bytes, out_dir, tex_cache, *, tex_out=None):
    """material name -> decoded channels plus the VMT's render semantics.

    The albedo PNG (and the self-illum emission mask derived from its alpha) is
    decoded once per basetexture and cached -- per model through ``tex_cache``, and across
    models through the process-level `_png_memo`, which skips the decode entirely once the
    target PNG was written this process. selfillum/additive/translucent/alphatest/envmap
    are per-material (per-VMT), so two materials that share a basetexture but differ in those
    flags do not inherit each other's. Source ``Refract`` is deliberately independent of
    albedo: its authored DUDV/normal map distorts the framebuffer and many such VMTs declare no
    ``$basetexture`` at all.

    The written albedo keeps whatever alpha its source stores (`tex_to_png.save_png`); no
    material's flags decide bytes here, so resolution order cannot change any file.

    ``tex_out`` is where decoded textures land, defaulting to ``<out_dir>/tex``.
    """
    from elysium_pipeline.formats.glass import derive_normal
    from elysium_pipeline.formats.tex_to_png import (
        decode as decode_texture, dudv_to_normal, save_png)
    tex_out = tex_out or os.path.join(out_dir, "tex")
    channels = material_channels(mat, search, read_bytes)
    if channels is None:
        return dict(NO_MATERIAL)
    info = channels["info"]
    bt = channels["albedo"] or None
    refract = channels["refract"]
    additive = channels["additive"]
    translucent = channels["translucent"]
    alphatest = channels["alphatest"]
    glass = channels["glass"]

    # Cache per basetexture: [albedo_png, emis_png_or_None, decoded_rgba_or_None].
    # emis is generated lazily the first time a selfillum material references this
    # texture; the decoded image is held so that generation needs no re-decode.
    # A miss consults `_png_memo` first: a PNG another model's resolve already wrote this
    # process is reused by name, and its image is re-decoded only if a derived product asks.
    albedo = emis = img = None
    if bt:
        ent = tex_cache.get(bt)
        if ent is None:
            fn = sanitize(bt) + ".png"
            if _memo_key(tex_out, fn) in _png_memo:
                ent = tex_cache[bt] = [fn, None, None]
            else:
                tth, ttz = read_bytes(f"materials/{bt}.tth"), read_bytes(f"materials/{bt}.ttz")
                if tth and ttz:
                    try:
                        img = decode_texture(tth, ttz).convert("RGBA")
                        save_png(img, os.path.join(tex_out, fn))
                        _png_memo[_memo_key(tex_out, fn)] = fn
                        albedo = fn
                    except Exception as e:
                        print(f"    texture decode failed for {mat} ({bt}): {e}")
                        img = None
                ent = tex_cache[bt] = [albedo, None, img]

        albedo, emis, img = ent
    if info.get("selfillum") and albedo and emis is None:
        efn = sanitize(bt) + "_ke.png"
        if _memo_key(tex_out, efn) in _png_memo:
            ent[1] = emis = efn
        else:
            if img is None:                  # albedo hit the memo without pixels
                img = ent[2] = _redecode(bt, read_bytes)
            if img is not None:
                import numpy as np
                from PIL import Image
                arr = np.asarray(img.convert("RGBA"), dtype=np.float32)
                a = arr[:, :, 3:4] / 255.0
                masked = (arr[:, :, :3] * a).clip(0, 255).astype("uint8")
                save_png(Image.fromarray(masked, "RGB"), os.path.join(tex_out, efn))
                _png_memo[_memo_key(tex_out, efn)] = efn
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
                info, bt, img, read_bytes, tex_out, tex_cache)
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
                fn = sanitize(refract_src) + "_refract_n.png"
                if _memo_key(tex_out, fn) in _png_memo:
                    tex_cache[key] = fn
                else:
                    tth = read_bytes(f"materials/{refract_src}.tth")
                    ttz = read_bytes(f"materials/{refract_src}.ttz")
                    if tth and ttz:
                        try:
                            decoded = decode_texture(tth, ttz)
                            normal = dudv_to_normal(decoded) if is_dudv else decoded.convert("RGB")
                            save_png(normal, os.path.join(tex_out, fn))
                            _png_memo[_memo_key(tex_out, fn)] = fn
                            tex_cache[key] = fn
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
            fn = sanitize(bump) + "_n.png"
            if _memo_key(tex_out, fn) in _png_memo:
                tex_cache[key] = fn
            else:
                tth, ttz = read_bytes(f"materials/{bump}.tth"), read_bytes(f"materials/{bump}.ttz")
                if tth and ttz:
                    try:
                        save_png(decode_texture(tth, ttz).convert("RGB"),
                                 os.path.join(tex_out, fn))
                        _png_memo[_memo_key(tex_out, fn)] = fn
                        tex_cache[key] = fn
                    except Exception:
                        pass
        bump_png = tex_cache[key]
    elif glass and bt and (img is not None or albedo):
        mask_id = info.get("envmapmask") or ("#basealpha" if info.get("basealphaenvmapmask") else "#alpha")
        key = "#glassnormal:%s|%s" % (bt, mask_id)
        if key not in tex_cache:
            fn = sanitize(bt) + "_glass_n.png"
            if _memo_key(tex_out, fn) in _png_memo:
                tex_cache[key] = fn
            else:
                if img is None:              # albedo hit the memo without pixels
                    img = ent[2] = _redecode(bt, read_bytes)
                if envmask and envmask_img is None and img is not None:
                    envmask_img = _envmask_image(info, bt, img, read_bytes)
                if img is not None:
                    save_png(derive_normal(img, envmask_img), os.path.join(tex_out, fn))
                    _png_memo[_memo_key(tex_out, fn)] = fn
                    tex_cache[key] = fn
        bump_png = tex_cache.get(key)

    # The "Eyes" shader's second layer. Kept RGBA: the iris is composited over the eyeball
    # by its own alpha, so dropping alpha would paint the whole sclera.
    iris_png = None
    iris = info.get("iris")
    if iris:
        iris = _norm(iris.replace("\\", "/").lstrip("/"))
        key = "#iris:" + iris
        if key not in tex_cache:
            tex_cache[key] = None
            fn = sanitize(iris) + "_iris.png"
            if _memo_key(tex_out, fn) in _png_memo:
                tex_cache[key] = fn
            else:
                tth, ttz = read_bytes(f"materials/{iris}.tth"), read_bytes(f"materials/{iris}.ttz")
                if tth and ttz:
                    try:
                        save_png(decode_texture(tth, ttz).convert("RGBA"),
                                 os.path.join(tex_out, fn))
                        _png_memo[_memo_key(tex_out, fn)] = fn
                        tex_cache[key] = fn
                    except Exception as e:
                        print(f"    iris decode failed for {mat} ({iris}): {e}")
        iris_png = tex_cache[key]

    return {
        "vmt": channels["vmt"] or "",
        "albedo": albedo,
        "emis": emis if info.get("selfillum") else None,
        "additive": additive,
        "translucent": translucent,
        "alphatest": alphatest,
        "envmap": envmap,
        "envmask": envmask,
        "envtint": envtint,
        "globalwetness": info.get("globalwetness"),
        "glass": glass,
        "bump": bump_png,
        "refract": refract,
        "refract_amount": float(info.get("refractamount") or 0.0),
        "refract_map": refract_png,
        "iris": iris_png,
        "vampire": bool(info.get("vampire")),
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


def write_obj_scene(meshes, name, out_dir, search, read_bytes, tex_cache, *, skins=None,
                    tex_out=None):
    """Write out_dir/<name>.obj + .mtl, decoding textures into out_dir/tex/
    (shared across models via tex_cache). Vertices are Unreal cm/Z-up/left-handed via
    bsp.source_to_unreal and triangle winding is reversed because the Y negation is a
    reflection. UVs are unchanged and the runtime reads the result verbatim.

    skins: `skin_families(d)` -- when the model has alternate families, every material any of
    them names is resolved into the .mtl too (so the bake authors a material instance for it),
    and out_dir/<name>.skins records the remap. A single-family model writes exactly what it
    always did, byte for byte.

    ``tex_out`` is where textures land -- separate from ``out_dir`` so a corpus can put one
    texture set beside many models' OBJs instead of under each one."""
    tex_out = tex_out or os.path.join(out_dir, "tex")
    os.makedirs(tex_out, exist_ok=True)

    # The .mtl carries the authored set first, in the order the meshes name it (so a model with
    # no alternate families is unchanged), then any material only an alternate family draws.
    names = list(dict.fromkeys(m.material for m in meshes))
    extra = _skin_materials(meshes, skins) - set(names)
    names += sorted(extra)

    mat_png = {n: _resolve_material(n, search, read_bytes, out_dir, tex_cache,
                                    tex_out=tex_out) for n in names}
    _write_skins(meshes, skins, name, out_dir)
    # The `.mtl` names each mesh slot and the corpus material it draws. Every channel and flag
    # lives once in `shared/materials.json`, so a model and a world surface that share a material
    # cannot state different things about it.
    with open(os.path.join(out_dir, name + ".mtl"), "w") as f:
        for mat, m in mat_png.items():
            f.write(f"newmtl {sanitize(mat)}\n")
            if m["vmt"]:
                f.write(f"mat {m['vmt']}\n")
            f.write("\n")
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
    """Read mdl+vtx bytes for an install model path. Returns (d, v) or None.

    `.dx80.vtx` first; `.dx7_2bone.vtx` is the engine-matching fallback for the models
    shipped without one (`fishtank/fish07_school` is the install's sole case), and the
    same parser reads both variants."""
    from elysium_pipeline.formats import install
    stem = model_path[:-4] if model_path.lower().endswith(".mdl") else model_path
    d = install.read(idx, stem + ".mdl")
    v = install.read(idx, stem + ".dx80.vtx") or install.read(idx, stem + ".dx7_2bone.vtx")
    return (d, v) if d and v else None
