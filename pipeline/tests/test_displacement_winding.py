"""`.dispcol` is wound so that Recast walks a displacement FLOOR (0018 story 21-2).

Nothing draws the displacement soup and a Chaos trimesh collides from both sides, so its winding
was unobservable until the terrain stood in a baked level and Recast rasterised it. Then it turned
out to be the legacy fan with the Source-to-Unreal Y reflection applied and the contract's paired
winding reversal NOT: every floor reached Recast as a ceiling and every ceiling as a floor, and
`sp_soc_3`'s navigation mesh stood six metres above six of its own graph's nodes.

"Faces up" is not an arithmetic fact here -- a component cross product says the opposite of what
the engine concludes. What decides it is four pieces of engine code applied in sequence, restated
below with where each lives, so this test asks the question the way Recast does.
"""

from __future__ import annotations

from pathlib import Path

import pytest


def _cross(u, v):
    return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])


def _sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def recast_normal_y(triangle) -> float:
    """The y of the normal Recast computes for a `.dispcol` row, unnormalised.

    1. The cook swaps v0 and v1 when `bFlipNormals` is set (`ChaosCooking.cpp:41-42`), and
       `UElysiumMapCollisionPayload::GetPhysicsTriMeshData` sets it, as `UStaticMesh` does.
    2. The navigation export feeds the stored indices as `{2, 1, 0}`
       (`RecastNavMeshGenerator.cpp:417`, the unmirrored case).
    3. `Unreal2RecastPoint` is the reflection (-x, z, -y) (`RecastHelpers.cpp:7`).
    4. `rcMarkWalkableTriangles` walks a triangle iff that normal's y clears the slope threshold
       (`Recast.cpp:377-379`).
    """
    v0, v1, v2 = triangle
    stored = (v1, v0, v2)
    fed = (stored[2], stored[1], stored[0])
    a, b, c = ((-p[0], p[2], -p[1]) for p in fed)
    return _cross(_sub(b, a), _sub(c, a))[1]


def test_the_chain_reads_a_component_cross_up_triangle_as_a_ceiling() -> None:
    # The trap, pinned: this is the measurement that first read "not the winding".
    arithmetic_up = ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0))
    assert _cross(_sub(arithmetic_up[1], arithmetic_up[0]),
                  _sub(arithmetic_up[2], arithmetic_up[0]))[2] > 0
    assert recast_normal_y(arithmetic_up) < 0

    arithmetic_down = (arithmetic_up[0], arithmetic_up[2], arithmetic_up[1])
    assert recast_normal_y(arithmetic_down) > 0


#: `sp_soc_3`'s six buried nodes, from the patch graph: the mesh answered 5.75-6.5 m above each
#: and nowhere near it. Each stands on displacement terrain, a few centimetres above a nearly flat
#: triangle, under a cavern roof.
BURIED_NODES = {
    0: (3396.0, -782.0, -145.0),
    7: (3386.0, -922.0, -132.0),
    9: (3343.0, -767.0, -140.0),
    73: (-9883.0, 178.0, -344.0),
    74: (-10406.0, 203.0, -429.0),
    75: (-10132.0, 460.0, -367.0),
}


def _triangles_over(rows, x, y):
    for row in rows:
        a, b, c = row[0:3], row[3:6], row[6:9]
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(d) < 1e-9:
            continue
        w0 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / d
        w1 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / d
        w2 = 1.0 - w0 - w1
        if min(w0, w1, w2) < -1e-6:
            continue
        yield w0 * a[2] + w1 * b[2] + w2 * c[2], (tuple(a), tuple(b), tuple(c))


def test_the_floor_under_each_of_soc3s_buried_nodes_is_one_recast_walks() -> None:
    pytest.importorskip("elysium_pipeline.paths")
    from elysium_pipeline import paths
    try:
        sidecar = Path(paths.export_root()) / "sp_soc_3" / "sp_soc_3.dispcol"
    except RuntimeError:
        pytest.skip("export root not configured")
    if not sidecar.is_file():
        pytest.skip("sp_soc_3 is not exported")

    rows = [[float(value) for value in line.split()]
            for line in sidecar.read_text(encoding="ascii").splitlines() if line.strip()]
    for node, (x, y, z) in BURIED_NODES.items():
        below = [(height, triangle) for height, triangle in _triangles_over(rows, x, y)
                 if z - 30.0 <= height <= z + 1.0]
        assert below, f"node {node}: no displacement floor within 30 cm under it"
        height, floor = max(below)
        assert recast_normal_y(floor) > 0, (
            f"node {node}: the floor {z - height:.1f} cm under it reaches Recast as a CEILING -- "
            "`.dispcol` has lost the winding reversal that pairs with the Y reflection")
