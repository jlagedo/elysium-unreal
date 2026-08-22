# Bakes VtMB wield models into real Unreal assets under the /ElysiumBaked mount
# (docs/architecture/wielded-weapon-integration.md).
#
# Each of the 67 real wield models gets its own private skeleton and skeletal mesh -- the same
# private-rig shape `bake_characters.bake_props` already uses for an animated placed model, and for
# the same reason: a wield model shares no bone tree with the cast, so it never enters a rig family
# and never merges with anything. Unlike a prop, a wield model DOES need real textures and material
# instances -- it is worn in a hand, not dropped into an already-materialed map -- so this worker
# also imports the corpus's own texture set and binds per-slot material instances the way
# `bake_characters.bake_bodies` does for a body.
#
# Everything this reads is the offline exporter's `items/wield_models.json` manifest and the
# `.eskm` containers it names; every asset it writes is built through the engine's own authoring
# path in C++ (UElysiumSkeletalBuildLibrary). This worker also writes
# `/ElysiumBaked/Items/DA_WieldModels`, the table `(classname, sex)` resolves through at runtime --
# filled for every manifest classname on every run, whether or not this run baked its stem, because
# a row's soft path is deterministic from the package layout alone.
#
# The output is derived from the user's own VtMB install, so it is gitignored and regenerable
# exactly like $ELYSIUM_EXPORT_ROOT -- only the .uplugin mount descriptor is committed.
#
# Internal editor worker coordinated by `uv run elysium export bundle items`:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript
#       -script="pipeline/unreal/bake_wield.py"
#       -BakeWield=w_m_katana,sheriff_sword -unattended -nosplash -nopause
import json
import os

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402
from elysium_pipeline import asset_names, wield_corpus as wc  # noqa: E402
from elysium_pipeline.formats import eskm  # noqa: E402
from elysium_pipeline.paths import export_root  # noqa: E402

MOUNT = "/ElysiumBaked"
ITEMS = MOUNT + "/Items"
#: "/ElysiumBaked/Items/Wield" -- the immutable contract `wield_corpus.py` states for both halves.
WIELD = wc.BAKED_ROOT
TEXTURES = WIELD + "/Textures"
DA_PACKAGE = ITEMS
DA_NAME = "DA_WieldModels"

#: The wield master family -- FROZEN contract, `docs/architecture/wielded-weapon-integration.md`:
#: package `/Game/VtMB/Materials`, one master per blend mode, texture parameters `Albedo`/`Normal`/
#: `EnvMask` on all four (a `EnvStrength` scalar may also exist -- this file only ever sets a
#: parameter it has enumerated on the selected master, never assumes one). Every wield section and
#: skin-family override is instanced from whichever of these its own manifest flags select.
WIELD_MATERIALS = "/Game/VtMB/Materials"
MAKE_WIELD_MASTERS = "pipeline/unreal/make_wield_materials.py"

#: `flags` (from `wield_corpus.MATERIAL_FLAG_FIELDS`) that pick a variant master over the opaque
#: default, checked in this order. A row may carry more than one; the more specialised blend mode
#: wins, mirroring the single-branch precedence a real shader would apply.
MASTER_BY_FLAG = (
    ("additive", "M_Wield_Additive"),
    ("translucent", "M_Wield_Translucent"),
    ("alphatest", "M_Wield_Masked"),
)
DEFAULT_WIELD_MASTER = "M_Wield"


def master_path(name):
    return "%s/%s.%s" % (WIELD_MATERIALS, name, name)


def master_for(flags):
    """The wield master name one material row's own `flags` set selects."""
    flags = set(flags or ())
    for flag_name, master_name in MASTER_BY_FLAG:
        if flag_name in flags:
            return master_name
    return DEFAULT_WIELD_MASTER


_MASTER_CACHE = {}


def load_master(name):
    """The loaded wield master `name`, cached for the run.

    Fails loudly rather than falling back to anything else: a missing master means
    `MAKE_WIELD_MASTERS` has not been run yet against this mount, which is a prerequisite this
    file cannot repair for itself.
    """
    if name not in _MASTER_CACHE:
        path = master_path(name)
        master = unreal.EditorAssetLibrary.load_asset(path)
        if master is None:
            raise SystemExit("[wield] %s is missing -- run %s first" % (path, MAKE_WIELD_MASTERS))
        _MASTER_CACHE[name] = master
    return _MASTER_CACHE[name]


_MASTER_PARAMS = {}


