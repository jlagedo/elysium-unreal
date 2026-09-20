"""Build this level's navigation meshes and save them with it (0018 story 3, job 5).

Navigation is generated at RUN time today: one mesh, rebuilt on every load of every map. Nothing
about it can be inspected offline, the baked jump links point at a mesh that does not exist until
the map is playing, and the acceptance this story owes -- every ground link paths on its agent's
mesh, the rat-only links path on the rat's and not the human's -- can only be asked of a mesh that
exists before the game runs.

Which agents a map builds is the map's own answer. `UsedHullBits` in its nav graph is an OR of the
hulls its links were built for; both witnesses are `0x80001`, human and rat. The project declares
one agent per hull that carries links ANYWHERE (14 of retail's 22 rows), and this hands the level
the subset its own graph names, so no map pays for a Ming Xiao mesh it can never use.

Everything here is `UElysiumNavBakeLibrary`'s; this module only reads the manifest and reports.

**NOT WIRED INTO `bake map` YET, and the reason is not scheduling.** At bake time the level holds
no navigation-relevant geometry at all: the baked world meshes wear `ElysiumPickOnly`, which
ignores both pawn channels, and the collision bodies are TRANSIENT components the runtime builds
from the payload at map load. Recast would run over nothing and save an empty mesh -- which would
look like success and fail only later, as NPCs that never find a path.

What unblocks it is job 3's level actor: one static component per contents signature, referencing
the payload's cooked body setups, saved in the `.umap`. That is also what makes the mesh cut from
the RIGHT solids, which is the whole point of the story -- a body affects navigation exactly when
its signature blocks an NPC, so the NPC-only clips cut the mesh and the sight-only brushes do not.
Until that actor exists, navigation stays a run-time build restricted to the one agent a body
stands on (`AElysiumMapActor::RestrictNavigationToUsableAgents`), which both witnesses boot Active
with.
"""
import unreal

#: Bumped when this writer's output shape changes, so the level recipe re-authors.
NAVMESH_ACTOR_SHAPE = 1


def log(message):
    unreal.log("[bake-navmesh] %s" % message)


def author(world, payload, bounds):
    """Give `world` the agents `payload` names, place `bounds`, build, and report per agent.

    `payload` is the manifest's `jumpLinks` block, which carries the graph's own `usedHullBits`.
    `bounds` is the world collision union, already padded by the caller.

    Returns the per-agent build rows. An empty list is a failure the caller must treat as one:
    a level with no navigation mesh is a level no NPC can move on.
    """
    hull_bits = int(payload.get("usedHullBits", 0))
    if not hull_bits:
        raise ValueError(
            "map manifest carries no usedHullBits; re-stage the V2 map so the bake knows which "
            "agents this map's graph uses")

    agents = unreal.ElysiumNavBakeLibrary.set_map_nav_agents(world, hull_bits)
    if not agents:
        raise ValueError("UsedHullBits %#x named no supported agent; re-run "
                         "`elysium research gen_hull_table`" % hull_bits)
    log("agents from UsedHullBits %#x: %s" % (hull_bits, ", ".join(agents)))

    if not unreal.ElysiumNavBakeLibrary.place_nav_bounds(world, bounds):
        raise ValueError("could not place the navigation bounds volume")

    rows = unreal.ElysiumNavBakeLibrary.build_agent_nav_meshes(world)
    if not rows:
        raise ValueError("the navigation build produced no mesh")
    for row in rows:
        log("  %-18s radius %6.2f height %7.2f -> %d tile(s), %d byte(s), %.2fs" % (
            row.agent, row.agent_radius, row.agent_height, row.tiles, row.bytes, row.seconds))
    built = {row.agent for row in rows}
    missing = [name for name in agents if name not in built]
    if missing:
        # A declared agent with no mesh is a hole nothing will report at run time: the NPC simply
        # never finds a path and the failure looks like behaviour.
        raise ValueError("no mesh was built for %s" % ", ".join(missing))
    return rows
