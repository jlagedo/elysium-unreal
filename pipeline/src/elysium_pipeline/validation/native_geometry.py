"""Independent R8 LOD0 geometry acceptance; no staging/bake writer imports.

The editor reader supplies native facts only. Expected geometry comes from the staged
binary, checked against the pinned GLB and its source-vertex joins before any native
comparison. A pass covers authoring and CPU-visible built buffers, never evaluated GPU
skinning, packed GPU morph streams, clothing, higher LODs, or a rendered frame.
"""
from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import hashlib
import struct

import numpy as np

from elysium_pipeline.formats.unit_contract.container import decode_glb
from elysium_pipeline.formats.unit_contract.precision import decode as precise
from elysium_pipeline.validation.skeletal_diff import records, sections
from elysium_pipeline.validation.geometry_inventory import morph_source_inventory
from elysium_pipeline.validation.native_geometry_equivalence import audit_vertex_equivalence


EXT = "ELYSIUM_vtmb_model"
POSITION_CM = 1e-4
NORMAL = 2e-6
RAW_WEIGHT_MAX = 65535


class GeometryVerificationError(ValueError):
    """A missing, malformed, or changed datum, including unavailable native evidence."""


def require(condition, field):
    if not condition:
        raise GeometryVerificationError(field)


def finite_array(value, field):
    """Reject native diagnostic tokens with their owning vertex/channel, never coerce to zero."""
    try:
        array = np.asarray(value, dtype=float)
    except (TypeError, ValueError, OverflowError) as exc:
        raise GeometryVerificationError(field + ": invalid numeric value") from exc
    invalid = np.flatnonzero(~np.isfinite(array))
    if invalid.size:
        index = tuple(map(int, np.unravel_index(invalid[0], array.shape)))
        number = array[index]
        token = "NaN" if np.isnan(number) else "-Infinity" if number < 0 else "Infinity"
        raise GeometryVerificationError(f"{field}: nonfinite component {index}: {token}")
    return array


def close(actual, expected, tolerance, field):
    a, b = finite_array(actual, field + " actual"), finite_array(expected, field + " expected")
    require(a.shape == b.shape, field + ": shape mismatch")
    error = np.abs(a - b)
    if not np.all(error <= tolerance):
        index = np.unravel_index(np.argmax(error), error.shape)
        raise GeometryVerificationError(f"{field}: maxAbsoluteError={error[index]:.9g} at {index}; actual={a[index]:.9g}, expected={b[index]:.9g}")


def all_rows_close(actual_rows, expected, tolerance):
    """Success-only batch fast path; failures fall back to the original ordered diagnostics."""
    try:
        actual, expected = np.asarray(list(actual_rows), dtype=float), np.asarray(expected, dtype=float)
        return (actual.shape == expected.shape and np.isfinite(actual).all() and np.isfinite(expected).all()
                and bool(np.all(np.abs(actual - expected) <= tolerance)))
    except (ValueError, TypeError, KeyError, IndexError, OverflowError):
        return False


def unique(rows, field, label):
    result = {r[field]: r for r in rows}
    require(len(result) == len(rows), label + ": duplicate identity")
    return result


def unit_vector(vector):
    vector = np.asarray(vector, dtype=float)
    length = np.linalg.norm(vector, axis=-1, keepdims=True)
    require(np.isfinite(length).all() and np.all(length > 1e-8), "cancelled/invalid normal")
    return vector / length


@dataclass
class Geometry:
    bones: list[dict]
    positions: np.ndarray
    normals: np.ndarray
    uvs: np.ndarray
    joints: np.ndarray
    weights: np.ndarray
    slots: list[str]
    triangles: list[tuple]
    morphs: dict[str, dict[int, np.ndarray]]
    tangents: np.ndarray | None = None


def read_stage(blob: bytes) -> Geometry:
    """Use the independent section reader, with additional geometry validity checks."""
    parts = sections(blob)
    require("SKEL" in parts and "MESH" in parts, "stage has no skeleton/mesh")
    skel = {k: v for k, v, _ in records("SKEL", parts["SKEL"])}
    bones = [{key: skel[f"{i}.{key}"] for key in ("name", "parent", "position", "rotation")}
             for i in range(skel["count"])]
    unique(bones, "name", "stage bones")
    mesh = {k: v for k, v, _ in records("MESH", parts["MESH"])}
    count = mesh["count"]
    require(count > 0, "empty staged mesh")
    for key in ("position", "normal", "uv", "weights"):
        require(np.isfinite(mesh[key]).all(), "stage nonfinite " + key)
    require(np.all(mesh["joints"] < len(bones)), "stage bone index outside skeleton")
    require(np.all(mesh["weights"] >= 0), "stage negative skin weight")
    close(mesh["weights"].sum(axis=1), np.ones(count), 1e-6, "stage skin weights do not sum to one")
    unit_vector(mesh["normal"])
    indices = np.frombuffer(mesh["indices"], dtype="<u4").reshape(-1, 3)
    require(np.all(indices < count), "stage triangle index out of range")
    slots, triangles, at = [], [], 0
    for i in range(mesh["slots"]):
        slots.append(mesh[f"{i}.name"])
        start, length = mesh[f"{i}.start"], mesh[f"{i}.length"]
        require(start == at and start + length <= len(indices), "stage section gap/overlap")
        triangles.extend((i, *map(int, t)) for t in indices[start:start + length])
        at += length
    require(at == len(indices) and len(slots) == len(set(slots)), "stage section coverage/identity")
    morphs = {}
    if "MORF" in parts:
        morph = {k: v for k, v, _ in records("MORF", parts["MORF"])}
        for i in range(morph["count"]):
            name = morph[f"{i}.name"]
            require(name not in morphs, "stage duplicate morph")
            vertices = morph[f"{i}.vertex"].tolist()
            require(len(vertices) == len(set(vertices)) and all(0 <= v < count for v in vertices),
                    "stage duplicate/invalid morph vertex")
            values = np.concatenate([morph[f"{i}.position"], morph[f"{i}.normal"]], axis=1)
            require(np.isfinite(values).all(), "stage nonfinite morph")
            morphs[name] = dict(zip(vertices, values.astype(float)))
    tangents = None
    if "TANG" in parts:
        channel = parts["TANG"]
        require(len(channel) >= 4, "truncated TANG header")
        tangent_count = struct.unpack_from("<I", channel)[0]
        require(tangent_count == count and len(channel) == 4 + 16 * tangent_count, "TANG count/extent differs from MESH")
        tangents = np.frombuffer(channel, dtype="<f4", offset=4).reshape(count, 4).astype(float)
        finite_array(tangents, "stage TANG vertex/component")
        require(np.isin(tangents[:, 3], (-1., 1.)).all(), "stage TANG handedness must be -1 or +1")
    return Geometry(bones, mesh["position"].astype(float), mesh["normal"].astype(float),
                    mesh["uv"].astype(float), mesh["joints"], mesh["weights"].astype(float),
                    slots, triangles, morphs, tangents)


