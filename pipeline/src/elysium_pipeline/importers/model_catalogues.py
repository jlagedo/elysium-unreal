"""Whole-corpus R8 catalogue composition and digest-bound editor handoff.

Only GLBs and merged producer manifests are inputs. Heavy GLB/projector imports
are local to build_model_catalogues; editor-side verification needs only stdlib.
No public function launches Unreal or acquires the caller's generated-state lease.
"""
from __future__ import annotations

from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import re

from elysium_pipeline.asset_paths import baked_unit, corpus_path
from elysium_pipeline.importers.catalogue_common import evidence, object_path, require_coverage, unique

PRODUCER = "model-catalogues"
SCHEMA = "1.0.0"
POLICY = "whole-model-catalogues-v1"
PACKAGE_ROOT = "/ElysiumBaked/Models/_Corpus"
KINDS = {"WieldModels": "ElysiumWieldCatalogue", "PlacedModels": "ElysiumPlacedModelCatalogue",
         "PropSkins": "ElysiumPropSkinCatalogue", "OrnamentModels": "ElysiumOrnamentCatalogue"}
SOURCE_PATTERNS = ("models/**/*.glb", "maps/*.glb", "vdata/items/*.glb")
MISSING_MATERIAL = "/Game/ElysiumGenerated/Materials/V2/MI_V2_Missing"
_UNSPECIFIED = object()


class CatalogueStageError(ValueError):
    pass


def staging_root(work_root):
    return Path(work_root) / "import" / "model-catalogues"


def json_bytes(value):
    return (evidence(value) + "\n").encode("utf-8")


def digest_file(path):
    digest = hashlib.sha256()
    before = path.stat()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise CatalogueStageError(f"input changed while being read: {path}")
    return digest.hexdigest()


def bounded_path(root, relative):
    root = Path(root).resolve()
    if not isinstance(relative, str) or not relative or Path(relative).is_absolute():
        raise CatalogueStageError(f"expected a relative input path: {relative!r}")
    path = (root / relative).resolve()
    if not path.is_relative_to(root) or path == root:
        raise CatalogueStageError(f"path escapes its input root: {relative}")
    return path


#: The code half of every catalogue projection: bump when a projection rule changes what it
#: emits. The data half is the input ledger (every read file's digest) and the inventories.
#: Code is never hashed (`docs/architecture/seam_map_unit_contract.md` -> "Recipes").
RULES_VERSION = "model-catalogues-v2-ornaments"


def rules_fingerprint():
    return RULES_VERSION


class Inputs:
    def __init__(self, roots):
        self.roots = {key: Path(value).resolve() for key, value in roots.items()}
        self.files = {}
        self.inventories = {pattern: sorted(p.relative_to(self.roots["exports"]).as_posix()
                             for p in self.roots["exports"].glob(pattern)) for pattern in SOURCE_PATTERNS}

    def file(self, root, relative, expected=_UNSPECIFIED):
        if expected is not _UNSPECIFIED and (not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected)):
            raise CatalogueStageError(f"missing/invalid producer digest: {root}/{relative}")
        path = bounded_path(self.roots[root], relative)
        key = (root, relative)
        if key not in self.files:
            self.files[key] = {"root": root, "path": relative, "sha256": digest_file(path)}
        if expected is not _UNSPECIFIED and self.files[key]["sha256"] != expected:
            raise CatalogueStageError(f"stale {root} input {relative}: digest differs from producer manifest")
        return path

    def json(self, root, relative, expected=_UNSPECIFIED):
        path = self.file(root, relative, expected)
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != self.files[(root, relative)]["sha256"]:
            raise CatalogueStageError(f"input changed while reading JSON: {path}")
        return json.loads(raw)

    def ledger(self):
        return [self.files[key] for key in sorted(self.files)]

    def verify(self):
        _verify_inputs(self.roots, self.ledger(), self.inventories)


