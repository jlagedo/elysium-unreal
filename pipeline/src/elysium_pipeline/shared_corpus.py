"""The shared asset corpus: one decode per source, addressed by install identity.

A texture, a material and a static model are properties of the user's install, not of a map.
``materials/metal/metalox`` decodes to the same bytes whichever map's BSP named it, and
``models/scenery/structural/doorknoba/doorknoba.mdl`` is one doorknob however many maps hang it on
a door. So the corpus decodes each one once and every map references the single asset.

The identity is the **install-relative source path**, never the exported filename. That
distinction is load-bearing rather than tidy: a map's sky faces are written under the map-local
alias ``sky_<face>``, so keyed by filename two maps sharing a sky present one name carrying two
sets of bytes. Keyed by the sky's own name they are one asset, which is what they are.

`character_partition` states the same discipline for bank rigs -- compute once over the whole
corpus, write it down, and let readers join rather than recompute. This module owns the static
half's keys, its shared/per-map predicate, and the two documents the corpus writes:

- ``shared/manifest.json`` is the census: which units landed, from which install key, and what the
  decode could not resolve.
- ``shared/materials.json`` is the definition: what each material *means*, once. Two maps can no
  longer disagree about one material, because there is only one record to read.

Both are game-derived like every other export product: regenerated, never tracked.
"""
from __future__ import annotations

import hashlib
import re

MANIFEST_SCHEMA = "elysium.shared-corpus"
MATERIALS_SCHEMA = "elysium.shared-corpus-materials"
VERSION = 1
#: Bumping this invalidates every corpus receipt. Bump it when the shape of a record changes, not
#: when the install does -- the install is covered by ``corpus_fingerprint``.
REVISION = "elysium-shared-corpus-v1"

REGENERATE = "re-run: uv run elysium export bundle corpus"

# --- the corpus on disk, under $ELYSIUM_EXPORT_ROOT --------------------------------------------
ROOT = "shared"
TEX = "tex"
#: The offline enhancement track's parallel set, same keys, same filenames.
TEX_HI = "tex_hi"
#: ``props`` rather than ``models``: it is the directory layout the bake's prop stage reads.
PROPS = "props"
MANIFEST = "manifest.json"
MATERIALS = "materials.json"

# --- the corpus on the /ElysiumBaked mount ------------------------------------------------------
BAKED_ROOT = "/ElysiumBaked/Shared"
BAKED_TEXTURES = BAKED_ROOT + "/Textures"
BAKED_MATERIALS = BAKED_ROOT + "/Materials"
BAKED_MESHES = BAKED_ROOT + "/Meshes"

#: The bake scope name, in the same position `items` held: a receipt store, an asset run plan and
#: a set of stages that is not a map's.
SCOPE = "shared"
#: The one prop skin table. VtMB's alternate skin families are a property of a model, and
#: the material each family repaints is now one shared instance, so the table that joins them
#: is global rather than one copy per map.
PROP_SKINS_ASSET = "DA_ElysiumPropSkins"
STAGES = ("textures", "materials", "props")

#: A world material instance patched to a baked env cubemap carries this separator plus the cube's
#: own id. The cube is per map and per position, so the tag is what makes such a material
#: map-scoped -- see `is_map_scoped_material`.
CUBEMAP_TAG = "@"


# ---------------------------------------------------------------------------- paths

def corpus_dir(export_root):
    """The corpus root under an export root. `export_root` is a ``Path`` or a string."""
    from pathlib import Path

    return Path(export_root) / ROOT


def tex_dir(export_root):
    return corpus_dir(export_root) / TEX


def tex_hi_dir(export_root):
    return corpus_dir(export_root) / TEX_HI


def props_dir(export_root):
    return corpus_dir(export_root) / PROPS


def manifest_path(export_root):
    return corpus_dir(export_root) / MANIFEST


def materials_path(export_root):
    return corpus_dir(export_root) / MATERIALS


# ------------------------------------------------------------------------ key rules

def _slashes(value):
    """Lower case, forward slashes, no doubled or edge separator."""
    return re.sub(r"/+", "/", str(value or "").replace("\\", "/").lower()).strip("/")


