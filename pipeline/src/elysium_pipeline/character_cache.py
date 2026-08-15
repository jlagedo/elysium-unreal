"""Which character packages this run still has to author.

The unit is one **generated package family** -- a body's mesh, a body's own clips, one bank's
clips, one rig family's skeleton, one placed model -- and each carries its own recipe receipt in
the same per-asset store the map and shared-corpus bakes use
(`elysium_pipeline.bake_cache.AssetReceiptStore`), under the `characters` scope. A unit is stale
when its own recipe changed; nothing else in the cast is touched.

Every dependency between units is stated **inside a recipe** rather than as a cascade over
stages:

- a rig family's skeleton names its declared members' bone trees, so a body whose geometry or
  clips changed does not move it;
- a mesh and a body's own clips name the tree digest of the skeleton they bind to, so a family
  whose tree did move takes them with it;
- a model family's skeleton also names the bank skeletons it declares compatibility with, so a
  moved bank restates that declaration without disturbing one mesh or one sequence.

A scope (`_global`, `bank.<family>`, `model.<family>`, `prop.<stem>`) remains the editor's
selector for which stages run at all; the plan names the exact units inside them.
"""
from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path
import uuid

from elysium_pipeline import bake_cache, character_partition
from elysium_pipeline.formats import eskm
from elysium_pipeline.tasking import ContentDigestCache, fingerprint_content

#: Bump to invalidate every character receipt -- a change in what a unit MEANS, not in its inputs.
CACHE_REVISION = "elysium-character-unit-v1"

STAGES = ("textures", "bank_skeletons", "banks", "family_skeletons", "meshes", "clips", "props")

GLOBAL_SCOPE = "_global"

#: The receipt scope inside the shared per-asset store. One document for the whole cast, beside
#: the per-map and shared-corpus ones.
SCOPE = "characters"

PLAN_SCHEMA = "elysium.character-bake-plan"
PLAN_VERSION = 2

#: Bodies per editor-side checkpoint. A body is a skeletal mesh plus every clip it owns -- tens of
#: megabytes of source and hundreds of compressed sequences -- so the batch is two orders of
#: magnitude smaller than the corpus's static-mesh batch, and a killed run loses at most this many.
BODY_BATCH = 16

MOUNT = "/ElysiumBaked"
CHARACTERS = MOUNT + "/Characters"
MESHES = CHARACTERS + "/Meshes"
TEXTURES = CHARACTERS + "/Textures"
ANIMS = CHARACTERS + "/Anims"
BANKS = ANIMS + "/_banks"
PROPS = MOUNT + "/Props"

#: The stages whose unit authors exactly one package, so a receipt can name the file it wrote and
#: a report can be checked against it. Every other stage authors a package set.
SINGLE_ASSET_STAGES = ("bank_skeletons", "family_skeletons", "meshes")

#: One stage's authoring closure inside `pipeline/unreal/bake_characters.py`. `main` is shared
#: rather than a root: it calls every stage, so expanding it would make each stage reach the rest.
#: `existing_textures` is a root of `meshes` as well as reachable from the texture import, because
#: a mesh's material bindings resolve through the table it returns whenever the texture stage is
#: current and the import is skipped.
_STAGE_ROOTS = {
    "textures": ("import_texture_corpus",),
    "bank_skeletons": ("bake_banks",),
    "banks": ("bake_banks",),
    "family_skeletons": ("bake_bodies",),
    "meshes": ("bake_bodies", "existing_textures"),
    "clips": ("bake_bodies",),
    "props": ("bake_props",),
}
#: Hashed verbatim into every stage. `main` calls every stage, so expanding it would make each
#: stage reach the rest; the three readers beside it decide which bodies, which partition and which
#: units a run authors at all, so an edit to one has to move every stage rather than none.
_SHARED_FUNCTIONS = ("main", "cmdline_arg", "read_partition", "read_plan")


def texture_object_path() -> str:
    return TEXTURES