def master_params(name):
    """({texture param name}, {scalar param name}) for wield master `name`, logged once per run.

    The owner reads this beside every per-row gap `log_master_gap` logs, to judge whether the
    selected master can already express what a manifest row wants.
    """
    if name not in _MASTER_PARAMS:
        master = load_master(name)
        texture_params = {str(p) for p in unreal.MaterialEditingLibrary.get_texture_parameter_names(master)}
        scalar_params = {str(p) for p in unreal.MaterialEditingLibrary.get_scalar_parameter_names(master)}
        log("material probe: %s texture param(s) %s, scalar param(s) %s"
            % (name, sorted(texture_params), sorted(scalar_params)))
        _MASTER_PARAMS[name] = (texture_params, scalar_params)
    return _MASTER_PARAMS[name]


OUT_ROOT = os.fspath(export_root())
WIELD_DIR = os.fspath(wc.wield_dir(OUT_ROOT))

#: Bone tolerances for the verify pass. Stated directly per the task rather than reused from
#: `wield_corpus.POS_EPS`, which is in INCHES for the offline export-side checks; `.eskm` values and
#: `RefPoseBoneTransform`'s output are already Unreal-native centimetres.
BONE_POS_EPS_CM = 1e-3
BONE_ROT_EPS_DEG = 0.05

#: The manifest's binding string -> `EElysiumWieldBinding` Python enumerator name. UE's Python
#: generator upper-snake-cases a C++ enumerator with no shared prefix (`SocketProp` -> `SOCKET_PROP`);
#: this is unverified against a live editor and is called out in the bake's own report.
BINDING_ENUM = {
    "socket_prop": "SOCKET_PROP",
    "socket_hand": "SOCKET_HAND",
    "leader_pose": "LEADER_POSE",
    "copy_pose": "COPY_POSE",
    "projectile": "PROJECTILE",
}

texture_asset_name = asset_names.texture_asset_name


def log(msg):
    unreal.log("[wield] %s" % msg)


def fail(msg):
    unreal.log_error("[wield] %s" % msg)


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line."""
    needle = "-%s=" % key
    for token in unreal.SystemLibrary.get_command_line().split():
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def release_packages():
    """Drop the packages this pass saved. Mirrors `bake_characters.release_packages`, scoped to
    the wield mount rather than the cast's."""
    released = unreal.ElysiumSkeletalBuildLibrary.release_baked_packages(WIELD)
    if released:
        log("released %d package(s)" % released)


def read_manifest():
    path = wc.manifest_path(OUT_ROOT)
    if not path.is_file():
        raise SystemExit("[wield] %s is missing (%s)" % (path, wc.REGENERATE))
    with open(path, "r", encoding="utf-8-sig") as handle:
        document = json.load(handle)
    if document.get("schema") != wc.MANIFEST_SCHEMA:
        raise SystemExit("[wield] %s is not a wield-models manifest" % path)
    return document


def selected_stems(models, arg):
    """The stems this run bakes: every csv entry, or every real model when the arg is empty."""
    if not arg:
        return sorted(models)
    wanted = sorted({s for s in arg.split(",") if s})
    unknown = [s for s in wanted if s not in models]
    if unknown:
        raise SystemExit(
            "[wield] not a real model in the manifest: %s (%s)" % (", ".join(unknown), wc.REGENERATE))
    return wanted


def check_no_stem_collisions(models):
    """Refuse to proceed if two stems would fold onto the same baked package path.

    Cheap and structural: every stem's package is `wc.baked_skeletal(stem)`, and a bake that let two
    stems collide there would have the second silently overwrite the first with no diagnostic beyond
    a mismatched clip count. The 67-stem corpus is already filesystem-safe today; this is the tripwire
    for the day it is not."""
    seen = {}
    collisions = []
    for stem in sorted(models):
        package = wc.baked_skeletal(stem)
        if package in seen:
            collisions.append("%s and %s both fold to %s" % (seen[package], stem, package))
        else:
            seen[package] = stem
    if collisions:
        raise SystemExit("[wield] duplicate baked package path(s): %s" % "; ".join(collisions))


# ---------------------------------------------------------------------------- stale sweep


