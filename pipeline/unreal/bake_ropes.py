"""Author the map's overhead cables as baked actors in the current baked map world (0018 story 21-3).

One `AElysiumRopeActor` per staged segment, standing at the segment's A endpoint and carrying the
eight facts `UElysiumMapVisuals::BuildRopes` builds a `UCableComponent` from. The actor draws
nothing; it is the serialized rope def and the thing a designer can select in the Outliner.

The editor half decides nothing: the rows are the producer's (`UE_map_sidecars.rope_rows` through
`importers/map_ropes.py`), already in centimetres and already carrying the RE'd `CRopeKeyframe`
state.
"""
import unreal

#: The payload recipe this writer reads (`map_ropes.RECIPE_VERSION`).
RECIPE_VERSION = 1

#: The shape `author` writes a row as -- bumped when the writer changes what it puts on the actor
#: for the same staged row, so the level re-authors.
ROPE_ACTOR_SHAPE = 1

#: Restates `Source/ElysiumUE/Public/ElysiumBakedTags.h`.
TAG_ROPE = "elysium.rope"
TAG_SOURCE_PREFIX = "elysium.src="
FOLDER = "Ropes"

MATERIAL_PREFIX = "vtmb:material:"


def validate_payload(payload):
    if not isinstance(payload, dict) or payload.get("version") != RECIPE_VERSION:
        raise ValueError("missing/unsupported rope bake payload")
    seen = set()
    for row in payload["rows"]:
        if row["index"] in seen:
            raise ValueError("rope %s staged twice" % row["index"])
        seen.add(row["index"])
        if not str(row["materialId"]).startswith(MATERIAL_PREFIX):
            raise ValueError("rope %s names %r, not a %s id"
                             % (row["index"], row["materialId"], MATERIAL_PREFIX))


def actor_label(row):
    """`Rope_12_cable`: the index a designer matches against the staged row, then the material's
    own stem, which is what distinguishes a washing line from a hanging chain at a glance."""
    stem = str(row["materialId"])[len(MATERIAL_PREFIX):].rsplit("/", 1)[-1]
    return "Rope_%d_%s" % (row["index"], stem or "cable")


def author(actors, payload):
    """Spawn every staged segment's actor; answer the actor count."""
    validate_payload(payload)
    placed = 0
    materials = set()
    for row in payload["rows"]:
        a = unreal.Vector(*row["aCm"])
        actor = actors.spawn_actor_from_class(unreal.ElysiumRopeActor, a)
        if actor is None:
            raise ValueError("cannot spawn rope %d" % row["index"])
        actor.configure_rope(
            row["index"], row["materialId"], a, unreal.Vector(*row["bCm"]),
            float(row["widthCm"]), float(row["restCm"]), int(row["nodes"]),
            float(row["texScale"]), int(row["flags"]))
        actor.set_actor_label(actor_label(row))
        actor.set_folder_path(FOLDER)
        actor.tags = [TAG_ROPE, "%s%d" % (TAG_SOURCE_PREFIX, row["index"])]
        materials.add(row["materialId"])
        placed += 1
    unreal.log("ropes: %d cable actors over %d material(s)" % (placed, len(materials)))
    return placed
