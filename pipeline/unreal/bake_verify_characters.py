# Verifies what pipeline/unreal/bake_characters.py wrote onto /ElysiumBaked/Characters.
#
# Reads the asset registry and the built assets themselves rather than re-deriving them from the
# export, so it answers "is what landed usable" rather than "did the bake believe it succeeded".
# Every check here is one that fails silently in game: a mesh whose material slot lost its instance
# renders untextured, a sequence that lost its compressed data plays as a rest pose, and a face with
# no morph-target curve metadata evaluates its facial track correctly and moves nothing.
#
# Clip coverage is a contract, not a statistic. Runtime has no second character build and no
# animation fallback, so a clip name the runtime can resolve and the mount cannot answer is a
# missing bake stage rather than permission to correct anything at runtime. Two consequences shape
# what is checked:
#
#   - EVERY CELL OF A GRID, not the neutral pick. A cell is chosen from pose parameters driven at
#     runtime, and `ResolveGridClip` hands that cell straight to `LoadBakedClip`; checking only what
#     `move_yaw = 0` selects would leave eight of a nine-cell fan unverified. A blend space standing
#     on the mount does not excuse its cells -- `ResolveClip` and `ResolveClipFromBank` never consult
#     the space.
#   - BANKS SETTLE BY FOLDER, because there are two resolvers. `ResolveClip`/`ResolveAssets` are
#     driven by a body's own vocabulary, which is enumerable per stem. `ResolveClipFromBank` is
#     handed a bank stem and a clip name straight from a choreographed scene with no vocabulary in
#     between, so its names are not enumerable -- but the banks it can be pointed at are, one per
#     `bonerename` actor in the index's cinematic sets. A bank with no folder answers no name, so
#     the folder settles every clip it owns at once.
#
# Internal editor worker coordinated by `uv run elysium export characters`:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript
#       -script="pipeline/unreal/bake_verify_characters.py" -BakeCharacters=<csv>
import json
import os

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from elysium_pipeline import character_partition  # noqa: E402
from elysium_pipeline import mounts  # noqa: E402
from elysium_pipeline import wield_corpus as wc  # noqa: E402
from elysium_pipeline.formats import eskm  # noqa: E402
from elysium_pipeline.paths import export_root  # noqa: E402

MOUNT = mounts.BAKED
CHARACTERS = MOUNT + "/Characters"
SKELETON_PREFIX = "SKEL_Elysium_"
BANK_SKELETON_PREFIX = "SKEL_ElysiumBank_"
MESHES = CHARACTERS + "/Meshes"
ANIMS = CHARACTERS + "/Anims"
PROPS = MOUNT + "/Props"
#: The one anim folder addressed by a bank rather than by a body, for the banks the whole cast
#: reaches through its skeletons' compatibility declarations.
BANKS_FOLDER = "_banks"

NPC_DIR = os.path.join(os.fspath(export_root()), "npc")


def log(msg):
    unreal.log("[chars-verify] %s" % msg)


def cmdline_arg(key, default=""):
    needle = "-%s=" % key
    for token in unreal.SystemLibrary.get_command_line().split():
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def assets_under(package):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    return registry.get_assets_by_path(package, recursive=True)


def read_partition():
    """The declared partition, refused unless this build can read it.

    Checked here as well as on the bake side: a stale `families.json` names a layout the verifier
    would then measure the mount against, and every miss would be reported as a missing asset.
    """
    with open(os.path.join(NPC_DIR, "families.json"), "r", encoding="utf-8") as handle:
        return character_partition.check(json.load(handle))


#: {container path: (the wield mounts it declares, {clip label: the mounts that clip channels})}.
#: A container is 0.4-30 MB and the mesh check and the clip check ask about the same file, so the
#: two small answers are kept rather than the blob.
_MOUNTS = {}


