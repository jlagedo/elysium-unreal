from collections import Counter

import pytest

from elysium_pipeline.importers.triangle_topology import collision_proxy, needs_split


@pytest.mark.parametrize('triangles', [
    [(0,1,2),(0,1,2)], [(0,1,2),(2,1,0)], [(0,0,1)],
    [(0,1,2),(1,0,3),(0,1,4)],
])
def test_collision_proxy_keeps_all_source_points_in_one_connected_hull_input(triangles):
    source = [(float(i),float(i*i),float(i%2)) for i in range(6)]
    vertices, faces = collision_proxy(source, triangles)
    assert vertices[:len(source)] == source
    assert not needs_split(faces)
    assert set(vertices) == set(source)
    assert {i for face in faces for i in face} == set(range(len(source)))
    assert all(face[0] == 0 for face in faces)


def test_manifold_collision_input_is_unchanged_and_bad_indices_fail():
    vertices = [(0.,0.,0.),(1.,0.,0.),(0.,1.,0.)]
    faces = [(0,1,2)]
    assert collision_proxy(vertices,faces) == (vertices,faces)
    with pytest.raises(ValueError):
        collision_proxy(vertices,[(0,1,3)])
