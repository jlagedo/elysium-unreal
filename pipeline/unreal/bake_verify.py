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
from elysium_pipeline.validation.png_alpha import alpha_range  # noqa: E402
from pipeline.unreal import bake_lib as bl  # noqa: E402

MOUNT = "/ElysiumBaked"


def arg(key, default=""):
    needle = "-%s=" % key
    for token in unreal.SystemLibrary.get_command_line().split():
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def asset_tag(data, name):
    """Read a string asset-registry tag across Unreal Python's out-param return shapes."""
    result = unreal.AssetRegistryHelpers.get_tag_value(data, name)
    if isinstance(result, tuple):
        if len(result) == 2 and isinstance(result[0], bool):
            return str(result[1]) if result[0] else ""
        return str(result[-1]) if result else ""
    return str(result) if result is not None else ""


def verify_map(map_name):
    errors = []
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
    textures = {
        str(data.asset_name): data
        for data in registry.get_assets_by_path(package + "/Props/Textures", recursive=False)
        if str(data.asset_class_path.asset_name) == "Texture2D"
    }
    world_textures = {
        str(data.asset_name): data
        for data in registry.get_assets_by_path(package + "/Textures", recursive=False)
        if str(data.asset_class_path.asset_name) == "Texture2D"
    }

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
            message = "static mesh failed to load: %s" % str(data.asset_name)
            unreal.log_error("[verify] " + message)
            errors.append(message)
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
    if unbound:
        errors.append("%d unbound static-mesh material slot(s)" % unbound)
    unreal.log("[verify] physics props %d, %d convex collision shapes, %d with authored mass"
               % (phys_meshes, phys_shapes, massed))

    # A blend flag with an RGB-only source was the prop-transparency failure: the material
    # correctly selected a translucent/masked master, but Albedo.A arrived as implicit 1. Check
    # the exported payload and the independently loaded Texture2D so a stale pre-fix asset cannot
    # pass merely because the mesh has a bound material slot.
    prop_dir = os.path.join(os.fspath(export_root()), map_name, "props")
    flagged = 0
    nonopaque = {}
    if os.path.isdir(prop_dir):
        for entry in sorted(os.listdir(prop_dir)):
            if not entry.lower().endswith(".mtl"):
                continue
            for mat in bl.read_mtl(os.path.join(prop_dir, entry)).values():
                if mat.refract:
                    continue
                if not (mat.blend or mat.scissor or mat.additive):
                    continue
                flagged += 1
                if not mat.albedo:
                    message = "%s/%s: alpha material has no albedo" % (entry, mat.name)
                    unreal.log_error("[verify] " + message)
                    errors.append(message)
                    continue
                source = os.path.join(prop_dir, mat.albedo.replace("/", os.sep))
                try:
                    source_alpha = alpha_range(source)
                except (OSError, ValueError) as exc:
                    message = "%s/%s: alpha source invalid: %s" % (entry, mat.name, exc)
                    unreal.log_error("[verify] " + message)
                    errors.append(message)
                    continue
                if source_alpha is None:
                    message = "%s/%s: alpha material exported an RGB albedo" % (entry, mat.name)
                    unreal.log_error("[verify] " + message)
                    errors.append(message)
                    continue
                if source_alpha[0] < 255:
                    asset_name = "T_" + bl.safe_name(os.path.splitext(mat.albedo)[0])
                    nonopaque[asset_name] = "%s/%s" % (entry, mat.name)

    alpha_capable = 0
    for asset_name, owner in sorted(nonopaque.items()):
        data = textures.get(asset_name)
        if data is None:
            message = "%s: baked alpha texture missing: %s" % (owner, asset_name)
            unreal.log_error("[verify] " + message)
            errors.append(message)
            continue
        if asset_tag(data, "HasAlphaChannel").lower() != "true":
            message = "%s: baked texture has no alpha channel: %s" % (owner, asset_name)
            unreal.log_error("[verify] " + message)
            errors.append(message)
            continue
        alpha_capable += 1
    unreal.log("[verify] prop alpha %d flagged / %d non-opaque / %d alpha-capable" % (
        flagged, len(nonopaque), alpha_capable))

    # Semantic glass must reach the dedicated Thin Translucent master with both pieces the
    # refraction graph needs: exact authored alpha on Albedo and a linear normal-compressed
    # BumpMap. Inspect the exported contract and independently load the baked MIC/Texture2D
    # assets, so stale material parents or texture settings cannot pass on MTL intent alone.
    glass_records = {}
    world_dir = os.path.join(os.fspath(export_root()), map_name)
    world_mtl = os.path.join(world_dir, map_name + ".mtl")
    for mat in bl.read_mtl(world_mtl).values():
        if not mat.glass:
            continue
        mic_name = "MI_" + bl.safe_name(mat.name)
        glass_records[(package + "/Materials", mic_name)] = (
            "%s/%s" % (map_name + ".mtl", mat.name), mat, world_dir,
            world_textures)
    if os.path.isdir(prop_dir):
        for entry in sorted(os.listdir(prop_dir)):
            if not entry.lower().endswith(".mtl"):
                continue
            for mat in bl.read_mtl(os.path.join(prop_dir, entry)).values():
                if not mat.glass:
                    continue
                key = "%s__%s" % (
                    bl.safe_name(mat.name),
                    bl.safe_name(os.path.splitext(mat.albedo)[0]))
                mic_name = "MI_" + bl.safe_name(key)
                glass_records[(package + "/Props/Materials", mic_name)] = (
                    "%s/%s" % (entry, mat.name), mat, prop_dir, textures)

    glass_alpha = 0
    glass_normals = 0
    glass_parented = 0
    expected_parent = "/Game/VtMB/Materials/M_World_Glass.M_World_Glass"
    for (mat_package, mic_name), (owner, mat, source_dir, tex_index) in sorted(
            glass_records.items()):
        if not mat.blend:
            message = "%s: glass material is not alpha blended" % owner
            unreal.log_error("[verify] " + message)
            errors.append(message)

        if not mat.albedo:
            message = "%s: glass material has no albedo" % owner
            unreal.log_error("[verify] " + message)
            errors.append(message)
        else:
            source = os.path.join(source_dir, mat.albedo.replace("/", os.sep))
            try:
                source_alpha = alpha_range(source)
            except (OSError, ValueError) as exc:
                source_alpha = None
                message = "%s: glass alpha source invalid: %s" % (owner, exc)
                unreal.log_error("[verify] " + message)
                errors.append(message)
            if source_alpha is None or source_alpha[0] >= 255:
                message = "%s: glass albedo has no non-opaque source alpha" % owner
                unreal.log_error("[verify] " + message)
                errors.append(message)
            else:
                albedo_name = "T_" + bl.safe_name(os.path.splitext(mat.albedo)[0])
                albedo_data = tex_index.get(albedo_name)
                if albedo_data is None or asset_tag(
                        albedo_data, "HasAlphaChannel").lower() != "true":
                    message = "%s: baked glass albedo is not alpha-capable: %s" % (
                        owner, albedo_name)
                    unreal.log_error("[verify] " + message)
                    errors.append(message)
                else:
                    glass_alpha += 1

        normal_asset = None
        if not mat.bump:
            message = "%s: glass material has no bumpmap" % owner
            unreal.log_error("[verify] " + message)
            errors.append(message)
        else:
            normal_name = "T_" + bl.safe_name(os.path.splitext(mat.bump)[0])
            normal_data = tex_index.get(normal_name)
            normal_asset = normal_data.get_asset() if normal_data is not None else None
            if normal_asset is None:
                message = "%s: baked glass normal missing: %s" % (owner, normal_name)
                unreal.log_error("[verify] " + message)
                errors.append(message)
            elif normal_asset.get_editor_property("srgb") or normal_asset.get_editor_property(
                    "compression_settings") != unreal.TextureCompressionSettings.TC_NORMALMAP:
                message = "%s: glass normal is not linear normal-compressed: %s" % (
                    owner, normal_name)
                unreal.log_error("[verify] " + message)
                errors.append(message)
            else:
                glass_normals += 1

        mic_path = "%s/%s" % (mat_package, mic_name)
        mic = unreal.EditorAssetLibrary.load_asset(mic_path)
        if mic is None:
            message = "%s: baked glass material instance missing: %s" % (owner, mic_path)
            unreal.log_error("[verify] " + message)
            errors.append(message)
            continue
        parent = mic.get_editor_property("parent")
        if parent is None or parent.get_path_name() != expected_parent:
            message = "%s: glass MIC parent is not M_World_Glass" % owner
            unreal.log_error("[verify] " + message)
            errors.append(message)
            continue
        bound_normal = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
            mic, "BumpMap")
        if normal_asset is None or bound_normal != normal_asset:
            message = "%s: glass MIC has no matching BumpMap binding" % owner
            unreal.log_error("[verify] " + message)
            errors.append(message)
            continue
        glass_parented += 1

    if map_name == "sp_theatre" and len(glass_records) != 2:
        message = "sp_theatre expected 2 semantic glass materials, found %d" % len(glass_records)
        unreal.log_error("[verify] " + message)
        errors.append(message)
    unreal.log("[verify] glass %d flagged / %d alpha-capable / %d normal-bound / %d parented" % (
        len(glass_records), glass_alpha, glass_normals, glass_parented))

    # Source Refract is the authored distortion overlay, independent of glass albedo. Its
    # converted DUDV/normal must be a linear normal texture, bound to the dedicated clear PNO
    # master along with the original $refractamount. This is the path the pawnshop rain-window
    # cards use; treating their UVWQ texture as colour is the opaque "bubble" failure.
    refract_records = {}
    for mat in bl.read_mtl(world_mtl).values():
        if not mat.refract:
            continue
        mic_name = "MI_" + bl.safe_name(mat.name)
        refract_records[(package + "/Materials", mic_name)] = (
            "%s/%s" % (map_name + ".mtl", mat.name), mat, world_dir,
            world_textures)
    if os.path.isdir(prop_dir):
        for entry in sorted(os.listdir(prop_dir)):
            if not entry.lower().endswith(".mtl"):
                continue
            for mat in bl.read_mtl(os.path.join(prop_dir, entry)).values():
                if not mat.refract:
                    continue
                key = "%s__%s" % (
                    bl.safe_name(mat.name),
                    bl.safe_name(os.path.splitext(mat.albedo)[0]))
                mic_name = "MI_" + bl.safe_name(key)
                refract_records[(package + "/Props/Materials", mic_name)] = (
                    "%s/%s" % (entry, mat.name), mat, prop_dir, textures)

    refract_normals = 0
    refract_parented = 0
    expected_refract_parent = "/Game/VtMB/Materials/M_Refract.M_Refract"
    for (mat_package, mic_name), (owner, mat, source_dir, tex_index) in sorted(
            refract_records.items()):
        normal_asset = None
        if not mat.refract_map:
            message = "%s: Source Refract has no refractmap" % owner
            unreal.log_error("[verify] " + message)
            errors.append(message)
        else:
            source = os.path.join(source_dir, mat.refract_map.replace("/", os.sep))
            if not os.path.isfile(source):
                message = "%s: Source Refract PNG missing: %s" % (owner, source)
                unreal.log_error("[verify] " + message)
                errors.append(message)
            normal_name = "T_" + bl.safe_name(os.path.splitext(mat.refract_map)[0])
            normal_data = tex_index.get(normal_name)
            normal_asset = normal_data.get_asset() if normal_data is not None else None
            if normal_asset is None:
                message = "%s: baked Source Refract normal missing: %s" % (
                    owner, normal_name)
                unreal.log_error("[verify] " + message)
                errors.append(message)
            elif normal_asset.get_editor_property("srgb") or normal_asset.get_editor_property(
                    "compression_settings") != unreal.TextureCompressionSettings.TC_NORMALMAP:
                message = "%s: Source Refract map is not linear normal-compressed: %s" % (
                    owner, normal_name)
                unreal.log_error("[verify] " + message)
                errors.append(message)
            else:
                refract_normals += 1

        mic_path = "%s/%s" % (mat_package, mic_name)
        mic = unreal.EditorAssetLibrary.load_asset(mic_path)
        if mic is None:
            message = "%s: baked Source Refract material instance missing: %s" % (
                owner, mic_path)
            unreal.log_error("[verify] " + message)
            errors.append(message)
            continue
        parent = mic.get_editor_property("parent")
        if parent is None or parent.get_path_name() != expected_refract_parent:
            message = "%s: Source Refract MIC parent is not M_Refract" % owner
            unreal.log_error("[verify] " + message)
            errors.append(message)
            continue
        bound_normal = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
            mic, "RefractMap")
        if normal_asset is None or bound_normal != normal_asset:
            message = "%s: Source Refract MIC has no matching RefractMap binding" % owner
            unreal.log_error("[verify] " + message)
            errors.append(message)
            continue
        bound_amount = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
            mic, "SourceRefractAmount")
        if abs(float(bound_amount) - mat.refract_amount) > 1e-6:
            message = "%s: Source Refract amount binding drifted (%.6f != %.6f)" % (
                owner, float(bound_amount), mat.refract_amount)
            unreal.log_error("[verify] " + message)
            errors.append(message)
            continue
        refract_parented += 1

    rain_placements = 0
    props_path = os.path.join(world_dir, map_name + ".props")
    if os.path.isfile(props_path):
        with open(props_path, "r", encoding="utf-8", errors="replace") as handle:
            rain_placements = sum(
                1 for line in handle
                if line.split() and line.split()[0] ==
                "models_scenery_structural_santamonica_rain_window")
    if map_name == "sp_theatre":
        if len(refract_records) != 1:
            message = "sp_theatre expected 1 Source Refract material, found %d" % len(
                refract_records)
            unreal.log_error("[verify] " + message)
            errors.append(message)
        if rain_placements != 6:
            message = "sp_theatre expected 6 rain-window Refract cards, found %d" % (
                rain_placements)
            unreal.log_error("[verify] " + message)
            errors.append(message)
    unreal.log("[verify] Source Refract %d flagged / %d normal / %d parented / "
               "%d rain-window placements" % (
                   len(refract_records), refract_normals, refract_parented, rain_placements))

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
        errors.append("missing brush mesh: %s" % stem)
    for stem in stale:
        unreal.log_error("[verify] stale brush mesh: %s" % stem)
        errors.append("stale brush mesh: %s" % stem)
    brush_tris = 0
    brush_slots = 0
    brush_unbound = 0
    brush_collision = 0
    for stem, mesh in sorted(baked_assets.items()):
        if not mesh:
            unreal.log_error("[verify] brush mesh failed to load: %s" % stem)
            errors.append("brush mesh failed to load: %s" % stem)
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
            errors.append("brush mesh has no triangles: %s" % stem)
        if not materials or unbound_slots:
            unreal.log_error("[verify] brush mesh material slots invalid: %s (%d slots, %d unbound)"
                             % (stem, len(materials), unbound_slots))
            errors.append("brush mesh material slots invalid: %s" % stem)
        if collision_shapes:
            unreal.log_error("[verify] brush mesh owns %d simple collision shapes: %s"
                             % (collision_shapes, stem))
            errors.append("brush mesh owns simple collision: %s" % stem)
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
    else:
        message = "level missing: %s" % level
        unreal.log_error("[verify] " + message)
        errors.append(message)
    return errors


def main():
    raw_maps = arg("BakeMaps", "")
    map_names = [item.strip() for item in raw_maps.split(",") if item.strip()]
    if not map_names:
        map_names = [arg("BakeMap", "sp_tutorial_1")]
    failures = {}
    for map_name in map_names:
        errors = verify_map(map_name)
        if errors:
            failures[map_name] = errors
    if failures:
        unreal.log_error("[verify] %d of %d map(s) failed verification" % (
            len(failures), len(map_names)))
        raise SystemExit(1)
    unreal.log("[verify] all %d map(s) passed" % len(map_names))


main()
