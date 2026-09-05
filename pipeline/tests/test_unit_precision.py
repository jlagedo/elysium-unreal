import struct

import pytest

from elysium_pipeline.exporters.model_glb import GlbBuilder
from elysium_pipeline.formats.unit_contract import precision


def fixture():
    builder = GlbBuilder()
    values = (480123.987654321, -0., 0.1234567890123456, 1., 2., 3.)
    raw = struct.pack("<6d", *values)
    record = precision.encode(builder.view, raw, (2, 3))
    return {"bufferViews": builder.buffer_views}, bytes(builder.binary), record, raw


def test_precision_survives_the_float32_projection_loss_and_keeps_signed_zero():
    doc, binary, record, raw = fixture()
    values = precision.decode(doc, binary, record, (2, 3))
    assert struct.pack("<6d", *values) == raw
    projected = struct.unpack("<f", struct.pack("<f", values[0] * .0254))[0] / .0254
    assert abs(projected - values[0]) > 1e-4


@pytest.mark.parametrize("change", [{"shape": [3, 2]}, {"encoding": "float32"},
                                    {"sha256": "0" * 64}, {"bufferView": 5}])
def test_a_corrupt_precision_descriptor_fails(change):
    doc, binary, record, _ = fixture()
    with pytest.raises(precision.PrecisionError):
        precision.decode(doc, binary, {**record, **change}, (2, 3))


def test_trailing_compressed_data_is_not_ignored():
    doc, binary, record, _ = fixture()
    binary += b"unclaimed"
    doc["bufferViews"][0]["byteLength"] += len(b"unclaimed")
    with pytest.raises(precision.PrecisionError, match="termination"):
        precision.decode(doc, binary, record, (2, 3))


def test_empty_precise_array_is_well_formed():
    builder = GlbBuilder()
    record = precision.encode(builder.view, b"", (0, 3))
    assert not precision.decode({"bufferViews": builder.buffer_views}, bytes(builder.binary), record, (0, 3))
