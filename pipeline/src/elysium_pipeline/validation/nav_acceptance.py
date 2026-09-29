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

#: Hulls whose NPC moves by FLIGHT, so its graph nodes stand in the air and its links are not
#: claims about a walkable surface at all.
#:
#: Recovered 2026-09-21 (0018 story 21-8), the first time a map with one reached the gate.
#: `la_ventruetower_3` is the corpus's only hull-20 map, and every one of its 70 findings was
#: hull 20 while hulls 0, 7 and 21 were clean on the same geometry. The bat's task list names
#: the movement outright -- `TASK_MANBAT_TAKEOFF`, `TASK_MANBAT_FLY_TO_HINT`,
#: `TASK_MANBAT_FLY_RANDOM`, `TASK_MANBAT_FALL_TO_GROUND`, `TASK_MANBAT_FIND_FLYNODE`,
#: `TASK_MANBAT_FIND_LANDNODE` (`vampire.dll` `thunk_FUN_10389f80`) -- and the graph agrees:
#: all seven failing ground links are claimed by hull 20 ALONE (`fields[1+h]` is 0 for hulls 0,
#: 7 and 21 on every one). Nodes 46 and 48 carry sixteen hull-20 links and ZERO links for every
#: other hull; node 47 carries twelve, against exactly one each for hulls 0 and 7 whose move
#: type is 2, a jump. No walking hull reaches any of the three, and the Manbat's own mesh
#: answers at no height within 600 cm of them.
#:
#: That these are the fly and land nodes those tasks search is INFERRED, not read: the node
#: record's type word is still typed-unidentified, so nothing here reads a `NODE_AIR`. What is
#: measured is that the only hull reaching them is one that flies. See
#: `docs/contracts/seam_map_nav_graph.md` § "Flying hulls" for the full disposition, including
#: the open question this leaves 0018 story 12.
#:
#: So the mesh is still cut -- `FIND_LANDNODE` and `FALL_TO_GROUND` say the bat does touch the
#: ground, and it needs somewhere to land -- but a flying hull's links are REPORTED rather than
#: failed: they are flight the port does not implement yet, not floor it failed to rasterise.
#: MANBAT_HULL's 160-unit height is a flight envelope, not a body that walks under a ceiling.
FLYING_HULLS: frozenset[int] = frozenset({20})


def flight_row(row: dict[str, Any], hull: int) -> dict[str, Any]:
    """Re-cast one walkable-mesh verdict as a flying hull's: reported, never failed.

    Takes the row a ground/projection/bridging check already produced rather than its pieces,
    so the claim COUNT is that check's own `failed` and not the length of its already-truncated
    `failures` list -- a flying hull with more than `MAX_LISTED` findings would otherwise report
    fewer claims than it has. Every other key the check set is carried through unchanged.
    """

    claims = list(row.get("failures") or ())
    carried = {k: v for k, v in row.items()
               if k not in ("check", "failed", "failures", "truncated")}
    return {
        "check": row["check"],
        "failed": 0,
        "failures": [],
        "truncated": 0,
        "flying": True,
        "flightClaims": claims,
        "flightClaimCount": int(row["failed"]),
        "flightClaimsTruncated": max(0, int(row["failed"]) - len(claims)),
        **carried,
        "hull": hull,
    }


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
                       excused: Iterable[int] = (),
                       known: dict[int, str] | None = None) -> dict[str, Any]:
    """Every ground link must path on its own agent's mesh, within `factor` of the straight line.

    `excused` are links the key already predicted unreachable -- the step-height outliers, where
    retail's graph asserts a rise its own NPCs cannot climb. They are reported by the key and not
    failed here, because the mesh is right and the graph is asking for something retail's motor
    would refuse too.

    `known` are findings already looked at and pinned by link index (`nav_known_findings.py`).
    A pinned finding that reproduces is reported, not failed; anything NEW fails; and a pin that
    no longer reproduces fails too, so a fixed finding is un-pinned rather than left standing as a
    tolerance nobody can see.
    """

    known = dict(known or {})
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
    found = holes + walls + detours
    found_indices = {int(row["index"]) for row in found}
    reproduced = [row for row in found if int(row["index"]) in known]
    fresh = [row for row in found if int(row["index"]) not in known]
    stale = [{"index": index,
              "reason": f"pinned finding no longer reproduces; remove the pin ({why})"}
             for index, why in sorted(known.items()) if index not in found_indices]
    return _row(f"ground-links-hull-{hull}", fresh + stale, hull=hull, links=len(links),
                excused=len(excused_set), holes=len(holes), walls=len(walls),
                detours=len(detours), factor=factor,
                knownFindings=[{**row, "pinned": known[int(row["index"])]}
                               for row in reproduced],
                stalePins=len(stale))


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