def world_matrices(bones):
    """Column-vector homogeneous matrices, independent of UE FTransform operations."""
    worlds = []
    for i, bone in enumerate(bones):
        parent = bone["parent"]
        require(isinstance(parent, int) and -1 <= parent < i, "reference hierarchy is not parent-first")
        q = np.asarray(bone["rotation"], dtype=float)
        close(np.dot(q, q), 1., 2e-6, "reference quaternion is not normalized")
        close(bone.get("scale", [1., 1., 1.]), [1., 1., 1.], 1e-8, "reference scale")
        q /= np.linalg.norm(q)
        x, y, z, w = q
        skew = np.array([[0, -z, y], [z, 0, -x], [-y, x, 0]])
        matrix = np.eye(4)
        matrix[:3, :3] += 2 * w * skew + 2 * skew @ skew
        matrix[:3, 3] = bone["position"]
        require(np.isfinite(matrix).all(), "nonfinite reference pose")
        worlds.append(matrix if parent == -1 else worlds[parent] @ matrix)
    return np.asarray(worlds)


def reference_skin(geometry: Geometry, reference_pose: list[dict] | None) -> Geometry:
    """Blend affine M_ref @ inverse(M_bind); transform deltas with its linear part.

    For each vertex L=|A*n|: n'=A*n/L and dn'=A*dn/L. Thus normalization of
    n'+t*dn' equals normalization of A*(n+t*dn) for arbitrary morph weight t.
    Translation affects base positions only. Source float weights are retained here;
    native normalized/quantized skin weights are verified as a separate boundary.
    """
    world_matrices(geometry.bones)
    if reference_pose is None:
        return geometry
    require([(b["name"], b["parent"]) for b in reference_pose]
            == [(b["name"], b["parent"]) for b in geometry.bones], "wield reference bone identity/order")
    transforms = world_matrices(reference_pose) @ np.linalg.inv(world_matrices(geometry.bones))
    blended = np.einsum("vi,vijk->vjk", geometry.weights, transforms[geometry.joints])
    linear = blended[:, :3, :3]
    positions = np.einsum("vij,vj->vi", linear, geometry.positions) + blended[:, :3, 3]
    raw_normals = np.einsum("vij,vj->vi", linear, geometry.normals)
    normals = unit_vector(raw_normals)
    lengths = np.linalg.norm(raw_normals, axis=1)
    morphs = {name: {v: np.concatenate((linear[v] @ d[:3], linear[v] @ d[3:] / lengths[v]))
                     for v, d in values.items()} for name, values in geometry.morphs.items()}
    tangents = None
    if geometry.tangents is not None:
        tangents = geometry.tangents.copy()
        raw_tangents = np.einsum("vij,vj->vi", linear, tangents[:, :3])
        tangent_lengths = np.linalg.norm(raw_tangents, axis=1)
        nonzero = tangent_lengths > 0
        tangents[:, :3] = 0
        tangents[nonzero, :3] = raw_tangents[nonzero] / tangent_lengths[nonzero, None]
        # Direction only: no translation, no normal-length divisor, no handedness flip.
        # Exact zero/cancelled tangents remain zero. Nonzero near-cancellations are not
        # silently discarded under an engine normalization epsilon.
    return Geometry(reference_pose, positions, normals, geometry.uvs, geometry.joints,
                    geometry.weights, geometry.slots, geometry.triangles, morphs, tangents)


def accessor(document, binary, index):
    row = document["accessors"][index]
    require("sparse" not in row and not row.get("normalized", False), "unsupported core accessor encoding")
    dtype = {5121: "u1", 5123: "<u2", 5125: "<u4", 5126: "<f4"}[row["componentType"]]
    width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[row["type"]]
    view = document["bufferViews"][row["bufferView"]]
    require(view.get("buffer", 0) == 0, "external GLB buffer")
    item = np.dtype(dtype).itemsize
    offset, count = row.get("byteOffset", 0), row["count"]
    stride = view.get("byteStride", item * width)
    needed = offset + (count - 1) * stride + item * width if count else offset
    require(count >= 0 and offset >= 0 and stride >= item * width and needed <= view["byteLength"]
            and view.get("byteOffset", 0) + needed <= len(binary), "GLB accessor bounds")
    return np.ndarray((count, width), dtype=dtype, buffer=binary,
                      offset=view.get("byteOffset", 0) + offset, strides=(stride, item))