def declared_mounts(path, container=None):
    """What one .eskm states about the wield mounts: the bones, and which clip channels which.

    A body carries these bones with zero skin weight and a bank animates them, so nothing about the
    geometry states they are needed and every stage that prunes by weight or by measured motion can
    drop them without a symptom. `docs/vtmb/wielded_weapons.md` owns the set.

    Per clip as well as per container, because a masked overlay legitimately states no channel for a
    bone its mask does not own -- 28 of the corpus's bank clips, all `*_layer`. Reading what each
    clip actually wrote is what keeps the check about the bake dropping a channel rather than about
    the export having authored one.
    """
    if path not in _MOUNTS:
        blob = container if container is not None else eskm.read(path)
        mounts = {index: name for index, (name, _parent) in enumerate(eskm.bones(blob))
                  if name.lower() in wc.PROP_BONES}
        per_clip = {label: [mounts[bone] for bone in sorted(bones) if bone in mounts]
                    for label, bones in eskm.clip_track_bones(blob).items()}
        _MOUNTS[path] = ([mounts[index] for index in sorted(mounts)], per_clip)
    return _MOUNTS[path]


def verify_declared_compat(partition, stem, skeleton, errors):
    """Every declared bank skeleton must be on this body skeleton's compatible list.

    A bank is baked once against a skeleton of its own, and the declaration is what builds the
    name-keyed bone map the evaluator remaps a bank clip through. Missing, the clips still load,
    still name the right bones, and evaluate on a rig that never learned them.
    """
    if skeleton is None:
        return
    declared = {"%s%s" % (BANK_SKELETON_PREFIX, name) for name in partition.get("banks", {})}
    if not declared:
        return
    # The binding resolves each TSoftObjectPtr to the USkeleton itself, or to None when the
    # reference no longer points at an asset -- which is the interesting failure, so it counts as
    # missing rather than being skipped.
    carried = {entry.get_name()
               for entry in skeleton.get_editor_property("compatible_skeletons")
               if entry is not None}
    missing = sorted(declared - carried)
    if missing:
        errors.append("%s: skeleton declares %d of %d bank skeleton(s) compatible, missing %s"
                      % (stem, len(declared) - len(missing), len(declared),
                         ", ".join(missing[:4])))


def verify_mount_inventory(partition, errors):
    """Nothing on the mount outside the declared partition.

    Only sound against a COMPLETE run: the folders are flat and shared, so a slice cannot tell an
    asset another body owns from an orphan. The caller decides; this assumes it already did.
    """
    stems = set(partition.get("models", {}))
    banks = set(partition.get("banks", {}))
    declared_skeletons = {"%s%s" % (SKELETON_PREFIX, name) for name in stems}
    declared_skeletons |= {"%s%s" % (BANK_SKELETON_PREFIX, name) for name in banks}
    for data in assets_under(CHARACTERS + "/Skeletons"):
        name = str(data.asset_name)
        if name not in declared_skeletons:
            errors.append("Skeletons/%s: on the mount and not in the declared partition" % name)

    declared_dirs = stems | {BANKS_FOLDER}
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    seen = set()
    for data in registry.get_assets_by_path(ANIMS, recursive=True):
        rel = str(data.package_path)[len(ANIMS) + 1:]
        if not rel:
            continue
        seen.add(rel.split("/")[0])
    for directory in sorted(seen - declared_dirs):
        errors.append("Anims/%s: neither a bank folder nor a body the declared partition names"
                      % directory)


