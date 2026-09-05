"""PHYS1 research arithmetic. No exporter, builder, runtime, or asset side effects.

All matrices act on column vectors. Scores are evidence, never bake admission.
Installed MDL inverse binds and hierarchical rest poses are separate references;
neither a staged skeleton nor a proposed native builder is a calibration oracle.
"""

from __future__ import annotations

from dataclasses import dataclass
from itertools import permutations, product

import numpy as np


class PhysicsEvidenceError(ValueError):
    """Missing, inconsistent, or nonfinite evidence must be reported explicitly."""


def finite(value, shape=None):
    result = np.asarray(value, dtype=float)
    if (shape is not None and result.shape != shape) or not np.isfinite(result).all():
        raise PhysicsEvidenceError(f"invalid finite array: {result.shape}, expected {shape}")
    return result


def rotation(value):
    result = finite(value, (3, 3))
    if not np.allclose(result.T @ result, np.eye(3), atol=2e-4, rtol=0) or abs(np.linalg.det(result) - 1) > 2e-4:
        raise PhysicsEvidenceError("reference is not a proper rotation; no implicit repair")
    return result


def signed_permutations():
    """All 48 explicit coordinate hypotheses, including improper basis changes."""
    for order in permutations(range(3)):
        for signs in product((1, -1), repeat=3):
            basis = np.eye(3)[list(order)] * np.array(signs)[:, None]
            label = ",".join(("-" if sign < 0 else "+") + "xyz"[axis]
                             for axis, sign in zip(order, signs))
            yield label, basis


def quaternion_matrix(xyzw):
    q = finite(xyzw, (4,))
    if abs(np.linalg.norm(q) - 1) > 2e-4:
        raise PhysicsEvidenceError("non-unit MDL quaternion")
    x, y, z, w = q / np.linalg.norm(q)
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])


def euler_matrix(degrees, convention="source_qangle"):
    """Source QAngle hypothesis: Rz(yaw) Ry(pitch) Rx(roll), not a finding."""
    values = finite(degrees, (3,))
    if convention == "source_qangle":
        x, y, z = np.radians(values[[2, 0, 1]])
    elif convention == "xyz":
        x, y, z = np.radians(values)
    else:
        raise PhysicsEvidenceError(f"unknown Euler convention: {convention}")
    cx, cy, cz = np.cos([x, y, z])
    sx, sy, sz = np.sin([x, y, z])
    return (np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
            @ np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
            @ np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]]))


def transform(position, orientation):
    result = np.eye(4)
    result[:3, :3] = rotation(orientation)
    result[:3, 3] = finite(position, (3,))
    return result


def rotation_error_degrees(left, right):
    delta = rotation(left).T @ rotation(right)
    # atan2 is stable at both zero and pi, without normalizing a bad reference.
    sine = np.linalg.norm([delta[2, 1]-delta[1, 2], delta[0, 2]-delta[2, 0],
                           delta[1, 0]-delta[0, 1]]) / 2
    return float(np.degrees(np.arctan2(sine, (np.trace(delta)-1) / 2)))


def bind_references(bones):
    """Two installed MDL references, retaining raw names and rejecting ambiguity."""
    hierarchy, inverse, names = [], {}, {}
    for index, bone in enumerate(bones):
        name = bone.name.casefold()
        if name in names or bone.parent < -1 or bone.parent >= index:
            raise PhysicsEvidenceError(f"invalid bone identity/parent: {bone.name}")
        names[name] = index
        local = transform(bone.pos, quaternion_matrix(bone.quat))
        hierarchy.append(hierarchy[bone.parent] @ local if bone.parent >= 0 else local)
        stored = np.eye(4)
        stored[:3] = finite(bone.pose_to_bone, (12,)).reshape(3, 4)
        rotation(stored[:3, :3])
        inverse[name] = np.linalg.inv(stored)
    return inverse, {name: hierarchy[i] for name, i in names.items()}


def constraint_bind_frames(properties, constraints, reference):
    """Retail's MDL-derived relative matrix, with identity and endpoint checks.

    client.dll 10089660 inverts the child's pose_to_bone then left-multiplies
    the parent's pose_to_bone: F_parent = B_parent^-1 B_child. The other frame
    is identity (10109420). These are reference measurements, not Chaos limits.
    """
    solids = {}
    for solid in properties:
        index = solid["index"]
        if index in solids:
            raise PhysicsEvidenceError(f"duplicate solid index: {index}")
        name = solid["name"].casefold()
        if name not in reference:
            raise PhysicsEvidenceError(f"unresolved solid bone: {solid['name']}")
        solids[index] = name
    result = []
    children = set()
    for joint in constraints:
        parent, child = joint["parent"], joint["child"]
        if parent not in solids or child not in solids or parent == child or child in children:
            raise PhysicsEvidenceError(f"invalid constraint endpoints: {parent}, {child}")
        children.add(child)
        relative = np.linalg.inv(reference[solids[parent]]) @ reference[solids[child]]
        result.append({"parentSolid": parent, "childSolid": child,
                       "parentBone": solids[parent], "childBone": solids[child],
                       "parentFrameSourceInches": relative.tolist(),
                       "childFrameSourceInches": np.eye(4).tolist()})
    return result


