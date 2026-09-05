"""D10 skins with explicit source-column and per-representation render-slot spaces.

Every family and every source column survives, even when unchanged or never drawn.
Materials are resolved by the caller from the current material stage; skeletal and
static routes must be supplied separately. No implicit _Skinned guess is permitted.
"""
from elysium_pipeline.importers.catalogue_common import catalogue, evidence, native_ref, object_path, require_coverage, unique


def skeletal_slots(document, entry):
    """Join the actual skeletal builder's material names to LOD0 primitive skinrefs.

The builder can fold multiple source columns into one named section. Such a fold
is accepted only if every family maps those columns identically (checked below).
"""
    unit = document["extensions"]["ELYSIUM_vtmb_model"]
    lod = next(r for r in unit["vtx"]["lods"] if r["index"] == 0)
    columns = {}
    for primitive in document["meshes"][lod["mesh"]]["primitives"]:
        ref = primitive["extensions"]["ELYSIUM_vtmb_model"]["skinReference"]
        name = unit["mdl"]["textures"][ref]["name"]
        columns.setdefault(name, [])
        if ref not in columns[name]:
            columns[name].append(ref)
    if set(columns) != {r["slot"] for r in entry["materials"]}:
        raise ValueError(f"{entry['assetId']}: skeletal render-slot join differs from stage")
    return [{"index": i, "slotName": r["slot"], "skinReferences": columns[r["slot"]]}
            for i, r in enumerate(entry["materials"])]


def project_skin_model(unit, representations, material_for):
    """representations: [{kind, mesh, slots:[{index,slotName,skinReferences}]}].

    material_for(material_id, kind) must return an explicitly staged native path,
    including the named missing-material asset for a source sentinel. The source
    sentinel stays in MaterialId; a missing baked material is never such a sentinel.
    """
    id = unit["identity"]["asset"]
    families = unit["materialBindings"]["skinFamilies"]
    table = unit["mdl"]["skinTable"]
    source_slots = unit["materialBindings"]["slots"]
    if len(families) != len(table):
        raise ValueError(f"{id}: raw and resolved skin family counts differ")
    width = len(families[0]) if families else 0
    if any(len(r) != width for r in families + table):
        raise ValueError(f"{id}: ragged skin index space")
    # Twelve published geometryless units author this exact empty texture reference.
    # Preserve the original index and empty cell as source evidence, without inventing a MI.
    empty_source = not source_slots and table == [[0]] and families == [[""]]
    if empty_source and not representations:
        return {"assetId": id, "familyCount": 1, "skinReferenceCount": 1, "representations": [],
                "sourceOnlyReason": "geometryless source has skin index 0 but no texture slots",
                "sourceEvidence": evidence({"materialBindings": unit["materialBindings"], "skinTable": table})}
    if any(type(i) is not int or not 0 <= i < len(source_slots) for r in table for i in r):
        raise ValueError(f"{id}: skin table refers to an absent source slot")
    for family, indices in zip(families, table):
        if any(material != source_slots[index]["material"] for material, index in zip(family, indices)):
            raise ValueError(f"{id}: resolved skin family disagrees with source texture index space")
    out = []
    seen = set()
    for rep in representations:
        kind = rep["kind"]
        if kind not in ("static", "skeletal") or kind in seen:
            raise ValueError(f"{id}: invalid/duplicate representation {kind}")
        seen.add(kind)
        mesh = native_ref(id, "SK" if kind == "skeletal" else "SM", rep["mesh"])
        slots = rep["slots"]
        unique(slots, "slotName")
        if [s["index"] for s in slots] != list(range(len(slots))):
            raise ValueError(f"{id}: render slot index order differs from stage")
        for slot in slots:
            refs = slot["skinReferences"]
            if not refs or len(set(refs)) != len(refs) or any(type(r) is not int or not 0 <= r < width for r in refs):
                raise ValueError(f"{id}: invalid skinReference for slot {slot['slotName']}")
            for family in families:
                if len({family[r] for r in refs}) != 1:
                    raise ValueError(f"{id}: folded slot {slot['slotName']} loses alternate skin columns {refs}")
        rows = []
        for index, family in enumerate(families):
            cells = []
            for ref, material in enumerate(family):
                if not isinstance(material, str) or not material.startswith(("vtmb:material:", "vtmb:missing-material:")):
                    raise ValueError(f"{id}: invalid material identity in family {index} column {ref}")
                path = material_for(material, kind)
                if not path or not path.startswith(("/ElysiumBaked/Materials/", "/Game/ElysiumGenerated/Materials/")):
                    raise ValueError(f"{id}: no staged {kind} material route for {material}")
                cells.append({"skinReference": ref, "sourceSlot": table[index][ref],
                              "materialId": material, "material": object_path(path),
                              "sourceAbsent": material.startswith("vtmb:missing-material:")})
            rows.append({"index": index, "cells": cells})
        out.append({"kind": kind, "staticMesh": mesh if kind == "static" else "",
                    "skeletalMesh": mesh if kind == "skeletal" else "", "slots": slots, "families": rows})
    return {"assetId": id, "familyCount": len(families), "skinReferenceCount": width,
            "representations": out, "sourceOnlyReason": "" if out else "no declared native mesh representation",
            "sourceEvidence": evidence({
                "materialBindings": unit["materialBindings"], "skinTable": table})}


def project_skin_catalogue(rows, *, expected_ids):
    """Whole-corpus fold only: merge the static AND skeletal producer inputs first."""
    models = unique(rows, "assetId")
    require_coverage(models, expected_ids)
    return catalogue("PropSkins", {"models": models})


def family_materials(model, kind, family):
    """Pure counterpart of native ResolveMaterials, including alternate -> base changes."""
    rep = next(r for r in model["representations"] if r["kind"] == kind)
    if not rep["families"]:
        raise ValueError(f"{model['assetId']}: representation has no skin families")
    index = min(max(0, family), model["familyCount"] - 1)
    cells = rep["families"][index]["cells"]
    return [(s["index"], s["slotName"], cells[s["skinReferences"][0]]["material"]) for s in rep["slots"]]