def verify_mesh(stem, manifest, container_path, errors):
    """Returns the stem read off the skeleton the baked body points at, or "" when it is not baked.

    A body owns its skeleton, so that stem is the body's own -- which makes a mesh bound to another
    body's skeleton directly readable off what landed rather than inferred from a missing asset.
    """
    asset = "SK_%s" % stem
    path = "%s/%s.%s" % (MESHES, asset, asset)
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if mesh is None:
        errors.append("%s: not baked" % asset)
        return ""
    container = eskm.read(container_path)
    albedo = eskm.materials(container)

    # Nothing skins to a wield mount, so a build that keeps the bones its weights need keeps a body
    # that renders identically and has nowhere to hang a weapon.
    mounts, _per_clip = declared_mounts(container_path, container)
    if mounts:
        carried = {str(name).lower()
                   for name in unreal.ElysiumCharacterBakeLibrary.mesh_bones(mesh)}
        missing = [name for name in mounts if name.lower() not in carried]
        if missing:
            errors.append("%s: %d of %d wield-mount bone(s) are not on the built mesh (%s)"
                          % (asset, len(missing), len(mounts), ", ".join(missing)))

    # The stem is read off what landed rather than recomputed, so the check cannot agree with the
    # bake by sharing its arithmetic: whatever skeleton the mesh actually points at is the one whose
    # clips have to exist.
    skeleton = mesh.get_editor_property("skeleton")
    baked_stem = ""
    if skeleton is None:
        errors.append("%s: has no skeleton" % asset)
    elif not skeleton.get_name().startswith(SKELETON_PREFIX):
        errors.append("%s: skeleton '%s' is not a baked body skeleton"
                      % (asset, skeleton.get_name()))
    else:
        baked_stem = skeleton.get_name()[len(SKELETON_PREFIX):]

    # A slot left holding a dynamic instance serialises as null and the section draws with the
    # engine's default material -- grey, and nothing logged.
    materials = mesh.get_editor_property("materials")
    if not materials:
        errors.append("%s: no material slots" % asset)
    for slot in materials:
        if slot.material_interface is None:
            errors.append("%s: slot '%s' is unbound" % (asset, slot.material_slot_name))
            continue
        # A material whose .glb declares an albedo must have one. This is the check that catches a
        # texture package that never reached disk: the instance still has its slot, its parent and
        # its factors, so nothing else about it looks wrong.
        material = str(slot.material_slot_name)
        if albedo.get(material) and not unreal.ElysiumCharacterBakeLibrary.material_has_texture(
                slot.material_interface):
            errors.append("%s: slot '%s' should carry %s and carries no texture"
                          % (asset, slot.material_slot_name, albedo[material]))

    record = manifest["npcs"].get(stem, {})
    expected_morphs = int(record.get("morphs", 0) or 0)
    morphs = mesh.get_editor_property("morph_targets")
    if expected_morphs and len(morphs) == 0:
        errors.append("%s: %d morph targets expected, none baked" % (asset, expected_morphs))
    if morphs:
        # The curve metadata is what puts a morph-target curve into ActiveMorphTargets. Without it
        # the facial rig writes the right weights onto a face that cannot receive them.
        missing = [m.get_name() for m in morphs
                   if not skeleton or not unreal.ElysiumCharacterBakeLibrary.skeleton_has_morph_curve(
                       skeleton, m.get_name())]
        if missing:
            errors.append("%s: %d morph target(s) carry no curve metadata (%s)"
                          % (asset, len(missing), ", ".join(sorted(missing)[:4])))
    return baked_stem


