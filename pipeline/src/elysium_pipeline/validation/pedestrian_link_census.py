"""The pedestrian roadway mark against the published `0x2000` link set, map by map.

Retail's pedestrian A* penalty rides one link flag, `CAI_Link::m_LinkInfo & 0x2000` (`link+0x64`,
the nav-graph unit's `fields[0]`; `seam_map_nav_graph.md` section "Link stream"). The port carries
the same fact as an AREA: `UElysiumNavArea_Pedestrian` laid over the `---p` brushes (`0x2000` and no
clip bit; `importers.map_collision.PEDESTRIAN_SIGNATURE`), which `UElysiumNavQueryFilter_Pedestrian`
prices per request. The two are views of one fact, so this census asks them against each other:

* per map, how many links carry the flag, and how many of those cross a pedestrian box;
* how many unflagged hull-0 ground links cross one (the false-positive side).

A link "crosses" a box by `map_nav_doors.segment_hits_box` over the box grown by hull 0's lateral
radius, the segment being the two node positions at hull 0's Z offset -- the test the door cut and
the oracle's own census use. It over-reports by construction (retail asks a swept hull), so the
expected agreement on `sm_hub_1` is about 455 of 461 flagged links (458 measured) against 0 of
1,185 unflagged hull-0 ground links, NOT 461 of 461. The matrix is reported, never asserted equal.

Exit status is non-zero only when a map with no flagged link has a crossing (the area would price
ground retail never flagged), or the flagged totals are not the published units' 1,277 links over 32
maps with 461 on `sm_hub_1` (see `EXPECTED_FLAGGED_TOTAL`).

    uv run python -m elysium_pipeline.validation.pedestrian_link_census [map ...]

Reads `$ELYSIUM_EXPORT_V2_ROOT/nav-graphs/<map>.glb` and the `<map>.hulls` sidecar under
`_sidecars/<map>/`; writes nothing, and never touches the VtMB install.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

from elysium_pipeline.formats.nav_graph_glb.model import LINK_OFF
from elysium_pipeline.importers import map_collision, map_nav_doors

#: `CAI_Link::m_LinkInfo` bit the A* at `FUN_102fe9f0` reads once for the pedestrian multiplier.
PEDESTRIAN_LINK_FLAG = 0x2000

#: The totals the census gates on: flagged links, maps carrying any, and `sm_hub_1`'s, over the
#: PUBLISHED units (Unofficial_Patch graphs first, as the exporter resolves them). The spec's
#: "1,331 links over 40 maps" reproduces from neither install source: the base `Vampire/` graphs
#: hold 1,320 on 40 maps (`sm_hub_1` 450) and the patch's hold 1,277 on 32 (`sm_hub_1` 461, the
#: figure the spec and `ElysiumNavAreas.h` quote), so the units the port loads are pinned instead.
EXPECTED_FLAGGED_TOTAL = 1277
EXPECTED_FLAGGED_MAPS = 32
EXPECTED_HUB_FLAGGED = 461
HUB_MAP = "sm_hub_1"

#: `fields[1 + hull]` value for a ground link (`2` is a jump link); `seam_map_nav_graph.md`.
GROUND_MOTION = 1

#: The hull the box is grown by and the link endpoints are placed at.
CENSUS_HULL = 0

Box = tuple[list[float], list[float]]


@dataclass(frozen=True)
class MapMatrix:
    """One map's confusion matrix: flag (retail) against crossing (the area mark)."""

    map_name: str
    boxes: int
    flagged: int
    flagged_crossing: int
    unflagged_ground: int
    unflagged_crossing: int

    @property
    def flagged_missed(self) -> int:
        return self.flagged - self.flagged_crossing

    def line(self) -> str:
        return (
            f"{self.map_name:<24} boxes {self.boxes:>3}  "
            f"flagged {self.flagged:>4} (cross {self.flagged_crossing:>4}, miss {self.flagged_missed:>3})  "
            f"unflagged ground {self.unflagged_ground:>5} (cross {self.unflagged_crossing:>4})"
        )


def pedestrian_boxes(hulls_path: Path) -> list[Box]:
    """One world-space AABB per `---p` hull of `<map>.hulls` (Unreal centimetres, as staged)."""

    boxes: list[Box] = []
    for signature, hulls in map_collision.read_hull_partitions(hulls_path):
        if signature != map_collision.PEDESTRIAN_SIGNATURE:
            continue
        for hull in hulls:
            bounds = map_nav_doors.hull_bounds_cm([hull])
            if bounds is not None:
                boxes.append(bounds)
    return boxes


def _crosses(start: Sequence[float], end: Sequence[float], grown: Iterable[Box]) -> bool:
    return any(map_nav_doors.segment_hits_box(start, end, lo, hi) for lo, hi in grown)


