"""Fresh-editor-process verification of all catalogue fields and every native reference.

-VerifyModelCatalogues=<manifest.json>. Never authors, saves, repairs or prunes assets.
"""
import json
from pathlib import Path


def run(manifest_path):
    from pipeline.unreal import import_model_catalogues as worker
    unreal, bl = worker._backend()
    from elysium_pipeline.importers.model_catalogues import KINDS, PRODUCER, verify_model_catalogue_stage
    path = Path(manifest_path).resolve()
    report = {"producer": PRODUCER, "complete": False, "verified": 0, "failed": [],
              "scope": "fresh-process-native-catalogue-fields-and-references", "renderedAcceptance": False}

    def checkpoint():
        (path.parent / "model_catalogues_verify_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    checkpoint()
    try:
        manifest, projections = verify_model_catalogue_stage(path)
        tool = worker.tool_fingerprint(unreal, bl)
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
            ["/ElysiumBaked/Models", "/ElysiumBaked/Materials", "/Game/ElysiumGenerated/Materials/V2"], force_rescan=True)
        for entry, projection in zip(manifest["assets"], projections):
            try:
                kind = getattr(unreal, entry["nativeClass"])
                asset = worker._check_target(unreal, bl, kind, entry["assetPath"])
                if asset is None or bl.stored_recipe(entry["assetPath"], producer=PRODUCER) != worker.native_recipe(bl, entry, tool):
                    raise RuntimeError("catalogue native asset/recipe is absent or stale: " + entry["assetPath"])
                error = kind.verify(asset, json.dumps(projection, ensure_ascii=False, allow_nan=False))
                if error:
                    raise RuntimeError("native fields differ: " + error)
                report["verified"] += 1
            except Exception as exc:
                report["failed"].append({"catalogueKind": entry["catalogueKind"], "reason": str(exc)})
            checkpoint()
        report.update(worker.verify_references(unreal, bl, manifest["references"]))
        worker.verify_generation(path, manifest)
        report["inputDigest"] = manifest["inputDigest"]
        report["coverage"] = manifest["summary"]
        report["complete"] = not report["failed"] and report["verified"] == len(KINDS)
    except Exception as exc:
        report["failed"].append({"reason": str(exc)})
    checkpoint()
    return report


if __name__ == "__main__":
    import unreal
    from pipeline.unreal.import_model_catalogues import argument
    manifest = argument(unreal.SystemLibrary.get_command_line(), "VerifyModelCatalogues")
    if not manifest:
        raise SystemExit("-VerifyModelCatalogues=<manifest.json> is required")
    if not run(manifest)["complete"]:
        raise SystemExit(1)