def verify_prop(stem, manifest, errors):
    """Verify one independently baked placed model without traversing the cast."""
    record = manifest.get("placed_models", {}).get(stem)
    if record is None:
        errors.append("prop %s: not in the manifest" % stem)
        return
    package = "%s/%s" % (PROPS, stem)
    skeleton_name = "SKEL_%s" % stem
    mesh_name = "SK_%s" % stem
    skeleton = unreal.EditorAssetLibrary.load_asset(
        "%s/%s.%s" % (package, skeleton_name, skeleton_name))
    mesh = unreal.EditorAssetLibrary.load_asset("%s/%s.%s" % (package, mesh_name, mesh_name))
    if skeleton is None:
        errors.append("prop %s: skeleton is missing" % stem)
    if mesh is None:
        errors.append("prop %s: skeletal mesh is missing" % stem)
    elif mesh.get_editor_property("skeleton") != skeleton:
        errors.append("prop %s: mesh does not use its declared skeleton" % stem)
    elif any(slot.material_interface is None for slot in mesh.get_editor_property("materials")):
        errors.append("prop %s: one or more material slots are unbound" % stem)

    baked = {str(data.asset_name) for data in assets_under(package)
             if str(data.asset_name).startswith("A_")}
    declared = {"A_" + unreal.ElysiumCharacterBakeLibrary.baked_asset_name(label)
                for label in record.get("clips", {})}

    def has_derived(name):
        prefix = name + "_"
        return any(other.startswith(prefix) and other not in declared
                   and "A_" + other[len(prefix):] in declared for other in baked)

    missing = []
    for label, meta in record.get("clips", {}).items():
        name = "A_" + unreal.ElysiumCharacterBakeLibrary.baked_asset_name(label)
        flags = int(meta.get("flags", 0))
        # A `_delta` is NOT exempt: it ships from its raw record under its plain label, so a
        # missing one is a hole rather than a form that lives elsewhere.
        if name not in baked and not has_derived(name):
            missing.append(label)
    if missing:
        errors.append("prop %s: %d clip(s) missing (%s)"
                      % (stem, len(missing), ", ".join(sorted(missing)[:4])))
    else:
        log("prop '%s': mesh, skeleton and %d declared clip(s) verified"
            % (stem, len(record.get("clips", {}))))


def verify_prop_bone_tracks(owner, package, clips, baked, container_path, errors):
    """A baked base clip carries every wield-mount channel its container wrote.

    Checked on ONE base clip per container, and that is the whole set rather than a sample: the
    bake decides which bones a container's clips bind once, for the whole container, so a container
    that kept the mounts on one pose kept them on all of them and one that dropped them dropped them
    everywhere. A container declaring no mount -- a non-biped rig -- has nothing to answer and is
    skipped, which is what lets every family be asked the same question.

    An additive is not eligible: a bone with no track on one evaluates to the additive identity
    rather than to the bind, so its track set answers a different question. The clip chosen is the
    one whose own channels cover the most mounts, because a masked overlay states fewer by design.
    """
    mounts, per_clip = declared_mounts(container_path)
    if not mounts:
        return
    label, expected = "", []
    for candidate in sorted(clips):
        if clips[candidate] & 0x4:
            continue
        asset = "A_" + unreal.ElysiumCharacterBakeLibrary.baked_asset_name(candidate)
        if asset not in baked:
            continue
        carried = per_clip.get(candidate, ())
        if len(carried) > len(expected):
            label, expected = candidate, carried
        if len(expected) == len(mounts):
            break
    if not expected:
        # No base clip landed under its plain label, or every one of them is a masked overlay
        # owning no mount. `verify_clips` already reports a container with nothing baked, so this
        # is a container the check cannot speak for rather than a failure of it.
        log("%s: no base clip states a wield-mount channel to verify" % owner)
        return
    name = "A_" + unreal.ElysiumCharacterBakeLibrary.baked_asset_name(label)
    sequence = unreal.EditorAssetLibrary.load_asset("%s/%s.%s" % (package, name, name))
    tracked = {str(bone).lower()
               for bone in unreal.ElysiumCharacterBakeLibrary.sequence_track_bones(sequence)}
    missing = [bone for bone in expected if bone.lower() not in tracked]
    if missing:
        errors.append("%s: baked '%s' carries no channel for %d of the %d wield mount(s) the "
                      "container writes for it (%s) -- a weapon on one of those bones would hold "
                      "the bind pose through the whole clip"
                      % (owner, label, len(missing), len(expected), ", ".join(missing)))