def link_matrix(map_name: str, block: dict[str, Any], boxes: Sequence[Box],
                radius_cm: float | None = None) -> MapMatrix:
    """The confusion matrix of `block`'s links against `boxes`.

    `flagged` is every link with `fields[0] & 0x2000`. `unflagged_ground` is every other link whose
    hull-0 motion word is the ground capability (`fields[1] == 1`; `2` is a jump link) and that is
    not switched off (`LINK_OFF`) -- the population `InitLinks`' walk test built and the roadway
    must not swallow. On `sm_hub_1` that is the 1,185 the spec names.
    """

    if radius_cm is None:
        radius_cm = map_nav_doors.hull_radius_cm(CENSUS_HULL)
    grown = [([v - radius_cm for v in lo], [v + radius_cm for v in hi]) for lo, hi in boxes]
    nodes = {int(node["index"]): node for node in block["nodes"]}
    hull_word = 1 + CENSUS_HULL

    flagged = flagged_crossing = unflagged = unflagged_crossing = 0
    for link in block["links"]:
        fields = link["fields"]
        if len(fields) != map_nav_doors.RETAIL_HULL_COUNT + 1:
            continue
        src, dst = int(link["src"]), int(link["dst"])
        if src not in nodes or dst not in nodes:
            continue
        is_flagged = bool(int(fields[0]) & PEDESTRIAN_LINK_FLAG)
        if not is_flagged and (int(fields[0]) & LINK_OFF or int(fields[hull_word]) != GROUND_MOTION):
            continue
        crossing = bool(grown) and _crosses(
            map_nav_doors._node_position(nodes[src], CENSUS_HULL),
            map_nav_doors._node_position(nodes[dst], CENSUS_HULL),
            grown,
        )
        if is_flagged:
            flagged += 1
            flagged_crossing += crossing
        else:
            unflagged += 1
            unflagged_crossing += crossing
    return MapMatrix(map_name, len(boxes), flagged, flagged_crossing, unflagged, unflagged_crossing)


def verdict(matrices: Sequence[MapMatrix], *, check_totals: bool = True) -> list[str]:
    """The reasons the census fails; empty when it passes."""

    failures = [
        f"{row.map_name}: no flagged link but {row.unflagged_crossing} crossing"
        for row in matrices
        if row.flagged == 0 and (row.unflagged_crossing or row.flagged_crossing)
    ]
    if check_totals:
        total = sum(row.flagged for row in matrices)
        maps = sum(1 for row in matrices if row.flagged)
        hub = next((row.flagged for row in matrices if row.map_name == HUB_MAP), None)
        if total != EXPECTED_FLAGGED_TOTAL:
            failures.append(f"flagged links {total}, expected {EXPECTED_FLAGGED_TOTAL}")
        if maps != EXPECTED_FLAGGED_MAPS:
            failures.append(f"maps carrying a flagged link {maps}, expected {EXPECTED_FLAGGED_MAPS}")
        if hub != EXPECTED_HUB_FLAGGED:
            failures.append(f"{HUB_MAP} flagged links {hub}, expected {EXPECTED_HUB_FLAGGED}")
    return failures


def sidecar_hulls_path(map_name: str, root: Path | None = None) -> Path:
    from elysium_pipeline import paths
    from elysium_pipeline.exporters.UE_map_sidecars import SIDECAR_DIR_NAME

    base = Path(root) if root is not None else paths.export_v2_root()
    return base / SIDECAR_DIR_NAME / map_name / f"{map_name}.hulls"


def census_map(map_name: str, root: Path | None = None) -> MapMatrix:
    block = map_nav_doors.load_graph_block(map_name, root)
    return link_matrix(map_name, block, pedestrian_boxes(sidecar_hulls_path(map_name, root)))


def published_maps(root: Path | None = None) -> list[str]:
    from elysium_pipeline import paths

    base = Path(root) if root is not None else paths.export_v2_root()
    return sorted(path.stem for path in (base / "nav-graphs").glob("*.glb"))


def main(argv: Sequence[str] | None = None) -> int:
    import argparse
    import os

    parser = argparse.ArgumentParser(prog="elysium_pipeline.validation.pedestrian_link_census")
    parser.add_argument("maps", nargs="*", help="maps to census (default: every published graph)")
    args = parser.parse_args(argv)

    # The module runs beside `uv run elysium`, which loads `.elysium.local.env` itself; a direct
    # run needs the same roots, so read the file the command reads.
    if not os.environ.get("ELYSIUM_WORK_ROOT", "").strip():
        from elysium_pipeline import paths
        from elysium_pipeline.config import _read_local_environment

        for key, value in _read_local_environment(paths.repo_root() / ".elysium.local.env").items():
            os.environ.setdefault(key, value)

    maps = args.maps or published_maps()
    matrices = [census_map(name) for name in maps]
    for row in matrices:
        if row.flagged or row.boxes or row.unflagged_crossing:
            print(row.line())
    quiet = sum(1 for row in matrices if not (row.flagged or row.boxes or row.unflagged_crossing))
    print(f"{quiet} map(s) carry neither a flagged link nor a pedestrian box")
    flagged = sum(row.flagged for row in matrices)
    crossing = sum(row.flagged_crossing for row in matrices)
    print(
        f"total: {len(matrices)} maps, flagged {flagged} on {sum(1 for r in matrices if r.flagged)} "
        f"maps (cross {crossing}), unflagged ground {sum(r.unflagged_ground for r in matrices)} "
        f"(cross {sum(r.unflagged_crossing for r in matrices)})"
    )

    # The totals are the whole corpus's; a single-map run judges only the per-map rule.
    failures = verdict(matrices, check_totals=not args.maps)
    for failure in failures:
        print(f"FAIL {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
