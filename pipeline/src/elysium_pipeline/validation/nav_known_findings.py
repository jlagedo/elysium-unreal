"""Findings `verify nav` has already reported, judged, and pinned.

A gate that is red for a known reason teaches everyone to ignore it, and a gate that tolerates
"about three detours" hides the fourth. So a finding that has been looked at is pinned HERE, by
map, hull and link index, with what was measured and what is believed about it -- and the gate
then fails on anything NEW and on any pin that stops reproducing (a stale pin is removed, not
left to rot).

Nothing here is a tolerance. Each row is one specific link.

The name says detours because that is what the first three rows were. It holds any of the three
ground-link findings `nav_acceptance.ground_link_errors` reports -- a hole (an endpoint projects
nowhere), a wall (both ends project and cannot be joined) and a detour -- keyed the same way.
"""

from __future__ import annotations

#: {map: {hull: {link index: why it is pinned}}}
KNOWN_DETOURS: dict[str, dict[int, dict[int, str]]] = {
    "sm_hub_1": {
        19: {
            # All three are RAT-ONLY links -- the human graph has no such link -- so each is a
            # passage retail's rat hull (12 x 12 x 10 units) fits and nothing else does: a rat
            # hole. The rat's mesh reaches both ends and joins them, but the long way round
            # (measured 2026-09-20: 1,086 cm straight / 4,561 routed; 152 / 577; 1,120 / 5,229).
            #
            # Leading hypothesis, NOT proven: Recast's quantisation closes the hole. The rat's
            # agent is radius 15.24 cm on 5 cm cells, so the mesh is eroded by ceil(15.24 / 5) = 4
            # cells = 20 cm a side -- a passage has to be 40 cm wide to stay open, where retail's
            # rat needs 30.48. Likewise height: ceil(25.4 / 2.5) = 11 cells = 27.5 cm of clearance
            # against retail's 25.4. A hole between those bounds is open in retail and closed on
            # the mesh. The experiment that would settle it is a rat mesh at 2.54 cm cells (erosion
            # exactly 15.24), at four times the tiles; story 5 inherits the question with the
            # step-height outliers, as the same class of finding: the graph asserts a walk the
            # agent's mesh does not offer.
            406: "rat hole: nodes 81->311, 1,086 cm straight, routed 4,561 (4.2x)",
            721: "rat hole: nodes 158->304, 152 cm straight, routed 577 (3.8x); also bridging",
            1472: "rat hole: nodes 400->175, 1,120 cm straight, routed 5,229 (4.7x)",
        },
    },
    "sm_pier_1": {
        0: {
            # Node 37 (6909, 681, -1537) stands on the BEACH, under the pier; node 59
            # (6899, -488, -1077) stands on the DECK 461 cm above it. Measured 2026-09-21, the
            # map's first time through the gate (0018 story 21-8), with
            # `scratch/21-8/probe_pier_path.py`: the deck OVERHANGS the beach from about 200 cm
            # east of the straight line (a lateral fan at three stations answers at both -1069
            # and -1569 from dx +200 outward, and at the beach alone on the line itself), so
            # there is no climbing surface anywhere between the two nodes. The mesh's path is
            # the only walk the geometry offers: 4,431 cm back along the beach to the ramp by
            # node 47 -- node 37's only other link -- where it climbs 507 cm in one step
            # (points 2->3, z -1574 to -1068) and returns along the deck.
            #
            # Retail asserts the link anyway, and says itself that it is a bad one. Link 97 is
            # the ONLY link of this map's 187 whose `m_LinkInfo` is non-zero, and the bit is
            # `0x2000`: the A* at `0x102fe9f0` multiplies that edge's cost by a random integer
            # in [5, 10] (`local_20 = rand(5, 10)`, applied when the navigation goal's flag byte
            # is set). Retail's own penalty for this link is therefore 5-10x, and the port's mesh
            # costs 3.53x -- INSIDE retail's own tolerance for it, so no route retail would take
            # is lost by pinning this. Story 5 owns what an NPC does on reaching a link its mesh
            # answers only the long way round.
            97: "under-pier detour: nodes 37->59, deck 461 cm above the beach and overhanging it; "
                "1,256 cm straight, routed 4,431 (3.53x) via the ramp at node 47; retail marks "
                "this link alone `m_LinkInfo & 0x2000`, a random 5-10x cost penalty",
        },
    },
    "sp_theatre": {
        0: {
            # Five links, one destination. Node 26 stands at (-12, 292, -294) and every node that
            # links to it -- 0, 1, 2, 20, 21 -- stands at z -162, so all five assert a 132 cm DROP
            # (measured 2026-09-20, first time this map went through the gate). Both ends project;
            # the mesh will not join them, which is what a ledge looks like to path following.
            #
            # 132 cm is not a step. Retail's own step height is 18 units (45.7 cm) and even its
            # graph builder's limit is 40 (101.6), so this is past both -- the graph records these
            # as ground links anyway, and retail's NPC gets down by falling. Nothing in the port
            # falls yet: a drop is not a jump link (the AIN marks none here) and the harness's
            # step-outlier excuse does not reach a rise this large. Story 5 owns what an NPC does
            # on reaching a link its mesh does not offer, and this is that question in its
            # cleanest form; story 7's per-agent links are where a drop could become traversable.
            4: "drop: node 0->26, 132 cm down onto node 26; both ends project, no path",
            8: "drop: node 1->26, 132 cm down onto node 26; both ends project, no path",
            10: "drop: node 2->26, 132 cm down onto node 26; both ends project, no path",
            53: "drop: node 20->26, 132 cm down onto node 26; both ends project, no path",
            56: "drop: node 21->26, 132 cm down onto node 26; both ends project, no path",
        },
    },
}


def known_detours(map_name: str, hull: int) -> dict[int, str]:
    return dict(KNOWN_DETOURS.get(map_name, {}).get(int(hull), {}))