def mesh_object_path(stem: str) -> str:
    return f"{MESHES}/SK_{stem}"


def clips_object_path(family: str, owner: str) -> str:
    return f"{ANIMS}/{family}/{owner}"


def bank_clips_object_path(bank: str) -> str:
    return f"{BANKS}/{bank}"


def prop_object_path(stem: str) -> str:
    return f"{PROPS}/{stem}"


def _code_paths(config) -> tuple[Path, ...]:
    """Everything besides `bake_characters.py` that decides what a character asset comes out as.

    The C++ is hashed whole rather than per function, and by content rather than by mtime: a
    change to it already costs a build and a link, but a rebuilt file whose bytes did not move
    must not re-bake the cast.
    """
    repo = config.repo_root
    return (
        repo / "pipeline" / "unreal" / "bake_lib.py",
        repo / "pipeline" / "src" / "elysium_pipeline" / "asset_names.py",
        repo / "pipeline" / "src" / "elysium_pipeline" / "character_partition.py",
        repo / "Source" / "ElysiumUE" / "Public" / "ElysiumSkeletalBuild.h",
        repo / "Source" / "ElysiumUE" / "Private" / "Editor" / "ElysiumSkeletalBuild.cpp",
        repo / "Source" / "ElysiumUE" / "Private" / "Visual" / "ElysiumSkeletalSource.h",
        repo / "Source" / "ElysiumUE" / "Private" / "Visual" / "ElysiumSkeletalSource.cpp",
        repo / "Source" / "ElysiumUE" / "Private" / "ElysiumContentPaths.h",
    )


def stage_policy(config, stage: str, *, cache: ContentDigestCache | None = None) -> str:
    """The authoring-code identity of one character stage.

    Split by the call closure `bake_characters.py` reaches for that stage, exactly as a map or
    shared-corpus stage is split across its bake class: the character bake is one commandlet that
    authors skeletons, meshes, clips and placed models, so taking the whole entrypoint as the
    policy would make an edit to a function no stage of this run calls invalidate every receipt it
    holds.
    """
    if stage not in STAGES:
        raise ValueError(f"unknown character bake stage: {stage}")
    bake = config.repo_root / "pipeline" / "unreal" / "bake_characters.py"
    return fingerprint_content(
        _code_paths(config),
        extra=(
            CACHE_REVISION,
            "character-policy",
            stage,
            bake_cache.module_closure_fingerprint(
                bake, f"character-bake-{stage}", _STAGE_ROOTS[stage],
                shared_names=_SHARED_FUNCTIONS, cache=cache,
            ),
        ),
        cache=cache,
    )


def stage_policies(config, *, cache: ContentDigestCache | None = None) -> dict[str, str]:
    return {stage: stage_policy(config, stage, cache=cache) for stage in STAGES}


def _container(npc_dir: Path, stem: str, *, bank: bool = False) -> Path:
    return npc_dir / "banks" / f"{stem}.eskm" if bank else npc_dir / f"{stem}.eskm"


def _prop_container(npc_dir: Path, stem: str) -> Path:
    return npc_dir / "placed_models" / f"{stem}.eskm"


def _blend_sidecar(npc_dir: Path, manifest: dict, owner: str, *, bank: bool) -> Path | None:
    section = manifest["banks"] if bank else manifest["npcs"]
    relative = section.get(owner, {}).get("blends", "")
    return npc_dir / relative if relative else None


def _file_digest(path: Path | None, cache: ContentDigestCache | None) -> str:
    """The content digest of one optional input. An absent one digests as absent, not as an error:
    most clip owners declare no blend grid and ship no sidecar."""
    if path is None:
        return "absent"
    return fingerprint_content([path], extra=("character-input",), cache=cache)