def texture_key(value):
    """A VMT texture reference as its install key: ``metal/metalox``.

    Accepts every spelling the authored files use -- a leading slash, a doubled separator, an
    explicit ``materials/`` prefix, a ``.tth``/``.ttz``/``.vtf`` extension -- because prop VMTs
    carry all of them and the engine resolves them alike.
    """
    key = _slashes(value)
    if key.startswith("materials/"):
        key = key[len("materials/"):]
    for suffix in (".tth", ".ttz", ".vtf"):
        if key.endswith(suffix):
            key = key[: -len(suffix)]
            break
    return key


def material_key(value):
    """A resolved ``.vmt`` path as its install key: ``models/scenery/.../spike``.

    This is a prop material's identity. A world material's identity is `world_material_key`,
    which additionally carries the baked cubemap tag VBSP patched into the face's name.
    """
    key = _slashes(value)
    if key.startswith("materials/"):
        key = key[len("materials/"):]
    if key.endswith(".vmt"):
        key = key[: -len(".vmt")]
    return key


def base_material(name):
    """A texdata material string with VBSP's map-baked cubemap decoration removed.

    VBSP patches an ``$envmap`` face to a per-map copy of its material -- ``maps/<map>/<mat>`` for
    the map-wide cube, ``maps/<map>/<mat>_x_y_z`` for a positioned one. The authored material is
    what is left after both.
    """
    n = _slashes(name)
    n = re.sub(r"^maps/[^/]+/", "", n)
    return re.sub(r"_-?\d+_-?\d+_-?\d+$", "", n)


def cubemap_of(name, mapbase):
    """The baked env cubemap a face samples, or ``None`` when the face is not env-patched.

    ``maps/<map>/<mat>`` samples the map-wide ``cubemapdefault``; ``maps/<map>/<mat>_x_y_z``
    samples the positioned ``c<x>_<y>_<z>`` -- the same suffix `base_material` strips. The value
    is the cubemap texture's stem under ``materials/maps/<map>/``.
    """
    n = _slashes(name)
    if not n.startswith(f"maps/{_slashes(mapbase)}/"):
        return None
    tag = re.search(r"_(-?\d+_-?\d+_-?\d+)$", n)
    return ("c" + tag.group(1)) if tag else "cubemapdefault"


def world_material_key(name, mapbase):
    """One world surface's material identity: the authored material, plus its cubemap when
    VBSP patched one in. ``asphalt/asphaltasan@cubemapdefault``, ``art/bdiorama1``."""
    base = base_material(name)
    cube = cubemap_of(name, mapbase)
    return f"{base}{CUBEMAP_TAG}{cube}" if cube else base


def static_stem(model_path):
    """A ``.mdl`` path as the corpus's model key -- the whole path folded, ``models/`` included.

    ``models/items/Rings/Ground/Ring03.mdl`` -> ``models_items_rings_ground_ring03``. Its C++ twin
    is `FElysiumContentPaths::PropModelStem`, and it is what the OBJ, the ``.props`` row's first
    field and the baked ``SM_`` asset are all named.

    Not to be confused with `placed_models.model_stem`, which drops the leading ``models/`` and
    keys the skeletal placed-model catalogue instead. The two identities stay separate.
    """
    from elysium_pipeline.formats import mdl

    path = _slashes(model_path)
    return mdl.sanitize(path[: -len(".mdl")] if path.endswith(".mdl") else path)


# --------------------------------------------------------------- decoded file names

#: Every decoded product of one texture key, by suffix. A key yields at most one file per suffix.
#: ``ENV_MASK`` has two sources -- an authored ``$envmapmask`` texture, and a base texture's own
#: inverted alpha under ``$basealphaenvmapmask`` -- which share this suffix under their own keys.
#: No install texture is named by both, and `build_manifest` fails loudly if one ever is.
ALBEDO = ""
EMISSIVE = "_ke"
ENV_MASK = "_envmask"
NORMAL = "_n"
GLASS_NORMAL = "_glass_n"
REFRACT_NORMAL = "_refract_n"
IRIS = "_iris"

#: The role `bake_lib.configure_texture` must apply to each product: its compression setting and
#: colour space follow from what the channel means, not from the file.
ROLES = {
    ALBEDO: "albedo",
    EMISSIVE: "albedo",
    ENV_MASK: "mask",
    NORMAL: "normal",
    GLASS_NORMAL: "normal",
    REFRACT_NORMAL: "normal",
    IRIS: "albedo",
}


