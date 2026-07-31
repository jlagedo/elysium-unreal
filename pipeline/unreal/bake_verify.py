# Reports what a map's bake actually produced, from the assets themselves rather than from
# the bake's own log: asset counts per class, Nanite coverage, triangles, material-slot
# binding, and the level's actor census.
#
# Run headless:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="pipeline/unreal/bake_verify.py"
#       -BakeMap=sp_tutorial_1 -unattended -nosplash -nopause
import json
import os
import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from elysium_pipeline.paths import export_root  # noqa: E402

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
    phys_meshes = 0
    phys_shapes = 0
    massed = 0
    for data in meshes:
        mesh = data.get_asset()
        if not mesh:
            continue
        if mesh.get_editor_property("nanite_settings").get_editor_property("enabled"):
            nanite_on += 1
        # A physics prop is the one mesh class carrying real simple collision: VtMB's own
        # convex hulls, plus its authored mass, plus the trace flag that lets a Chaos body
        # simulate while the debug pick still gets a per-poly face index.
        body = mesh.get_editor_property("body_setup")
        if body is not None and body.get_editor_property("collision_trace_flag") == \
                unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AND_COMPLEX:
            phys_meshes += 1
            phys_shapes += unreal.GeometryScript_Collision.get_simple_collision_shape_count(
                unreal.GeometryScript_Collision.get_simple_collision_from_static_mesh(mesh))
            if body.get_editor_property("default_instance").get_editor_property(
                    "override_mass"):
                massed += 1
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
    unreal.log("[verify] physics props %d, %d convex collision shapes, %d with authored mass"
               % (phys_meshes, phys_shapes, massed))

    ents_path = os.path.join(os.fspath(export_root()), map_name, map_name + ".ents")
    annotated = set()
    if os.path.isfile(ents_path):
        with open(ents_path, "r", encoding="utf-8") as handle:
            for entity in json.load(handle).get("entities", []):
                stem = entity.get("brush_mesh")
                if stem:
                    annotated.add(stem)
    baked = set()
    baked_assets = {}
    for data in registry.get_assets_by_path(package + "/Brushes", recursive=False):
        if str(data.asset_class_path.asset_name) == "StaticMesh":
            name = str(data.asset_name)
            if name.startswith("SM_"):
                stem = name[3:]
                baked.add(stem)
                baked_assets[stem] = data.get_asset()
    missing = sorted(annotated - baked)
    stale = sorted(baked - annotated)
    unreal.log("[verify] brush meshes %d annotated / %d baked / %d missing / %d stale" % (
        len(annotated), len(baked), len(missing), len(stale)))
    for stem in missing:
        unreal.log_error("[verify] missing brush mesh: %s" % stem)
    for stem in stale:
        unreal.log_error("[verify] stale brush mesh: %s" % stem)
    brush_tris = 0
    brush_slots = 0
    brush_unbound = 0
    brush_collision = 0
    for stem, mesh in sorted(baked_assets.items()):
        if not mesh:
            unreal.log_error("[verify] brush mesh failed to load: %s" % stem)
            continue
        try:
            mesh_tris = mesh.get_num_triangles(0)
        except Exception:
            mesh_tris = 0
        materials = mesh.get_editor_property("static_materials")
        unbound_slots = sum(
            1 for slot in materials
            if not slot.get_editor_property("material_interface"))
        body = mesh.get_editor_property("body_setup")
        collision_shapes = 0
        if body is not None:
            collision_shapes = unreal.GeometryScript_Collision.get_simple_collision_shape_count(
                unreal.GeometryScript_Collision.get_simple_collision_from_static_mesh(mesh))
        brush_tris += mesh_tris
        brush_slots += len(materials)
        brush_unbound += unbound_slots
        brush_collision += collision_shapes
        if mesh_tris <= 0:
            unreal.log_error("[verify] brush mesh has no triangles: %s" % stem)
        if not materials or unbound_slots:
            unreal.log_error("[verify] brush mesh material slots invalid: %s (%d slots, %d unbound)"
                             % (stem, len(materials), unbound_slots))
        if collision_shapes:
            unreal.log_error("[verify] brush mesh owns %d simple collision shapes: %s"
                             % (collision_shapes, stem))
    unreal.log("[verify] brush geometry %d tris / %d slots / %d unbound / %d collision shapes" % (
        brush_tris, brush_slots, brush_unbound, brush_collision))

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
