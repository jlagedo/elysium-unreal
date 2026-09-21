"""Phase 2 of `uv run elysium import map-collision`: land one staged collision payload per map as a
`UElysiumMapCollisionPayload` asset under `/ElysiumBaked/Maps/<map>/DA_<map>_Collision`.

Runs inside a headless editor (`-run=pythonscript -script=pipeline/unreal/import_map_collision.py
-ImportMapCollision=<manifest.json>`). The offline stage (`importers/map_collision.py`, R4.2) already
read `<map>.hulls`, `<map>.dispcol` and the brush-entity `hulls` of `<map>.ents`, applied the one
transform the runtime applies (the 3D-skybox scale) and asserted parity against those files; this
script only turns those numbers into cooked `UBodySetup`s and saves them.

Nothing is decided here. Every number written below is copied from the manifest verbatim, and every
body-setup flag is set by the asset's own C++ authoring functions rather than by this script, so the
recipe has one owner.

  * per map, compare the manifest recipe against the stamp the asset carries
    (`bake_lib.RECIPE_TAG`) and touch only what is new, changed or forced;
  * cook every authored setup at import time, so a payload that cannot cook fails here and not at
    map load;
  * write `import_report.json` beside the manifest and exit non-zero if any map failed.

Command line:
  -ImportMapCollision=<path>  the manifest (required)
  -ImportForce=1              re-author every map regardless of stamp (`0`/`false`/`no` = off)
"""
from __future__ import annotations

import json
import os
import re
import time
import traceback

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402

#: The recipe stage label every fingerprint hashes under -- the lane's own name.
STAGE = "map_collision"

#: Manifest schema this script understands.
MANIFEST_SCHEMA = "2.0.0"

#: The one class this lane authors.
ASSET_CLASS = "ElysiumMapCollisionPayload"


def log(msg):
    unreal.log("[import-map-collision] %s" % msg)


def fail(msg):
    unreal.log_error("[import-map-collision] %s" % msg)


def cmdline_arg(key, default=""):
    """Read -Key=value off the editor command line (a quoted path is one token)."""
    needle = "-%s=" % key
    for token in re.findall(r'"[^"]*"|\S+', unreal.SystemLibrary.get_command_line()):
        token = token.strip('"')
        if token.lower().startswith(needle.lower()):
            return token[len(needle):].strip('"')
    return default


def flag(value):
    """A command-line switch as a boolean (`bool("0")` is True, so this is not a truthiness test)."""
    return str(value).strip().strip('"').lower() in ("1", "true", "yes", "on")


class ManifestError(RuntimeError):
    """The manifest cannot be executed as written."""


