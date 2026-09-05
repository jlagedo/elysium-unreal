"""Whole-corpus model and cinematic lookup; stems are aliases, never package addresses."""
from collections import defaultdict
from pathlib import Path
import hashlib

from elysium_pipeline.asset_names import safe_name
from elysium_pipeline.asset_paths import baked_unit, corpus_path
from elysium_pipeline.importers.character_data import object_path

# Existing human-facing cast/capture keys. The expanded corpus also publishes unused variants
# with these basenames, so their introduction must not retarget an established console/capture key.
ESTABLISHED_CAST_ALIASES = {
    "vv": "vtmb:model:character/npc/unique/downtown/vv/vv",
    "andrei": "vtmb:model:character/npc/unique/hollywood/andrei/andrei",
    "stalker_female": "vtmb:model:character/npc/unique/malkavian_mansion/stalker/stalker_female",
}


def project_cast(manifest):
    inventory = {row["assetId"]: row for row in manifest["inventory"]}
    models, candidates, cinematics = {}, defaultdict(set), {}
    for entry in manifest["assets"]:
        id, key = entry["assetId"], entry["key"]
        path = "models/" + key + ".mdl"
        basename = key.rsplit("/", 1)[-1].lower()
        full_alias = safe_name(key.lower())
        model = {"assetId":id, "modelPath":path, "stem":basename,
                 "mesh":object_path(entry["meshAsset"] or ""),
                 "bodyData":object_path(baked_unit(id,"DA")) if entry.get("nativeMainOwner",True) else "",
                 "skeleton":object_path(entry["skeletonAsset"] or entry.get("animationSkeletonAsset") or ""),
                 "roles":inventory.get(id,{}).get("roles",[])}
        models[id] = model
        for alias in (id, path, key, full_alias, basename):
            candidates[alias.lower().replace("\\","/")].add(id)
        if entry.get("cinematic"):
            roots = {actor["root"]: {"assetId":id,"ownerRoot":actor["root"],
                                     "bodyData":object_path(baked_unit(id,"DA",role=actor["root"]))}
                     for actor in entry.get("actors",[])}
            if not roots:
                # Single-root performances share the model's main owner. Exact root names are
                # supplied by the stage's cinematicRoot records when the per-unit body is read.
                roots[""] = {"assetId":id,"ownerRoot":"","bodyData":model["bodyData"]}
            cinematics[id] = {"assetId":id,"modelPath":path,"roots":roots}
    collisions = {alias:sorted(ids) for alias,ids in candidates.items() if len(ids)>1}
    aliases = {alias:next(iter(ids)) for alias,ids in candidates.items() if len(ids)==1}
    preserved = {}
    for alias, id in ESTABLISHED_CAST_ALIASES.items():
        if id in models:
            aliases[alias] = id
            preserved[alias] = id
    for id, model in models.items():
        if model["stem"] in collisions and preserved.get(model["stem"]) != id:
            model["stem"] = safe_name(id.removeprefix("vtmb:model:"))
    return {"schemaVersion":"1.0.0", "assetPath":corpus_path("model","DA","Cast"),
            "models":models,"aliases":aliases,"ambiguousAliases":collisions,
            "cinematics":cinematics,"establishedAliases":preserved}


def stage_cast_data(stage_root, manifest):
    from elysium_pipeline.importers.characters import _write, _json_bytes
    import json

    root=Path(stage_root)
    data=project_cast(manifest)
    # A single-root cinematic accepts its authored bone token too; it still addresses the one
    # main owner and never creates a second animation copy.
    entries={row["assetId"]:row for row in manifest["assets"]}
    for id,cinematic in data["cinematics"].items():
        if "" not in cinematic["roots"]:
            continue
        entry=entries[id]
        body=json.loads((root/entry["body"]).read_text(encoding="utf-8"))
        base=cinematic["roots"].pop("")
        roots=[row["root"] for row in body["cinematicRoots"] if row["root"]]
        for bone_root in roots or [""]:
            cinematic["roots"][bone_root]=base
    encoded=_json_bytes(data)
    _write(root/"cast.json",encoded)
    manifest["castData"]={"file":"cast.json","sha256":hashlib.sha256(encoded).hexdigest(),"assetPath":data["assetPath"]}
    _write(root/"manifest.json",_json_bytes(manifest))
    return manifest
