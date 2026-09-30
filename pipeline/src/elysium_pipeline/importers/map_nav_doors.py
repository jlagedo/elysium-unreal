"""Which doors retail's graph runs through, and which are therefore walls.

Retail's door rule is a mask fact (`navigation-jump-links.md` section "Doors and NPC-clip, the
retail contract"). Three masks move an NPC and all three carry `MONSTERCLIP 0x20000`; the
graph-build mask `0x2000b` is the only one of them without `MOVEABLE 0x4000`, which is the bit a
door answers. So `CAI_Node::InitLinks` builds links straight through a standing door, while every
run-time probe finds that same door solid. A door the designers gave a link is one NPCs use; a
door with no link is a wall.

Unreal has no such distinction to inherit, so the bake has to make it: a door with no link is cut
out of every agent's mesh, and a door WITH one keeps its opening so story 7 can lay a smart link
over it. Computing that here, in the lane that already stages the door hulls, is what keeps the
answer from being a second opinion.

The test is the one the oracle's own census used for the pedestrian volumes: the brush's AABB,
grown by the agent's hull, against the link segment between its two node positions at that hull's
Z offset. It is deliberately generous -- retail asks whether a WALK between two nodes succeeds,
which is a swept hull, and an AABB test over-reports rather than under-reports. Over-reporting
gives a door a link retail's graph may not have walked; under-reporting would wall off a door NPCs
use, silently.

Story 7 (0018/7) closes the door lane: every door is cut from every mesh, each traversable row
carries `linkStartCm` / `linkEndCm` -- its largest crossing hull's witness link clipped to that
hull's grown box -- and `links` groups the rows by witness link: one `AElysiumNavDoorLink` per
AIN link, over every door it crosses, for the agents that cross them (`bake_nav_door_links`).
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Any, Iterable, Sequence

from elysium_pipeline.formats.bsp import source_to_unreal
# `0x102ff960`'s link-off bit and the ground type `CAI_Node::GetPosition 0x102fb0d0` offsets.
from elysium_pipeline.formats.nav_graph_glb.model import LINK_OFF, NODE_GROUND

#: Retail's hull count; a link carries one motion word per hull.
RETAIL_HULL_COUNT = 22

#: Door classnames. `func_door` slides and `func_door_rotating` swings; both answer `MOVEABLE`,
#: which is what puts them outside the graph-build mask and inside every run-time probe.
DOOR_CLASSNAMES = ("func_door", "func_door_rotating")


def _node_position(node: dict, hull: int) -> tuple[float, float, float]:
    origin = [float(value) for value in node["origin"]["source"]]
    offsets = node.get("hullOffsets") or []
    if len(origin) != 3 or len(offsets) != RETAIL_HULL_COUNT:
        raise ValueError("AIN node has an invalid position/hull-offset table")
    if node.get("type") == NODE_GROUND:
        origin[2] += float(offsets[hull])
    if not all(math.isfinite(value) for value in origin):
        raise ValueError("AIN node position is not finite")
    return tuple(source_to_unreal(*origin))


def hull_bounds_cm(hulls: Iterable[Sequence[float]],
                   origin_cm: Sequence[float] = (0.0, 0.0, 0.0),
                   ) -> tuple[list[float], list[float]] | None:
    """The AABB of one brush entity's staged hulls, in WORLD centimetres.

    A brush entity's hulls are staged in its OWN space -- the runtime places them through the
    entity transform -- so the entity's `origin` has to be added before they can be compared with
    node positions. Without it every door sits near the world origin and no link crosses any of
    them, which is a clean-looking answer and a wrong one.
    """

    lo = [math.inf] * 3
    hi = [-math.inf] * 3
    for hull in hulls:
        for index in range(0, len(hull) - 2, 3):
            for axis in range(3):
                value = float(hull[index + axis]) + float(origin_cm[axis])
                lo[axis] = min(lo[axis], value)
                hi[axis] = max(hi[axis], value)
    if not all(math.isfinite(value) for value in (*lo, *hi)):
        return None
    return lo, hi


def segment_box_span(start: Sequence[float], end: Sequence[float],
                     lo: Sequence[float], hi: Sequence[float]) -> tuple[float, float] | None:
    """Slab test: the segment parameters `(enter, exit)` inside the box, or None when it misses."""

    enter, exit_ = 0.0, 1.0
    for axis in range(3):
        a, b = float(start[axis]), float(end[axis])
        delta = b - a
        if abs(delta) < 1e-9:
            if a < lo[axis] or a > hi[axis]:
                return None
            continue
        t0 = (lo[axis] - a) / delta
        t1 = (hi[axis] - a) / delta
        if t0 > t1:
            t0, t1 = t1, t0
        enter = max(enter, t0)
        exit_ = min(exit_, t1)
        if enter > exit_:
            return None
    return enter, exit_


def segment_hits_box(start: Sequence[float], end: Sequence[float],
                     lo: Sequence[float], hi: Sequence[float]) -> bool:
    """Slab test: does the segment touch the axis-aligned box at all?"""

    return segment_box_span(start, end, lo, hi) is not None


def clip_link(start: Sequence[float], end: Sequence[float],
              lo: Sequence[float], hi: Sequence[float]) -> tuple[list[float], list[float]]:
    """The smart link's two endpoints: where the witness link enters and leaves the grown box.

    The port cuts every door out of every mesh (`UElysiumNavArea_DoorCut`) and the agent's own
    erosion keeps its mesh a hull radius off the cut, so the grown box's faces are where the mesh
    ends on either side of the closed leaf -- the two points the smart link joins (0018/7). A
    witness node that already stands inside the grown box keeps its own position.
    """

    span = segment_box_span(start, end, lo, hi)
    if span is None:
        raise ValueError("the witness link does not cross the door it witnesses")
    enter, exit_ = span

    def at(t: float) -> list[float]:
        return [float(start[axis]) + (float(end[axis]) - float(start[axis])) * t for axis in range(3)]

    return at(enter), at(exit_)


def crossings(block: dict, doors: Sequence[dict], origins: dict, hull_radius_cm) -> list[dict]:
    """Per door, which hulls have a graph link running through its closed box.

    `doors` are staged brush-entity rows -- `entityIndex`, `classname`, `hulls` -- and `origins`
    maps an entity index to its world origin in centimetres, which the hulls are stated relative
    to. `hull_radius_cm` answers a hull's lateral radius, which the box is grown by so the test
    asks "could a body of this size pass through here" rather than "does a line touch the brush".
    """

    header = block["header"]
    nodes = {int(node["index"]): node for node in block["nodes"]}
    used_bits = int(header["usedHullBits"]["value"])
    hulls = [bit for bit in range(RETAIL_HULL_COUNT) if used_bits & (1 << bit)]

    rows = []
    for door in doors:
        origin = origins.get(int(door["entityIndex"]), (0.0, 0.0, 0.0))
        bounds = hull_bounds_cm(door.get("hulls") or [], origin)
        if bounds is None:
            continue
        lo, hi = bounds
        crossed: list[int] = []
        witnesses: dict[int, int] = {}
        segments: dict[int, tuple] = {}
        for hull in hulls:
            radius = float(hull_radius_cm(hull))
            grown_lo = [lo[axis] - radius for axis in range(3)]
            grown_hi = [hi[axis] + radius for axis in range(3)]
            for link in block["links"]:
                fields = link["fields"]
                if len(fields) != RETAIL_HULL_COUNT + 1 or int(fields[0]) & LINK_OFF:
                    continue
                if not int(fields[1 + hull]):
                    continue   # this link serves no motion for this hull
                src, dst = int(link["src"]), int(link["dst"])
                if src not in nodes or dst not in nodes:
                    continue
                start = _node_position(nodes[src], hull)
                end = _node_position(nodes[dst], hull)
                if segment_hits_box(start, end, grown_lo, grown_hi):
                    crossed.append(hull)
                    witnesses[hull] = int(link["index"])
                    segments[hull] = (list(start), list(end))
                    break
        row = {
            "entityIndex": int(door["entityIndex"]),
            "originCm": [float(value) for value in origin],
            "classname": door.get("classname", ""),
            "boundsCm": [lo, hi],
            "crossedByHulls": crossed,
            "witnessLinks": witnesses,
            "traversable": bool(crossed),
        }
        if crossed:
            # The link is laid for the LARGEST crossing hull (0018/7 review): its grown box contains
            # every smaller hull's, so endpoints clipped to it are outside every crossing agent's
            # box and each agent's mesh reaches them. Ties keep the lower hull index.
            link_hull = max(crossed, key=lambda hull: (float(hull_radius_cm(hull)), -hull))
            radius = float(hull_radius_cm(link_hull))
            start, end = segments[link_hull]
            grown_lo = [lo[axis] - radius for axis in range(3)]
            grown_hi = [hi[axis] + radius for axis in range(3)]
            row["linkHull"] = link_hull
            row["linkWitness"] = witnesses[link_hull]
            row["linkSegmentCm"] = [start, end]
            row["linkStartCm"], row["linkEndCm"] = clip_link(start, end, grown_lo, grown_hi)
        rows.append(row)
    return rows


def door_links(rows: Sequence[dict], hull_radius_cm) -> list[dict]:
    """One smart link per witness AIN link (0018/7 review): retail has ONE link through a doorway
    however many door entities stand in it (the hub's smoke-shop pair shares link 958), and the
    port's link holds a body while ANY of its doors is shut.

    The group's segment is its largest hull's; the endpoints are where that segment enters the
    first of the doors' grown boxes and leaves the last (each door's box grown by that hull's
    radius), so they lie outside every crossing hull's box of every door in the group.
    """

    groups: dict[int, list[dict]] = {}
    for row in rows:
        if row.get("traversable"):
            groups.setdefault(int(row["linkWitness"]), []).append(row)
    links = []
    for witness, members in sorted(groups.items()):
        base = max(members, key=lambda row: (float(hull_radius_cm(row["linkHull"])), -row["linkHull"]))
        radius = float(hull_radius_cm(base["linkHull"]))
        start, end = base["linkSegmentCm"]
        enter, exit_ = 1.0, 0.0
        for row in members:
            lo, hi = row["boundsCm"]
            span = segment_box_span(start, end, [lo[axis] - radius for axis in range(3)],
                                    [hi[axis] + radius for axis in range(3)])
            if span is None:
                raise ValueError(f"door {row['entityIndex']} does not stand on its witness link {witness}")
            enter, exit_ = min(enter, span[0]), max(exit_, span[1])

        def at(t: float) -> list[float]:
            return [float(start[axis]) + (float(end[axis]) - float(start[axis])) * t for axis in range(3)]

        links.append({
            "witness": witness,
            "doors": sorted(int(row["entityIndex"]) for row in members),
            "crossedByHulls": sorted({hull for row in members for hull in row["crossedByHulls"]}),
            "linkHull": base["linkHull"],
            "startCm": at(enter),
            "endCm": at(exit_),
        })
    return links


def door_rows(brush_bodies: Sequence[dict]) -> list[dict]:
    """The staged brush entities that are doors, in lump order."""

    return [row for row in brush_bodies
            if str(row.get("classname", "")).lower() in DOOR_CLASSNAMES]


def stage(block: dict, brush_bodies: Sequence[dict], origins: dict, hull_radius_cm) -> dict:
    """`manifest["navDoors"]`: every door, and whether the graph runs through it."""

    doors = door_rows(brush_bodies)
    rows = crossings(block, doors, origins, hull_radius_cm)
    traversable = [row for row in rows if row["traversable"]]
    links = door_links(rows, hull_radius_cm)

    # A door can be traversable for ONE agent and not another -- the rat hull is not a subset of
    # the human one, and on `sp_tutorial_1` 1 of the 8 linked doors is crossed only by the human
    # and 2 only by the rat. A nav AREA is not per-agent (`FAreaNavModifier` marks every mesh
    # alike), so story 7 cuts EVERY door out of every mesh and lays a smart link through each
    # traversable one for exactly the agents that cross it (`bake_nav_door_links`): cut per mesh,
    # link per agent. `partialByAgent` still counts the doors that need the per-agent half.
    all_hulls = {hull for row in rows for hull in row["crossedByHulls"]}
    partial = [row for row in traversable
               if all_hulls and set(row["crossedByHulls"]) != all_hulls]
    return {
        "doors": len(rows),
        "partialByAgent": len(partial),
        "partialRows": [row["entityIndex"] for row in partial],
        "traversable": len(traversable),
        # Doors no graph link runs through: walls. Story 7 cuts the traversable ones as well
        # (`bake_map_collision.place_nav_areas`) and gives each a smart link (`linked`).
        "cut": len(rows) - len(traversable),
        "rows": rows,
        # One smart link per witness link (`bake_nav_door_links`): `linked` of them.
        "links": links,
        "linked": len(links),
    }


def hull_radius_cm(hull: int) -> float:
    """A hull's lateral radius in centimetres, from the recovered table.

    The box is grown by this so the question is "could a body this wide pass here", which is what
    retail's own walk test between two nodes asks, rather than "does a line touch the brush".
    """

    import json

    from elysium_pipeline.paths import repo_root

    global _HULL_ROWS
    if _HULL_ROWS is None:
        path = repo_root() / "docs" / "vtmb" / "data" / "hull_table.json"
        _HULL_ROWS = json.loads(path.read_text(encoding="utf-8"))["rows"]
    row = _HULL_ROWS[hull]
    return max(abs(float(row["maxs"][0])), abs(float(row["mins"][0]))) * 2.54


_HULL_ROWS = None


def load_graph_block(map_name: str, root: Path | None = None) -> dict:
    """The nav-graph extension block for one map, straight from its published GLB."""

    import json
    import struct

    from elysium_pipeline import paths
    from elysium_pipeline.formats.nav_graph_glb.model import NAV_GRAPH_EXTENSION, normalize_key

    key = normalize_key(map_name)
    path = (Path(root) if root is not None else paths.export_v2_root()) / "nav-graphs" / f"{key}.glb"
    if not path.is_file():
        raise FileNotFoundError(f"missing AIN unit {path}; run: uv run elysium export_v2 "
                                f"nav-graph-glb {key}")
    data = path.read_bytes()
    offset = 12
    while offset < len(data):
        length, kind = struct.unpack_from("<II", data, offset)
        if kind == 0x4E4F534A:       # 'JSON'
            document = json.loads(data[offset + 8:offset + 8 + length].decode("utf-8"))
            block = document.get("extensions", {}).get(NAV_GRAPH_EXTENSION)
            if block is None:
                raise ValueError(f"{path} carries no {NAV_GRAPH_EXTENSION} extension")
            return block
        offset += 8 + length
    raise ValueError(f"{path} has no JSON chunk")
