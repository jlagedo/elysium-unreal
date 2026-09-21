"""Stage one map's BSP-authored AI infrastructure rows for the baked-actor lane (0018 story 2).

Five families, a disjoint partition of the entity lump, one baked actor per row:

* ``hint`` -- every row retail's ``CNodeEnt::Spawn`` (``0x102d78d0``) turns into a ``CAI_Hint``
  (live classname ``ai_hint``); patrol points are hints of type 10000.
* ``place`` -- ``intersting_place`` (retail's spelling).
* ``conversation`` -- ``intersting_place_conversation``.
* ``maker`` -- ``npc_maker``, ``npc_maker_fleshpile``, ``npc_maker_zombie``.
* ``npc`` -- every other ``npc_*`` classname.

A staged row is the entity table's own row (``UE_map_sidecars.collect_entity_fields`` over the
same pair reading ``.ents`` and ``UElysiumMapEntities`` are built from), plus the ordered authored
pairs that reading folds away. So the def the runtime rebuilds from a baked actor is the def the
transports load, byte for byte, unless someone edited the actor.

Malformed input raises instead of being repaired: a hint row the census rule and the retail rule
classify differently, an authored hint type that disagrees with the class-forced one, a parented
hint row (it would spawn out of BSP order and move the hint list), or a row in the 3D-skybox
miniature.
"""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from typing import Any, Sequence

from elysium_pipeline.exporters import UE_map_sidecars as producer
from elysium_pipeline.formats.bsp import source_angles_to_unreal_quat, source_to_unreal

#: 1: the first recipe (0018 story 2).
RECIPE_VERSION = 1

FAMILIES = ("hint", "place", "conversation", "maker", "npc")

PLACE_CLASSNAME = "intersting_place"
CONVERSATION_CLASSNAME = "intersting_place_conversation"
MAKER_CLASSNAMES = frozenset({"npc_maker", "npc_maker_fleshpile", "npc_maker_zombie"})

#: Every `info_node*` string in vampire.dll's entity-factory table except `info_node_link`
#: (`CAI_DynamicLink`), plus `info_hint`: the classnames `CNodeEnt` is created for. Restated from
#: `Source/ElysiumUE/Private/Substrate/ElysiumNodeEntity.cpp`, which the runtime applies.
NODE_CLASSNAMES = frozenset({
    "info_hint", "info_node", "info_node_air", "info_node_air_hint",
    "info_node_bach_run_1", "info_node_bach_run_2", "info_node_bach_teleport_1",
    "info_node_bach_teleport_2", "info_node_bach_teleport_3", "info_node_bach_teleport_4",
    "info_node_chang_column", "info_node_chang_jumpbase", "info_node_chang_ledge",
    "info_node_chang_teleport", "info_node_climb", "info_node_cover_corner", "info_node_cover_low",
    "info_node_cover_med", "info_node_crosswalk", "info_node_hint", "info_node_kick_at",
    "info_node_kick_over", "info_node_manbat_fly_to_point", "info_node_patrol_point",
    "info_node_sabbat_arch", "info_node_sabbat_bottom", "info_node_sabbat_dive",
    "info_node_sabbat_hide", "info_node_sabbat_nojump", "info_node_sabbat_top",
    "info_node_shoot_at", "info_node_tzimisce", "info_node_tzimisce_claw_left",
    "info_node_tzimisce_claw_right", "info_node_werewolf", "info_node_werewolf_hint",
})

#: `FUN_102d7d30`: the hint type a node's classname forces.
FORCED_HINT_TYPES = {
    "info_node": 0, "info_node_cover_med": 100, "info_node_cover_low": 101,
    "info_node_cover_corner": 10200, "info_node_crosswalk": 11000,
    "info_node_tzimisce_claw_left": 14000, "info_node_tzimisce_claw_right": 14001,
    "info_node_kick_over": 10300, "info_node_kick_at": 10301, "info_node_shoot_at": 10400,
    "info_node_werewolf": 0, "info_node_sabbat_bottom": 16000, "info_node_sabbat_top": 16001,
    "info_node_sabbat_arch": 16002, "info_node_sabbat_hide": 16003,
    "info_node_sabbat_nojump": 16004, "info_node_sabbat_dive": 16005,
    "info_node_bach_teleport_1": 17000, "info_node_bach_teleport_2": 17001,
    "info_node_bach_teleport_3": 17002, "info_node_bach_teleport_4": 17003,
    "info_node_bach_run_1": 17004, "info_node_bach_run_2": 17005,
    "info_node_chang_jumpbase": 18000, "info_node_chang_column": 18001,
    "info_node_chang_teleport": 18002, "info_node_chang_ledge": 18003,
    "info_node_manbat_fly_to_point": 20000,
}

