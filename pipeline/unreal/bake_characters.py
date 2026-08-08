# Bakes VtMB characters into real Unreal assets under the /ElysiumBaked mount (ANM1,
# docs/project/animation-roadmap.md).
#
# Where the runtime builds a USkeletalMesh, a throwaway USkeleton and every UAnimSequence at map
# load, this pass runs once in a headless editor and writes them out as assets: one shared USkeleton
# per rig family, a mesh per model, and a compressed UAnimSequence per clip.
#
# Everything it reads is an `.eskm` container written by the offline exporter, and every asset it
# writes is built through the engine's own authoring path in C++ (UElysiumSkeletalBuildLibrary).
# This worker is orchestration only: it decides which models share a skeleton, which textures to
# import, and what each asset is called.
#
# The output is derived from the user's own VtMB install, so it is gitignored and regenerable
# exactly like $ELYSIUM_EXPORT_ROOT -- only the .uplugin mount descriptor is committed.
#
# Internal editor worker coordinated by `uv run elysium export characters`:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript
#       -script="pipeline/unreal/bake_characters.py"
#       -BakeCharacters=smiling_jack,tremere_male_armor_0 -unattended -nosplash -nopause
import json
import os

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402
from elysium_pipeline import asset_names, character_partition  # noqa: E402
from elysium_pipeline.formats import eskm  # noqa: E402
from elysium_pipeline.paths import export_root  # noqa: E402

MOUNT = "/ElysiumBaked"
CHARACTERS = MOUNT + "/Characters"
#: Every skeleton package path comes off `npc/families.json`; these two exist so the folder can be
#: created and so the verifier can read a family back off a skeleton's name. The partition module
#: is the one authority on the spelling.
SKELETONS = character_partition.SKELETON_DIR
SKELETON_PREFIX = character_partition.MODEL_SKELETON_PREFIX
MESHES = CHARACTERS + "/Meshes"
MATERIALS = CHARACTERS + "/Materials"
TEXTURES = CHARACTERS + "/Textures"
ANIMS = CHARACTERS + "/Anims"
#: Bank clips live here ONCE, addressed by owner alone. A bank is recorded on one rig and played by
#: every body that includes it, so baking it per rig family turned 7,871 distinct clips across the
#: cast into ~90,000 assets. The families share these through `AddCompatibleSkeleton` instead.
BANKS = ANIMS + "/_banks"
BANK_SKELETON_PREFIX = character_partition.BANK_SKELETON_PREFIX

#: Every body section is instanced from this one master. Its parameter names are glTF's, which is
#: what lets one instance serve a body drawn as an NPC and the same body worn by the player -- the
#: two differ only in the ModelAlpha the runtime drives, not in the asset.
BODY_MASTER = "/Game/VtMB/Materials/M_PlayerBody.M_PlayerBody"

#: The eyeball sections take this instead. `UElysiumEntityBodies::InstallEyes` finds an eye by
#: asking whether a slot's base material IS this master -- two independent tests, it says, "because
#: either alone can be defeated" -- so a body whose every section is parented to BODY_MASTER
#: defeats both at once and no eye is ever installed.
EYE_MASTER = "/Game/VtMB/Materials/M_Eyes.M_Eyes"

NPC_DIR = os.path.join(os.fspath(export_root()), "npc")


def log(msg):
    unreal.log("[chars] %s" % msg)