@dataclass(frozen=True)
class FrameCandidate:
    basis_name: str
    basis: np.ndarray
    convention: str
    frame: str
    transpose: bool

    @property
    def name(self):
        return f"{self.basis_name}/{self.convention}/{self.frame}/{'inverse' if self.transpose else 'forward'}"


def frame_candidates():
    return [FrameCandidate(label, basis, convention, frame, transpose)
            for label, basis in signed_permutations()
            for convention in ("source_qangle", "xyz")
            for frame in ("model", "parent") for transpose in (False, True)]


def score_solid_frames(properties, reference, candidates=None):
    """Per-rig RMS cm and geodesic degrees; no fitted offset, scale, or outlier removal.

    p = 2.54 P origin; R = P Euler(angles) P^T (or its transpose).
    Comparisons happen in Source inches before the distance residual is scaled to cm.
    Parent hypotheses compose with the *MDL* parent named by the PHY, never another
    guessed solid. Candidate failures remain rows and cannot win on reduced coverage.
    """
    candidates = frame_candidates() if candidates is None else candidates
    if not properties:
        raise PhysicsEvidenceError("empty solid cohort")
    scores = []
    for candidate in candidates:
        positions, angles, failures = [], [], []
        for solid in properties:
            try:
                name = solid["name"].casefold()
                expected = reference[name]
                orient = euler_matrix(solid["angles"], candidate.convention)
                if candidate.transpose:
                    orient = orient.T
                actual = transform(candidate.basis @ finite(solid["origin"], (3,)),
                                   candidate.basis @ orient @ candidate.basis.T)
                parent = solid.get("parent", "").casefold()
                if candidate.frame == "parent" and parent:
                    actual = reference[parent] @ actual
                positions.append(float(np.linalg.norm(actual[:3, 3] - expected[:3, 3]) * 2.54))
                angles.append(rotation_error_degrees(actual[:3, :3], expected[:3, :3]))
            except (KeyError, PhysicsEvidenceError) as error:
                failures.append({"solid": solid.get("index"), "name": solid.get("name"),
                                 "reason": str(error)})
        scores.append({"candidate": candidate.name, "expected": len(properties),
                       "compared": len(positions), "failures": failures,
                       "positionRmsCm": float(np.sqrt(np.mean(np.square(positions)))) if positions else None,
                       "rotationRmsDegrees": float(np.sqrt(np.mean(np.square(angles)))) if angles else None,
                       "positionMaxCm": max(positions, default=None),
                       "rotationMaxDegrees": max(angles, default=None)})
    return scores


def interval(minimum, maximum, friction):
    """Exact scalar interval identity; NOT proof of a three-axis solver mapping."""
    low, high, drag = finite([minimum, maximum, friction], (3,))
    if low > high or drag < 0:
        raise PhysicsEvidenceError("reversed limit or negative friction")
    return {"midpointDegrees": float((low + high) / 2),
            "halfRangeDegrees": float((high - low) / 2),
            "friction": float(drag), "zeroWidth": bool(low == high)}


def score_axis_rates(observations):
    """Score 48 signed x/y/z -> twist/swing1/swing2 maps from measured tangent rates.

    Each observation must contain independently measured rates in deg/s, source
    and native semantic order. No rate may be generated from a guessed mapping.
    Full rank excitation is required; bind or gravity-only rest cannot label axes.
    This directional score still does not accept finite-angle reachable poses.
    """
    if not observations:
        return {"status": "unobserved", "accepted": False, "scores": []}
    source = finite([r["sourceRates"] for r in observations])
    native = finite([r["nativeRates"] for r in observations])
    if source.ndim != 2 or source.shape[1] != 3 or native.shape != source.shape:
        raise PhysicsEvidenceError("axis observations must be paired N by 3")
    rank = int(np.linalg.matrix_rank(source))
    scores = [{"candidate": label,
               "rmsDegreesPerSecond": float(np.sqrt(np.mean(np.sum((source @ basis.T-native)**2, axis=1))))}
              for label, basis in signed_permutations()]
    scores.sort(key=lambda r: (r["rmsDegreesPerSecond"], r["candidate"]))
    return {"status": "ranked" if rank == 3 else "underexcited", "accepted": False,
            "excitationRank": rank, "observations": len(observations),
            "marginDegreesPerSecond": scores[1]["rmsDegreesPerSecond"] - scores[0]["rmsDegreesPerSecond"],
            "scores": scores}


