"""Cookable mesh semantics projected from a model GLB, with no install or legacy reads."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path

from elysium_pipeline.asset_names import rig_bone_name
from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.formats.unit_contract.container import read_document
from elysium_pipeline.importers.expression_tables import expression_model_projection
from elysium_pipeline.skeletal_stage import payload
from elysium_pipeline.skeletal_stage.unit import source_position

EXTENSION = "ELYSIUM_vtmb_model"
OPERATIONS = {"CONST", "FETCH1", "FETCH2", "ADD", "SUB", "MUL", "DIV"}


def object_path(path):
    if not path or "." in path.rsplit("/", 1)[-1]:
        return path
    return path + "." + path.rsplit("/", 1)[-1]


class MaterialFacts:
    """Effective facts from the material stage's actual parent graph and consumer routing."""
    def __init__(self, manifest):
        self.entries = {row["assetPath"]: row for row in manifest["assets"]}
        self.cache = {}

    def effective(self, path, seen=()):
        if path in self.cache:
            return self.cache[path]
        if path in seen:
            raise ValueError("cyclic staged material parent: " + path)
        entry = self.entries[path]
        inherited = self.effective(entry["parent"], (*seen, path)) if entry["patched"] else {}
        value = {"assetPath": path}
        for key in ("textures", "scalars"):
            value[key] = {**inherited.get(key, {}), **entry[key]}
        self.cache[path] = value
        return value

    def __call__(self, id):
        if id.startswith("vtmb:missing-material:"):
            return {"assetPath": "/Game/ElysiumGenerated/Materials/V2/MI_V2_Missing", "textures": {}, "scalars": {}}
        entry = self.entries[baked_unit(id, "MI")]
        path = entry.get("skinnedAsset")
        if not path:
            raise ValueError("material has no skeletal route: " + id)
        return self.effective(path)


def stage_mesh_data(export_root, stage_root, manifest, material_manifest, log=print):
    """Add hashed typed-data inputs to the core stage; selected meshes only, atomic files."""
    from elysium_pipeline.importers.characters import _write, _json_bytes

    facts = MaterialFacts(json.loads(Path(material_manifest).read_text(encoding="utf-8")))
    selected = set(manifest["selectedUnits"])
    count = 0
    for entry in manifest["assets"]:
        if entry["assetId"] not in selected or not entry["meshAsset"]:
            continue
        try:
            source = Path(export_root) / entry["unitGlb"]
            with source.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            if digest != entry["recipe"]["unitSha256"]:
                raise ValueError("model changed after skeletal staging")
            projection = mesh_projection(read_document(source), facts)
            data = _json_bytes(projection)
            relative = entry["key"] + ".mesh.json"
            _write(Path(stage_root) / relative, data)
            entry["meshData"] = relative
            entry["recipe"]["meshDataSha256"] = hashlib.sha256(data).hexdigest()
            count += 1
        except (ValueError, KeyError, OSError, IndexError) as exc:
            manifest["stageFailures"].append({"assetId": entry["assetId"], "reason": "mesh data: " + str(exc)})
    _write(Path(stage_root) / "manifest.json", _json_bytes(manifest))
    log(f"character mesh data: {count} projected, {len(manifest['stageFailures'])} stage failures")
    return manifest


def facial_projection(extension):
    source = extension["facial"]
    if not source:
        return None
    rules = []
    for rule in source["rules"]:
        ops = []
        for op in rule["operations"]:
            name = op["operation"]
            if name not in OPERATIONS:
                raise ValueError("unhandled facial operation: " + name)
            row = [name]
            if name == "CONST":
                row.append(op["value"])
            elif name in ("FETCH1", "FETCH2"):
                row.append(op["index"])
            ops.append(row)
        rules.append({"flexdesc": rule["flexDescription"], "ops": ops})
    return {
        "flexdescs": list(source["flexDescriptions"]),
        "controllers": [{key: row[key] for key in ("type", "name", "min", "max")} for row in source["controllers"]],
        "rules": rules,
        "morphs": [{"name": row["name"], "flexdesc": row["flexDescription"], "targets": list(row["targets"])}
                   for row in source["morphTargets"]],
        # Forward is diagnostic today, but still obeys the cooked coordinate contract.
        "mouths": [{"bone": row["bone"], "forward": list(map(float, payload._conv_dir(row["forward"]))),
                    "flexdesc": row["flexDescription"]} for row in source["mouths"]],
        "phoneme_filter": list(extension["mdl"]["header"]["phonemeFilter"]),
    }


