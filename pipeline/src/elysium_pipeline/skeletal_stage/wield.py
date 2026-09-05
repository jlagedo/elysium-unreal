"""Wield decisions from V2 model/item/entity units; the retail decision rules stay shared."""
from __future__ import annotations

from elysium_pipeline import wield_corpus as rules
from elysium_pipeline.asset_names import rig_bone_name
from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.skeletal_stage import payload
from elysium_pipeline.skeletal_stage.unit import read_document, SkeletalUnitError


def discover(export_root, units):
    carried = set()
    for path in sorted((export_root / "maps").glob("*.entities.glb")):
        entities = read_document(path)["extensions"]["ELYSIUM_vtmb_map_entities"]["entities"]
        for entity in entities:
            if not entity.get("classname", "").startswith("npc_"):
                continue
            values = {r["key"].lower(): r["value"].strip() for r in entity["keyValues"]}
            carried.update(values[key].lower() for key in ("additionalequipment", "alternateequipment") if values.get(key))
    items, model_ids, failures, source_gaps = [], set(), [], []
    for path in sorted((export_root / "vdata/items").glob("*.glb")):
        root = read_document(path)["extensions"]["ELYSIUM_vtmb_vdata"]
        if root["projection"].get("rootKey") != "WeaponData":
            raise SkeletalUnitError(f"{path}: item projection is not WeaponData")
        fields = root["projection"]["fields"]
        id = root["identity"]["asset"]
        classname = id.removeprefix("vtmb:vdata:items/")
        item = {"assetId": id, "classname": classname,
                "animPrefix": fields["animPrefix"]["value"] or "",
                "showsViewModel": bool(fields["showsViewModel"]["value"]),
                "cameraClass": (fields["cameraClass"]["value"] or "").strip().lower(),
                "cantBeLast": fields["bitFlagCantBeLast"]["value"],
                "disciplineTarget": fields["bitFlagDisciplineTgt"]["value"],
                "reloadSingle": fields["reloadSingle"]["value"], "npcCarried": classname in carried,
                "models": {}}
        for sex in rules.SEXES:
            field = fields["models"]["wieldmodel_" + sex]
            source = rules.normalize(field["raw"] or "")
            kind = "empty" if not source else "null" if rules.is_null(source) else "real" if field["asset"] in units else "absent"
            item["models"][sex] = {"assetId": field["asset"], "source": source, "kind": kind,
                                    "meshAsset": baked_unit(field["asset"], "SK") if kind == "real" else None}
            if kind == "real":
                model_ids.add(field["asset"])
            elif kind == "absent":
                detail = {"assetId": id, "sex": sex, "model": field["asset"]}
                if field["resolved"]:
                    failures.append({**detail, "reason": "resolved wield model has no published unit"})
                else:
                    source_gaps.append({**detail, "reason": "authored model is absent from the source install"})
        if (item["models"]["f"]["kind"] == "empty") != (item["models"]["m"]["kind"] == "empty"):
            raise SkeletalUnitError(f"{id}: one-sided wield definition")
        items.append(item)
    return {"items": items, "modelIds": sorted(model_ids), "failures": failures, "sourceGaps": source_gaps}


def describe(unit, bodies, bone_rows, bone_map, reparented):
    lod = next((row for row in unit.extension["vtx"]["lods"] if row["index"] == 0), None)
    if lod is None:
        raise SkeletalUnitError(f"{unit.id}: wield model has no LOD0 geometry")
    skinned, positions = set(), []
    for primitive in unit.document["meshes"][lod["mesh"]]["primitives"]:
        source = primitive["extensions"]["ELYSIUM_vtmb_model"]
        count = len(source["sourceVertices"])
        points = unit.precise(source["sourcePositions"], (count, 3))
        joints = unit.accessor(primitive["attributes"]["JOINTS_0"])
        weights = unit.accessor(primitive["attributes"]["WEIGHTS_0"])
        used = list(dict.fromkeys(int(i) for i in unit.accessor(primitive["indices"]).reshape(-1)))
        positions.extend(tuple(float(v) for v in points[i]) for i in used)
        skinned.update(int(bone) for i in used for bone, weight in zip(joints[i], weights[i]) if weight > 0.)
    frames = lambda clip: unit.animation_frames(clip.base, clip.frames)
    animated = any(rules.frame_variance(unit.bones, frames(clip), range(len(unit.bones)))
                   for clip in unit.sequences if clip.frames > 0)
    classification = rules.classify_bones(unit.bones, skinned, bodies=bodies, animated=animated)
    motion = rules.check_motion_from_samples(unit.bones, classification, unit.sequences, frames)
    binding = "copy_pose" if classification.binding in ("socket_hand", "socket_prop") and not motion.ok else classification.binding
    pose = rules.bake_pose_from_samples(unit.bones, unit.sequences, frames)
    ref_rows = payload._ref_pose_rows(bone_rows, bone_map, pose.locals, unit.id, reparented)
    tip = rules.trail_tip_from_geometry(unit.bones, classification, positions) if binding == "socket_prop" else None
    checks = [rules.check_subtree(unit.bones, skinned, classification),
              rules.check_collapse(unit.bones, skinned, classification), motion]
    return {
        "binding": binding, "mountBone": rig_bone_name(classification.mount_bone),
        "handBone": rig_bone_name(classification.hand_bone), "collapseBone": rig_bone_name(classification.collapse_bone),
        "grip": classification.grip, "referencePoseSource": pose.source, "referenceClip": pose.label,
        "referencePose": [{"name": name, "parent": parent, "position": list(payload._conv_pos(pos)),
                           "rotation": list(payload._conv_quat(quat))} for name, parent, pos, quat in ref_rows],
        "referencePoseSourceLocals": [[list(pos), list(quat)] for pos, quat in pose.locals],
        "mountBindSource": [list(v) for v in classification.mount_bind] if classification.mount_bind else None,
        "trailTipSource": {"bone": classification.mount_bone, "position": list(tip[0]), "rotation": list(tip[1])} if tip else None,
        "trailTip": {"bone": rig_bone_name(classification.mount_bone), "position": list(payload._conv_pos(tip[0])),
                     "rotation": list(payload._conv_quat(tip[1]))} if tip else None,
        "onBody": rules.on_body_scope(None, classification.mount_bone, bodies=bodies) if classification.mount_bone else None,
        "boneCount": len(unit.bones), "skinnedBoneCount": len(skinned),
        "checks": {c.name: {"ok": c.ok, "detail": c.detail} for c in checks},
        "anomalies": list(classification.anomalies),
    }
