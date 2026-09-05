"""Fresh-process verification of every saved native expression product."""
import json
from pathlib import Path


def run(manifest_path):
    import unreal
    from pipeline.unreal import _bootstrap  # noqa: F401
    from pipeline.unreal import bake_lib as bl
    from elysium_pipeline.importers.expression_tables import PRODUCER, json_text, verify_expression_stage

    root = Path(manifest_path).resolve().parent
    report = {"producer": PRODUCER, "complete": False, "verified": 0, "failed": [],
              "scope": "fresh-process-native-expression-fields", "renderedAcceptance": False}

    def checkpoint():
        (root / "native_verify_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    checkpoint()
    try:
        manifest, projections, corpus = verify_expression_stage(manifest_path)
        products = [(unreal.ElysiumExpressionData, p) for p in projections]
        products.append((unreal.ElysiumExpressionTables, corpus))
        for kind, data in products:
            try:
                path = data["assetPath"]
                asset = unreal.load_asset(path)
                if not isinstance(asset, kind) or not bl.stored_recipe(path, producer=PRODUCER):
                    raise RuntimeError("native expression asset, class or recipe is absent: " + path)
                error = kind.verify(asset, json_text(data))
                if error:
                    raise RuntimeError(error)
                report["verified"] += 1
            except Exception as exc:
                report["failed"].append({"assetId": data.get("assetId", "expression-corpus"), "reason": str(exc)})
            checkpoint()
        report["coverage"] = manifest["summary"]
        report["complete"] = not report["failed"] and report["verified"] == len(products)
    except Exception as exc:
        report["failed"].append({"assetId": "expression-corpus", "reason": str(exc)})
    checkpoint()
    return report


if __name__ == "__main__":
    import unreal
    from pipeline.unreal.import_expression_tables import argument
    manifest = argument(unreal.SystemLibrary.get_command_line(), "ImportExpressionTables")
    if not manifest:
        raise SystemExit("-ImportExpressionTables=<manifest> is required")
    if not run(manifest)["complete"]:
        raise SystemExit(1)