def stale_sweep(manifest):
    """Delete every stem folder below WIELD whose stem the manifest no longer names.

    Wield packages are private per stem -- SK_, SKEL_ and every MI_ instance for one model live
    together under its own folder -- so this sweeps whole subfolders rather than pruning names out
    of one shared package the way `bake_lib.prune_package` does for a corpus pool. Runs before any
    save this pass makes, like the sibling bakes' pre-save sweep.
    """
    known = set(manifest.get("models", {}))
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    stems_on_mount = set()
    for data in registry.get_assets_by_path(WIELD, recursive=True):
        rel = str(data.package_path)[len(WIELD) + 1:]
        if not rel:
            continue
        top = rel.split("/")[0]
        if top != "Textures":
            stems_on_mount.add(top)
    stale = sorted(stems_on_mount - known)
    removed = 0
    for stem in stale:
        folder = "%s/%s" % (WIELD, stem)
        for path in unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False):
            bl.delete_owned_asset(path)
            removed += 1
        unreal.EditorAssetLibrary.delete_directory(folder)
        log("swept stale package %s (not in the manifest)" % folder)
    if removed:
        log("stale sweep: %d asset(s) removed across %d stem(s)" % (removed, len(stale)))
    return removed


# ---------------------------------------------------------------------------- textures


def existing_textures():
    """{asset name: Texture2D} already on the wield texture mount."""
    out = {}
    for path in unreal.EditorAssetLibrary.list_assets(TEXTURES, recursive=False, include_folder=False):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if asset is not None:
            out[asset.get_name()] = asset
    return out


def texture_closure(manifest, stems):
    """{raw texture key: role} over every selected stem's materials AND skin-family overrides.

    `role` is one of "albedo" (sRGB colour), "mask" (linear envmask) or "normal" (tangent bump),
    matching `bake_lib.configure_texture`'s roles. A key that is used as an albedo anywhere in the
    selection is always classified "albedo" -- the corpus's one instance of a key doing double duty
    (handleclaws' envmask equals its own albedo key) must decode sRGB, not linear, or the texture
    that IS bound looks wrong to save a texture that never gets bound at all.
    """
    roles = {}
    models = manifest["models"]
    for stem in stems:
        model = models[stem]
        rows = list(model.get("materials", ())) + list(model.get("skin_families", ()))
        for row in rows:
            for field, role in (("albedo", "albedo"), ("envmask", "mask"), ("bump", "normal")):
                key = row.get(field, "")
                if key and roles.get(key) != "albedo":
                    roles[key] = role
    return roles


def import_wield_textures(manifest, roles, failed, force=False):
    """Import every texture key `roles` names that is not already on the mount, once.

    Scoped to the selection's own closure rather than the manifest's whole 89-key union -- unlike
    the character cast's single shared textures.json, a wield-only run is expected to bake a handful
    of stems at a time, and importing the other 66 models' textures on a one-stem run would defeat
    the point of a scoped bake. Returns {asset name: Texture2D} over everything the mount now
    carries, which is what a mesh's material bindings resolve through -- exactly
    `bake_characters.import_texture_corpus`'s contract, scoped down.
    """
    table = manifest.get("textures", {})
    out = existing_textures()
    wanted = {}
    role_of = {}
    for key, role in sorted(roles.items()):
        uri = table.get(key)
        if not uri:
            fail("wield texture key %r is not in the manifest's texture table" % key)
            failed.append(key)
            continue
        name = texture_asset_name(uri)
        role_of[name] = role
        if name in out and not force:
            continue
        source = os.path.join(WIELD_DIR, uri.replace("/", os.sep))
        if os.path.isfile(source):
            wanted[name] = source
        else:
            fail("wield texture source missing: %s" % source)
            failed.append(name)
    if not wanted:
        log("textures: %d on the mount, none to import" % len(out))
        return out
    imported = bl.import_textures([(src, name) for name, src in sorted(wanted.items())], TEXTURES)
    saved = 0
    for name, texture in sorted(imported.items()):
        bl.configure_texture(texture, role_of.get(name, "albedo"))
        if bl.save("%s/%s" % (TEXTURES, name)):
            out[name] = texture
            saved += 1
        else:
            fail("could not save wield texture %s" % name)
            failed.append(name)
    for name in sorted(set(wanted) - set(imported)):
        fail("wield texture did not import: %s" % wanted[name])
        failed.append(name)
    log("textures: %d imported, %d saved, %d on the mount" % (len(imported), saved, len(out)))
    return out


# ---------------------------------------------------------------------------- materials


#: A material row's own flags that ARE addressed -- each selects the master's own blend mode
#: rather than needing a parameter, so it is never a gap on its own.
_VARIANT_FLAGS = frozenset(name for name, _master in MASTER_BY_FLAG)