def load_manifest(path):
    with open(path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    if not isinstance(manifest, dict):
        raise ManifestError("manifest is not an object")
    if manifest.get("schemaVersion") != MANIFEST_SCHEMA:
        raise ManifestError("manifest schema %r is not %s"
                            % (manifest.get("schemaVersion"), MANIFEST_SCHEMA))
    entries = manifest.get("maps")
    if not isinstance(entries, list):
        raise ManifestError("manifest maps is not a list")
    mount = manifest.get("mount")
    if not isinstance(mount, str) or not mount.startswith("/") or mount.endswith("/"):
        raise ManifestError("manifest mount %r is not a mount path" % (mount,))
    for index, entry in enumerate(entries):
        for key in ("map", "assetPath", "packageRoot", "worldHulls", "parity"):
            if key not in entry:
                raise ManifestError("maps[%d] lacks %r" % (index, key))
        if not entry["assetPath"].startswith(mount + "/"):
            raise ManifestError("maps[%d] %s is outside %s" % (index, entry["assetPath"], mount))
        # The stage refuses to write an entry whose parity failed, so an unchecked or unequal
        # payload reaching this process is a manifest that must not be executed.
        parity = entry["parity"]
        if not parity.get("checked") or not parity.get("equal"):
            raise ManifestError("maps[%d] %s carries no passing parity verdict: %r"
                                % (index, entry["map"], parity))
    return manifest


def split_asset_path(asset_path):
    """`/Root/dir/DA_name` -> (`/Root/dir`, `DA_name`)."""
    package, _, name = asset_path.rpartition("/")
    return package, name


def make_hull(flat):
    """One convex volume: the sidecar's flat `x y z x y z ...` row, whole triples only."""
    hull = unreal.ElysiumCollisionHull()
    hull.set_editor_property(
        "vertices",
        [unreal.Vector(flat[i], flat[i + 1], flat[i + 2]) for i in range(0, len(flat) - 2, 3)],
    )
    return hull


def make_hulls(rows):
    return [make_hull(row) for row in rows]


def author_map(entry, force=False):
    """Author or reuse one map's `UElysiumMapCollisionPayload`. Returns "imported" or "reused"."""
    object_path = entry["assetPath"]
    package_root, asset_name = split_asset_path(object_path)

    # The fingerprint covers every number the asset carries, so a re-stage that moves one vertex
    # rebuilds and a re-stage that changes nothing does not.
    fingerprint = bl.recipe_fingerprint(
        STAGE, object_path,
        {
            "recipeVersion": entry.get("recipeVersion"),
            "skyScale": entry.get("skyScale"),
            # The partition as well as the geometry: the same vertices under a different
            # signature are different bodies wearing different profiles, and a re-stage that
            # moves only the signature must still re-author.
            "worldBodies": entry.get("worldBodies", []),
            "worldHulls": entry["worldHulls"],
            "displacementVertices": entry.get("displacementVertices", []),
            "displacementIndices": entry.get("displacementIndices", []),
            "brushBodies": entry.get("brushBodies", []),
            # The door answer comes from the GRAPH, not from the hulls, so a re-exported graph can
            # flip a door between cut and traversable while every number above is unchanged.
            # Without it here the payload reuses, the level keeps yesterday's cuts, and the report
            # is clean -- the same shape of bug as the level state this lane already learned to
            # ask about rather than infer.
            "navDoors": entry.get("navDoors", {}),
            "navAreas": entry.get("navAreas", []),
        },
    )
    if (not force and unreal.EditorAssetLibrary.does_asset_exist(object_path)
            and bl.asset_class_name(object_path) == ASSET_CLASS
            and bl.stored_recipe(object_path, producer='map-collision') == fingerprint):
        # The payload is current, but the LEVEL is a separate product this lane also owns, and the
        # fingerprint above covers none of it: `bake map` rewrites the level from scratch, so a
        # re-bake with unchanged collision leaves a level with no world-collision actor and no
        # navigation mesh while this lane reports "reused". Ask the level itself.
        place_world_collision_actor(entry, unreal.load_asset(object_path), skip_if_current=True)
        return "reused"

    bl.ensure_dir(package_root)
    asset = unreal.load_asset(object_path)
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.ElysiumMapCollisionPayload)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, package_root, unreal.ElysiumMapCollisionPayload, factory)
    if asset is None:
        raise RuntimeError("could not create %s" % object_path)

    asset.set_editor_property("map_name", entry["map"])
    # Re-authoring an existing asset drops what was there first: the body setups are subobjects of
    # this package, and leaving the previous set behind would carry it into the save.
    asset.reset_authoring()
    # One world body per contents signature: a brush answers four retail masks and the answers
    # differ, so a single body cannot say that an NPC clip stops an NPC and not the player. A
    # manifest staged before the partition existed carries only the flat list, and authors the one
    # body it always did.
    world_bodies = entry.get("worldBodies")
    if world_bodies:
        for row in world_bodies:
            asset.author_world_body(int(row["signature"]), make_hulls(row["hulls"]))
    else:
        asset.author_world_hulls(make_hulls(entry["worldHulls"]))

    flat = entry.get("displacementVertices", [])
    vertices = [unreal.Vector(flat[i], flat[i + 1], flat[i + 2])
                for i in range(0, len(flat) - 2, 3)]
    asset.author_displacement(vertices, [int(i) for i in entry.get("displacementIndices", [])])

    for row in entry.get("brushBodies", []):
        # A mover wears the profile of its own brushes: a door blocks sight and both pawns through
        # MOVEABLE, a glass func_brush blocks neither pawn's sight.
        asset.author_brush_body_with_signature(
            int(row["entityIndex"]), int(row.get("signature", 0)), make_hulls(row["hulls"]))

    # Cook now, in the import, so the DDC entry exists before any map load and a payload that
    # cannot cook is this run's failure rather than a silent fallback at runtime.
    failure = asset.cook_authored()
    if failure:
        raise RuntimeError("collision cook failed for %s (%s)" % (object_path, failure))

    bl.stamp_recipe(asset, fingerprint, producer='map-collision')
    if not bl.save(object_path):
        raise RuntimeError("save failed: %s" % object_path)

    place_world_collision_actor(entry, asset)
    return "imported"