def own_clip_owners(manifest: dict, members) -> list[str]:
    """The named bodies whose own containers hold clips the bake authors sequences from.

    A body that declares no own clip authors no sequence package, so it is not a unit: declaring
    one would leave a receipt the bake never writes and a scope that is stale forever. Every other
    label a body plays is owned by a bank, which is its own unit.
    """
    npcs = manifest.get("npcs", {})
    return sorted(stem for stem in members if npcs.get(stem, {}).get("own_clips"))


def reached_banks(manifest: dict, stems) -> list[str]:
    """Every bank the named bodies can play, plus every bank a cinematic scene names.

    The editor's `owner_clips` walk, stated offline. A cinematic bank is reached by a
    choreographed scene rather than by any body's clip map, so it joins unconditionally or its
    performance is absent from the mount.
    """
    banks = set()
    for stem in stems:
        for owner in manifest.get("npcs", {}).get(stem, {}).get("clips", {}).values():
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

    A prop is its own scope. It joins no rig family, so there is nothing to slice it by: one stage
    builds its skeleton, its mesh and its clips together.

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
        owners = {
            owner
            for stem in stems
            for owner in manifest.get("npcs", {}).get(stem, {}).get("clips", {}).values()
        }
        bank_families = {
            partition["bank_family_of"][owner]
            for owner in owners if owner in partition["bank_family_of"]
        }
    banks = [f"bank.{family}" for family in sorted(bank_families)]
    families = sorted({partition["model_family_of"][stem] for stem in stems
                       if stem in partition["model_family_of"]})
    global_scopes = [GLOBAL_SCOPE] if stems else []
    return [*global_scopes, *banks, *[f"model.{family}" for family in families],
            *[f"prop.{stem}" for stem in sorted(props)]]


