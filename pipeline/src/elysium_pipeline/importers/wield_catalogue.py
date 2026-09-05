"""Cookable D9 item/model join. Decisions are copied from body.wield, never reclassified.

Call project_wield_catalogue with the WHOLE merged stage manifest, a body loader, and
the same complete GLB character bone trees used by skeletal_stage.wield.describe.
The result is passed to UElysiumWieldCatalogue::ApplyJson at editor finalization.
"""
import hashlib
import json

from elysium_pipeline.asset_names import rig_bone_name
from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.importers.catalogue_common import (
    catalogue, evidence, native_ref, object_path, quaternion, unique, vector,
)

BINDINGS = {"socket_prop": "SocketProp", "socket_hand": "SocketHand",
            "leader_pose": "LeaderPose", "copy_pose": "CopyPose", "projectile": "Projectile"}


def compatibility(pose, body_trees, mount, hand):
    """Name ownership, not a promise that a runtime binding supports unmatched motion."""
    result = {}
    for id, source_tree in sorted(body_trees.items()):
        baked_unit(id, "SK")
        tree = {rig_bone_name(name).casefold(): rig_bone_name(parent).casefold()
                for name, parent in source_tree.items()}
        if len(tree) != len(source_tree):
            raise ValueError(f"{id}: folded body bone collision")
        names = {r['name'].casefold() for r in pose}
        result[id] = {"assetId": id, "mountPresent": mount.casefold() in tree,
                      "handPresent": hand.casefold() in tree,
                      "mountParent": tree.get(mount.casefold(), ""),
                      "matchedBones": [r['name'] for r in pose if r['name'].casefold() in tree],
                      "unmatchedBones": [r['name'] for r in pose if r['name'].casefold() not in tree]}
        if len(names) != len(pose):
            raise ValueError("folded wield bone collision")
    return result


def project_wield_catalogue(manifest, body_for, body_trees):
    source = manifest["wieldCatalogue"]
    if manifest["stageFailures"] or source["failures"]:
        raise ValueError("cannot project a failed character/wield stage")
    entries = unique(manifest["assets"], "assetId")
    unique(source["items"], "classname")
    declared = source["modelIds"]
    if len(set(declared)) != len(declared):
        raise ValueError("duplicate wield model id")
    if not body_trees:
        raise ValueError("complete wield body compatibility corpus is required")
    # The producer walks model paths in canonical order and preserves each source bone order.
    # This refuses even a missing body which happens not to declare this weapon's mount.
    tree_digest = hashlib.sha256(json.dumps(dict(sorted(body_trees.items())), ensure_ascii=False,
                                           allow_nan=False, separators=(",", ":")).encode("utf-8")).hexdigest()
    models = {}
    for id in declared:
        entry = entries[id]
        if entry["recipe"]["wieldBodyTrees"] != tree_digest:
            raise ValueError(f"{id}: complete body tree fingerprint differs from wield staging")
        body = body_for(entry)
        decision = body["wield"]
        if body["assetId"] != id or not decision or decision["binding"] != entry["wieldBinding"]:
            raise ValueError(f"{id}: missing or inconsistent staged wield decision")
        binding = BINDINGS[decision["binding"]]
        if not entry["wieldSkeletonSource"] or not entry["recipe"]["wieldRigSha256"]:
            raise ValueError(f"{id}: wield reference skeleton is not declared")
        pose = decision["referencePose"]
        if not pose or any(r['parent'] >= i or r['parent'] < -1 for i, r in enumerate(pose)):
            raise ValueError(f"{id}: invalid reference pose")
        tip = decision["trailTip"]
        if (binding == "SocketProp") != bool(tip):
            raise ValueError(f"{id}: TrailTip disagrees with staged binding")
        if decision["referencePoseSource"] not in ("clip", "bind"):
            raise ValueError(f"{id}: unknown reference pose ownership")
        clips = {p.rsplit("/A_", 1)[-1]: object_path(p) for p in entry["animationAssets"]}
        reference_clip = ""
        if decision["referencePoseSource"] == "clip":
            reference_clip = native_ref(id, "A", baked_unit(id, "A", label=decision["referenceClip"]),
                                        label=decision["referenceClip"])
            if reference_clip not in clips.values():
                raise ValueError(f"{id}: reference clip is not a staged native product")
        on_body = decision["onBody"]
        scope = compatibility(pose, body_trees, decision["mountBone"], decision["handBone"])
        if on_body is not None and sum(r["mountPresent"] for r in scope.values()) != on_body["bodies"]:
            raise ValueError(f"{id}: body compatibility corpus differs from wield staging")
        models[id] = {"assetId": id, "mesh": native_ref(id, "SK", entry["meshAsset"]),
                      "skeleton": native_ref(id, "SKEL", entry["animationSkeletonAsset"]),
                      "binding": binding, "mountBone": decision["mountBone"],
                      "handBone": decision["handBone"], "collapseBone": decision["collapseBone"],
                      "grip": decision["grip"], "referencePoseSource": decision["referencePoseSource"],
                      "referenceOwner": id, "referenceClipLabel": decision["referenceClip"],
                      "referenceClip": reference_clip, "nativeSequences": clips,
                      "referencePose": [{"name": r["name"], "parent": r["parent"],
                                         "position": vector(r["position"]), "rotation": quaternion(r["rotation"])} for r in pose],
                      "hasTrailTip": bool(tip), "trailTipBone": tip["bone"] if tip else "",
                      "trailTipPosition": vector(tip["position"] if tip else [0, 0, 0]),
                      "trailTipRotation": quaternion(tip["rotation"] if tip else [0, 0, 0, 1]),
                      "bodies": scope, "decisionEvidence": evidence(decision)}
    items, used, absent = {}, set(), []
    for item in source["items"]:
        refs = {}
        for sex in ("f", "m"):
            ref = item["models"][sex]
            state = {"real": "Real", "empty": "Empty", "null": "Null", "absent": "Absent"}[ref["kind"]]
            id = ref["assetId"] or ""
            mesh = ""
            if state == "Real":
                used.add(id)
                mesh = native_ref(id, "SK", ref["meshAsset"])
                if models[id]["mesh"] != mesh:
                    raise ValueError(f"{item['classname']}: wield mesh ownership mismatch")
            elif ref["meshAsset"]:
                raise ValueError("authored non-real wield reference carries a mesh")
            if state == "Absent":
                absent.append((item["assetId"], sex, id))
            refs[sex] = {"assetId": id, "source": ref["source"], "state": state, "mesh": mesh}
        items[item["classname"].casefold()] = {
            "assetId": item["assetId"], "classname": item["classname"],
            "showsWieldModel": item["showsViewModel"], "female": refs["f"], "male": refs["m"],
            "sourceEvidence": evidence(item)}
    if used != set(models):
        raise ValueError("wield model catalogue is not exactly the item model closure")
    gaps = [(r["assetId"], r["sex"], r["model"]) for r in source["sourceGaps"]]
    if sorted(gaps) != sorted(absent):
        raise ValueError("absent references and explicit source gap ledger disagree")
    return catalogue("WieldModels", {"items": items, "models": models,
                                     "sourceGaps": evidence(source["sourceGaps"])})