def place_world_collision_actor(entry, asset, skip_if_current=False):
    """Stand the map's world collision in its own level, as one static component per signature.

    The runtime used to build these into transient components at map load, which meant the level
    held no navigation-relevant geometry at all and a navigation mesh could only ever be generated
    at run time. Saved in the level, they are what Recast can bake a mesh FROM -- and from the
    right solids, since a body affects navigation exactly when its contents signature blocks an
    NPC, so the NPC-only clips cut the mesh and the sight-only brushes do not.

    Placed here rather than in `bake map` because this lane is what authors the bodies the actor
    points at: one lane writes both, so the actor and the asset cannot disagree. A map whose level
    is not baked yet is skipped -- it will get its actor the next time this runs.

    Called on BOTH the authored and the reused path, because whether the level needs this is a
    question about the level, not about the payload: `bake map` rewrites the level and takes the
    actor and the meshes with it, leaving a payload that still fingerprints clean.
    """
    level_path = "%s/%s" % (entry["packageRoot"], entry["map"])
    if not unreal.EditorAssetLibrary.does_asset_exist(level_path):
        log("%s: no baked level yet; its world-collision actor waits for one" % entry["map"])
        return 0

    if not unreal.EditorLoadingAndSavingUtils.load_map(level_path):
        raise RuntimeError("could not open %s to place its world collision" % level_path)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # Only when the payload was NOT re-authored. A re-author replaces every body setup, so the
    # actor standing in the level points at objects that no longer exist and must be re-placed
    # whatever the level looks like.
    if skip_if_current and level_collision_is_current(entry, asset, world, actors):
        log("%s: the level already carries its world collision and meshes" % entry["map"])
        return 0

    # Exactly one, always: a second would be a lane that ran twice, and the runtime refuses a
    # level carrying two rather than picking one.
    #
    # Navigation data is dropped with it. This project generates navigation at run time, over the
    # agents a body can actually use, so a RecastNavMesh saved in the level is always a leftover --
    # and a stale one is worse than none, because the runtime reads a saved mesh as "already
    # built" and would skip the build the map actually needs.
    stale_nav = 0
    stale_names = []
    for existing in actors.get_all_level_actors():
        if isinstance(existing, unreal.ElysiumWorldCollisionActor):
            actors.destroy_actor(existing)
        elif isinstance(existing, (unreal.RecastNavMesh, unreal.NavMeshBoundsVolume)):
            stale_names.append(existing.get_name())
            actors.destroy_actor(existing)
            stale_nav += 1
    if stale_nav:
        log("%s: dropped %d stale navigation actor(s) from the level: %s"
            % (entry["map"], stale_nav, ", ".join(sorted(stale_names))))

    actor = actors.spawn_actor_from_class(
        unreal.ElysiumWorldCollisionActor, unreal.Vector(0.0, 0.0, 0.0))
    if actor is None:
        raise RuntimeError("could not spawn the world-collision actor in %s" % level_path)
    actor.set_actor_label("ElysiumWorldCollision")
    placed = actor.author_from_payload(asset)
    if not placed:
        raise RuntimeError("%s: the payload authored no world body" % entry["map"])
    log("%s: %d world collision body(ies) stand in the level" % (entry["map"], placed))

    place_nav_areas(entry, actors)

    wanted = build_navigation(entry["map"], world)
    prune_unwanted_navmeshes(entry["map"], actors, wanted)

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, level_path):
        raise RuntimeError("could not save %s after placing its world collision" % level_path)
    return placed


