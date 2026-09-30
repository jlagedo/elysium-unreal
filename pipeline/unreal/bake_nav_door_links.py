"""Author the map's door smart links: one `AElysiumNavDoorLink` per door retail's graph runs through.

0018/7. Every door is cut out of every mesh (`bake_map_collision.place_nav_areas`); each AIN link
that crosses doors -- doors the designers gave NPCs -- gets ONE smart link across the cut (retail
has one link however many door entities stand in the doorway: the hub's smoke-shop pair is one
link, 958), for exactly the agents whose hulls cross them (`crossedByHulls` ->
`UElysiumNavBakeLibrary::AgentNamesForHullBits`). The endpoints are the staged link row's
(`map_nav_doors.door_links`), and the link carries its doors' lump ordinals, which are
`FElysiumEntityHandle::Index` at run time, so a door finds its link and the link its doors. What happens at the doorway is the runtime's: the link
holds the body while the door is not open and the kernel's slot 531 opens it.
"""
import unreal

TAG_DOOR_LINK = "elysium.nav-door"


def _vector(cm):
    return unreal.Vector(float(cm[0]), float(cm[1]), float(cm[2]))


def _hull_bits(hulls):
    bits = 0
    for hull in hulls:
        bits |= 1 << int(hull)
    return bits


def author(actors, nav_doors):
    """Spawn one door link per staged `navDoors["links"]` row (one per witness AIN link, over every
    door it crosses); answers how many were placed."""
    nav_doors = nav_doors or {}
    links = nav_doors.get("links")
    if links is None:
        if nav_doors.get("traversable"):
            raise ValueError("the door answer carries no link rows; stage the map again")
        links = []
    placed = 0
    for row in links:
        start, end = row["startCm"], row["endCm"]
        doors = [int(door) for door in row["doors"]]
        actor = actors.spawn_actor_from_class(unreal.ElysiumNavDoorLink, _vector(start))
        if actor is None:
            raise ValueError("cannot spawn the smart link on AIN link %s" % row["witness"])
        actor.configure_door_link(unreal.Vector(0, 0, 0),
                                  unreal.Vector(*[float(end[i]) - float(start[i]) for i in range(3)]),
                                  doors, int(row["witness"]))
        names = list(unreal.ElysiumNavBakeLibrary.agent_names_for_hull_bits(
            _hull_bits(row["crossedByHulls"])))
        resolved = actor.set_supported_agent_names(names)
        if not names or resolved != len(names):
            raise ValueError("AIN link %s: %d of the agents %s are supported" % (
                row["witness"], resolved, names))
        actor.set_actor_label("NavDoor_%d_%s" % (int(row["witness"]), "_".join(str(d) for d in doors)))
        actor.set_folder_path("Navigation")
        actor.tags = [TAG_DOOR_LINK]
        placed += 1
    unreal.log("door smart links: %d over %d traversable door(s) of %d (%d cut with no link); "
               "%d door(s) partial by agent" % (
                   placed, nav_doors.get("traversable", 0), nav_doors.get("doors", 0),
                   nav_doors.get("cut", 0), nav_doors.get("partialByAgent", 0)))
    return placed
