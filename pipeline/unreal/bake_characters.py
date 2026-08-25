# Bakes VtMB characters into real Unreal assets under the /ElysiumBaked mount (ANM1,
# docs/project/animation-roadmap.md).
#
# Where the runtime builds a USkeletalMesh, a throwaway USkeleton and every UAnimSequence at map
# load, this pass runs once in a headless editor and writes them out as assets: a USkeleton and a
# mesh per model, one shared USkeleton per bank rig family, and a compressed UAnimSequence per clip.
#
# Everything it reads is an `.eskm` container written by the offline exporter, and every asset it
# writes is built through the engine's own authoring path in C++ (UElysiumSkeletalBuildLibrary).
# This worker is orchestration only: it decides which banks share a skeleton, which textures to
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
from pathlib import Path

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402
from elysium_pipeline import (  # noqa: E402
    asset_names, character_partition, character_recipes as cr)
from elysium_pipeline.formats import eskm  # noqa: E402
from elysium_pipeline.tasking import ContentDigestCache, DIGEST_CACHE_FILE  # noqa: E402
from elysium_pipeline.paths import export_root  # noqa: E402

MOUNT = "/ElysiumBaked"
CHARACTERS = MOUNT + "/Characters"
#: Every skeleton package path comes off `npc/families.json`; this exists so the folder can be
#: created. The partition module is the one authority on the spelling.
SKELETONS = character_partition.SKELETON_DIR
MESHES = CHARACTERS + "/Meshes"
MATERIALS = CHARACTERS + "/Materials"
TEXTURES = CHARACTERS + "/Textures"
ANIMS = CHARACTERS + "/Anims"
#: Bank clips live here ONCE, addressed by owner alone. A bank is recorded on one rig and played by
#: every body that includes it, so baking one copy per body that reaches it would turn 7,871
#: distinct clips across the cast into an order of magnitude more assets. Every body reaches them
#: through `AddCompatibleSkeleton` instead.
BANKS = ANIMS + "/_banks"

#: Animated props sit apart from the cast: a prop is absent from the declared partition, and
#: nothing declares compatibility with its skeleton -- a crane, a wolf and a wineglass share no
#: tree with each other or with a biped. Everything a prop owns lives under its own stem.
PROPS = MOUNT + "/Props"

#: Every body section is instanced from this one master. Its parameter names are glTF's, which is
#: what lets one instance serve a body drawn as an NPC and the same body worn by the player -- the
#: two differ only in the ModelAlpha the runtime drives, not in the asset.
BODY_MASTER = "/Game/VtMB/Materials/M_PlayerBody.M_PlayerBody"

#: The eyeball sections take this instead. `UElysiumEntityBodies::InstallEyes` finds an eye by
#: asking whether a slot's base material IS this master -- two independent tests, it says, "because
#: either alone can be defeated" -- so a body whose every section is parented to BODY_MASTER
#: defeats both at once and no eye is ever installed.
EYE_MASTER = "/Game/VtMB/Materials/M_Eyes.M_Eyes"

OUT_ROOT = os.fspath(export_root())
NPC_DIR = os.path.join(OUT_ROOT, "npc")


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


def prop_source_path(stem):
    return os.path.join(NPC_DIR, "placed_models", stem + ".eskm")


def prop_package(stem):
    """Where one animated prop's assets live. Its own folder, because it owns its own skeleton."""
    return "%s/%s" % (PROPS, stem)


