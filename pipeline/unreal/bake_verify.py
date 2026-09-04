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
import re
import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from elysium_pipeline import map_transport  # noqa: E402
from elysium_pipeline import mounts  # noqa: E402
from elysium_pipeline import shared_corpus as SC  # noqa: E402
from elysium_pipeline.paths import export_root, work_root  # noqa: E402
from elysium_pipeline.validation.png_alpha import alpha_range  # noqa: E402
from pipeline.unreal import bake_lib as bl  # noqa: E402

MOUNT = mounts.BAKED
MATERIALS = mounts.MATERIALS


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

    R7.2 retired the `Materials/Decals` split this used to answer for a `$decal` surface: neither
    lane authors a per-map decal material any more (`bake_map._material_sets` returns an empty
    decal set and `bake_map_v2` the same), so the one per-map package left is `Materials`. The
    projector a decal actually draws through is the corpus-wide `MI_<unit>_Decal`, which is not a
    surface slot and never appears here.
    """
    # The surface's own key, not the material's: the cubemap tag the predicate reads is what the
    # map added to the slot, and the material key is the untagged definition underneath it.
    if SC.is_map_scoped_material(mat.name, decal=mat.decal,
                                 wetness_driven=mat.wetness_driven, local=mat.local):
        return ("%s/Materials" % package, "MI_" + bl.safe_name(mat.name))
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
        MATERIALS + "/MPC_ElysiumEnvironment",
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
#: `bake_map_v2` writes this onto every reflection capture it places (R5.5).
CAPTURE_TAG = "elysium.capture"


def verify_captures(actors, world):
    """Every placed reflection capture carries built MapBuildData (R5.5).

    The capture's image lives in `<map>_BuiltData`, not on the actor, so a level that saved with
    its captures placed but never built looks identical in a census; only the registry says. The
    count comes from the same library call the bake asserted with, over the loaded level.
    """
    errors = []
    placed = sum(1 for actor in actors if CAPTURE_TAG in [str(tag) for tag in actor.tags])
    if not placed:
        return errors
    counted = unreal.ElysiumMapBakeLibrary.count_built_reflection_captures(world)
    built, components = (counted if isinstance(counted, tuple) else (int(counted), placed))
    unreal.log("[verify] %d reflection capture(s) placed, %d component(s), %d with MapBuildData"
               % (placed, components, built))
    if built != placed:
        errors.append("reflection captures: %d placed, %d built into MapBuildData"
                      % (placed, built))
    return errors


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


def _lights_rows_that_place(path):
    """The `.lights` rows the bake places an actor for: type 0-3 with `max(rgb) > 0`, keyed by
    line index (the lump-15 ordinal). Type 5 skyambient tints the SkyLight and places none."""
    rows = {}
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for index, line in enumerate(handle):
            tok = line.split()
            if len(tok) < 15:
                continue
            if int(tok[0]) in (0, 1, 2, 3) and max(float(v) for v in tok[7:10]) > 0.0:
                rows[index] = tok
    return rows


def verify_lights_baked(actors, world_dir, map_name):
    """R5.6, `MapsOnV2Models` maps only: light-count parity against the legacy `.lights`, and the
    four derivation assertions re-homed from `Elysium.Substrate.LightRig` onto the bake's own
    output (`seam_map_map_lighting.md` -> "Lights final (R5.6)" -> "Verification, re-homed").

    Parity: one `elysium.light` actor per `.lights` row that places one (type 0-3, `max(rgb) > 0`),
    every actor's `elysium.src` resolving to exactly one such row, no row placed twice. The
    sidecar is the independent witness here -- it is written by the same `light_rows` the staged
    table is, so a disagreement means the editor half dropped or doubled a row.

    Baked values: non-inverse-square falloff, MegaLights allowed with the RT shadow method on every
    local light, shadows per the lighting page's flags and the type (a texlight never), and the
    `elysium.type`/`elysium.style` tags equal to the row's -- the two facts the slim rig reads.
    """
    errors = []
    if not map_transport.is_map_on_v2_models(map_name):
        return errors
    path = os.path.join(world_dir, "%s.lights" % map_name)
    if not os.path.isfile(path):
        errors.append("%s: on MapsOnV2Models but no %s.lights to check parity against"
                      % (map_name, map_name))
        return errors
    rows = _lights_rows_that_place(path)
    page = unreal.get_default_object(unreal.ElysiumLightingSettings)
    shadows = {
        0: False,
        1: bool(page.get_editor_property("point_shadows")),
        2: bool(page.get_editor_property("spot_shadows")),
        3: bool(page.get_editor_property("sun_shadows")),
    }

    seen = {}
    lights = [actor for actor in actors if LIGHT_TAG in [str(tag) for tag in actor.tags]]
    for actor in lights:
        tags = [str(tag) for tag in actor.tags]
        label = actor.get_actor_label()

        def tag_int(prefix):
            value = next((t[len(prefix):] for t in tags if t.startswith(prefix)), "")
            return int(value) if value.lstrip("-").isdigit() else None

        source = tag_int("elysium.src=")
        tok = rows.get(source) if source is not None else None
        if tok is None:
            errors.append("%s: elysium.src=%s names no placing .lights row" % (label, source))
            continue
        if source in seen:
            errors.append("%s: .lights row %d is also %s" % (label, source, seen[source]))
            continue
        seen[source] = label
        kind = int(tok[0])
        if tag_int("elysium.type=") != kind:
            errors.append("%s: elysium.type tag %s, .lights type %d"
                          % (label, tag_int("elysium.type="), kind))
        if tag_int("elysium.style=") != int(tok[14]):
            errors.append("%s: elysium.style tag %s, .lights style %s"
                          % (label, tag_int("elysium.style="), tok[14]))
        component = actor.light_component
        if component is None:
            errors.append("%s: has no light component" % label)
            continue
        if bool(component.get_editor_property("cast_shadows")) != shadows[kind]:
            errors.append("%s: cast_shadows %s, the page says %s for type %d"
                          % (label, component.get_editor_property("cast_shadows"),
                             shadows[kind], kind))
        if kind in (0, 1, 2):
            if component.get_editor_property("use_inverse_squared_falloff"):
                errors.append("%s: inverse-square falloff on a baked local light" % label)
            if not component.get_editor_property("allow_mega_lights"):
                errors.append("%s: MegaLights not allowed on a baked local light" % label)
            method = component.get_editor_property("mega_lights_shadow_method")
            if method != unreal.MegaLightsShadowMethod.RAY_TRACING:
                errors.append("%s: MegaLights shadow method %s, not RayTracing" % (label, method))
    missing = sorted(set(rows) - set(seen))
    if missing:
        errors.append("%d placing .lights row(s) have no actor: %s%s" % (
            len(missing), ", ".join(str(i) for i in missing[:8]),
            " ..." if len(missing) > 8 else ""))
    unreal.log("[verify] lights parity: %d actor(s) / %d placing .lights row(s) / %d matched"
               % (len(lights), len(rows), len(seen)))
    if len(lights) != len(rows):
        errors.append("light-count parity: %d actors, %d placing .lights rows"
                      % (len(lights), len(rows)))
    for message in errors[:8]:
        unreal.log_error("[verify] " + message)
    if len(errors) > 8:
        unreal.log_error("[verify] ... and %d more baked-light finding(s) on %s"
                         % (len(errors) - 8, map_name))
    return errors


DETAIL_TAG = "elysium.detail"
DETAIL_MODEL_TAG_PREFIX = "elysium.model="


def _staged_details(map_name):
    """The staged `details` table the V2 bake instanced from (`map_geometry.stage_map`, manifest
    v5, under `$ELYSIUM_WORK_ROOT/import/map_geometry/<map>/`), or None when the pair is absent."""
    path = os.path.join(os.fspath(work_root()), "import", "map_geometry", map_name, "manifest.json")
    if not os.path.isfile(path):
        return None
    with open(path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    return manifest.get("details") or {"models": [], "records": []}


def verify_details(actors, map_name):
    """R6.3 (`seam_map_map.md` -> "Detail props (R6.3)"), `MapsOnV2Models` maps only: every
    `elysium.detail` actor's instanced component counted back against the staged `details`
    table -- one component per `(model, sky)` group, the same instance count, no model missing and
    none extra -- plus the cull range the Models page names and exactly one custom-data float per
    component (the `swayAmount / 255` slot)."""
    errors = []
    if not map_transport.is_map_on_v2_models(map_name):
        return errors
    details = _staged_details(map_name)
    if details is None:
        errors.append("%s: on MapsOnV2Models but no staged map_geometry manifest to count "
                      "detail props against (run: uv run elysium export map %s)"
                      % (map_name, map_name))
        return errors
    stems = {int(model["model"]): str(model["stem"]) for model in details.get("models") or []}
    expected = {}
    for row in details.get("records") or []:
        key = (stems[int(row[1])], bool(row[10]))
        expected[key] = expected.get(key, 0) + 1

    page = unreal.get_default_object(unreal.ElysiumModelSettings)
    end_cm = max(0.0, float(page.get_editor_property("detail_draw_distance_cm")))
    start_cm = max(0.0, end_cm - max(0.0, float(page.get_editor_property("detail_fade_range_cm"))))

    found = {}
    instances = 0
    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        if DETAIL_TAG not in tags:
            continue
        stem = next((tag[len(DETAIL_MODEL_TAG_PREFIX):] for tag in tags
                     if tag.startswith(DETAIL_MODEL_TAG_PREFIX)), "")
        sky = str(actor.get_folder_path()).startswith("Sky")
        component = actor.get_editor_property("instances")
        if component is None:
            errors.append("%s: detail actor %s has no instanced component"
                          % (map_name, actor.get_actor_label()))
            continue
        count = int(component.get_instance_count())
        key = (stem, sky)
        if key in found:
            errors.append("%s: two detail components for %s%s"
                          % (map_name, stem, " (sky)" if sky else ""))
        found[key] = count
        instances += count
        if int(component.get_editor_property("num_custom_data_floats")) != 1:
            errors.append("%s: detail %s carries %d custom data floats, not 1" % (
                map_name, stem, int(component.get_editor_property("num_custom_data_floats"))))
        got_start = int(component.get_editor_property("instance_start_cull_distance"))
        got_end = int(component.get_editor_property("instance_end_cull_distance"))
        if got_start != int(round(start_cm)) or got_end != int(round(end_cm)):
            errors.append("%s: detail %s culls %d..%d cm, the Models page says %d..%d" % (
                map_name, stem, got_start, got_end, int(round(start_cm)), int(round(end_cm))))
    matched = 0
    for key, want in sorted(expected.items()):
        got = found.get(key)
        if got is None:
            errors.append("%s: no detail component for %s%s (%d staged records)"
                          % (map_name, key[0], " (sky)" if key[1] else "", want))
        elif got != want:
            errors.append("%s: detail %s%s has %d instances, the unit stages %d"
                          % (map_name, key[0], " (sky)" if key[1] else "", got, want))
        else:
            matched += 1
    for key in sorted(set(found) - set(expected)):
        errors.append("%s: detail component %s%s has no staged records"
                      % (map_name, key[0], " (sky)" if key[1] else ""))
    unreal.log("[verify] details: %d instances over %d component(s), %d staged records over "
               "%d model group(s), %d matched; cull %d..%d cm" % (
                   instances, len(found), len(details.get("records") or []), len(expected),
                   matched, int(round(start_cm)), int(round(end_cm))))
    for message in errors:
        unreal.log_error("[verify] " + message)
    return errors


SPRITE_TAG = "elysium.sprite"
SPRITE_ENTITY_TAG_PREFIX = "elysium.ent="
#: `bake_map_v2.SPRITE_BLEND_MEMBERS`, restated (this module restates the writer's tables rather
#: than importing the writer). Spelled out rather than derived as `"BLEND_" + blend.upper()`:
#: `AlphaComposite` -- the member `$spriterendermode` 8 selects -- is `BLEND_ALPHA_COMPOSITE` in
#: Unreal's Python spelling, so the upper-casing shortcut resolved it to `None` and let a
#: mis-baked blend verify clean.
SPRITE_BLEND_MEMBERS = {
    "Opaque": "BLEND_OPAQUE", "Masked": "BLEND_MASKED", "Translucent": "BLEND_TRANSLUCENT",
    "Additive": "BLEND_ADDITIVE", "Modulate": "BLEND_MODULATE",
    "AlphaComposite": "BLEND_ALPHA_COMPOSITE",
}


def _staged_sprites(map_name):
    """The staged `sprites` table the V2 bake placed billboards from (manifest v6), or None when
    the pair is absent."""
    path = os.path.join(os.fspath(work_root()), "import", "map_geometry", map_name, "manifest.json")
    if not os.path.isfile(path):
        return None
    with open(path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    return manifest.get("sprites") or []


def verify_sprites(actors, map_name):
    """R6.1 (`seam_map_map.md` -> "Sprites (R6.1)"), `MapsOnV2Models` maps only: every
    `elysium.sprite` actor matched to its staged row by the entity index tag -- size, colour,
    mode, orientation, the spawn-hidden state, and the material child's parent and blend -- no
    row missing and no actor extra."""
    errors = []
    if not map_transport.is_map_on_v2_models(map_name):
        return errors
    rows = _staged_sprites(map_name)
    if rows is None:
        errors.append("%s: on MapsOnV2Models but no staged map_geometry manifest to count "
                      "sprites against (run: uv run elysium export map %s)" % (map_name, map_name))
        return errors
    expected = {int(row["index"]): row for row in rows}
    found = {}
    glow = 0
    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        if SPRITE_TAG not in tags:
            continue
        index = next((int(tag[len(SPRITE_ENTITY_TAG_PREFIX):]) for tag in tags
                      if tag.startswith(SPRITE_ENTITY_TAG_PREFIX)), -1)
        if index in found:
            errors.append("%s: two sprite actors carry entity index %d" % (map_name, index))
        found[index] = actor
    matched = 0
    for index, row in sorted(expected.items()):
        actor = found.get(index)
        if actor is None:
            errors.append("%s: no sprite actor for env_sprite %d (%s)"
                          % (map_name, index, row["material"]))
            continue
        component = actor.get_editor_property("sprite")
        if component is None:
            errors.append("%s: sprite actor %s has no sprite component"
                          % (map_name, actor.get_actor_label()))
            continue
        problems = []
        size = component.get_editor_property("size_inches")
        want = (float(row["scale"]) * int(row["width"]), float(row["scale"]) * int(row["height"]))
        if abs(float(size.x) - want[0]) > 1e-3 or abs(float(size.y) - want[1]) > 1e-3:
            problems.append("size %.2fx%.2f, staged %.2fx%.2f" % (size.x, size.y, want[0], want[1]))
        color = component.get_editor_property("color")
        got_color = (int(color.r), int(color.g), int(color.b), int(color.a))
        want_color = tuple(int(v) for v in row["color"]) + (int(row["alpha"]),)
        if got_color != want_color:
            problems.append("colour %s, staged %s" % (got_color, want_color))
        if int(component.get_editor_property("render_mode")) != int(row["mode"]):
            problems.append("mode %d, staged %d" % (
                int(component.get_editor_property("render_mode")), int(row["mode"])))
        if bool(component.get_editor_property("upright")) != bool(row["upright"]):
            problems.append("upright %s, staged %s" % (
                bool(component.get_editor_property("upright")), bool(row["upright"])))
        if bool(actor.get_editor_property("hidden")) != bool(row["hidden"]):
            problems.append("hidden %s, staged %s" % (
                bool(actor.get_editor_property("hidden")), bool(row["hidden"])))
        if int(actor.get_editor_property("entity_index")) != index:
            problems.append("entity_index %d, tag %d" % (
                int(actor.get_editor_property("entity_index")), index))
        material = component.get_editor_property("material")
        if material is None:
            problems.append("no material")
        else:
            parent = material.get_editor_property("parent")
            parent_path = parent.get_path_name().split(".", 1)[0] if parent else ""
            if parent_path != row["asset"]:
                problems.append("material parent %s, staged %s" % (parent_path, row["asset"]))
            overrides = material.get_editor_property("base_property_overrides")
            blend = overrides.get_editor_property("blend_mode")
            member = SPRITE_BLEND_MEMBERS.get(row["blend"])
            want_blend = getattr(unreal.BlendMode, member, None) if member else None
            if want_blend is None:
                problems.append("staged blend %r names no BlendMode" % (row["blend"],))
            elif not overrides.get_editor_property("override_blend_mode") or blend != want_blend:
                problems.append("blend %s (override %s), staged %s" % (
                    blend, bool(overrides.get_editor_property("override_blend_mode")), row["blend"]))
        if problems:
            errors.append("%s: env_sprite %d (%s): %s" % (
                map_name, index, row["material"], "; ".join(problems)))
        else:
            matched += 1
            glow += 1 if row["glow"] else 0
    for index in sorted(set(found) - set(expected)):
        errors.append("%s: sprite actor %s carries entity index %d, which stages no env_sprite"
                      % (map_name, found[index].get_actor_label(), index))
    unreal.log("[verify] sprites: %d actors, %d staged rows, %d matched (%d glow)" % (
        len(found), len(expected), matched, glow))
    for message in errors[:8]:
        unreal.log_error("[verify] " + message)
    if len(errors) > 8:
        unreal.log_error("[verify] ... and %d more sprite finding(s) on %s"
                         % (len(errors) - 8, map_name))
    return errors


EFFECT_TAG = "elysium.effect"
#: The staged table -> the actor class the V2 bake places it as (`bake_map_v2.EFFECT_ACTOR_CLASSES`).
EFFECT_ACTOR_CLASSES = {
    "effects": "ElysiumEffectActor", "dustmotes": "ElysiumDustActor",
    "steam": "ElysiumSteamActor", "beams": "ElysiumBeamActor",
}


def verify_effects(actors, map_name):
    """R7.3 (`seam_map_map.md` -> "Import -- effects (R7.3)"), `MapsOnV2Models` maps only: every
    `elysium.effect` actor matched to its staged row by the entity index tag -- the class per
    table, and for an `effects[]` row the root, the attach data and the tree's leaf count -- no
    row missing (an unresolved root places no actor, by VtMB's own rule) and no actor extra."""
    errors = []
    if not map_transport.is_map_on_v2_models(map_name):
        return errors
    manifest = _staged_manifest(map_name)
    if manifest is None:
        errors.append("%s: on MapsOnV2Models but no staged map_geometry manifest to count "
                      "effects against (run: uv run elysium export map %s)" % (map_name, map_name))
        return errors
    expected = {}
    for table in ("effects", "dustmotes", "steam", "beams"):
        for row in manifest.get(table) or []:
            if table == "effects" and not row.get("particle"):
                continue
            expected[int(row["index"])] = (table, row)
    trees = manifest.get("particleTrees") or {}
    found = {}
    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        if EFFECT_TAG not in tags:
            continue
        index = next((int(tag[len(SPRITE_ENTITY_TAG_PREFIX):]) for tag in tags
                      if tag.startswith(SPRITE_ENTITY_TAG_PREFIX)), -1)
        if index in found:
            errors.append("%s: two effect actors carry entity index %d" % (map_name, index))
        found[index] = actor
    matched = 0
    per_table = {}
    for index, (table, row) in sorted(expected.items()):
        actor = found.get(index)
        if actor is None:
            errors.append("%s: no effect actor for %s row %d" % (map_name, table, index))
            continue
        problems = []
        class_name = actor.get_class().get_name()
        if class_name != EFFECT_ACTOR_CLASSES[table]:
            problems.append("class %s, staged table %s wants %s" % (
                class_name, table, EFFECT_ACTOR_CLASSES[table]))
        if int(actor.get_editor_property("entity_index")) != index:
            problems.append("entity_index %d, tag %d" % (
                int(actor.get_editor_property("entity_index")), index))
        if table == "effects":
            if str(actor.get_editor_property("root")) != row["particle"]:
                problems.append("root %s, staged %s" % (
                    actor.get_editor_property("root"), row["particle"]))
            if int(actor.get_editor_property("attach_type")) != int(row["attach_type"]):
                problems.append("attach_type %d, staged %d" % (
                    int(actor.get_editor_property("attach_type")), int(row["attach_type"])))
            if str(actor.get_editor_property("parent_name") or "") != str(row.get("parentname") or ""):
                problems.append("parent_name %r, staged %r" % (
                    actor.get_editor_property("parent_name"), row.get("parentname")))
            tree = trees.get(row["particle"]) or {}
            want_leaves = int((tree.get("stats") or {}).get("leafCount", 0))
            try:
                got_leaves = int(actor.get_editor_property("tree").get_editor_property("leaf_count"))
            except Exception as error:  # noqa: BLE001 -- the property is the contract's
                got_leaves = None
                problems.append("tree unreadable (%s)" % error)
            if got_leaves is not None and got_leaves != want_leaves:
                problems.append("leaf_count %d, staged %d" % (got_leaves, want_leaves))
        if problems:
            errors.append("%s: %s row %d: %s" % (map_name, table, index, "; ".join(problems)))
        else:
            matched += 1
            per_table[table] = per_table.get(table, 0) + 1
    for index in sorted(set(found) - set(expected)):
        errors.append("%s: effect actor %s carries entity index %d, which stages no effects row"
                      % (map_name, found[index].get_actor_label(), index))
    unreal.log("[verify] effects: %d actors, %d staged rows, %d matched (%s)" % (
        len(found), len(expected), matched,
        ", ".join("%d %s" % (n, t) for t, n in sorted(per_table.items())) or "none"))
    for message in errors[:8]:
        unreal.log_error("[verify] " + message)
    if len(errors) > 8:
        unreal.log_error("[verify] ... and %d more effect finding(s) on %s"
                         % (len(errors) - 8, map_name))
    return errors


SKY_TAG = "elysium.sky"


def _staged_manifest(map_name):
    """The whole staged manifest (`map_geometry.stage_map`), or None when the pair is absent."""
    path = os.path.join(os.fspath(work_root()), "import", "map_geometry", map_name, "manifest.json")
    if not os.path.isfile(path):
        return None
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _fog_slots(component):
    """The six fog floats the bake stamped as the primitive's default custom data
    (`ElysiumFog.h`: colour 0..3, start 4, 1/range 5), or None when the slot was never written."""
    data = component.get_editor_property("custom_primitive_data")
    values = [float(v) for v in data.get_editor_property("data")] if data is not None else []
    return tuple(values[:6]) if len(values) >= 6 else None


def verify_sky_scope(actors, map_name):
    """R6.7 (`seam_map_map.md` -> "3D-skybox composition (R6.7)"), `MapsOnV2Models` maps whose
    manifest says `sky.ok`: every class the corpus places in the miniature counted back against
    the staged rows through the one transform -- sky props (`elysium.sky` static-mesh actors
    labelled `Prop_`; position `scale * (p - origin)` and actor scale `scale` per row), sky detail
    components (`elysium.detail` + `elysium.sky`, one per staged sky model group, instance scale
    `scale`), sky sprites (`elysium.sprite` + `elysium.sky`, position and scale per row) -- and
    that every sky prop and sky detail component carries the same fog slots the sky chunks do."""
    errors = []
    if not map_transport.is_map_on_v2_models(map_name):
        return errors
    manifest = _staged_manifest(map_name)
    if manifest is None:
        errors.append("%s: on MapsOnV2Models but no staged map_geometry manifest to count the "
                      "3D skybox against (run: uv run elysium export map %s)" % (map_name, map_name))
        return errors
    sky = manifest.get("sky") or {}
    if not sky.get("ok"):
        unreal.log("[verify] sky scope: %s has no sky_camera; nothing is in the miniature" % map_name)
        return errors
    scale = float(sky["scale"])
    origin = tuple(float(v) for v in sky["origin"])

    def transform(position):
        return tuple(scale * (float(position[i]) - origin[i]) for i in range(3))

    def near(a, b, tol=0.05):
        return all(abs(float(a[i]) - float(b[i])) <= tol for i in range(3))

    props_by_index = {}
    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        if SKY_TAG in tags and DETAIL_TAG not in tags and SPRITE_TAG not in tags:
            label = str(actor.get_actor_label())
            if label.startswith("Prop_"):
                props_by_index[int(label.split("_")[1])] = actor
    staged_props = {int(row["index"]): row for row in manifest.get("placements") or []
                    if row.get("sky")}
    chunk_slots = None
    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        if SKY_TAG in tags and str(actor.get_actor_label()).startswith("SM_Sky_"):
            chunk_slots = _fog_slots(actor.get_editor_property("static_mesh_component"))
            break

    matched_props = 0
    for index, row in sorted(staged_props.items()):
        actor = props_by_index.get(index)
        if actor is None:
            errors.append("%s: sky prop %d (%s) has no elysium.sky actor" % (
                map_name, index, row["stem"]))
            continue
        problems = []
        location = actor.get_actor_location()
        want = transform(row["position"])
        if not near((location.x, location.y, location.z), want):
            problems.append("at (%.1f, %.1f, %.1f), the transform says (%.1f, %.1f, %.1f)" % (
                location.x, location.y, location.z, want[0], want[1], want[2]))
        actor_scale = actor.get_actor_scale3d()
        if not near((actor_scale.x, actor_scale.y, actor_scale.z), (scale,) * 3, 1e-3):
            problems.append("scale %.2f, the miniature's is %.2f" % (actor_scale.x, scale))
        component = actor.get_editor_property("static_mesh_component") \
            if isinstance(actor, unreal.StaticMeshActor) else None
        if component is not None and chunk_slots is not None \
                and _fog_slots(component) != chunk_slots:
            problems.append("fog slots %s, the sky chunks carry %s" % (
                _fog_slots(component), chunk_slots))
        if problems:
            errors.append("%s: sky prop %d (%s): %s" % (
                map_name, index, row["stem"], "; ".join(problems)))
        else:
            matched_props += 1
    for index in sorted(set(props_by_index) - set(staged_props)):
        errors.append("%s: elysium.sky prop actor %s stands for no sky-flagged placement" % (
            map_name, props_by_index[index].get_actor_label()))

    details = manifest.get("details") or {}
    stems = {int(model["model"]): str(model["stem"]) for model in details.get("models") or []}
    staged_detail_groups = {}
    for row in details.get("records") or []:
        if row[10]:
            staged_detail_groups.setdefault(stems[int(row[1])], []).append(row)
    sky_detail_actors = {}
    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        if DETAIL_TAG in tags and SKY_TAG in tags:
            stem = next((tag[len(DETAIL_MODEL_TAG_PREFIX):] for tag in tags
                         if tag.startswith(DETAIL_MODEL_TAG_PREFIX)), "")
            sky_detail_actors[stem] = actor
    matched_details = 0
    for stem, rows in sorted(staged_detail_groups.items()):
        actor = sky_detail_actors.get(stem)
        if actor is None:
            errors.append("%s: %d sky detail record(s) of %s have no elysium.sky component" % (
                map_name, len(rows), stem))
            continue
        component = actor.get_editor_property("instances")
        problems = []
        if component is None or int(component.get_instance_count()) != len(rows):
            problems.append("%s instances, the unit stages %d" % (
                "no" if component is None else int(component.get_instance_count()), len(rows)))
        else:
            first = component.get_instance_transform(0, True)
            want = transform(rows[0][2:5])
            if not near((first.translation.x, first.translation.y, first.translation.z), want):
                problems.append("instance 0 at (%.1f, %.1f, %.1f), the transform says "
                                "(%.1f, %.1f, %.1f)" % (
                                    first.translation.x, first.translation.y,
                                    first.translation.z, want[0], want[1], want[2]))
            if not near((first.scale3d.x, first.scale3d.y, first.scale3d.z), (scale,) * 3, 1e-3):
                problems.append("instance scale %.2f, the miniature's is %.2f" % (
                    first.scale3d.x, scale))
            if chunk_slots is not None and _fog_slots(component) != chunk_slots:
                problems.append("fog slots %s, the sky chunks carry %s" % (
                    _fog_slots(component), chunk_slots))
        if problems:
            errors.append("%s: sky detail %s: %s" % (map_name, stem, "; ".join(problems)))
        else:
            matched_details += 1
    for stem in sorted(set(sky_detail_actors) - set(staged_detail_groups)):
        errors.append("%s: elysium.sky detail component %s has no sky-flagged records" % (
            map_name, stem))

    staged_sprites = {int(row["index"]): row for row in manifest.get("sprites") or []
                      if row.get("sky")}
    sky_sprite_actors = {}
    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        if SPRITE_TAG in tags and SKY_TAG in tags:
            index = next((int(tag[len(SPRITE_ENTITY_TAG_PREFIX):]) for tag in tags
                          if tag.startswith(SPRITE_ENTITY_TAG_PREFIX)), -1)
            sky_sprite_actors[index] = actor
    matched_sprites = 0
    for index, row in sorted(staged_sprites.items()):
        actor = sky_sprite_actors.get(index)
        if actor is None:
            errors.append("%s: sky env_sprite %d (%s) has no elysium.sky actor" % (
                map_name, index, row["material"]))
            continue
        problems = []
        location = actor.get_actor_location()
        want = transform(row["position"])
        if not near((location.x, location.y, location.z), want):
            problems.append("at (%.1f, %.1f, %.1f), the transform says (%.1f, %.1f, %.1f)" % (
                location.x, location.y, location.z, want[0], want[1], want[2]))
        actor_scale = actor.get_actor_scale3d()
        if not near((actor_scale.x, actor_scale.y, actor_scale.z), (scale,) * 3, 1e-3):
            problems.append("scale %.2f, the miniature's is %.2f" % (actor_scale.x, scale))
        if problems:
            errors.append("%s: sky env_sprite %d (%s): %s" % (
                map_name, index, row["material"], "; ".join(problems)))
        else:
            matched_sprites += 1
    for index in sorted(set(sky_sprite_actors) - set(staged_sprites)):
        errors.append("%s: elysium.sky sprite actor %s stands for no sky-flagged env_sprite" % (
            map_name, sky_sprite_actors[index].get_actor_label()))

    unreal.log("[verify] sky scope: scale %.0f about (%.1f, %.1f, %.1f); props %d actors / %d "
               "staged / %d matched; details %d components / %d staged groups / %d matched; "
               "sprites %d actors / %d staged / %d matched; sky chunk fog slots %s" % (
                   scale, origin[0], origin[1], origin[2],
                   len(props_by_index), len(staged_props), matched_props,
                   len(sky_detail_actors), len(staged_detail_groups), matched_details,
                   len(sky_sprite_actors), len(staged_sprites), matched_sprites,
                   "unstamped" if chunk_slots is None else
                   "start %.0f, 1/range %.6f" % (chunk_slots[4], chunk_slots[5])))
    for message in errors[:8]:
        unreal.log_error("[verify] " + message)
    if len(errors) > 8:
        unreal.log_error("[verify] ... and %d more sky-scope finding(s) on %s"
                         % (len(errors) - 8, map_name))
    return errors


def verify_ropes(world_dir, map_name):
    """R6.5 (`seam_map_material.md` -> "Ropes on `MI_`, and the factory shape"): every line of
    `<map>.ropes` names a `vtmb:material:` id that folds, by the R5.4 rule
    (`importers.materials.asset_path_for`), to an existing `MaterialInstanceConstant` package under
    `/ElysiumBaked/Materials/`. The runtime binds exactly that asset; nothing else on the line is
    a look."""
    # `importers.materials.asset_path_for` restated: that module imports numpy transitively and
    # the editor's Python has none (`bake_map_v2` restates its roots for the same reason). The
    # fold is `asset_names.safe_name` per path part, `MI_` on the stem, under this root.
    from elysium_pipeline.asset_names import safe_name

    package_root = "/ElysiumBaked/Materials"

    def asset_path_for(key):
        parts = key.split("/")
        folded = "/".join(safe_name(part) for part in parts[:-1])
        name = "MI_" + safe_name(parts[-1])
        return f"{package_root}/{folded}/{name}" if folded else f"{package_root}/{name}"

    errors = []
    path = os.path.join(world_dir, map_name + ".ropes")
    if not os.path.isfile(path):
        unreal.log("[verify] ropes: no %s.ropes (the map strings no cables)" % map_name)
        return errors
    segments = 0
    ids = {}
    with open(path, "r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            tokens = line.split()
            if not tokens:
                continue
            segments += 1
            if len(tokens) != 12 or not tokens[0].startswith("vtmb:material:"):
                errors.append("%s.ropes line %d: not a 12-token R6.5 line: %r"
                              % (map_name, line_number, line.strip()[:80]))
                continue
            ids.setdefault(tokens[0], 0)
            ids[tokens[0]] += 1
    resolved = 0
    for material_id in sorted(ids):
        package = asset_path_for(material_id[len("vtmb:material:"):])
        if not package.startswith(package_root + "/"):
            errors.append("%s: rope material %s folds outside %s: %s"
                          % (map_name, material_id, package_root, package))
            continue
        asset = unreal.EditorAssetLibrary.load_asset(package) \
            if unreal.EditorAssetLibrary.does_asset_exist(package) else None
        if asset is None:
            errors.append("%s: rope material %s -> %s is not imported "
                          "(run: uv run elysium import materials)" % (map_name, material_id, package))
            continue
        if not isinstance(asset, unreal.MaterialInstanceConstant):
            errors.append("%s: rope material %s -> %s is a %s, not a MaterialInstanceConstant"
                          % (map_name, material_id, package, asset.get_class().get_name()))
            continue
        resolved += 1
    unreal.log("[verify] ropes: %d segments over %d material(s), %d bound under %s/"
               % (segments, len(ids), resolved, package_root))
    return errors


def _atof(text):
    """C `atof`: the longest numeric prefix, 0.0 when there is none (the producer's own reader;
    `UE_map_sidecars` needs numpy and cannot be imported here)."""
    match = re.match(r"\s*[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", text or "")
    return float(match.group(0)) if match else 0.0


def verify_brush_cull(map_name, ents_path):
    """R6.4 (`seam_map_map.md` -> "Brush fade distances"): every meshed `func_lod` row carries
    `cull_max_cm == DisappearDist x 2.54` in the `.ents`, no other row carries one, and the R4.1
    entity asset (`DA_<map>_Entities`, the transport a listed map actually loads) says the same
    number at the same index."""

    errors = []
    if not os.path.isfile(ents_path):
        return errors
    with open(ents_path, "r", encoding="utf-8") as handle:
        rows = json.load(handle).get("entities", [])
    expected = {}
    for index, row in enumerate(rows):
        classname = row.get("classname", "").lower()
        keys = row.get("keys", {})
        cull = row.get("cull_max_cm")
        if classname == "func_lod" and row.get("brush_mesh"):
            distance = _atof(keys.get("DisappearDist", "0"))
            want = round(distance * 2.54, 4) if distance > 0 else None
            if cull != want:
                errors.append("%s: entity %d func_lod %s cull_max_cm %r, DisappearDist %s says %r"
                              % (map_name, index, row.get("brush_mesh"), cull,
                                 keys.get("DisappearDist"), want))
            if want is not None:
                expected[index] = want
        elif cull is not None:
            errors.append("%s: entity %d (%s) carries cull_max_cm %r but is not a meshed func_lod"
                          % (map_name, index, classname, cull))
    asset_path = "%s/%s/DA_%s_Entities" % (MOUNT, map_name, map_name)
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    asset_rows = asset.get_editor_property("entities") if asset else None
    matched = 0
    if asset_rows is None:
        if map_transport.is_map_on_new_transport(map_name):
            errors.append("%s: on MapsOnNewTransport but %s does not load" % (map_name, asset_path))
    elif len(asset_rows) != len(rows):
        errors.append("%s: %s has %d rows, %s has %d" % (
            map_name, asset_path, len(asset_rows), os.path.basename(ents_path), len(rows)))
    else:
        for index, row in enumerate(asset_rows):
            got = float(row.get_editor_property("cull_max_cm"))
            want = expected.get(index, 0.0)
            if abs(got - want) > 1e-3:
                errors.append("%s: %s row %d cull_max_cm %.4f, .ents says %.4f"
                              % (map_name, asset_path, index, got, want))
            elif index in expected:
                matched += 1
    unreal.log("[verify] brush cull: %d func_lod row(s) with a cull range in %s.ents, %d matched in %s"
               % (len(expected), map_name, matched, asset_path))
    for message in errors:
        unreal.log_error("[verify] " + message)
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
    expected_parent = MATERIALS + "/M_World_Glass.M_World_Glass"
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
    expected_refract_parent = MATERIALS + "/M_Refract.M_Refract"
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
    errors.extend(verify_brush_cull(map_name, ents_path))
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
        errors.extend(verify_lights_baked(actors, world_dir, map_name))
        errors.extend(verify_details(actors, map_name))
        errors.extend(verify_sprites(actors, map_name))
        errors.extend(verify_effects(actors, map_name))
        errors.extend(verify_sky_scope(actors, map_name))
        errors.extend(verify_ropes(world_dir, map_name))
        errors.extend(verify_captures(
            actors, unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()))
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
