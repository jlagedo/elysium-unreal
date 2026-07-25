# Reports what a map's bake actually produced, from the assets themselves rather than from
# the bake's own log: asset counts per class, Nanite coverage, triangles, material-slot
# binding, and the level's actor census.
#
# Run headless:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="tools/bake_verify.py"
#       -BakeMap=sp_tutorial_1 -unattended -nosplash -nopause
import unreal

MOUNT = "/ElysiumBaked"


def arg(key, default=""):
    needle = "-%s=" % key
    for token in unreal.SystemLibrary.get_command_line().split():
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def main():
    map_name = arg("BakeMap", "sp_tutorial_1")
    package = "%s/%s" % (MOUNT, map_name)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([MOUNT], force_rescan=True)

    by_class = {}
    meshes = []
    for data in registry.get_assets_by_path(package, recursive=True):
        name = str(data.asset_class_path.asset_name)
        by_class[name] = by_class.get(name, 0) + 1
        if name == "StaticMesh":
            meshes.append(data)

    unreal.log("[verify] %s" % package)
    for name in sorted(by_class):
        unreal.log("[verify]   %-28s %d" % (name, by_class[name]))

    nanite_on = 0
    tris = 0
    slots = 0
    unbound = 0
    for data in meshes:
        mesh = data.get_asset()
        if not mesh:
            continue
        if mesh.get_editor_property("nanite_settings").get_editor_property("enabled"):
            nanite_on += 1
        # The StaticMeshEditorSubsystem is absent in a commandlet, so triangles are
        # best-effort here; the bake's own per-stage counts are the authority.
        try:
            tris += mesh.get_num_triangles(0)
        except Exception:
            pass
        for slot in mesh.get_editor_property("static_materials"):
            slots += 1
            if not slot.get_editor_property("material_interface"):
                unbound += 1
    unreal.log("[verify] meshes %d, Nanite on %d, off %d" % (
        len(meshes), nanite_on, len(meshes) - nanite_on))
    unreal.log("[verify] triangles %d across %d material slots (%d unbound)" % (
        tris, slots, unbound))

    level = "%s/%s" % (package, map_name)
    if unreal.EditorAssetLibrary.does_asset_exist(level):
        unreal.EditorLoadingAndSavingUtils.load_map(level)
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        census = {}
        for actor in actors:
            key = actor.get_class().get_name()
            census[key] = census.get(key, 0) + 1
        unreal.log("[verify] level %s: %d actors" % (level, len(actors)))
        for key in sorted(census, key=lambda k: -census[k]):
            unreal.log("[verify]   %-28s %d" % (key, census[key]))


main()
