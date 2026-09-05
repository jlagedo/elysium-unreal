"""Counterexamples to zero-X-only sign canonicalization, following UE's shader equations."""
import hashlib
import json
import os
from pathlib import Path

import numpy as np
import pytest

from elysium_pipeline.validation.native_geometry import read_stage


def test_zero_determinant_packs_positive_sign_but_pixel_interpolation_can_observe_it():
    # RenderMath.h GetBasisDeterminantSign; a zero X basis always canonicalizes to +1.
    x, z, authored_sign = np.zeros(3), np.array([0., 0., 1.]), -1.
    y = np.cross(z, x) * authored_sign
    packed_sign = -1. if np.linalg.det(np.stack([x, y, z])) < 0 else 1.
    assert packed_sign == 1.
    # GPU VF passes sign independently to the pixel interpolants. MaterialTemplate
    # assembles cross(interpolated N, interpolated X) * interpolated W there.
    barycentric = np.array([.5, .25, .25])
    vertex_x = np.array([[0., 0., 0.], [1., 0., 0.], [1., 0., 0.]])
    pixel_x = barycentric @ vertex_x
    original_w = barycentric @ np.array([-1., 1., 1.])
    canonical_w = barycentric @ np.ones(3)
    assert not np.array_equal(np.cross(z, pixel_x) * original_w, np.cross(z, pixel_x) * canonical_w)


def test_unmirror_shader_consumer_observes_raw_sign_even_when_every_x_is_zero():
    # MaterialTemplate.ush UnMirror: Coordinate * Parameters.UnMirrored * 0.5 + 0.5.
    coordinate = .2
    assert coordinate * -1. * .5 + .5 != coordinate * 1. * .5 + .5


@pytest.mark.skipif(os.environ.get("ELYSIUM_R8_SIGN_INCIDENCE") != "1", reason="explicit four-model tangent sign incidence audit")
def test_four_model_zero_tangent_sign_triangle_incidence():
    stage = Path("E:/elysium-work/import/characters")
    manifest_bytes = (stage / "manifest.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    keys = {"character/npc/unique/santa_monica/jeanette/jeanette", "character/npc/unique/downtown/bomb_guy/bomb_guy",
            "weapons/handleclaws/wield/w_f_handleclaws", "scenery/structural/chinese/lanternskins"}
    products = []
    for entry in manifest["assets"]:
        if entry["assetId"].removeprefix("vtmb:model:") not in keys:
            continue
        payload = (stage / entry["payload"]).read_bytes()
        assert hashlib.sha256(payload).hexdigest() == entry["recipe"]["payloadSha256"]
        geometry = read_stage(payload)
        assert geometry.tangents is not None
        zero = np.all(geometry.tangents[:, :3] == 0, axis=1)
        affected = zero & (geometry.tangents[:, 3] == -1)
        triangles = np.asarray([t[1:] for t in geometry.triangles], dtype=np.intp)
        incident = np.any(affected[triangles], axis=1)
        mixed = incident & np.any(~zero[triangles], axis=1)
        p = geometry.positions[triangles]
        nondegenerate = np.any(np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0]) != 0, axis=1)
        examples = [{"triangle": int(i), "material": geometry.slots[geometry.triangles[i][0]],
                     "sourceVertices": triangles[i].tolist(), "sourceSigns": geometry.tangents[triangles[i], 3].tolist(),
                     "zeroX": zero[triangles[i]].tolist()} for i in np.flatnonzero(mixed & nondegenerate)[:6]]
        products.append({"assetId": entry["assetId"], "payloadSha256": entry["recipe"]["payloadSha256"],
                         "negativeSignZeroXVertices": int(affected.sum()), "incidentTriangles": int(incident.sum()),
                         "mixedTriangles": int(mixed.sum()), "nondegenerateMixedTriangles": int((mixed & nondegenerate).sum()),
                         "examples": examples})
    output = {"scope": "four staged source meshes, rest-position triangle incidence; no shader/material closure acceptance",
              "manifestSha256": hashlib.sha256(manifest_bytes).hexdigest(), "products": products,
              "zeroXCanonicalizationProvenUnobservable": False}
    Path("E:/elysium-work/_r8_explore/agents/geometry/zero_tangent_sign_incidence.json").write_text(json.dumps(output, indent=2), encoding="utf-8")
    assert len(products) == 4
