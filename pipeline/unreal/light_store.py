"""The per-map light store: hand-tuned lights, harvested out of the baked level and re-applied
by the next bake.

R5.6 made the baked light actor the truth on a V2 map: `bake_map_v2._place_lights` writes every
final value once, from `worldLights[]` through `derive_light`, and the runtime rig derives
nothing. That left the level -- a generated, gitignored package the bake deletes and re-authors
every run -- as the only place a light's value exists, so a lighting pass done the ordinary Unreal
way (open the map, drag the gizmo, tune the Details panel, Ctrl+S) died at the next `export map`
with nothing said.

This module closes that loop, and closes it the way that cannot be forgotten:

  1. **Harvest.** `bake_one` calls `harvest` before it tears the previous world down, so the very
     first thing a bake does with a map is read the light actors off the level already on disk --
     including whatever a human just saved into it -- and write them to
     `Content/ElysiumAuthored/Lighting/<map>.lights.json`.
  2. **Apply.** `_place_lights` places the store's records verbatim when the file exists, and
     falls back to `derive_light` when it does not.

The store is a **full snapshot, never a delta**. There is no baseline to diff against, no
per-field override toggle, no identity to reconcile: the file *is* the map's light set. A light
added in the editor is a record with a null `src`; a light deleted in the editor is a record that
is not there. That is the whole of the design, and the reason it needs no machinery.

**A stored map stops listening to `UElysiumLightingSettings`**, which is the point -- you have
taken manual control of it -- but it is also the one surprise here, because a map self-stores on
its second bake (the first writes a level, the second harvests it) whether or not a human ever
touched it. `-NoLightStore=1` (`uv run elysium export map <map> --no-light-store`) skips both
halves for a launch, and handing a map back to the settings page is that flag plus deleting the
JSON: **deleting the file alone does nothing**, because the harvest would read the tuned values
straight back off the level that is still on the mount and write the store again. With the flag,
that bake re-derives the level, and the next ordinary bake harvests the derived level into a
fresh store.

**No launch can destroy an edit it did not first read.** `bake_one` is the only thing that
overwrites a level, and the harvest is the first thing it does -- so a profile bake that skips
its launch entirely (`export_manager._bake_profile_maps`, whose receipt does not name the level)
leaves both the level and the store exactly as they were, and the next launch picks the edit up.
That is why the loop needs no receipt of its own.

**Scope: the V2 lane's `elysium.light` actors and nothing else.** A legacy-lane map re-derives
every value at load from the `.lights` sidecar (`UElysiumLightRig::Adopt`), so a value baked into
its actors would be overwritten before the first frame; those maps are skipped. The SkyLight is
`_place_sky`'s and is not harvested, and the type-5 skyambient row places no actor at all, so its
`(colour, magnitude)` keeps coming from the staged table on both paths.
"""

from __future__ import annotations

import json
import os

try:  # The harvest half needs the editor; the format half is pure, so pytest imports it bare.
    import unreal
except ImportError:  # pragma: no cover - the editor is present in every launch that harvests
    unreal = None

#: Under `Content/`, the one authored tree the bake never prunes and Git tracks: `.gitignore`
#: excepts `Content/ElysiumAuthored/**`, and the LFS filters in `.gitattributes` match only the
#: binary package extensions, so this JSON is an ordinary tracked, diffable, mergeable text file.
STORE_DIR = ("ElysiumAuthored", "Lighting")
STORE_SUFFIX = ".lights.json"

#: Bumped when a record grows or loses a field. A store written by an older schema is ignored
#: rather than half-read: the bake falls back to `derive_light` and says so, which is the same
#: place a map with no store starts from.
SCHEMA = 1

#: `elysium_pipeline`'s own tag, restated rather than imported -- this module is loaded by the
#: editor's embedded CPython and must not drag `bake_map`'s import graph in behind it.
TAG_LIGHT = "elysium.light"