def fail(msg):
    unreal.log_error("[chars] %s" % msg)


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line."""
    needle = "-%s=" % key
    for token in unreal.SystemLibrary.get_command_line().split():
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def source_path(stem, bank=False):
    return os.path.join(NPC_DIR, "banks" if bank else "", stem + ".eskm")


def read_partition():
    """The declared rig partition, written by the offline half over the WHOLE corpus.

    Read rather than recomputed. `eskm.rig_families` is greedy over the set it is given, so
    partitioning whatever a run happens to name renames families -- and can merge two the cast
    keeps apart -- while the family name is the path contract for every clip and skeleton the
    mount carries."""
    path = os.path.join(NPC_DIR, "families.json")
    if not os.path.isfile(path):
        raise SystemExit(
            "[chars] %s is missing (run: uv run elysium export characters)" % path)
    with open(path, "r", encoding="utf-8-sig") as handle:
        return character_partition.check(json.load(handle))


def read_plan():
    """{scope: set(stages)} the orchestrator still wants authored, or None for "everything".

    A scope absent from the plan is current on the mount and must be left untouched -- not
    rebuilt, because rebuilding a family skeleton renames its blend-mask profiles out from under
    sequences that are not being rebuilt with it. No plan means a hand-run bake, which does the
    lot; that is the recovery surface and it is deliberately not receipted.
    """
    path = cmdline_arg("BakeCharacterPlan")
    if not path:
        return None
    with open(path, "r", encoding="utf-8-sig") as handle:
        document = json.load(handle)
    if document.get("schema") != "elysium.character-bake-plan":
        raise SystemExit("[chars] %s is not a character bake plan" % path)
    return {scope: set(stages) for scope, stages in document.get("scopes", {}).items()}


def wants(plan, scope, stage):
    """Whether this run authors `stage` for `scope`."""
    return plan is None or stage in plan.get(scope, ())


def release_packages():
    """Drop the packages this pass saved, and say how many went.

    Not `unreal.SystemLibrary.collect_garbage()`, which frees nothing here: it only raises flags
    that the engine tick consumes, and a `-run=pythonscript` commandlet never ticks."""
    released = unreal.ElysiumSkeletalBuildLibrary.release_baked_packages(CHARACTERS)
    if released:
        log("released %d package(s)" % released)


#: Shared with the offline half, which names the same asset in `npc/textures.json` without an
#: editor to ask. The two must agree or the bake imports under a name nothing looks up.
texture_asset_name = asset_names.texture_asset_name


def import_textures_for(paths):
    """Import every albedo the named models reference, once. The npc/tex/ tree is shared across the
    cast -- 514 files for 166 models -- so the textures are one package for all of them rather than
    a per-model copy, which is also what keeps a re-bake of one body from re-importing them all.

    Takes PATHS and reads one container at a time: the cast's containers are 400 MB together, and
    this pass needs nothing from one after its material table has been read."""
    wanted = {}
    for path in paths:
        blob = eskm.read(path)
        for uri in eskm.materials(blob).values():
            if not uri:
                continue
            source = os.path.join(NPC_DIR, uri.replace("/", os.sep))
            if os.path.isfile(source):
                wanted[texture_asset_name(uri)] = source
            else:
                fail("texture source missing: %s" % source)
    if not wanted:
        return {}
    imported = bl.import_textures([(src, name) for name, src in sorted(wanted.items())], TEXTURES)
    saved = 0
    for name, texture in sorted(imported.items()):
        bl.configure_texture(texture, "albedo")
        # import_textures leaves the package unsaved by design -- the map bake saves through its own
        # asset tracker. Nothing tracks these, and an unsaved texture package is the worst kind of
        # failure here: the material instance still serialises its reference, so the bake reports
        # success and the body draws untextured on the next run.
        if bl.save("%s/%s" % (TEXTURES, name)):
            saved += 1
        else:
            fail("could not save texture %s" % name)
    log("textures: %d imported, %d saved" % (len(imported), saved))
    return imported


def eye_slot_masters(manifest, stem, blob):
    """{material slot: EYE_MASTER} for the sections this model's eye sidecar claims.

    Resolved against the container's OWN material names rather than used verbatim: the sidecar
    writes `eyeball_r` and the container's slot is `Eyeball_r`. Unreal would call those one FName,
    but the map the mesh builder looks the slot up in is an ordinary case-sensitive one, so the
    match has to be made here where both spellings are in hand.
    """
    relative = manifest.get("npcs", {}).get(stem, {}).get("eyes", "")
    if not relative:
        return {}
    path = os.path.join(NPC_DIR, relative.replace("/", os.sep))
    if not os.path.isfile(path):
        fail("eye sidecar missing: %s" % path)
        return {}
    with open(path, "r", encoding="utf-8-sig") as handle:
        rig = json.load(handle)
    wanted = {entry["material"].lower() for entry in rig.get("meshes", [])
              if entry.get("material")}
    if not wanted:
        return {}
    slots = {name: EYE_MASTER for name in eskm.materials(blob) if name.lower() in wanted}
    unmatched = wanted - {name.lower() for name in slots}
    if unmatched:
        fail("%s: eye sidecar names %s, which the container has no section for"
             % (stem, ", ".join(sorted(unmatched))))
    return slots


def existing_textures():
    """{asset name: Texture2D} already on the mount, for a run whose texture stage is current.

    The bindings a mesh is built with resolve through this, so skipping the import must not mean
    skipping the lookup -- a mesh built against an empty table binds no albedo and draws white.
    """
    out = {}
    for path in unreal.EditorAssetLibrary.list_assets(TEXTURES, recursive=False,
                                                      include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if asset is not None:
            out[asset.get_name()] = asset
    log("textures: %d already current" % len(out))
    return out


def material_bindings(blob, textures):
    """{material slot name: Texture2D asset path} for one model."""
    out = {}
    for material, uri in eskm.materials(blob).items():
        if not uri:
            continue
        asset = textures.get(texture_asset_name(uri))
        if asset is not None:
            out[material] = asset.get_path_name()
    return out


def owner_clips(manifest, stems):
    """{owner stem: is a bank} over everything the named models can play.

    A body owns its own dialogue clips; every other label is owned by a bank it reaches through the
    include DAG. A whole owner is baked at once because that is one container read, and the flags
    that decide which clips are additive travel inside the container rather than beside it."""
    owners = {}
    for stem in stems:
        record = manifest["npcs"].get(stem)
        if record is None:
            fail("%s is not in the manifest" % stem)
            continue
        if record.get("own_clips"):
            owners[stem] = False
        for owner in record.get("clips", {}).values():
            if owner == stem or owner in owners:
                continue
            if owner not in manifest["banks"]:
                fail("%s names bank '%s', which the manifest does not carry" % (stem, owner))
                continue
            owners[owner] = True
    return owners


def blend_source(manifest, owner, is_bank):
    """One clip owner's blend sidecar path, relative to `npc/`, or "" when it declares no grid.

    The value is the manifest's own `blends` field rather than a path this file builds, because the
    same field addresses both "blends/<stem>.json" and the animated props' nested variant, and the
    C++ reader resolves it the one way the runtime does."""
    section = manifest["banks"] if is_bank else manifest["npcs"]
    return section.get(owner, {}).get("blends", "")


def bake_banks(manifest, partition, stems, library, failed, plan):
    """Bake every bank the named bodies reach ONCE, and return the skeletons they play them from.

    A `UAnimSequence` is bound to exactly one `USkeleton`, so a bank recorded once was rebuilt for
    every rig family that included it -- and the families are numerous for small reasons, mostly the
    generic `BoneNN` hair chains VtMB reuses across bodies for different chains. Two banks alone
    carry 4,551 of the cast's 7,871 distinct clips, so the per-family rebuild was 11x the work and
    11x the mount for no additional animation.

    The banks get their own skeletons, partitioned exactly the way the bodies are, and the bodies
    declare compatibility with them. That is non-destructive: the engine maps bones by NAME per
    skeleton pair and drops what a body's rig lacks -- the same binding rule the per-family bake
    applied when it left a Gangrel ponytail track unbound. The reference poses agree to a median of
    0.03 degrees across the shared bones, because VtMB's banks were recorded on character rigs, so
    the remapping is a pure index map with no pose correction in it.

    **Every declared bank family's skeleton is built, whatever this run bakes.** A body declares
    compatibility with all of them, and a skeleton missing because no named body happened to reach
    its family is a body that cannot play a bank it does not yet need. Only the SEQUENCES are
    restricted to what the named bodies reach.
    """
    needed = set()
    for bank in sorted(owner for owner, is_bank in owner_clips(manifest, stems).items() if is_bank):
        if os.path.isfile(source_path(bank, bank=True)):
            needed.add(bank)
        else:
            fail("no .eskm for clip owner %s" % bank)
            failed.append(bank)

    families = partition["banks"]
    log("%d bank(s) in %d declared rig famil%s, %d to bake"
        % (len(partition["bank_family_of"]), len(families),
           "y" if len(families) == 1 else "ies", len(needed)))

    skeletons = []
    for name in sorted(families):
        family = families[name]
        skeleton_package = family["skeleton"]
        scope = "bank.%s" % name
        # The package path is returned whether or not this run authors it: a body still declares
        # compatibility with every bank family, and a skeleton left alone is still on the mount.
        skeletons.append(skeleton_package)
        if not (wants(plan, scope, "bank_skeletons") or wants(plan, scope, "banks")):
            continue
        members = [b for b in family["members"] if os.path.isfile(source_path(b, bank=True))]
        if len(members) != len(family["members"]):
            fail("bank family %s: %d of %d member containers are missing"
                 % (name, len(family["members"]) - len(members), len(family["members"])))
            failed.append(name)
            continue
        if wants(plan, scope, "bank_skeletons"):
            # Rebuilt from every declared member, in declared order, rather than merged into
            # whatever a previous run left behind: a skeleton that only grows keeps the bones of
            # banks a later partition moved elsewhere, and an untracked bone falls back to exactly
            # that tree.
            error, bones = library.build_family_skeleton(
                [source_path(b, bank=True) for b in members], skeleton_package, True)
            if error:
                fail("bank skeleton %s: %s" % (name, error))
                failed.append(name)
                continue
            if bones != family["bones"]:
                fail("bank skeleton %s: built %d bones, the partition declares %d"
                     % (name, bones, family["bones"]))
                failed.append(name)
        else:
            bones = family["bones"]

        if not wants(plan, scope, "banks"):
            continue

        total = 0
        spaces_total = 0
        dropped_total = 0
        grids_skipped = 0
        for bank in [b for b in family["members"] if b in needed]:
            package = "%s/%s" % (BANKS, bank)
            error, count, dropped = library.build_anim_sequences_from_source(
                source_path(bank, bank=True), package, skeleton_package)
            if error:
                fail("%s: %s" % (bank, error))
                failed.append(bank)
            total += count
            dropped_total += dropped

            blends = blend_source(manifest, bank, True)
            if not blends:
                continue
            error, spaces, skipped_grids, skipped_cells = library.build_blend_spaces_from_grids(
                blends, package, skeleton_package)
            if error:
                fail("%s blends: %s" % (bank, error))
                failed.append(bank)
            spaces_total += spaces
            grids_skipped += skipped_grids
            if skipped_cells:
                log("%s: %d grid cell(s) had no baked clip" % (bank, skipped_cells))
        log("bank family '%s': %d bank(s) (%d bones), %d sequence(s), %d blend space(s), "
            "%d track(s) unbound%s"
            % (name, len(members), bones, total, spaces_total, dropped_total,
               ", %d grid(s) skipped" % grids_skipped if grids_skipped else ""))
        release_packages()
    return skeletons


def main():
    stems = [s for s in cmdline_arg("BakeCharacters").split(",") if s]
    if not stems:
        raise SystemExit("[chars] no -BakeCharacters=<csv> given")

    with open(os.path.join(NPC_DIR, "npc_manifest.json"), "r", encoding="utf-8") as handle:
        manifest = json.load(handle)

    missing = [s for s in stems if not os.path.isfile(source_path(s))]
    if missing:
        raise SystemExit("[chars] no .eskm for: %s (run: uv run elysium export characters)"
                         % ", ".join(sorted(missing)))

    # A fresh commandlet has not indexed the mount, so does_asset_exist reports False for assets
    # that are already there and a re-bake would rewrite what it could have reused.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([MOUNT],
                                                                           force_rescan=True)
    for package in (SKELETONS, MESHES, MATERIALS, TEXTURES, ANIMS, BANKS):
        bl.ensure_dir(package)

    library = unreal.ElysiumSkeletalBuildLibrary
    partition = read_partition()
    plan = read_plan()
    if plan is not None:
        log("plan: %d scope(s) -- %s" % (
            len(plan), ", ".join("%s[%s]" % (s, "+".join(sorted(v)))
                                 for s, v in sorted(plan.items()))))
    baking = sorted({partition["model_family_of"][stem] for stem in stems})
    log("%d model(s) in %d of %d declared rig famil%s"
        % (len(stems), len(baking), len(partition["models"]),
           "y" if len(partition["models"]) == 1 else "ies"))

    failed = []
    textures = (import_textures_for([source_path(stem) for stem in stems])
                if wants(plan, "_global", "textures") else existing_textures())

    bank_skeletons = bake_banks(manifest, partition, stems, library, failed, plan)

    for name in baking:
        family = partition["models"][name]
        skeleton_package = family["skeleton"]
        scope = "model.%s" % name
        members = family["members"]
        if not any(wants(plan, scope, stage)
                   for stage in ("family_skeletons", "meshes", "clips")):
            continue
        # Every declared member seeds the skeleton, not just the ones this run bakes. The tree and
        # the reference pose are therefore a function of the partition rather than of the slice --
        # which is what an untracked bone falls back to, and what a blend mask is content-addressed
        # against. A slice that seeded from its own members alone would hash the same authored mask
        # to a different profile name and fail the next grid that spans two slices.
        missing = [s for s in members if not os.path.isfile(source_path(s))]
        if missing:
            fail("family %s: %d declared member container(s) missing: %s"
                 % (name, len(missing), ", ".join(missing[:4])))
            failed.append(name)
            continue
        if wants(plan, scope, "family_skeletons"):
            error, bones = library.build_family_skeleton(
                [source_path(s) for s in members], skeleton_package, True)
            if error:
                fail("family skeleton %s: %s" % (name, error))
                failed.append(name)
                continue
            if bones != family["bones"]:
                fail("family skeleton %s: built %d bones, the partition declares %d"
                     % (name, bones, family["bones"]))
                failed.append(name)
        else:
            bones = family["bones"]

        # The meshes merge into a tree that already carries every bone the family declares, so the
        # merge can only be a no-op -- and a refusal means the declared partition disagrees with
        # `MergeAllBonesToBoneTree`, which is the authority. That is a fatal disagreement rather
        # than something to route around: a slice that invented a family here would write a second
        # answer for every clip label under a name nothing else points at.
        mesh_failed = False
        built = [s for s in members if s in stems] if wants(plan, scope, "meshes") else []
        for stem in built:
            blob = eskm.read(source_path(stem))
            error = library.build_skeletal_mesh_from_source(
                source_path(stem), "%s/SK_%s" % (MESHES, stem), skeleton_package,
                BODY_MASTER, MATERIALS,
                material_bindings(blob, textures),
                eye_slot_masters(manifest, stem, blob))
            if error:
                fail("SK_%s: %s" % (stem, error))
                failed.append(stem)
                mesh_failed = True
        log("family '%s': %d mesh(es) of %d declared model(s), %d bones"
            % (name, len(built), len(members), bones))

        # A family whose skeleton is short of a body's bones cannot bake that body's clips, and the
        # clip builder would report every owner in turn against a skeleton that was never finished.
        # The mesh error above is the one worth reading, so stop here rather than bury it.
        if mesh_failed:
            fail("family '%s': skipping clips, a model in it did not build" % name)
            continue

        # What makes the editor offer this family's bodies and the banks' clips together. The
        # runtime needs no declaration -- `DecompressPose` builds the name-keyed remapping for any
        # skeleton pair -- but every editor-side validator consults it.
        if wants(plan, scope, "family_skeletons"):
            error = library.declare_compatible_skeletons(skeleton_package, bank_skeletons)
            if error:
                fail("family '%s': %s" % (name, error))
                failed.append(name)

        if not wants(plan, scope, "clips"):
            release_packages()
            continue

        owners = owner_clips(manifest, [s for s in members if s in stems])
        total = 0
        spaces_total = 0
        # Grids the bake declined. A grid the exporter left with fewer than two live cells is not a
        # blend space and is dropped by the runtime reader too, so it is reported rather than fatal.
        grids_skipped = 0
        # Stays zero here: a body's OWN clips are the only ones this loop builds, and the builder
        # fails outright rather than drop one of those. A bank track with nowhere to bind is the
        # bank pass's business now.
        dropped_total = 0
        for owner, is_bank in sorted(owners.items()):
            if is_bank:
                # Already baked, once, onto a bank skeleton this family is compatible with.
                continue
            path = source_path(owner, bank=is_bank)
            if not os.path.isfile(path):
                fail("no .eskm for clip owner %s" % owner)
                failed.append(owner)
                continue
            error, count, dropped = library.build_anim_sequences_from_source(
                path, "%s/%s/%s" % (ANIMS, name, owner), skeleton_package)
            if error:
                fail("%s: %s" % (owner, error))
                failed.append(owner)
            total += count
            dropped_total += dropped

            # The blend spaces AFTER this owner's sequences, because a sample is one of them. Most
            # owners declare no grid at all and so ship no sidecar -- 913 of the 1,166 sequences on
            # either `move_and_ranged` are a single cell, which is a clip and needs no table.
            blends = blend_source(manifest, owner, is_bank)
            if not blends:
                continue
            error, spaces, skipped_grids, skipped_cells = library.build_blend_spaces_from_grids(
                blends, "%s/%s/%s" % (ANIMS, name, owner), skeleton_package)
            if error:
                fail("%s blends: %s" % (owner, error))
                failed.append(owner)
            spaces_total += spaces
            grids_skipped += skipped_grids
            if skipped_cells:
                log("%s: %d grid cell(s) had no baked clip" % (owner, skipped_cells))
        log("family '%s': %d own clip owner(s), %d sequence(s), %d blend space(s)%s"
            % (name, sum(1 for is_bank in owners.values() if not is_bank), total, spaces_total,
               ", %d grid(s) skipped" % grids_skipped if grids_skipped else ""))

        release_packages()

    if failed:
        raise SystemExit("[chars] failed: %s" % ", ".join(sorted(set(failed))))
    log("done")


main()