def level_collision_is_current(entry, asset, world, actors):
    """Whether this level already carries exactly what this lane would place into it.

    Exactly one world-collision actor, pointing at THIS payload with a body per world body, plus a
    navigation mesh carrying TILES for every agent the map's own `UsedHullBits` names. Anything
    else -- most often a level `bake map` has just rewritten, which carries none of them -- means
    place it again.

    Tiles rather than actors, because an empty mesh saves, loads and reads as "already built"
    exactly like a full one: presence alone would call a wiped level current.
    """
    found = None
    marks = 0
    for existing in actors.get_all_level_actors():
        if isinstance(existing, unreal.ElysiumWorldCollisionActor):
            if found is not None:
                return False        # two of them: re-place, which drops both and spawns one
            found = existing
        elif isinstance(existing, unreal.ElysiumNavAreaActor):
            marks += 1
    if found is None:
        return False
    # The marks are cut INTO the meshes, so a level missing them has meshes that were built
    # without them -- priced roadway and door cuts included.
    wants_marks = bool(entry.get("navAreas")) or any(
        not row.get("traversable") for row in (entry.get("navDoors") or {}).get("rows", []))
    if wants_marks and marks != 1:
        return False
    if (found.get_editor_property("map_name") != entry["map"]
            or found.get_editor_property("payload") != asset
            or len(found.get_editor_property("bodies")) != len(entry.get("worldBodies", []))):
        return False

    tiles = {}
    for row in unreal.ElysiumNavBakeLibrary.nav_mesh_tile_counts(world):
        name, _, count = row.rpartition("=")
        tiles[name] = int(count)
    log("%s: the level's navigation meshes are %s"
        % (entry["map"], ", ".join("%s %d tile(s)" % kv for kv in sorted(tiles.items())) or "none"))

    bits = used_hull_bits(entry["map"])
    if bits is None:
        return any(count > 0 for count in tiles.values())
    wanted = unreal.ElysiumNavBakeLibrary.agent_names_for_hull_bits(bits)
    return bool(wanted) and all(
        tiles.get("RecastNavMesh-%s" % name, 0) > 0 for name in wanted)


def place_nav_areas(entry, actors):
    """Stand this map's nav-area marks beside its world collision, before the meshes are cut.

    Two marks, both from rows this lane staged:

      * the priced roadway -- the `---p` signature, `0x2000` with no clip bit, which is the only
        one that reaches a link. Cost 1: it is not cheaper ground, it is ground story 5's
        pedestrian query filter prefers, and marking it now is what lets that story be a filter
        change rather than a re-bake.
      * a null area over every door NO graph link runs through. Retail's graph builds through a
        standing door (the build mask is the one without `MOVEABLE`) while every run-time probe
        finds it solid, so a door with a link is one NPCs use and a door without one is a wall.
        The doors that DO carry a link are left alone: story 7 gives them their cut and their
        smart link together, so no commit in between turns a door NPCs use into a wall.

    Placed before the build, because an area mark only reaches tiles that are rasterised after it.
    """
    for existing in actors.get_all_level_actors():
        if isinstance(existing, unreal.ElysiumNavAreaActor):
            actors.destroy_actor(existing)

    areas = entry.get("navAreas") or []
    doors = entry.get("navDoors") or {}
    cut_rows = [row for row in doors.get("rows", []) if not row.get("traversable")]
    if not areas and not cut_rows:
        if doors.get("skipped"):
            log("%s: no door answer (%s); no door is cut" % (entry["map"], doors["skipped"]))
        return 0

    actor = actors.spawn_actor_from_class(
        unreal.ElysiumNavAreaActor, unreal.Vector(0.0, 0.0, 0.0))
    if actor is None:
        raise RuntimeError("could not spawn the nav-area actor in %s" % entry["map"])
    actor.set_actor_label("ElysiumNavAreas")
    actor.author(entry["map"])

    placed = 0
    for row in areas:
        points, sizes = _flatten_hulls(row["hulls"])
        if actor.add_area("pedestrian", unreal.ElysiumNavArea_Pedestrian, points, sizes):
            placed += len(sizes)

    # One convex per cut door, from the same staged hulls its collision body is cooked from, moved
    # into world space by the entity origin those hulls are stated relative to.
    door_points, door_sizes = [], []
    for row in cut_rows:
        origin = row.get("originCm") or [0.0, 0.0, 0.0]
        body = next((b for b in entry.get("brushBodies", [])
                     if int(b["entityIndex"]) == int(row["entityIndex"])), None)
        if body is None:
            continue
        points, sizes = _flatten_hulls(body["hulls"], origin)
        door_points.extend(points)
        door_sizes.extend(sizes)
    if door_sizes and actor.add_area("doorcut", unreal.ElysiumNavArea_DoorCut,
                                     door_points, door_sizes):
        placed += len(door_sizes)

    log("%s: %d nav-area convex(es) -- roadway %d, doors cut %d of %d (%d carry a link)"
        % (entry["map"], placed, sum(len(r["hulls"]) for r in areas), len(cut_rows),
           doors.get("doors", 0), doors.get("traversable", 0)))
    return placed


