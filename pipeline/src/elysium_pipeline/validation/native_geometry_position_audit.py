"""Diagnostic writer/ideal/FVector3f comparison; never an acceptance oracle or tolerance change."""
import numpy as np

from elysium_pipeline.validation.native_geometry import reference_skin


def rotate(q, value):
    q = np.asarray(q, dtype=float)
    value = np.asarray(value, dtype=float)
    twice_cross = 2 * np.cross(q[..., :3], value)
    return value + q[..., 3, None] * twice_cross + np.cross(q[..., :3], twice_cross)


def multiply(a, b):
    return np.concatenate((a[3] * b[:3] + b[3] * a[:3] + np.cross(a[:3], b[:3]),
                           [a[3] * b[3] - np.dot(a[:3], b[:3])]))


def quaternion_worlds(bones, normalization):
    rotations, translations = [], []
    for bone in bones:
        q = np.asarray(bone["rotation"], dtype=float)
        if normalization == "float32":
            q32 = q.astype(np.float32)
            scale = np.float32(1.) / np.sqrt(np.sum(q32 * q32, dtype=np.float32))
            q = (q32 * scale).astype(float)
        elif normalization == "double-then-float32":
            q = (q / np.linalg.norm(q)).astype(np.float32).astype(float)
        elif normalization == "double":
            q /= np.linalg.norm(q)
        elif normalization != "none":
            raise ValueError(normalization)
        p = np.asarray(bone["position"], dtype=float)
        parent = bone["parent"]
        if parent >= 0:
            p = rotate(rotations[parent], p) + translations[parent]
            q = multiply(rotations[parent], q)
        rotations.append(q)
        translations.append(p)
    return np.asarray(rotations), np.asarray(translations)


def writer_positions(geometry, reference, *, old_normalization="float32", new_normalization="none"):
    """FTransform-style quaternion composition and conjugate unrotation, not matrix inverse.

    ReadSkeleton normalizes its FQuat4f before promotion; reference transforms are supplied
    separately. Float32 normalization variants are diagnostics for platform/SIMD rounding,
    not claimed bit-for-bit emulation of a native instruction sequence.
    """
    old_q, old_p = quaternion_worlds(geometry.bones, old_normalization)
    new_q, new_p = quaternion_worlds(reference, new_normalization)
    joints = geometry.joints
    inverse_q = old_q[joints] * [-1, -1, -1, 1]
    local = rotate(inverse_q, geometry.positions[:, None, :] - old_p[joints])
    moved = rotate(new_q[joints], local) + new_p[joints]
    return np.sum(geometry.weights[..., None] * moved, axis=1).astype(np.float32).astype(float)


def errors(actual, expected):
    delta = np.abs(actual - expected)
    return {"maxAbsoluteCm": float(delta.max()), "componentsOver1e4Cm": int((delta > 1e-4).sum()),
            "verticesOver1e4Cm": int(np.any(delta > 1e-4, axis=1).sum()), "exactComponents": int((delta == 0).sum())}


def position_precision_audit(geometry, body, native):
    reference = body["wield"]["referencePose"] if body.get("wield") else None
    positions = {r["id"]: r["position"] for r in native["authoring"]["vertices"]}
    if set(positions) != set(range(len(geometry.positions))):
        raise ValueError("authoring source vertex inventory differs")
    actual = np.asarray([positions[i] for i in range(len(positions))], dtype=float)
    ideal = reference_skin(geometry, reference).positions
    rounded = ideal.astype(np.float32).astype(float)
    result = {"wield": reference is not None, "vertices": len(actual),
              "nativeIsFloat32": bool(np.array_equal(actual, actual.astype(np.float32).astype(float))),
              "idealDouble": errors(actual, ideal), "idealFloat32": errors(actual, rounded),
              "idealRoundingMagnitude": errors(rounded, ideal), "variants": {}}
    index = np.unravel_index(np.argmax(np.abs(actual - ideal)), actual.shape)
    vertex, component = map(int, index)
    result["worst"] = {"sourceVertex": vertex, "component": component, "actual": actual[vertex].tolist(),
                       "idealDouble": ideal[vertex].tolist(), "idealFloat32": rounded[vertex].tolist()}
    if reference is not None:
        if [(b["name"], b["parent"]) for b in reference] != [(b["name"], b["parent"]) for b in native["bones"]]:
            raise ValueError("native reference identity differs")
        for old_mode in ("none", "float32", "double-then-float32", "double"):
            for new_mode in ("none", "double"):
                prediction = writer_positions(geometry, reference, old_normalization=old_mode, new_normalization=new_mode)
                result["variants"][old_mode + "/" + new_mode] = errors(actual, prediction)
        prediction = writer_positions(geometry, native["bones"])
        result["variants"]["float32/native-saved-reference"] = errors(actual, prediction)
        new_q = np.asarray([b["rotation"] for b in reference])
        saved_q = np.asarray([b["rotation"] for b in native["bones"]])
        result["referenceQuaternionMaxDifference"] = float(np.abs(new_q - saved_q).max())
        result["referenceQuaternionMaxNormError"] = float(np.abs(np.linalg.norm(new_q, axis=1) - 1).max())
        result["doubleQuaternionVsIdealMatrix"] = errors(
            writer_positions(geometry, reference, old_normalization="double", new_normalization="double"), rounded)
    return result
