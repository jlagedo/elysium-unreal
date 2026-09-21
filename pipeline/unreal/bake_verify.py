# Reports what a map's bake actually produced, from the assets themselves rather than from
# the bake's own log: asset counts per class, Nanite coverage, triangles, material-slot
# binding, and the level's actor census. R7.4 (water-complete) adds the water lanes: the
# underside twins, the light-style chunk tags and the water volumes' fluid rows.
# Phase 3 (the content run) is what proves those lanes on disk: verify_water hard-fails the
# bake rather than warning, so a missing twin or an unbound '#underside' section stops the map.
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
from elysium_pipeline import mounts  # noqa: E402
from elysium_pipeline.asset_names import brush_slot_style  # noqa: E402
from elysium_pipeline.paths import export_root, work_root  # noqa: E402
from elysium_pipeline.validation.dds_alpha import DdsAlphaError, alpha_minimum  # noqa: E402
from elysium_pipeline.validation.png_alpha import alpha_range  # noqa: E402
from pipeline.unreal import bake_lib as bl  # noqa: E402

MOUNT = mounts.BAKED
from elysium_pipeline.asset_paths import map_package
MATERIALS = mounts.MATERIALS


#: 0018 story 21-4: `-BakeMapSidecars=<root>` names where the producer wrote this run's sidecars,
#: one directory per map below it. `bake map` passes the same flag to both commandlets, so the
#: verifier reads the files the bake read. Empty (a hand-run verify with no flag) falls back to
#: the legacy export root, which is where they used to stand.
SIDECAR_ROOT_FLAG = "BakeMapSidecars"


def _sidecar_dir(map_name):
    root = arg(SIDECAR_ROOT_FLAG, "")
    return os.path.join(root or os.fspath(export_root()), map_name)


#: `map_geometry.UNDERSIDE_SUFFIX` / `LIGHTSTYLE_SUFFIX` restated (that module imports numpy, which
#: the editor's Python has not). Both name a per-face BINDING the bake makes on a section, so a
#: suffixed group key is the same material as its unsuffixed sibling.
UNDERSIDE_SUFFIX = "#underside"
LIGHTSTYLE_SUFFIX = "#style"


def _material_group_key(key):
    """`map_geometry.split_section_key`'s first element: the material group key, R7.4's two
    per-face suffixes stripped."""
    head, sep, tail = key.rpartition(LIGHTSTYLE_SUFFIX)
    if sep and tail.isdigit():
        key = head
    if key.endswith(UNDERSIDE_SUFFIX):
        key = key[: -len(UNDERSIDE_SUFFIX)]
    return key


_STAGED_MODELS = None


def _staged_models():
    """The model lane's own manifest, indexed by R1 stem.

    `$ELYSIUM_WORK_ROOT/import/models/manifest.json` is what `uv run elysium import models`
    wrote: one row per staged `SM_`, carrying the material slots it binds. 0018 story 21-5 made
    this the answer to "which materials does a placed model draw" -- the question the shared
    corpus's `shared/props/<stem>.mtl` used to answer, on a corpus that no longer exists.

    Empty when the lane has not staged on this machine; the caller names that as a missing input.
    """
    global _STAGED_MODELS
    if _STAGED_MODELS is None:
        path = os.path.join(os.fspath(work_root()), "import", "models", "manifest.json")
        rows = []
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8") as handle:
                rows = json.load(handle).get("assets") or []
        _STAGED_MODELS = {str(row.get("stem") or ""): row for row in rows}
        _STAGED_MODELS.pop("", None)
    return _STAGED_MODELS


def _map_prop_material_units(map_name):
    """`{material unit key: owner label}` for every model THIS map places.

    The corpus holds every model in the install. A map is answerable for the ones it places, so
    the join runs its own stems against the model lane's manifest rather than walking all of it.
    The stems come from the staged `placements` rows and the staged entity rows.

    0018 story 21-5 replaced the `shared/props/<stem>.mtl` read this used to be. The `.mtl` was
    the decoder's per-model material list; the model unit's own `slots[]` is the same list,
    already resolved, and it is what the bake actually binds. The owner label keeps the shape the
    `.mtl` join produced, so a material the lane never staged is still reported against the model
    and slot that wanted it.
    """
    stems = set()
    manifest = _staged_manifest(map_name) or {}
    for placement in manifest.get("placements") or []:
        if placement.get("stem"):
            stems.add(placement["stem"])
    ents = os.path.join(_sidecar_dir(map_name), map_name + ".ents")
    if os.path.isfile(ents):
        with open(ents, "r", encoding="utf-8", errors="replace") as handle:
            for entity in json.load(handle).get("entities", []):
                if entity.get("model_mesh"):
                    stems.add(entity["model_mesh"])
    models = _staged_models()
    units = {}
    for stem in sorted(stems):
        row = models.get(stem)
        if row is None:
            continue
        for slot in row.get("slots") or []:
            unit = str(slot.get("materialId") or "")
            if not unit.startswith("vtmb:material:"):
                # A `vtmb:missing-material:` slot is the model lane's own sentinel and is
                # already reported there; it names no unit for this lane to ask about.
                continue
            units.setdefault(unit[len("vtmb:material:"):],
                             "%s/%s" % (stem, slot.get("slotName") or slot.get("index")))
    return units


def _wetness_scale(row):
    """A staged material row's `$globalwetness` scalar, or `None` when it is not wetness-driven.

    The material lane writes `WetnessDriven`/`WetnessScale` into the instance's own scalar
    overrides, so the staged manifest answers what `shared/materials.json`'s `MatDef.wet` and
    `MatDef.wetness_scale` answered before 0018 story 21-5 retired that corpus.
    """
    scalars = (row or {}).get("scalars") or {}
    if not scalars.get("WetnessDriven"):
        return None
    value = scalars.get("WetnessScale")
    return None if value is None else float(value)


# `_world_materials` is gone (0018 story 21-4). It read `<map>.mtl` and `<map>.materials.json`
# under the legacy export root to give the hub's wetness check the map's own surfaces, and it was
# the last thing in this lane that needed a legacy directory -- the check reported an empty corpus
# the first time the hub was baked without one. The surfaces are the STAGED material table's now,
# joined to the corpus on each row's `provenance` (see `verify_sm_hub_1_weather`). The map-local
# document is owed no replacement: exactly one map in the export tree ever had one (`sm_pier_1`,
# which this group delisted), and the V2 lane stages a PAKFILE-only material as a first-class unit
# keyed `maps/<map>/...` with its own `patchBase` chain.


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


