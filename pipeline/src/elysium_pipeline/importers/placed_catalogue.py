"""D10 model catalogue and rest equivalence measured solely from published GLBs.

Projection never writes generated state. All units can have rows, including units
without native geometry. Explicit unresolved source references can be appended with
absent_model; no alias/stem lookup or guessed native package establishes availability.
"""
from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.importers.catalogue_common import catalogue, evidence, native_ref, object_path, require_coverage, unique
from elysium_pipeline.placed_models import INTRINSIC_CLIPS, fnv1a_32, normalize_model_path, rest_candidates

POLICY = "glb-bind-and-all-rest-frame0-v1"


def collect_placed_references(entity_units, map_units, *, published_ids):
    """Complete non-NPC entity + GAME_LUMP join from exported units, read-only.

    Retain source record indices as provenance. Runtime placement tokens are STILL
    the live entity Handle.Index (or the map owner's existing token), never these
    source indices. Output wires/key values are evidence here, not a new dispatcher.
    """
    from elysium_pipeline.formats.map_glb.model import model_asset_id
    published = set(published_ids)
    rows = {}

    def add(id, path, resolved, use, full=False, required=()):
        if resolved and id not in published:
            raise ValueError(f"resolved placed source has no published model unit: {id}")
        row = rows.setdefault(id, {"assetId": id, "modelPath": normalize_model_path(path),
                                   "sourceAbsent": not resolved, "uses": [], "fullClips": False, "requiredClips": []})
        if row["sourceAbsent"] != (not resolved) or row["modelPath"] != normalize_model_path(path):
            raise ValueError(f"placed source identity/resolution disagrees: {id}")
        row["uses"].append(use)
        row["fullClips"] |= full
        row["requiredClips"] = sorted(set(row["requiredClips"]) | set(required))

    for unit in entity_units:
        targets = [w["target"].strip().casefold() for e in unit["entities"] for w in e["outputs"]
                   if w["input"].casefold() == "setanimation" and w["target"] and not w["target"].startswith("!")]
        for entity in unit["entities"]:
            model = entity.get("model") or {}
            if entity["classname"].casefold().startswith("npc_") or model.get("kind") != "model":
                continue
            keys = {r["key"].casefold(): r["value"] for r in entity["keyValues"]}
            id = model["asset"]
            reference = next((r for r in entity["references"] if r["role"] == "model" and r["asset"] == id), None)
            if reference is None:
                raise ValueError(f"{id}: entity model lacks source resolution record")
            name = keys.get("targetname", "").casefold()
            targeted = any(name.startswith(t[:-1]) if t.endswith("*") else name == t for t in targets)
            meaningful = lambda key: keys.get(key, "").strip().casefold() not in ("", "0", "none", "null")
            use = {"mapId": unit["identity"]["asset"], "source": "entity", "sourceRecordIndex": entity["index"],
                   "classname": entity["classname"], "keyValues": entity["keyValues"], "reference": reference}
            add(id, model["path"], reference["resolved"], use,
                targeted or meaningful("demo_sequence") or meaningful("loopsequence"),
                INTRINSIC_CLIPS.get(entity["classname"].casefold(), ()))
    for unit in map_units:
        source = unit["staticProps"]
        for prop in source["props"]:
            if not 0 <= prop["propType"] < len(source["dictionary"]):
                raise ValueError(f"{unit['identity']['asset']}: placed model has invalid dictionary index")
            path = source["dictionary"][prop["propType"]]["name"]
            id = model_asset_id(path)
            if not prop["asset"]:
                raise ValueError(f"{id}: static prop has no explicit model reference")
            resolved = prop["asset"].startswith("vtmb:model:")
            use = {"mapId": unit["identity"]["asset"], "source": "static-prop", "sourceRecordIndex": prop["index"],
                   "dictionaryModel": path, "record": prop}
            add(id, path, resolved, use)
    return rows