def verify_source_geometry(geometry, body, document, binary):
    """Check the staged render projection and retain a distinct ordered source inventory.

    Explicit zeros, repeated contributions, null render vertices, and whole unrendered
    meshes are counted and hashed from GLB records. Dense native morphs cannot prove
    this inventory. The caller must pin the GLB SHA and keep the GLB as authority.
    """
    root = document["extensions"][EXT]
    require(geometry.tangents is not None, "V2 staged mesh is missing TANG")
    require(body["assetId"] == root["identity"]["asset"], "body/source asset identity")
    require(body["sourceSemantics"]["mdl"] == root["mdl"]
            and body["sourceSemantics"]["facial"] == root["facial"], "body source semantics changed")
    lods = [r for r in root["vtx"]["lods"] if r["index"] == 0]
    require(len(lods) == 1, "no unique source LOD0")
    primitives = document["meshes"][lods[0]["mesh"]]["primitives"]
    maps = body["renderVertexMap"]
    require(len(maps) == len(primitives), "source primitive join inventory")
    names = {b["name"]: i for i, b in enumerate(geometry.bones)}
    source_bones = root["mdl"]["bones"]
    # The existing rig naming contract folds characters, not runs. Keep raw names in
    # the source inventory and expose every renamed identity instead of losing the join.
    legal = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-.| ")
    folded = ["".join(c if c in legal and not (i == 0 and c == " ") else "_"
                       for i, c in enumerate(b["name"])) for b in source_bones]
    require(len({n.casefold() for n in folded}) == len(folded), "source bone identity fold collision")
    require(len(names) == len(folded) and all(n in names for n in folded), "source bone identity missing/extra")
    bone_map = np.array([names[n] for n in folded])
    targets = root["facial"]["morphTargets"]
    target_names = {t["index"]: t["name"] for t in targets}
    require(len(target_names) == len(targets) and len(set(target_names.values())) == len(targets),
            "duplicate source morph identity")
    require(list(geometry.morphs) == list(target_names.values()), "stage/source morph target inventory/order")
    expected_morphs = {name: {} for name in target_names.values()}
    covered, expected_triangles, slot_order = set(), [], []
    normal_fallbacks, material_bindings = [], {}
    source_meshes = set()
    for primitive_index, (primitive, join) in enumerate(zip(primitives, maps)):
        require(primitive.get("mode", 4) == 4, "source nontriangle primitive")
        src = primitive["extensions"][EXT]
        key = tuple(src[k] for k in ("bodyPart", "model", "mesh"))
        require(key not in source_meshes, "duplicate source primitive mesh")
        source_meshes.add(key)
        require(tuple(join[k] for k in ("bodyPart", "model", "mesh")) == key, "source mesh join identity")
        slot = root["mdl"]["textures"][src["skinReference"]]["name"]
        require(join["material"] == slot, "source material join")
        material = primitive["extensions"]["ELYSIUM_material_reference"]["asset"]
        require(slot not in material_bindings or material_bindings[slot] == material, "source slot has conflicting materials")
        material_bindings[slot] = material
        if slot not in slot_order:
            slot_order.append(slot)
        section = geometry.slots.index(slot)
        source_vertices = src["sourceVertices"]
        require(len(source_vertices) == len(set(source_vertices)), "duplicate source vertex identity")
        joined = dict(join["vertices"])
        require(len(joined) == len(join["vertices"]), "duplicate staged source join")
        indices = accessor(document, binary, primitive["indices"]).reshape(-1)
        require(len(indices) % 3 == 0 and np.all(indices < len(source_vertices)), "source triangle bounds")
        drawn = list(dict.fromkeys(map(int, indices)))
        require(list(joined) == [source_vertices[i] for i in drawn], "source drawn vertex join order/coverage")
        stage_ids = [joined[source_vertices[i]] for i in drawn]
        require(all(isinstance(v, int) and 0 <= v < len(geometry.positions) and v not in covered for v in stage_ids)
                and len(set(stage_ids)) == len(stage_ids), "stage join vertex collision/bounds")
        covered.update(stage_ids)
        count = len(source_vertices)
        pos = np.asarray(precise(document, binary, src["sourcePositions"], (count, 3))).reshape(count, 3)
        nrm = np.asarray(precise(document, binary, src["sourceNormals"], (count, 3))).reshape(count, 3)
        close(geometry.positions[stage_ids], (pos[drawn] * [2.54, -2.54, 2.54]).astype(np.float32), 0, "source positions")
        expected_normals = nrm * [1, -1, 1]
        missing = np.all(nrm == 0, axis=1)
        if np.any(missing[drawn]):
            # A zero source normal has no direction to preserve. Independently sum the
            # projected face area vectors in source triangle order, the documented
            # stage fallback. Retain every affected identity as a named projection.
            projected = pos * [2.54, -2.54, 2.54]
            faces = indices.reshape(-1, 3)
            area = np.cross(projected[faces[:, 1]] - projected[faces[:, 0]],
                            projected[faces[:, 2]] - projected[faces[:, 0]])
            accumulated = np.zeros_like(projected)
            np.add.at(accumulated, faces.ravel(), np.repeat(area, 3, axis=0))
            lengths = np.linalg.norm(accumulated, axis=1)
            valid = missing & (lengths > 1e-12)
            expected_normals[valid] = accumulated[valid] / lengths[valid, None]
            expected_normals[missing & ~valid] = [0, 0, 1]
            normal_fallbacks.extend({"primitive": primitive_index, "sourceVertex": source_vertices[i],
                                     "stagedVertex": joined[source_vertices[i]], "cancelledFaces": bool(lengths[i] <= 1e-12)}
                                    for i in drawn if missing[i])
        close(geometry.normals[stage_ids], expected_normals[drawn].astype(np.float32), NORMAL, "source normals")
        attrs = primitive["attributes"]
        require("TANGENT" in attrs, "source primitive missing required TANGENT")
        tangent_accessor = document["accessors"][attrs["TANGENT"]]
        require(tangent_accessor["componentType"] == 5126 and tangent_accessor["type"] == "VEC4", "source TANGENT must be FLOAT VEC4")
        source_tangents = accessor(document, binary, attrs["TANGENT"])
        require(source_tangents.shape == (count, 4), "source TANGENT vertex count")
        expected_tangents = source_tangents[drawn][:, [0, 2, 1, 3]].copy()
        expected_tangents[:, 3] *= -1
        close(geometry.tangents[stage_ids], expected_tangents, 0, "source TANGENT to TANG identity/basis/sign")
        uv = accessor(document, binary, attrs["TEXCOORD_0"])
        joints = accessor(document, binary, attrs["JOINTS_0"])
        weights = accessor(document, binary, attrs["WEIGHTS_0"])
        require(uv.shape == (count, 2) and joints.shape == weights.shape == (count, 4), "source channel sizes")
        require(np.all(joints < len(bone_map)), "source joint bounds")
        close(geometry.uvs[stage_ids], uv[drawn], 0, "source UVs")
        close(geometry.joints[stage_ids], bone_map[joints[drawn]], 0, "source bone influence identities")
        close(geometry.weights[stage_ids], weights[drawn], 0, "source bone influence weights")
        for a, b, c in indices.reshape(-1, 3):
            expected_triangles.append((section, *(joined[source_vertices[i]] for i in (a, c, b))))
        for row in src["morphRecords"]:
            require(row["target"] in target_names, "source morph target bounds")
            v = row["vertex"]
            delta = np.asarray([*row["position"], *row["normal"]], dtype=float)
            require(delta.shape == (6,) and np.isfinite(delta).all(), "source invalid morph delta")
            if v is None:
                require(row["sourceVertex"] not in joined, "source unrendered morph has a drawn join")
                continue
            require(isinstance(v, int) and 0 <= v < count and source_vertices[v] == row["sourceVertex"]
                    and row["sourceVertex"] in joined, "source morph vertex identity")
            staged_vertex = joined[row["sourceVertex"]]
            bucket = expected_morphs[target_names[row["target"]]]
            bucket.setdefault(staged_vertex, np.zeros(6))[:] += delta * [2.54, -2.54, 2.54, 1, -1, 1]
    require(slot_order == geometry.slots and covered == set(range(len(geometry.positions))), "source slot/vertex coverage")
    # Material grouping can interleave primitives; stable sort restores staged section order.
    require(sorted(expected_triangles, key=lambda t: t[0]) == geometry.triangles, "source topology/winding/sections")
    for name, expected in expected_morphs.items():
        require(geometry.morphs[name].keys() == expected.keys(), "source sparse morph record coverage: " + name)
        if not all_rows_close((geometry.morphs[name][v] for v in expected), list(expected.values()), [POSITION_CM] * 3 + [NORMAL] * 3):
            for vertex, delta in expected.items():
                close(geometry.morphs[name][vertex], delta, [POSITION_CM] * 3 + [NORMAL] * 3, "source morph: " + name)
    return {**morph_source_inventory(document), "scope": "pinned-GLB-source-inventory-and-stage-LOD0-projection",
            "stagedMorphRecords": sum(map(len, geometry.morphs.values())),
            "vertices": len(geometry.positions), "triangles": len(geometry.triangles),
            "sourceTangentsVerified": True, "tangentVectors": len(geometry.tangents),
            "zeroTangentVectors": int(np.all(geometry.tangents[:, :3] == 0, axis=1).sum()),
            "sourceNormalFallbacks": normal_fallbacks,
            "materialBindings": [{"slot": slot, "assetId": material_bindings[slot]} for slot in slot_order],
            "boneNameProjection": [{"source": b["name"], "native": n, "nativeIndex": names[n]}
                                   for b, n in zip(source_bones, folded) if b["name"] != n],
            "higherLodsRetainedInGlb": [r["index"] for r in root["vtx"]["lods"] if r["index"] != 0]}