def verify_clips(package, owner, clips, container_path, errors):
    """Every clip `owner` declares, against what landed under `package`."""
    baked = {}
    for data in assets_under(package):
        baked[str(data.asset_name)] = data
    if not baked:
        errors.append("%s: no sequences baked under %s" % (owner, package))
        return 0

    # A clip the container binds to a host ships as `<clip>@<host>`, one per declaring host,
    # because writing it needs that host's pose. The plain label is then deliberately absent:
    # a delta has no base to be a difference from without one, and a masked overlay owning the
    # split bone has no chain to express that bone's rotation against. Retail only ever reaches
    # either through the same binding, so the derived forms are the whole story.
    # The separator survives asset naming as an underscore, which an ordinary label also contains,
    # so a prefix match alone cannot tell `leap@ascend` from the unrelated label `leap_ascend`.
    # 352 of the 2,494 shipped labels are shadowed that way, and every one of them was a clip that
    # could go missing and still pass. A candidate is only a derived form when it is NOT itself a
    # declared label, and when what follows the prefix IS one -- the host.
    baked_names = sorted(baked)
    declared = {"A_" + unreal.ElysiumCharacterBakeLibrary.baked_asset_name(label)
                for label in clips}

    def has_derived(name):
        prefix = name + "_"
        for other in baked_names:
            if not other.startswith(prefix) or other in declared:
                continue
            host = "A_" + other[len(prefix):]
            if host in declared:
                return True
        return False

    additive_expected = 0
    additive_found = 0
    for label, flags in sorted(clips.items()):
        name = "A_" + unreal.ElysiumCharacterBakeLibrary.baked_asset_name(label)
        if name not in baked:
            # Deliberately absent when the clip ships only in derived `<clip>@<host>` form, which
            # is what an aim grid's cells do. A `_delta` is NOT one of those: it ships from its raw
            # record under its plain label, because retail's post-multiply is not a conversion any
            # base could carry, so a missing one is a hole rather than a form.
            if has_derived(name):
                continue
            errors.append("%s: clip '%s' is missing" % (owner, label))
            continue
        if not flags & 0x4:
            continue
        additive_expected += 1
        sequence = unreal.EditorAssetLibrary.load_asset("%s/%s.%s" % (package, name, name))
        if sequence is None:
            errors.append("%s: clip '%s' does not load" % (owner, label))
            continue
        # Two halves, and each is silent alone. The tag is the ONLY thing that tells a reader this
        # clip is a difference rather than a pose -- every runtime resolver keys on it -- and the
        # engine's own additive stamp must stay OFF, because it would make the compressor subtract
        # a base out of keys that already are the difference.
        tagged = any(meta is not None
                     and meta.get_class().get_name() == "ElysiumAnimPostAdditive"
                     for meta in (sequence.get_editor_property("meta_data") or []))
        stamped = (sequence.get_editor_property("additive_anim_type")
                   != unreal.AdditiveAnimationType.AAT_NONE)
        if not tagged:
            errors.append(
                "%s: '%s' carries the delta flag and is untagged, so every reader composes it as "
                "a pose" % (owner, label))
        if stamped:
            errors.append(
                "%s: '%s' carries an additive stamp, which subtracts a base out of keys that are "
                "already the difference" % (owner, label))
        if tagged and not stamped:
            additive_found += 1
    if additive_expected:
        log("%s: %d/%d delta sequences tagged post-additive"
            % (owner, additive_found, additive_expected))
    verify_prop_bone_tracks(owner, package, clips, baked, container_path, errors)
    return len(baked)


def cell_is_baked(package, grid):
    """Whether this grid's cells exist under their plain labels.

    Absent means the cells ship only in derived `<cell>@<host>` form, which is what an aim grid's
    do -- they own the split bone, so the chain their rotation is expressed against comes from a
    host rather than from the file."""
    for cell in grid.get("cells", ()):
        clip = cell.get("clip") if isinstance(cell, dict) else cell
        if not clip:
            continue
        name = "A_" + unreal.ElysiumCharacterBakeLibrary.baked_asset_name(clip)
        return unreal.EditorAssetLibrary.does_asset_exist("%s/%s.%s" % (package, name, name))
    return False


