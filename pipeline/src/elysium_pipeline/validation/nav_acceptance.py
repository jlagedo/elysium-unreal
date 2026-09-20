"""The pure verdicts: retail's answer key against what the baked meshes actually answered.

Split from the editor half on purpose. Everything here is arithmetic over two dictionaries -- the
key the graph states and the lengths Recast returned -- so every rule can be tested without an
editor, and the editor script stays a query runner with no judgement in it.

A length is centimetres, or one of two refusals the verify library distinguishes:

  * ``-1`` the mesh reaches both ends and cannot join them -- a wall;
  * ``-2`` an endpoint does not project onto the mesh at all -- a hole.

They are different findings and are never collapsed: a hole is missing floor, a wall is floor that
does not connect, and the fix for one is not the fix for the other.
"""

from __future__ import annotations

from typing import Any, Iterable, Sequence

#: How far a path may exceed the straight line before it is worth reporting. Recast routes around
#: the mesh's own polygon edges where retail's link asserts a straight walk, so some excess is
#: expected; this is the band above which the route is a different route, not a rounder one.
DEFAULT_LENGTH_FACTOR = 3.0

#: How many outliers a report lists before it just counts them. A listing that runs to hundreds of
#: lines is not read, and the count is what says whether the number moved.
MAX_LISTED = 25

NO_PATH = -1.0
OFF_MESH = -2.0


def _row(check: str, failures: Sequence[dict[str, Any]], **extra: Any) -> dict[str, Any]:
    return {
        "check": check,
        "failed": len(failures),
        "failures": list(failures[:MAX_LISTED]),
        "truncated": max(0, len(failures) - MAX_LISTED),
        **extra,
    }


def ground_link_errors(links: Sequence[dict[str, Any]], lengths: Sequence[float], hull: int,
                       *, factor: float = DEFAULT_LENGTH_FACTOR,
                       excused: Iterable[int] = ()) -> dict[str, Any]:
    """Every ground link must path on its own agent's mesh, within `factor` of the straight line.

    `excused` are links the key already predicted unreachable -- the step-height outliers, where
    retail's graph asserts a rise its own NPCs cannot climb. They are reported by the key and not
    failed here, because the mesh is right and the graph is asking for something retail's motor
    would refuse too.
    """

    excused_set = set(excused)
    holes, walls, detours = [], [], []
    for link, length in zip(links, lengths):
        if int(link["index"]) in excused_set:
            continue
        straight = float(link.get("straightCm") or 0.0)
        if length == OFF_MESH:
            holes.append({"index": link["index"], "reason": "an endpoint is off the mesh",
                          "src": link["src"], "dst": link["dst"]})
        elif length == NO_PATH:
            walls.append({"index": link["index"], "reason": "both ends on the mesh, no path",
                          "src": link["src"], "dst": link["dst"]})
        elif straight > 0.0 and length > straight * factor:
            detours.append({"index": link["index"], "straightCm": round(straight, 1),
                            "pathCm": round(float(length), 1),
                            "factor": round(float(length) / straight, 2)})
    failures = holes + walls + detours
    return _row(f"ground-links-hull-{hull}", failures, hull=hull, links=len(links),
                excused=len(excused_set), holes=len(holes), walls=len(walls),
                detours=len(detours), factor=factor)


def bridging_errors(bridging: Sequence[dict[str, Any]], agent_lengths: Sequence[float],
                    base_lengths: Sequence[float], hull: int, base_hull: int) -> dict[str, Any]:
    """The check a one-mesh port cannot pass -- and the reach difference beside it.

    A bridging link is one THIS agent has and the base hull does not, joining node sets the base
    hull's own links leave apart. It MUST path on this agent's mesh: that is the whole claim of
    baking a mesh per agent, and a port with one mesh wearing two names cannot make it.

    What it must NOT do was written here first and is wrong, measured 2026-09-20. The base agent's
    mesh paths all fourteen of them -- 70 to 9,155 cm across the two witnesses -- and that is not a
    defect, it is the spec's own named modernization at work: "NPCs can reach floor retail's sparse
    graph never covered". Retail's graph is a sparse set of designer-placed nodes; a rasterised
    floor is not, and demanding that Recast reproduce the graph's CONNECTIVITY would be demanding
    it reproduce the sparseness. So the base-mesh result is reported as reach that changed against
    retail, with its length, rather than failed -- which is what the spec asks for two paragraphs
    above: reach that changes is "seen, not discovered".
    """

    missing, reach = [], []
    for link, mine, theirs in zip(bridging, agent_lengths, base_lengths):
        if mine < 0.0:
            missing.append({"index": link["index"], "src": link["src"], "dst": link["dst"],
                            "reason": "does not path on its own agent's mesh"})
        if theirs >= 0.0:
            reach.append({"index": link["index"], "src": link["src"], "dst": link["dst"],
                          "pathCm": round(float(theirs), 1)})
    return _row(f"bridging-hull-{hull}", missing, hull=hull, baseHull=base_hull,
                bridging=len(bridging), missing=len(missing),
                alsoReachedByBase=len(reach), baseReach=reach[:MAX_LISTED])


def projection_errors(label: str, points: Sequence[Any], landed: Sequence[bool],
                      hull: int) -> dict[str, Any]:
    """Points every consumer downstream needs on the mesh -- jump endpoints, places, patrol."""

    failures = [{"point": index, "reason": f"{label} does not project onto hull {hull}'s mesh"}
                for index, ok in enumerate(landed) if not ok]
    return _row(f"{label}-projection-hull-{hull}", failures, hull=hull, points=len(points))


def mesh_errors(expected_agents: Sequence[str], meshes: Sequence[str]) -> dict[str, Any]:
    """The level carries a mesh WITH TILES for every agent its own graph names, and no others.

    Tiles rather than actors, and this map's own agents rather than any: a travel once reported an
    adopted mesh on a map whose level had none, because the check it passed could be satisfied by
    the PREVIOUS map's nav data. An empty mesh saves, loads and reads as "already built" exactly
    like a full one.
    """

    carried = {}
    for row in meshes:
        name, _, tiles = str(row).rpartition("=")
        carried[name] = int(tiles)
    failures = []
    for agent in expected_agents:
        if agent not in carried:
            failures.append({"agent": agent, "reason": "the level carries no mesh for this agent"})
        elif carried[agent] <= 0:
            failures.append({"agent": agent, "reason": "the mesh carries no tiles"})
    for name, tiles in sorted(carried.items()):
        if name not in expected_agents:
            failures.append({"agent": name, "tiles": tiles,
                             "reason": "the level carries a mesh its graph never asked for"})
    return _row("agent-meshes", failures, expected=list(expected_agents), carried=carried)


def report(rows: Sequence[dict[str, Any]]) -> dict[str, Any]:
    """Every verdict for one map, and whether any of them failed."""

    return {
        "checks": list(rows),
        "failed": sum(int(row["failed"]) for row in rows),
        "clean": all(int(row["failed"]) == 0 for row in rows),
    }
