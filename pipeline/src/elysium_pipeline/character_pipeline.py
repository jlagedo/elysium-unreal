"""Shared GLB character staging/import workflow for CLI and map/profile dependencies."""
from __future__ import annotations

import json
from pathlib import Path


def _read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8")) if Path(path).is_file() else None


def import_characters(config, runner, *, bodies=None, stage_only=False, force=False, log=print):
    from elysium_pipeline.importers import characters
    from elysium_pipeline.importers import character_data, clip_data, body_data, cast_data, dynamics_data, cloth_data, materials
    from elysium_pipeline import unreal

    root = characters.staging_root(config.work_root)
    manifest = characters.stage_characters(
        config.export_v2_root, root, bodies=bodies,
        content_root=config.repo_root / "Plugins/ElysiumBaked/Content", log=log)
    material_manifest = materials.staging_root(config.work_root) / "manifest.json"
    if material_manifest.is_file():
        manifest = character_data.stage_mesh_data(config.export_v2_root, root, manifest, material_manifest, log=log)
    elif not stage_only:
        raise RuntimeError("stage the material corpus before native character import")
    manifest = clip_data.stage_clip_data(config.export_v2_root, root, manifest, log=log)
    manifest = dynamics_data.stage_dynamics_data(root, manifest, log=log)
    manifest = cloth_data.stage_cloth_data(root, manifest, log=log)
    manifest = body_data.stage_body_data(config.export_v2_root, root, manifest, log=log)
    manifest = cast_data.stage_cast_data(root, manifest)
    failures = manifest["stageFailures"]
    log(f"characters: {len(manifest['assets'])} staged, {len(failures)} failed; {root / 'manifest.json'}")
    for row in failures[:10]:
        log(f"{row['assetId']}: {row['reason']}")
    if failures:
        raise RuntimeError("character stage failed; previous baked assets remain in use")
    if stage_only:
        return manifest
    (root / "import_report.json").unlink(missing_ok=True)
    editor_failure = None
    try:
        unreal.import_characters(config, runner, root / "manifest.json", force=force)
    except unreal.UnrealFailure as error:
        editor_failure = error
    report = _read_json(root / "import_report.json")
    failed = report.get("failed", []) if report else []
    if report:
        log(f"character import: {report.get('imported', 0)} imported, "
                      f"{report.get('reused', 0)} reused, {len(failed)} failed")
        for row in failed[:10]:
            log(f"{row['assetId']}: {row['reason']}")
        pending = report.get("pendingProjections", {})
        if pending:
            log(f"{len(pending)} units still need data projections; see import_report.json")
    if editor_failure or failed or not report:
        raise RuntimeError(str(editor_failure or "character native import failed or returned no report"))
    return manifest