def _family_tree_digest(npc_dir: Path, family: dict, *, bank: bool) -> str:
    """One rig family's skeleton identity: its declared members' bone trees and nothing else.

    `build_family_skeleton` reads only the `SKEL` section of each declared member, so this is what
    the skeleton package is a function of. Geometry, morph targets and clips live in the same
    containers and must not move it -- that is the difference between re-skinning one body and
    re-authoring every mesh and every sequence in its family.
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


@dataclass
class Unit:
    """One generated package family, and what its recipe is a function of."""

    stage: str
    scope: str
    name: str
    object_path: str
    recipe: dict
    fingerprint: str = ""
    output: str = ""


@dataclass
class CharacterPlan:
    policies: dict[str, str]
    scopes: dict[str, list[str]] = field(default_factory=dict)
    units: dict[str, dict[str, Unit]] = field(default_factory=dict)

    @property
    def stale(self) -> bool:
        return any(units for units in self.units.values())


def _unit(repo_root: Path, policies: dict[str, str], stage: str, scope: str,
          name: str, object_path: str, recipe: dict) -> Unit:
    fingerprint = bake_cache.asset_recipe_fingerprint(
        stage, object_path, policies[stage], recipe)
    output = ""
    if stage in SINGLE_ASSET_STAGES:
        output = str(bake_cache.unreal_output_path(repo_root, object_path))
    return Unit(stage=stage, scope=scope, name=name, object_path=object_path, recipe=recipe,
                fingerprint=fingerprint, output=output)


def _is_current(store, unit: Unit, policies: dict[str, str]) -> bool:
    if not store.has_stage(unit.stage, policies[unit.stage]):
        return False
    if not store.matches(unit.stage, unit.object_path, unit.fingerprint):
        return False
    if unit.output and not Path(unit.output).is_file():
        return False
    return True


def plan(config, npc_dir: Path, manifest: dict, partition: dict, stems, *,
         props=None, force: bool = False,
         cache: ContentDigestCache | None = None) -> CharacterPlan:
    """The exact units a bake of `stems` (and `props`) still has to author.

    Every unit's fingerprint is decided here, offline, against the receipt store, so the editor is
    launched only when at least one is stale and is then told precisely which ones to build.
    """
    stems = list(dict.fromkeys(stems))
    selected_props = sorted(
        manifest.get("placed_models", {}) if props is None else dict.fromkeys(props))
    owned_cache = cache is None
    if owned_cache:
        cache = ContentDigestCache(config.export_root / bake_cache.DIGEST_CACHE_FILE)
    try:
        policies = stage_policies(config, cache=cache)
        store = bake_cache.AssetReceiptStore(config.export_root, SCOPE)
        repo_root = config.repo_root
        scopes = scopes_for(partition, stems, selected_props, manifest=manifest)
        declared = {stage: {} for stage in STAGES}

        if GLOBAL_SCOPE in scopes:
            unit = _unit(
                repo_root, policies, "textures", GLOBAL_SCOPE, "textures",
                texture_object_path(),
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
                    repo_root, policies, "props", scope, name, prop_object_path(name),
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
                    repo_root, policies, "bank_skeletons", scope, name,
                    family["skeleton"], {"tree": tree},
                )
                declared["bank_skeletons"][unit.object_path] = unit
                for bank in family["members"]:
                    if bank not in needed_banks:
                        continue
                    unit = _unit(
                        repo_root, policies, "banks", scope, bank,
                        bank_clips_object_path(bank),
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
                repo_root, policies, "family_skeletons", scope, name,
                family["skeleton"], {"tree": tree, "compatible": compatible},
            )
            declared["family_skeletons"][unit.object_path] = unit

            members = [stem for stem in family["members"] if stem in stems]
            for stem in members:
                unit = _unit(
                    repo_root, policies, "meshes", scope, stem, mesh_object_path(stem),
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
                    repo_root, policies, "clips", scope, owner,
                    clips_object_path(name, owner),
                    {
                        "container": _file_digest(_container(npc_dir, owner), cache),
                        "blends": _file_digest(
                            _blend_sidecar(npc_dir, manifest, owner, bank=False), cache),
                        "skeleton": tree,
                    },
                )
                declared["clips"][unit.object_path] = unit

        units = {
            stage: {
                path: unit
                for path, unit in sorted(stage_units.items())
                if force or not _is_current(store, unit, policies)
            }
            for stage, stage_units in declared.items()
        }
        scope_stages: dict[str, list[str]] = {scope: [] for scope in scopes}
        for stage in STAGES:
            for unit in units[stage].values():
                if stage not in scope_stages.setdefault(unit.scope, []):
                    scope_stages[unit.scope].append(stage)
        for stages in scope_stages.values():
            stages.sort(key=STAGES.index)
        return CharacterPlan(policies=policies, scopes=scope_stages, units=units)
    finally:
        if owned_cache:
            cache.write()


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


def write_run_plan(npc_dir: Path, planned: CharacterPlan, *,
                   force: bool = False) -> tuple[Path, dict]:
    """Freeze one editor invocation: the scopes and stages it runs, and the units inside them."""

    document = {
        "schema": PLAN_SCHEMA,
        "version": PLAN_VERSION,
        "run_id": uuid.uuid4().hex,
        "force": bool(force),
        "batch": BODY_BATCH,
        "scopes": {scope: list(stages) for scope, stages in sorted(planned.scopes.items())},
        "policies": dict(sorted(planned.policies.items())),
        "units": {
            stage: {
                path: {
                    "unit": unit.name,
                    "scope": unit.scope,
                    "fingerprint": unit.fingerprint,
                    "output": unit.output,
                }
                for path, unit in sorted(stage_units.items())
            }
            for stage, stage_units in sorted(planned.units.items())
            if stage_units
        },
    }
    path = npc_dir / ".elysium-character-plan.json"
    bake_cache.atomic_json(path, document)
    return path, document


def report_path(config, run_id: str) -> Path:
    return bake_cache.asset_run_report_path(config.export_root, run_id, SCOPE)


def load_run_report(config, document: dict) -> dict:
    """The bake's own report, checked against the plan it was given.

    A character unit is a package family rather than one file -- a clip owner authors every
    sequence it declares -- so only the single-asset stages can be checked against an output file.

    A stage's inventory is the units this run authored plus the receipts it carried in unchanged,
    because promoting a stage replaces it wholesale; a carried receipt therefore states an older
    fingerprint by design and is not an error. What is refused is a disagreement between the two
    halves: a stage the plan did not name, a policy that is not the one the plan froze, or a
    receipt naming a package that is not on disk.
    """
    path = report_path(config, str(document["run_id"]))
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise RuntimeError(f"character bake report missing or invalid: {error}") from error
    if (
        report.get("schema") != bake_cache.ASSET_RUN_SCHEMA
        or report.get("version") != bake_cache.ASSET_SCHEMA_VERSION
        or report.get("run_id") != str(document["run_id"])
        or report.get("map") != SCOPE
    ):
        raise RuntimeError("character bake report identity mismatch")
    planned = document.get("units", {})
    stages = report.get("stages")
    if not isinstance(stages, dict):
        raise RuntimeError("character bake report has no stages")
    for stage, stage_report in stages.items():
        if stage not in planned:
            raise RuntimeError(f"character bake report names an unplanned stage: {stage}")
        if stage_report.get("policy") != document["policies"].get(stage):
            raise RuntimeError(f"character bake policy mismatch for {stage}")
        assets = stage_report.get("assets")
        if not isinstance(assets, dict):
            raise RuntimeError(f"character bake report has no inventory for {stage}")
        for object_path, receipt in assets.items():
            if receipt.get("object_path") != object_path:
                raise RuntimeError(f"character receipt identity mismatch: {object_path}")
            if len(receipt.get("fingerprint", "")) != 64:
                raise RuntimeError(f"character receipt has invalid fingerprint: {object_path}")
            output = receipt.get("output", "")
            if output and not Path(output).is_file():
                raise RuntimeError(f"character receipt output is missing: {output}")
    return report


def promote_run(config, report: dict) -> None:
    """Write the validated report's receipts over the store, one stage at a time."""
    store = bake_cache.AssetReceiptStore(config.export_root, SCOPE)
    store.replace_stages({
        stage: {"policy": stage_report["policy"], "assets": stage_report["assets"]}
        for stage, stage_report in report.get("stages", {}).items()
    })


