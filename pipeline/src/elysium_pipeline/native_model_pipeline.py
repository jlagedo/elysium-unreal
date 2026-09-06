"""Native model dependencies shared by map/profile workflows and focused import commands."""
from __future__ import annotations

import json
from pathlib import Path


def _read_report(path):
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() else None


def require_map_prerequisites(config):
    """Fail before map/profile mutation when its separately published inputs are absent.

    A profile export (clean or not) reads the V2 GLB corpus and the staged static model and
    material lanes; it never publishes them, so their absence is an operator step, not a
    reason to fall back to a retired transport.
    """
    from elysium_pipeline.importers import models, materials

    root = getattr(config, "export_v2_root", None)
    work = getattr(config, "work_root", None)
    if root is None or work is None:
        raise RuntimeError("native map dependencies require configured export_v2 and work roots")
    root = Path(root)
    missing = [str(root / family) for family in ("models", "vdata/items", "expression-tables")
               if next((root / family).rglob("*.glb"), None) is None]
    missing.extend(str(module.staging_root(work) / "manifest.json") for module in (models, materials)
                   if not (module.staging_root(work) / "manifest.json").is_file())
    if missing:
        raise RuntimeError("native map prerequisites are absent: " + ", ".join(missing) +
            "; publish the V2 corpus and run `elysium import materials` and "
            "`elysium import models --all` first. Profile exports reuse that corpus and mount.")


def import_expression_tables(config, runner, *, stage_only=False, force=False, log=print):
    from elysium_pipeline import unreal
    from elysium_pipeline.importers import expression_tables

    root = expression_tables.staging_root(config.work_root)
    manifest = expression_tables.stage_expression_tables(config.export_v2_root, root, log=log)
    if not manifest.get("complete") or manifest["stageFailures"]:
        for failure in manifest["stageFailures"]:
            log(f"{failure['assetId']}: {failure['reason']}")
        raise RuntimeError("expression table stage failed")
    if stage_only:
        return manifest
    receipt = root / "import_report.json"
    receipt.unlink(missing_ok=True)
    unreal.expression_tables(config, runner, root / "manifest.json", force=force)
    report = _read_report(receipt)
    expected = len(manifest["keep"])
    if (not report or not report.get("complete") or report.get("failed")
            or report.get("imported", 0) + report.get("reused", 0) != expected):
        raise RuntimeError("expression table import failed or did not account for every product")
    log(f"expression tables: {report['imported']} imported, {report['reused']} reused")
    return manifest


def import_model_catalogues(config, runner, *, stage_only=False, force=False, log=print):
    from elysium_pipeline import unreal
    from elysium_pipeline.importers import model_catalogues, characters, models, materials

    root = model_catalogues.staging_root(config.work_root)
    manifest = model_catalogues.stage_model_catalogues(config.export_v2_root, root,
        characters_root=characters.staging_root(config.work_root),
        models_root=models.staging_root(config.work_root),
        materials_root=materials.staging_root(config.work_root))
    if stage_only:
        log(f"model catalogues staged: {root / 'manifest.json'}")
        return manifest
    receipt = root / "model_catalogues_import_report.json"
    receipt.unlink(missing_ok=True)
    unreal.model_catalogues(config, runner, root / "manifest.json", force=force)
    report = _read_report(receipt)
    if not report or not report.get("complete") or report.get("failed"):
        raise RuntimeError("native model catalogue import failed; see model_catalogues_import_report.json")
    log("merged wield, placed-model and skin catalogues imported")
    return manifest


def import_map_dependencies(config, runner, *, bodies=None, force=False, log=print):
    """One ordered import; existing V2 static models/materials are prerequisites."""
    from elysium_pipeline import character_pipeline

    require_map_prerequisites(config)
    import_expression_tables(config, runner, force=force, log=log)
    manifest = character_pipeline.import_characters(config, runner, bodies=bodies, force=force, log=log)
    import_model_catalogues(config, runner, force=force, log=log)
    return manifest