def _verify_inputs(roots, ledger, inventories):
    require_coverage(roots, ("exports", "characters", "models", "materials"))
    require_coverage(inventories, SOURCE_PATTERNS)
    if not ledger:
        raise CatalogueStageError("catalogue has no input ledger")
    seen = set()
    for row in ledger:
        key = (row["root"], row["path"])
        if key in seen or row["root"] not in roots:
            raise CatalogueStageError("duplicate/unknown catalogue input")
        seen.add(key)
        if digest_file(bounded_path(roots[row["root"]], row["path"])) != row["sha256"]:
            raise CatalogueStageError(f"stale catalogue input: {row['root']}/{row['path']}")
    for pattern, expected in inventories.items():
        actual = sorted(p.relative_to(Path(roots["exports"])).as_posix() for p in Path(roots["exports"]).glob(pattern))
        if actual != expected:
            raise CatalogueStageError(f"source inventory changed: {pattern}")
        if any(("exports", relative) not in seen for relative in expected):
            raise CatalogueStageError(f"unhashed source inventory: {pattern}")
    if any((key, "manifest.json") not in seen for key in ("characters", "models", "materials")):
        raise CatalogueStageError("catalogue is missing a producer manifest")


def _manifest(inputs, key, package, producer=None, settings=None):
    value = inputs.json(key, "manifest.json")
    if (value.get("schemaVersion") != SCHEMA or value.get("packageRoot") != package
            or producer and value.get("producer") != producer
            or settings and value.get("settingsVersion") != settings):
        raise CatalogueStageError(f"noncanonical or incompatible {key} manifest")
    if "stageFailures" not in value or value["stageFailures"]:
        raise CatalogueStageError(f"{key} manifest is incomplete or has stage failures")
    return value


def _source_document(inputs, relative, extension):
    from elysium_pipeline.formats.unit_contract.container import read_document
    path = inputs.file("exports", relative)
    document = read_document(path)
    unit = document.get("extensions", {}).get(extension)
    if unit is None:
        raise CatalogueStageError(f"{relative}: missing {extension}")
    return document, unit