def weight_bound(influences):
    """16-bit nearest rounding, sum normalization, and <1 raw unit carry error.

    With k source entries summing S, sum error <= k/(2Q). The quotient error
    bound is (0.5/Q + k/(2Q))/(S-k/(2Q)); normalization's integer carry adds 1/Q.
    This is a bound, not a reimplementation of FBoneWeights::Create.
    """
    positive = [(int(b), float(w)) for b, w in influences if w > 0]
    total = sum(w for _, w in positive)
    epsilon = len(positive) / (2 * RAW_WEIGHT_MAX)
    require(total > epsilon, "source weights cannot be quantized")
    return (0.5 / RAW_WEIGHT_MAX + epsilon) / (total - epsilon) + 1 / RAW_WEIGHT_MAX


def check_weights(actual, geometry, vertex, *, bits, field):
    require(bits in (8, 16), field + ": unsupported weight precision")
    source = list(zip(geometry.joints[vertex], geometry.weights[vertex]))
    expected = Counter()
    for bone, weight in source:
        if weight > 0:
            expected[int(bone)] += float(weight)
    total = sum(expected.values())
    expected = {b: w / total for b, w in expected.items()}
    observed = {}
    for bone, raw in actual:
        require(isinstance(bone, int) and 0 <= bone < len(geometry.bones) and bone not in observed,
                field + ": invalid/duplicate bone identity")
        require(isinstance(raw, int) and 0 < raw <= RAW_WEIGHT_MAX and (bits == 16 or raw % 257 == 0),
                field + ": invalid native raw weight")
        require(bone in expected, field + ": unexpected influence identity")
        observed[bone] = raw / RAW_WEIGHT_MAX
    # Missing identity is allowed only when its magnitude can fall below the actual
    # quantizer's zero bin. Report those individually; never permit dropping a larger bone.
    tolerance = weight_bound(source) + (1 / 255 if bits == 8 else 0)
    losses = []
    for bone, expected_weight in expected.items():
        if bone not in observed:
            zero_bound = 0.5 / RAW_WEIGHT_MAX if bits == 16 else 255 / RAW_WEIGHT_MAX + weight_bound(source)
            require(expected_weight <= zero_bound, field + ": missing influence identity")
            losses.append({"vertex": vertex, "bone": geometry.bones[bone]["name"], "weight": expected_weight, "bits": bits})
        require(abs(observed.get(bone, 0) - expected_weight) <= tolerance, field + ": weight exceeds quantization bound")
    require(bool(observed), field + ": no native weights")
    sum_tolerance = len(expected) / 255 + NORMAL if bits == 8 else NORMAL
    require(abs(sum(observed.values()) - 1) <= sum_tolerance, field + ": native weight sum")
    return losses


