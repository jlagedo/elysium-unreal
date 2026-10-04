from copy import deepcopy

import numpy as np
import pytest

from elysium_pipeline.validation.native_geometry import read_stage, check_render
from elysium_pipeline.validation.native_geometry_equivalence import audit_vertex_equivalence
from pipeline.tests.test_native_geometry import fixture as geometry_fixture  # noqa: F401


@pytest.fixture
def aliases(geometry_fixture):
    geometry = read_stage(geometry_fixture["payload"])
    for name in ("positions", "normals", "uvs", "joints", "weights", "tangents"):
        values = getattr(geometry, name)
        setattr(geometry, name, np.concatenate([values, values[:1]], axis=0))
    geometry.morphs["smile"][3] = geometry.morphs["smile"][0].copy()
    geometry.triangles.append((0, 3, 2, 1))
    render = deepcopy(geometry_fixture["snapshot"]["render"])
    render["indices"] += [0, 2, 1]
    render["sections"][0]["numTriangles"] += 1
    return geometry, render


def test_exact_source_alias_preserves_all_channels_and_triangle_multiplicity(aliases):
    geometry, render = aliases
    classes, proof = audit_vertex_equivalence(geometry, render)
    assert proof["passed"] and classes == [0, 1, 2, 0]
    assert proof["sourceAliases"] == [[3, 0]]
    assert proof["originalSourceVertices"] == 4 and proof["representedSourceIds"] == 3
    native = check_render(geometry, render)
    assert native["passed"] and native["sourceVertices"] == 4 and native["sourceAliasCount"] == 1
    assert native["sourceAliases"] == [[3, 0]]


@pytest.mark.parametrize("channel", ["positions", "normals", "uvs", "weights", "joints", "morphPosition", "morphNormal", "morphMembership"])
def test_alias_requires_every_meaningful_channel_to_be_exact(aliases, channel):
    geometry, render = aliases
    if channel == "morphMembership":
        geometry.morphs["extraZero"] = {3: np.zeros(6)}
    elif channel in ("morphPosition", "morphNormal"):
        geometry.morphs["smile"][3][0 if channel == "morphPosition" else 3] += 1e-12
    else:
        values = getattr(geometry, channel)
        values[3, 0] += 1 if channel == "joints" else 1e-12
    _, proof = audit_vertex_equivalence(geometry, render)
    assert not proof["passed"] and proof["unprovenSourceIds"] == 1
    with pytest.raises(ValueError, match="source vertex omission"):
        check_render(geometry, render)


@pytest.mark.parametrize("mode", ["multiplicity", "winding", "material"])
def test_exact_attributes_do_not_excuse_different_triangles(aliases, mode):
    geometry, render = aliases
    if mode == "multiplicity":
        render["indices"] = render["indices"][:3]
        render["sections"][0]["numTriangles"] = 1
    elif mode == "winding":
        render["indices"][3:] = [0, 1, 2]
    else:
        render["sections"][0]["material"] = 1
    _, proof = audit_vertex_equivalence(geometry, render)
    assert not proof["passed"] and proof["missingEquivalentTriangles"] > 0
