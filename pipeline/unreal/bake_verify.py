# Reports what a map's bake actually produced, from the assets themselves rather than from
# the bake's own log: asset counts per class, Nanite coverage, triangles, material-slot
# binding, and the level's actor census.
#
# Run headless:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="pipeline/unreal/bake_verify.py"
#       -BakeMap=sp_tutorial_1 -unattended -nosplash -nopause
import json
import math
import os
import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from elysium_pipeline import shared_corpus as SC  # noqa: E402
from elysium_pipeline.paths import export_root  # noqa: E402
from elysium_pipeline.validation.png_alpha import alpha_range  # noqa: E402
from pipeline.unreal import bake_lib as bl  # noqa: E402

MOUNT = "/ElysiumBaked"


_CORPUS_MATERIALS = None


def _corpus_materials():
    """The one material definition per key, which every `.mtl` joins against. Read once: the
    document covers the whole install, and a caller in a per-model loop would otherwise re-read
    several megabytes of JSON per model."""
    global _CORPUS_MATERIALS
    if _CORPUS_MATERIALS is None:
        path = os.fspath(SC.materials_path(export_root()))
        if not os.path.isfile(path):
            _CORPUS_MATERIALS = {}
        else:
            with open(path, "r", encoding="utf-8") as handle:
                _CORPUS_MATERIALS = SC.check_materials(json.load(handle))["materials"]
    return _CORPUS_MATERIALS


