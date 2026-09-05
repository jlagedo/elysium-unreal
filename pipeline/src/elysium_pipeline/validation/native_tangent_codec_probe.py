"""Proof-of-encoding probe for the stock 16-bit tangent codec, not a verifier exception.

Authoring X/Z/W never change. A returned X/Y is only a proposed intermediate input to
SetTangents. It must produce exactly the same packed XYZ and the requested raw W.
Native validation of this route is still required; this module writes no assets.
"""
import numpy as np


def pack16(value):
    value = np.asarray(value, dtype=np.float32)
    if not np.isfinite(value).all() or np.any(np.abs(value) > 2):
        raise ValueError("probe requires finite direction components within [-2,2]")
    # PackedNormal.h FVector3f/4f overloads: float32 multiply, RoundToInt32, clamp.
    scaled = value * np.float32(32767)
    return np.clip(np.floor(scaled + np.float32(.5)), -32768, 32767).astype(np.int16)


def determinant_sign(x, y, z):
    m = np.asarray([x, y, z], dtype=np.float64)
    # Matrix.inl RotDeterminant's actual expansion, followed by strict < 0.
    determinant = (m[0, 0] * (m[1, 1] * m[2, 2] - m[1, 2] * m[2, 1])
                   - m[1, 0] * (m[0, 1] * m[2, 2] - m[0, 2] * m[2, 1])
                   + m[2, 0] * (m[0, 1] * m[1, 2] - m[0, 2] * m[1, 1]))
    return -1. if determinant < 0 else 1.


def exact_codec_input(x, z, sign):
    x, z = np.asarray(x, dtype=np.float32), np.asarray(z, dtype=np.float32)
    if x.shape != (3,) or z.shape != (3,) or sign not in (-1, 1) or not np.any(z):
        raise ValueError("invalid authored basis for codec probe")
    packed_x, packed_z = pack16(x), pack16(z)
    center = (packed_x.astype(np.float32) / np.float32(32767)).astype(np.float32)
    candidates = [("original-x-double-cross", x), ("cell-center", center)]
    # Exactly representable f32, strictly inside the SNORM16 zero cell:
    # 32767 * 2^-17 = 0.24999237060546875 < 0.5. This is not a matching tolerance.
    step = np.float32(2 ** -17)
    for axis in range(3):
        for direction in (1, -1):
            candidate = center.copy()
            candidate[axis] += np.float32(direction) * step
            candidates.append((f"cell-interior-axis{axis}-{direction:+d}", candidate))
    for mode, candidate in candidates:
        if not np.array_equal(pack16(candidate), packed_x):
            continue
        # Preserve the nonzero double cross before its final f32 storage, avoiding
        # float32 cancellation in the lantern's nearly antiparallel basis.
        y = (np.cross(z.astype(float), candidate.astype(float)) * sign).astype(np.float32)
        if determinant_sign(candidate, y, z) == sign:
            return {"mode": mode, "codecX": candidate.tolist(), "codecY": y.tolist(), "codecZ": z.tolist(),
                    "packedX": packed_x.tolist(), "packedZ": packed_z.tolist(), "packedW": int(sign * 32767),
                    "packedXYZUnchanged": True, "rawSignRetained": True}
    raise ValueError("no proven stock-codec input in tested quantization cell")