#: Every float is rounded on write. Two reasons, both about the file rather than the render: a
#: raw `repr` turns `0.003 * 100` into `0.30000000000000004` and makes every diff unreadable, and
#: an unrounded harvest of a value the editor round-tripped through a float32 property would
#: rewrite the file on bakes that changed nothing.
PRECISION = 6


#: Every key one record carries, which is exactly what `bake_map_v2._place_one_light` reads off
#: it. `record` below is the single writer, so this set and that function are the two halves of
#: one contract; `pipeline/tests/test_light_store.py` pins them against each other.
RECORD_FIELDS = frozenset((
    "src", "kind", "style", "sky", "label", "position", "rotation", "color", "intensity",
    "reach_cm", "falloff_exponent", "use_inverse_squared_falloff", "outer_cone_deg",
    "inner_cone_deg", "cast_shadows", "specular_scale", "indirect_lighting_intensity",
    "volumetric_scattering_intensity", "sun_source_angle_deg", "sun_soft_source_angle_deg",
    "allow_mega_lights",
))


def store_path(content_dir, map_name):
    """`<Content>/ElysiumAuthored/Lighting/<map>.lights.json`, the one path accessor."""

    return os.path.join(os.fspath(content_dir), *STORE_DIR, "%s%s" % (map_name, STORE_SUFFIX))


def content_dir():
    """The project's `Content/`, from the editor. Callers outside a launch pass their own."""

    return unreal.Paths.project_content_dir()


def _round(value):
    return round(float(value), PRECISION)


def record(final, *, src, style, sky, rotation, label=""):
    """One store record from `derive_light`'s output.

    `final` carries the values; the four keywords carry what the staged row knows and the
    derivation does not -- the lump-15 ordinal the actor is tagged with, the lightstyle the
    runtime rig animates against, the 3D-skybox flag that picks the actor's folder, and the
    rotation, which is `_dir_rotator(direction)` on this path and the human's own gizmo on the
    harvested one. Written explicitly rather than splatted from `final`, so the file format is
    pinned here and a new `derive_light` key cannot silently join it.
    """

    return {
        "src": None if src is None else int(src),
        "kind": int(final["kind"]),
        "style": int(style),
        "sky": bool(sky),
        "label": str(label),
        "position": [_round(v) for v in final["position"]],
        "rotation": [_round(v) for v in rotation],
        "color": [_round(v) for v in final["color"]],
        "intensity": _round(final["intensity"]),
        "reach_cm": _round(final["reach_cm"]),
        "falloff_exponent": _round(final["falloff_exponent"]),
        "use_inverse_squared_falloff": bool(final.get("use_inverse_squared_falloff", False)),
        "outer_cone_deg": _round(final["outer_cone_deg"]),
        "inner_cone_deg": _round(final["inner_cone_deg"]),
        "cast_shadows": bool(final["cast_shadows"]),
        "specular_scale": _round(final["specular_scale"]),
        "indirect_lighting_intensity": _round(final["indirect_lighting_intensity"]),
        "volumetric_scattering_intensity": _round(final["volumetric_scattering_intensity"]),
        "sun_source_angle_deg": _round(final["sun_source_angle_deg"]),
        "sun_soft_source_angle_deg": _round(final["sun_soft_source_angle_deg"]),
        "allow_mega_lights": bool(final["allow_mega_lights"]),
    }


def sort_key(row):
    """Stable, diff-friendly order: the retail lights in lump-15 order, then anything a human
    added, by label. A file whose rows moved for no reason is a file nobody reads."""

    return (row.get("src") is None, row.get("src") or 0, str(row.get("label", "")))


