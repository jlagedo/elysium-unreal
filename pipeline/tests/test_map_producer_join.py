"""The entities+root join behind `.ents` (R3.1, MP-2.1).

`docs/architecture/seam_map_map.md` -> "Producer join: the entities+root join behind `.ents`"
states the join the sidecar producer performs and the hull solver it ports verbatim. These tests
pin the two solver halves against synthetic lumps -- the tolerances and the tree walk are the
contract, and a port that silently changes one of them changes shipped collision -- and pin the
hull-frame invariant against a real rotating door in the exported corpus.
"""
from __future__ import annotations

import json
import struct

import numpy as np
import pytest

from elysium_pipeline import paths
from elysium_pipeline.exporters.UE_bsp_to_scene import _brush_hull, _model_brushes


def _sides(rows):
    """dbrushside_t bytes: planenum, texinfo, dispinfo, bevel."""
    return b"".join(struct.pack("<hhhh", plane, 0, 0, bevel) for plane, bevel in rows)


def _brush(first_side, num_sides, contents):
    return struct.pack("<iii", first_side, num_sides, contents)


# A 20 x 40 x 60 box centred on the model frame, as six halfspaces n.x <= d.
_BOX = np.array(
    [[1.0, 0.0, 0.0, 10.0], [-1.0, 0.0, 0.0, 10.0],
     [0.0, 1.0, 0.0, 20.0], [0.0, -1.0, 0.0, 20.0],
     [0.0, 0.0, 1.0, 30.0], [0.0, 0.0, -1.0, 30.0]],
    dtype=np.float32,
)
_BOX_CORNERS = {(x, y, z) for x in (-10.0, 10.0) for y in (-20.0, 20.0) for z in (-30.0, 30.0)}


def _corner_set(points):
    return {(round(float(p[0]), 1), round(float(p[1]), 1), round(float(p[2]), 1)) for p in points}


def test_brush_hull_returns_the_eight_box_corners_and_ignores_bevel_sides():
    # A seventh side clipping x <= 5 would drop the four x = 10 corners if bevels were solved.
    planes = np.vstack([_BOX, np.array([[1.0, 0.0, 0.0, 5.0]], dtype=np.float32)])
    sides = _sides([(0, 0), (1, 0), (2, 0), (3, 0), (4, 0), (5, 0), (6, 1)])

    contents, points = _brush_hull(planes, sides, _brush(0, 7, 0x1), 0)

    assert contents == 0x1
    assert len(points) == 8
    assert _corner_set(points) == _BOX_CORNERS


def test_brush_hull_dedupes_coincident_points_from_duplicate_planes():
    # A duplicated +X plane makes every x = 10 corner come out of two accepted triples; the
    # 0.1-Source-unit dedupe key is what collapses them back to eight.
    planes = np.vstack([_BOX, _BOX[0:1]])
    sides = _sides([(0, 0), (1, 0), (2, 0), (3, 0), (4, 0), (5, 0), (6, 0)])

    _contents, points = _brush_hull(planes, sides, _brush(0, 7, 0x1), 0)

    assert len(points) > 8, "the duplicate plane must actually produce repeated intersections"
    uniq = {(round(float(p[0]), 1), round(float(p[1]), 1), round(float(p[2]), 1)) for p in points}
    assert uniq == _BOX_CORNERS


def test_brush_hull_abstains_when_fewer_than_four_sides_are_not_bevels():
    sides = _sides([(0, 0), (1, 0), (2, 0), (3, 1), (4, 1), (5, 1)])

    contents, points = _brush_hull(_BOX, sides, _brush(0, 6, 0x4000), 0)

    assert contents == 0x4000
    assert points is None


def test_model_brushes_collects_only_leaves_under_the_given_headnode():
    def node(child0, child1):
        return struct.pack("<i", 0) + struct.pack("<ii", child0, child1) + bytes(32 - 12)

    def leaf(first_leaf_brush, num_leaf_brushes):
        return bytes(24) + struct.pack("<HH", first_leaf_brush, num_leaf_brushes) + bytes(4)

    nodes = node(-1, 1) + node(-2, -3) + node(-4, -5)
    leafs = leaf(0, 1) + leaf(1, 1) + leaf(2, 1) + leaf(3, 2) + leaf(5, 1)
    leafbrushes = np.array([100, 101, 102, 200, 201, 201], dtype=np.uint16).tobytes()

    assert _model_brushes(nodes, leafs, leafbrushes, 0) == {100, 101, 102}
    assert _model_brushes(nodes, leafs, leafbrushes, 2) == {200, 201}


def _gltf_extension(path, key):
    data = path.read_bytes()
    length = struct.unpack_from("<I", data, 12)[0]
    return json.loads(data[20:20 + length])["extensions"][key]


def test_rotating_door_hulls_are_origin_relative_not_world_space():
    ents_path = paths.export_root() / "sm_pawnshop_1" / "sm_pawnshop_1.ents"
    root_path = paths.export_v2_root() / "maps" / "sm_pawnshop_1.glb"
    if not ents_path.is_file():
        pytest.skip(f"no legacy sidecar at {ents_path}")
    if not root_path.is_file():
        pytest.skip(f"no exported map root unit at {root_path}")

    entities = json.loads(ents_path.read_text())["entities"]
    models = _gltf_extension(root_path, "ELYSIUM_vtmb_map")["models"]

    door = next(e for e in entities if e.get("targetname") == "havenrm")
    assert door["classname"] == "func_door_rotating" and door["model"] == 18

    verts = [v for hull in door["hulls"] for v in np.array(hull).reshape(-1, 3)]
    # Unreal cm -> Source inches; the Y flip swaps the bound it came from.
    src = np.array([[v[0] / 2.54, -v[1] / 2.54, v[2] / 2.54] for v in verts])
    model = models[18]

    def to_source(v):
        return np.array([v[0] / 0.0254, -v[2] / 0.0254, v[1] / 0.0254])

    lo = np.minimum(to_source(model["mins"]), to_source(model["maxs"]))
    hi = np.maximum(to_source(model["mins"]), to_source(model["maxs"]))
    assert np.allclose(src.min(axis=0), lo, atol=1e-3)
    assert np.allclose(src.max(axis=0), hi, atol=1e-3)
    assert model["origin"] == [0.0, 0.0, -0.0] or not any(model["origin"])
    # The hull frame is nowhere near the entity: only origin + hull places the door in the map.
    assert max(abs(c) for c in door["origin"]) > 5000.0

    # Decisive: one brush model instanced by several doors at distinct origins cannot carry a
    # world position. `*33` is three of sm_pawnshop_1's ten func_door_rotating entities.
    origins = {tuple(e["origin"]) for e in entities if e.get("model") == 33}
    assert len(origins) == 3