def canonical_triangle(section, vertices):
    a, b, c = vertices
    return (section, *min((a, b, c), (b, c, a), (c, a, b)))


def sparse(rows, width, valid, field):
    ids, seen = [], set()
    for row in rows:
        require(len(row) == width + 1 and row[0] in valid and row[0] not in seen, field + ": identity/width")
        ids.append(row[0])
        seen.add(row[0])
    if not rows:
        return {}
    try:
        values = np.asarray([row[1:] for row in rows], dtype=float)
    except (ValueError, TypeError, OverflowError):
        # Diagnostic slow path retains the original sparse ID and component context.
        for row in rows:
            finite_array(row[1:], field + f" id {row[0]}")
        raise GeometryVerificationError(field + ": invalid numeric rows")
    invalid = np.flatnonzero(~np.isfinite(values).all(axis=1))
    if invalid.size:
        index = int(invalid[0])
        finite_array(values[index], field + f" id {ids[index]}")
    return dict(zip(ids, values))


def dense_rows(values, size, width, slots=None):
    """Materialize one morph at a time, including absent zeros, without losing membership."""
    dense = np.zeros((size, width))
    present = np.zeros(size, dtype=bool)
    if values:
        ids = np.asarray([slots[v] if slots is not None else v for v in values], dtype=np.intp)
        dense[ids] = np.asarray(list(values.values()), dtype=float)
        present[ids] = True
    return dense, present


def compare_morph_rows(actual, expected, tolerance, field, *, present=None, missing_field=None, zero_field):
    """Vectorize the old row checks; on failure run the same scalar checks on its first row.

    Numeric bounds, nonfinite refusal, sparse presence and exact nonzero-channel guards
    are unchanged. A missing 1e-7 normal cannot disappear inside the 2e-6 float bound.
    """
    require(actual.shape == expected.shape, field + ": shape mismatch")
    numeric_bad = (~np.isfinite(actual).all(axis=1) | ~np.isfinite(expected).all(axis=1)
                   | np.any(np.abs(actual - expected) > tolerance, axis=1))
    if actual.shape[1] == 6:
        expected_nonzero = np.column_stack((np.any(expected[:, :3] != 0, axis=1), np.any(expected[:, 3:] != 0, axis=1)))
        actual_nonzero = np.column_stack((np.any(actual[:, :3] != 0, axis=1), np.any(actual[:, 3:] != 0, axis=1)))
    else:
        expected_nonzero = np.any(expected != 0, axis=1)[:, None]
        actual_nonzero = np.any(actual != 0, axis=1)[:, None]
    zero_bad = np.any(expected_nonzero & ~actual_nonzero, axis=1)
    missing = np.any(expected_nonzero, axis=1) & ~present if present is not None else np.zeros(len(actual), dtype=bool)
    invalid = np.flatnonzero(numeric_bad | zero_bad | missing)
    if invalid.size:
        row = int(invalid[0])
        close(actual[row], expected[row], tolerance, field)
        if missing[row]:
            require(False, missing_field)
        require(False, zero_field)


