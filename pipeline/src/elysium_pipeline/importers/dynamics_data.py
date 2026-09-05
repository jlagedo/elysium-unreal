"""Cookable secondary-motion records and provisional stock AnimDynamics recipes."""
from copy import deepcopy
import hashlib
import json
import math
from pathlib import Path
from types import SimpleNamespace

from elysium_pipeline.asset_names import rig_bone_name
from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.formats import mdl_secondary_motion as motion

SOURCE_FIELDS = {"sourceOffset", "firstBone", "terminalBone", "unusedAuthoredPreset",
                 "gravity", "damping", "springExponent", "maxAngleDegrees"}
POLICY = "provisional-animdynamics-v1"


def dynamics_projection(asset_id, semantics):
    """Keep source order/values and resolve bone walks without reading an MDL or legacy sidecar."""
    bones = [SimpleNamespace(index=row["index"], parent=row["parent"], name=row["name"])
             for row in semantics["mdl"]["bones"]]
    for index, bone in enumerate(bones):
        if bone.index != index or bone.parent >= index or bone.parent < -1:
            raise ValueError("secondary-motion skeleton is not a parent-first indexed tree")
    path = "models/" + asset_id.removeprefix("vtmb:model:") + ".mdl"
    chains, bodies, records = [], [], []
    for index, source in enumerate(semantics.get("secondaryMotion") or []):
        if set(source) != SOURCE_FIELDS:
            raise ValueError(f"secondary-motion record {index} has an unhandled field set")
        raw = motion.BoneChainRecord(source["firstBone"], source["terminalBone"],
                                     source["unusedAuthoredPreset"], source["gravity"],
                                     source["damping"], source["springExponent"], source["maxAngleDegrees"])
        if not all(math.isfinite(source[key]) for key in SOURCE_FIELDS):
            raise ValueError(f"secondary-motion record {index} is not finite")
        walk = motion.child_walk(bones, raw.first_bone, raw.terminal_bone)
        first = bones[walk[0]]
        parent = bones[first.parent].name.casefold() if first.parent >= 0 else ""
        first_name = first.name.casefold()
        gravity, damping, spring = motion._native_gravity_damping_spring(raw)
        if not all(math.isfinite(value) for value in (gravity, damping, spring)):
            raise ValueError(f"secondary-motion record {index} has an unrepresentable native recipe")
        settings = {"boundBone": rig_bone_name(first.name), "gravityScale": gravity,
                    "damping": damping, "angularSpring": spring,
                    "coneAngleDegrees": min(90., max(0., raw.max_angle_degrees))}
        record = {**deepcopy(source), "boneIndices": walk,
                  "boneNames": [rig_bone_name(bones[bone].name) for bone in walk],
                  "projection": "", "recipeIndex": -1, "reason": ""}
        if len(walk) > 1:
            record.update(projection="chain", recipeIndex=len(chains))
            # Preserve the existing provisional chain mapping; authored tuning limits what
            # is installed. The untouched source angle remains on the record beside it.
            chains.append({**settings, "chainEnd": record["boneNames"][-1],
                           "coneAngleDegrees": min(179., max(0., raw.max_angle_degrees))})
        elif parent.startswith("bip01 spine"):
            if (motion._breast_token_in(first.name)
                    or first_name in motion.ANONYMOUS_BREAST_FIRST_BONES.get(path.casefold(), ())):
                record.update(projection="breast-body", recipeIndex=len(bodies))
                bodies.append(settings)
            elif first_name in motion.REJECTED_SPINE_SINGLE_BONES.get(path.casefold(), ()):
                record.update(projection="source-only", reason="torso flap/rib; excluded from the breast host")
            else:
                raise ValueError(f"{path}: unclassified one-bone spine chain {first.name!r}")
        else:
            record.update(projection="source-only", reason=("single head curl" if parent == "bip01 head"
                                                           else "singleton outside the admitted body host"))
        records.append(record)
    return {"schemaVersion": "1.0.0", "assetId": asset_id,
            "assetPath": baked_unit(asset_id, "DYN"), "projectionPolicy": POLICY,
            "sourceRecordCount": len(records), "records": records, "chains": chains, "bodies": bodies}


def stage_dynamics_data(stage_root, manifest, log=print):
    from elysium_pipeline.importers.characters import _write, _json_bytes

    root = Path(stage_root)
    selected = set(manifest["selectedUnits"])
    count = records = 0
    for entry in manifest["assets"]:
        if entry["assetId"] not in selected:
            continue
        try:
            encoded = (root / entry["body"]).read_bytes()
            if hashlib.sha256(encoded).hexdigest() != entry["recipe"]["bodySha256"]:
                raise ValueError("body semantics changed after skeletal staging")
            body = json.loads(encoded)
            projection = dynamics_projection(entry["assetId"], body["sourceSemantics"])
            entry["dynamicsData"] = None
            entry["dynamicsDataAsset"] = None
            entry["recipe"]["dynamicsDataSha256"] = None
            if not projection["records"]:
                continue
            encoded = _json_bytes(projection)
            relative = entry["key"] + ".dynamics.json"
            _write(root / relative, encoded)
            entry["dynamicsData"] = relative
            entry["dynamicsDataAsset"] = projection["assetPath"]
            entry["recipe"]["dynamicsDataSha256"] = hashlib.sha256(encoded).hexdigest()
            count += 1
            records += projection["sourceRecordCount"]
        except (ValueError, KeyError, OSError, IndexError, OverflowError) as exc:
            manifest["stageFailures"].append({"assetId": entry["assetId"], "reason": "dynamics data: " + str(exc)})
    _write(root / "manifest.json", _json_bytes(manifest))
    log(f"character dynamics: {count} projected assets, {records} source records; {len(manifest['stageFailures'])} stage failures")
    return manifest