def measure_static_equivalence(unit):
    """Compare GLB source geometry against bind and every corrected rest frame zero.

    This checks geometry, not whether cloth/animation/entity behavior can be replaced.
    A missing pose/normal/topology is an explicit unproved result, never a true bit.
    """
    import numpy as np
    from elysium_pipeline.placed_models import _transform, _inverse_bind, _geometric_normals
    from elysium_pipeline.skeletal_stage import payload

    result = {"policy": POLICY, "proven": False, "equivalent": False, "reason": "",
              "maxPositionCm": 0., "maxNormalDegrees": 0., "poses": []}
    candidates = rest_candidates(unit.sequences)
    if not unit.bones or not candidates:
        result["reason"] = "no bones or no rest candidates"
        return result
    lod = next((r for r in unit.extension["vtx"]["lods"] if r["index"] == 0), None)
    if lod is None:
        result["reason"] = "no LOD0 geometry"
        return result
    surfaces = []
    for primitive_index, primitive in enumerate(unit.document["meshes"][lod["mesh"]]["primitives"]):
        ext = primitive["extensions"]["ELYSIUM_vtmb_model"]
        count = len(ext["sourceVertices"])
        positions = unit.precise(ext["sourcePositions"], (count, 3))
        normals = unit.precise(ext["sourceNormals"], (count, 3)).copy()
        tris = unit.accessor(primitive["indices"]).reshape(-1, 3).astype(int)
        used = np.unique(tris)
        if not len(used):
            continue
        normals = _geometric_normals({"pos": positions, "nrm": normals, "tris": tris})
        joints = unit.accessor(primitive["attributes"]["JOINTS_0"])[used].astype(int)
        weights = unit.accessor(primitive["attributes"]["WEIGHTS_0"])[used]
        if (joints.shape != weights.shape or np.any(joints < 0) or np.any(joints >= len(unit.bones))
                or not np.isfinite(weights).all() or np.any(weights < 0)
                or not np.allclose(weights.sum(axis=1), 1., atol=1e-5, rtol=0)):
            raise ValueError(f"{unit.id}: invalid static-equivalence skin data")
        undefined_normals = np.flatnonzero(np.linalg.norm(normals[used], axis=1) < 1e-12)
        if len(undefined_normals):
            # A degenerate source surface may have no recoverable shading normal. It
            # cannot prove static replacement; preserve its skeletal representation.
            result["reason"] = "source surface has undefined geometric normals"
            result["undefinedNormalVertices"] = [int(used[i]) for i in undefined_normals]
            result["sourcePrimitive"] = primitive_index
            return result
        surfaces.append((positions[used], normals[used], joints, weights))
    if not surfaces:
        result["reason"] = "no drawn LOD0 vertices"
        return result
    inverse = [_inverse_bind(b) for b in unit.bones]

    def score(name, locals):
        world = []
        for bone, (position, rotation) in zip(unit.bones, locals):
            matrix = _transform(position, rotation)
            world.append(matrix if bone.parent < 0 else world[bone.parent] @ matrix)
        skin = np.asarray([w @ inv for w, inv in zip(world, inverse)])
        for positions, normals, joints, weights in surfaces:
            transforms = skin[joints]
            points = np.column_stack((positions, np.ones(len(positions))))
            posed = np.einsum("nkij,nj,nk->ni", transforms, points, weights)[:, :3]
            posed_normals = np.einsum("nkij,nj,nk->ni", transforms[:, :, :3, :3], normals, weights)
            lengths = np.linalg.norm(posed_normals, axis=1)
            if np.any(lengths < 1e-12) or not np.isfinite(posed).all() or not np.isfinite(posed_normals).all():
                raise ValueError(f"{unit.id}: invalid posed geometry for {name}")
            dots = np.clip(np.einsum("ni,ni->n", posed_normals / lengths[:, None], normals), -1., 1.)
            result["maxPositionCm"] = max(result["maxPositionCm"], float(np.linalg.norm(posed - positions, axis=1).max()) * 2.54)
            result["maxNormalDegrees"] = max(result["maxNormalDegrees"], float(np.degrees(np.arccos(dots)).max()))
        result["poses"].append(name)

    score("bind", [(b.pos, b.quat) for b in unit.bones])
    for clip in candidates:
        if clip.frames <= 0:
            result["reason"] = f"rest candidate {clip.label} has no frames"
            return result
        frames = unit.animation_frames(clip.base, clip.frames)
        if not frames:
            result["reason"] = f"rest candidate {clip.label} has no samples"
            return result
        split, _ = payload._split_rotation_tracks(unit.bones, frames, clip.frames)
        locals = []
        for bone, (position, rotation) in zip(unit.bones, frames[0]):
            if np.linalg.norm(rotation) <= 1e-12:
                position, rotation = bone.pos, bone.quat
            locals.append((position, split[bone.index][0] if bone.index in split else rotation))
        score(clip.label, locals)
    result["proven"] = True
    result["equivalent"] = result["maxPositionCm"] <= .01 and result["maxNormalDegrees"] <= .1
    result["reason"] = "within 0.01 cm / 0.1 degrees" if result["equivalent"] else "rest geometry differs"
    return result


