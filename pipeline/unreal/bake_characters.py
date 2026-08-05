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


def import_textures_for(containers):
    """Import every albedo the named models reference, once. The npc/tex/ tree is shared across the
    cast -- 514 files for 166 models -- so the textures are one package for all of them rather than
    a per-model copy, which is also what keeps a re-bake of one body from re-importing them all."""
    wanted = {}
    for blob in containers:
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


def rig_families(sources, stems):
    """Partition the models into sets that one USkeleton can carry.

    The cast is not one rig. VtMB's Bip01 biped is consistent across every model and every bank --
    which is what makes a bank clip shareable at all -- but the appendix chains are not: the generic
    `BoneNN` hair names denote a different chain on different bodies (`Bone05` hangs off `Bone04` on
    one body and off `Bip01 Head` on another), and Unreal's reference skeleton is single-rooted so
    the models whose VtMB skeleton forks into a second skinned root carry a synthetic root above
    Bip01. A family is a maximal set that agrees on every bone it shares.

    Greedy and order-dependent by construction, so the stems are sorted: the partition has to be the
    same on every run or a re-bake renames the families out from under the meshes that point at them.
    """
    families = []
    for stem in sorted(stems):
        tree = eskm.bone_parents(sources[stem])
        for family in families:
            if bl.rig_trees_compatible(family["tree"], tree):
                bl.rig_tree_merge(family["tree"], tree)
                family["stems"].append(stem)
                break
        else:
            families.append({"name": stem, "tree": dict(tree), "stems": [stem]})
    return families


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
    for package in (SKELETONS, MESHES, MATERIALS, TEXTURES, ANIMS):
        bl.ensure_dir(package)

    library = unreal.ElysiumSkeletalBuildLibrary
    sources = {stem: eskm.read(source_path(stem)) for stem in stems}
    families = rig_families(sources, stems)
    log("%d model(s) in %d rig famil%s"
        % (len(stems), len(families), "y" if len(families) == 1 else "ies"))

    textures = import_textures_for(sources.values())
    failed = []

    for family in families:
        name = family["name"]
        skeleton_package = "%s/%s%s" % (SKELETONS, SKELETON_PREFIX, name)

        # The meshes first: the skeleton's bone tree is the union of the bodies merged into it, and
        # a clip cannot bind to a bone the tree does not carry yet.
        for stem in family["stems"]:
            error = library.build_skeletal_mesh_from_source(
                source_path(stem), "%s/SK_%s" % (MESHES, stem), skeleton_package,
                BODY_MASTER, MATERIALS, material_bindings(sources[stem], textures))
            if error:
                fail("SK_%s: %s" % (stem, error))
                failed.append(stem)
        log("family '%s': %d model(s), %d bones"
            % (name, len(family["stems"]), len(family["tree"])))

        owners = owner_clips(manifest, family["stems"])
        total = 0
        for owner, is_bank in sorted(owners.items()):
            path = source_path(owner, bank=is_bank)
            if not os.path.isfile(path):
                fail("no .eskm for clip owner %s" % owner)
                failed.append(owner)
                continue
            error, count = library.build_anim_sequences_from_source(
                path, "%s/%s/%s" % (ANIMS, name, owner), skeleton_package)
            if error:
                fail("%s: %s" % (owner, error))
                failed.append(owner)
            total += count
        log("family '%s': %d clip owner(s), %d sequence(s)" % (name, len(owners), total))

    if failed:
        raise SystemExit("[chars] failed: %s" % ", ".join(sorted(set(failed))))
    log("done")


main()