def log_master_gap(stem, label, row):
    """Log what the row's SELECTED wield master cannot express for one material row, if anything.

    `additive`/`translucent`/`alphatest` pick the master's own blend mode and so are never a gap.
    `envmap`/`selfillum` select nothing -- no wield master parameter stands for either -- and are
    always logged when present. `envmask`/`bump` are logged only when the selected master turns
    out not to carry `EnvMask`/`Normal`; the FROZEN contract says every wield master does, so a hit
    here is a master/manifest drift, which is what this probe exists to catch.
    """
    flags = set(row.get("flags", ()))
    master_name = master_for(flags)
    texture_params, _scalar_params = master_params(master_name)
    gaps = sorted(flags - _VARIANT_FLAGS)
    if row.get("envmask") and "EnvMask" not in texture_params:
        gaps.append("envmask")
    if row.get("bump") and "Normal" not in texture_params:
        gaps.append("bump")
    if gaps:
        log("probe %s: %s carries %s -- %s has no such parameter"
            % (stem, label, ", ".join(gaps), master_name))


def resolve_texture_key(key, texture_table, textures):
    """The imported Texture2D for a manifest material row's raw texture key (albedo, envmask or
    bump), or None."""
    if not key:
        return None
    uri = texture_table.get(key, "")
    if not uri:
        return None
    return textures.get(texture_asset_name(uri))


#: Manifest field -> the FROZEN wield master parameter name it binds.
TEXTURE_FIELDS = (("albedo", "Albedo"), ("bump", "Normal"), ("envmask", "EnvMask"))


def wield_material_parents(stem, model, texture_table, textures):
    """({slot: wield master object path}, ok) for one model's PRIMARY (family-0) material set.

    Sourced from the manifest's own material rows rather than from `eskm.materials()` -- the
    manifest already carries the resolved texture key per slot, so there is no reason to re-derive
    it from the container's MATL section a second time. The handleclaws `null` material's empty
    albedo is the one known, expected gap and is logged as such rather than turning `ok` false; the
    caller appends `stem` to its own `failed` list exactly once when `ok` comes back false, so no
    failure bookkeeping happens in here.

    Only the PARENT is resolved here, for `BuildSkeletalMeshFromSource`'s `MaterialParents` -- the
    Albedo/Normal/EnvMask parameters themselves are bound afterward by `bind_slot_textures`, once
    the instance exists under that parent. The C++ builder's own texture bind writes
    `baseColorTexture`, BODY_MASTER's parameter name, which none of the four wield masters carry.
    """
    out = {}
    ok = True
    for row in model.get("materials", ()):
        name = row.get("name", "")
        log_master_gap(stem, "slot '%s'" % name, row)
        out[name] = master_path(master_for(row.get("flags", ())))
        albedo_key = row.get("albedo", "")
        if not albedo_key:
            if name.lower() == "null":
                log("%s: slot '%s' is the known handleclaws retail defect (empty .ttz) -- "
                    "binding no texture" % (stem, name))
            else:
                fail("%s: slot '%s' names no albedo and is not the known handleclaws defect"
                     % (stem, name))
                ok = False
            continue
        if resolve_texture_key(albedo_key, texture_table, textures) is None:
            fail("%s: slot '%s' albedo %r did not import" % (stem, name, albedo_key))
            ok = False
    return out, ok


def bind_row_textures(stem, describe, mic, master_name, row, texture_table, textures, failed):
    """Set every Albedo/Normal/EnvMask key `row` names onto `mic`, in the FROZEN parameter names.

    Only ever sets a parameter `master_params` has actually enumerated on the selected master --
    "only set parameters you can see the master has" -- rather than trusting the contract blindly;
    a key the master turns out not to carry was already reported once by `log_master_gap` and is
    silently skipped here rather than reported a second time. Returns (changed, ok): `changed` is
    whether any parameter was set (so the caller knows whether to save), `ok` is false when a named
    key failed to resolve to an imported texture (the caller appends `stem` to its own `failed`
    list exactly once per row, so no bookkeeping happens in here beyond that one `fail()` call).
    """
    texture_params, _scalar_params = master_params(master_name)
    changed = False
    ok = True
    for field, param in TEXTURE_FIELDS:
        key = row.get(field, "")
        if not key or param not in texture_params:
            continue
        asset = resolve_texture_key(key, texture_table, textures)
        if asset is None:
            fail("%s: %s %s %r did not import" % (stem, describe, field, key))
            failed.append(stem)
            ok = False
            continue
        bl.set_tex_param(mic, param, asset)
        changed = True
    return changed, ok