def load(content_dir_path, map_name, log=None):
    """The map's stored records, or `None` when it has no store.

    A store that will not parse, or that a newer schema wrote, is `None` too: the bake then
    derives, which is where a map with no store already starts. Refusing to bake over a typo in a
    hand-edited JSON would strand the map instead of lighting it.
    """

    path = store_path(content_dir_path, map_name)
    if not os.path.isfile(path):
        return None
    try:
        with open(path, "r", encoding="utf-8") as handle:
            payload = json.load(handle)
        if int(payload.get("schema", 0)) != SCHEMA:
            if log:
                log("light store: %s is schema %s, not %d -- deriving instead"
                    % (path, payload.get("schema"), SCHEMA))
            return None
        rows = payload["lights"]
    except (ValueError, KeyError, TypeError, OSError) as exc:
        if log:
            log("light store: %s unreadable (%s) -- deriving instead" % (path, exc))
        return None
    # Every record is read field by field by `_place_one_light`, hundreds of lights into a bake
    # that has already authored a map's textures, materials and meshes. A row missing a key is
    # caught here, where the answer is still "derive", rather than there, where it is a crash.
    rows = [dict(row) for row in rows]
    for index, row in enumerate(rows):
        missing = RECORD_FIELDS - set(row)
        if missing:
            if log:
                log("light store: %s row %d is missing %s -- deriving instead"
                    % (path, index, ", ".join(sorted(missing))))
            return None
    return rows


def save(content_dir_path, map_name, records, log=None):
    """Write the map's store, and answer whether the file changed.

    Byte-compared before writing: a bake that harvests a level nobody edited must leave the file's
    mtime and its Git status alone, or every `export map` would show up as a lighting change.
    """

    path = store_path(content_dir_path, map_name)
    payload = {
        "schema": SCHEMA,
        "map": map_name,
        # The file is machine-written on every bake, so it says so in itself rather than only in
        # a document somebody has to find.
        "note": ("Harvested from the baked level by pipeline/unreal/light_store.py on each bake, "
                 "and applied by the same bake. Edit the lights in the Unreal editor and save "
                 "the level; this file follows. To hand the map back to UElysiumLightingSettings, "
                 "delete this file AND bake once with --no-light-store -- deleting it alone only "
                 "makes the next bake harvest the same values off the level again."),
        "lights": sorted(records, key=sort_key),
    }
    text = json.dumps(payload, indent=2) + "\n"
    if os.path.isfile(path):
        with open(path, "r", encoding="utf-8") as handle:
            if handle.read() == text:
                return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
    if log:
        log("light store: wrote %d light(s) to %s" % (len(records), path))
    return True


# --------------------------------------------------------------------------- harvest


#: `FColor` -> `FLinearColor`, the inverse of the `ToFColor(true)` inside
#: `ULightComponent::SetLightColor`. The placement writes linear and the component stores sRGB
#: bytes, so a harvest that read the bytes raw would darken every light it round-tripped.
def _srgb_to_linear(byte_value):
    channel = float(byte_value) / 255.0
    if channel <= 0.04045:
        return channel / 12.92
    return ((channel + 0.055) / 1.055) ** 2.4


def _tag_int(tags, prefix, default):
    for tag in tags:
        text = str(tag)
        if text.startswith(prefix):
            try:
                return int(text[len(prefix):])
            except ValueError:
                return default
    return default


def _kind_of(actor, tags):
    """The VtMB light type. The tag is authoritative where there is one -- only it can tell a
    type-0 texlight from a type-1 point, which are the same Unreal class -- and the actor's class
    answers for a light a human added, which carries no tag at all."""

    kind = _tag_int(tags, "elysium.type=", None)
    if kind is not None:
        return kind
    if isinstance(actor, unreal.DirectionalLight):
        return 3
    if isinstance(actor, unreal.SpotLight):  # ASpotLight derives from APointLight: test it first
        return 2
    return 1


def _property(component, name, default):
    try:
        return component.get_editor_property(name)
    except Exception:  # pragma: no cover - a property absent on this light class
        return default