def bake_props(manifest, library, failed, tracker, selected=()):
    """One skeleton, one mesh and one selected clip set per placed model.

    A prop takes the same container and the same builders a body does -- it IS a skeletal model,
    and the only thing that ever made it a separate path was the loader it went through. What
    differs is the rig: a prop shares no bone tree with anything, so nothing declares compatibility
    with its skeleton and no bank clip is authored against it.

    Returns the number of props baked."""
    props = manifest.get("placed_models", {})
    if not props:
        return 0
    selected = set(selected or props)
    bl.ensure_dir(PROPS)
    baked = 0
    for stem in sorted(props):
        if stem not in selected:
            continue
        unit = cr.prop_object_path(stem)
        if not tracker.wants_unit("props", unit):
            continue
        path = prop_source_path(stem)
        if not os.path.isfile(path):
            fail("no .eskm for placed model %s (run: uv run elysium export characters)" % stem)
            failed.append(stem)
            continue
        package = prop_package(stem)
        bl.ensure_dir(package)
        skeleton_package = "%s/SKEL_%s" % (package, stem)
        error, bones = library.build_family_skeleton(
            [path], skeleton_package, True,
            recipe_fingerprint=tracker.fingerprint("props", unit))
        if error:
            fail("prop skeleton %s: %s" % (stem, error))
            failed.append(stem)
            continue

        # The skeletal asset preserves slot names but uses the neutral body master. A live or
        # GAME_LUMP placement copies the already-baked map material into those slots, avoiding a
        # second global import of the complete prop texture corpus.
        bindings = {}
        error = library.build_skeletal_mesh_from_source(
            path, "%s/SK_%s" % (package, stem), skeleton_package,
            BODY_MASTER, "", bindings, {},
            recipe_fingerprint=tracker.fingerprint("props", unit))
        if error:
            fail("prop SK_%s: %s" % (stem, error))
            failed.append(stem)
            continue

        error, count, dropped = library.build_anim_sequences_from_source(
            path, package, skeleton_package,
            recipe_fingerprint=tracker.fingerprint("props", unit))
        if error:
            fail("prop %s clips: %s" % (stem, error))
            failed.append(stem)
            continue

        spaces = 0
        blend_failed = False
        blends = props[stem].get("blends", "")
        if blends:
            error, spaces, skipped_grids, skipped_cells = library.build_blend_spaces_from_grids(
                blends, package, skeleton_package,
                recipe_fingerprint=tracker.fingerprint("props", unit))
            if error:
                fail("prop %s blends: %s" % (stem, error))
                failed.append(stem)
                blend_failed = True
            elif skipped_cells:
                log("prop %s: %d grid cell(s) had no baked clip" % (stem, skipped_cells))
        log("prop '%s': %d bones, %d clip(s)%s%s"
            % (stem, bones, count,
               ", %d blend space(s)" % spaces if spaces else "",
               ", %d track(s) dropped" % dropped if dropped else ""))
        baked += 1
        if not blend_failed:
            tracker.record("props", unit)
        release_packages()
        tracker.maybe_checkpoint("props through %s" % stem)
    return baked


def read_partition():
    """The declared rig partition, written by the offline half over the WHOLE corpus.

    Read rather than recomputed. A model is a singleton -- one skeleton per stem -- and the file is
    the one statement of which bodies are declared at all. The BANKS are grouped by
    `eskm.rig_families`, which is greedy over the set it is given, so partitioning whatever a run
    happens to name renames bank families -- and can merge two the cast keeps apart -- while the
    bank family name is the path contract for the skeleton every one of its sequences binds to."""
    path = os.path.join(NPC_DIR, "families.json")
    if not os.path.isfile(path):
        raise SystemExit(
            "[chars] %s is missing (run: uv run elysium export characters)" % path)
    with open(path, "r", encoding="utf-8-sig") as handle:
        return character_partition.check(json.load(handle))