def check_tangent_basis(geometry, rows, sources, *, label, packed_bits=None):
    """Compare saved X/sign and Y independently, including the packed-basis error bound.

    Y = cross(N, X) * sign. Cross-product component error from two axes bounded by e
    is e*(|Nj|+|Nk|+|Xj|+|Xk|)+2e^2, plus float32 arithmetic/return rounding.
    Also compare Y against the actual captured X/N/sign to reject a corrupt Y channel
    that happens to fit the larger source-to-packed propagation bound.
    """
    require(geometry.tangents is not None and geometry.tangents.shape == (len(geometry.positions), 4),
            label + " requires complete staged TANG")
    sources = np.asarray(sources, dtype=np.intp)
    row_ids = [row["id"] if label == "authoring" else i for i, row in enumerate(rows)]

    def field_at(key, index):
        return f"{label} {key} {row_ids[index]} (source {sources[index]})"

    def column(key, width):
        for i, row in enumerate(rows):
            require(key in row, field_at(key, i) + ": missing")
        try:
            values = np.asarray([row[key] for row in rows], dtype=float)
        except (ValueError, TypeError, OverflowError):
            for i, row in enumerate(rows):
                finite_array(row[key], field_at(key, i))
            raise GeometryVerificationError(label + " " + key + ": invalid shape")
        shape = (len(rows), width) if width else (len(rows),)
        if not len(rows):
            values = values.reshape(shape)
        if values.shape != shape:
            for i, row in enumerate(rows):
                require(np.asarray(row[key]).shape == ((width,) if width else ()), field_at(key, i) + ": invalid shape")
            raise GeometryVerificationError(label + " " + key + ": invalid shape")
        invalid = np.flatnonzero(~np.isfinite(values).all(axis=1) if width else ~np.isfinite(values))
        if invalid.size:
            i = int(invalid[0])
            finite_array(values[i], field_at(key, i))
        return values

    def compare(actual, expected, tolerance, key):
        bad = np.abs(actual - expected) > tolerance
        invalid = np.flatnonzero(np.any(bad, axis=1) if bad.ndim == 2 else bad)
        if invalid.size:
            i = int(invalid[0])
            row_tolerance = tolerance[i] if isinstance(tolerance, np.ndarray) and tolerance.ndim == 2 else tolerance
            close(actual[i], expected[i], row_tolerance, field_at(key, i))

    x, y, sign = column("tangentX", 3), column("tangentY", 3), column("binormalSign", None)
    native_n = column("normal", 3)
    expected_x = geometry.tangents[sources, :3]
    expected_sign = geometry.tangents[sources, 3]
    expected_n = geometry.normals[sources] if packed_bits is None else unit_vector(geometry.normals[sources])
    epsilon = NORMAL if packed_bits is None else 1 / (127 if packed_bits == 8 else 32767) + NORMAL
    compare(x, expected_x, epsilon, "tangentX")
    compare(sign, expected_sign, 0, "binormalSign")
    zero_source = np.all(expected_x == 0, axis=1)
    changed_zero = np.flatnonzero(zero_source & (np.any(x != 0, axis=1) | np.any(y != 0, axis=1)))
    if changed_zero.size:
        require(False, field_at("zero tangent", int(changed_zero[0])) + ": source zero changed")
    dropped = np.flatnonzero(~zero_source & ~np.any(x != 0, axis=1))
    if dropped.size:
        require(False, field_at("tangentX", int(dropped[0])) + ": dropped nonzero tangent")
    j, k = [1, 2, 0], [2, 0, 1]
    roundoff = 4 * np.finfo(np.float32).eps * np.maximum(1., np.abs(native_n[:, j] * x[:, k]) + np.abs(native_n[:, k] * x[:, j]))
    compare(y, np.cross(native_n, x) * sign[:, None], roundoff, "tangentY native basis")
    propagation = epsilon * (np.abs(expected_n[:, j]) + np.abs(expected_n[:, k])
                             + np.abs(expected_x[:, j]) + np.abs(expected_x[:, k])) + 2 * epsilon * epsilon
    compare(y, np.cross(expected_n, expected_x) * expected_sign[:, None], propagation + roundoff, "tangentY source basis")
    return {"tangentsVerified": True, "tangentVectors": len(rows), "zeroTangentVectors": int(zero_source.sum()),
            "tangentComponentBound": epsilon}


def check_authoring(geometry, data):
    vertices = unique(data["vertices"], "id", "authoring vertices")
    require(set(vertices) == set(range(len(geometry.positions))), "authoring vertex identity/count")
    groups = unique(data["groups"], "id", "authoring groups")
    require([groups[i]["slot"] for i in sorted(groups)] == geometry.slots, "authoring material groups")
    group_slots = {i: geometry.slots.index(g["slot"]) for i, g in groups.items()}
    losses = []
    vertex_order = np.asarray(list(vertices), dtype=np.intp)
    # FMeshDescription positions are FVector3f. Round the independent double oracle
    # at that declared projection boundary, then apply the unchanged absolute bound.
    native_positions = geometry.positions.astype(np.float32).astype(float)
    positions_match = all_rows_close((row["position"] for row in vertices.values()), native_positions[vertex_order], POSITION_CM)
    for v, row in vertices.items():
        if not positions_match:
            close(row["position"], native_positions[v], POSITION_CM, f"authoring position {v}")
        losses += check_weights(row["influences"], geometry, v, bits=16, field=f"authoring skin {v}")
    instances = unique(data["instances"], "id", "authoring instances")
    instance_sources = [row["vertex"] for row in instances.values()]
    require(all(v in vertices for v in instance_sources), "authoring instance source vertex")
    instance_sources = np.asarray(instance_sources, dtype=np.intp)
    normals_match = all_rows_close((row["normal"] for row in instances.values()), geometry.normals[instance_sources], NORMAL)
    uvs_match = all_rows_close((row["uv"] for row in instances.values()), geometry.uvs[instance_sources], 0)
    for row in instances.values():
        v = row["vertex"]
        if not normals_match:
            close(row["normal"], geometry.normals[v], NORMAL, "authoring source normal")
        if not uvs_match:
            close(row["uv"], geometry.uvs[v], 0, "authoring UV")
    require(data.get("tangentYDerived") is True, "authoring tangentY provenance missing")
    tangent_evidence = check_tangent_basis(geometry, list(instances.values()), instance_sources, label="authoring")
    triangles, used = [], Counter()
    for row in data["triangles"]:
        require(row["group"] in groups and len(row["instances"]) == 3
                and all(i in instances for i in row["instances"]), "authoring triangle bounds")
        used.update(row["instances"])
        triangles.append(canonical_triangle(group_slots[row["group"]], [instances[i]["vertex"] for i in row["instances"]]))
    require(set(used) == set(instances), "unreferenced authoring instance")
    require(Counter(triangles) == Counter(canonical_triangle(t[0], t[1:]) for t in geometry.triangles),
            "authoring topology/winding/material section")
    morphs = unique(data["morphs"], "name", "authoring morphs")
    require(morphs.keys() == geometry.morphs.keys(), "authoring morph inventory")
    instance_slots = {identity: index for index, identity in enumerate(instances)}
    for name, expected in geometry.morphs.items():
        row = morphs[name]
        require(row["normalsPresent"], "authoring morph normal attribute missing: " + name)
        positions = sparse(row["positions"], 3, vertices, "authoring morph position: " + name)
        normals = sparse(row["normals"], 3, instances, "authoring morph normal: " + name)
        source_delta, _ = dense_rows(expected, len(vertices), 6)
        position_delta, _ = dense_rows(positions, len(vertices), 3)
        normal_delta, _ = dense_rows(normals, len(instances), 3, instance_slots)
        compare_morph_rows(position_delta[vertex_order], source_delta[vertex_order, :3], POSITION_CM,
                           "authoring morph position: " + name, zero_field="authoring dropped nonzero morph position: " + name)
        compare_morph_rows(normal_delta, source_delta[instance_sources, 3:], NORMAL,
                           "authoring morph normal: " + name, zero_field="authoring dropped nonzero morph normal: " + name)
    return {"passed": True, "vertices": len(vertices), "instances": len(instances), "morphs": len(morphs), **tangent_evidence,
            "quantizedInfluenceLosses": losses}


