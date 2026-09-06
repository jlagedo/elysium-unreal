import math
import struct

import pytest

from elysium_pipeline.skeletal_stage import payload as UE_mdl_skeletal
from elysium_pipeline.formats import eskm, mdl_skel


def _image(matrix=None):
    image = bytearray(560)
    struct.pack_into("<i", image, 240, 2)       # NumBones
    struct.pack_into("<ii", image, 328, 1, 400)
    struct.pack_into("<iii", image, 400, 120, 7, 1)
    values = matrix or (
        1.0, 0.0, 0.0, 1.4,
        0.0, 1.0, 0.0, 4.5,
        0.0, 0.0, 1.0, 0.0,
    )
    struct.pack_into("<12f", image, 412, *values)
    image[520:526] = b"mouth\0"
    return image


def test_decodes_record_relative_name_bone_and_local_matrix():
    record = mdl_skel.attachments(_image())[0]

    assert record.name == "mouth"
    assert record.flags == 7
    assert record.bone == 1
    assert record.pos[0] == pytest.approx(1.4, abs=1e-6)
    assert record.pos[1] == pytest.approx(4.5, abs=1e-6)
    assert record.pos[2] == pytest.approx(0.0, abs=1e-6)
    assert record.quat == (0.0, 0.0, 0.0, 1.0)


def test_rotation_matrix_becomes_a_unit_quaternion():
    record = mdl_skel.attachments(_image((
        0.0, -1.0, 0.0, 0.0,
        1.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
    )))[0]

    assert record.quat[2] == pytest.approx(math.sqrt(0.5), abs=1e-6)
    assert record.quat[3] == pytest.approx(math.sqrt(0.5), abs=1e-6)


def test_eskm_attachment_is_unreal_native_and_uses_emitted_bone():
    payload = UE_mdl_skeletal._attachment_section(_image(), [0, 2])
    container = UE_mdl_skeletal._assemble([(b"ATCH", payload)])

    name, bone, translation, rotation = eskm.attachments(container)[0]
    assert name == "mouth"
    assert bone == 2
    assert translation[0] == pytest.approx(1.4 * 2.54, abs=1e-5)
    assert translation[1] == pytest.approx(-4.5 * 2.54, abs=1e-5)
    assert translation[2] == pytest.approx(0.0, abs=1e-5)
    assert rotation == (0.0, 0.0, 0.0, 1.0)


def test_rejects_attachment_outside_the_model_image():
    image = _image()
    struct.pack_into("<i", image, 332, 540)
    with pytest.raises(ValueError, match="runs past"):
        mdl_skel.attachments(image)
