"""What each generated character package set is a function of.

The unit is one **generated package set** -- a body's mesh, a body's own clips, one bank's
clips, one skeleton, one placed model -- and its recipe names exactly the inputs it is authored
from. The editor computes each recipe's fingerprint (`bake_lib.recipe_fingerprint`), compares it
against the hash stamped on the assets themselves, and authors only the mismatches; no plan,
receipt or run state exists outside the assets on the mount.

Every dependency between units is stated **inside a recipe** rather than as a cascade over
stages:

- a body's skeleton names its own container's bone tree, so nothing another body does moves it;
- a mesh and a body's own clips name the tree digest of the skeleton they bind to, so a tree
  that did move takes them with it and nothing else;
- a body's skeleton also names the bank skeletons it declares compatibility with, so a moved
  bank restates that declaration on every body without disturbing one mesh or one sequence.

A scope (`_global`, `bank.<family>`, `model.<stem>`, `prop.<stem>`) is the unit's grouping for
reporting and selection; the declared units are the whole decision surface.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path

from elysium_pipeline.formats import eskm
from elysium_pipeline.tasking import ContentDigestCache, fingerprint_content

#: The scope label character work reports under.
SCOPE = "characters"

#: Report-order for the unit stages. `SINGLE_ASSET_STAGES` units are one asset at one object
#: path; every other stage's unit is a package family below the unit's root path.
STAGES = ("textures", "bank_skeletons", "banks", "family_skeletons", "meshes", "clips", "props")
SINGLE_ASSET_STAGES = ("bank_skeletons", "family_skeletons", "meshes")

#: Per-stage recipe version literals. Bumping one re-authors that stage's units; an editor-side
#: builder change that alters output without changing inputs is expressed here.
STAGE_VERSIONS = {
    "textures": "chars-textures-v1",
    "bank_skeletons": "chars-bank-skeletons-v1",
    "banks": "chars-banks-v2",
    "family_skeletons": "chars-family-skeletons-v1",
    "meshes": "chars-meshes-v1",
    "clips": "chars-clips-v2",
    "props": "chars-props-v1",
}

GLOBAL_SCOPE = "_global"

#: Bodies are heavy -- a skeletal mesh plus every sequence it owns -- so the editor releases
#: packages after this many authored units.
BODY_BATCH = 16

MOUNT = "/ElysiumBaked"
CHARACTERS = MOUNT + "/Characters"
MESHES = CHARACTERS + "/Meshes"
TEXTURES = CHARACTERS + "/Textures"
ANIMS = CHARACTERS + "/Anims"
BANKS = ANIMS + "/_banks"
PROPS = MOUNT + "/Props"


def texture_object_path() -> str:
    return TEXTURES


def mesh_object_path(stem: str) -> str:
    return f"{MESHES}/SK_{stem}"


def clips_object_path(stem: str) -> str:
    return f"{ANIMS}/{stem}"


def bank_clips_object_path(bank: str) -> str:
    return f"{BANKS}/{bank}"


def prop_object_path(stem: str) -> str:
    return f"{PROPS}/{stem}"


def _container(npc_dir: Path, stem: str, *, bank: bool = False) -> Path:
    return npc_dir / "banks" / f"{stem}.eskm" if bank else npc_dir / f"{stem}.eskm"


def _prop_container(npc_dir: Path, stem: str) -> Path:
    return npc_dir / "placed_models" / f"{stem}.eskm"


def _blend_sidecar(npc_dir: Path, manifest: dict, owner: str, *, bank: bool) -> Path | None:
    section = manifest["banks"] if bank else manifest["npcs"]
    relative = section.get(owner, {}).get("blends", "")
    return npc_dir / relative if relative else None


def _blend_sidecar_prop(npc_dir: Path, manifest: dict, stem: str) -> Path | None:
    relative = manifest.get("placed_models", {}).get(stem, {}).get("blends", "")
    return npc_dir / relative if relative else None


def _eye_sidecar(npc_dir: Path, manifest: dict, stem: str) -> Path | None:
    relative = manifest.get("npcs", {}).get(stem, {}).get("eyes", "")
    return npc_dir / relative if relative else None


_TEXTURE_BINDINGS: dict[Path, tuple[tuple[int, int], dict]] = {}


def _texture_bindings(npc_dir: Path, stem: str) -> dict:
    """One body's own rows out of `npc/textures.json`.

    The document covers the whole cast, so taking it whole would make one re-decoded albedo
    re-author all 166 meshes. Memoized on the file's stat identity because every body asks.
    """
    path = npc_dir / "textures.json"
    try:
        stat = path.stat()
    except OSError:
        return {"missing": True}
    identity = (stat.st_size, stat.st_mtime_ns)
    cached = _TEXTURE_BINDINGS.get(path)
    if cached is None or cached[0] != identity:
        try:
            document = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, ValueError):
            document = {}
        _TEXTURE_BINDINGS[path] = (identity, document)
        cached = _TEXTURE_BINDINGS[path]
    document = cached[1]
    bindings = document.get("bindings", {}).get(stem, {})
    textures = document.get("textures", {})
    return {
        "bindings": bindings,
        "textures": {name: textures.get(name, {}).get("uri", "")
                     for name in sorted(set(bindings.values()))},
    }


def _file_digest(path: Path | None, cache: ContentDigestCache | None) -> str:
    """The content digest of one optional input. An absent one digests as absent, not as an error:
    most clip owners declare no blend grid and ship no sidecar."""
    if path is None:
        return "absent"
    return fingerprint_content([path], extra=("character-input",), cache=cache)


def own_clip_owners(manifest: dict, members) -> list[str]:
    """The named bodies whose own containers hold clips the bake authors sequences from.

    A body that declares no own clip authors no sequence package, so it is not a unit: declaring
    one would leave a unit the bake never stamps and a scope that is stale forever. Every other
    label a body plays is owned by a bank, which is its own unit.
    """
    npcs = manifest.get("npcs", {})
    return sorted(stem for stem in members if npcs.get(stem, {}).get("own_clips"))


def clip_owners(manifest: dict, stem: str):
    """Every bank that declares a clip the named body plays.

    **A clip label maps to a LIST of owners, not to one.** A label names every bank that declares
    it, in include-tree order, so the owner set is the flattened values rather than the values
    themselves -- the same shape `export_manager.character_source_plan` reads. Consuming a value
    as a scalar puts a list into a set, which raised `TypeError: unhashable type: 'list'` and took
    down every bake scoped to named bodies before it had written anything. A lone string is still
    accepted, because a single-owner label is written either way.
    """
    for declared in manifest.get("npcs", {}).get(stem, {}).get("clips", {}).values():
        yield from (declared if isinstance(declared, list) else [declared])


def reached_banks(manifest: dict, stems) -> list[str]:
    """Every bank the named bodies can play, plus every bank a cinematic scene names.

    The editor's `owner_clips` walk, stated offline. A cinematic bank is reached by a
    choreographed scene rather than by any body's clip map, so it joins unconditionally or its
    performance is absent from the mount.
    """
    banks = set()
    for stem in stems:
        for owner in clip_owners(manifest, stem):
            if owner != stem and owner in manifest.get("banks", {}):
                banks.add(owner)
    for record in manifest.get("cinematics", {}).values():
        for root in record.get("roots", []):
            bank = root.get("bank")
            if bank and bank in manifest.get("banks", {}):
                banks.add(bank)
    return sorted(banks)


def scopes_for(partition: dict, stems, props=(), *, manifest: dict | None = None) -> list[str]:
    """Every scope a bake of `stems` touches, banks first -- they gate the compatibility call.

    A body and a prop are each their own scope: one stage set builds a skeleton, a mesh and
    clips for exactly one container.

    A complete-cast bake covers every declared bank family, including cinematic-only banks. A
    focused bake covers only bank families its named bodies reach; existing skeleton packages for
    all other families remain available for the editor compatibility declaration and untouched.
    """
    stems = tuple(stems)
    selected = set(stems)
    all_models = set(partition["model_family_of"])
    if not stems:
        bank_families = set()
    elif manifest is None or selected == all_models:
        bank_families = set(partition["banks"])
    else:
        owners = {owner for stem in stems for owner in clip_owners(manifest, stem)}
        bank_families = {
            partition["bank_family_of"][owner]
            for owner in owners if owner in partition["bank_family_of"]
        }
    banks = [f"bank.{family}" for family in sorted(bank_families)]
    bodies = sorted(stem for stem in set(stems) if stem in partition["model_family_of"])
    global_scopes = [GLOBAL_SCOPE] if stems else []
    return [*global_scopes, *banks, *[f"model.{stem}" for stem in bodies],
            *[f"prop.{stem}" for stem in sorted(props)]]


def _family_tree_digest(npc_dir: Path, family: dict, *, bank: bool) -> str:
    """One skeleton's identity: its declared members' bone trees and nothing else.

    A body's entry declares one member -- itself -- and a bank family declares several.
    `build_family_skeleton` reads only the `SKEL` section of each declared member, so this is
    what the skeleton package is a function of. Geometry, morph targets and clips live in the
    same containers and must not move it -- that is the difference between re-skinning one body
    and re-authoring its mesh and every sequence bound to its skeleton.
    """
    digest = hashlib.sha256()
    digest.update(str(family.get("bones", 0)).encode("ascii"))
    digest.update(b"\0")
    digest.update(str(family.get("tree_fingerprint", "")).encode("ascii"))
    digest.update(b"\0")
    for stem in family.get("members", []):
        digest.update(stem.encode("utf-8", errors="surrogateescape"))
        digest.update(b"\0")
        digest.update(
            eskm.section_digest(_container(npc_dir, stem, bank=bank), (b"SKEL",)).encode("ascii")
        )
        digest.update(b"\0")
    return digest.hexdigest()


@dataclass(frozen=True)
class Unit:
    """One generated package set, and what its recipe is a function of."""

    stage: str
    scope: str
    name: str
    object_path: str
    recipe: dict


def _unit(stage: str, scope: str, name: str, object_path: str, recipe: dict) -> Unit:
    return Unit(stage=stage, scope=scope, name=name, object_path=object_path,
                recipe={**recipe, "version": STAGE_VERSIONS[stage]})


def declared_units(npc_dir: Path, manifest: dict, partition: dict, stems, props=None, *,
                   cache: ContentDigestCache | None = None) -> dict[str, dict[str, Unit]]:
    """Every unit a bake of `stems` (and `props`) covers, with its recipe.

    Whether a unit is authored is not decided here: the editor compares each recipe's
    fingerprint against the stamp on the mount and builds the mismatches.
    """
    stems = list(dict.fromkeys(stems))
    selected_props = sorted(
        manifest.get("placed_models", {}) if props is None else dict.fromkeys(props))
    scopes = scopes_for(partition, stems, selected_props, manifest=manifest)
    declared: dict[str, dict[str, Unit]] = {stage: {} for stage in STAGES}

    if GLOBAL_SCOPE in scopes:
        unit = _unit(
            "textures", GLOBAL_SCOPE, "textures", texture_object_path(),
            {"textures": _file_digest(npc_dir / "textures.json", cache)},
        )
        declared["textures"][unit.object_path] = unit

    bank_trees: dict[str, str] = {}
    for name, family in sorted(partition["banks"].items()):
        bank_trees[name] = _family_tree_digest(npc_dir, family, bank=True)
    # Every model family declares compatibility with every bank family, so the declaration is
    # a function of all of them whichever ones this run reaches.
    compatible = {
        partition["banks"][name]["skeleton"]: bank_trees[name]
        for name in sorted(partition["banks"])
    }

    needed_banks = set(reached_banks(manifest, stems)) if stems else set()
    for scope in scopes:
        kind, _, name = scope.partition(".")
        if scope == GLOBAL_SCOPE:
            continue
        if kind == "prop":
            blends = _blend_sidecar_prop(npc_dir, manifest, name)
            unit = _unit(
                "props", scope, name, prop_object_path(name),
                {
                    "container": _file_digest(_prop_container(npc_dir, name), cache),
                    "blends": _file_digest(blends, cache),
                },
            )
            declared["props"][unit.object_path] = unit
            continue
        if kind == "bank":
            family = partition["banks"][name]
            tree = bank_trees[name]
            unit = _unit(
                "bank_skeletons", scope, name, family["skeleton"], {"tree": tree},
            )
            declared["bank_skeletons"][unit.object_path] = unit
            for bank in family["members"]:
                if bank not in needed_banks:
                    continue
                unit = _unit(
                    "banks", scope, bank, bank_clips_object_path(bank),
                    {
                        "container": _file_digest(
                            _container(npc_dir, bank, bank=True), cache),
                        "blends": _file_digest(
                            _blend_sidecar(npc_dir, manifest, bank, bank=True), cache),
                        "skeleton": tree,
                    },
                )
                declared["banks"][unit.object_path] = unit
            continue

        family = partition["models"][name]
        tree = _family_tree_digest(npc_dir, family, bank=False)
        unit = _unit(
            "family_skeletons", scope, name, family["skeleton"],
            {"tree": tree, "compatible": compatible},
        )
        declared["family_skeletons"][unit.object_path] = unit

        members = [name] if name in stems else []
        for stem in members:
            unit = _unit(
                "meshes", scope, stem, mesh_object_path(stem),
                {
                    "container": _file_digest(_container(npc_dir, stem), cache),
                    "bindings": _texture_bindings(npc_dir, stem),
                    "eyes": _file_digest(_eye_sidecar(npc_dir, manifest, stem), cache),
                    "skeleton": tree,
                },
            )
            declared["meshes"][unit.object_path] = unit
        for owner in own_clip_owners(manifest, members):
            unit = _unit(
                "clips", scope, owner, clips_object_path(name),
                {
                    "container": _file_digest(_container(npc_dir, owner), cache),
                    "blends": _file_digest(
                        _blend_sidecar(npc_dir, manifest, owner, bank=False), cache),
                    "skeleton": tree,
                },
            )
            declared["clips"][unit.object_path] = unit

    return declared