def bind_slot_textures(stem, mesh_asset_name, package, model, texture_table, textures, failed):
    """Bind Albedo/Normal/EnvMask on every PRIMARY slot instance, after
    `BuildSkeletalMeshFromSource` has created it parented to its wield master via
    `wield_material_parents`'s `MaterialParents` map.

    Instance names are derived exactly as `ElysiumSkeletalBuild.cpp`'s `MakeSectionMaterial`
    derives them (`asset_names.baked_asset_name` mirrors `FElysiumContentPaths::BakedAssetName`),
    so this resolves the very instance the C++ call just saved rather than creating a new one.
    """
    bound = 0
    for row in model.get("materials", ()):
        name = row.get("name", "")
        instance_name = "MI_%s_%s" % (
            asset_names.baked_asset_name(mesh_asset_name), asset_names.baked_asset_name(name))
        instance_path = "%s/%s" % (package, instance_name)
        mic = unreal.EditorAssetLibrary.load_asset(instance_path)
        if mic is None:
            fail("%s: slot instance %s did not build" % (stem, instance_path))
            failed.append(stem)
            continue
        master_name = master_for(row.get("flags", ()))
        changed, _ok = bind_row_textures(
            stem, "slot '%s'" % name, mic, master_name, row, texture_table, textures, failed)
        if changed:
            if bl.save(instance_path):
                bound += 1
            else:
                fail("%s: could not save %s" % (stem, instance_path))
                failed.append(stem)
    return bound


def build_skin_family_instances(stem, model, mesh_asset_name, package, texture_table, textures, failed):
    """One MaterialInstanceConstant per skin-family override row, saved beside the slot's own
    instance and never applied to the mesh -- `docs/architecture/wielded-weapon-integration.md`
    names `w_{m,f}_fire_axe` family 1 as the one shipped case. Parented to the wield master its own
    flags select, the same way the primary slots are."""
    built = 0
    for row in model.get("skin_families", ()):
        family = int(row.get("family", 0))
        slot = row.get("slot", "")
        log_master_gap(stem, "skin family %d slot '%s'" % (family, slot), row)
        instance_name = "MI_%s_%s_family%d" % (
            asset_names.baked_asset_name(mesh_asset_name), asset_names.baked_asset_name(slot), family)
        master_name = master_for(row.get("flags", ()))
        # `set_material_instance_parent` takes the loaded MaterialInterface, not a path string --
        # the C++ builder accepts a path for the slot instances, but this Python-side path does not.
        mic = bl.make_material_instance(instance_name, package, load_master(master_name))
        if mic is None:
            fail("%s: skin family %d instance '%s' could not be created" % (stem, family, instance_name))
            failed.append(stem)
            continue
        _changed, ok = bind_row_textures(
            stem, "skin family %d slot '%s'" % (family, slot), mic, master_name, row,
            texture_table, textures, failed)
        if not ok:
            continue
        if bl.save("%s/%s" % (package, instance_name)):
            built += 1
        else:
            fail("%s: could not save %s" % (stem, instance_name))
            failed.append(stem)
    return built


# ---------------------------------------------------------------------------- mesh + skeleton


def build_wield_model(stem, model, library, texture_table, textures, failed):
    """Build one model's private skeleton, skeletal mesh and material instances.

    Returns the mesh's object path on success, or None -- the caller appends `stem` to `failed`
    and moves on, per the sibling bakes' log-and-continue-per-model policy."""
    source_path = os.path.join(OUT_ROOT, model["eskm"].replace("/", os.sep))
    if not os.path.isfile(source_path):
        fail("no .eskm for wield model %s (%s)" % (stem, wc.REGENERATE))
        failed.append(stem)
        return None

    package = "%s/%s" % (WIELD, stem)
    bl.ensure_dir(package)
    skeleton_package = "%s/SKEL_%s" % (package, stem)
    error, bones = library.build_family_skeleton([source_path], skeleton_package, True)
    if error:
        fail("wield skeleton %s: %s" % (stem, error))
        failed.append(stem)
        return None
    # The built skeleton's bone count must equal the .eskm's own row count exactly -- a
    # multi-rooted model (`w_f_severed_arm` is the corpus's one case) resolves its fork onto one
    # of its own bones (`UE_mdl_skeletal.unreal_bones`) rather than gaining an extra synthetic
    # one, so the container's row count is always the manifest's StudioBone count.
    container_bones = len(eskm.bone_locals(eskm.read(source_path)))
    if bones != container_bones:
        fail("wield skeleton %s: built %d bones, the .eskm carries %d"
             % (stem, bones, container_bones))
        failed.append(stem)
        return None

    parents, ok = wield_material_parents(stem, model, texture_table, textures)
    if not ok:
        failed.append(stem)
        return None

    # `wc.skeletal_asset` (not a hand-rolled "SK_%s") so this name is byte-identical to what
    # `wield_model_ref` derives independently for the DA row's soft path -- both read
    # `wc.baked_skeletal`'s contract rather than restating "SK_" + stem twice.
    mesh_asset_name = wc.skeletal_asset(stem)
    mesh_package = "%s/%s" % (package, mesh_asset_name)
    # No MaterialTextures: the C++ builder only ever writes `baseColorTexture`, which none of the
    # four wield masters carry (their contract is Albedo/Normal/EnvMask) -- `bind_slot_textures`
    # binds those, in Python, once the per-slot instances this call creates actually exist. Every
    # material row resolves its own parent through `parents`, so the fallback
    # `master_path(DEFAULT_WIELD_MASTER)` only ever covers a section the manifest did not name.
    error = library.build_skeletal_mesh_from_source(
        source_path, mesh_package, skeleton_package,
        master_path(DEFAULT_WIELD_MASTER), package, {}, parents)
    if error:
        fail("%s: %s" % (mesh_asset_name, error))
        failed.append(stem)
        return None

    bound = bind_slot_textures(stem, mesh_asset_name, package, model, texture_table, textures, failed)
    families = build_skin_family_instances(
        stem, model, mesh_asset_name, package, texture_table, textures, failed)
    log("wield '%s': %d bones, %d material slot(s), %d texture bind(s)%s"
        % (stem, bones, len(model.get("materials", ())), bound,
           ", %d skin-family instance(s)" % families if families else ""))
    return "%s.%s" % (mesh_package, mesh_asset_name)