def verify_sm_hub_1_weather(package, map_name):
    """Verify the authored data contract and the one generated UE weather presentation path.

    0018 story 21-4: the contract is read from the STAGED weather block rather than from
    `<map>.weather.json` under the export root. Every assertion below is unchanged -- two
    `attach_type` 11 emitters, the pinned footprint, the R16 metadata, the height texture and the
    per-map instances -- and the footprint is the sharpest of them: it is what says the ported
    cover built its raster over the same world the decoder did.
    """
    errors = []

    def fail(message):
        unreal.log_error("[verify] weather: " + message)
        errors.append("weather: " + message)

    staged = (_staged_manifest(map_name) or {}).get("weather")
    if not staged:
        fail("no staged weather payload (run: uv run elysium bake map --maps %s --force)"
             % map_name)
        return errors
    weather = staged.get("document") or {}
    if weather.get("schema") != "elysium.map-weather" or weather.get("version") != 1:
        fail("staged payload schema/version is not elysium.map-weather v1")
    emitters = weather.get("emitters", [])
    if len(emitters) != 2 or any(
            item.get("particle_definition") != "rain_follow_emitter"
            or item.get("attach_type") != 11 for item in emitters):
        fail("the payload does not contain exactly two attach_type=11 rain_follow emitters")
    bounds = weather.get("world_bounds_cm", {})
    minimum = bounds.get("min", [])
    maximum = bounds.get("max", [])
    if len(minimum) != 3 or len(maximum) != 3:
        fail("the payload's world bounds are incomplete")
    else:
        footprint = (maximum[0] - minimum[0], maximum[1] - minimum[1])
        if abs(footprint[0] - 28971.24) > 0.01 or abs(footprint[1] - 19639.28) > 0.01:
            fail("footprint drifted: %.3f x %.3f cm" % footprint)
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

    # 0018 story 21-4 moved the world half off `<map>.mtl` and onto the STAGED material table,
    # which is what the map binds. 0018 story 21-5 moved the SCALAR off `shared/materials.json`
    # and onto the material lane's own manifest, which is where it was going to end up anyway:
    # the lane writes `WetnessDriven`/`WetnessScale` into the instance's scalar overrides, so the
    # staged row states the number the corpus `MatDef` used to. Nothing here reads a corpus.
    #
    # The world dedupe is on the group key with R7.4's `#underside`/`#style<n>` suffixes
    # stripped, because the `.mtl` had one entry per `<material>@<cubemap>` group and those
    # suffixes split a group without adding a material. The prop half is the material units the
    # map's placed models bind, which the model lane's `slots[]` names.
    wetness_values = []
    _by_asset, by_unit = _staged_materials()
    staged_materials = (_staged_manifest("sm_hub_1") or {}).get("materials") or {}
    by_group = {}
    for key, row in sorted(staged_materials.items()):
        by_group.setdefault(_material_group_key(key), row.get("provenance"))
    # One value per GROUP, not per unit: two cubemap-patched copies of one material were two
    # `.mtl` entries and are two staged groups, and the expected 14 counts them that way.
    for provenance in by_group.values():
        scale = _wetness_scale(by_unit.get(provenance)) if provenance else None
        if scale is not None:
            wetness_values.append(scale)
    for unit in _map_prop_material_units("sm_hub_1"):
        scale = _wetness_scale(by_unit.get(unit))
        if scale is not None:
            wetness_values.append(scale)
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
    """R5.6, light-count parity against the legacy `.lights`, and the
    four derivation assertions re-homed from `Elysium.Substrate.LightRig` onto the bake's own
    output.

    Parity: one `elysium.light` actor per `.lights` row that places one (type 0-3, `max(rgb) > 0`),
    every actor's `elysium.src` resolving to exactly one such row, no row placed twice. The
    sidecar is the independent witness here -- it is written by the same `light_rows` the staged
    table is, so a disagreement means the editor half dropped or doubled a row.

    Baked values: non-inverse-square falloff, MegaLights allowed with the RT shadow method on every
    local light, shadows per the lighting page's flags and the type (a texlight never), and the
    `elysium.type`/`elysium.style` tags equal to the row's -- the two facts the slim rig reads.
    """
    errors = []
    path = os.path.join(world_dir, "%s.lights" % map_name)
    if not os.path.isfile(path):
        errors.append("%s: no %s.lights to check parity against"
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
    """R6.3, every
    `elysium.detail` actor's instanced component counted back against the staged `details`
    table -- one component per `(model, sky)` group, the same instance count, no model missing and
    none extra -- plus the cull range the Models page names and exactly one custom-data float per
    component (the `swayAmount / 255` slot)."""
    errors = []
    details = _staged_details(map_name)
    if details is None:
        errors.append("%s: no staged map_geometry manifest to count "
                      "detail props against (run: uv run elysium export map %s)"
                      % (map_name, map_name))
        return errors
    from pipeline.unreal.model_catalogue_views import model_id
    stems = {int(model["model"]): model_id(model["modelPath"]) for model in details.get("models") or []}
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
    """R6.1, every
    `elysium.sprite` actor matched to its staged row by the entity index tag -- size, colour,
    mode, orientation, the spawn-hidden state, and the material child's parent and blend -- no
    row missing and no actor extra."""
    errors = []
    rows = _staged_sprites(map_name)
    if rows is None:
        errors.append("%s: no staged map_geometry manifest to count "
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
    """R7.3, every
    `elysium.effect` actor matched to its staged row by the entity index tag -- the class per
    table, and for an `effects[]` row the root, the attach data and the tree's leaf count -- no
    row missing (an unresolved root places no actor, by VtMB's own rule) and no actor extra."""
    errors = []
    manifest = _staged_manifest(map_name)
    if manifest is None:
        errors.append("%s: no staged map_geometry manifest to count "
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


WATER_TAG = "elysium.water"
# R7.4 (water-complete): the world chunk tag a styled section carries beside `SKY_TAG` (below --
# both restate `bake_map.TAG_WORLD`/`TAG_SKY`, `chunk_style_suffix`'s consumers) and the
# style-index prefix `bake_map.LIGHT_STYLE_TAG_PREFIX` restates (`ElysiumBakedTags::LightStyle`'s
# own format).
WORLD_TAG = "elysium.world"
LIGHT_STYLE_TAG_PREFIX = "elysium.style="


#: 0018 story 2: family tag -> the actor class the bake places for it (`bake_ai_infra.FAMILIES`,
#: restated: that module imports `unreal` at load and this one is imported by pure tests).
AI_INFRA_CLASSES = {
    "elysium.infra.hint": "ElysiumHintActor",
    "elysium.infra.place": "ElysiumInterestingPlaceActor",
    "elysium.infra.conversation": "ElysiumConversationPlaceActor",
    "elysium.infra.maker": "ElysiumNpcMakerActor",
    "elysium.infra.npc": "ElysiumNpcPlacementActor",
}
AI_INFRA_INDEX_TAG = "elysium.infra.index"
AI_INFRA_FAMILY_TAGS = {"hint": "elysium.infra.hint", "place": "elysium.infra.place",
                        "conversation": "elysium.infra.conversation",
                        "maker": "elysium.infra.maker", "npc": "elysium.infra.npc"}


def ai_infra_errors(map_name, placed, payload, index_count):
    """The declared-set check, pure: `placed` is one `(class_name, tags, entity_index,
    source_classname)` per actor carrying an infrastructure family tag, `payload` the staged
    `aiInfra` block, `index_count` how many index actors stand. Every staged row must stand exactly
    once, as its family's class, carrying its family tag and `elysium.ent=<index>`, with the staged
    classname; nothing unstaged may stand."""
    errors = []
    if index_count != 1:
        errors.append("%s: %d AI infrastructure index actors, want exactly one" % (map_name, index_count))
    staged = {int(row["index"]): row for row in payload.get("rows") or []}
    seen = {}
    for class_name, tags, entity_index, source_classname in placed:
        family_tags = [tag for tag in tags if tag in AI_INFRA_CLASSES]
        if len(family_tags) != 1:
            errors.append("%s: %s carries %d infrastructure family tags" % (map_name, class_name, len(family_tags)))
            continue
        if AI_INFRA_CLASSES[family_tags[0]] != class_name:
            errors.append("%s: entity %s is a %s under %s" % (map_name, entity_index, class_name, family_tags[0]))
        if "elysium.ent=%d" % entity_index not in tags:
            errors.append("%s: entity %s: its elysium.ent tag does not name it" % (map_name, entity_index))
        if entity_index in seen:
            errors.append("%s: entity %s placed twice" % (map_name, entity_index))
            continue
        seen[entity_index] = family_tags[0]
        row = staged.get(entity_index)
        if row is None:
            errors.append("%s: entity %s stands but was not staged" % (map_name, entity_index))
            continue
        if AI_INFRA_FAMILY_TAGS[row["family"]] != family_tags[0]:
            errors.append("%s: entity %s staged as %s, placed under %s"
                          % (map_name, entity_index, row["family"], family_tags[0]))
        if row["classname"] != source_classname:
            errors.append("%s: entity %s staged as %s, placed as %s"
                          % (map_name, entity_index, row["classname"], source_classname))
    missing = sorted(set(staged) - set(seen))
    if missing:
        errors.append("%s: %d staged AI infrastructure row(s) have no actor (first: %s)"
                      % (map_name, len(missing), missing[:5]))
    return errors


def verify_ai_infra(actors, map_name):
    """0018 story 2, the placed infrastructure actors are exactly the
    staged `aiInfra` rows (`ai_infra_errors`)."""
    manifest = _staged_manifest(map_name)
    if manifest is None or manifest.get("aiInfra") is None:
        return ["%s: no staged aiInfra rows to check the level against "
                "(run: uv run elysium bake map --maps %s --force)" % (map_name, map_name)]
    placed, index_count = [], 0
    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        if AI_INFRA_INDEX_TAG in tags:
            index_count += 1
        if any(tag in AI_INFRA_CLASSES for tag in tags):
            placed.append((actor.get_class().get_name(), tags,
                           int(actor.get_editor_property("entity_index")),
                           str(actor.get_editor_property("source_classname"))))
    errors = ai_infra_errors(map_name, placed, manifest["aiInfra"], index_count)
    unreal.log("[verify] AI infrastructure: %d actors, %d staged rows, %d problem(s)"
               % (len(placed), len(manifest["aiInfra"].get("rows") or []), len(errors)))
    return errors


def verify_water(actors, map_name):
    """R7.1, exactly one
    `elysium.water` actor iff the map stages a `water.volumes[]` row, the actor's row count, and
    each row's `surface_z_cm` and brush count against the staged manifest -- no volume the actor
    disagrees with the stage on, and no actor at all when the stage places none.

    R7.4 (water-complete, contract 4) extends the per-volume comparison to `fluid` (`has_fluid`
    plus every scalar the row carries, when the row carries one -- an unstaged fluid is not an
    error) and the `pieces` / `leaf_boxes_cm` / `near_boxes_cm` counts. Two more checks run once
    per map, independent of the actor: every `_Underside` twin a staged `materials` row names
    (contract 1) actually resolves to an asset, and every lightstyle a staged `materials` row
    carries (contract 3) has at least one world/sky chunk actor tagged for it -- `bake_map.py`'s
    own `chunk_style_suffix`/`parse_chunk_style` pairing, checked from the placed level rather than
    re-parsing a chunk name here. A third asks whether the water sections BIND what the stage said
    they would (`_verify_water_section_bindings`), which is the half of contract 1 an existence
    check cannot see.
    """
    errors = []
    manifest = _staged_manifest(map_name)
    if manifest is None:
        errors.append("%s: no staged map_geometry manifest to count "
                      "water against (run: uv run elysium export map %s)" % (map_name, map_name))
        return errors
    rows = list((manifest.get("water") or {}).get("volumes") or [])
    found = [actor for actor in actors if WATER_TAG in [str(tag) for tag in actor.tags]]
    if len(found) > 1:
        errors.append("%s: %d water actors carry %s, want at most one"
                      % (map_name, len(found), WATER_TAG))
    if not rows:
        if found:
            errors.append(
                "%s: %d water actor(s) placed but the stage carries no water.volumes[] row"
                % (map_name, len(found)))
    elif not found:
        errors.append("%s: %d staged water.volumes[] row(s) but no %s actor"
                      % (map_name, len(rows), WATER_TAG))
        unreal.log("[verify] water: %d staged rows, 0 actors" % len(rows))
    else:
        actor = found[0]
        volumes = list(actor.get_editor_property("volumes"))
        if len(volumes) != len(rows):
            errors.append("%s: water actor carries %d volume(s), staged %d"
                          % (map_name, len(volumes), len(rows)))
        matched = 0
        for index, (volume, row) in enumerate(zip(volumes, rows)):
            problems = _verify_water_volume(volume, row)
            if problems:
                errors.append("%s: water volume %d: %s" % (map_name, index, "; ".join(problems)))
            else:
                matched += 1
        unreal.log("[verify] water: %d staged rows, %d matched" % (len(rows), matched))

    meshes = brush_meshes(map_name)
    errors.extend(_verify_water_undersides(manifest, map_name))
    errors.extend(_verify_water_section_bindings(actors, meshes, manifest, map_name))
    errors.extend(_verify_water_lightstyle_tags(actors, meshes, manifest, map_name))

    for message in errors[:8]:
        unreal.log_error("[verify] " + message)
    if len(errors) > 8:
        unreal.log_error("[verify] ... and %d more water finding(s) on %s"
                         % (len(errors) - 8, map_name))
    return errors


def _verify_water_volume(volume, row):
    """One `(actor volume, staged row)` pair's problems -- R7.1's original four fields plus the
    R7.4 contract 4 ones, each skipped when the staged row carries nothing to check (an older
    manifest, or a volume the compiler authored no fluid/pieces/boxes for)."""
    problems = []
    got_index = int(volume.get_editor_property("index"))
    want_index = int(row["index"])
    if got_index != want_index:
        problems.append("index %d, staged %d" % (got_index, want_index))
    got_z = float(volume.get_editor_property("surface_z_cm"))
    want_z = float(row["surfaceZCm"])
    if abs(got_z - want_z) > 1e-2:
        problems.append("surface_z_cm %.2f, staged %.2f" % (got_z, want_z))
    got_brushes = len(list(volume.get_editor_property("brushes")))
    want_brushes = len(row.get("brushes") or [])
    if got_brushes != want_brushes:
        problems.append("%d brush(es), staged %d" % (got_brushes, want_brushes))

    fluid_row = row.get("fluid")
    if fluid_row is not None:
        fluid = volume.get_editor_property("fluid")
        if not bool(fluid.get_editor_property("has_fluid")):
            problems.append("fluid: staged index %s but the actor's has_fluid is false"
                            % fluid_row.get("index"))
        else:
            got_fluid_index = int(fluid.get_editor_property("index"))
            want_fluid_index = int(fluid_row.get("index") or 0)
            if got_fluid_index != want_fluid_index:
                problems.append("fluid.index %d, staged %d" % (got_fluid_index, want_fluid_index))
            got_density = float(fluid.get_editor_property("density"))
            want_density = float(fluid_row.get("density") or 0.0)
            if abs(got_density - want_density) > 1e-3:
                problems.append("fluid.density %.3f, staged %.3f" % (got_density, want_density))
    for field, prop in (("pieces", "pieces"), ("leafBoxesCm", "leaf_boxes_cm"),
                        ("nearBoxesCm", "near_boxes_cm")):
        staged = row.get(field)
        if staged is None:
            continue
        got_count = len(list(volume.get_editor_property(prop)))
        want_count = len(staged)
        if got_count != want_count:
            problems.append("%d %s, staged %d" % (got_count, field, want_count))
    return problems


def _verify_water_undersides(manifest, map_name):
    """Contract 1: every staged `materials` row naming an `undersideAsset` resolves to a real
    asset -- the `_Underside` twin Lane C stages beside the surface instance, which
    `bake_map_v2._V2Material.slot_asset` binds a down-facing water face's own mesh section to."""
    errors = []
    materials = manifest.get("materials") or {}
    twins = sorted({str(row["undersideAsset"]) for row in materials.values()
                    if row.get("underside") and row.get("undersideAsset")})
    missing = [path for path in twins if not unreal.EditorAssetLibrary.does_asset_exist(path)]
    if missing:
        errors.append("%s: %d underside twin instance(s) staged but not imported "
                      "(run: uv run elysium import materials): %s"
                      % (map_name, len(missing), ", ".join(missing[:8])))
    unreal.log("[verify] water undersides: %d twin(s) staged, %d missing"
              % (len(twins), len(missing)))
    return errors


def brush_meshes(map_name):
    """`{stem: UStaticMesh}` for every baked brush-entity mesh of this map (`/<map>/Brushes`).

    A brush entity's mesh is never placed in the level, so it is reached through the package rather
    than through an actor walk -- and it is a first-class carrier of both facts the water checks
    below ask about: `map_geometry` stages `water.faces[]` rows and `#style<n>` groups in BRUSH
    scenes as readily as in the world one (`read_geometry` runs the same `_build_scene` over every
    brush model), so a check that walks chunk actors alone reports a correctly baked brush section
    as missing.
    """
    meshes = {}
    package = map_package(map_name) + "/Brushes"
    if not unreal.EditorAssetLibrary.does_directory_exist(package):
        return meshes
    for path in unreal.EditorAssetLibrary.list_assets(package, recursive=False,
                                                      include_folder=False):
        name = str(path).split("/")[-1].split(".")[0]
        if not name.startswith("SM_"):
            continue
        mesh = unreal.EditorAssetLibrary.load_asset(path)
        if mesh is not None:
            meshes[name[3:]] = mesh
    return meshes


def _slot_names(mesh):
    """The mesh's material slot names, as the strings `asset_names.brush_slot_style` reads."""
    return [str(slot.get_editor_property("material_slot_name"))
            for slot in mesh.get_editor_property("static_materials")]


def _verify_water_lightstyle_tags(actors, meshes, manifest, map_name):
    """Contract 3: every lightstyle a staged `materials` row carries is reachable by the light
    rig's clock -- through a world/sky chunk actor tagged `elysium.style=<n>` for a WORLD face, and
    through the brush mesh's own `_style<n>` slot names for a BRUSH-ENTITY face
    (`ElysiumLightStyle::StyleFromSlotNames`, read back by
    `UElysiumMapVisuals::RegisterRuntimeBrush`). `sm_pier_1`'s 17 `objects/surf` foam bodies are
    the second kind and are the census's motivating case for G6, so a check that asked only about
    chunk actors would have passed while delivering nothing on them.
    """
    errors = []
    materials = manifest.get("materials") or {}
    styles = sorted({int(row["lightStyle"]) for row in materials.values()
                     if row.get("lightStyle")})
    if not styles:
        return errors
    tagged = set()
    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        if WORLD_TAG not in tags and SKY_TAG not in tags:
            continue
        for tag in tags:
            if tag.startswith(LIGHT_STYLE_TAG_PREFIX):
                tagged.add(int(tag[len(LIGHT_STYLE_TAG_PREFIX):]))
    styled_brushes = {stem: brush_slot_style(_slot_names(mesh))
                      for stem, mesh in meshes.items()}
    on_brushes = {style for style in styled_brushes.values() if style}
    missing = [style for style in styles if style not in tagged and style not in on_brushes]
    if missing:
        errors.append("%s: lightstyle(s) %s staged on a water/foam face but no world/sky chunk "
                      "actor carries %s<n> and no brush mesh names it in a slot"
                      % (map_name, missing, LIGHT_STYLE_TAG_PREFIX))
    styled_count = sum(1 for style in styled_brushes.values() if style)
    unreal.log("[verify] water lightstyles: %d staged, %d chunk-tagged, %d on brush meshes "
               "(%d styled brush mesh(es))"
               % (len(styles), len(tagged), len(on_brushes), styled_count))
    return errors


def chunk_bound_materials(actors, meshes=None):
    """Every material asset path a world/sky chunk -- or a brush-entity mesh -- binds on this map.

    `bake_map_v2` binds one section per staged face group, so the set this returns is exactly the
    set of instances the map's surfaces actually draw through. BOTH carriers are walked, because a
    `water.faces[]` row names the scene it came from and `read_geometry` stages brush models
    through the same `_build_scene` the world goes through: `sm_pier_1`'s water is all in the world
    scene, but a map whose swimmable brush belongs to a `func_` entity puts its group on
    `/<map>/Brushes/SM_brush_<n>` and nowhere else. Object paths are folded to their package path
    (`/ElysiumBaked/Materials/water/MI_invisible_water`), which is the spelling the stage's
    `materials` rows use.
    """
    bound = set()

    def take(mesh):
        if mesh is None:
            return
        for slot in mesh.get_editor_property("static_materials"):
            material = slot.get_editor_property("material_interface")
            if material is not None:
                bound.add(str(material.get_path_name()).split(".", 1)[0])

    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        if WORLD_TAG not in tags and SKY_TAG not in tags:
            continue
        component = getattr(actor, "static_mesh_component", None)
        take(component.get_editor_property("static_mesh") if component else None)
    for mesh in (meshes or {}).values():
        take(mesh)
    return bound


def _verify_water_section_bindings(actors, meshes, manifest, map_name):
    """Contract 1, on the BAKED asset: every water face group's own instance is bound into a chunk
    -- or, for a brush-entity scene, into that entity's own mesh.

    `_verify_water_undersides` above asks whether the `_Underside` twin was imported; this asks
    the harder half -- whether the down-facing sections actually BIND it, rather than falling back
    to the surface instance and drawing a reflection under the waterline. The expected path per
    group is `map_geometry.MaterialBinding.slot_asset`'s own rule (`undersideAsset or asset`; a
    water unit is never a decal surface), and the groups are the ones `water.faces[]` names, so a
    group that meshed nothing is not asked about.

    The geometric half of the pin -- the area of each section against the compiler's own
    `faces[].area` (G26/verdict B3) -- is answered on the STAGED side, where both numbers exist:
    every `water.faces[]` row carries `areaCm2` (vbsp's) beside `meshedAreaCm2` (the stage's), and
    `test_the_water_face_split_on_the_exported_corpus` pins them equal per face and per section.
    Asking a baked `UStaticMesh` for a per-section area would re-measure the same triangles through
    a GeometryScript query that cannot run outside a live editor, for no fact the offline pin does
    not already have.
    """
    errors = []
    materials = manifest.get("materials") or {}
    groups = sorted({str(row["group"]) for row in
                     ((manifest.get("water") or {}).get("faces") or [])})
    if not groups:
        return errors
    expected = {}
    for group in groups:
        row = materials.get(group)
        if row is None:
            errors.append("%s: water face group %r has no materials row to bind"
                          % (map_name, group))
            continue
        expected[group] = str(row.get("undersideAsset") or row.get("asset") or "")
    bound = chunk_bound_materials(actors, meshes)
    missing = sorted({group for group, path in expected.items()
                      if path and path not in bound})
    if missing:
        errors.append(
            "%s: %d water face group(s) whose instance no world/sky chunk and no brush mesh "
            "binds: %s"
            % (map_name, len(missing),
               ", ".join("%s -> %s" % (group, expected[group]) for group in missing[:6])))
    unreal.log("[verify] water sections: %d group(s), %d bound on a chunk or brush mesh"
              % (len(expected), len(expected) - len(missing)))
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
    """R6.7, maps whose
    manifest says `sky.ok`: every class the corpus places in the miniature counted back against
    the staged rows through the one transform -- sky props (`elysium.sky` static-mesh actors
    labelled `Prop_`; position `scale * (p - origin)` and actor scale `scale` per row), sky detail
    components (`elysium.detail` + `elysium.sky`, one per staged sky model group, instance scale
    `scale`), sky sprites (`elysium.sprite` + `elysium.sky`, position and scale per row) -- and
    that every sky prop and sky detail component carries the same fog slots the sky chunks do."""
    errors = []
    manifest = _staged_manifest(map_name)
    if manifest is None:
        errors.append("%s: no staged map_geometry manifest to count the "
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


#: 0018 story 21-3: the tags `bake_ropes.author` stamps on a baked cable, restated here because
#: that module imports `unreal` at load and this half is imported by pure tests.
ROPE_TAG = "elysium.rope"
ROPE_SOURCE_PREFIX = "elysium.src="
ROPE_MATERIAL_PREFIX = "vtmb:material:"
#: A cm tolerance for the float round trip through the actor's reflected properties.
ROPE_EPSILON = 0.01


def rope_errors(map_name, placed, payload):
    """The baked-cable check, pure: `placed` is one `(tags, facts)` per actor carrying the rope tag,
    where `facts` is the eight-key dict the actor's reflected properties answer, and `payload` the
    staged `ropes` block. Every staged row must stand exactly once, carrying `elysium.src=<index>`
    and every one of its eight facts; nothing unstaged may stand.

    Eight, not seven: `flags` is what the runtime turns into `UCableComponent::bAttachEnd` (bit 0,
    `Dangling`, clears `ROPE_LOCK_END_POINT`), so a lost flag word unpins every hanging end."""
    errors = []
    staged = {int(row["index"]): row for row in payload.get("rows") or []}
    seen = set()
    for tags, facts in placed:
        index = None
        for tag in tags:
            if tag.startswith(ROPE_SOURCE_PREFIX):
                index = int(tag[len(ROPE_SOURCE_PREFIX):])
        if index is None:
            errors.append("%s: a rope actor carries no %s tag" % (map_name, ROPE_SOURCE_PREFIX))
            continue
        if index in seen:
            errors.append("%s: rope %d placed twice" % (map_name, index))
            continue
        seen.add(index)
        row = staged.get(index)
        if row is None:
            errors.append("%s: rope %d stands but was not staged" % (map_name, index))
            continue
        if facts["materialId"] != row["materialId"]:
            errors.append("%s: rope %d staged as %s, placed as %s"
                          % (map_name, index, row["materialId"], facts["materialId"]))
        for key in ("aCm", "bCm"):
            for axis in range(3):
                if abs(facts[key][axis] - row[key][axis]) > ROPE_EPSILON:
                    errors.append("%s: rope %d %s differs: staged %s, placed %s"
                                  % (map_name, index, key, row[key], facts[key]))
                    break
        for key in ("widthCm", "restCm", "texScale"):
            if abs(float(facts[key]) - float(row[key])) > ROPE_EPSILON:
                errors.append("%s: rope %d %s differs: staged %s, placed %s"
                              % (map_name, index, key, row[key], facts[key]))
        for key in ("nodes", "flags"):
            if int(facts[key]) != int(row[key]):
                errors.append("%s: rope %d %s differs: staged %s, placed %s"
                              % (map_name, index, key, row[key], facts[key]))
    missing = sorted(set(staged) - seen)
    if missing:
        errors.append("%s: %d staged rope row(s) have no actor (first: %s)"
                      % (map_name, len(missing), missing[:5]))
    return errors


def rope_material_errors(map_name, material_ids):
    """R6.5, still asked: every id a cable binds folds, by the R5.4 rule
    (`importers.materials.asset_path_for`), to an existing `MaterialInstanceConstant` package under
    `/ElysiumBaked/Materials/`. The runtime binds exactly that asset; nothing else on the row is a
    look."""
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

    errors, resolved = [], 0
    for material_id in sorted(material_ids):
        if not material_id.startswith(ROPE_MATERIAL_PREFIX):
            errors.append("%s: rope material %r is not a %s id"
                          % (map_name, material_id, ROPE_MATERIAL_PREFIX))
            continue
        package = asset_path_for(material_id[len(ROPE_MATERIAL_PREFIX):])
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
    return errors, resolved


def verify_ropes(actors, map_name):
    """0018 story 21-3: the placed rope actors are exactly the staged `ropes` rows (`rope_errors`),
    and every material they bind is imported (`rope_material_errors`). Before this story the
    runtime parsed `<map>.ropes` off the export root at map load and no check ever asked whether the
    level carried the cables at all."""
    manifest = _staged_manifest(map_name)
    if manifest is None or manifest.get("ropes") is None:
        return ["%s: no staged rope rows to check the level against "
                "(run: uv run elysium bake map --maps %s --force)" % (map_name, map_name)]
    placed = []
    for actor in actors:
        tags = [str(tag) for tag in actor.tags]
        if ROPE_TAG not in tags:
            continue
        rope = actor.get_editor_property("rope")
        a = rope.get_editor_property("a")
        b = rope.get_editor_property("b")
        placed.append((tags, {
            "materialId": str(rope.get_editor_property("material_id")),
            "aCm": [a.x, a.y, a.z],
            "bCm": [b.x, b.y, b.z],
            "widthCm": rope.get_editor_property("width_cm"),
            "restCm": rope.get_editor_property("rest_cm"),
            "nodes": rope.get_editor_property("nodes"),
            "texScale": rope.get_editor_property("tex_scale"),
            "flags": rope.get_editor_property("flags"),
        }))
    errors = rope_errors(map_name, placed, manifest["ropes"])
    material_errors, resolved = rope_material_errors(
        map_name, {facts["materialId"] for _, facts in placed})
    errors.extend(material_errors)
    unreal.log("[verify] ropes: %d cable actors, %d staged rows, %d material(s) bound, "
               "%d problem(s)" % (len(placed), len(manifest["ropes"].get("rows") or []),
                                  resolved, len(errors)))
    return errors


#: 0018 story 21-4: the tag `_place_decals` stamps on a baked projector, and the id prefix a
#: staged row names its material by. Restated here rather than imported, as the rope lane restates
#: its own: an assertion that calls the code it asserts cannot fail.
DECAL_TAG = "elysium.decal"
DECAL_MATERIAL_PREFIX = "vtmb:material:"
#: A cm tolerance for the float round trip through the actor's transform and `DecalSize`.
DECAL_EPSILON = 0.05
#: `bake_map.DECAL_HALF_DEPTH`, the projection box's reach along the decal's own -X.
DECAL_HALF_DEPTH = 16.0


def decal_errors(map_name, placed, payload):
    """The baked-projector check, pure: `placed` is one `facts` dict per actor carrying the decal
    tag -- its sort order, its material asset path, its world location and its `decal_size` -- and
    `payload` the staged `decals` block.

    **Keyed on the component's sort order, never on list position.** `_place_decals` stamps the
    staged row's own index as `UDecalComponent::SortOrder` -- which is load-bearing in its own
    right, because it is how two decals on one wall layer -- and `get_all_level_actors` returns
    the editor's order, not the bake's. The rope lane learned the same lesson with
    `elysium.src=<index>`; a decal needs no extra tag because the index is already on the
    component.

    `decal_size` is `(half depth, half height, half width)`: a deferred decal maps its texture U
    to the component's local Z and V to local Y, which is why the staged half-extents arrive
    swapped.
    """
    errors = []
    staged = {int(row["index"]): row for row in payload.get("rows") or []}
    seen = set()
    for facts in placed:
        index = int(facts["sortOrder"])
        if index in seen:
            errors.append("%s: decal %d placed twice" % (map_name, index))
            continue
        seen.add(index)
        row = staged.get(index)
        if row is None:
            errors.append("%s: decal %d stands but was not staged" % (map_name, index))
            continue
        expected = _decal_instance_path(row["materialId"])
        if facts["material"] != expected:
            errors.append("%s: decal %d binds %s, staged as %s"
                          % (map_name, index, facts["material"], expected))
        for axis in range(3):
            if abs(facts["locCm"][axis] - row["locCm"][axis]) > DECAL_EPSILON:
                errors.append("%s: decal %d stands at %s, staged at %s"
                              % (map_name, index, facts["locCm"], row["locCm"]))
                break
        size = facts["sizeCm"]
        for label, value, want in (
            ("depth", size[0], DECAL_HALF_DEPTH),
            ("height", size[1], float(row["halfHCm"])),
            ("width", size[2], float(row["halfWCm"])),
        ):
            if abs(value - want) > DECAL_EPSILON:
                errors.append("%s: decal %d half %s is %.4f, staged %.4f"
                              % (map_name, index, label, value, want))
    missing = sorted(set(staged) - seen)
    if missing:
        errors.append("%s: %d staged decal row(s) have no actor (first: %s)"
                      % (map_name, len(missing), missing[:5]))
    return errors


def _decal_instance_path(material_id):
    """`bake_map.decal_instance_path` restated over a staged `vtmb:material:` id (R7.2 ruling 2:
    `/ElysiumBaked/Materials/<dir>/MI_<safe stem>_Decal`)."""
    from elysium_pipeline.asset_names import safe_name

    key = (material_id[len(DECAL_MATERIAL_PREFIX):]
           if material_id.startswith(DECAL_MATERIAL_PREFIX) else material_id)
    parts = key.split("/")
    folded = "/".join(safe_name(part) for part in parts[:-1])
    name = "MI_" + safe_name(parts[-1]) + "_Decal"
    root = "/ElysiumBaked/Materials"
    return f"{root}/{folded}/{name}" if folded else f"{root}/{name}"


def verify_decals(actors, map_name):
    """0018 story 21-4: the placed `ADecalActor`s are exactly the staged `decals` rows.

    Before this story the projectors came from `<map>.decals` under the export root, produced by
    the BSP decoder, and no check ever asked whether the level carried them -- the decal count was
    a line in the bake log and nothing compared it with anything. The rows are the producer's now,
    so this asks the level the same question `verify_ropes` asks of the cables.
    """
    manifest = _staged_manifest(map_name)
    if manifest is None or manifest.get("decals") is None:
        return ["%s: no staged decal rows to check the level against "
                "(run: uv run elysium bake map --maps %s --force)" % (map_name, map_name)]
    placed = []
    for actor in actors:
        if DECAL_TAG not in [str(tag) for tag in actor.tags]:
            continue
        component = actor.decal
        size = component.get_editor_property("decal_size")
        material = component.get_editor_property("decal_material")
        location = actor.get_actor_location()
        placed.append({
            "sortOrder": component.get_editor_property("sort_order"),
            "material": (material.get_path_name().split(".", 1)[0] if material else ""),
            "locCm": [location.x, location.y, location.z],
            "sizeCm": [size.x, size.y, size.z],
        })
    errors = decal_errors(map_name, placed, manifest["decals"])
    counts = manifest["decals"].get("counts") or {}
    unreal.log("[verify] decals: %d actor(s), %d staged row(s) (%d unmatched, %d displacement "
               "face(s) the projector index cannot offer), %d problem(s)"
               % (len(placed), len(manifest["decals"].get("rows") or []),
                  counts.get("unmatched", 0), counts.get("dispFacesSkipped", 0), len(errors)))
    return errors


def _atof(text):
    """C `atof`: the longest numeric prefix, 0.0 when there is none (the producer's own reader;
    `UE_map_sidecars` needs numpy and cannot be imported here)."""
    match = re.match(r"\s*[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", text or "")
    return float(match.group(0)) if match else 0.0


def verify_brush_cull(map_name, ents_path):
    """R6.4: every meshed `func_lod` row carries
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
    asset_path = "%s/DA_%s_Entities" % (map_package(map_name), map_name)
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    asset_rows = asset.get_editor_property("entities") if asset else None
    matched = 0
    if asset_rows is None:
        errors.append("%s: %s does not load" % (map_name, asset_path))
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


#: The two blend modes whose V2 master reads the base texture's alpha per texel: `$translucent`
#: -> Translucent and `$alphatest` -> Masked (`importers/materials._resolve_blend`). Additive and
#: Modulate read the texture's colour only, and an opaque surface reads no alpha at all.
V2_ALPHA_BLEND_MODES = ("Translucent", "Masked")

_STAGED_MATERIALS = None


def _unit_key(row):
    """The corpus key behind a staged row: `vtmb:material:glass/glass01` -> `glass/glass01`."""
    return str(row.get("unit") or "").rsplit(":", 1)[-1]


def _staged_materials():
    """The material lane's own manifest, indexed by staged asset path and by unit key.

    `$ELYSIUM_WORK_ROOT/import/materials/manifest.json` is what `uv run elysium import materials`
    wrote: one row per staged `MI_`, carrying the master it parents to, the blend mode it
    overrides and the textures it binds. It is the only document that answers for an instance a
    converted map BINDS but never authors -- the V2 lane authors no per-map material at all, it
    prunes the package the legacy lane wrote (`bake_map_v2._material_sets`).

    Read once: ~19,700 rows covering the whole install, and every V2 material check joins on it.
    Empty when the lane has not staged on this machine; the caller names that as a missing input.

    **The unit index is built from the SURFACE rows only.** R7.2 stages two rows under one unit id
    for a `$decal`/`decalmodulate` material -- the surface `MI_<stem>` a model or a brush face
    binds, and the `MI_<stem>_Decal` projector a `UDecalComponent` lays, which is parented to
    `M_V2_Decal` and always Translucent or Modulate. A plain last-wins index hands the projector
    back for every one of the 588 such units on this install (126 of them disagree with their
    surface row on blend mode), and it is the SURFACE row that answers the questions this index is
    consulted for: what a placed model's `.mtl` unit resolves to, whether that unit reads a
    per-texel alpha, and which master its instance parents to. The projector is reached by asset
    path (`decalAsset`) when a caller wants it, never by unit.
    """
    global _STAGED_MATERIALS
    if _STAGED_MATERIALS is None:
        path = os.path.join(os.fspath(work_root()), "import", "materials", "manifest.json")
        rows = []
        if os.path.isfile(path):
            with open(path, "r", encoding="utf-8") as handle:
                rows = json.load(handle).get("assets") or []
        projectors = {str(row.get("decalAsset") or "") for row in rows}
        projectors.discard("")
        _STAGED_MATERIALS = ({str(row.get("assetPath") or ""): row for row in rows},
                             {_unit_key(row): row for row in rows
                              if str(row.get("assetPath") or "") not in projectors})
    return _STAGED_MATERIALS


def _staged_corpus_row(row, by_asset):
    """The staged row that actually carries a unit's master and its textures.

    A per-map patched unit (`/ElysiumBaked/Materials/maps/<map>/...`) is a parameter override the
    material lane authors ON the corpus instance: its parent is that instance, and it states only
    what the map's own `$envmap`/fog keys changed -- never a blend mode, never a texture. So both
    answers are the parent's, and the walk climbs while the parent is itself on the baked mount.
    """
    seen = set()
    while row is not None:
        parent = str(row.get("parent") or "")
        if not parent.startswith(MOUNT + "/") or parent in seen:
            return row
        seen.add(parent)
        row = by_asset.get(parent)
    return None


def v2_bound_units(map_materials, prop_units, by_asset, by_unit):
    """Every staged corpus material unit a converted map binds, once each.

    Returns `({unit key: (staged row, owner)}, [owner of a material the lane never staged])`.

    Two sources answer "binds", both of them the V2 lane's own: the map's staged manifest, which
    is keyed by SURFACE slot and names the instance the bake bound to it, and the corpus `.mtl` of
    every model the map places. Keying the result by unit is what keeps one broken corpus instance
    from being reported once per surface that draws it -- `glass/glass01` alone answers for ten
    slots on `sm_pawnshop_1`, each of them a `@cubemap` patch on that same asset.
    """
    units = {}
    missing = []
    for slot in sorted(map_materials):
        owner = "surface %s" % slot
        row = _staged_corpus_row(
            by_asset.get(str((map_materials[slot] or {}).get("asset") or "")), by_asset)
        if row is None:
            missing.append(owner)
            continue
        units.setdefault(_unit_key(row), (row, owner))
    for key in sorted(prop_units):
        row = _staged_corpus_row(by_unit.get(key), by_asset)
        if row is None:
            missing.append(prop_units[key])
            continue
        units.setdefault(_unit_key(row), (row, prop_units[key]))
    return units, missing


def v2_alpha_units(units):
    """`(unit key, owner, blend mode, BaseTexture asset)` for the bound units the shader reads a
    per-texel alpha through -- the set whose baked texture must have an alpha channel at all."""
    rows = []
    for key in sorted(units):
        row, owner = units[key]
        blend = str((row.get("basePropertyOverrides") or {}).get("blendMode") or "Opaque")
        if blend in V2_ALPHA_BLEND_MODES:
            rows.append((key, owner, blend,
                         str((row.get("textures") or {}).get("BaseTexture") or "")))
    return rows


def _baked_texture(registry, asset_path):
    """The registry row for a baked texture a staged manifest names by asset path.

    The legacy corpus index `verify_map` builds is keyed by asset NAME under one flat package; the
    material lane's textures are one package per corpus family
    (`/ElysiumBaked/Textures/<family>/T_<stem>`), so this lookup is by path. Existence is all this
    row is asked for -- see `_authored_alpha_minimum` for why no *derived* tag on it can be
    trusted in a commandlet.
    """
    data = registry.get_asset_by_object_path(bl.object_path_of(asset_path))
    return data if data is not None and data.is_valid() else None


def _authored_alpha_minimum(row):
    """The smallest alpha byte in the staged payload behind a material row's `BaseTexture`, or
    `None` when the lane's own documents do not lead to one.

    **Why this is not `HasAlphaChannel`.** The obvious read -- the baked `Texture2D`'s
    `HasAlphaChannel` registry tag -- cannot be trusted inside a `-run=pythonscript` commandlet, in
    either of the registry's two modes, and the failure is silent:

    * *In-memory* (the registry default): the tag is `UTexture2D::HasAlphaChannel()`, which is
      derived from BUILT platform data and returns `false` outright when there is none
      (`Texture2D.cpp:1049-1065`). Nothing in a commandlet builds it, so every texture the verify
      session has faulted in answers "no alpha channel" whatever it is encoded as.
    * *`bIncludeOnlyOnDiskAssets`*: correct for one map, then not. The registry updates its own
      cached disk row FROM the loaded object as it ticks, so in a four-map batch the failures grow
      with position: measured 2026-09-04, `sp_tutorial_1` (first) 0 findings, `sm_pawnshop_1` 2,
      `sm_hub_1` 21, `sm_pier_1` 10 -- on textures whose `T_*.uasset` on disk records
      `Format = DXT5`, `HasAlphaChannel = True`.

    So the question is asked of the pipeline's own documents instead, which are the same on every
    run: the unit's material provenance sidecar names the texture unit its `BaseTexture` resolved
    to (`textureBindings`, `vtmb:texture:<key>`), and that unit's staged DDS is the exact payload
    Unreal imported (`importers/texture_dds` decodes every BC level itself). `validation/dds_alpha`
    reads its top mip.

    That is also the honest form of the assertion. Every corpus texture imports `TC_Default` with
    `CompressionNoAlpha` unset (`import_textures.settings_for`), so the format is `AutoDXT`, which
    Unreal resolves to DXT1 exactly when no source texel is non-opaque -- "the baked texture has no
    alpha channel" and "the authored texture is opaque" are the same statement, and the second one
    is checkable. A `$translucent`/`$alphatest` unit over an opaque texture is a corpus fact VtMB
    shared (the 2004 shader sampled the same opaque texels and read alpha 1 everywhere); what is a
    defect is a mask the lane cannot show, which is what `None` and a sub-255 minimum separate.
    """
    sidecar = str(row.get("provenance") or "")
    if not sidecar:
        return None
    root = os.path.join(os.fspath(work_root()), "import")
    try:
        with open(os.path.join(root, "materials", *sidecar.split("/")), "r",
                  encoding="utf-8") as handle:
            provenance = json.load(handle)
    except (OSError, ValueError):
        return None
    prefix = "vtmb:texture:"
    key = next((str(binding.get("asset") or "")[len(prefix):]
                for binding in (provenance.get("textureBindings") or ())
                if isinstance(binding, dict) and binding.get("parameter") == "BaseTexture"
                and str(binding.get("asset") or "").startswith(prefix)), None)
    if not key:
        return None
    try:
        return alpha_minimum(os.path.join(root, "textures", *key.split("/")) + ".dds")
    except (OSError, DdsAlphaError):
        return None


def verify_v2_materials(map_name, registry, prop_units):
    """R5.1/R7.2, the materials a converted map binds, read back off
    the staged manifests instead of the legacy `.mtl`/PNG pair.

    Three assertions, each the V2 half of a legacy check that cannot answer on this lane:

    * **alpha** -- a unit whose blend reads the base texture's alpha binds a baked `Texture2D`
      that has an alpha channel. The legacy check probed the exported PNG and then the legacy
      corpus texture package, and a converted map binds neither: its surfaces bind the material
      lane's `/ElysiumBaked/Textures/<family>/` assets, imported from DDS.
    * **glass and Source Refract** -- every `$glass`/`$refract` unit the map's surfaces or its
      placed models bind is staged, and the instance on the mount parents to the master the lane
      recorded for it. The retired walk keyed these to a per-map `<map>/Materials/MI_...`
      package the bake prunes rather than writes, so every patched glass surface reported
      "material instance missing" and the check proved nothing.
    * **normals** -- a `NormalMap` the manifest names is a linear normal-compressed texture and is
      what the instance binds. The legacy `BumpMap` sub-check re-keyed: the V2 masters take
      `NormalMap`, and a unit the lane staged without one (`glass/glass01` is one) has nothing to
      check rather than a missing bump map -- `M_World_Glass` demanded one because its refraction
      graph had a slot for it, `M_V2_LitTranslucent` has none.

    One row per unit throughout: a corpus instance is one asset and gets one answer, however many
    surfaces draw it.
    """
    errors = []
    by_asset, by_unit = _staged_materials()
    if not by_asset:
        errors.append("%s: the material lane has staged nothing to check "
                      "the bound instances against (run: uv run elysium import materials)"
                      % map_name)
        return errors
    manifest = _staged_manifest(map_name)
    if manifest is None:
        errors.append("%s: no staged map_geometry manifest to read the "
                      "bound materials from (run: uv run elysium export map %s)"
                      % (map_name, map_name))
        return errors

    units, missing = v2_bound_units(
        manifest.get("materials") or {}, prop_units, by_asset, by_unit)
    for owner in missing:
        message = "%s: binds a material the lane never staged" % owner
        unreal.log_error("[verify] " + message)
        errors.append(message)

    alpha_capable = 0
    opaque_sources = []
    alpha_rows = v2_alpha_units(units)
    for key, owner, blend, base in alpha_rows:
        data = _baked_texture(registry, base) if base else None
        authored = _authored_alpha_minimum(units[key][0]) if base else None
        if not base:
            message = "%s: %s material %s binds no BaseTexture to blend" % (owner, blend, key)
        elif data is None:
            message = "%s: bound BaseTexture is not on the mount: %s" % (owner, base)
        elif authored is None:
            message = ("%s: %s material's BaseTexture has no staged payload to read an authored "
                       "alpha from: %s" % (owner, blend, base))
        elif authored >= 255:
            # The corpus's own fact, not a bake defect: see `_authored_alpha_minimum`.
            opaque_sources.append("%s (%s)" % (key, base))
            continue
        else:
            alpha_capable += 1
            continue
        unreal.log_error("[verify] " + message)
        errors.append(message)
    unreal.log("[verify] V2 materials: %d unit(s) bound, %d blended or masked, %d alpha-capable, "
               "%d opaque-authored" % (len(units), len(alpha_rows), alpha_capable,
                                       len(opaque_sources)))
    for row in sorted(opaque_sources):
        unreal.log("[verify]   opaque-authored blend: %s" % row)

    # The semantic pair the legacy walks owned. 0018 story 21-4 dropped the
    # `<map>.materials.json` overlay that used to be merged in here: exactly one map in the
    # export tree ever had one (`sm_pier_1`, delisted by this group), and a PAKFILE-only material
    # is a first-class unit in the V2 lane rather than a map-local record. 0018 story 21-5
    # retired the shared corpus underneath, so the SELECTOR is the staged row's own now rather
    # than the corpus `MatDef`'s `glass`/`refract` flags.
    #
    # What the two flags picked out is what these three assertions are worth asking of: a unit
    # that refracts (the Source framebuffer-distortion master, `M_V2_Refract`) or one that binds
    # a normal map at all -- the legacy `glass 1` has no master of its own on this lane, it is a
    # translucent lit instance like any other, and the normal is the thing the check is about.
    flagged = parented = normals = 0
    for key in sorted(units):
        row, owner = units[key]
        master_name = str(row.get("parent") or "").rsplit("/", 1)[-1]
        refracts = master_name == "M_V2_Refract"
        if not refracts and not (row.get("textures") or {}).get("NormalMap"):
            continue
        kind = "Source Refract" if refracts else "normal-mapped"
        flagged += 1
        asset = str(row.get("assetPath") or "")
        instance = unreal.EditorAssetLibrary.load_asset(asset)
        if instance is None:
            message = "%s: baked %s material instance missing: %s" % (owner, kind, asset)
            unreal.log_error("[verify] " + message)
            errors.append(message)
            continue
        master = str(row.get("parent") or "")
        parent = instance.get_editor_property("parent")
        if parent is None or parent.get_path_name() != bl.object_path_of(master):
            message = "%s: %s instance %s does not parent to the master the lane staged (%s)" % (
                owner, kind, asset, master or "none")
            unreal.log_error("[verify] " + message)
            errors.append(message)
            continue
        parented += 1
        normal = str((row.get("textures") or {}).get("NormalMap") or "")
        if not normal:
            continue
        data = _baked_texture(registry, normal)
        normal_asset = data.get_asset() if data is not None else None
        bound = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
            instance, "NormalMap")
        if normal_asset is None:
            message = "%s: baked %s normal missing: %s" % (owner, kind, normal)
        elif normal_asset.get_editor_property("srgb") or normal_asset.get_editor_property(
                "compression_settings") != unreal.TextureCompressionSettings.TC_NORMALMAP:
            message = "%s: %s normal is not linear normal-compressed: %s" % (owner, kind, normal)
        elif bound != normal_asset:
            message = "%s: %s instance has no matching NormalMap binding: %s" % (
                owner, kind, normal)
        else:
            normals += 1
            continue
        unreal.log_error("[verify] " + message)
        errors.append(message)
    unreal.log("[verify] V2 glass/refract %d flagged / %d parented / %d normal-bound"
               % (flagged, parented, normals))
    return errors


def verify_map(map_name):
    errors = []
    package = map_package(map_name)
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

    # The material units every model this map places binds: what the material questions are
    # asked of. 0018 story 21-5 moved this off the shared corpus's per-model `.mtl` and onto the
    # model lane's own resolved `slots[]`.
    prop_units = _map_prop_material_units(map_name)
    # The light lanes read `<map>.lights` and `<map>.sky`, two of the producer's own eight, so
    # they read them where the producer wrote them (0018 story 21-4). Nothing in this lane reads
    # `$ELYSIUM_EXPORT_ROOT/<map>/` any more; what is left of that root here is the shared corpus
    # (`shared/materials.json`, `shared/props/*.mtl`), which 21-5 retires.
    world_dir = _sidecar_dir(map_name)
    # `verify_v2_materials` asks the three material questions -- alpha capability, glass and
    # refract staging and parentage, NormalMap linearity -- of what the map actually binds:
    # the material lane's `/ElysiumBaked/Materials/<family>/` instances. The three walks that
    # used to stand here read the retired lane's own output (the exported PNG corpus, the
    # per-map `<map>/Materials` package, the shared corpus) and answered about assets nobody
    # drew (0018 story 21-1).
    errors.extend(verify_v2_materials(map_name, registry, prop_units))

    ents_path = os.path.join(_sidecar_dir(map_name), map_name + ".ents")
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
        errors.extend(verify_sm_hub_1_weather(package, map_name))

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
        errors.extend(verify_water(actors, map_name))
        errors.extend(verify_ai_infra(actors, map_name))
        errors.extend(verify_sky_scope(actors, map_name))
        errors.extend(verify_ropes(actors, map_name))
        errors.extend(verify_decals(actors, map_name))
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
    single = arg("BakeMap", "").strip()
    if single and not map_names:
        map_names = [single]
    if not map_names:
        # Named rather than defaulted to one map: `verify_bakes` always passes `-BakeMaps`, so an
        # empty selection is a launch that asked for nothing, and verifying `sp_tutorial_1`
        # instead would report a green map nobody asked about. It is also what lets this file be
        # loaded by its own tests, which are not an editor process.
        raise SystemExit("[verify] -BakeMaps=<map,...> or -BakeMap=<map> is required")
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
