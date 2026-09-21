"""The entities+root join behind `.ents` (R3.1, MP-2.1).

The hull-frame invariant, pinned against a real rotating door in the exported corpus: a brush
entity's hulls are stated about its own origin, never in world space, so one brush model can be
instanced by several doors standing in different places.

The solver halves themselves are pinned in `test_map_sidecars.py`, against the producer's own
`brush_hull` / `model_brushes`. 0018 story 21-5 deleted the decoder whose twins this file used
to reach them through.
"""
from __future__ import annotations

import json
import struct

import numpy as np
import pytest

from elysium_pipeline import paths


def _gltf_extension(path, key):
    data = path.read_bytes()
    length = struct.unpack_from("<I", data, 12)[0]
    return json.loads(data[20:20 + length])["extensions"][key]


def test_rotating_door_hulls_are_origin_relative_not_world_space():
    ents_path = (paths.export_v2_root() / "_sidecars" / "sm_pawnshop_1"
                 / "sm_pawnshop_1.ents")
    root_path = paths.export_v2_root() / "maps" / "sm_pawnshop_1.glb"
    if not ents_path.is_file():
        pytest.skip(f"no produced sidecar at {ents_path}")
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