def measure_clip_bounds(unit):
    """Port the legacy max-coordinate bound/FK extent, in cm, without install reads.

    This is the legacy conservative bounds policy, not a new geometric radius rule.
    Keep it separate from the 0.01cm/0.1deg rest equivalence measurement.
    """
    import numpy as np
    from elysium_pipeline.formats.mdl_skel import rot_matrices
    result = {}
    for clip in unit.sequences:
        extent = max(abs(v) for v in (*clip.bbmin, *clip.bbmax))
        if unit.bones and clip.frames > 0:
            frames = unit.animation_frames(clip.base, clip.frames)
            if len(frames) != clip.frames:
                raise ValueError(f"{unit.id}: missing bounds samples for {clip.label}")
            n = len(unit.bones)
            lt = np.asarray([[r[0] for r in frame] for frame in frames], dtype=float)
            lq = np.asarray([[r[1] for r in frame] for frame in frames], dtype=float)
            lr = rot_matrices(lq.reshape(-1, 4)).reshape(clip.frames, n, 3, 3)
            wt, wr = np.empty_like(lt), np.empty_like(lr)
            for i, bone in enumerate(unit.bones):
                if bone.parent < 0:
                    wt[:, i], wr[:, i] = lt[:, i], lr[:, i]
                else:
                    wt[:, i] = wt[:, bone.parent] + np.einsum("fab,fb->fa", wr[:, bone.parent], lt[:, i])
                    wr[:, i] = np.einsum("fab,fbc->fac", wr[:, bone.parent], lr[:, i])
            extent = max(extent, float(np.abs(wt).max()))
        if not np.isfinite(extent):
            raise ValueError(f"{unit.id}: nonfinite bounds for {clip.label}")
        # Legacy serialized metres rounded to four places; retain that exact value in cm.
        result[clip.label] = round(extent * .0254, 4) * 100.
    return result