# ---------------------------------------------------------------------------- the data asset


def wield_model_ref(manifest, sex_entry, failed):
    """One `FElysiumWieldModelRef`, built for EVERY manifest classname regardless of whether this
    run baked its stem -- the soft path is deterministic from the package layout alone."""
    ref = unreal.ElysiumWieldModelRef()
    if sex_entry.get("kind") != "real":
        # The struct's own defaults are already the no-geometry answer: an unset Mesh, NAME_None on
        # both bones, Binding::None. Nothing to set.
        return ref
    stem = sex_entry.get("stem", "")
    model = manifest.get("models", {}).get(stem)
    if model is None:
        fail("row names stem '%s' as real, absent from the models table" % stem)
        failed.append(stem)
        return ref
    package = wc.baked_skeletal(stem)
    asset_name = package.rsplit("/", 1)[-1]
    # A bare SoftObjectPath does not nativize into a typed TSoftObjectPtr property in this
    # binding; Conv_SoftObjPathToSoftObjRef wraps the path WITHOUT loading the asset, which is
    # what keeps rows for not-yet-baked stems fillable.
    soft = unreal.SystemLibrary.conv_soft_obj_path_to_soft_obj_ref(
        unreal.SoftObjectPath("%s.%s" % (package, asset_name)))
    ref.set_editor_property("mesh", soft)
    binding = model.get("binding")
    enum_name = BINDING_ENUM.get(binding)
    if enum_name is None:
        fail("stem '%s' carries unknown binding %r" % (stem, binding))
        failed.append(stem)
        enum_name = "NONE"
    ref.set_editor_property("binding", getattr(unreal.ElysiumWieldBinding, enum_name))
    # NAME_None is FName("None"), not FName("") -- an empty python string would construct a
    # DIFFERENT, non-null FName, so an authored-empty mount/hand bone (leader_pose's w_{f,m}_claws)
    # is normalised to the literal "None" spelling here.
    ref.set_editor_property("mount_bone", model.get("mount_bone") or "None")
    ref.set_editor_property("hand_bone", model.get("hand_bone") or "None")
    return ref


def build_data_asset(manifest, failed):
    bl.ensure_dir(DA_PACKAGE)
    target = "%s/%s" % (DA_PACKAGE, DA_NAME)
    asset = unreal.load_asset(target)
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.ElysiumWieldTable)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            DA_NAME, DA_PACKAGE, unreal.ElysiumWieldTable, factory)
    if asset is None:
        fail("%s could not be created" % target)
        failed.append(DA_NAME)
        return None

    rows_map = unreal.Map(unreal.Name, unreal.ElysiumWieldRow)
    for classname, entry in sorted(manifest.get("rows", {}).items()):
        row = unreal.ElysiumWieldRow()
        row.set_editor_property("female", wield_model_ref(manifest, entry.get("f", {}), failed))
        row.set_editor_property("male", wield_model_ref(manifest, entry.get("m", {}), failed))
        # UE's Python generator drops a UPROPERTY bool's leading "b" (e.g. bOverrideMass ->
        # override_mass in bake_lib.set_phy_collision); unverified against a live editor for this
        # exact property, so `shows_wield_model` is this file's one guess at that renaming.
        row.set_editor_property("shows_wield_model", bool(entry.get("shows_view_model", 1)))
        # Row keys are case-folded to lower, matching FElysiumWieldTable.h's own stated contract:
        # VtMB compares classnames case-insensitively.
        rows_map[unreal.Name(classname.lower())] = row
    asset.set_editor_property("rows", rows_map)
    if not bl.save(target):
        fail("%s could not be saved" % target)
        failed.append(DA_NAME)
        return None
    log("%s: %d row(s)" % (target, len(rows_map)))
    return asset