def verify_blend_spaces(package, owner, blends, errors):
    """Every grid the sidecar declares has a BS_ asset under `package`, and it carries its samples.

    A blend space that lost its samples is the failure worth catching here: it loads, it lists, and
    it poses nothing -- the same shape as a sequence that lost its compressed data. The bake refuses
    to write one, so reaching this is a sign the asset did not survive the save."""
    with open(os.path.join(NPC_DIR, *blends.split("/")), "r", encoding="utf-8") as handle:
        sidecar = json.load(handle)
    grids = sidecar.get("grids", {})

    # A grid the autolayer table binds to hosts ships one blend space PER host, over that host's
    # own cells -- an aim grid's cells own the split bone, so there is no host-free form of them
    # to sample. Every other grid ships once under its plain label.
    hosts_by_target = {}
    for host, targets in sidecar.get("autolayers", {}).items():
        for target in targets:
            hosts_by_target.setdefault(target, []).append(host)

    found = 0
    for label, grid in sorted(grids.items()):
        # Single-cell grids are not blend spaces and neither the exporter nor the bake writes one.
        if len(grid.get("cells", ())) < 2:
            continue
        hosts = sorted(hosts_by_target.get(label, ()))
        if not hosts and not cell_is_baked(package, grid):
            # An aim grid no host declares. Its cells own the split bone, so they ship only in
            # derived form against a host -- and it has none, which leaves no correct form of
            # either the cells or the grid. Orphan content the model carries and never reaches,
            # the same case as an additive no host declares.
            log("%s: grid '%s' is declared by no host and is not built" % (owner, label))
            continue
        wanted = [label + "@" + h for h in hosts] or [label]
        missing, loaded = [], []
        for want in wanted:
            name = "BS_" + unreal.ElysiumCharacterBakeLibrary.baked_asset_name(want)
            space = unreal.EditorAssetLibrary.load_asset("%s/%s.%s" % (package, name, name))
            if space is None:
                missing.append(want)
            else:
                loaded.append((want, space))
        if missing:
            errors.append("%s: blend grid '%s' has no baked blend space (%d of %d form(s): %s)"
                          % (owner, label, len(missing), len(wanted), ", ".join(missing[:3])))
            continue
        # Every form is checked, not just one: a blend space that loads, lists its samples and
        # poses nothing is the failure this exists to catch, and it can happen to one host's
        # form alone.
        empty = [w for w, s in loaded if not s.get_editor_property("sample_data")]
        if empty:
            errors.append("%s: blend space(s) '%s' carry no samples and would pose nothing"
                          % (owner, ", ".join(empty[:3])))
            continue
        found += len(loaded)
    if found:
        log("%s: %d blend space(s)" % (owner, found))
    return found