def _flatten_hulls(hulls, origin=(0.0, 0.0, 0.0)):
    """`[[x,y,z,...], ...]` to one `unreal.Vector` list plus the per-convex point counts."""
    points, sizes = [], []
    for hull in hulls:
        count = 0
        for index in range(0, len(hull) - 2, 3):
            points.append(unreal.Vector(
                float(hull[index]) + float(origin[0]),
                float(hull[index + 1]) + float(origin[1]),
                float(hull[index + 2]) + float(origin[2])))
            count += 1
        sizes.append(count)
    return points, sizes


def prune_unwanted_navmeshes(map_name, actors, wanted_agents):
    """Destroy every navigation mesh in the level whose agent this map did not ask for.

    `UNavigationSystemV1::Build` spawns missing navigation data for every SUPPORTED agent, mask or
    no mask, so a level that named two ends up holding fourteen -- twelve of them empty. They are
    not harmless clutter: the runtime reads any saved mesh as "already built", and each one asks
    Recast for a tile grid it then clamps, an error per agent per load.

    Done from Python, over the editor's own actor list, because that list sees them: the same
    sweep in C++ over the world's actors and over NavDataSet both come up empty, so whatever holds
    these twelve is not either of those.
    """
    if not wanted_agents:
        return 0
    keep = {"RecastNavMesh-%s" % name for name in wanted_agents}
    dropped = []
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.RecastNavMesh) and actor.get_name() not in keep:
            dropped.append(actor.get_name())
            actors.destroy_actor(actor)
    if dropped:
        log("%s: dropped %d unused navigation mesh(es): %s"
            % (map_name, len(dropped), ", ".join(sorted(dropped))))
    return len(dropped)


