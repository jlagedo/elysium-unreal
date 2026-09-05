"""Native global catalogue worker; importing this module has no side effects.

-ImportModelCatalogues=<manifest.json> [-ImportForce=1]
The shared CLI owns the generated-state lease and process lifecycle.
"""
import hashlib
import json
from pathlib import Path
import re


def argument(command_line, name):
    pattern = r'(?:^|\s)"?-' + re.escape(name) + r'=(?:"([^"]*)"|([^\r\n]*?)(?="(?:\s|$)|\s+-|$))'
    match = re.search(pattern, command_line, re.IGNORECASE)
    return (match.group(1) if match.group(1) is not None else match.group(2).strip()) if match else ""


def _backend():
    import unreal
    from pipeline.unreal import _bootstrap  # noqa: F401
    from pipeline.unreal import bake_lib
    return unreal, bake_lib


def tool_fingerprint(unreal, bl):
    from elysium_pipeline.importers.model_catalogues import digest_file, json_bytes
    directory = Path(__file__).resolve().parent
    dll = Path(unreal.Paths.project_dir()) / "Binaries/Win64/UnrealEditor-ElysiumUE.dll"
    files = [Path(__file__), directory / "verify_model_catalogues.py", Path(bl.__file__), dll]
    return hashlib.sha256(json_bytes([(path.name, digest_file(path)) for path in files])).hexdigest()


def native_recipe(bl, entry, tool):
    from elysium_pipeline.importers.model_catalogues import PRODUCER
    return bl.recipe_fingerprint(PRODUCER, entry["assetPath"], {"stage": entry["recipe"], "nativeTool": tool})


def verify_generation(path, expected):
    from elysium_pipeline.importers.model_catalogues import verify_model_catalogue_stage
    current, _ = verify_model_catalogue_stage(path)
    if current != expected:
        raise RuntimeError("catalogue stage generation changed during the job")


def _owner(unreal, bl, asset):
    return str(unreal.EditorAssetLibrary.get_metadata_tag(asset, bl.PRODUCER_TAG))


def _check_target(unreal, bl, kind, path):
    from elysium_pipeline.importers.model_catalogues import PRODUCER, object_path
    asset = unreal.load_asset(path)
    if asset is not None:
        if not isinstance(asset, kind) or _owner(unreal, bl, asset) != PRODUCER:
            raise RuntimeError("catalogue target is foreign, unstamped or wrong class: " + path)
        if asset.get_path_name() != object_path(path):
            raise RuntimeError("catalogue address resolves through a different asset: " + path)
    return asset


def verify_references(unreal, bl, references):
    """Resolve every expected path/class/owner. This is an editor gate, never a tick API."""
    counts = {"references": 0, "hardReferences": 0, "softReferences": 0}
    for index, row in enumerate(references):
        kind = getattr(unreal, row["class"])
        asset = unreal.load_asset(row["path"])
        if asset is None or not isinstance(asset, kind):
            raise RuntimeError("native reference missing or wrong class: " + row["path"])
        if asset.get_path_name() != row["path"]:
            raise RuntimeError("native reference resolves through a different asset: " + row["path"])
        if row["producer"] and _owner(unreal, bl, asset) != row["producer"]:
            raise RuntimeError("native reference has wrong producer: " + row["path"])
        counts["references"] += 1
        counts["hardReferences" if row["hard"] else "softReferences"] += 1
        # Do not keep a whole animation corpus resident just to verify address reachability.
        asset = None
        if (index + 1) % 64 == 0:
            unreal.collect_garbage()
    return counts


def preflight(unreal, bl, entries, projections, references):
    """Reject every ownership/class/reference/native-field problem before package writes."""
    for entry in entries:
        kind = getattr(unreal, entry["nativeClass"])
        _check_target(unreal, bl, kind, entry["assetPath"])
    counts = verify_references(unreal, bl, references)
    for entry, projection in zip(entries, projections):
        kind = getattr(unreal, entry["nativeClass"])
        candidate = unreal.new_object(kind)
        result, error = kind.apply_json(candidate, json.dumps(projection, ensure_ascii=False, allow_nan=False))
        if result is None or error:
            raise RuntimeError("catalogue native preflight failed: " + entry["assetPath"] + ": " + error)
    return counts


