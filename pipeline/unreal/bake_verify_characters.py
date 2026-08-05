# Verifies what pipeline/unreal/bake_characters.py wrote onto /ElysiumBaked/Characters.
#
# Reads the asset registry and the built assets themselves rather than re-deriving them from the
# export, so it answers "is what landed usable" rather than "did the bake believe it succeeded".
# Every check here is one that fails silently in game: a mesh whose material slot lost its instance
# renders untextured, a sequence that lost its compressed data plays as a rest pose, and a face with
# no morph-target curve metadata evaluates its facial track correctly and moves nothing.
#
# Internal editor worker coordinated by `uv run elysium export characters`:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript
#       -script="pipeline/unreal/bake_verify_characters.py" -BakeCharacters=<csv>
import json
import os

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from elysium_pipeline.formats import eskm  # noqa: E402
from elysium_pipeline.paths import export_root  # noqa: E402

MOUNT = "/ElysiumBaked"
CHARACTERS = MOUNT + "/Characters"
SKELETON_PREFIX = "SKEL_Elysium_"
MESHES = CHARACTERS + "/Meshes"
ANIMS = CHARACTERS + "/Anims"

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


def verify_mesh(stem, manifest, albedo, errors):
    """Returns the rig family the baked body belongs to, or "" when it is not baked."""
    asset = "SK_%s" % stem
    path = "%s/%s.%s" % (MESHES, asset, asset)
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if mesh is None:
        errors.append("%s: not baked" % asset)
        return ""

    # The family is read off what landed rather than recomputed, so the check cannot agree with the
    # bake by sharing its arithmetic: whatever skeleton the mesh actually points at is the one whose
    # clips have to exist.
    skeleton = mesh.get_editor_property("skeleton")
    family = ""
    if skeleton is None:
        errors.append("%s: has no skeleton" % asset)
    elif not skeleton.get_name().startswith(SKELETON_PREFIX):
        errors.append("%s: skeleton '%s' is not a baked rig family" % (asset, skeleton.get_name()))
    else:
        family = skeleton.get_name()[len(SKELETON_PREFIX):]

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
    return family


def verify_clips(family, owner, clips, errors):
    package = "%s/%s/%s" % (ANIMS, family, owner)
    baked = {}
    for data in assets_under(package):
        baked[str(data.asset_name)] = data
    if not baked:
        errors.append("%s/%s: no sequences baked" % (family, owner))
        return 0

    additive_expected = 0
    additive_found = 0
    for label, flags in sorted(clips.items()):
        name = "A_" + unreal.ElysiumCharacterBakeLibrary.baked_asset_name(label)
        if name not in baked:
            errors.append("%s: clip '%s' is missing" % (owner, label))
            continue
        if not flags & 0x4:
            continue
        additive_expected += 1
        sequence = unreal.EditorAssetLibrary.load_asset("%s/%s.%s" % (package, name, name))
        if sequence is None:
            errors.append("%s: clip '%s' does not load" % (owner, label))
            continue
        if sequence.get_editor_property("additive_anim_type") == unreal.AdditiveAnimationType.AAT_NONE:
            errors.append("%s: '%s' carries the delta flag but baked non-additive" % (owner, label))
        else:
            additive_found += 1
    if additive_expected:
        log("%s/%s: %d/%d delta sequences additive"
            % (family, owner, additive_found, additive_expected))
    return len(baked)


def main():
    stems = [s for s in cmdline_arg("BakeCharacters").split(",") if s]
    if not stems:
        raise SystemExit("[chars-verify] no -BakeCharacters=<csv> given")

    with open(os.path.join(NPC_DIR, "npc_manifest.json"), "r", encoding="utf-8") as handle:
        manifest = json.load(handle)

    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([MOUNT],
                                                                           force_rescan=True)
    errors = []
    # {family: {owner: {clip label: flags}}}. Built off the family each baked BODY reports, so a
    # model whose mesh landed on one skeleton while its clips were written under another name shows
    # up as missing sequences rather than passing quietly.
    wanted = {}

    for stem in stems:
        record = manifest["npcs"].get(stem)
        if record is None:
            errors.append("%s: not in the manifest" % stem)
            continue
        albedo = eskm.materials(eskm.read(os.path.join(NPC_DIR, stem + ".eskm")))
        family = verify_mesh(stem, manifest, albedo, errors)
        if not family:
            continue

        owners = wanted.setdefault(family, {})
        own = {label: int(meta.get("flags", 0))
               for label, meta in record.get("own_clips", {}).items()}
        if own:
            owners.setdefault(stem, {}).update(own)
        for _label, bank in record.get("clips", {}).items():
            if bank == stem or bank in owners:
                continue
            bank_record = manifest["banks"].get(bank, {})
            owners[bank] = {label: int(meta.get("flags", 0))
                            for label, meta in bank_record.get("clips", {}).items()}

    total = 0
    for family, owners in sorted(wanted.items()):
        skeleton = unreal.EditorAssetLibrary.load_asset(
            "%s/Skeletons/%s%s.%s%s" % (CHARACTERS, SKELETON_PREFIX, family,
                                        SKELETON_PREFIX, family))
        bones = unreal.ElysiumCharacterBakeLibrary.skeleton_bone_count(skeleton)
        log("family '%s': %d bones, %d owners" % (family, bones, len(owners)))
        for owner, clips in sorted(owners.items()):
            total += verify_clips(family, owner, clips, errors)
    log("%d sequences over %d famil%s"
        % (total, len(wanted), "y" if len(wanted) == 1 else "ies"))

    for error in errors:
        unreal.log_error("[chars-verify] %s" % error)
    if errors:
        raise SystemExit("[chars-verify] %d problem(s)" % len(errors))
    log("ok")


main()