class CharacterTracker(object):
    """Decide per-unit work off each asset's recipe stamp, and stamp what this run authors.

    A unit is one package set. A single-asset stage's unit is one object path; every other
    stage's unit is the package family below its root, and the unit is current only when every
    asset under that root carries the unit's fingerprint -- a crash that saved half a family
    reads as stale and the family is authored again whole. No state exists outside the assets:
    the mount is the record, exactly as it is for the map and corpus bakes.
    """

    def __init__(self, units, force=False):
        self.units = units
        self.force = bool(force)
        self.decisions = {}
        self.fingerprints = {}
        self.stages = {}
        self.pending = 0
        self.snapshot = None

    def _counters(self, stage):
        return self.stages.setdefault(stage, {"built": 0, "reused": 0})

    def _unit_assets(self, stage, object_path):
        if stage in cr.SINGLE_ASSET_STAGES:
            if unreal.EditorAssetLibrary.does_asset_exist(object_path):
                return [object_path]
            return []
        listed = unreal.EditorAssetLibrary.list_assets(
            object_path, recursive=True, include_folder=False)
        return [value.split(".", 1)[0] for value in listed]

    def _mount_snapshot(self):
        """Every asset below the character and prop roots, bucketed by package directory --
        one registry pass at first use instead of one recursive listing per unit.

        Sound for `wants_unit` because its decisions are memoized and taken before the unit
        authors anything, and no unit writes below another unit's root. `record` runs after
        the builders created assets this snapshot cannot carry, so it keeps its own live
        listing."""
        if self.snapshot is None:
            by_dir = {}
            count = 0
            for root in (CHARACTERS, PROPS):
                for value in unreal.EditorAssetLibrary.list_assets(
                        root, recursive=True, include_folder=False):
                    path = value.split(".", 1)[0]
                    by_dir.setdefault(path.rsplit("/", 1)[0], []).append(path)
                    count += 1
            self.snapshot = by_dir
            log("registry snapshot: %d asset(s) across %d package dir(s)" % (count, len(by_dir)))
        return self.snapshot

    def _snapshot_unit_assets(self, stage, object_path):
        """`_unit_assets` answered from the process-start snapshot, for `wants_unit`."""
        snapshot = self._mount_snapshot()
        if stage in cr.SINGLE_ASSET_STAGES:
            bucket = snapshot.get(object_path.rsplit("/", 1)[0], ())
            return [object_path] if object_path in bucket else []
        prefix = object_path + "/"
        assets = []
        for directory, paths in snapshot.items():
            if directory == object_path or directory.startswith(prefix):
                assets.extend(paths)
        return assets

    def wants_unit(self, stage, object_path):
        """Whether this run authors one named unit.

        Memoized: a body's several clip owners land in one unit, and every one of them must see
        the same answer within a run -- the first one authors, the rest add to the same package
        family.
        """
        key = (stage, object_path)
        if key in self.decisions:
            return self.decisions[key]
        unit = self.units.get(stage, {}).get(object_path)
        if unit is None:
            self.decisions[key] = False
            return False
        fingerprint = bl.recipe_fingerprint(stage, object_path, unit.recipe)
        self.fingerprints[key] = fingerprint
        assets = self._snapshot_unit_assets(stage, object_path)
        stale = (self.force or not assets
                 or any(bl.stored_recipe(path) != fingerprint for path in assets))
        if not stale:
            self._counters(stage)["reused"] += 1
        self.decisions[key] = stale
        return stale

    def fingerprint(self, stage, object_path):
        """The fingerprint `wants_unit` computed for this unit, for the builders to stamp
        onto each asset as it saves."""
        return self.fingerprints.get((stage, object_path), "")

    def record(self, stage, object_path, stamp=False):
        """Count one authored unit. Recorded only after the builders succeeded.

        The builders stamp each asset as they save it (`RecipeFingerprint` rides the build),
        so recording verifies the unit produced assets instead of re-saving them. A unit that
        never lands its stamps reads as stale next run, which is the correct failure mode.

        `stamp=True` is the textures unit's path: its import skips assets already on the
        mount, so a moved unit fingerprint must be re-stamped across the whole set here --
        the one remaining load-and-resave, paid only when that unit is stale."""
        fingerprint = self.fingerprints.get((stage, object_path))
        if not fingerprint:
            raise SystemExit("[chars] a unit that was never decided was authored: %s %s"
                             % (stage, object_path))
        assets = self._unit_assets(stage, object_path)
        if not assets:
            raise SystemExit("[chars] unit authored nothing to stamp: %s %s"
                             % (stage, object_path))
        if stamp:
            for path in assets:
                asset = unreal.EditorAssetLibrary.load_asset(path)
                if not asset:
                    raise SystemExit(
                        "[chars] authored asset could not be loaded to stamp: %s" % path)
                bl.stamp_recipe(asset, fingerprint)
                if not bl.save(path):
                    raise SystemExit("[chars] stamped asset could not be saved: %s" % path)
        self._counters(stage)["built"] += 1
        self.pending += 1

    def summary(self):
        return "; ".join("%s %d built / %d reused" % (stage, data["built"], data["reused"])
                         for stage, data in sorted(self.stages.items()))

    def checkpoint(self, label):
        """Release this pass's packages. Every recorded unit is already saved and stamped, so
        there is nothing to publish; the release bounds memory."""
        self.pending = 0
        release_packages()
        log("checkpoint: %s -- %s" % (label, self.summary()))

    def maybe_checkpoint(self, label):
        """Checkpoint once a batch of units has landed. Bodies are heavy -- a skeletal mesh plus
        every sequence it owns -- so the batch is small and the editor releases them often."""
        if self.pending >= cr.BODY_BATCH:
            self.checkpoint(label)


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