def build_model_catalogues(export_root, *, characters_root, models_root, materials_root):
    """Return (projections, inputs, expected inventories), without writing anything.

    Static role selection and skeletal/include selection are checked against fresh GLB
    identities. Missing selected products must be explicit geometryless source skips.
    """
    from elysium_pipeline.importers import models as static_rules, materials as material_rules
    from elysium_pipeline.importers import ornament_catalogue, placed_catalogue, prop_skin_catalogue, wield_catalogue
    from elysium_pipeline.model_usage import skeletal_candidate, placed_animation_models
    from elysium_pipeline import ornament_models
    from elysium_pipeline.skeletal_stage import payload
    from elysium_pipeline.skeletal_stage import wield, cinematics
    from elysium_pipeline.skeletal_stage.unit import ModelUnit

    inputs = Inputs({"exports": export_root, "characters": characters_root, "models": models_root, "materials": materials_root})
    chars = _manifest(inputs, "characters", "/ElysiumBaked/Models", "characters")
    static = _manifest(inputs, "models", "/ElysiumBaked/Models", "models", static_rules.SETTINGS_VERSION)
    mats = _manifest(inputs, "materials", "/ElysiumBaked/Materials", settings=material_rules.SETTINGS_VERSION)
    characters = unique(chars["assets"], "assetId")
    statics = unique(static["assets"], "unit")
    material_entries = unique(mats["assets"], "assetPath")
    units, trees, ornament_requests = {}, {}, []
    for relative in inputs.inventories["models/**/*.glb"]:
        document, unit = _source_document(inputs, relative, "ELYSIUM_vtmb_model")
        identity, mdl = unit["identity"], unit["mdl"]
        ornament_requests.extend(ornament_models.collect_requests(mdl["sequences"], identity["asset"]))
        id = identity["asset"]
        if id in units or not id.startswith("vtmb:model:") or relative != "models/" + id[11:] + ".glb":
            raise CatalogueStageError(f"duplicate/noncanonical model source identity: {id}")
        units[id] = {"relative": relative, "identity": identity, "boneCount": len(mdl["bones"]),
                     "sequenceLabels": [row["label"] for row in mdl["sequences"]] if mdl["bones"] else [],
                     "cloth": bool((unit.get("cloth") or {}).get("garments")),
                     "includes": [r["asset"] for r in mdl["includeModels"]],
                     "geometry": bool(unit["vtx"]["lods"] and any(m["primitives"] for m in document.get("meshes", [])))}
        if identity["family"] == "character" and mdl["bones"]:
            trees[id] = {b["name"].lower(): mdl["bones"][b["parent"]]["name"].lower() if b["parent"] >= 0 else "" for b in mdl["bones"]}
    if not units or not inputs.inventories["vdata/items/*.glb"]:
        raise CatalogueStageError("empty model/item corpus cannot publish global catalogues")
    require_coverage((r["assetId"] for r in chars["inventory"]), units)
    entity_sources, map_sources = [], []
    entity_ids, map_ids = [], []
    for relative in inputs.inventories["maps/*.glb"]:
        from elysium_pipeline.formats.unit_contract.container import read_document
        document = read_document(inputs.file("exports", relative))
        extensions = document.get("extensions", {})
        if "ELYSIUM_vtmb_map_entities" in extensions:
            entity_sources.append(relative)
            entity_ids.append(extensions["ELYSIUM_vtmb_map_entities"]["identity"]["asset"].split(":", 2)[2])
        if "ELYSIUM_vtmb_map" in extensions:
            map_sources.append(relative)
            map_ids.append(extensions["ELYSIUM_vtmb_map"]["identity"]["asset"].split(":", 2)[2])
    if not map_sources or not entity_sources:
        raise CatalogueStageError("map and entity units are required for the global placement inventory")
    require_coverage(map_ids, entity_ids)
    for relative in inputs.inventories["vdata/items/*.glb"]:
        inputs.file("exports", relative)
    current_wield = wield.discover(inputs.roots["exports"], units)
    if chars["wieldCatalogue"] != current_wield:
        raise CatalogueStageError("wield item/model catalogue differs from the current GLB item/map join")
    def placement_units(paths, extension):
        for relative in paths:
            _, unit = _source_document(inputs, relative, extension)
            yield unit
    placed = placed_catalogue.collect_placed_references(
        placement_units(entity_sources, "ELYSIUM_vtmb_map_entities"),
        placement_units(map_sources, "ELYSIUM_vtmb_map"), published_ids=units)
    required_characters = {id for id, r in units.items() if skeletal_candidate(r["identity"], r["boneCount"], has_cloth=r["cloth"])}
    required_characters.update(placed_animation_models(placed, lambda id: units[id]["sequenceLabels"]))
    required_characters.update(current_wield["modelIds"])
    required_characters.update(cinematics.discover_sets(inputs.roots["exports"]))
    ornaments = ornament_models.resolve(ornament_requests, published_ids=units)
    required_characters.update(ornaments["modelIds"])
    pending = list(required_characters)
    while pending:
        id = pending.pop()
        if id not in units:
            raise CatalogueStageError(f"missing published included/cinematic model: {id}")
        for target in units[id]["includes"]:
            if target not in required_characters:
                required_characters.add(target)
                pending.append(target)
    if required_characters - characters.keys() or characters.keys() - units.keys():
        raise CatalogueStageError(f"incomplete character manifest: {sorted(required_characters - characters.keys())}")
    required_static = {id for id, r in units.items() if r["identity"]["roles"]}
    skipped = {"vtmb:model:" + r["unit"] for r in static["skipped"]}
    if required_static - statics.keys() - skipped or statics.keys() - units.keys() or skipped - units.keys():
        raise CatalogueStageError("static manifest does not account for the complete referenced model inventory")
    for id in skipped:
        if units[id]["geometry"]:
            raise CatalogueStageError(f"static source skip still has published geometry: {id}")
    for id, entry in statics.items():
        if entry["assetPath"] != baked_unit(id, "SM") or entry["unitGlb"] != units[id]["relative"]:
            raise CatalogueStageError(f"noncanonical static model: {id}")
        if entry["recipe"]["settingsVersion"] != static_rules.SETTINGS_VERSION or entry["recipe"]["unitSha256"] != entry["unitSha256"]:
            raise CatalogueStageError(f"stale static recipe identity: {id}")
        inputs.file("exports", entry["unitGlb"], entry["unitSha256"])
        provenance = inputs.json("models", id[11:] + ".provenance.json", entry["recipe"]["provenanceSha256"])
        if provenance["assetId"] != id or provenance["unitSha256"] != entry["unitSha256"]:
            raise CatalogueStageError(f"static provenance identity differs: {id}")
        for field in ("slots", "skinFamilies", "familyCount"):
            if provenance[field] != entry[field]:
                raise CatalogueStageError(f"static {field} differs from hashed provenance: {id}")
    for id, entry in characters.items():
        if entry["unitGlb"] != units[id]["relative"]:
            raise CatalogueStageError(f"character source identity differs: {id}")
        inputs.file("exports", entry["unitGlb"], entry["recipe"]["unitSha256"])
        inputs.file("characters", entry["payload"], entry["recipe"]["payloadSha256"])
        if entry.get("wieldSkeletonSource"):
            inputs.file("characters", entry["wieldSkeletonSource"], entry["recipe"]["wieldRigSha256"])
        if entry.get("meshAsset"):
            if entry["meshAsset"] != baked_unit(id, "SK") or not entry.get("bodyData"):
                raise CatalogueStageError(f"character mesh or prepared BodyData declaration is absent/noncanonical: {id}")
            inputs.file("characters", entry["meshData"], entry["recipe"]["meshDataSha256"])

    @lru_cache(maxsize=32)
    def body_for_id(id):
        entry = characters[id]
        body = inputs.json("characters", entry["body"], entry["recipe"]["bodySha256"])
        if body["assetId"] != id:
            raise CatalogueStageError(f"character body identity differs: {id}")
        return body

    @lru_cache(maxsize=32)
    def native_body(id):
        entry = characters[id]
        if not entry.get("bodyData") or not entry.get("clipData"):
            raise CatalogueStageError(f"missing BodyData/clip inventory: {id}")
        body = inputs.json("characters", entry["bodyData"], entry["bodyDataSha256"])
        clips = inputs.json("characters", entry["clipData"], entry["clipDataSha256"])
        if (body["assetId"] != id or body["ownerRoot"] or clips["assetId"] != id or clips["ownerRoot"]
                or body["assetPath"] != baked_unit(id, "DA") or entry["bodyDataAsset"] != body["assetPath"]):
            raise CatalogueStageError(f"native body owner/address differs: {id}")
        for field, key, prefix in (("nativeSequences", "clips", "A"), ("nativeBlendSpaces", "blendSpaces", "BS")):
            expected = {label: object_path(baked_unit(id, prefix, label=label)) for label in clips[key]}
            if body[field] != expected:
                raise CatalogueStageError(f"native {field} differs from clip metadata: {id}")
        require_coverage(map(object_path, entry["animationAssets"]), body["nativeSequences"].values())
        return body

    @lru_cache(maxsize=None)
    def material_for(id, kind):
        if id.startswith("vtmb:missing-material:"):
            return MISSING_MATERIAL
        base = baked_unit(id, "MI")
        if base not in material_entries:
            raise CatalogueStageError(f"missing canonical material: {id}")
        entry = material_entries[base]
        if entry["unit"] != id or entry["unitGlb"] != "materials/" + id[14:] + ".glb":
            raise CatalogueStageError(f"material identity/path differs: {id}")
        _, source = _source_document(inputs, entry["unitGlb"], "ELYSIUM_vtmb_material")
        inputs.file("exports", entry["unitGlb"], entry["unitSha256"])
        if source["identity"]["asset"] != id:
            raise CatalogueStageError(f"material source identity differs: {id}")
        provenance = inputs.json("materials", entry["provenance"], entry["recipe"]["provenanceSha256"])
        if provenance["assetId"] != id or provenance["unitSha256"] != entry["unitSha256"]:
            raise CatalogueStageError(f"material provenance identity differs: {id}")
        path = base if kind == "static" else provenance.get("skinnedAsset")
        if (path not in (base, baked_unit(id, "MI", role="Skinned")) or path not in material_entries
                or material_entries[path]["unit"] != id or material_entries[path]["unitSha256"] != entry["unitSha256"]):
            raise CatalogueStageError(f"missing explicit {kind} material product: {id}")
        return path

    wield_projection = wield_catalogue.project_wield_catalogue(chars, lambda e: body_for_id(e["assetId"]), trees)
    for id, row in wield_projection["data"]["models"].items():
        names = native_body(id)["nativeSequences"]
        require_coverage(row["nativeSequences"].values(), names.values())
        # The model's prepared vocabulary retains original labels; package leaf names are folded.
        row["nativeSequences"] = dict(names)
    ornament_ids = set(ornaments["modelIds"])
    ornament_bones = {}
    skins, placements = [], []
    for id, info in sorted(units.items()):
        document, unit = _source_document(inputs, info["relative"], "ELYSIUM_vtmb_model")
        if id in ornament_ids:
            rows, _map, _reparented = payload.unreal_bones(ModelUnit.metadata(
                inputs.roots["exports"] / info["relative"], document).bones)
            ornament_bones[id] = [name for name, _parent, _pos, _quat in rows]
        reps = []
        if id in statics:
            e = statics[id]
            for slot in e["slots"]:
                if object_path(slot["materialAsset"]) != object_path(material_for(slot["materialId"], "static")):
                    raise CatalogueStageError(f"static base material differs from canonical routing: {id}")
            reps.append({"kind": "static", "mesh": e["assetPath"], "slots": [
                {"index": s["index"], "slotName": s["slotName"], "skinReferences": [s["skinReference"]]} for s in e["slots"]]})
        entry = characters.get(id)
        vocabulary = None
        if entry and entry.get("meshAsset"):
            vocabulary = native_body(id)
            if body_for_id(id)["skinFamilies"] != unit["materialBindings"]["skinFamilies"]:
                raise CatalogueStageError(f"stale character skin family data: {id}")
            reps.append({"kind": "skeletal", "mesh": entry["meshAsset"],
                         "slots": prop_skin_catalogue.skeletal_slots(document, entry)})
        skins.append(prop_skin_catalogue.project_skin_model(unit, reps, material_for))
        if id in placed:
            sampled = ModelUnit(inputs.file("exports", info["relative"]))
            # Existing static-source coverage is the testable milestone. Do not make
            # a fresh all-rest fidelity measurement a publication dependency for it.
            proof = None if info["identity"]["shape"] in ("static", "rigid") else placed_catalogue.measure_static_equivalence(sampled)
            placements.append(placed_catalogue.project_placed_model(sampled, staged=entry, native_body=vocabulary,
                static_mesh=statics.get(id, {}).get("assetPath", ""), usage=placed[id],
                proof=proof))
    for id, use in placed.items():
        if id not in units:
            if not use["sourceAbsent"]:
                raise CatalogueStageError(f"placed unit was lost: {id}")
            row = placed_catalogue.absent_model(id, use["modelPath"], "authored reference absent from published source")
            row["placementEvidence"] = evidence(use)
            placements.append(row)
    projections = [wield_projection, placed_catalogue.project_placed_catalogue(placements, expected_ids=placed),
                   prop_skin_catalogue.project_skin_catalogue(skins, expected_ids=units),
                   ornament_catalogue.project_ornament_catalogue(ornaments, characters, ornament_bones.__getitem__)]
    expected = {"modelIds": sorted(units), "placedIds": sorted(placed), "wieldModelIds": sorted(current_wield["modelIds"]),
                "wieldItemIds": sorted(r["assetId"] for r in current_wield["items"]),
                "ornamentPaths": sorted(r["path"] for r in ornaments["models"]),
                "ornamentModelIds": sorted(ornaments["modelIds"]),
                "staticRequiredIds": sorted(required_static), "characterRequiredIds": sorted(required_characters)}
    inputs.verify()
    return projections, inputs, expected


