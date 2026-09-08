"""Packaging-root writer. Main invokes this after producer publication under its lease.

No work at import time; no CLI, config, catalogue producer, map or runtime edits.
Only the label is loaded. Corpus targets are scanned as Asset Registry metadata
and authored as soft references by ElysiumCookRoot; no eager all-corpus load.
"""
import json
from pathlib import Path
import re


#: The code half of the cook-root recipe; bump when this writer or `UElysiumCookRoot` changes
#: what it authors. Never hash code.
PRODUCER_VERSION = "r8-cook-roots-v1"


def _tag(data, key):
    # FAssetData's ScriptMethod returns the string value (or None when absent).
    value = data.get_tag_value(key)
    if isinstance(value, tuple):
        value = value[-1] if value and value[0] else ""
    return str(value or "")


def snapshot(unreal):
    from elysium_pipeline.cook_roots import SCOPES
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(list(SCOPES), force_rescan=True)
    rows = []
    for scope in SCOPES:
        for data in registry.get_assets_by_path(scope, recursive=True, include_only_on_disk_assets=True):
            package = str(data.package_name)
            rows.append({"packagePath": package, "objectPath": package + "." + str(data.asset_name),
                         "className": str(data.asset_class_path.asset_name),
                         "producer": _tag(data, "ElysiumProducer"), "recipe": _tag(data, "ElysiumRecipe")})
    return sorted(rows, key=lambda row: (row["packagePath"], row["objectPath"]))


def _publish(unreal, bl, plan, recipe, force=False):
    from elysium_pipeline.cook_roots import PRODUCER, ROOT_PACKAGE, json_text, object_path
    if not plan["readyToPublish"] or plan["issues"]:
        raise RuntimeError("cook root has publication issues: " + json_text(plan["issues"]))
    kind = getattr(unreal, "ElysiumCookRoot", None)
    if kind is None:
        raise RuntimeError("ElysiumCookRoot needs the main agent's build before publication")
    asset = unreal.load_asset(ROOT_PACKAGE)
    if asset is not None and (not isinstance(asset, kind)
            or str(unreal.EditorAssetLibrary.get_metadata_tag(asset, bl.PRODUCER_TAG)) != PRODUCER
            or asset.get_path_name() != object_path(ROOT_PACKAGE)):
        raise RuntimeError("cook-root target is foreign, unstamped, wrong class or redirected")
    encoded = json_text(plan)
    if asset is not None and not force and bl.stored_recipe(ROOT_PACKAGE, producer=PRODUCER) == recipe:
        error = kind.verify(asset, encoded)
        if not error:
            return asset, "reused"
        unreal.log_warning("[cook-root] repairing changed label fields: " + error)
    if asset is None:
        folder, name = ROOT_PACKAGE.rsplit("/", 1)
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", kind)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, kind, factory)
    value, error = kind.apply_json(asset, encoded)
    if value is None or error:
        raise RuntimeError("cook-root authoring failed: " + str(error))
    if kind.verify(value, encoded):
        raise RuntimeError("cook-root fields failed verification before save")
    bl.stamp_recipe(value, recipe, producer=PRODUCER)
    if not bl.save(ROOT_PACKAGE):
        raise RuntimeError("cook-root save failed")
    error = kind.verify(value, encoded)
    if error:
        raise RuntimeError("cook-root fields differ after save: " + error)
    return value, "imported"


def publish(manifest_paths, *, force=False):
    """Return a receipt; caller persists it in its own packaging stage/report tree."""
    import unreal
    from pipeline.unreal import bake_lib as bl
    from elysium_pipeline import cook_roots as roots
    declarations = roots.read_declarations(manifest_paths)
    before = snapshot(unreal)
    declarations = roots.reconcile_declarations(declarations, before)
    plan = roots.plan_roots(declarations, before)
    roots.verify_inputs(declarations)
    tool = PRODUCER_VERSION
    recipe = bl.recipe_fingerprint(roots.PRODUCER, roots.ROOT_PACKAGE, {"tool": tool, "plan": plan})
    label, state = _publish(unreal, bl, plan, recipe, force)
    roots.verify_inputs(declarations)
    after = roots.plan_roots(declarations, snapshot(unreal))
    if after != plan:
        raise RuntimeError("producer publication inventory changed during cook-root authoring; regenerate before cook")
    cook_error = unreal.ElysiumCookRoot.verify_cook_rules(roots.json_text(plan))
    return {"outcome": state, "assetPath": roots.ROOT_PACKAGE, "recipe": recipe, "plan": plan,
            "readyToCook": not cook_error, "cookRuleCheck": cook_error,
            "cookedOutputVerificationRequired": True}


def run(job_path):
    """Small main-authored job: {manifestPaths: {...}, force: false}.

    Writes only a packaging report beside that job. The input manifests and
    producer roots are never modified. Main must acquire the generated-state lease.
    """
    path = Path(job_path).resolve()
    job = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(job.get("manifestPaths"), dict) or type(job.get("force", False)) is not bool:
        raise RuntimeError("invalid cook-root job manifestPaths/force")
    for manifest in job["manifestPaths"].values():
        if path.parent.is_relative_to(Path(manifest).resolve().parent):
            raise RuntimeError("cook-root job/report must be outside producer input trees")
    try:
        report = publish(job["manifestPaths"], force=job.get("force", False))
        report["complete"] = report["readyToCook"]
    except Exception as error:
        report = {"complete": False, "error": str(error), "cookedOutputVerificationRequired": True}
    destination = path.parent / "cook_root_import_report.json"
    temporary = destination.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(report, indent=2), encoding="utf-8")
    temporary.replace(destination)
    return report


if __name__ == "__main__":
    import unreal
    command = unreal.SystemLibrary.get_command_line()
    match = re.search(r'(?:^|\s)"?-CookRootJob=(?:"([^"]*)"|([^\r\n]*?)(?="(?:\s|$)|\s+-|$))', command, re.IGNORECASE)
    job_path = (match.group(1) if match and match.group(1) is not None else match.group(2).strip()) if match else ""
    if not job_path:
        raise SystemExit("-CookRootJob=<job.json> is required")
    result = run(job_path)
    if not result["complete"]:
        unreal.log_error("[cook-root] " + result.get("error", result.get("cookRuleCheck", "not ready")))
        raise SystemExit(1)