def _record_from_actor(actor):
    """One store record read back off a placed light actor."""

    component = actor.light_component
    if component is None:
        return None
    tags = [str(tag) for tag in actor.tags]
    kind = _kind_of(actor, tags)
    location = actor.get_actor_location()
    rotation = actor.get_actor_rotation()
    color = _property(component, "light_color", None)
    linear = ([_srgb_to_linear(color.r), _srgb_to_linear(color.g), _srgb_to_linear(color.b)]
              if color is not None else [1.0, 1.0, 1.0])
    final = {
        "kind": kind,
        "position": [location.x, location.y, location.z],
        "color": linear,
        "intensity": _property(component, "intensity", 0.0),
        "reach_cm": _property(component, "attenuation_radius", 0.0),
        "falloff_exponent": _property(component, "light_falloff_exponent", 1.0),
        "use_inverse_squared_falloff": _property(
            component, "use_inverse_squared_falloff", False),
        "outer_cone_deg": _property(component, "outer_cone_angle", 0.0),
        "inner_cone_deg": _property(component, "inner_cone_angle", 0.0),
        "cast_shadows": _property(component, "cast_shadows", True),
        "specular_scale": _property(component, "specular_scale", 1.0),
        "indirect_lighting_intensity": _property(component, "indirect_lighting_intensity", 1.0),
        "volumetric_scattering_intensity": _property(
            component, "volumetric_scattering_intensity", 1.0),
        "sun_source_angle_deg": _property(component, "light_source_angle", 0.0),
        "sun_soft_source_angle_deg": _property(component, "light_source_soft_angle", 0.0),
        "allow_mega_lights": _property(component, "allow_mega_lights", kind != 3),
    }
    return record(
        final,
        src=_tag_int(tags, "elysium.src=", None),
        style=_tag_int(tags, "elysium.style=", 0),
        # The bake files a miniature light under `Sky/Lights`, and that folder is the only place
        # the flag survives on the actor -- there is no `elysium.sky=` tag.
        sky=str(actor.get_folder_path()) == "Sky/Lights",
        rotation=[rotation.pitch, rotation.yaw, rotation.roll],
        label=str(actor.get_actor_label()),
    )


def harvest(content_dir_path, map_name, level_path, log=None, warn=None):
    """Read the light actors off the level already on disk and write the map's store.

    Called at the top of `bake_one`, before the previous world is torn down, because this is the
    last moment the level a human saved still exists -- `stage_level` authors into a blank world
    and overwrites the package.

    Every `APointLight`/`ASpotLight`/`ADirectionalLight` in the level is taken, tagged or not, so
    a light added by hand is harvested beside the ones the bake placed. A map with no level yet
    harvests nothing and leaves any existing store alone: nothing was read, so nothing may be
    concluded about what the map's lights are.
    """

    if not unreal.EditorAssetLibrary.does_asset_exist(level_path):
        if log:
            log("light store: %s is not on the mount yet -- nothing to harvest" % level_path)
        return False
    unreal.EditorLoadingAndSavingUtils.load_map(level_path)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    records = []
    skipped = 0
    for actor in actors:
        if not isinstance(actor, (unreal.PointLight, unreal.SpotLight, unreal.DirectionalLight)):
            continue
        row = _record_from_actor(actor)
        if row is None:
            skipped += 1
            continue
        records.append(row)
    if skipped and warn:
        warn("light store: %d light actor(s) had no light component and were dropped" % skipped)
    if not records:
        # An empty harvest is not the same fact as "this map has no lights": a level that failed
        # to load, or one whose light actors were pruned by hand, would otherwise write an empty
        # store and put the map in the dark on every bake from here on.
        if warn:
            warn("light store: %s yielded no light actors -- store left as it was" % level_path)
        return False
    added = sum(1 for row in records if row["src"] is None)
    changed = save(content_dir_path, map_name, records, log=log)
    if log:
        log("light store: harvested %d light(s) from %s (%d hand-added); %s"
            % (len(records), level_path, added,
               "store updated" if changed else "store unchanged"))
    return changed