def _map_prop_mtls(map_name):
    """The corpus `.mtl` files for the models THIS map places, as ``[(file name, path)]``.

    The corpus holds every model in the install. A map is answerable for the ones it places, so
    the scan joins its own `.props` and `.ents` stems against the corpus rather than walking all
    of it -- which would re-read three thousand models for every map verified and still say
    nothing about this one.
    """
    root = os.fspath(export_root())
    prop_dir = os.fspath(SC.props_dir(root))
    stems = set()
    props = os.path.join(root, map_name, map_name + ".props")
    if os.path.isfile(props):
        with open(props, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                token = line.split()
                if token:
                    stems.add(token[0])
    ents = os.path.join(root, map_name, map_name + ".ents")
    if os.path.isfile(ents):
        with open(ents, "r", encoding="utf-8", errors="replace") as handle:
            for entity in json.load(handle).get("entities", []):
                if entity.get("model_mesh"):
                    stems.add(entity["model_mesh"])
    out = []
    for stem in sorted(stems):
        entry = stem + ".mtl"
        path = os.path.join(prop_dir, entry)
        if os.path.isfile(path):
            out.append((entry, path))
    return out


def _local_materials(map_name):
    """The definitions of materials that exist only inside this map's own PAKFILE."""
    path = os.path.join(os.fspath(export_root()), map_name, "%s.materials.json" % map_name)
    if not os.path.isfile(path):
        return {}
    with open(path, "r", encoding="utf-8") as handle:
        return SC.check_materials(json.load(handle))["materials"]


def _world_materials(map_name):
    """The map's surfaces, joined to their definitions the same way the bake joins them.

    A `.mtl` states slot names and material keys; every channel and flag lives in the corpus, or
    in the map's own `materials.json` for a material only its PAKFILE carries. Reading the `.mtl`
    without both documents yields nothing at all, which would pass every check by verifying an
    empty set.
    """
    world_dir = os.path.join(os.fspath(export_root()), map_name)
    return bl.read_mtl(os.path.join(world_dir, map_name + ".mtl"),
                       corpus=_corpus_materials(), local=_local_materials(map_name))


def _material_slot(package, mat):
    """(package, MIC name) for one surface, on the same split the material stage authored.

    A surface that stamps this map's cubemap, fog or weather is the map's own instance; every
    other surface is the corpus's one instance, under its material key rather than its slot.
    """
    # The surface's own key, not the material's: the cubemap tag the predicate reads is what the
    # map added to the slot, and the material key is the untagged definition underneath it.
    if SC.is_map_scoped_material(mat.name, decal=mat.decal,
                                 wetness_driven=mat.wetness_driven, local=mat.local):
        return ("%s/Materials/Decals" % package if mat.decal else "%s/Materials" % package,
                "MI_" + bl.safe_name(mat.name))
    return (SC.BAKED_MATERIALS, SC.material_asset(mat.material_key))


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


def verify_sm_hub_1_weather(package, world_dir):
    """Verify the authored data contract and the one generated UE weather presentation path."""
    errors = []

    def fail(message):
        unreal.log_error("[verify] weather: " + message)
        errors.append("weather: " + message)

    sidecar_path = os.path.join(world_dir, "sm_hub_1.weather.json")
    try:
        with open(sidecar_path, "r", encoding="utf-8") as handle:
            weather = json.load(handle)
    except (OSError, ValueError) as exc:
        fail("sidecar missing or invalid: %s" % exc)
        return errors
    if weather.get("schema") != "elysium.map-weather" or weather.get("version") != 1:
        fail("sidecar schema/version is not elysium.map-weather v1")
    emitters = weather.get("emitters", [])
    if len(emitters) != 2 or any(
            item.get("particle_definition") != "rain_follow_emitter"
            or item.get("attach_type") != 11 for item in emitters):
        fail("sidecar does not contain exactly two attach_type=11 rain_follow emitters")
    bounds = weather.get("world_bounds_cm", {})
    minimum = bounds.get("min", [])
    maximum = bounds.get("max", [])
    if len(minimum) != 3 or len(maximum) != 3:
        fail("sidecar world bounds are incomplete")
    else:
        footprint = (maximum[0] - minimum[0], maximum[1] - minimum[1])
        if abs(footprint[0] - 28971.24) > 0.01 or abs(footprint[1] - 19639.28) > 0.01:
            fail("sidecar footprint drifted: %.3f x %.3f cm" % footprint)
    height_meta = weather.get("height_texture", {})
    if (height_meta.get("format") != "R16_UNORM"
            or height_meta.get("resolution") != 2048
            or height_meta.get("sentinel") != 0):
        fail("height metadata is not 2048 R16_UNORM with zero sentinel")

    height_path = package + "/Weather/T_RainHeight"
    height = unreal.EditorAssetLibrary.load_asset(height_path)
    if height is None:
        fail("height texture asset is missing: " + height_path)
    else:
        # blueprint_get_size_* reports the currently resident RHI mip (32x32 in a
        # commandlet), not the imported source. Dimensions is authored from Texture.Source.
        dimensions = asset_tag(
            unreal.EditorAssetLibrary.find_asset_data(height_path), "Dimensions")
        if dimensions != "2048x2048":
            fail("height texture source is %s, expected 2048x2048" % (
                dimensions or "<unknown>"))
        if (height.get_editor_property("srgb")
                or height.get_editor_property("compression_settings")
                != unreal.TextureCompressionSettings.TC_DISPLACEMENTMAP):
            fail("height texture is not linear displacement-map R16")

    # The rain master, system and textures are tracked authored assets
    # (Content/ElysiumAuthored/VFX); their presence is a checkout sanity check, not a bake
    # product check. Only the per-map material instances are bake products here.
    expected_assets = [
        "/Game/VtMB/Materials/MPC_ElysiumEnvironment",
        "/Game/ElysiumAuthored/VFX/M_ElysiumRain",
        "/Game/ElysiumAuthored/VFX/NS_ElysiumRain",
        "/Game/ElysiumAuthored/VFX/T_RainDroplet",
        "/Game/ElysiumAuthored/VFX/T_RainMist",
    ]
    for asset in expected_assets:
        if not unreal.EditorAssetLibrary.does_asset_exist(asset):
            fail("required weather asset is missing: " + asset)

    rain_material = unreal.EditorAssetLibrary.load_asset(
        "/Game/ElysiumAuthored/VFX/M_ElysiumRain")
    mic_path = package + "/Weather/MI_ElysiumRain"
    rain_mic = unreal.EditorAssetLibrary.load_asset(mic_path)
    if rain_mic is None:
        fail("per-map rain material instance is missing: " + mic_path)
    else:
        parent = rain_mic.get_editor_property("parent")
        if parent is None or parent != rain_material:
            fail("per-map rain material instance has the wrong parent")
        bound_height = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
            rain_mic, "RainHeightTexture")
        if height is None or bound_height != height:
            fail("per-map rain material has no matching height texture")

    wetness_values = []
    corpus = _corpus_materials()
    for mat in _world_materials("sm_hub_1").values():
        if mat.wet:
            wetness_values.append(float(mat.wetness_scale))
    for _entry, path in _map_prop_mtls("sm_hub_1"):
        for mat in bl.read_mtl(path, corpus=corpus).values():
            if mat.wet:
                wetness_values.append(float(mat.wetness_scale))
    expected_wetness = [0.56] + [0.60] * 6 + [1.0] * 7
    if sorted(wetness_values) != sorted(expected_wetness):
        fail("expected the exact 14-material GlobalWetness scalar corpus, found %r"
             % sorted(wetness_values))

    if not errors:
        unreal.log("[verify] weather one system / 3 emitters / 4 sprites / "
                   "2 authored placements / 14 wet materials / 2048 R16 cover")
    return errors


