"""The collision and navigation half of `uv run elysium bake map`.

Two products, one lane, because one lane authoring both is what keeps them from disagreeing:

  * `/ElysiumBaked/<map>/DA_<map>_Collision`, a cooked `UElysiumMapCollisionPayload` -- one body
    per contents signature the map carries, plus the displacement surface and one body per brush
    entity;
  * what stands in the map's own level: the world-collision actor pointing at those bodies, the
    nav-area marks, and the Recast meshes cut from them for the agents the map's own graph names.

Runs inside the bake's own editor session, called from `bake_map.Bake`. The offline stage
(`importers/map_collision.py`, R4.2) already read `<map>.hulls`, `<map>.dispcol` and the
brush-entity `hulls` of `<map>.ents`, applied the one transform the runtime applies (the 3D-skybox
scale) and asserted parity against those files; nothing is decided here. Every number is copied
from the manifest verbatim and every body-setup flag is set by the asset's own C++ authoring
functions, so the recipe has one owner.

Until 0018 story 21-2 this was `import_map_collision.py`, phase 2 of a command that ran AFTER
`bake map` and re-opened the level the bake had just written. It carried a whole routine
(`level_collision_is_current`) whose only job was to notice a level the bake had wiped. Inside one
session there is no such window: the level is authored, this lane's actors and meshes go into it
before it is ever written, and the one save carries all of it.
"""
from __future__ import annotations

import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from pipeline.unreal import bake_lib as bl  # noqa: E402

#: The recipe stage label every fingerprint hashes under -- the lane's own name. It is also the
#: producer tag on the asset, so this lane's stamps never collide with the map bake's own.
STAGE = "map_collision"
PRODUCER = "map-collision"

#: Manifest schema this module understands.
MANIFEST_SCHEMA = "2.0.0"

#: The one class this lane authors.
ASSET_CLASS = "ElysiumMapCollisionPayload"

#: What an entry must carry to be executable.
REQUIRED_KEYS = ("map", "assetPath", "packageRoot", "worldHulls", "parity")


def log(msg):
    unreal.log("[bake-map-collision] %s" % msg)


def fail(msg):
    unreal.log_error("[bake-map-collision] %s" % msg)


def load_manifest(path):
    return bl.load_stage_manifest(
        path, schema=MANIFEST_SCHEMA, required=REQUIRED_KEYS, lane="map-collision")


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


def payload_fingerprint(entry):
    """The hash the payload is stamped with -- and, since 21-2, part of the level's own recipe.

    The level recipe needs it because `reset_authoring` replaces every `UBodySetup` subobject: a
    re-cooked payload leaves a level whose world-collision actor points at bodies that no longer
    exist, which the runtime refuses at load. Nothing the level recipe already digests can say
    that the bodies were replaced.
    """
    return bl.recipe_fingerprint(
        STAGE, entry["assetPath"],
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
            "navDoors": entry.get("navDoors", {}),
            "navAreas": entry.get("navAreas", []),
        },
    )


def author_payload(entry, force=False):
    """Author or reuse one map's `UElysiumMapCollisionPayload`. Returns (asset, outcome).

    The level is not touched here. Whether the level carries this payload's bodies is no longer a
    question anything has to ask: the bake authors the level in the same session, after this, and
    places the actor unconditionally.
    """
    object_path = entry["assetPath"]
    package_root, asset_name = bl.split_asset_path(object_path)
    fingerprint = payload_fingerprint(entry)

    if (not force and unreal.EditorAssetLibrary.does_asset_exist(object_path)
            and bl.asset_class_name(object_path) == ASSET_CLASS
            and bl.stored_recipe(object_path, producer=PRODUCER) == fingerprint):
        asset = unreal.load_asset(object_path)
        if asset is not None:
            log("%s: payload reused -> %s" % (entry["map"], object_path))
            return asset, "reused"

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

    # Cook now, in the bake, so the DDC entry exists before any map load and a payload that cannot
    # cook is this run's failure rather than a silent fallback at runtime.
    failure = asset.cook_authored()
    if failure:
        raise RuntimeError("collision cook failed for %s (%s)" % (object_path, failure))

    bl.stamp_recipe(asset, fingerprint, producer=PRODUCER)
    if not bl.save(object_path):
        raise RuntimeError("save failed: %s" % object_path)

    stats = entry.get("stats", {})
    log("%s: %d world hull(s), %d disp tri(s), %d brush body(ies) cooked -> %s"
        % (entry["map"], stats.get("worldHulls", 0), stats.get("displacementTriangles", 0),
           stats.get("brushBodies", 0), object_path))
    return asset, "imported"