#: `CNodeEnt::Spawn`'s standalone set: a hint only for a non-zero type.
STANDALONE_HINT_CLASSNAMES = frozenset({
    "info_hint", "info_node_kick_over", "info_node_kick_at", "info_node_shoot_at"})

#: The two keys the entity table hoists out of `keys` (`build_entities`' `keys.pop`).
HOISTED_KEYS = ("classname", "targetname")


class MapAiInfraError(ValueError):
    """A map's infrastructure rows cannot be staged faithfully."""


def atoi(text: str) -> int:
    """C `atoi`: optional whitespace and sign, then the longest digit run; 0 when there is none."""
    match = re.match(r"\s*([+-]?\d+)", text or "")
    return int(match.group(1)) if match else 0


def _short(value: int) -> int:
    """The `short` both `FUN_102d7d30` and `CNodeEnt::Spawn` read `m_eHintType` through."""
    value &= 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def _folded_get(pairs: Sequence[tuple[str, str]], key: str) -> str | None:
    """The last value of `key` compared case-insensitively, as the datamap parse resolves it."""
    found = None
    for pair_key, value in pairs:
        if pair_key.lower() == key.lower():
            found = value
    return found


def class_hint_type(classname: str, authored: int) -> int:
    """`FUN_102d7d30`: the class-forced hint type, or the authored one for an unlisted class."""
    name = classname.lower()
    if name == "info_node_werewolf_hint":
        short = _short(authored)
        return 0 if short < 15000 or short > 15018 else short
    return FORCED_HINT_TYPES.get(name, authored)


def makes_hint(classname: str, pairs: Sequence[tuple[str, str]]) -> bool:
    """`CNodeEnt::Spawn`'s decision over one row's classname and authored pairs."""
    name = classname.lower()
    if name not in NODE_CLASSNAMES:
        return False
    if name == "info_node_tzimisce":
        name = "info_node"
    hint_type = _short(class_hint_type(name, atoi(_folded_get(pairs, "hinttype") or "")))
    if name in STANDALONE_HINT_CLASSNAMES:
        return hint_type != 0
    return hint_type != 0 or bool(_folded_get(pairs, "Group"))


def census_hint(classname: str, pairs: Sequence[tuple[str, str]]) -> bool:
    """Story 1's census rule: an `info_node_*` / `info_hint` row that carries a `hinttype` key."""
    name = classname.lower()
    return ((name.startswith("info_node_") or name == "info_hint")
            and _folded_get(pairs, "hinttype") is not None)


def family_of(classname: str, pairs: Sequence[tuple[str, str]]) -> str | None:
    """The baked family of one row, or None for a row that stays on the entity table alone."""
    name = classname.lower()
    if makes_hint(classname, pairs):
        return "hint"
    if name == PLACE_CLASSNAME:
        return "place"
    if name == CONVERSATION_CLASSNAME:
        return "conversation"
    if name in MAKER_CLASSNAMES:
        return "maker"
    if name.startswith("npc_"):
        return "npc"
    return None


def _authored_keys(pairs: Sequence[tuple[str, str]], skip: set[int]) -> list[list[str]]:
    """The pairs `collect_entity_fields` keeps as keys, in order with repeats, minus the hoisted
    two. `skip` is `producer.output_pair_indexes`' answer: which keyvalues the unit's own parser
    turned into output rows."""
    out: list[list[str]] = []
    for position, (key, value) in enumerate(pairs):
        if position in skip or key in HOISTED_KEYS:
            continue
        out.append([key, value])
    return out


