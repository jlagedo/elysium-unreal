import struct
from types import SimpleNamespace

import numpy as np
import pytest

from elysium_pipeline.skeletal_stage.geometry import geometry
from elysium_pipeline.skeletal_stage.unit import MODEL_EXTENSION, SkeletalUnitError


def unit():
    arrays = {
        0: np.zeros((3, 3), dtype=np.float32),
        1: np.tile([0., 1., 0.], (3, 1)),
        2: np.array([[1., 0., 0., 1.], [.6, 0., .8, -1.], [0., 0., 0., 1.]], dtype=np.float32),
        3: np.zeros((3, 2), dtype=np.float32),
        4: np.zeros((3, 4), dtype=np.uint16),
        5: np.tile([1., 0., 0., 0.], (3, 1)),
        6: np.array([2, 0, 1], dtype=np.uint32),
    }
    attrs = dict(zip(('POSITION', 'NORMAL', 'TANGENT', 'TEXCOORD_0', 'JOINTS_0', 'WEIGHTS_0'), range(6)))
    source = dict(skinReference=0, sourceVertices=[100, 101, 102], sourcePositions='positions',
                  sourceNormals='normals', bodyPart=0, model=0, mesh=0)
    primitive = dict(attributes=attrs, indices=6, extensions={MODEL_EXTENSION: source,
                     'ELYSIUM_material_reference': {'asset':'vtmb:material:test/face'}})
    return SimpleNamespace(id='vtmb:model:test/body', mdl={'textures':[{'name':'face'}]},
        extension={'vtx':{'lods':[{'index':0,'mesh':0}]}, 'facial':{'morphTargets':[]}},
        document={'meshes':[{'primitives':[primitive]}]}, accessor=arrays.__getitem__,
        precise=lambda key,shape: np.tile([0.,0.,1.],(3,1)) if key=='normals' else np.zeros(shape))


def test_tangents_follow_vertex_identity_and_reflect_handedness_once():
    mesh, _, _, _, mapping, packed = geometry(unit(), [0])
    assert struct.unpack_from('<I', mesh)[0] == 3  # Coincident source vertices stay separate.
    assert mapping[0]['vertices'] == [[102,0],[100,1],[101,2]]
    assert struct.unpack_from('<I', packed)[0] == 3
    values = np.frombuffer(packed, dtype='<f4', offset=4).reshape(3,4)
    np.testing.assert_array_equal(values, np.array([[0.,0.,0.,-1.],[1.,0.,0.,-1.],[.6,.8,0.,1.]], dtype=np.float32))


def test_absent_authored_tangents_refuse_the_new_stage():
    value = unit()
    del value.document['meshes'][0]['primitives'][0]['attributes']['TANGENT']
    with pytest.raises(SkeletalUnitError, match='required channel'):
        geometry(value, [0])