def place_world_collision(entry, asset, actors):
    """Stand the map's world collision in its level, as one static component per signature.

    The runtime used to build these into transient components at map load, which meant the level
    held no navigation-relevant geometry at all and a navigation mesh could only ever be generated
    at run time. Saved in the level, they are what Recast can bake a mesh FROM -- and from the
    right solids, since a body affects navigation exactly when its contents signature blocks an
    NPC, so the NPC-only clips cut the mesh and the sight-only brushes do not.

    Nothing is swept first. The level this authors into is the blank world `bake_one` opened, so
    there is no previous actor and no stale Recast mesh to find; the sweep this carried while it
    ran as its own command existed only because it re-opened a level that already had them.
    """
    actor = actors.spawn_actor_from_class(
        unreal.ElysiumWorldCollisionActor, unreal.Vector(0.0, 0.0, 0.0))
    if actor is None:
        raise RuntimeError("%s: could not spawn the world-collision actor" % entry["map"])
    actor.set_actor_label("ElysiumWorldCollision")
    placed = actor.author_from_payload(asset)
    if not placed:
        raise RuntimeError("%s: the payload authored no world body" % entry["map"])
    log("%s: %d world collision body(ies) stand in the level" % (entry["map"], placed))
    return placed


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


def build_navigation(map_name, world, hull_bits):
    """Build this level's navigation meshes and leave them in it to be saved.

    Done after the collision actor is placed, because that actor IS the geometry a mesh can be cut
    from: the baked world meshes wear `ElysiumPickOnly` and ignore both pawn channels, so before
    it existed the level held nothing walkable and Recast would have saved an empty mesh --
    success-looking, failing later as NPCs that never find a path. The relevant-component count
    and the per-agent tile check below are what turn that silent failure into a loud one.

    The agent set is the map's own `UsedHullBits`, and it is applied by CREATING the navigation
    system from it: masking a system that already exists leaves data spawned for every supported
    agent, because editor-mode creation spawns the missing data as it initialises.
    """
    if hull_bits is None:
        # There is no run-time build to fall back on any more (0018 story 21): a level saved with
        # no mesh fails the load after its grace window. Refusing here names the missing lane
        # instead of shipping a level that cannot be entered.
        raise RuntimeError(
            "%s: no staged UsedHullBits, so no agent set to build for; run: "
            "uv run elysium export_v2 nav-graph-glb %s" % (map_name, map_name))

    agents = unreal.ElysiumNavBakeLibrary.create_navigation_for_agents(world, hull_bits)
    if not agents:
        raise RuntimeError("%s: UsedHullBits %#x named no supported agent" % (map_name, hull_bits))

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


def saved_navigation_is_complete(map_name, level_path, hull_bits):
    """Whether the SAVED level carries a mesh with tiles for every agent the map asked for.

    The build above asserted this of the world in memory. That the meshes survived the save is a
    separate fact and it is the one the runtime depends on -- an empty mesh saves, loads and reads
    as "already built" exactly like a full one. Until 21-2 nothing in the bake ever asked it; the
    only thing that ever did was the collision import's staleness check, on the NEXT run, and the
    fold retired that check. So the bake asks it of its own output, the way the capture re-count
    does.

    **Through the editor's loader, not `LoadPackage`.** This was written first as a C++ helper
    that loaded the package and counted tiles, in the shape of
    `CountBuiltReflectionCapturesInPackage`, and it reported "no meshes" on a level that had just
    built 1,235 and 915 tiles. Two reasons, both fatal: `TActorIterator` walks `UWorld::Levels`,
    which a package-loaded world leaves empty, and `ARecastNavMesh::GetNumActiveTiles` reads its
    GENERATOR, which exists only once a navigation system has registered the data. The capture
    counter avoids both by reading `ULevel::Actors` and the level's own registry; there is no such
    shortcut for a tile count. Re-opening the level is what the old staleness check did, and it is
    what works.
    """
    if not unreal.EditorLoadingAndSavingUtils.load_map(level_path):
        fail("%s: %s did not re-open after the save" % (map_name, level_path))
        return False
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None:
        fail("%s: no editor world after re-opening %s" % (map_name, level_path))
        return False
    tiles = {}
    for row in unreal.ElysiumNavBakeLibrary.nav_mesh_tile_counts(world):
        name, _, count = row.rpartition("=")
        tiles[name] = int(count)
    log("%s: the saved level's navigation meshes are %s"
        % (map_name, ", ".join("%s %d tile(s)" % kv for kv in sorted(tiles.items())) or "none"))

    wanted = unreal.ElysiumNavBakeLibrary.agent_names_for_hull_bits(hull_bits)
    if not wanted:
        fail("%s: UsedHullBits %#x names no agent; the saved level owes no mesh"
             % (map_name, hull_bits))
        return False
    short = [name for name in wanted
             if tiles.get("RecastNavMesh-%s" % name, 0) <= 0]
    if short:
        fail("%s: the saved level carries no mesh with tiles for %s"
             % (map_name, ", ".join(short)))
        return False
    return True