def reference_inventory(projections):
    """Every reflected hard/soft reference, with class and producer expectations."""
    refs = {}

    def add(path, cls, owner, hard=False):
        if not path:
            return
        path = object_path(path)
        row = {"path": path, "class": cls, "producer": owner, "hard": hard}
        if path in refs:
            if any(refs[path][key] != row[key] for key in ("class", "producer")):
                raise CatalogueStageError(f"conflicting native reference types/owners: {path}")
            row["hard"] |= refs[path]["hard"]
        refs[path] = row

    for projection in projections:
        kind, data = projection["catalogueKind"], projection["data"]
        for row in data["models"].values():
            if kind == "WieldModels":
                add(row["mesh"], "SkeletalMesh", "characters")
                add(row["skeleton"], "Skeleton", "characters")
                add(row["referenceClip"], "AnimSequence", "characters")
            elif kind == "OrnamentModels":
                add(row["mesh"], "SkeletalMesh", "characters")
                add(row["skeleton"], "Skeleton", "characters")
            elif kind == "PlacedModels":
                add(row["staticMesh"], "StaticMesh", "models")
                add(row["skeletalMesh"], "SkeletalMesh", "characters")
                add(row["bodyData"], "ElysiumBodyData", "characters")
                for clip in row["clips"]:
                    add(clip["sequence"], "AnimSequence", "characters")
                    add(clip["baseCell"], "AnimSequence", "characters")
                    add(clip["blendSpace"], "BlendSpace", "characters")
            else:
                for rep in row["representations"]:
                    add(rep["staticMesh"], "StaticMesh", "models")
                    add(rep["skeletalMesh"], "SkeletalMesh", "characters")
                    for family in rep["families"]:
                        for cell in family["cells"]:
                            path = cell["material"]
                            add(path, "MaterialInterface", "" if path == object_path(MISSING_MATERIAL) else "materials", True)
            for path in row.get("nativeSequences", {}).values():
                add(path, "AnimSequence", "characters")
            for path in row.get("nativeBlendSpaces", {}).values():
                add(path, "BlendSpace", "characters")
        if kind == "WieldModels":
            for item in data["items"].values():
                for sex in ("female", "male"):
                    add(item[sex]["mesh"], "SkeletalMesh", "characters")
    return [refs[key] for key in sorted(refs)]