# --- The place reports (0018 story 4) -----------------------------------------------------------
#
# Observations, not checks: every row below carries `failed: 0` and lives in the report's
# `observations`, never in `checks`, so nothing here can fail the gate. They say where reach changed
# against retail so it is seen rather than discovered; pinning any of it is 0018/21-9's.

#: Story 3's lesson, carried on every zone row: zones are a connected-components pass over LINKS
#: (`0x102f49c0`), and a link is refused by `CAI_Node::InitLinks 0x102fb4e0`'s HULL sweep, which
#: hits static props a ray against brushes misses (the `146-184` barrel). A mesh joining two zones
#: is therefore not evidence they "should be joined".
ZONE_CAVEAT = ("observation only: before any 'should be joined' claim, reproduce with a hull sweep "
               "against props (InitLinks 0x102fb4e0), not a ray against brushes")


def _observation(check: str, found: Sequence[dict[str, Any]], **extra: Any) -> dict[str, Any]:
    return {
        "check": check,
        "failed": 0,
        "observed": len(found),
        "listed": list(found[:MAX_LISTED]),
        "truncated": max(0, len(found) - MAX_LISTED),
        **extra,
    }


def places_off_mesh(points: Sequence[dict[str, Any]], landed: Sequence[Sequence[bool]],
                    wider_extents: Sequence[Sequence[float]], hull: int,
                    agent: str) -> dict[str, Any]:
    """Every place and authored point that does not project onto `agent`'s mesh within the gate's
    own extent (`landed[0]`), with how far off it is: the first wider extent it lands within
    (`landed[1:]`, `wider_extents`), or none -- the verify library answers "lands", not a distance.
    """

    off, by_kind = [], {}
    for position, point in enumerate(points):
        if landed[0][position]:
            continue
        within = next((list(extent) for extent, rung in zip(wider_extents, landed[1:])
                       if rung[position]), None)
        by_kind[point["kind"]] = by_kind.get(point["kind"], 0) + 1
        off.append({**point, "landsWithinCm": within})
    return _observation(f"places-off-mesh-hull-{hull}", off, hull=hull, agent=agent,
                        points=len(points), byKind=by_kind,
                        beyondWidest=sum(1 for row in off if row["landsWithinCm"] is None))


def uncovered_points(coverage: dict[str, Any]) -> dict[str, Any]:
    """The authored points no node covers; the key computed them (no editor is needed)."""

    found = list(coverage["uncovered"])
    by_kind: dict[str, int] = {}
    for row in found:
        by_kind[row["kind"]] = by_kind.get(row["kind"], 0) + 1
    return _observation("uncovered-points", found, byKind=by_kind,
                        boxUnits=coverage["boxUnits"], pairedHints=coverage["pairedHints"],
                        unpairedHints=coverage["unpairedHints"], places=coverage["places"],
                        farthestFromANode=coverage.get("farthestFromANode"))


def zone_pairs(pairs: Sequence[dict[str, Any]], lengths: Sequence[float], hull: int,
               agent: str) -> dict[str, Any]:
    """Per AIN zone pair, whether `agent`'s mesh joins them.

    `lengths` answers every pair's candidates in order, flattened. The nearest candidate that
    answers decides: a length joins them; -1 (both ends on the mesh, no path) keeps them apart;
    a pair whose every candidate is off the mesh is unanswered.
    """

    joined, separate, unanswered = [], [], []
    cursor = 0
    for pair in pairs:
        candidates = pair["candidates"]
        answers = list(lengths[cursor:cursor + len(candidates)])
        cursor += len(candidates)
        decided = next(((row, length) for row, length in zip(candidates, answers)
                        if length != OFF_MESH), None)
        head = {"zoneA": pair["zoneA"], "zoneB": pair["zoneB"]}
        if decided is None:
            unanswered.append(head)
            continue
        row, length = decided
        entry = {**head, "src": row["src"], "dst": row["dst"],
                 "straightCm": round(float(row["straightCm"]), 1)}
        if length >= 0.0:
            joined.append({**entry, "pathCm": round(float(length), 1),
                           "observation": "joined-on-mesh-but-separate-zones"})
        else:
            separate.append(entry)
    return _observation(f"zone-pairs-hull-{hull}", joined, hull=hull, agent=agent,
                        pairs=len(pairs), joined=len(joined), separate=len(separate),
                        unanswered=len(unanswered), unansweredPairs=unanswered[:MAX_LISTED],
                        caveat=ZONE_CAVEAT)


