"""Author the BSP-authored AI infrastructure actors into the current baked map world (0018 story 2).

One native actor per staged row -- a hint, an interesting place, a conversation place, a maker or a
placed NPC -- carrying its entity index, its authored classname and targetname, every authored
keyvalue pair in order, and its output rows; then the one `AElysiumInfraIndex` that declares the
set. The runtime adopts them by tag (`ElysiumBakedTags::Infra*`) and rebuilds each entity def at
its own index before the entity world is built.

The editor half decides nothing: the rows are the stage's (`importers/map_ai_infra.py`), and the
typed keyfield parse is the actor's own C++ setter, so the bake never re-implements `atoi`/`atof`.
"""
import unreal

#: The payload recipe this writer reads (`map_ai_infra.RECIPE_VERSION`).
RECIPE_VERSION = 1

#: The shape `author` writes a row as -- bumped when the writer changes what it puts on the actor
#: for the same staged row, so the level re-authors.
INFRA_ACTOR_SHAPE = 1

#: Family -> (actor class name, family tag, Outliner folder). The tags restate
#: `Source/ElysiumUE/Public/ElysiumBakedTags.h`.
FAMILIES = {
    "hint": ("ElysiumHintActor", "elysium.infra.hint", "AI/Hints"),
    "place": ("ElysiumInterestingPlaceActor", "elysium.infra.place", "AI/Places"),
    "conversation": ("ElysiumConversationPlaceActor", "elysium.infra.conversation", "AI/Conversation"),
    "maker": ("ElysiumNpcMakerActor", "elysium.infra.maker", "AI/Makers"),
    "npc": ("ElysiumNpcPlacementActor", "elysium.infra.npc", "AI/NPCs"),
}
TAG_INDEX = "elysium.infra.index"
TAG_ENTITY_PREFIX = "elysium.ent="
#: Patrol points are hints; the Outliner shows them apart because the level designer thinks of them
#: apart. The actor and the tag are the hint's.
PATROL_FOLDER = "AI/PatrolPoints"


def actor_label(row):
    """`Hint_417_s1`, `Maker_1203_blueblood_maker`: family, entity index, then the name a designer
    searches for -- the patrol `Group`, else the targetname, else the authored classname."""
    prefix = {"hint": "Hint", "place": "Place", "conversation": "Conversation",
              "maker": "Maker", "npc": "Npc"}[row["family"]]
    name = row.get("targetname") or ""
    if row["classname"].lower() == "info_node_patrol_point":
        prefix = "Patrol"
        name = _last_key(row, "group") or name
    return "%s_%d_%s" % (prefix, row["index"], name or row["classname"])


def actor_folder(row):
    if row["family"] == "hint" and row["classname"].lower() == "info_node_patrol_point":
        return PATROL_FOLDER
    return FAMILIES[row["family"]][2]


def actor_tags(row):
    return [FAMILIES[row["family"]][1], "%s%d" % (TAG_ENTITY_PREFIX, row["index"])]


def _last_key(row, key):
    found = None
    for pair_key, value in row["keys"]:
        if pair_key.lower() == key:
            found = value
    return found


def validate_payload(payload):
    if not isinstance(payload, dict) or payload.get("version") != RECIPE_VERSION:
        raise ValueError("missing/unsupported AI infrastructure bake payload")
    seen = set()
    for row in payload["rows"]:
        if row["family"] not in FAMILIES:
            raise ValueError("entity %s: unknown family %r" % (row["index"], row["family"]))
        if row["index"] in seen:
            raise ValueError("entity %s staged twice" % row["index"])
        seen.add(row["index"])


def _output_structs(row):
    outputs = []
    for out in row["outputs"]:
        value = unreal.ElysiumInfraOutput()
        value.set_editor_property("name", out["name"])
        value.set_editor_property("target", out["target"])
        value.set_editor_property("input", out["input"])
        value.set_editor_property("param", out["param"])
        value.set_editor_property("delay", float(out["delay"]))
        value.set_editor_property("times", int(out["times"]))
        value.set_editor_property("python", out["python"])
        outputs.append(value)
    return outputs


def author(actors, payload):
    """Spawn every staged row's actor and the declared-set index; answer the actor count."""
    validate_payload(payload)
    placed = 0
    for row in payload["rows"]:
        class_name, _, _ = FAMILIES[row["family"]]
        actor_class = getattr(unreal, class_name)
        qx, qy, qz, qw = row["rotationQuat"]
        rotation = unreal.Quat(qx, qy, qz, qw).rotator()
        actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*row["originCm"]), rotation)
        if actor is None:
            raise ValueError("cannot spawn %s for entity %d" % (class_name, row["index"]))
        actor.configure_baked_identity(row["index"], row["classname"], row["targetname"])
        keys = [pair[0] for pair in row["keys"]]
        values = [pair[1] for pair in row["keys"]]
        if not actor.apply_baked_keyvalues(keys, values):
            raise ValueError("entity %d: keyvalue arrays rejected" % row["index"])
        actor.set_baked_outputs(_output_structs(row))
        actor.set_actor_label(actor_label(row))
        actor.set_folder_path(actor_folder(row))
        actor.tags = actor_tags(row)
        placed += 1

    index = actors.spawn_actor_from_class(unreal.ElysiumInfraIndex, unreal.Vector(0, 0, 0))
    if index is None:
        raise ValueError("cannot spawn the AI infrastructure index")
    if not index.configure_declared_set([row["index"] for row in payload["rows"]],
                                        [FAMILIES[row["family"]][1] for row in payload["rows"]]):
        raise ValueError("AI infrastructure index rejected the declared set")
    index.set_stage_sha256(payload["sha256"])
    index.set_actor_label("AI_InfrastructureIndex")
    index.set_folder_path("AI")
    index.tags = [TAG_INDEX]
    unreal.log("AI infrastructure: %d actors (%s)" % (placed, ", ".join(
        "%s %d" % (family, payload["counts"][family]) for family in FAMILIES)))
    return placed