def texture_file(key, suffix=ALBEDO):
    """The corpus filename one decoded product lands under: ``metal_metalox_envmask.png``.

    The fold is `mdl.sanitize` -- one replacement per illegal character, keeping ``.``, ``_`` and
    ``-`` -- which is collision-free over every texture the install carries and is the same fold
    `static_stem` and `FElysiumContentPaths::PropModelStem` apply. `build_manifest` asserts the
    collision-freedom rather than assuming it, because a colliding pair would silently present one
    file as two assets.
    """
    from elysium_pipeline.formats import mdl

    return mdl.sanitize(texture_key(key)) + suffix + ".png"


def map_relative(name):
    """One corpus texture as a path relative to a MAP directory: ``../shared/tex/<file>``.

    A map's own sidecars name textures relative to themselves, and the runtime joins them onto the
    map directory. Stating the hop out to the corpus keeps that one join working for a texture the
    map no longer owns a copy of.
    """
    return f"../{ROOT}/{TEX}/{name}" if name else ""


def texture_rel(key, suffix=ALBEDO):
    """`texture_file` as a corpus-relative path, which is the form `materials.json` records and
    the bake joins against `corpus_dir`."""
    return f"{TEX}/{texture_file(key, suffix)}"


#: The six face suffixes a VtMB sky names, and the prefix its faces live under.
SKY_FACES = ("bk", "dn", "ft", "lf", "rt", "up")
SKY_DIR = "skybox"


def sky_texture_key(sky_name, face):
    """One sky face's install key. A sky face is an ordinary texture -- ``worldspawn``'s
    ``skyname`` plus a face suffix under ``materials/skybox/`` -- so it needs no rule of its own.

    The map export names these ``sky_<face>``, which is an alias for *this* map's sky: two maps
    sharing one sky then hold two files under one name. Keyed by the sky, they are one texture.
    """
    return texture_key(f"{SKY_DIR}/{_slashes(sky_name)}{face}")


def sky_face_prefix(sky_name):
    """The prefix `ElysiumEnvironment::BuildSkyCubeFrom` appends its face suffixes to, so the
    runtime addresses the corpus set with the same call it already makes."""
    return texture_file(sky_texture_key(sky_name, ""))[: -len(".png")]


def sky_face_file(sky_name, face):
    """One decoded sky face in the corpus: ``skybox_nightsky1bk.png``."""
    return texture_file(sky_texture_key(sky_name, face))


# ------------------------------------------------------------------- baked asset names

def texture_asset(file_name):
    """The ``T_`` asset a decoded file imports under. Takes the file name, not the key, because a
    key yields several products and each is its own asset."""
    from elysium_pipeline.asset_names import safe_name

    stem = file_name.replace("\\", "/").rsplit("/", 1)[-1]
    if "." in stem:
        stem = stem.rsplit(".", 1)[0]
    return "T_" + safe_name(stem)


def material_asset(key):
    """The ``MI_`` asset one material key instances under."""
    from elysium_pipeline.asset_names import safe_name

    return "MI_" + safe_name(key)


def mesh_asset(stem):
    """The ``SM_`` asset one model stem builds under."""
    from elysium_pipeline.asset_names import safe_name

    return "SM_" + safe_name(stem)


def baked_texture(file_name):
    name = texture_asset(file_name)
    return f"{BAKED_TEXTURES}/{name}"


def baked_material(key):
    name = material_asset(key)
    return f"{BAKED_MATERIALS}/{name}"


def baked_mesh(stem):
    name = mesh_asset(stem)
    return f"{BAKED_MESHES}/{name}"


# --------------------------------------------------------------------- the predicate

def is_map_scoped_material(key, *, decal=False, wetness_driven=False, local=False):
    """True when a material carries an input only its map can supply, so it cannot be shared.

    Three stamp a value into the instance rather than reading it at draw time:

    - a baked env cubemap (the `CUBEMAP_TAG` in the key), which is per map and per position;
    - a deferred decal's fog, because a ``UDecalComponent`` is a ``USceneComponent`` and carries
      no custom primitive data for the world's fog term to ride on;
    - a wetness-driven surface, which stamps its map's weather bounds and source cube.

    The fourth is scope rather than content: a material VBSP wrote into one map's own PAKFILE and
    nowhere else. The corpus reads the install, so it never sees that definition -- if the map did
    not author it, no package would hold it and the surface would bind nothing.

    Everything else -- every prop material, and every world material VBSP did not patch -- is a
    property of the install and belongs to the corpus.
    """
    return (bool(decal) or bool(wetness_driven) or bool(local)
            or CUBEMAP_TAG in str(key or ""))


# ------------------------------------------------------------------------- documents

