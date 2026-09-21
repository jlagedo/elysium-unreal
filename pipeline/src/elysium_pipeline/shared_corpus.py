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
half's key rules and its shared/per-map predicate.

It used to own two documents as well -- ``shared/manifest.json``, the census, and
``shared/materials.json``, the per-material definition. 0018 story 21-5 deleted the decoder that
wrote them and the corpus bake that read them; a texture, a material and a model are each a
published ``export_v2`` unit now, and the lanes that import them carry their own manifests. What
survives here is what those lanes still join on: how an install path folds to a key.
"""
from __future__ import annotations

import re

# --- the corpus on disk, under $ELYSIUM_EXPORT_ROOT --------------------------------------------
# What is left of it. 0018 story 21-5 deleted the decoder that wrote `shared/manifest.json` and
# `shared/materials.json` and the bake that read them, so this module no longer owns any
# document -- only the key rules, which the V2 producers and importers join on.
ROOT = "shared"
TEX = "tex"
#: The offline enhancement track's parallel set, same keys, same filenames.
TEX_HI = "tex_hi"
#: ``props`` rather than ``models``: it is the directory layout the bake's prop stage read.
PROPS = "props"

#: The one prop skin table. VtMB's alternate skin families are a property of a model, and
#: the material each family repaints is now one shared instance, so the table that joins them
#: is global rather than one copy per map.
PROP_SKINS_ASSET = "DA_ElysiumPropSkins"

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