def main():
    stems = [s for s in cmdline_arg("BakeCharacters").split(",") if s]
    prop_stems = [s for s in cmdline_arg("BakeProps").split(",") if s]
    if not stems and not prop_stems:
        raise SystemExit(
            "[chars-verify] no -BakeCharacters=<csv> or -BakeProps=<csv> given")

    with open(os.path.join(NPC_DIR, "npc_manifest.json"), "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    partition = read_partition()
    declared_stems = set(partition.get("model_family_of", {}))

    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([MOUNT],
                                                                           force_rescan=True)
    errors = []
    for stem in prop_stems:
        verify_prop(stem, manifest, errors)
    # {stem: {clip label: flags}}. Keyed by the stem each baked BODY's own skeleton reports, so a
    # model whose mesh landed on another body's skeleton shows up as missing sequences rather than
    # passing quietly.
    wanted = {}
    # {owner: blends sidecar path relative to npc/}. A grid is declared by the model that owns the
    # clips.
    gridded = {}
    # {bank owner: {clip label: flags}}. Banks are baked ONCE, under `_banks`, and reached by every
    # body through its skeleton's compatibility declaration -- so they are checked once here rather
    # than once per body that includes them.
    bank_wanted = {}

    for stem in stems:
        record = manifest["npcs"].get(stem)
        if record is None:
            errors.append("%s: not in the manifest" % stem)
            continue
        baked_stem = verify_mesh(stem, manifest, os.path.join(NPC_DIR, stem + ".eskm"), errors)
        if not baked_stem:
            continue

        # The stem read off the baked body's own skeleton against the body being verified. A body
        # owns its skeleton, so these disagree exactly when a mesh landed on ANOTHER body's rig --
        # which addresses every clip and every mask through a name nothing else points at, and is
        # how a whole second copy of a cast member appears under a second name.
        if baked_stem != stem:
            errors.append("%s: mesh is bound to '%s%s', not to its own skeleton"
                          % (stem, SKELETON_PREFIX, baked_stem))

        clips = wanted.setdefault(stem, {})
        own = {label: int(meta.get("flags", 0))
               for label, meta in record.get("own_clips", {}).items()}
        if own:
            clips.update(own)
            if record.get("blends"):
                gridded[stem] = record["blends"]
        # A label names every bank that DECLARES it, so a clip entry is a LIST of owners rather
        # than one -- flattened here as `character_source_plan` flattens it. Read as a scalar the
        # list reached `in bank_wanted` and raised `TypeError: unhashable type: 'list'` on the
        # first body, before a single asset had been verified.
        for _label, declared in record.get("clips", {}).items():
            for bank in (declared if isinstance(declared, list) else [declared]):
                if bank == stem or bank in bank_wanted:
                    continue
                bank_record = manifest["banks"].get(bank, {})
                bank_wanted[bank] = {label: int(meta.get("flags", 0))
                                     for label, meta in bank_record.get("clips", {}).items()}
                if bank_record.get("blends"):
                    gridded[bank] = bank_record["blends"]

    total = 0
    spaces = 0
    for stem, clips in sorted(wanted.items()):
        skeleton = unreal.EditorAssetLibrary.load_asset(
            "%s/Skeletons/%s%s.%s%s" % (CHARACTERS, SKELETON_PREFIX, stem,
                                        SKELETON_PREFIX, stem))
        bones = unreal.ElysiumCharacterBakeLibrary.skeleton_bone_count(skeleton)
        log("body '%s': %d bones, %d own clip(s)" % (stem, bones, len(clips)))
        verify_declared_compat(partition, stem, skeleton, errors)
        # A body that owns no clip has no folder of its own; the banks it plays carry the lot.
        if not clips:
            continue
        package = "%s/%s" % (ANIMS, stem)
        total += verify_clips(package, stem, clips,
                              os.path.join(NPC_DIR, stem + ".eskm"), errors)
        if stem in gridded:
            spaces += verify_blend_spaces(package, stem, gridded[stem], errors)

    log("%d bank(s) shared across the cast" % len(bank_wanted))
    for owner, clips in sorted(bank_wanted.items()):
        package = "%s/%s/%s" % (ANIMS, BANKS_FOLDER, owner)
        total += verify_clips(package, owner, clips,
                              os.path.join(NPC_DIR, "banks", owner + ".eskm"), errors)
        if owner in gridded:
            spaces += verify_blend_spaces(package, owner, gridded[owner], errors)
    log("%d sequences, %d blend spaces over %d bod%s"
        % (total, spaces, len(wanted), "y" if len(wanted) == 1 else "ies"))

    # Same rule as the sweep: an inventory check is only sound against a complete run, because
    # Skeletons/ and Anims/ are flat folders the whole cast shares.
    if set(stems) >= declared_stems:
        verify_mount_inventory(partition, errors)
    else:
        log("skipping the mount inventory: %d of %d declared model(s) in this slice"
            % (len(stems), len(declared_stems)))

    for error in errors:
        unreal.log_error("[chars-verify] %s" % error)
    if errors:
        raise SystemExit("[chars-verify] %d problem(s)" % len(errors))
    log("ok")


main()