def _digest(parts):
    out = hashlib.sha256()
    for part in parts:
        out.update(str(part).encode("utf-8"))
        out.update(b"\0")
    return out.hexdigest()


def corpus_fingerprint(texture_keys, material_keys, model_paths):
    """A stable digest of what the corpus is *made of*, independent of decode order.

    Sorted because the corpus is a set of source identities, not an ordered one: the map walk that
    discovers them visits maps in whatever order the install lists them, and that must not change
    the answer.
    """
    return _digest([
        REVISION,
        "textures", *sorted(texture_keys),
        "materials", *sorted(material_keys),
        "models", *sorted(model_paths),
    ])


def build_manifest(*, textures, materials, models, missing=None, fingerprint=""):
    """The body of ``shared/manifest.json``.

    ``textures`` is ``{key: {"files": {name: role}, "alpha": bool}}``. ``alpha`` is the corpus-wide
    union over every material that references the key, so whether a base texture keeps its alpha
    channel is a property of the install rather than of whichever material happened to resolve it
    first -- which is what made it a per-map, decode-order answer before. ``materials`` is
    ``{key: {"map_scoped": bool}}``, and ``models`` is
    ``{stem: {"model": install path, "materials": [key, ...]}}``.

    Raises ``ValueError`` when two texture keys fold to one file name, which would otherwise
    present one file as two assets with nothing logged.
    """
    seen = {}
    for key, record in sorted((textures or {}).items()):
        for name in record.get("files", {}):
            previous = seen.setdefault(name, key)
            if previous != key:
                raise ValueError(
                    f"texture file name collision: {previous!r} and {key!r} both fold to {name!r}"
                )
    return {
        "schema": MANIFEST_SCHEMA,
        "version": VERSION,
        "revision": REVISION,
        "corpus_fingerprint": fingerprint,
        "textures": dict(sorted((textures or {}).items())),
        "materials": dict(sorted((materials or {}).items())),
        "models": dict(sorted((models or {}).items())),
        "missing": missing or {},
    }


#: The fields of a `material_record` that state a decoded texture file. A reader that asks which
#: materials draw a given texture asks these, not `albedo` alone: one install texture reaches
#: different materials as a normal map, an env mask or a second blend layer.
CHANNEL_FIELDS = ("albedo", "emissive", "bump", "refract_map", "env_mask", "base_tex2",
                  "water_normal")


def material_record(channels, files=None, *, decal=False):
    """One `materials.json` row from `mdl.material_channels`.

    Carries the same fields `bake_lib.MatDef` holds, with every texture stated as a corpus-relative
    path, so a reader joins one document instead of re-parsing a `.mtl` per map. `files`, when
    given, is the set of file names the corpus actually decoded: a channel whose file is absent is
    stated empty rather than pointing at nothing. Left `None`, every named channel is stated --
    which is what a map-local material needs, since it never went through the corpus decode.
    """
    def rel(key, suffix=ALBEDO):
        if not key:
            return ""
        name = texture_file(key, suffix)
        return f"{TEX}/{name}" if files is None or name in files else ""

    albedo = channels.get("albedo")
    env_mask = ""
    if channels.get("envmap"):
        if channels.get("envmask"):
            env_mask = rel(channels["envmask"], ENV_MASK)
        elif channels.get("envmask_from_alpha"):
            env_mask = rel(albedo, ENV_MASK)
    # An authored $bumpmap always outranks the normal semantic glass derives from its own albedo.
    bump = (rel(channels["bump"], NORMAL) if channels.get("bump")
            else rel(albedo, GLASS_NORMAL) if channels.get("glass") else "")
    tint = channels.get("envtint") or [1.0, 1.0, 1.0]
    wetness = channels.get("globalwetness")
    return {
        "albedo": rel(albedo),
        "albedo_key": albedo or "",
        "emissive": rel(albedo, EMISSIVE) if channels.get("selfillum") else "",
        "bump": bump,
        "refract_map": rel(channels.get("refract_map"), REFRACT_NORMAL),
        "env_mask": env_mask,
        "base_tex2": rel(channels.get("base_tex2")),
        "scissor": bool(channels.get("alphatest")),
        "blend": bool(channels.get("translucent")),
        "additive": bool(channels.get("additive")),
        "glass": bool(channels.get("glass")),
        "refract": bool(channels.get("refract")),
        "refract_amount": float(channels.get("refract_amount") or 0.0),
        # The cube the VMT itself names: `env_cubemap` where the surface samples whatever VBSP
        # patched in, otherwise a named cubemap. `env_cube` is the id the `.mtl` and the bake use;
        # `env_cube_path` is the install key it decodes from, unfolded, because a sanitized id is
        # not a path.
        "env_cube": channels.get("envmap") or "",
        "env_cube_path": channels.get("envmap_path") or "",
        "env_tint": [float(c) for c in tint[:3]],
        "wetness": float(wetness) if wetness is not None else None,
        # The exporter's flag chain is an if/elif, so a water surface is written as `water` INSTEAD
        # of `blend` and never carries the translucent flag. Reading only `blend` therefore calls
        # every canal, sewer and pier surface opaque -- `bake_lib.MatDef` states the same trap.
        "water": bool(channels.get("water")),
        "water_normal": rel(channels.get("water_normal"), NORMAL),
        "water_fog_color": channels.get("water_fog_color") or [0.10, 0.10, 0.13],
        "water_fog_start": float(channels.get("water_fog_start") or 0.0),
        "water_fog_end": float(channels.get("water_fog_end") or 128.0),
        "water_reflect_tint": channels.get("water_reflect_tint") or [1.0, 1.0, 1.0],
        "decal_scale": float(channels.get("decal_scale") or 0.0),
        "unlit": bool(channels.get("unlit")),
        # An `infodecal` material projects rather than surfaces. That is a placement fact, not a
        # VMT one -- the same material can also skin a wall -- so it is recorded from the entity
        # walk rather than read off the shader.
        "decal": bool(decal),
    }


