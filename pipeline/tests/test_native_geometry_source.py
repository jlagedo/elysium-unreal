import struct

import numpy as np
import pytest

from elysium_pipeline.validation.native_geometry_source import mdl_vertex_witness, vtx_triangle_witness


def source_fixture(boned=False):
    mdl, vtx = bytearray(920), bytearray(180)
    mdl[:4] = b"IDST"
    struct.pack_into("<iI", mdl, 4, 2531, 123)
    struct.pack_into("<2i", mdl, 320, 1, 400)
    struct.pack_into("<4i", mdl, 400, 0, 1, 0, 16)
    struct.pack_into("<6i", mdl, 416 + 136, 1, 224, 3, 304, 436, 0)
    struct.pack_into("<2i", mdl, 640 + 8, 3, 0)
    for i in range(3):
        struct.pack_into("<4B4h8f", mdl, 720 + i * 44, 255, 0, 0, 1, 0, 0, 0, 0,
                         float(i), 0., 0., 0., 0., 1., 0., -1.097e24 if i == 2 else .25)
    struct.pack_into("<i", vtx, 0, 107)
    struct.pack_into("<I", vtx, 16, 123)
    struct.pack_into("<2i", vtx, 28, 1, 36)
    struct.pack_into("<2i", vtx, 36, 1, 8)
    struct.pack_into("<2i", vtx, 44, 1, 8)
    struct.pack_into("<2i", vtx, 52, 1, 12)
    struct.pack_into("<H", vtx, 64, 1)
    struct.pack_into("<i", vtx, 68, 8)
    index_base = 128 if boned else 98
    struct.pack_into("<4H3i", vtx, 72, 3, 3, 1, 8 if boned else 16, 20, index_base - 72, index_base + 6 - 72)
    for i in range(3):
        struct.pack_into("<H", vtx, 92 + (12 * i + 10 if boned else 2 * i), i)
    struct.pack_into("<3H", vtx, index_base, 0, 1, 2)
    struct.pack_into("<2H", vtx, index_base + 6, 3, 0)
    return bytes(mdl), bytes(vtx)


@pytest.mark.parametrize("boned", [False, True])
def test_independent_raw_layout_and_vtx_used_vertex_witness(boned):
    mdl, vtx = source_fixture(boned)
    witness = mdl_vertex_witness(mdl, 0, 0, 0, 2)
    assert witness["vertexByteOffset"] == 808 and witness["uvByteOffset"] == 844
    assert witness["vertexStride"] == witness["poolStrideFromTangentBoundary"] == 44
    assert witness["uv"][1] == float(np.float32(-1.097e24))
    topology = vtx_triangle_witness(vtx, witness)
    assert topology["originalMeshVertex"] == 2 and topology["corner"] == 2
    assert topology["vertexTableStride"] == (12 if boned else 2)


def test_wrong_mesh_identity_and_vtx_index_refuse_instead_of_reading_plausible_bytes():
    mdl, vtx = source_fixture()
    with pytest.raises(ValueError, match="outside model/mesh"):
        mdl_vertex_witness(mdl, 0, 0, 0, 3)
    corrupted = bytearray(vtx)
    struct.pack_into("<H", corrupted, 98, 9)
    with pytest.raises(ValueError, match="outside its vertex"):
        vtx_triangle_witness(bytes(corrupted), mdl_vertex_witness(mdl, 0, 0, 0, 2))