def salvage(config, document: dict) -> int:
    """Promote the receipts of a character bake that died mid-run, and say how many landed.

    The commandlet checkpoints every batch: each package the C++ builders returned success for is
    already saved to disk, and the interim report names only those. A report left by a run that
    died later is therefore promotable as it stands, and the next run resumes instead of
    rebuilding what already landed.
    """
    try:
        report = load_run_report(config, document)
    except Exception as error:
        print(f"[bake] character bake failed with no promotable interim report: {error}")
        return 0
    promote_run(config, report)
    salvaged = sum(
        len(stage_report.get("assets", {}))
        for stage_report in report.get("stages", {}).values()
    )
    print(f"[bake] character bake failed; salvaged the receipts of {salvaged} unit(s)")
    return salvaged


def revoke(config, document: dict) -> int:
    """Drop the receipts of every unit this run planned, and say how many went.

    The verifier is the only reader that can tell what is ON THE MOUNT from what the bake left
    resident, so a run that did not reach a passing verify leaves units that are not known good --
    whatever the bake's own checkpoints recorded while it was running. Only the planned units go:
    a receipt this run carried in unchanged was never at stake.
    """
    store = bake_cache.AssetReceiptStore(config.export_root, SCOPE)
    replacements = {}
    removed = 0
    for stage, planned in document.get("units", {}).items():
        assets = dict(store.stage_assets(stage))
        policy = store.data.get("stages", {}).get(stage, {}).get("policy", "")
        for object_path in planned:
            if assets.pop(object_path, None) is not None:
                removed += 1
        replacements[stage] = {"policy": policy, "assets": assets}
    if replacements:
        store.replace_stages(replacements)
    return removed