def build_materials(records, *, fingerprint=""):
    """The body of ``shared/materials.json``: ``{key: record}``, one definition per material.

    A record carries the same fields `bake_lib.MatDef` holds, with every texture stated as a
    corpus-relative path, so the bake reads one document instead of re-parsing a ``.mtl`` per map.
    """
    return {
        "schema": MATERIALS_SCHEMA,
        "version": VERSION,
        "revision": REVISION,
        "corpus_fingerprint": fingerprint,
        "materials": dict(sorted((records or {}).items())),
    }


def _check(document, schema, name):
    if not isinstance(document, dict):
        raise ValueError(f"{name} is not an object")
    if document.get("schema") != schema:
        raise ValueError(f"{name} is {document.get('schema')!r}, expected {schema!r}")
    if document.get("version") != VERSION:
        raise ValueError(
            f"{name} is version {document.get('version')}, expected {VERSION} - {REGENERATE}"
        )
    return document


def check_manifest(document):
    """Raise ``ValueError`` unless ``document`` is a manifest this build can read."""
    _check(document, MANIFEST_SCHEMA, MANIFEST)
    for key in ("textures", "materials", "models"):
        if not isinstance(document.get(key), dict):
            raise ValueError(f"{MANIFEST} is missing '{key}' - {REGENERATE}")
    return document


def check_materials(document):
    """Raise ``ValueError`` unless ``document`` is a material set this build can read."""
    _check(document, MATERIALS_SCHEMA, MATERIALS)
    if not isinstance(document.get("materials"), dict):
        raise ValueError(f"{MATERIALS} is missing 'materials' - {REGENERATE}")
    return document


# ------------------------------------------------------------------------- accessors

def texture_files(manifest):
    """Every decoded file the corpus holds, as ``{file name: role}`` -- the wanted-set a
    whole-corpus texture pass authors and prunes against."""
    out = {}
    for record in manifest.get("textures", {}).values():
        out.update(record.get("files", {}))
    return out


def material_definition(materials, key):
    """One material's definition out of a `materials.json` document, or ``None`` when the corpus
    does not name it. `material_record` is the other direction -- it builds one."""
    return materials.get("materials", {}).get(key)


def model_record(manifest, stem):
    """One model's census row, or ``None`` when the corpus does not name it."""
    return manifest.get("models", {}).get(stem)


def missing_models(models, stems):
    """The stems, sorted and deduplicated, that `models` does not name.

    `models` is a manifest's own model table. A map states the stem every prop it places folds
    to; this is what turns a stem the corpus never decoded into a named failure rather than a
    prop that silently places no mesh.
    """
    return sorted({stem for stem in stems if stem and stem not in models})


def shared_material_keys(manifest):
    """Every material key the corpus owns, in declared order."""
    return [key for key, row in manifest.get("materials", {}).items()
            if not row.get("map_scoped")]