def mesh_projection(document, material_for):
    """Native-frame value records. `material_for(id)` supplies effective staged material facts."""
    extension = document["extensions"][EXTENSION]
    id = extension["identity"]["asset"]
    mdl = extension["mdl"]
    bones = mdl["bones"]
    key = id.removeprefix("vtmb:model:")
    facial = facial_projection(extension)
    eye_nodes = [(index, node) for index, node in enumerate(document["nodes"])
                 if "eyeballIndex" in node.get("extensions", {}).get(EXTENSION, {})]
    eyes, anomalies = [], []
    for part in mdl["bodyParts"]:
        for model in part["models"]:
            by_eye = {}
            for mesh in model["meshes"]:
                if mesh["materialType"] != 1:
                    continue
                index, slot = mesh["materialParam"], mesh["material"]
                if index in by_eye and by_eye[index] != slot:
                    raise ValueError(f"{id}: eye {index} binds multiple material columns")
                by_eye[index] = slot
            for source in model["eyeballs"]:
                ordinal = len(eyes)
                if ordinal >= len(eye_nodes):
                    raise ValueError(f"{id}: eye {ordinal} has no GLB origin node")
                node_index, node = eye_nodes[ordinal]
                tag = node["extensions"][EXTENSION]
                bone = source["bone"]
                if tag["eyeballIndex"] != source["index"] or tag["bone"] != bone:
                    raise ValueError(f"{id}: eye record/node order disagrees at {ordinal}")
                if node_index not in document["nodes"][bone].get("children", []):
                    raise ValueError(f"{id}: eye node is not local to its declared bone")
                slot = by_eye.get(source["index"])
                material = None
                if slot is not None:
                    material_id = extension["materialBindings"]["skinFamilies"][0][slot]
                    material = material_for(material_id)
                else:
                    anomalies.append({"code": "eyeHasNoMaterialMesh", "bodyPart": part["index"],
                                      "bodyModel": model["index"], "eye": source["index"]})
                eye = {name: deepcopy(source[name]) for name in (
                    "index", "up", "forward", "zoffset", "radius", "iris_scale", "upperflexdesc", "lowerflexdesc",
                    "uppertarget", "lowertarget", "upperlidflexdesc", "lowerlidflexdesc")}
                eye.update(bone=rig_bone_name(bones[bone]["name"]), bone_index=bone,
                           org=list(source_position(node["translation"])),
                           body_part=part["index"], body_model=model["index"],
                           material=mdl["textures"][slot]["name"] if slot is not None else "",
                           iris_asset=object_path((material or {}).get("textures", {}).get("Iris", "")),
                           vampire=bool((material or {}).get("scalars", {}).get("Vampire", 0)))
                eyes.append(eye)
    if len(eye_nodes) != len(eyes):
        raise ValueError(f"{id}: {len(eye_nodes)} eye nodes but {len(eyes)} records")
    eye_set = payload.unreal_eye_rig({"eyeballs": eyes}) if eyes else None
    raw_rules = (extension["procedural"] or {}).get("axisInterpolation", [])
    rules = payload.unreal_axis_rules([{
        "bone": rig_bone_name(row["bone"]), "bone_index": row["boneIndex"],
        "control": rig_bone_name(row["control"]), "control_index": row["controlIndex"],
        "axis_index": row["axisIndex"], "pos": row["pos"], "quat": row["quat"],
    } for row in raw_rules])
    return {"schemaVersion": "1.0.0", "modelPath": "models/" + key + ".mdl", "stem": key.rsplit("/", 1)[-1],
            "facial": facial, "eyes": eye_set,
            "composition": {"driver_axes": [list(axis) for axis in payload.DRIVER_AXES], "rules": rules},
            "splitBones": [rig_bone_name(row["name"]) for row in bones if row["flags"] & 2],
            "materialSlots": deepcopy(extension["materialBindings"]["slots"]),
            "skinTable": deepcopy(mdl["skinTable"]),
            "skinFamilies": [[object_path(material_for(material)["assetPath"]) for material in family]
                             for family in extension["materialBindings"]["skinFamilies"]],
            "expressionTables": deepcopy((extension["facial"] or {}).get("selectedTables", [])),
            "expressionData": expression_model_projection(extension),
            "anomalies": anomalies}