def score_axis_cohort(expected_joints, observations):
    """Require coverage per named joint; many poses of one knee cannot cover a cast."""
    expected = {tuple(k) for k in expected_joints}
    if len(expected) != len(expected_joints):
        raise PhysicsEvidenceError("duplicate expected joint identity")
    grouped = {k: [] for k in sorted(expected)}
    for row in observations:
        key = tuple(row[k] for k in ("model", "parentSolid", "childSolid"))
        if key not in grouped:
            raise PhysicsEvidenceError(f"unexpected axis-probe joint: {key}")
        grouped[key].append(row)
    scores = [{"joint": key, **score_axis_rates(rows)} for key, rows in grouped.items()]
    return {"accepted": False, "expectedJoints": len(expected),
            "rankedJoints": sum(r["status"] == "ranked" for r in scores),
            "unobservedJoints": sum(r["status"] == "unobserved" for r in scores),
            "underexcitedJoints": sum(r["status"] == "underexcited" for r in scores),
            "joints": scores}


def score_pose_traces(retail, candidates):
    """Compare settled/capture poses with identical model, repeat, time, and solid keys.

    Transforms must already share a declared physical frame and centimetres; never
    align, retime, Procrustes-fit, or skip missing samples to make a solver win.
    Separate cm and degrees avoid an invented physical weighting parameter.
    """
    def index(rows):
        result = {}
        for row in rows:
            key = tuple(row[k] for k in ("model", "repeat", "time", "solid"))
            if key in result:
                raise PhysicsEvidenceError(f"duplicate pose sample: {key}")
            result[key] = transform(row["positionCm"], row["rotation"])
        return result
    truth = index(retail)
    if not truth:
        raise PhysicsEvidenceError("no independent retail poses")
    results = []
    for label, rows in candidates.items():
        measured = index(rows)
        if measured.keys() != truth.keys():
            results.append({"candidate": label, "status": "coverage-mismatch",
                            "missing": sorted(truth.keys()-measured.keys()),
                            "extra": sorted(measured.keys()-truth.keys())})
            continue
        distances = [np.linalg.norm(truth[k][:3, 3]-measured[k][:3, 3]) for k in truth]
        angles = [rotation_error_degrees(truth[k][:3, :3], measured[k][:3, :3]) for k in truth]
        results.append({"candidate": label, "status": "scored", "samples": len(truth),
                        "positionRmsCm": float(np.sqrt(np.mean(np.square(distances)))),
                        "rotationRmsDegrees": float(np.sqrt(np.mean(np.square(angles)))),
                        "accepted": False})
    return results


def retail_ragdoll_input(constraint, reference_mass, *, degrees_to_radians,
                         solver_axis_order=None):
    """Installed parser -> pre-builder limit/motor evidence, never a Chaos recipe.

    ParseRagdollConstraint 26023c40 sets UseClockwiseRotations (+9a). SetupAxis
    2600dc10 maps x/y without sign reversal and z with reversal; InitRagdoll
    2600b990 then reverses all three intervals for that flag. Net x/y negate,
    z retains sign. Both installed axis tables are (0,2,1).

    solver_axis_order must come from an independent observation/verified builder
    result: slot -> intermediate axis. Omitting it leaves the final routing open.
    The zero-speed friction strength is reference mass * parsed friction. No
    friction-to-damping conversion, effective-inertia guess or residual fit.
    """
    mass, factor = finite([reference_mass, degrees_to_radians], (2,))
    if mass <= 0 or factor <= 0:
        raise PhysicsEvidenceError("invalid observed mass/unit conversion")
    if solver_axis_order is not None and sorted(solver_axis_order) != [0, 1, 2]:
        raise PhysicsEvidenceError("solver axis order must be a permutation")
    result = []
    for source_axis, intermediate_axis, sign in zip("xyz", (0, 2, 1), (-1, -1, 1)):
        limits = interval(constraint[source_axis+"min"], constraint[source_axis+"max"],
                          constraint[source_axis+"friction"])
        low, high = constraint[source_axis+"min"], constraint[source_axis+"max"]
        converted = (low, high) if sign == 1 else (-high, -low)
        slot = None if solver_axis_order is None else list(solver_axis_order).index(intermediate_axis)
        result.append({"sourceAxis": source_axis, "intermediateAxis": intermediate_axis,
                       "limitsRadians": [float(v*factor) for v in converted],
                       "motorStrength": float(mass*limits["friction"]),
                       "targetAngularSpeed": 0.0, "solverSlot": slot})
    return result