# ---------------------------------------------------------------------------- verify


def verify(manifest, baked_stems, errors):
    """After-save verification for everything THIS run baked.

    A stem this run did not bake is left alone -- its package may not exist yet on a fresh mount,
    and re-checking a stem an earlier run already verified duplicates that run's own report."""
    if not baked_stems:
        return
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([WIELD], force_rescan=True)
    for stem in sorted(baked_stems):
        model = manifest["models"][stem]
        asset_name = wc.skeletal_asset(stem)
        package = "%s/%s" % (WIELD, stem)
        mesh = unreal.EditorAssetLibrary.load_asset("%s/%s.%s" % (package, asset_name, asset_name))
        if mesh is None:
            errors.append("%s: not baked" % asset_name)
            continue
        skeleton = mesh.get_editor_property("skeleton")
        if skeleton is None:
            errors.append("%s: has no skeleton" % asset_name)
            continue

        registry_bones = unreal.ElysiumCharacterBakeLibrary.skeleton_bone_count(skeleton)
        source_path = os.path.join(OUT_ROOT, model["eskm"].replace("/", os.sep))
        blob = eskm.read(source_path)
        rows = eskm.bone_locals(blob)
        declared_bones = int(model.get("bone_count", -1))
        # A multi-rooted rig resolves its fork onto one of its own bones rather than gaining an
        # extra synthetic one (`UE_mdl_skeletal.unreal_bones`), so the .eskm's row count is always
        # exactly the manifest's declared StudioBone count.
        if len(rows) != declared_bones:
            errors.append("%s: .eskm carries %d bones, the manifest declares %d"
                          % (stem, len(rows), declared_bones))
        if registry_bones != len(rows):
            errors.append("%s: skeleton has %d bones, .eskm carries %d"
                          % (stem, registry_bones, len(rows)))

        for name, _parent, translation, rotation in rows:
            # The binding collapses `bool RefPoseBoneTransform(..., FTransform&)` to a bare
            # Transform return; a tuple would carry the found flag, so accept both shapes. A
            # missing bone then reads as identity and is caught by the tolerance compare below
            # (the bone-count assert above already rules out a dropped bone).
            result = unreal.ElysiumCharacterBakeLibrary.ref_pose_bone_transform(
                mesh, unreal.Name(name))
            if isinstance(result, tuple):
                ok, local = result
                if not ok:
                    errors.append("%s: bone '%s' has no reference-pose transform on the built mesh"
                                  % (stem, name))
                    continue
            else:
                local = result
            loc = local.translation
            dpos = max(abs(loc.x - translation[0]), abs(loc.y - translation[1]),
                       abs(loc.z - translation[2]))
            if dpos > BONE_POS_EPS_CM:
                errors.append("%s: bone '%s' translation off by %.5f cm" % (stem, name, dpos))
            rot = local.rotation
            drot = wc.quat_angle((rot.x, rot.y, rot.z, rot.w), rotation)
            if drot > BONE_ROT_EPS_DEG:
                errors.append("%s: bone '%s' rotation off by %.4f deg" % (stem, name, drot))

        # The melee-trail VFX's TrailTip socket: a manifest that names one must have baked a real
        # socket at the mount bone, at the .eskm's own converted attachment transform -- the same
        # "the .eskm and the built asset must agree" check the bone loop above runs, just for the
        # one attachment record `wield_corpus.trail_tip` synthesised rather than `.mdl` authored.
        trail_tip = model.get("trail_tip")
        if trail_tip:
            attachment = next(
                (row for row in eskm.attachments(blob) if row[0] == "TrailTip"), None)
            if attachment is None:
                errors.append("%s: manifest names a trail_tip but the .eskm carries no "
                              "'TrailTip' attachment" % stem)
            else:
                socket = mesh.find_socket(unreal.Name("TrailTip"))
                if socket is None:
                    errors.append("%s: baked mesh carries no 'TrailTip' socket" % stem)
                else:
                    _name, _bone, translation, _rotation = attachment
                    bone_name = str(socket.get_editor_property("bone_name"))
                    if bone_name != trail_tip.get("bone"):
                        errors.append("%s: TrailTip socket bone is '%s', manifest names '%s'"
                                      % (stem, bone_name, trail_tip.get("bone")))
                    loc = socket.get_editor_property("relative_location")
                    dpos = max(abs(loc.x - translation[0]), abs(loc.y - translation[1]),
                              abs(loc.z - translation[2]))
                    if dpos > BONE_POS_EPS_CM:
                        errors.append("%s: TrailTip socket translation off by %.5f cm"
                                      % (stem, dpos))

    da = unreal.EditorAssetLibrary.load_asset("%s/%s" % (DA_PACKAGE, DA_NAME))
    if da is None:
        errors.append("%s/%s: data asset not baked" % (DA_PACKAGE, DA_NAME))
        return
    rows_map = da.get_editor_property("rows")
    resolved = 0
    for classname, entry in manifest.get("rows", {}).items():
        key = unreal.Name(classname.lower())
        # `in` / `[]` rather than a `.get()` this Python TMap wrapper may not implement.
        if key not in rows_map:
            errors.append("DA row missing for classname '%s'" % classname)
            continue
        row = rows_map[key]
        for sex_key, prop in (("f", "female"), ("m", "male")):
            sex_entry = entry.get(sex_key, {})
            if sex_entry.get("kind") != "real" or sex_entry.get("stem") not in baked_stems:
                continue
            ref = row.get_editor_property(prop)
            # The binding hands a resolvable TSoftObjectPtr back as the LOADED object (whose str()
            # is a wrapper repr does_asset_exist rejects) and an unresolvable one as None -- so the
            # object's own path is the checkable form, and None IS the does-not-resolve answer.
            mesh_obj = ref.get_editor_property("mesh")
            soft_path = mesh_obj.get_path_name() if mesh_obj is not None else ""
            if not soft_path or not unreal.EditorAssetLibrary.does_asset_exist(soft_path):
                errors.append("DA row '%s' (%s): mesh %r does not resolve through the asset registry"
                              % (classname, prop, soft_path))
            else:
                resolved += 1
    log("verify: %d baked stem(s), %d DA mesh reference(s) resolved" % (len(baked_stems), resolved))