def _validate_products(projections, expected):
    by_kind = unique(projections, "catalogueKind")
    require_coverage(by_kind, KINDS)
    for kind, projection in by_kind.items():
        if projection["schemaVersion"] != SCHEMA or projection["assetPath"] != corpus_path("model", "DA", kind):
            raise CatalogueStageError(f"noncanonical catalogue product: {kind}")
    require_coverage(by_kind["PropSkins"]["data"]["models"], expected["modelIds"])
    require_coverage(by_kind["PlacedModels"]["data"]["models"], expected["placedIds"])
    require_coverage(by_kind["WieldModels"]["data"]["models"], expected["wieldModelIds"])
    require_coverage(by_kind["OrnamentModels"]["data"]["models"], expected["ornamentPaths"])
    require_coverage((r["assetId"] for r in by_kind["OrnamentModels"]["data"]["models"].values()
                      if not r["sourceAbsent"]), expected["ornamentModelIds"])
    require_coverage((r["assetId"] for r in by_kind["WieldModels"]["data"]["items"].values()), expected["wieldItemIds"])
    if any(r["acceptanceIssues"] for r in by_kind["PlacedModels"]["data"]["models"].values()):
        raise CatalogueStageError("placed catalogue contains unaccepted native products")


def coverage_summary(projections):
    data = {p["catalogueKind"]: p["data"] for p in projections}
    skins = list(data["PropSkins"]["models"].values())
    placed = list(data["PlacedModels"]["models"].values())
    items = list(data["WieldModels"]["items"].values())
    ornaments = list(data["OrnamentModels"]["models"].values())
    return {"modelUnits": len(skins), "placedModels": len(placed), "wieldItems": len(items),
            "wieldModels": len(data["WieldModels"]["models"]),
            "skinFamilies": sum(r["familyCount"] for r in skins),
            "skinCells": sum(r["familyCount"] * r["skinReferenceCount"] for r in skins),
            "sourceOnlySkinModels": sum(not r["representations"] for r in skins),
            "staticRepresentations": sum(p["kind"] == "static" for r in skins for p in r["representations"]),
            "skeletalRepresentations": sum(p["kind"] == "skeletal" for r in skins for p in r["representations"]),
            "absentWieldReferences": sum(r[sex]["state"] == "Absent" for r in items for sex in ("female", "male")),
            "absentPlacedModels": sum(r["sourceAbsent"] for r in placed),
            "staticRestSuffices": sum(r["staticRestSuffices"] for r in placed),
            "staticSourceRepresentations": sum(r.get("staticSourceRepresentation", False) for r in placed),
            "sourceOnlyPlacedClips": sum(c["state"] == "source-only" for r in placed for c in r["clips"]),
            "absentIntrinsicClips": sum(len(json.loads(r["sourceEvidence"]).get("absentIntrinsicClips", [])) for r in placed),
            "clothPlacedModels": sum(r["hasCloth"] for r in placed),
            "ornamentPaths": len(ornaments),
            "ornamentModels": sum(not r["sourceAbsent"] for r in ornaments),
            "absentOrnamentModels": sum(r["sourceAbsent"] for r in ornaments),
            "ornamentEventRequests": data["OrnamentModels"]["requestCount"]}


