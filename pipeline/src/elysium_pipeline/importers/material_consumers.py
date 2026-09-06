"""Material usage from published model bindings and drawn map faces."""
from elysium_pipeline.formats.unit_contract.container import read_document
from elysium_pipeline.model_usage import skeletal_candidate, discover_placed_animation_models


def collect(root):
    model_paths = sorted((root / "models").rglob("*.glb"))
    published = {"vtmb:model:" + path.relative_to(root / "models").with_suffix("").as_posix() for path in model_paths}
    animated_placed = discover_placed_animation_models(root, published)
    usage = {}
    def add(material, kind, owner):
        if isinstance(material, str) and material.startswith("vtmb:material:"):
            usage.setdefault(material, {"skeletal": set(), "static": set(), "map": set()})[kind].add(owner)
    for path in model_paths:
        unit = read_document(path)["extensions"]["ELYSIUM_vtmb_model"]
        identity = unit["identity"]
        kinds = []
        if identity["shape"] != "skeletal":
            kinds.append("static")
        if (identity["asset"] in animated_placed
                or skeletal_candidate(identity, len(unit["mdl"]["bones"]), has_cloth=bool((unit.get("cloth") or {}).get("garments")))):
            kinds.append("skeletal")
        bindings = unit["materialBindings"]
        for slot in bindings["slots"]:
            for kind in kinds:
                add(slot["material"], kind, identity["asset"])
        for family in bindings["skinFamilies"]:
            for material in family:
                for kind in kinds:
                    add(material, kind, identity["asset"])
    for path in sorted((root / "maps").glob("*.glb")):
        document = read_document(path)
        unit = document.get("extensions", {}).get("ELYSIUM_vtmb_map")
        if unit is None:
            continue
        for face in unit["faces"]:
            if face["tool"] or face["noDraw"]:
                continue
            if not 0 <= face["texInfo"] < len(unit["texinfos"]):
                raise ValueError(f"{unit['identity']['asset']}: drawn face has invalid texinfo")
            info = unit["texinfos"][face["texInfo"]]
            if not 0 <= info["texData"] < len(unit["textures"]):
                raise ValueError(f"{unit['identity']['asset']}: drawn face has invalid texdata")
            texture = unit["textures"][info["texData"]]
            add(texture["asset"], "map", unit["identity"]["asset"])
    return {key: {kind: sorted(owners) for kind, owners in kinds.items()} for key, kinds in usage.items()}


def route(entries, provenance, usage, master_root, exposed):
    """Route lit instances and patch parents; retain a world twin where both consume them."""
    from copy import deepcopy
    from elysium_pipeline.asset_paths import baked_unit, assert_unique_paths
    canonical = {e["assetPath"]: e for e in entries if e["assetPath"] == baked_unit(e["unit"], "MI")}
    flags = {}
    for path, entry in canonical.items():
        consumers = usage.get(entry["unit"], {})
        flags[path] = [bool(consumers.get("skeletal")), bool(consumers.get("static") or consumers.get("map"))]
    for path in canonical:
        current, seen = path, set()
        while canonical[current].get("patched") and canonical[current]["parent"] in canonical:
            parent = canonical[current]["parent"]
            if parent in seen:
                raise ValueError(f"material patch cycle at {parent}")
            seen.add(parent)
            flags[parent] = [a or b for a, b in zip(flags[parent], flags[path])]
            current = parent
    lit = {master_root + "/M_V2_Lit": "M_V2_LitSkinned",
           master_root + "/M_V2_LitTranslucent": "M_V2_LitSkinnedTranslucent"}
    destinations = {}
    for path in canonical:
        if flags[path][0]:
            destinations[path] = (path + "_Skinned" if flags[path][1] else path) if provenance[path].get("master") in lit else path
    additions = []
    for path, original in canonical.items():
        p = provenance[path]
        p["consumers"] = usage.get(original["unit"], {"skeletal": [], "static": [], "map": []})
        p["skinnedAsset"] = destinations.get(path)
        original["skinnedAsset"] = destinations.get(path)
        source_master = p.get("master")
        if not flags[path][0] or source_master not in lit:
            continue
        twin = destinations[path] != path
        target = deepcopy(original) if twin else original
        target["assetPath"] = destinations[path]
        target["decalAsset"] = target["undersideAsset"] = None
        target["resolvedMaster"] = master_root + "/" + lit[source_master]
        target["parent"] = (destinations.get(original["parent"], original["parent"])
                            if original["patched"] else target["resolvedMaster"])
        overrides = target["basePropertyOverrides"]
        if not target["patched"] or "blendMode" in overrides:
            if overrides.get("blendMode", "Opaque") in ("Opaque", "Masked"):
                overrides["blendMode"] = "Masked"
                if overrides.get("opacityMaskClipValue") is None:
                    overrides["opacityMaskClipValue"] = .333
        alpha_test = target.get("sourceAlphaTest")
        if not target["patched"]:
            target["allSwitches"] = sorted(k for k, v in exposed[lit[source_master]].items() if v == "#")
            target["switches"]["UseAlphaTest"] = bool(alpha_test)
            target["scalars"].setdefault("ModelAlpha", 1.)
        elif alpha_test is not None:
            target["switches"]["UseAlphaTest"] = alpha_test
        target["recipe"]["parent"] = target["parent"]
        target["recipe"]["resolvedMaster"] = target["resolvedMaster"]
        target["recipe"]["params"] = {k: deepcopy(target[k]) for k in (
            "textures", "scalars", "vectors", "switches", "allSwitches", "basePropertyOverrides")}
        p["skinnedMaster"] = target["resolvedMaster"]
        if twin:
            additions.append(target)
        else:
            p["master"] = target["resolvedMaster"]
            p["blendMode"] = target["basePropertyOverrides"].get("blendMode")
    routed = entries + additions
    assert_unique_paths((e["unit"], e["assetPath"]) for e in routed)
    return routed, {"skinnedUnits": len(destinations), "skinnedTwins": len(additions)}