# ---------------------------------------------------------------------------- main


def main():
    manifest = read_manifest()
    models = manifest.get("models", {})
    check_no_stem_collisions(models)
    stems = selected_stems(models, cmdline_arg("BakeWield"))
    force = cmdline_arg("BakeWieldForce", "0") == "1"

    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([MOUNT], force_rescan=True)
    for package in (WIELD, TEXTURES, DA_PACKAGE):
        bl.ensure_dir(package)

    stale_sweep(manifest)

    # Probe every wield master up front, not just the ones this run's own rows happen to select --
    # a run naming only additive/translucent/alphatest stems would otherwise leave a missing
    # DEFAULT_WIELD_MASTER undetected until it silently became MaterialParentPath's fallback.
    for probe_name in (DEFAULT_WIELD_MASTER,) + tuple(name for _flag, name in MASTER_BY_FLAG):
        master_params(probe_name)

    library = unreal.ElysiumSkeletalBuildLibrary
    failed = []
    errors = []

    texture_table = manifest.get("textures", {})
    roles = texture_closure(manifest, stems)
    textures = import_wield_textures(manifest, roles, failed, force=force)

    baked = []
    for stem in stems:
        model = models[stem]
        # One model's failure -- including an unexpected exception -- must not abort the rest of
        # the run; it is logged with its stem and folded into the final summary, per the sibling
        # bakes' policy.
        try:
            mesh_path = build_wield_model(stem, model, library, texture_table, textures, failed)
        except Exception as exc:  # noqa: BLE001 - reported per stem, never swallowed
            fail("%s: build raised %s: %s" % (stem, type(exc).__name__, exc))
            failed.append(stem)
            mesh_path = None
        if mesh_path is not None:
            baked.append(stem)

    build_data_asset(manifest, failed)

    # Verify BEFORE releasing: a package saved and then released in the same process does not
    # reliably reload through `load_asset`, which is why the character bake runs its verifier as
    # a separate commandlet. The whole wield corpus is a few hundred small packages, so holding
    # them to the end costs nothing -- the per-cast memory bound that motivated per-stem release
    # in `bake_characters` does not apply here.
    verify(manifest, set(baked), errors)
    for error in errors:
        unreal.log_error("[wield-verify] %s" % error)
    release_packages()

    if failed or errors:
        raise SystemExit("[wield] failed: %d model failure(s), %d verify error(s) -- %s"
                         % (len(set(failed)), len(errors), ", ".join(sorted(set(failed)))))
    log("done: %d/%d model(s) baked" % (len(baked), len(stems)))


main()
