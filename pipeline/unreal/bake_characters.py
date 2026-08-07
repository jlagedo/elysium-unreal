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
from elysium_pipeline.formats import eskm  # noqa: E402
from elysium_pipeline.paths import export_root  # noqa: E402

MOUNT = "/ElysiumBaked"
CHARACTERS = MOUNT + "/Characters"
SKELETONS = CHARACTERS + "/Skeletons"
SKELETON_PREFIX = "SKEL_Elysium_"
MESHES = CHARACTERS + "/Meshes"
MATERIALS = CHARACTERS + "/Materials"
TEXTURES = CHARACTERS + "/Textures"
ANIMS = CHARACTERS + "/Anims"
#: Bank clips live here ONCE, addressed by owner alone. A bank is recorded on one rig and played by
#: every body that includes it, so baking it per rig family turned 7,871 distinct clips across the
#: cast into ~90,000 assets. The families share these through `AddCompatibleSkeleton` instead.
BANKS = ANIMS + "/_banks"
BANK_SKELETON_PREFIX = "SKEL_ElysiumBank_"

#: Every body section is instanced from this one master. Its parameter names are glTF's, which is
#: what lets one instance serve a body drawn as an NPC and the same body worn by the player -- the
#: two differ only in the ModelAlpha the runtime drives, not in the asset.
BODY_MASTER = "/Game/VtMB/Materials/M_PlayerBody.M_PlayerBody"

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


def texture_asset_name(uri):
    return "T_" + bl.safe_name(os.path.splitext(os.path.basename(uri))[0])


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


def bake_banks(manifest, stems, library, failed):
    """Bake every bank the cast reaches ONCE, and return the skeletons the bodies play them from.

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
    """
    needed = set()
    for bank in sorted(owner for owner, is_bank in owner_clips(manifest, stems).items() if is_bank):
        if os.path.isfile(source_path(bank, bank=True)):
            needed.add(bank)
        else:
            fail("no .eskm for clip owner %s" % bank)
            failed.append(bank)
    if not needed:
        return []

    # Partitioned over EVERY bank the manifest carries, not just the ones this run reaches, because
    # `rig_families` is greedy over the set it is given: naming two models would otherwise seed the
    # families differently and write the same clips under a differently-named skeleton. Each
    # skeleton is likewise built from all of its family's banks, so its bone tree does not depend on
    # which models were named either. Only the SEQUENCES are restricted to what this run needs.
    everything = sorted(bank for bank in manifest["banks"]
                        if os.path.isfile(source_path(bank, bank=True)))
    trees = {bank: eskm.bone_parents(eskm.read(source_path(bank, bank=True)))
             for bank in everything}
    bank_families = eskm.rig_families(trees, everything)
    log("%d bank(s) in %d rig famil%s, %d to bake"
        % (len(everything), len(bank_families), "y" if len(bank_families) == 1 else "ies",
           len(needed)))

    skeletons = []
    for family in bank_families:
        skeleton_package = "%s/%s%s" % (SKELETONS, BANK_SKELETON_PREFIX, family["name"])
        for bank in family["stems"]:
            error = library.build_skeleton_from_source(source_path(bank, bank=True),
                                                       skeleton_package)
            if error:
                fail("bank skeleton %s: %s" % (bank, error))
                failed.append(bank)
        skeletons.append(skeleton_package)

        total = 0
        spaces_total = 0
        dropped_total = 0
        grids_skipped = 0
        for bank in [b for b in family["stems"] if b in needed]:
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
        log("bank family '%s': %d bank(s), %d sequence(s), %d blend space(s), "
            "%d track(s) unbound%s"
            % (family["name"], len(family["stems"]), total, spaces_total, dropped_total,
               ", %d grid(s) skipped" % grids_skipped if grids_skipped else ""))
        unreal.SystemLibrary.collect_garbage()
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
    # Bone trees only, NOT the containers. The cast's `.eskm` files are 400 MB together and the
    # bake needs a container's bytes for exactly one call; holding all of them for the length of
    # the run is most of a gigabyte that never gets used again. The bytes are released as each
    # tree is taken.
    trees = {stem: eskm.bone_parents(eskm.read(source_path(stem))) for stem in stems}
    families = eskm.rig_families(trees, stems)
    log("%d model(s) in %d rig famil%s"
        % (len(stems), len(families), "y" if len(families) == 1 else "ies"))

    textures = import_textures_for([source_path(stem) for stem in stems])
    failed = []

    bank_skeletons = bake_banks(manifest, stems, library, failed)

    # A WORKLIST, not a plain loop: `rig_families` PREDICTS what one USkeleton can carry, and
    # `USkeleton::MergeAllBonesToBoneTree` is the authority on it. Where the two disagree the model
    # is re-homed into a family of its own and built there, rather than the run failing on a
    # partition it cannot revise. That keeps the prediction free to be imperfect -- it is a
    # grouping heuristic that saves assets, not a correctness rule -- and it costs one extra
    # skeleton for a model no other body could have shared anyway.
    pending = list(families)
    rehomed = []
    while pending:
        family = pending.pop(0)
        name = family["name"]
        skeleton_package = "%s/%s%s" % (SKELETONS, SKELETON_PREFIX, name)

        # The meshes first: the skeleton's bone tree is the union of the bodies merged into it, and
        # a clip cannot bind to a bone the tree does not carry yet.
        mesh_failed = False
        for stem in list(family["stems"]):
            error = library.build_skeletal_mesh_from_source(
                source_path(stem), "%s/SK_%s" % (MESHES, stem), skeleton_package,
                BODY_MASTER, MATERIALS,
                material_bindings(eskm.read(source_path(stem)), textures))
            if error and "another rig family" in error and stem != family["stems"][0]:
                # The seed is excluded: it CREATED this skeleton, so it cannot be incompatible
                # with it, and re-homing it would leave the family without one.
                family["stems"].remove(stem)
                pending.append({"name": stem, "tree": dict(trees[stem]),
                                "stems": [stem]})
                rehomed.append(stem)
                log("%s: own rig family -- the skeleton refused the merge the partition expected"
                    % stem)
                continue
            if error:
                fail("SK_%s: %s" % (stem, error))
                failed.append(stem)
                mesh_failed = True
        log("family '%s': %d model(s)" % (name, len(family["stems"])))

        # A family whose skeleton is short of a body's bones cannot bake that body's clips, and the
        # clip builder would report every owner in turn against a skeleton that was never finished.
        # The mesh error above is the one worth reading, so stop here rather than bury it.
        if mesh_failed:
            fail("family '%s': skipping clips, a model in it did not build" % name)
            continue

        # What lets this family play the banks without owning a copy of them. Declared after the
        # meshes, because the skeleton does not exist until the first body merges into it.
        error = library.declare_compatible_skeletons(skeleton_package, bank_skeletons)
        if error:
            fail("family '%s': %s" % (name, error))
            failed.append(name)

        owners = owner_clips(manifest, family["stems"])
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

        # Everything this family built is saved and will never be touched again, but it stays
        # rooted in memory until something collects it. Over the whole cast that is tens of
        # thousands of sequences, and the run dies of a failed allocation somewhere in the tail
        # rather than of anything wrong with the model it was on.
        unreal.SystemLibrary.collect_garbage()

    if rehomed:
        log("%d model(s) re-homed into their own rig family: %s"
            % (len(rehomed), ", ".join(sorted(rehomed))))
    if failed:
        raise SystemExit("[chars] failed: %s" % ", ".join(sorted(set(failed))))
    log("done")


main()