def _atomic_write(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(raw)
    os.replace(temporary, path)


def stage_model_catalogues(export_root, stage_root, *, characters_root, models_root, materials_root):
    """Publish immutable projection blobs then the manifest last. No partial replacement."""
    destination = Path(stage_root).resolve()
    for value in (export_root, characters_root, models_root, materials_root):
        other = Path(value).resolve()
        if destination.is_relative_to(other) or other.is_relative_to(destination):
            raise CatalogueStageError("catalogue destination must be disjoint from every input root")
    rules = rules_fingerprint()
    projections, inputs, expected = build_model_catalogues(export_root, characters_root=characters_root,
                                                          models_root=models_root, materials_root=materials_root)
    _validate_products(projections, expected)
    if rules != rules_fingerprint():
        raise CatalogueStageError("catalogue projection rules changed during staging")
    ledger = inputs.ledger()
    identity = hashlib.sha256(json_bytes({"files": ledger, "inventories": inputs.inventories, "expected": expected})).hexdigest()
    manifest = {"schemaVersion": SCHEMA, "producer": PRODUCER, "policy": POLICY, "complete": True,
                "packageRoot": PACKAGE_ROOT, "pruneScope": PACKAGE_ROOT + "/",
                "roots": {key: str(path) for key, path in inputs.roots.items()},
                "inputs": ledger, "inventories": inputs.inventories, "inputDigest": identity,
                "rulesFingerprint": rules, "expectedInventories": expected,
                "references": reference_inventory(projections), "summary": coverage_summary(projections), "assets": []}
    for projection in projections:
        kind = projection["catalogueKind"]
        raw = json_bytes(projection)
        sha = hashlib.sha256(raw).hexdigest()
        relative = f"{kind}.{sha}.json"
        manifest["assets"].append({"catalogueKind": kind, "nativeClass": KINDS[kind], "assetPath": projection["assetPath"],
            "projection": relative, "projectionSha256": sha,
            "recipe": {"policy": POLICY, "inputDigest": identity, "rulesFingerprint": rules, "projectionSha256": sha}})
        _atomic_write(destination / relative, raw)
    manifest["keep"] = sorted(r["assetPath"] for r in manifest["assets"])
    inputs.verify()
    _atomic_write(destination / "manifest.json", json_bytes(manifest))
    return manifest


def verify_model_catalogue_stage(manifest_path):
    """Validate every staged field's digest, expected inventory and current source inputs."""
    path = Path(manifest_path).resolve()
    manifest = json.loads(path.read_bytes())
    if (manifest.get("schemaVersion") != SCHEMA or manifest.get("producer") != PRODUCER
            or manifest.get("policy") != POLICY or manifest.get("complete") is not True
            or manifest.get("packageRoot") != PACKAGE_ROOT or manifest.get("pruneScope") != PACKAGE_ROOT + "/"
            or manifest.get("rulesFingerprint") != rules_fingerprint()):
        raise CatalogueStageError("incompatible, incomplete or stale model catalogue stage")
    identity = hashlib.sha256(json_bytes({"files": manifest["inputs"], "inventories": manifest["inventories"],
                                         "expected": manifest["expectedInventories"]})).hexdigest()
    if identity != manifest["inputDigest"]:
        raise CatalogueStageError("catalogue input identity differs")
    _verify_inputs(manifest["roots"], manifest["inputs"], manifest["inventories"])
    projections = []
    for entry in manifest["assets"]:
        kind = entry["catalogueKind"]
        raw = bounded_path(path.parent, entry["projection"]).read_bytes()
        if hashlib.sha256(raw).hexdigest() != entry["projectionSha256"]:
            raise CatalogueStageError(f"catalogue projection digest differs: {kind}")
        projection = json.loads(raw)
        if (entry["nativeClass"] != KINDS[kind] or projection["catalogueKind"] != kind
                or entry["assetPath"] != projection["assetPath"]
                or entry["recipe"] != {"policy": POLICY, "inputDigest": identity,
                    "rulesFingerprint": manifest["rulesFingerprint"], "projectionSha256": entry["projectionSha256"]}):
            raise CatalogueStageError(f"catalogue recipe/class/identity differs: {kind}")
        projections.append(projection)
    _validate_products(projections, manifest["expectedInventories"])
    require_coverage(manifest["keep"], (r["assetPath"] for r in projections))
    if manifest["references"] != reference_inventory(projections):
        raise CatalogueStageError("catalogue native-reference inventory differs")
    if manifest["summary"] != coverage_summary(projections):
        raise CatalogueStageError("catalogue coverage summary differs")
    return manifest, projections
