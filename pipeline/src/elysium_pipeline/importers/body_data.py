"""Body/owner vocabularies, resolved from the GLB include graph to native asset addresses."""
from elysium_pipeline.asset_paths import baked_unit
from elysium_pipeline.importers.character_data import object_path
from elysium_pipeline.importers.clip_data import descriptor
from elysium_pipeline.skeletal_stage.sequences import blend_clip_plan
from elysium_pipeline.skeletal_stage.unit import ModelUnit
from functools import lru_cache
from pathlib import Path
import hashlib
import json


def first_reference_bases(units, root):
    """Count every include occurrence, retaining each owner's first global sequence base."""
    order, seen = [], set()
    counter = 0

    def visit(id, stack):
        nonlocal counter
        if id in stack:
            return
        if id not in units:
            raise ValueError(f"{root}: missing included unit {id}")
        unit = units[id]
        if id not in seen:
            seen.add(id)
            order.append((id, counter))
        # Raw declarations, including duplicates/invalid descriptors the playback projection omits.
        counter += len(unit.mdl["sequences"])
        for include in unit.mdl["includeModels"]:
            visit(include["asset"], stack | {id})

    visit(root, set())
    return order


def native_reference(owner, label, products, role="", host=""):
    """Resolve once at bake, keeping the existing derived-then-plain address preference."""
    result = {"owner": owner, "ownerRoot": role, "label": label, "host": host,
              "sequence": "", "blendSpace": "", "baseCell": "", "resolvedGridLabel": ""}
    names = [label + "@" + host, label] if host else [label]
    for name in names:
        actual = next((key for key in products.get("clips", {}) if key.casefold() == name.casefold()), None)
        if actual is not None:
            result["sequence"] = object_path(baked_unit(owner, "A", role=role or None, label=actual))
            break
    for name in names:
        actual = next((key for key in products.get("blendSpaces", {}) if key.casefold() == name.casefold()), None)
        if actual is not None:
            result["blendSpace"] = object_path(baked_unit(owner, "BS", role=role or None, label=actual))
            result["resolvedGridLabel"] = actual
            break
    return result


def project_body(units, root, products_for, role=""):
    """Ordered selection rows and layer addresses; no game install or native asset load."""
    order = first_reference_bases(units, root)
    rows, first = [], {}
    grids = {}

    def resolve(owner, label, owner_role, host=""):
        products = products_for(owner, owner_role)
        reference = native_reference(owner, label, products, owner_role, host)
        if reference["blendSpace"]:
            if owner not in grids:
                grids[owner] = blend_clip_plan(units[owner], units[owner].sequences)[1]
            grid = next((value for name,value in grids[owner].items() if name.casefold()==label.casefold()), None)
            if grid is None:
                raise ValueError(f"{owner}: native blend space has no source grid {label}")
            cell = next((cell for cell in grid["cells"] if cell["axis"] == [0,0]), None)
            if cell and cell["clip"]:
                grid_host = reference["resolvedGridLabel"].rsplit("@",1)[1] if "@" in reference["resolvedGridLabel"] else ""
                reference["baseCell"] = native_reference(owner,cell["clip"],products,owner_role,grid_host)["sequence"]
            if not reference["baseCell"]:
                raise ValueError(f"{owner}: native blend space {label} lost its base-cell address")
        return reference
    for owner, base in order:
        unit = units[owner]
        owner_role = role if owner == root else ""
        products = products_for(owner, owner_role)
        positions = {}
        for raw in unit.mdl["sequences"]:
            positions.setdefault(raw["label"].casefold(), raw["index"])
        for clip in unit.sequences:
            meta = descriptor(clip)
            reference = resolve(owner, clip.label, owner_role)
            row = {"label": clip.label, "owner": owner, "ownerRoot": owner_role,
                   "rawIndex": base + positions[clip.label.casefold()],
                   "activity": meta["activity"], "weight": meta["weight"], "flags": meta["flags"],
                   "frames": meta["frames"], "fps": meta["fps"], "fade": meta["fade"],
                   "reachCm": meta["reach_cm"] or 0.,
                   "lowReachCm": meta["low_reach_cm"] if meta["low_reach_cm"] is not None else -1.,
                   "comboMask": meta["combo"]["mask"] if meta["combo"] else -1,
                   "hasCombo": meta["combo"] is not None, "assets": reference,
                   "layers": [], "declaredLayers": list(clip.autolayers)}
            rows.append(row)
            first.setdefault(clip.label.casefold(), row)
    unresolved = []
    for row in rows:
        for label in row["declaredLayers"]:
            label = label.removeprefix("@")
            target = first.get(label.casefold())
            if target is None:
                unresolved.append({"owner": row["owner"], "host": row["label"], "layer": label,
                                   "reason": "source layer has no descriptor in the include vocabulary"})
                row["layers"].append({"label": label, "owner": "", "ownerRoot": "", "host": row["label"],
                                      "sequence": "", "blendSpace": "", "baseCell": ""})
                continue
            reference = resolve(target["owner"], target["label"], target["ownerRoot"], row["label"])
            row["layers"].append(reference)
            if not reference["sequence"] and not reference["blendSpace"]:
                unresolved.append({"owner": row["owner"], "host": row["label"], "layer": label,
                                   "targetOwner": target["owner"], "reason": "declared layer has no native form for this host"})
    own_products = products_for(root, role)
    return {"schemaVersion":"1.0.0", "assetId":root, "ownerRoot":role,
            "assetPath":baked_unit(root, "DA", role=role or None),
            "includeOwners":[{"assetId":owner,"sequenceBase":base} for owner,base in order],
            "nativeSequences":{label:object_path(baked_unit(root,"A",role=role or None,label=label)) for label in own_products.get("clips",{})},
            "nativeBlendSpaces":{label:object_path(baked_unit(root,"BS",role=role or None,label=label)) for label in own_products.get("blendSpaces",{})},
            "sequences":rows, "unresolvedLayers":unresolved}


