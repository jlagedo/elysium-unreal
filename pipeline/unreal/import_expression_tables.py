"""Editor worker for the R8.5 expression lane. Importing this module does no work.

CLI integration: -ImportExpressionTables=<manifest> [-ImportForce=1]. The caller
owns the generated-state lease/process checks. Legacy deletion is never performed.
"""
import json
from pathlib import Path
import re


#: The code half of every expression-table recipe; bump when this writer or the
#: `UElysiumExpressionData`/`UElysiumExpressionTables` classes change what they author.
#: Never hash code (`seam_map_unit_contract.md` -> "Recipes").
PRODUCER_VERSION = "expression-tables-v1"


def argument(command_line, name):
    # Handles both -Key="path with spaces" and "-Key=path with spaces".
    match = re.search(r'(?:^|\s)"?-' + re.escape(name) + r'=(?:"([^"]*)"|([^\r\n]*?)(?="(?:\s|$)|\s+-|$))',
                      command_line, re.IGNORECASE)
    return (match.group(1) if match and match.group(1) is not None else match.group(2).strip()) if match else ""


def _publish(unreal, bl, kind, path, encoded, digest, force, producer):
    """Never trust a recipe alone: verify saved fields on both reuse and replacement."""
    asset = unreal.load_asset(path)
    if asset is not None:
        owner = str(unreal.EditorAssetLibrary.get_metadata_tag(asset, bl.PRODUCER_TAG))
        if owner != producer:
            raise RuntimeError("expression target is foreign or unstamped: " + path)
        if not isinstance(asset, kind):
            raise RuntimeError("expression target has wrong native class: " + path)
    if asset is not None and not force and bl.stored_recipe(path, producer=producer) == digest:
        error = kind.verify(asset, encoded)
        if not error:
            return "reused"
        unreal.log_warning("[import-expression-tables] repairing altered asset: " + path + ": " + error)
    if asset is None:
        directory, name = path.rsplit("/", 1)
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", kind)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, kind, factory)
    result, error = kind.apply_json(asset, encoded)
    if result is None:
        raise RuntimeError("expression authoring failed: " + error)
    bl.stamp_recipe(result, digest, producer=producer)
    if not bl.save(path):
        raise RuntimeError("expression save failed: " + path)
    error = kind.verify(result, encoded)
    if error:
        raise RuntimeError("expression verification failed: " + path + ": " + error)
    return "imported"


def run(manifest_path, force=False):
    import unreal
    from pipeline.unreal import _bootstrap  # noqa: F401
    from pipeline.unreal import bake_lib as bl
    from elysium_pipeline.importers.expression_tables import (
        PRODUCER, json_text, verify_expression_stage,
    )

    root = Path(manifest_path).resolve().parent
    report = {"producer": PRODUCER, "complete": False, "imported": 0, "reused": 0,
              "failed": [], "pruned": 0, "assets": []}
    active_id = "expression-corpus"

    def checkpoint():
        (root / "import_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    checkpoint()
    try:
        manifest, projections, corpus = verify_expression_stage(manifest_path)
        tool_digest = PRODUCER_VERSION
        products = [(unreal.ElysiumExpressionData, p) for p in projections]
        products.append((unreal.ElysiumExpressionTables, corpus))
        # Resolve every class/ownership conflict before the first package mutation.
        for kind, data in products:
            existing = unreal.load_asset(data["assetPath"])
            if existing is not None and (not isinstance(existing, kind)
                    or str(unreal.EditorAssetLibrary.get_metadata_tag(existing, bl.PRODUCER_TAG)) != PRODUCER):
                raise RuntimeError("expression target is foreign, unstamped or wrong class: " + data["assetPath"])
        for kind, data in products:
            active_id = data.get("assetId", "expression-corpus")
            path, encoded = data["assetPath"], json_text(data)
            digest = bl.recipe_fingerprint(PRODUCER, path, {"tool": tool_digest, "projection": data})
            outcome = _publish(unreal, bl, kind, path, encoded, digest, force, PRODUCER)
            report[outcome] += 1
            report["assets"].append(path)
            if data.get("runtimeStatus", "ready") != "ready":
                unreal.log_warning("[import-expression-tables] retained " + path + ": " + data["runtimeStatus"])
            if data.get("counts", {}).get("unresolved"):
                unreal.log_warning("[import-expression-tables] retained unresolved source comparison: " + path)
            checkpoint()
        report["coverage"] = manifest["summary"]
        report["complete"] = True
    except Exception as exc:
        report["failed"].append({"assetId": active_id, "reason": str(exc)})
        unreal.log_error("[import-expression-tables] " + str(exc))
    checkpoint()
    return report


if __name__ == "__main__":
    import unreal
    command_line = unreal.SystemLibrary.get_command_line()
    manifest = argument(command_line, "ImportExpressionTables")
    if not manifest:
        raise SystemExit("-ImportExpressionTables=<manifest> is required")
    result = run(manifest, argument(command_line, "ImportForce") == "1")
    if not result["complete"]:
        raise SystemExit(1)