def check_render(geometry, data):
    vertices, sections_data = data["vertices"], data["sections"]
    require(bool(vertices) and bool(sections_data), "render geometry absent")
    require(data["weightBits"] in (8, 16) and data["normalBits"] in (8, 16), "render precision metadata")
    require(data["uvChannels"] == 1, "render UV channel inventory")
    # FPackedNormal uses signed normalized 8/16 bit components. One integer step
    # plus source normalization roundoff bounds unpacked components, not angles.
    normal_tolerance = 1 / (127 if data["normalBits"] == 8 else 32767) + NORMAL
    indices = data["indices"]
    native_positions = geometry.positions.astype(np.float32).astype(float)
    positions_match = normals_match = uvs_match = False
    claimed_sources = [row["sourceVertex"] for row in vertices]
    if all(isinstance(v, int) and 0 <= v < len(geometry.positions) for v in claimed_sources):
        claimed_sources = np.asarray(claimed_sources, dtype=np.intp)
        positions_match = all_rows_close((row["position"] for row in vertices), native_positions[claimed_sources], POSITION_CM)
        try:
            normals_match = all_rows_close((row["normal"] for row in vertices), unit_vector(geometry.normals[claimed_sources]), normal_tolerance)
            source_uvs = geometry.uvs[claimed_sources]
            if data["fullPrecisionUVs"]:
                uvs_match = all_rows_close((row["uv"] for row in vertices), source_uvs, 0)
            else:
                with np.errstate(over="ignore", invalid="ignore"):
                    half = source_uvs.astype(np.float16)
                    bounds = np.maximum(np.abs(np.nextafter(half, np.float16(np.inf)).astype(float) - half.astype(float)),
                                        np.abs(np.nextafter(half, np.float16(-np.inf)).astype(float) - half.astype(float)))
                if np.isfinite(half).all() and np.isfinite(bounds).all():
                    uvs_match = all_rows_close((row["uv"] for row in vertices), source_uvs, bounds)
        except (ValueError, TypeError, KeyError, IndexError, OverflowError):
            pass  # Original per-vertex checks below report the owning field.
    seen_vertices, seen_indices, coverage, triangles, losses = set(), set(), set(), [], []
    for section_index, section in enumerate(sections_data):
        require(not section["disabled"], "render section disabled")
        material = section["material"]
        require(0 <= material < len(geometry.slots), "render material index")
        first, count = section["baseVertex"], section["numVertices"]
        base, tri_count = section["baseIndex"], section["numTriangles"]
        require(first >= 0 and count >= 0 and first + count <= len(vertices) and base >= 0 and tri_count >= 0
                and base + 3 * tri_count <= len(indices), "render section bounds")
        ids, offsets = set(range(first, first + count)), set(range(base, base + 3 * tri_count))
        require(not ids & seen_vertices and not offsets & seen_indices, "render section overlap")
        seen_vertices |= ids
        seen_indices |= offsets
        for v in range(first, first + count):
            row = vertices[v]
            source = row["sourceVertex"]
            require(row["section"] == section_index and isinstance(source, int) and 0 <= source < len(geometry.positions),
                    "render/source vertex mapping")
            coverage.add(source)
            if not positions_match:
                close(row["position"], native_positions[source], POSITION_CM, f"render position {v}")
            if not normals_match:
                close(row["normal"], unit_vector(geometry.normals[source]), normal_tolerance, f"render source normal {v}")
            if not uvs_match:
                uv = geometry.uvs[source]
                native_uv = finite_array(row["uv"], f"render UV {v} (source {source}) actual")
                # Same per-component half spacing and exact full-precision rule as the
                # success-only batch above. Keep original failures and overflow context.
                if data["fullPrecisionUVs"]:
                    uv_bound = 0
                else:
                    with np.errstate(over="ignore", invalid="ignore"):
                        half = uv.astype(np.float16)
                    finite_array(half, f"render UV {v} (source {source}) source half UV projection")
                    with np.errstate(over="ignore", invalid="ignore"):
                        uv_bound = np.maximum(np.abs(np.nextafter(half, np.float16(np.inf)).astype(float) - half.astype(float)),
                                              np.abs(np.nextafter(half, np.float16(-np.inf)).astype(float) - half.astype(float)))
                    require(np.isfinite(uv_bound).all(), f"render UV {v} (source {source}): native half UV boundary overflow")
                close(native_uv, uv, uv_bound, f"render UV {v}")
            losses += check_weights(row["influences"], geometry, source, bits=data["weightBits"], field=f"render skin {v}")
        for at in range(base, base + tri_count * 3, 3):
            tri = indices[at:at + 3]
            require(all(v in ids for v in tri), "render triangle escapes section")
            triangles.append(canonical_triangle(material, [vertices[v]["sourceVertex"] for v in tri]))
    require(seen_vertices == set(range(len(vertices))) and seen_indices == set(range(len(indices))), "render buffer coverage")
    tangent_evidence = check_tangent_basis(geometry, vertices, [r["sourceVertex"] for r in vertices],
                                          label="render", packed_bits=data["normalBits"])
    # Built vertices may represent several source IDs only when every base channel
    # and the entire sparse morph signature are EXACTLY equal. The authoring and GLB
    # inventories above still preserve every original identity; native quantization
    # tolerances are never used to discover aliases.
    _, equivalence = audit_vertex_equivalence(geometry, data)
    require(equivalence["unprovenSourceIds"] == 0,
            "render source vertex omission: no exact full-signature alias for "
            + str(equivalence["unprovenSourceIds"]) + " source IDs; " + str(equivalence["unprovenReasons"]))
    require(equivalence["missingEquivalentTriangles"] == equivalence["extraEquivalentTriangles"] == 0,
            "render topology/winding/material sections after exact vertex equivalence")
    morphs = unique(data["morphs"], "name", "render morphs")
    require(morphs.keys() == geometry.morphs.keys(), "render morph inventory")
    render_sources = np.asarray([row["sourceVertex"] for row in vertices], dtype=np.intp)
    for name, expected in geometry.morphs.items():
        morph = morphs[name]
        values = sparse(morph["deltas"], 6, range(len(vertices)), "render morph: " + name)
        # UE 5.8 PopulateDeltas sets NumBaseMeshVerts to its input delta count,
        # despite the field name. It is metadata, never the render/source-map proof.
        require(len(values) <= morph["baseVertices"] <= len(vertices), "render morph base vertex metadata bounds: " + name)
        source_delta, _ = dense_rows(expected, len(geometry.positions), 6)
        native_delta, present = dense_rows(values, len(vertices), 6)
        compare_morph_rows(native_delta, source_delta[render_sources], [POSITION_CM] * 3 + [NORMAL] * 3,
                           "render morph position/normal: " + name, present=present,
                           missing_field="render dropped nonzero morph delta: " + name,
                           zero_field="render dropped nonzero morph channel: " + name)
        expected_sections = sorted({vertices[v]["section"] for v in values})
        require(len(set(morph["sections"])) == len(morph["sections"])
                and sorted(morph["sections"]) == expected_sections, "render morph section coverage: " + name)
    return {"passed": True, "vertices": len(vertices), "sourceVertices": len(geometry.positions), **tangent_evidence,
            "representedSourceIds": len(coverage), "sourceAliasCount": equivalence["provenAliasCount"],
            "sourceAliases": equivalence["sourceAliases"], "sourceAliasSha256": equivalence["sourceAliasSha256"],
            "exactEquivalenceClasses": equivalence["exactEquivalenceClasses"],
            "splitCopies": len(vertices) - len(coverage), "sections": len(sections_data), "morphs": len(morphs),
            "weightBits": data["weightBits"], "normalComponentBound": normal_tolerance,
            "quantizedInfluenceLosses": losses, "packedGpuMorphStreamsVerified": False}