def used_hull_bits(map_name):
    """The map's own `UsedHullBits`, staged by the jump-link lane, or None when it has not run.

    It is the graph's answer to "which hulls do my links serve", so it is also the answer to
    "which agents does this map need a mesh for". Both witnesses are `0x80001`: human and rat.

    PRESENCE, not truth: a staged 0 is a graph that names no hull at all, which is a different
    failure from a lane that never ran, and the two get different diagnostics. No shipped `.ain`
    carries one -- the eleven whose `NumNodes` is 0 all still name a hull -- but conflating them
    would report the wrong remedy on the day one does.
    """
    root = os.environ.get("ELYSIUM_WORK_ROOT")
    if not root:
        return None
    path = os.path.join(root, "import", "map_geometry", map_name, "manifest.json")
    if not os.path.isfile(path):
        return None
    with open(path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    bits = (manifest.get("jumpLinks") or {}).get("usedHullBits")
    return None if bits is None else int(bits)


def build_navigation(map_name, world):
    """Build this level's navigation meshes and leave them in it to be saved.

    Done here, right after the collision actor is placed, because that actor IS the geometry a
    mesh can be cut from: the baked world meshes wear `ElysiumPickOnly` and ignore both pawn
    channels, so before it existed the level held nothing walkable and Recast would have saved an
    empty mesh -- success-looking, failing later as NPCs that never find a path. The
    relevant-component count and the per-agent tile check below are what turn that silent failure
    into a loud one.

    The agent set is the map's own `UsedHullBits`, and it is applied by CREATING the navigation
    system from it: masking a system that already exists leaves data spawned for every supported
    agent, because editor-mode creation spawns the missing data as it initialises.
    """
    bits = used_hull_bits(map_name)
    if bits is None:
        # There is no run-time build to fall back on any more (0018 story 21): a level saved with
        # no mesh fails the load after its grace window. Refusing here names the missing lane
        # instead of shipping a level that cannot be entered.
        raise RuntimeError(
            "%s: no staged UsedHullBits, so no agent set to build for; run: "
            "uv run elysium export_v2 nav-graph-glb %s && uv run elysium bake map --maps %s"
            % (map_name, map_name, map_name))

    agents = unreal.ElysiumNavBakeLibrary.create_navigation_for_agents(world, bits)
    if not agents:
        raise RuntimeError("%s: UsedHullBits %#x named no supported agent" % (map_name, bits))

    relevant = unreal.ElysiumNavBakeLibrary.count_navigation_relevant_components(world)
    if not relevant:
        raise RuntimeError(
            "%s: no navigation-relevant component in the level; a mesh built now would be empty"
            % map_name)
    log("%s: %d navigation-relevant component(s), agents %s"
        % (map_name, relevant, ", ".join(agents)))

    bounds = unreal.ElysiumNavBakeLibrary.navigation_bounds_of(world)
    if not unreal.ElysiumNavBakeLibrary.place_nav_bounds(world, bounds):
        raise RuntimeError("%s: could not place the navigation bounds" % map_name)

    rows = unreal.ElysiumNavBakeLibrary.build_agent_nav_meshes(world)
    if not rows:
        raise RuntimeError("%s: the navigation build produced no mesh" % map_name)
    built = set()
    for row in rows:
        log("%s: agent %-16s r%6.2f h%7.2f -> %d tile(s), %d byte(s), %.2fs"
            % (map_name, row.agent, row.agent_radius, row.agent_height,
               row.tiles, row.bytes, row.seconds))
        if not row.tiles:
            raise RuntimeError(
                "%s: agent %s built 0 tiles; the mesh would save empty" % (map_name, row.agent))
        built.add(row.agent)
    missing = [name for name in agents if name not in built]
    if missing:
        raise RuntimeError("%s: no mesh for %s" % (map_name, ", ".join(missing)))
    extra = [name for name in built if name not in agents]
    if extra:
        # An agent this map's graph never named BUILT a mesh, which the mask should have
        # prevented. (Agents that were spawned empty are pruned before the save instead.)
        raise RuntimeError(
            "%s: built meshes for agents the map does not use: %s" % (map_name, ", ".join(extra)))
    return agents


def run(manifest_path, force=False):
    manifest = load_manifest(manifest_path)
    staging_root = os.path.dirname(os.path.abspath(manifest_path))
    entries = manifest["maps"]
    log("manifest %s: %d map(s)%s" % (manifest_path, len(entries), " (forced)" if force else ""))

    # A fresh commandlet has not indexed the mount; the stamps every reuse decision reads live on
    # the registry.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [manifest["mount"]], force_rescan=True)

    report = {
        "manifest": manifest_path,
        "imported": 0,
        "reused": 0,
        "failed": [],
        "maps": [],
        "stageFailures": manifest.get("stageFailures", []),
    }
    for entry in entries:
        started = time.time()
        try:
            outcome = author_map(entry, force=force)
        except Exception as exc:  # noqa: BLE001 - one map's failure is not the run's
            report["failed"].append({"map": entry["map"], "assetPath": entry["assetPath"],
                                     "reason": "%s" % exc})
            fail("%s: %s" % (entry["map"], exc))
            unreal.log_error(traceback.format_exc())
            continue
        report[outcome] += 1
        stats = entry.get("stats", {})
        report["maps"].append({
            "map": entry["map"],
            "assetPath": entry["assetPath"],
            "worldHulls": stats.get("worldHulls", len(entry["worldHulls"])),
            "displacementTriangles": stats.get("displacementTriangles", 0),
            "brushBodies": stats.get("brushBodies", len(entry.get("brushBodies", []))),
            "outcome": outcome,
            "seconds": round(time.time() - started, 2),
        })
        log("%s: %d world hull(s), %d disp tri(s), %d brush body(ies) %s -> %s"
            % (entry["map"], stats.get("worldHulls", 0), stats.get("displacementTriangles", 0),
               stats.get("brushBodies", 0), outcome, entry["assetPath"]))

    report_path = os.path.join(staging_root, "import_report.json")
    with open(report_path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
    log("%d imported / %d reused / %d failed -> %s"
        % (report["imported"], report["reused"], len(report["failed"]), report_path))
    return report


def main():
    manifest_path = cmdline_arg("ImportMapCollision", "")
    if not manifest_path:
        raise SystemExit("[import-map-collision] -ImportMapCollision=<manifest.json> is required")
    force = flag(cmdline_arg("ImportForce", ""))
    try:
        report = run(manifest_path, force=force)
    except (ManifestError, OSError, ValueError) as exc:
        fail("manifest refused: %s" % exc)
        raise SystemExit(1)
    if report["failed"]:
        fail("%d map(s) failed; see import_report.json" % len(report["failed"]))
        raise SystemExit(1)


main()
