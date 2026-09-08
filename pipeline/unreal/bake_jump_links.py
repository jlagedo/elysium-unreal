"""Author native NavLinkProxy jump connections in the current baked map world."""
import unreal


def author(actors, payload):
    if payload.get("version") != 1:
        raise ValueError("missing/unsupported map jump-link bake payload")
    for row in payload["links"]:
        start, end = row["startCm"], row["endCm"]
        actor = actors.spawn_actor_from_class(unreal.ElysiumNavJumpLink, unreal.Vector(*start))
        if actor is None:
            raise ValueError("cannot spawn AIN jump link %s" % row["index"])
        actor.configure_jump_link(unreal.Vector(0, 0, 0),
                                  unreal.Vector(*[end[i] - start[i] for i in range(3)]),
                                  row["index"], row["src"], row["dst"])
        actor.set_actor_label("AIN_Jump_%d_%d_%d" % (row["index"], row["src"], row["dst"]))
        actor.set_folder_path("Navigation")
        actor.tags = ["elysium.nav-jump"]
    unreal.log("AIN jump links: %d human-hull NavLinkProxy actors" % len(payload["links"]))
    return len(payload["links"])