def read_texture_table():
    """{asset name: albedo path relative to `npc/`} for the whole cast, or None when it is absent.

    The offline half writes `npc/textures.json` from every body container's material table while it
    partitions the corpus, so the document is both the complete inventory and the one input the
    textures unit's recipe is a function of. Only bodies contribute a `MATL` section, so every uri
    in it resolves against `npc/`.
    """
    path = os.path.join(NPC_DIR, "textures.json")
    if not os.path.isfile(path):
        fail("%s is missing (run: uv run elysium export characters)" % path)
        return None
    with open(path, "r", encoding="utf-8-sig") as handle:
        document = json.load(handle)
    return {name: entry.get("uri", "")
            for name, entry in document.get("textures", {}).items()}


def import_texture_corpus(failed, force=False):
    """Import every albedo `npc/textures.json` names that is not already on the mount, once.

    The npc/tex/ tree is shared across the cast -- 514 files for 166 models -- so the textures are
    one package for all of them rather than a per-model copy, which is also what keeps a re-bake of
    one body from re-importing them all.

    The WHOLE table is covered whichever bodies this run bakes, because the textures unit is a
    single receipt over the whole document: a run that imported only its own slice would record
    that receipt with another body's albedo still absent, and every later run would then skip the
    import and build that body untextured. Everything already on the mount is skipped, so a sliced
    run pays only for what its slice added; `force` is the recovery surface and re-imports the lot.

    Returns {asset name: Texture2D} over everything the mount now carries, which is what a mesh's
    material bindings resolve through.
    """
    table = read_texture_table()
    if table is None:
        failed.append("textures.json")
        return {}
    out = existing_textures()
    wanted = {}
    for name, uri in sorted(table.items()):
        if not uri:
            fail("texture table names %s with no source uri" % name)
            failed.append(name)
            continue
        if name in out and not force:
            continue
        source = os.path.join(NPC_DIR, uri.replace("/", os.sep))
        if os.path.isfile(source):
            wanted[name] = source
        else:
            fail("texture source missing: %s" % source)
            failed.append(os.path.basename(source))
    if not wanted:
        log("textures: %d on the mount, none to import" % len(out))
        return out
    imported = bl.import_textures([(src, name) for name, src in sorted(wanted.items())], TEXTURES)
    saved = 0
    for name, texture in sorted(imported.items()):
        bl.configure_texture(texture, "albedo")
        # import_textures leaves the package unsaved by design -- the map bake saves through its own
        # asset tracker. Nothing tracks these, and an unsaved texture package is the worst kind of
        # failure here: the material instance still serialises its reference, so the bake reports
        # success and the body draws untextured on the next run.
        if bl.save("%s/%s" % (TEXTURES, name)):
            out[name] = texture
            saved += 1
        else:
            fail("could not save texture %s" % name)
            failed.append(name)
    for name in sorted(set(wanted) - set(imported)):
        fail("texture did not import: %s" % wanted[name])
        failed.append(name)
    log("textures: %d imported, %d saved, %d on the mount" % (len(imported), saved, len(out)))
    return out


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
    """{asset name: Texture2D} already on the mount.

    The bindings a mesh is built with resolve through this, so skipping the import must not mean
    skipping the lookup -- a mesh built against an empty table binds no albedo and draws white. It
    is also what an import consults to know which rows of the table it still owes.
    """
    out = {}
    for path in unreal.EditorAssetLibrary.list_assets(TEXTURES, recursive=False,
                                                      include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if asset is not None:
            out[asset.get_name()] = asset
    log("textures: %d already on the mount" % len(out))
    return out


class LazyTextureTable(object):
    """The mount's texture table, read on first actual use.

    The reuse path only consults textures when a stale mesh unit binds them
    (`material_bindings`); a fully-current cast never asks, so its run loads no texture
    package at all. First use loads the whole table through `existing_textures`, which
    keeps the binding contract identical to the eager dict."""

    def __init__(self):
        self._table = None

    def get(self, name, default=None):
        if self._table is None:
            self._table = existing_textures()
        return self._table.get(name, default)


def material_bindings(blob, textures):
    """({material slot: Texture2D asset path}, [albedo the table holds no asset for]) for one model.

    An unresolved slot is reported rather than dropped. A mesh built without it still serialises a
    material instance, one that binds no albedo and draws untextured, and its recipe names the same
    bindings either way -- so nothing would ever rebuild it.
    """
    out = {}
    unbound = set()
    for material, uri in eskm.materials(blob).items():
        if not uri:
            continue
        name = texture_asset_name(uri)
        asset = textures.get(name)
        if asset is None:
            unbound.add(name)
            continue
        out[material] = asset.get_path_name()
    return out, sorted(unbound)


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
        # A label names every bank that DECLARES it, in include-tree order, so a clip entry is a
        # list of owners rather than one -- flattened here for the same reason
        # `character_source_plan` flattens it. Read as a scalar the list reaches a dict lookup and
        # raises `TypeError: unhashable type: 'list'`. A lone string is still accepted.
        for declared in record.get("clips", {}).values():
            for owner in (declared if isinstance(declared, list) else [declared]):
                if owner == stem or owner in owners:
                    continue
                if owner not in manifest["banks"]:
                    fail("%s names bank '%s', which the manifest does not carry" % (stem, owner))
                    continue
                owners[owner] = True

    # A cinematic bank is named by a choreographed SCENE rather than by any body's clip map, so
    # the walk above never reaches one. Without it the performance is absent from the mount and
    # `ResolveClipFromBank` answers out of the glb instead -- in glTFRuntime's basis rather than
    # the container's, which stands every actor of every scene a quarter turn off, and the scene
    # writes that facing back into the entity's angles when it ends
    # (the character verifier).
    for record in manifest.get("cinematics", {}).values():
        for root in record.get("roots", []):
            bank = root.get("bank")
            if not bank or bank in owners:
                continue
            if bank not in manifest["banks"]:
                fail("cinematic root names bank '%s', which the manifest does not carry" % bank)
                continue
            owners[bank] = True
    return owners


def blend_source(manifest, owner, is_bank):
    """One clip owner's blend sidecar path, relative to `npc/`, or "" when it declares no grid.

    The value is the manifest's own `blends` field rather than a path this file builds, because the
    same field addresses both "blends/<stem>.json" and the animated props' nested variant, and the
    C++ reader resolves it the one way the runtime does."""
    section = manifest["banks"] if is_bank else manifest["npcs"]
    return section.get(owner, {}).get("blends", "")


def bake_banks(manifest, partition, stems, library, failed, tracker):
    """Bake every bank the named bodies reach ONCE, and return the skeletons they play them from.

    A `UAnimSequence` is bound to exactly one `USkeleton`, so a bank authored against the rig of
    every body that includes it is a whole rebuild per body -- and the cast disagrees about its
    rigs for small reasons, mostly the generic `BoneNN` hair chains VtMB reuses across bodies for
    different chains. Two banks alone carry 4,551 of the cast's 7,871 distinct clips, so that shape
    costs multiples of the work and multiples of the mount for no additional animation.

    The banks get their own skeletons, grouped by `eskm.rig_families` over the whole corpus, and
    every body declares compatibility with all of them. The common `USkeleton` reference rotations
    are deliberately neutral, so Unreal's compatible-skeleton remap is an index map and cannot
    rotate the decoded pose. Each sequence names its donor bind as a retarget source; stock
    `OrientAndScale` maps only translation onto the playing mesh. The mesh itself retains its exact
    authored bind.

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
        # The package path is returned whether or not this run authors it: a body still declares
        # compatibility with every bank family, and a skeleton left alone is still on the mount.
        skeletons.append(skeleton_package)
        members = [b for b in family["members"] if os.path.isfile(source_path(b, bank=True))]
        if len(members) != len(family["members"]):
            fail("bank family %s: %d of %d member containers are missing"
                 % (name, len(family["members"]) - len(members), len(family["members"])))
            failed.append(name)
            continue
        if tracker.wants_unit("bank_skeletons", skeleton_package):
            # Rebuilt from every declared member, in declared order, rather than merged into
            # whatever a previous run left behind: a skeleton that only grows keeps the bones of
            # banks a later partition moved elsewhere, and an untracked bone falls back to exactly
            # that tree.
            error, bones = library.build_family_skeleton(
                [source_path(b, bank=True) for b in members], skeleton_package, True,
                recipe_fingerprint=tracker.fingerprint("bank_skeletons", skeleton_package))
            if error:
                fail("bank skeleton %s: %s" % (name, error))
                failed.append(name)
                continue
            if bones != family["bones"]:
                fail("bank skeleton %s: built %d bones, the partition declares %d"
                     % (name, bones, family["bones"]))
                failed.append(name)
                # The sequences below bind to this skeleton, so a tree the partition disagrees with
                # takes the family's banks with it rather than receipting them against it.
                continue
            tracker.record("bank_skeletons", skeleton_package)
        else:
            bones = family["bones"]

        total = 0
        spaces_total = 0
        dropped_total = 0
        grids_skipped = 0
        baked_banks = 0
        for bank in [b for b in family["members"] if b in needed]:
            unit = cr.bank_clips_object_path(bank)
            if not tracker.wants_unit("banks", unit):
                continue
            baked_banks += 1
            broken = False
            package = "%s/%s" % (BANKS, bank)
            error, count, dropped = library.build_anim_sequences_from_source(
                source_path(bank, bank=True), package, skeleton_package,
                recipe_fingerprint=tracker.fingerprint("banks", unit))
            if error:
                fail("%s: %s" % (bank, error))
                failed.append(bank)
                broken = True
            total += count
            dropped_total += dropped

            blends = blend_source(manifest, bank, True)
            if blends:
                error, spaces, skipped_grids, skipped_cells = (
                    library.build_blend_spaces_from_grids(
                        blends, package, skeleton_package,
                        recipe_fingerprint=tracker.fingerprint("banks", unit)))
                if error:
                    fail("%s blends: %s" % (bank, error))
                    failed.append(bank)
                    broken = True
                spaces_total += spaces
                grids_skipped += skipped_grids
                if skipped_cells:
                    log("%s: %d grid cell(s) had no baked clip" % (bank, skipped_cells))
            if not broken:
                tracker.record("banks", unit)
            tracker.maybe_checkpoint("banks through %s" % bank)
        log("bank family '%s': %d bank(s) of %d member(s) (%d bones), %d sequence(s), "
            "%d blend space(s), %d track(s) unbound%s"
            % (name, baked_banks, len(members), bones, total, spaces_total, dropped_total,
               ", %d grid(s) skipped" % grids_skipped if grids_skipped else ""))
        release_packages()
    return skeletons


def bake_bodies(manifest, partition, stems, library, failed, tracker,
                textures, bank_skeletons, baking):
    """One skeleton, one mesh and its own clips, per model.

    Each body and each clip owner is a unit of its own: a body's skeleton is a function of its own
    container's bone tree, so a body whose geometry changed rebuilds that body and leaves every
    other mesh and every sequence on the mount alone.
    """
    for name in baking:
        entry = partition["models"][name]
        skeleton_package = entry["skeleton"]
        if not os.path.isfile(source_path(name)):
            fail("body %s: the declared container is missing" % name)
            failed.append(name)
            continue
        authoring_skeleton = tracker.wants_unit("family_skeletons", skeleton_package)
        if authoring_skeleton:
            error, bones = library.build_family_skeleton(
                [source_path(name)], skeleton_package, True,
                recipe_fingerprint=tracker.fingerprint("family_skeletons", skeleton_package))
            if error:
                fail("body skeleton %s: %s" % (name, error))
                failed.append(name)
                continue
            if bones != entry["bones"]:
                # The container's own tree against what `FReferenceSkeletonModifier` accepted: a
                # case-fold merge inside one body reads back here as a short bone count.
                fail("body skeleton %s: built %d bones, the partition declares %d"
                     % (name, bones, entry["bones"]))
                failed.append(name)
                # The mesh and every sequence below bind to this skeleton, so a tree the partition
                # disagrees with takes this body with it rather than leaving receipts against a
                # skeleton this run just declared wrong.
                continue
        else:
            bones = entry["bones"]

        # The mesh merges into a tree seeded from this body's own container, so the merge can only
        # be a no-op -- and a refusal means the declared partition disagrees with
        # `MergeAllBonesToBoneTree`, which is the authority. That is a fatal disagreement rather
        # than something to route around.
        mesh_failed = False
        built = []
        if (name in stems
                and tracker.wants_unit("meshes", cr.mesh_object_path(name))):
            built = [name]
        for stem in built:
            blob = eskm.read(source_path(stem))
            bindings, unbound = material_bindings(blob, textures)
            if unbound:
                fail("SK_%s: %d albedo(s) absent from %s: %s"
                     % (stem, len(unbound), TEXTURES, ", ".join(unbound)))
                failed.append(stem)
                mesh_failed = True
                continue
            error = library.build_skeletal_mesh_from_source(
                source_path(stem), "%s/SK_%s" % (MESHES, stem), skeleton_package,
                BODY_MASTER, MATERIALS, bindings,
                eye_slot_masters(manifest, stem, blob),
                recipe_fingerprint=tracker.fingerprint("meshes", cr.mesh_object_path(stem)))
            if error:
                fail("SK_%s: %s" % (stem, error))
                failed.append(stem)
                mesh_failed = True
            else:
                tracker.record("meshes", cr.mesh_object_path(stem))
                tracker.maybe_checkpoint("meshes through %s" % stem)
        log("body '%s': %d mesh(es) authored, %d bones" % (name, len(built), bones))

        # A skeleton short of this body's bones cannot bake this body's clips, and the clip builder
        # would report every owner in turn against a skeleton that was never finished. The mesh
        # error above is the one worth reading, so stop here rather than bury it.
        if mesh_failed:
            fail("body '%s': skipping clips, its mesh did not build" % name)
            continue

        # What makes the editor offer this body and the banks' clips together. The runtime needs no
        # declaration -- `DecompressPose` builds the name-keyed remapping for any skeleton pair --
        # but every editor-side validator consults it.
        if authoring_skeleton:
            error = library.declare_compatible_skeletons(
                skeleton_package, bank_skeletons,
                recipe_fingerprint=tracker.fingerprint("family_skeletons", skeleton_package))
            if error:
                fail("body '%s': %s" % (name, error))
                failed.append(name)
                # The clips below play through that declaration, so an unfinished skeleton stops
                # them being authored against it.
                continue
            tracker.record("family_skeletons", skeleton_package)

        owners = owner_clips(manifest, [name])
        total = 0
        spaces_total = 0
        # Grids the bake declined. A grid the exporter left with fewer than two live cells is not a
        # blend space and is dropped by the runtime reader too, so it is reported rather than fatal.
        grids_skipped = 0
        # Stays zero here: a body's OWN clips are the only ones this loop builds, and the builder
        # fails outright rather than drop one of those. A bank track with nowhere to bind is the
        # bank pass's business now.
        dropped_total = 0
        authored_owners = 0
        for owner, is_bank in sorted(owners.items()):
            if is_bank:
                # Already baked, once, onto a bank skeleton this body is compatible with.
                continue
            unit = cr.clips_object_path(name)
            if not tracker.wants_unit("clips", unit):
                continue
            authored_owners += 1
            broken = False
            path = source_path(owner, bank=is_bank)
            if not os.path.isfile(path):
                fail("no .eskm for clip owner %s" % owner)
                failed.append(owner)
                continue
            error, count, dropped = library.build_anim_sequences_from_source(
                path, "%s/%s" % (ANIMS, name), skeleton_package,
                recipe_fingerprint=tracker.fingerprint("clips", unit))
            if error:
                fail("%s: %s" % (owner, error))
                failed.append(owner)
                broken = True
            total += count
            dropped_total += dropped

            # The blend spaces AFTER this owner's sequences, because a sample is one of them. Most
            # owners declare no grid at all and so ship no sidecar -- 913 of the 1,166 sequences on
            # either `move_and_ranged` are a single cell, which is a clip and needs no table.
            blends = blend_source(manifest, owner, is_bank)
            if blends:
                error, spaces, skipped_grids, skipped_cells = (
                    library.build_blend_spaces_from_grids(
                        blends, "%s/%s" % (ANIMS, name), skeleton_package,
                        recipe_fingerprint=tracker.fingerprint("clips", unit)))
                if error:
                    fail("%s blends: %s" % (owner, error))
                    failed.append(owner)
                    broken = True
                spaces_total += spaces
                grids_skipped += skipped_grids
                if skipped_cells:
                    log("%s: %d grid cell(s) had no baked clip" % (owner, skipped_cells))
            if not broken:
                tracker.record("clips", unit)
            tracker.maybe_checkpoint("clips through %s" % owner)
        log("body '%s': %d own clip owner(s), %d sequence(s), %d blend space(s)%s"
            % (name, authored_owners, total, spaces_total,
               ", %d grid(s) skipped" % grids_skipped if grids_skipped else ""))

        release_packages()


def main():
    stems = [s for s in cmdline_arg("BakeCharacters").split(",") if s]
    prop_stems = [s for s in cmdline_arg("BakeProps").split(",") if s]
    if not stems and not prop_stems:
        raise SystemExit("[chars] no -BakeCharacters=<csv> or -BakeProps=<csv> given")

    with open(os.path.join(NPC_DIR, "npc_manifest.json"), "r", encoding="utf-8") as handle:
        manifest = json.load(handle)

    missing = [s for s in stems if not os.path.isfile(source_path(s))]
    if missing:
        raise SystemExit("[chars] no .eskm for: %s (run: uv run elysium export characters)"
                         % ", ".join(sorted(missing)))
    unknown_props = [s for s in prop_stems if s not in manifest.get("placed_models", {})]
    if unknown_props:
        raise SystemExit("[chars] placed model(s) absent from manifest: %s"
                         % ", ".join(sorted(unknown_props)))
    missing_props = [s for s in prop_stems if not os.path.isfile(prop_source_path(s))]
    if missing_props:
        raise SystemExit("[chars] no placed-model .eskm for: %s"
                         % ", ".join(sorted(missing_props)))

    # A fresh commandlet has not indexed the mount, so does_asset_exist reports False for assets
    # that are already there and a re-bake would rewrite what it could have reused. Forced,
    # because the registry's own start-up scan runs in the background and a plain scan of a
    # path that gatherer already owns returns at once, leaving the recipe tags unread.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([MOUNT], force_rescan=True)
    for package in (SKELETONS, MESHES, MATERIALS, TEXTURES, ANIMS, BANKS):
        bl.ensure_dir(package)

    library = unreal.ElysiumSkeletalBuildLibrary
    partition = read_partition()
    force = bool(cmdline_arg("BakeForce", ""))
    digest_cache = ContentDigestCache(Path(OUT_ROOT) / DIGEST_CACHE_FILE)
    units = cr.declared_units(Path(NPC_DIR), manifest, partition, stems,
                              prop_stems or None, cache=digest_cache)
    digest_cache.write()
    tracker = CharacterTracker(units, force=force)
    log("declared: %d unit(s) across %d stage(s)%s" % (
        sum(len(stage_units) for stage_units in units.values()),
        sum(1 for stage_units in units.values() if stage_units),
        " (forced)" if force else ""))
    baking = sorted(set(stems))
    undeclared = [stem for stem in baking if stem not in partition["models"]]
    if undeclared:
        raise SystemExit("[chars] not in the declared partition: %s "
                         "(run: uv run elysium export characters)" % ", ".join(undeclared))
    log("%d model(s) of %d declared" % (len(baking), len(partition["models"])))

    failed = []
    # A prop-only run authors no body, so it reaches no binding and imports nothing: the placed
    # models take the neutral body master and their owning map's already-baked materials.
    authoring_textures = bool(stems) and tracker.wants_unit("textures", cr.TEXTURES)
    if authoring_textures:
        before = len(failed)
        textures = import_texture_corpus(failed, force=force)
        # A texture that did not import or did not save leaves a body drawing untextured, so the
        # unit is not stamped and the next run imports it again.
        if len(failed) == before:
            tracker.record("textures", cr.TEXTURES, stamp=True)
    else:
        # Only a stale mesh unit consults the table, so the reuse path defers the load:
        # a run that rebuilds no mesh opens no texture package.
        textures = LazyTextureTable()
        log("textures: not authoring, table loads on first use")

    bank_skeletons = bake_banks(manifest, partition, stems, library, failed, tracker)
    props_baked = bake_props(manifest, library, failed, tracker, prop_stems)
    if props_baked:
        log("%d animated prop(s) baked to %s" % (props_baked, PROPS))

    bake_bodies(manifest, partition, stems, library, failed, tracker,
                textures, bank_skeletons, baking)

    tracker.checkpoint("final")

    if failed:
        raise SystemExit("[chars] failed: %s" % ", ".join(sorted(set(failed))))
    log("done")


main()