def publish(unreal, bl, entry, projection, digest, force):
    from elysium_pipeline.importers.model_catalogues import PRODUCER
    path = entry["assetPath"]
    kind = getattr(unreal, entry["nativeClass"])
    encoded = json.dumps(projection, ensure_ascii=False, allow_nan=False)
    asset = _check_target(unreal, bl, kind, path)
    if asset is not None and not force and bl.stored_recipe(path, producer=PRODUCER) == digest:
        error = kind.verify(asset, encoded)
        if not error:
            return "reused"
        unreal.log_warning("[model-catalogues] repairing altered native fields: " + path + ": " + error)
    if asset is None:
        directory, name = path.rsplit("/", 1)
        bl.ensure_dir(directory)
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", kind)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, kind, factory)
    if asset is None:
        raise RuntimeError("catalogue asset creation failed: " + path)
    result, error = kind.apply_json(asset, encoded)
    if result is None or error:
        raise RuntimeError("catalogue authoring failed: " + path + ": " + error)
    bl.stamp_recipe(result, digest, producer=PRODUCER)
    if not bl.save(path):
        raise RuntimeError("catalogue save failed: " + path)
    loaded = unreal.load_asset(path)
    error = "asset missing after save" if loaded is None else kind.verify(loaded, encoded)
    if error:
        raise RuntimeError("catalogue post-save field verification failed: " + path + ": " + error)
    return "imported"


def run(manifest_path, force=False):
    unreal, bl = _backend()
    from elysium_pipeline.importers.model_catalogues import (
        PRODUCER, PACKAGE_ROOT, verify_model_catalogue_stage,
    )
    path = Path(manifest_path).resolve()
    report = {"producer": PRODUCER, "complete": False, "imported": 0, "reused": 0, "pruned": 0,
              "pruneDeferred": True, "foreign": 0, "unstamped": 0, "failed": [], "assets": [],
              "freshProcessVerificationRequired": True}

    def checkpoint():
        (path.parent / "model_catalogues_import_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    checkpoint()
    try:
        manifest, projections = verify_model_catalogue_stage(path)
        entries = manifest["assets"]
        tool = tool_fingerprint(unreal, bl)
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
            ["/ElysiumBaked/Models", "/ElysiumBaked/Materials", "/Game/ElysiumGenerated/Materials/V2"], force_rescan=True)
        report.update(preflight(unreal, bl, entries, projections, manifest["references"]))
        # Preflight may load many assets. Recheck source/stage freshness before the first write.
        verify_generation(path, manifest)
        for entry, projection in zip(entries, projections):
            state = publish(unreal, bl, entry, projection, native_recipe(bl, entry, tool), force)
            report[state] += 1
            report["assets"].append(entry["assetPath"])
            checkpoint()
        verify_generation(path, manifest)
        # Root is shared with CastData and other catalogues; producer tags are mandatory.
        counts = {}
        report["pruned"] = bl.prune_owned(PACKAGE_ROOT, set(manifest["keep"]), manifest["pruneScope"], PRODUCER, counts)
        report.update(counts)
        report["pruneDeferred"] = False
        report["complete"] = True
        report["inputDigest"] = manifest["inputDigest"]
        report["coverage"] = manifest["summary"]
    except Exception as exc:
        report["failed"].append({"reason": str(exc)})
        unreal.log_error("[model-catalogues] " + str(exc))
    checkpoint()
    return report


if __name__ == "__main__":
    import unreal
    command = unreal.SystemLibrary.get_command_line()
    manifest = argument(command, "ImportModelCatalogues")
    if not manifest:
        raise SystemExit("-ImportModelCatalogues=<manifest.json> is required")
    if not run(manifest, argument(command, "ImportForce").casefold() in ("1", "true", "yes", "on"))["complete"]:
        raise SystemExit(1)