def same_zone_unjoined(rows: Sequence[dict[str, Any]], lengths: Sequence[float], hull: int,
                       agent: str) -> dict[str, Any]:
    """The converse: a zone member `agent`'s mesh cannot reach from its zone's anchor.

    A zone is a component over EVERY hull's links, jumps included, and the mesh carries no jump,
    so each finding says whether this hull's own ground links (`groundLinked`) or any of its links
    (`hullLinked`) join the two -- only the first is the graph asserting a walk this agent's mesh
    refuses. Off-mesh ends are counted, not listed: `places_off_mesh` lists them.
    """

    unjoined, off = [], 0
    for row, length in zip(rows, lengths):
        if length == OFF_MESH:
            off += 1
        elif length == NO_PATH:
            unjoined.append({"zone": row["zone"], "anchor": row["anchor"], "node": row["node"],
                             "straightCm": round(float(row["straightCm"]), 1),
                             "groundLinked": bool(row["groundLinked"]),
                             "hullLinked": bool(row["hullLinked"]),
                             "observation": "same-zone-but-no-mesh-path"})
    return _observation(f"same-zone-hull-{hull}", unjoined, hull=hull, agent=agent,
                        members=len(rows), offMesh=off,
                        groundLinked=sum(1 for row in unjoined if row["groundLinked"]),
                        caveat=ZONE_CAVEAT)


def place_observations(queries: dict[str, Any], answers: dict[str, Any],
                       agent_names: dict[str, str]) -> dict[str, Any]:
    """All three reports for one map, from the key's `placeQueries` and the editor's `places`."""

    if "unavailable" in queries:
        return {"unavailable": queries["unavailable"]}
    rows: list[dict[str, Any]] = [uncovered_points(queries["coverage"])]
    for hull, asked in queries["perHull"].items():
        given = answers.get(hull)
        if given is None:
            continue
        agent = agent_names.get(hull, hull)
        found = [
            places_off_mesh(queries["points"], given["landed"], queries["widerExtentsCm"],
                            int(hull), agent),
            zone_pairs(asked["zonePairs"], given["zonePairLengths"], int(hull), agent),
            same_zone_unjoined(asked["sameZone"], given["sameZoneLengths"], int(hull), agent),
        ]
        if int(hull) in FLYING_HULLS:
            for row in found:
                row["flying"] = True
        rows.extend(found)
    return {"zones": queries["zones"], "rows": rows}


def place_summary(observations: dict[str, Any]) -> str:
    """The console line: `places off mesh: Human 3, Rat 12; uncovered: 5; ...`."""

    if "unavailable" in observations:
        return f"place reports unavailable ({observations['unavailable']})"

    def per_agent(prefix: str, value) -> str:
        cells = [f"{row['agent']} {value(row)}" for row in observations["rows"]
                 if row["check"].startswith(prefix)]
        return ", ".join(cells) or "none"

    uncovered = next(row for row in observations["rows"] if row["check"] == "uncovered-points")
    return (f"places off mesh: {per_agent('places-off-mesh-', lambda r: r['observed'])}; "
            f"uncovered: {uncovered['observed']}; "
            f"zone pairs joined on mesh: "
            f"{per_agent('zone-pairs-', lambda r: str(r['joined']) + '/' + str(r['pairs']))}; "
            f"same zone, no mesh path: "
            f"{per_agent('same-zone-', lambda r: r['observed'])}")


def report(rows: Sequence[dict[str, Any]]) -> dict[str, Any]:
    """Every verdict for one map, and whether any of them failed."""

    return {
        "checks": list(rows),
        "failed": sum(int(row["failed"]) for row in rows),
        "clean": all(int(row["failed"]) == 0 for row in rows),
    }