def project_placed_model(unit, *, staged=None, native_body=None, static_mesh="", proof=None, clip_bounds=None, usage=None):
    """native_body is the owning body_data.project_body result, including native grids.

    An unproved row remains useful for inventory, but cannot authorize static replacement.
    A caller must refuse acceptanceIssues before publishing a replacement catalogue.
    """
    id = unit.id
    identity = unit.extension["identity"]
    usage = usage or {"assetId": id, "fullClips": False, "requiredClips": [], "uses": []}
    if usage["assetId"] != id:
        raise ValueError(f"{id}: placed reference join belongs to another model")
    cloth = bool((unit.extension.get("cloth") or {}).get("garments"))
    proof = proof or {"policy": POLICY, "proven": False, "equivalent": False, "reason": "not measured"}
    if proof["policy"] != POLICY:
        raise ValueError(f"{id}: stale static-equivalence policy")
    if staged and staged["assetId"] != id or native_body and native_body["assetId"] != id:
        raise ValueError(f"{id}: native projection identity differs")
    mesh = native_ref(id, "SK", staged["meshAsset"]) if staged and staged.get("meshAsset") else ""
    static = native_ref(id, "SM", static_mesh) if static_mesh else ""
    # Static importer admits submodel 0 only. Never lose a second submodel (Ming Xiao).
    lod = next((r for r in unit.extension["vtx"]["lods"] if r["index"] == 0), None)
    primitives = unit.document["meshes"][lod["mesh"]]["primitives"] if lod else []
    topology_equal = all(p["extensions"]["ELYSIUM_vtmb_model"]["model"] == 0 for p in primitives)
    sufficient = bool(static and proof["proven"] and proof["equivalent"] and not cloth and topology_equal)
    native_body = native_body or {"sequences": [], "nativeSequences": {}, "nativeBlendSpaces": {}}
    for field, prefix in (("nativeSequences", "A"), ("nativeBlendSpaces", "BS")):
        for label, path in native_body[field].items():
            native_ref(id, prefix, path, label=label)
    descriptors = {r["label"].casefold(): r for r in native_body["sequences"] if r["owner"] == id}
    bounds = measure_clip_bounds(unit) if clip_bounds is None else clip_bounds
    require_coverage(bounds, (c.label for c in unit.sequences))
    rests = [unit.sequences.index(r) for r in rest_candidates(unit.sequences)]
    clips, issues = [], []
    for index, clip in enumerate(unit.sequences):
        ref = descriptors.get(clip.label.casefold(), {}).get("assets", {})
        sequence, blend, base = (ref.get(k, "") for k in ("sequence", "blendSpace", "baseCell"))
        if (sequence and sequence not in native_body["nativeSequences"].values()
                or blend and blend not in native_body["nativeBlendSpaces"].values()
                or base and base not in native_body["nativeSequences"].values()
                or blend and not base):
            raise ValueError(f"{id}: clip {clip.label} references an undeclared native product")
        state = "native" if sequence or blend else "static-rest-only" if sufficient and index in rests else "unavailable"
        if state == "unavailable":
            issues.append(f"clip has no native representation: {clip.label}")
        clips.append({"index": index, "label": clip.label, "activity": clip.activity,
                      "weight": clip.actweight, "flags": clip.flags, "frames": clip.frames, "fps": clip.fps,
                      "owner": id, "sequence": sequence, "blendSpace": blend, "baseCell": base, "state": state,
                      "boundsRadiusCm": bounds[clip.label]})
    if cloth and not mesh:
        issues.append("cloth owner has no declared skeletal mesh")
    if not static and not mesh:
        issues.append("model has no declared native geometry")
    for required in usage["requiredClips"]:
        match = next((c for c in clips if c["label"].casefold() == required.casefold()), None)
        if not match or match["state"] != "native":
            issues.append(f"intrinsic clip has no native representation: {required}")
    if usage["fullClips"] and any(c["state"] != "native" for c in clips):
        issues.append("animated placement requires the complete native clip vocabulary")
    return {"assetId": id, "modelPath": normalize_model_path(identity["modelPath"]),
            "sourceAbsent": False, "sourceReason": "", "roles": identity["roles"],
            "staticMesh": static, "skeletalMesh": mesh,
            "bodyData": object_path(baked_unit(id, "DA")) if mesh and native_body.get("assetId") else "",
            "hasCloth": cloth, "staticEquivalentProven": proof["proven"],
            "staticEquivalent": proof["equivalent"], "staticRestSuffices": sufficient,
            "staticTopologyEquivalent": topology_equal, "restCandidates": rests, "clips": clips,
            "fullClipsRequired": usage["fullClips"], "requiredClips": usage["requiredClips"],
            "placementEvidence": evidence(usage),
            "nativeSequences": native_body["nativeSequences"], "nativeBlendSpaces": native_body["nativeBlendSpaces"],
            "acceptanceIssues": issues, "sourceEvidence": evidence({
                "identity": identity, "sequences": unit.mdl["sequences"],
                "discardedSequenceDescriptors": unit.dropped_sequences, "staticEquivalence": proof})}


def absent_model(asset_id, model_path, reason):
    baked_unit(asset_id, "SM")
    if not reason:
        raise ValueError("absent source reference requires an explicit reason")
    return {"assetId": asset_id, "modelPath": normalize_model_path(model_path), "sourceAbsent": True,
            "sourceReason": reason, "roles": [], "staticMesh": "", "skeletalMesh": "", "bodyData": "",
            "hasCloth": False, "staticEquivalentProven": False, "staticEquivalent": False,
            "staticRestSuffices": False, "staticTopologyEquivalent": False,
            "restCandidates": [], "clips": [], "nativeSequences": {}, "nativeBlendSpaces": {},
            "fullClipsRequired": False, "requiredClips": [], "placementEvidence": "",
            "acceptanceIssues": [], "sourceEvidence": evidence({"modelPath": model_path, "reason": reason})}


def project_placed_catalogue(rows, *, expected_ids, require_accepted=True):
    models = unique(rows, "assetId")
    require_coverage(models, expected_ids)
    unique(models.values(), "modelPath")
    issues = {id: r["acceptanceIssues"] for id, r in models.items() if r["acceptanceIssues"]}
    if issues and require_accepted:
        raise ValueError(f"placed catalogue has unaccepted native coverage: {evidence(issues)}")
    return catalogue("PlacedModels", {"models": models})


def select_rest(row, placement_token):
    """Preserve the existing UTF-8 + LE uint32 FNV-1a weighted selection exactly."""
    choices = [row["clips"][i] for i in row["restCandidates"]]
    if not choices:
        return None
    pick = fnv1a_32(row["modelPath"], placement_token) % sum(max(1, c["weight"]) for c in choices)
    for clip in choices:
        pick -= max(1, clip["weight"])
        if pick < 0:
            return clip
    raise AssertionError("unreachable weighted selection")