def verify_native_geometry(payload, snapshot, *, body, source_glb, expected_source_sha256, material_paths):
    """Callable worker API; returns JSON-safe bounded evidence and fails closed.

    In the editor, capture_geometry(mesh) returns (error, JSON); check the error and
    hand off JSON to this offline NumPy-enabled process. Unreal's embedded Python need
    not import this module. material_paths maps source material asset IDs to their
    already-resolved native package paths. IDs are independently joined from the GLB;
    an incorrect staged material ID cannot pass by supplying its own expected path.
    Supply fresh-reloaded assets to the native reader.
    No paths are constructed, no assets are loaded, written, or built by this module.
    """
    result = {"passed": False, "scope": "source-stage-editor-authoring-and-built-render-LOD0",
              "tangentsVerified": False, "fullChannelVerificationPassed": False,
              "sourceInventory": {"passed": False}, "authoring": {"passed": False}, "render": {"passed": False},
              "evaluatedSkinningVerified": False, "renderedAcceptance": False, "differences": []}
    try:
        require(hashlib.sha256(source_glb).hexdigest() == expected_source_sha256, "source GLB digest changed since staging")
        document, binary = decode_glb(source_glb)
        geometry = read_stage(payload)
        result["sourceInventory"] = verify_source_geometry(geometry, body, document, binary)
        result["sourceInventory"]["glbSha256"] = expected_source_sha256
        geometry = reference_skin(geometry, body["wield"]["referencePose"] if body.get("wield") else None)
        require(snapshot["schemaVersion"] == 1 and snapshot["lod"] == 0, "native snapshot schema/LOD")
        require(type(snapshot.get("tangentCaptureVersion")) is int and snapshot["tangentCaptureVersion"] == 1,
                "native snapshot lacks tangent capture v1")
        require(len(snapshot["bones"]) == len(geometry.bones), "native bone count")
        for actual, expected in zip(snapshot["bones"], geometry.bones):
            require(actual["name"] == expected["name"] and actual["parent"] == expected["parent"], "native bone identity/parent")
            close(actual["position"], expected["position"], POSITION_CM, "native reference position")
            q = finite_array(actual["rotation"], "native reference rotation: " + actual["name"])
            r = finite_array(expected["rotation"], "staged reference rotation: " + expected["name"])
            require(q.shape == r.shape == (4,), "native reference rotation shape")
            close(q if np.dot(q, r) >= 0 else -q, r, NORMAL, "native reference rotation")
            close(actual["scale"], [1, 1, 1], 1e-8, "native reference scale")
        require([m["slot"] for m in snapshot["materials"]] == geometry.slots, "native material slot order")
        bindings = result["sourceInventory"]["materialBindings"]
        require(set(material_paths) == {m["assetId"] for m in bindings}, "source/native material identity inventory")
        require([m["asset"] for m in snapshot["materials"]] == [material_paths[m["assetId"]] for m in bindings],
                "native material package bindings")
        result["authoring"] = check_authoring(geometry, snapshot["authoring"])
        result["render"] = check_render(geometry, snapshot["render"])
        result["tangentsVerified"] = result["sourceInventory"]["sourceTangentsVerified"] and result["authoring"]["tangentsVerified"] and result["render"]["tangentsVerified"]
        result["fullChannelVerificationPassed"] = result["tangentsVerified"]
        result["passed"] = True
    except (ValueError, KeyError, IndexError, TypeError, OverflowError) as exc:
        result["differences"].append({"reason": str(exc)})
    return result
