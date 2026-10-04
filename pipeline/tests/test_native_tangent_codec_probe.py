
import numpy as np

from elysium_pipeline.validation.native_tangent_codec_probe import determinant_sign, exact_codec_input, pack16


def test_zero_bin_is_exact_and_does_not_change_packed_source_zero():
    assert np.float32(32767) * np.float32(2 ** -17) < np.float32(.5)
    assert np.array_equal(pack16([2 ** -17, -2 ** -17, 0]), [0, 0, 0])
    for normal in np.eye(3):
        result = exact_codec_input([0, 0, 0], normal, -1)
        assert result["packedX"] == [0, 0, 0]
        assert result["packedW"] == -32767
        assert determinant_sign(result["codecX"], result["codecY"], result["codecZ"]) == -1


def test_lantern_float32_cross_cancellation_can_use_double_cross_without_changing_x():
    x = np.array([.9070675969, .4209849238, 0], dtype=np.float32)
    z = -np.array([.9070675373, .4209848940, 0], dtype=np.float32)
    assert not np.any(np.cross(z, x))
    assert np.any(np.cross(z.astype(float), x.astype(float)))
    result = exact_codec_input(x, z, -1)
    assert result["mode"] == "original-x-double-cross"
    assert np.array_equal(result["codecX"], x)
    assert result["packedW"] == -32767


def test_codec_probe_preserves_packed_axes_for_parallel_and_general_bases():
    rng = np.random.default_rng(827)
    for i in range(600):
        n = rng.normal(size=3)
        n = (n / np.linalg.norm(n)).astype(np.float32)
        x = np.zeros(3) if i % 3 == 0 else n.copy() if i % 3 == 1 else -n.copy()
        sign = -1 if i % 2 else 1
        result = exact_codec_input(x, n, sign)
        assert np.array_equal(pack16(result["codecX"]), pack16(x))
        assert np.array_equal(pack16(result["codecZ"]), pack16(n))
        assert determinant_sign(result["codecX"], result["codecY"], result["codecZ"]) == sign