#: `bake_map` writes these onto every light it places. Duplicated rather than imported because an
#: assertion that calls the code it asserts cannot fail: these say what the bake was ASKED for.
LIGHT_TAG = "elysium.light"
LIGHT_RADIUS_SCALE = 1.0
LIGHT_FALLBACK_RADIUS_CM = 2500.0
LIGHT_SKY_SCALE = 16.0
LIGHT_MIN_SKY_REACH_CM = 5000.0


def verify_lights(actors, world_dir, map_name):
    """Every baked light against its own `.lights` row.

    Nothing read a baked light property before this, which is how every light in every map came to
    carry Unreal's default 1000 cm radius and 44 degree cone: `APointLight` and `ASpotLight`
    construct Stationary, and `SetAttenuationRadius` / `SetInnerConeAngle` / `SetOuterConeAngle`
    all gate on `AreDynamicDataChangesAllowed(false)` and return SILENTLY on one. Intensity and
    colour pass the same gate with `bIgnoreStationary`, so the level looked merely mistuned.

    Mobility is therefore asserted first and on its own: it is the precondition the other three
    depend on, and a Stationary light is the state in which they cannot be trusted at all.
    """
    errors = []
    path = os.path.join(world_dir, "%s.lights" % map_name)
    if not os.path.isfile(path):
        return errors
    rows = {}
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for index, line in enumerate(handle):
            tok = line.split()
            if len(tok) >= 15:
                rows[index] = tok

    # A source inside the 3D-skybox miniature has its reach scaled by the map's own sky scale, so
    # the sidecar that states it is an input here too. 16 is Source's default when a map ships none.
    sky_scale = LIGHT_SKY_SCALE
    sky_path = os.path.join(world_dir, "%s.sky" % map_name)
    if os.path.isfile(sky_path):
        with open(sky_path, "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                tok = line.split()
                if len(tok) == 2 and tok[0] == "scale":
                    sky_scale = float(tok[1])

    checked = 0
    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        if LIGHT_TAG not in tags:
            continue
        source = next((t[len("elysium.src="):] for t in tags if t.startswith("elysium.src=")), "")
        tok = rows.get(int(source)) if source.isdigit() else None
        if tok is None:
            errors.append("%s: carries no resolvable .lights row" % actor.get_actor_label())
            continue
        component = actor.light_component
        if component is None:
            errors.append("%s: has no light component" % actor.get_actor_label())
            continue
        checked += 1

        if component.get_editor_property("mobility") != unreal.ComponentMobility.MOVABLE:
            errors.append("%s: is not Movable, so its radius and cone were dropped in silence"
                          % actor.get_actor_label())
            continue

        kind = int(tok[0])
        is_sky = len(tok) >= 16 and int(tok[15]) != 0
        if kind in (0, 1, 2):
            radius_cm = float(tok[10])
            reach = (radius_cm if radius_cm > 1.0 else LIGHT_FALLBACK_RADIUS_CM) \
                * LIGHT_RADIUS_SCALE
            if is_sky:
                reach = max(reach * sky_scale, LIGHT_MIN_SKY_REACH_CM)
            actual = float(component.get_editor_property("attenuation_radius"))
            if abs(actual - reach) > max(1.0, reach * 1e-3):
                errors.append("%s: .lights states %.1f cm of reach and the actor carries %.1f cm"
                              % (actor.get_actor_label(), reach, actual))
        if kind == 2:
            inner = math.degrees(math.acos(max(-1.0, min(1.0, float(tok[11])))))
            outer = math.degrees(math.acos(max(-1.0, min(1.0, float(tok[12])))))
            inner = max(1.0, min(80.0, inner))
            outer = max(1.0, min(80.0, outer))
            for label, wanted, prop in (("outer", outer, "outer_cone_angle"),
                                        ("inner", min(inner, outer), "inner_cone_angle")):
                actual = float(component.get_editor_property(prop))
                if abs(actual - wanted) > 0.05:
                    errors.append("%s: .lights states a %s cone of %.2f deg and the actor "
                                  "carries %.2f deg" % (actor.get_actor_label(), label,
                                                        wanted, actual))
    unreal.log("[verify] %d baked light(s) checked against %s.lights" % (checked, map_name))
    # Named individually up to a point, then counted: one broken setter breaks every light in the
    # map, and 395 identical lines bury whatever else the run found.
    for message in errors[:8]:
        unreal.log_error("[verify] " + message)
    if len(errors) > 8:
        unreal.log_error("[verify] ... and %d more baked light(s) disagree with %s.lights"
                         % (len(errors) - 8, map_name))
    return errors


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
    # Every surface texture is the shared corpus's, so there is one index rather than a prop set
    # and a world set per map.
    textures = {
        str(data.asset_name): data
        for data in registry.get_assets_by_path(SC.BAKED_TEXTURES, recursive=False)
        if str(data.asset_class_path.asset_name) == "Texture2D"
    }
    world_textures = textures

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
    corpus_dir = os.fspath(SC.corpus_dir(export_root()))
    prop_mtls = _map_prop_mtls(map_name)
    flagged = 0
    nonopaque = {}
    for entry, mtl_path in prop_mtls:
        for mat in bl.read_mtl(mtl_path, corpus=_corpus_materials()).values():
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
            source = os.path.join(corpus_dir, mat.albedo.replace("/", os.sep))
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
                asset_name = SC.texture_asset(mat.albedo)
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
    world_materials = _world_materials(map_name)
    for mat in world_materials.values():
        if not mat.glass:
            continue
        glass_records[_material_slot(package, mat)] = (
            "%s/%s" % (map_name + ".mtl", mat.name), mat, corpus_dir,
            world_textures)
    for entry, mtl_path in prop_mtls:
        for mat in bl.read_mtl(mtl_path, corpus=_corpus_materials()).values():
            if not mat.glass:
                continue
            glass_records[(SC.BAKED_MATERIALS, SC.material_asset(mat.material_key))] = (
                "%s/%s" % (entry, mat.name), mat, corpus_dir, textures)

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
                albedo_name = SC.texture_asset(mat.albedo)
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
            normal_name = SC.texture_asset(mat.bump)
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
    for mat in world_materials.values():
        if not mat.refract:
            continue
        refract_records[_material_slot(package, mat)] = (
            "%s/%s" % (map_name + ".mtl", mat.name), mat, corpus_dir,
            world_textures)
    for entry, mtl_path in prop_mtls:
        for mat in bl.read_mtl(mtl_path, corpus=_corpus_materials()).values():
            if not mat.refract:
                continue
            refract_records[(SC.BAKED_MATERIALS, SC.material_asset(mat.material_key))] = (
                "%s/%s" % (entry, mat.name), mat, corpus_dir, textures)

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
            normal_name = SC.texture_asset(mat.refract_map)
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

    if map_name == "sm_hub_1":
        errors.extend(verify_sm_hub_1_weather(package, world_dir))

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
        errors.extend(verify_lights(actors, world_dir, map_name))
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