def stage_rows(map_name: str, entity_rows: Sequence[dict[str, Any]],
               sky: Any | None = None) -> dict[str, Any]:
    """The staged payload for one map, from its structured entity rows.

    `sky` is the map's `SkyScope`; a family row inside the miniature is refused.
    """
    pair_blocks = producer.entity_pair_blocks(entity_rows)
    rows: list[dict[str, Any]] = []
    counts = {family: 0 for family in FAMILIES}
    patrol = 0
    for index, (entity, pairs) in enumerate(zip(entity_rows, pair_blocks)):
        if int(entity.get("index", index)) != index:
            raise MapAiInfraError(f"{map_name}: entity row {index} carries index {entity.get('index')}")
        classname = str(entity.get("classname") or "")
        family = family_of(classname, pairs)
        if census_hint(classname, pairs) != (family == "hint"):
            raise MapAiInfraError(f"{map_name} entity {index} ({classname}): the census and "
                                  "CNodeEnt::Spawn disagree on whether it is a hint")
        if family is None:
            continue
        outputs, keys = producer.collect_entity_fields(entity)
        if family == "hint":
            authored = atoi(_folded_get(pairs, "hinttype") or "")
            forced = class_hint_type(classname, authored)
            if classname.lower() in FORCED_HINT_TYPES and _short(forced) != _short(authored):
                raise MapAiInfraError(f"{map_name} entity {index} ({classname}): authored hinttype "
                                      f"{authored} differs from the class-forced {forced}")
            if keys.get("parentname"):
                raise MapAiInfraError(f"{map_name} entity {index} ({classname}): a parented hint "
                                      "spawns out of BSP order")
            patrol += 1 if classname.lower() == "info_node_patrol_point" else 0
        tokens = keys.get("origin", "").split()
        origin_src = [producer.atof(token) for token in tokens] if len(tokens) == 3 else [0.0, 0.0, 0.0]
        if sky is not None and sky.is_sky(sky.entity_point(origin_src, keys.get("model", ""))):
            raise MapAiInfraError(f"{map_name} entity {index} ({classname}): an infrastructure row "
                                  "in the 3D-skybox miniature is not staged")
        angles = keys.get("angles", "").split()
        pitch_yaw_roll = [producer.atof(a) for a in angles] if len(angles) == 3 else [0.0, 0.0, 0.0]
        counts[family] += 1
        rows.append({
            "index": index,
            "family": family,
            "classname": _last(pairs, "classname", ""),
            "targetname": _last(pairs, "targetname", ""),
            "originCm": [round(float(c), 5) for c in source_to_unreal(*origin_src)],
            "rotationQuat": [round(float(c), 6) for c in source_angles_to_unreal_quat(*pitch_yaw_roll)],
            "keys": _authored_keys(pairs, producer.output_pair_indexes(entity)),
            "outputs": [
                {"name": row["name"], "target": row["target"], "input": row["input"],
                 "param": row["param"], "delay": float(row["delay"]), "times": int(row["times"]),
                 "python": row["python"]}
                for row in outputs
            ],
        })
    result = {
        "version": RECIPE_VERSION,
        "map": map_name,
        "entityRows": len(entity_rows),
        "counts": {**counts, "patrol": patrol, "actors": len(rows)},
        "rows": rows,
    }
    result["sha256"] = hashlib.sha256(
        json.dumps(result, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return result


def _last(pairs: Sequence[tuple[str, str]], key: str, default: str) -> str:
    """The last value of an exact-spelling key: `build_entities`' `keys.pop(key, default)`."""
    found = default
    for pair_key, value in pairs:
        if pair_key == key:
            found = value
    return found


def stage_for_join(join: Any, map_name: str) -> dict[str, Any]:
    """The payload from a prepared `MapJoin` (the map-geometry stage already holds one)."""
    return stage_rows(map_name, join.units.entities["entities"], join.sky)


def stage_map(map_name: str, root: Path | None = None) -> dict[str, Any]:
    """The payload for one map straight from its units, without meshing its geometry."""
    units = producer.read_units(map_name, root)
    sky = producer.SkyScope(units, producer.entity_pair_blocks(units.entities["entities"]))
    return stage_rows(map_name, units.entities["entities"], sky)
