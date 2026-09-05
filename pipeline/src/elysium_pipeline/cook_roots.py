"""R8 packaging-only roots, downstream of producer publication.

No producer imports, install reads, UE launch, configuration edits, or deletion.
R8 physics scope is data conservation only; no simulation product is predicted.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re

from elysium_pipeline.asset_paths import baked_unit, corpus_path

PRODUCER = "r8-cook-roots"
PRIMARY_TYPE = "ElysiumR8CookRoot"
ROOT_PACKAGE = corpus_path("model", "DA", "R8CookRoot")
SCOPES = ("/ElysiumBaked/Models", "/ElysiumBaked/ExpressionTables")
GLOBALS = {
    corpus_path("model", "DA", "Cast"): "ElysiumCastData",
    corpus_path("expression-table", "DA", "ExpressionTables"): "ElysiumExpressionTables",
    corpus_path("model", "DA", "WieldModels"): "ElysiumWieldCatalogue",
    corpus_path("model", "DA", "PlacedModels"): "ElysiumPlacedModelCatalogue",
    corpus_path("model", "DA", "PropSkins"): "ElysiumPropSkinCatalogue",
}
OWNERS = {"characters": "characters", "models": "models", "expressions": "expression-tables", "catalogues": "model-catalogues"}
PREFIX = re.compile(r"^(?:SM|SK|SKEL|A|BS|CLOTH|PHYS|DYN|DA|MI)_.+")
PACKAGE = re.compile(r"^/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+$")


class CookRootError(ValueError):
    pass


def json_text(value):
    return json.dumps(value, ensure_ascii=True, allow_nan=False, sort_keys=True, separators=(",", ":"))


def digest(value):
    return hashlib.sha256(json_text(value).encode()).hexdigest()


def package_path(value):
    """Strict package/object distinction; object suffix must match the package name."""
    if not isinstance(value, str):
        raise CookRootError("asset path is not a string")
    package, dot, object_name = value.partition(".")
    if not PACKAGE.fullmatch(package) or (dot and object_name != package.rsplit("/", 1)[-1]):
        raise CookRootError("noncanonical package/object path: " + value)
    return package


def object_path(package):
    package = package_path(package)
    return package + "." + package.rsplit("/", 1)[-1]


def in_scope(package):
    return any(package.startswith(root + "/") for root in SCOPES)


def _declared_paths(value):
    """Read published address declarations, including merged entries outside the last slice.

    Directory roots/prune patterns and foreign asset families are not target packages.
    Future native model product fields automatically remain in this address inventory.
    """
    if isinstance(value, dict):
        for key, item in value.items():
            yield from _declared_paths(key)
            yield from _declared_paths(item)
    elif isinstance(value, list):
        for item in value:
            yield from _declared_paths(item)
    elif isinstance(value, str) and in_scope(value) and PREFIX.fullmatch(value.rsplit("/", 1)[-1].split(".", 1)[0]):
        yield package_path(value)


def read_declarations(manifest_paths):
    """Bounded metadata-only reads. Catalogues are consumed, never rebuilt/imported here."""
    required = {"characters", "models", "expressions"}
    if not required <= set(manifest_paths) or set(manifest_paths) - set(OWNERS):
        raise CookRootError("supply characters/models/expressions and optionally catalogues manifests")
    expected = set(GLOBALS)
    ledger, source_evidence = [], {}
    for kind, name in sorted(manifest_paths.items()):
        path = Path(name).resolve()
        before = path.stat()
        if before.st_size > 64*1024*1024:
            raise CookRootError("producer manifest exceeds 64 MiB metadata bound: " + str(path))
        raw = path.read_bytes()
        after = path.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            raise CookRootError("producer manifest changed during read: " + str(path))
        document = json.loads(raw)
        if document.get("producer") != OWNERS[kind] or document.get("schemaVersion") != "1.0.0":
            raise CookRootError("unexpected producer/schema: " + str(path))
        fields = {"characters": ("assets", "inventory", "selectedUnits", "stageFailures"),
                  "models": ("assets", "skipped", "stageFailures"),
                  "expressions": ("assets", "summary", "complete", "stageFailures"),
                  "catalogues": ("assets", "references", "expectedInventories", "summary", "complete")}[kind]
        if any(key not in document for key in fields):
            raise CookRootError("missing producer inventory/absence fields: " + str(path))
        if not isinstance(document["assets"], list):
            raise CookRootError("producer assets must be an explicit list")
        if kind in ("expressions", "catalogues") and document["complete"] is not True:
            raise CookRootError("producer has not completed its metadata stage")
        if document.get("stageFailures") or document.get("complete") is False:
            raise CookRootError("producer has incomplete/failed staging: " + str(path))
        expected.update(_declared_paths(document.get("assets", [])))
        for key in ("keep", "castData", "corpus", "references"):
            expected.update(_declared_paths(document.get(key)))
        # Preserve source-only/absence decisions verbatim. 'selected' can describe
        # only the last incremental slice; it NEVER narrows the root target set.
        source_evidence[kind] = {key: deepcopy(document[key]) for key in (
            "inventory", "skipped", "omissions", "summary", "expectedInventories", "inventories", "sourceGaps",
            "selectedUnits", "selection", "stageFailures", "complete") if key in document}
        ledger.append({"kind": kind, "path": str(path), "sha256": hashlib.sha256(raw).hexdigest()})
    if ROOT_PACKAGE in expected:
        raise CookRootError("a producer references the downstream packaging root")
    return {"expectedPackages": sorted(expected), "inputs": ledger,
            "sourceDecisions": source_evidence,
            "catalogueManifestSupplied": "catalogues" in manifest_paths}


def verify_inputs(declarations):
    for row in declarations["inputs"]:
        if hashlib.sha256(Path(row["path"]).read_bytes()).hexdigest() != row["sha256"]:
            raise CookRootError("stale cook-root producer input: " + row["path"])


def plan_roots(declarations, published_assets, *, metadata_verified=True):
    """Every on-disk product in the two canonical scopes is explicitly rooted.

    Metadata rows are Asset Registry snapshots, not loaded UObjects. Extra published
    products remain visible/rooted; missing declarations, redirectors or unstamped
    packages stop publication. Source-only decisions are evidence, not fake assets.
    """
    targets, issues = {}, []
    for row in published_assets:
        package = package_path(row["packagePath"])
        if not in_scope(package):
            raise CookRootError("registry snapshot escaped canonical scopes: " + package)
        if package == ROOT_PACKAGE:
            continue  # Administrative root must never manage itself.
        if row["objectPath"] != object_path(package):
            raise CookRootError("noncanonical top-level object: " + row["objectPath"])
        if package in targets:
            raise CookRootError("duplicate/multiple top-level exports in package: " + package)
        if not PREFIX.fullmatch(package.rsplit("/", 1)[-1]):
            issues.append({"package": package, "reason": "nonstandard native product name"})
        if metadata_verified:
            if row.get("className") == "ObjectRedirector":
                issues.append({"package": package, "reason": "redirector is not a published native product"})
            if not row.get("producer") or not row.get("recipe") or not row.get("className"):
                issues.append({"package": package, "reason": "missing publication class/producer/recipe"})
            if package in GLOBALS and row.get("className") != GLOBALS[package]:
                issues.append({"package": package, "reason": "wrong global data class", "expected": GLOBALS[package]})
        targets[package] = deepcopy(row)
    expected = set(declarations["expectedPackages"])
    # PHYS_<unit> simulation assets are deferred outside R8. Published ones are
    # still rooted; an unbuilt prediction must not gate data-conservation closure.
    deferred = {p for p in expected-targets.keys() if p.rsplit("/", 1)[-1].startswith("PHYS_")}
    missing = sorted(expected - targets.keys() - deferred)
    issues.extend({"package": path, "reason": "declared native package is not published"} for path in missing)
    if not metadata_verified:
        issues.append({"reason": "disk paths only; Asset Registry publication metadata still needs verification"})
    if not declarations["catalogueManifestSupplied"]:
        issues.append({"reason": "model-catalogue publication manifest must be supplied by its owner"})
    ordered = [targets[key] for key in sorted(targets)]
    return {"schemaVersion": "1.0.0", "producer": PRODUCER, "primaryAssetType": PRIMARY_TYPE,
            "assetPath": ROOT_PACKAGE, "physicsScope": "export-import-data-conservation",
            "targets": ordered, "sourceEvidenceJson": json_text(declarations),
            "inputDigest": digest(declarations), "inventoryDigest": digest(ordered),
            "readyToPublish": not issues, "issues": issues,
            "missingPackages": missing, "extraPublishedPackages": sorted(targets.keys()-expected),
            "deferredUnpublishedPhysicsPackages": sorted(deferred),
            "counts": {"globals": sum(key in targets for key in GLOBALS), "targets": len(targets),
                       "modelProducts": sum(key.startswith(SCOPES[0]+"/") for key in targets),
                       "expressionProducts": sum(key.startswith(SCOPES[1]+"/") for key in targets)}}


def scan_package_files(baked_content_root):
    """Read-only offline visibility check; class/producer validation needs the editor registry."""
    root = Path(baked_content_root).resolve()
    rows = []
    for folder in ("Models", "ExpressionTables"):
        for file in sorted((root / folder).rglob("*.uasset")):
            if not file.resolve().is_relative_to(root):
                raise CookRootError("published package path escapes supplied content root")
            package = "/ElysiumBaked/" + file.relative_to(root).with_suffix("").as_posix()
            rows.append({"packagePath": package, "objectPath": object_path(package)})
    return rows


def cook_map_packages(map_ids):
    """Explicit level packages, independent of map bundle directories; never CookAll."""
    ids = list(map_ids)
    if not ids or any(not value.startswith("vtmb:map:") for value in ids):
        raise CookRootError("explicit vtmb:map: identities are required")
    return sorted({"/Game/ElysiumGenerated/Boot", *(baked_unit(value, "") for value in ids)})


def verify_cooked_packages(plan, cooked_packages):
    """Post-cook/package-list gate. A good editor label is not cooked-output proof."""
    expected = {row["packagePath"] for row in plan["targets"]}
    # Other engine/plugin families have their own naming contracts; this gate
    # audits only the two R8 scopes, not arbitrary Engine/font/map package names.
    actual = {package_path(value) for value in cooked_packages if isinstance(value, str) and in_scope(value)}
    return {"complete": plan["readyToPublish"] and expected <= actual,
            "missing": sorted(expected-actual), "expected": len(expected),
            "note": "checks R8 roots/products only; map/dependency/global package acceptance remains with cook owner"}


def asset_manager_setting():
    """Exact proposed addition for main; no configuration mutation."""
    return ('+PrimaryAssetTypesToScan=(PrimaryAssetType="' + PRIMARY_TYPE
        + '",AssetBaseClass="/Script/ElysiumUE.ElysiumCookRoot",bHasBlueprintClasses=False,'
          'bIsEditorOnly=False,Directories=(),SpecificAssets=("' + object_path(ROOT_PACKAGE)
        + '"),Rules=(Priority=1,ChunkId=-1,bApplyRecursively=True,CookRule=AlwaysCook))')