class UnitCatalogue:
    """Bounded metadata cache; a body table never needs the GLB's geometry/sample buffers."""
    def __init__(self, export_root, entries):
        self.root = Path(export_root)
        self.entries = {entry["assetId"]: entry for entry in entries}

    def __contains__(self, id):
        return id in self.entries

    @lru_cache(maxsize=64)
    def __getitem__(self, id):
        return ModelUnit.metadata(self.root / self.entries[id]["unitGlb"])


def stage_body_data(export_root, stage_root, manifest, log=print):
    from elysium_pipeline.importers.characters import _write, _json_bytes

    root = Path(stage_root)
    units = UnitCatalogue(export_root, manifest["assets"])
    entries = units.entries
    selected = set(manifest["selectedUnits"])

    @lru_cache(maxsize=64)
    def products(id, role):
        entry = entries[id]
        owner = next((actor for actor in entry.get("actors", []) if actor["root"] == role), None) if role else entry
        if not owner or not owner.get("clipData"):
            return {"clips": {}, "blendSpaces": {}}
        data = (root / owner["clipData"]).read_bytes()
        if hashlib.sha256(data).hexdigest() != owner["clipDataSha256"]:
            raise ValueError("clip metadata changed after staging: " + id)
        return json.loads(data)

    count, rows = 0, 0
    for entry in manifest["assets"]:
        if entry["assetId"] not in selected:
            continue
        owners = ([entry] if entry.get("nativeMainOwner", True) else []) + entry.get("actors", [])
        for owner in owners:
            role = owner.get("root", "")
            try:
                data = project_body(units, entry["assetId"], products, role)
                relative = entry["key"] + ("/" + role if role else "") + ".vocabulary.json"
                encoded = _json_bytes(data)
                _write(root / relative, encoded)
                owner["bodyData"] = relative
                owner["bodyDataSha256"] = hashlib.sha256(encoded).hexdigest()
                owner["bodyDataAsset"] = data["assetPath"]
                count += 1
                rows += len(data["sequences"])
            except (KeyError, ValueError, OSError, IndexError) as exc:
                manifest["stageFailures"].append({"assetId":entry["assetId"], "ownerRoot":role, "reason":"body data: " + str(exc)})
    units.__getitem__.cache_clear()
    _write(root / "manifest.json", _json_bytes(manifest))
    log(f"body vocabularies: {count} tables, {rows} sequence rows, {len(manifest['stageFailures'])} stage failures")
    return manifest
