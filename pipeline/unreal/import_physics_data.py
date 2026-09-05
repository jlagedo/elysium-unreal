"""Library worker for the character import owner; importing this file does no work.

The caller owns the generated-state lease and editor lifetime. Call publish_entry
only after compiling ElysiumPhysicsData. This worker never attaches a reference,
activates physics, prunes packages, or modifies the character stage/manifest.
"""
import hashlib
from pathlib import Path


def _publish(unreal, bl, kind, projection, recipe, force=False):
    from elysium_pipeline.importers.physics_data import PRODUCER, json_text
    path = projection["assetPath"]
    encoded = json_text(projection)
    asset = unreal.load_asset(path)
    if asset is not None:
        if not isinstance(asset, kind) or str(unreal.EditorAssetLibrary.get_metadata_tag(asset, bl.PRODUCER_TAG)) != PRODUCER:
            raise RuntimeError("physics source target is foreign, unstamped or wrong class: " + path)
        if not force and bl.stored_recipe(path, producer=PRODUCER) == recipe:
            error = kind.verify(asset, encoded)
            if not error:
                return asset, "reused"
            unreal.log_warning("[physics-source] repairing altered native source data: " + path + ": " + error)
    if asset is None:
        directory, name = path.rsplit("/", 1)
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", kind)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, kind, factory)
    if asset is None:
        raise RuntimeError("physics source asset creation failed: " + path)
    result, error = kind.apply_json(asset, encoded)
    if result is None or error:
        raise RuntimeError("physics source authoring failed: " + path + ": " + str(error))
    error = kind.verify(result, encoded)
    if error:
        raise RuntimeError("physics source verification failed before save: " + path + ": " + error)
    bl.stamp_recipe(result, recipe, producer=PRODUCER)
    if not bl.save(path):
        raise RuntimeError("physics source save failed: " + path)
    error = kind.verify(result, encoded)
    if error:
        raise RuntimeError("physics source verification failed after save: " + path + ": " + error)
    return result, "imported"


def publish_entry(entry, selected_units, export_root, stage_root, *, force=False):
    """Return (resident native asset, receipt); main attaches the hard reference.

    Re-read hash-checked GLB/body inputs instead of trusting a cached recipe or a
    precomputed projection without its source. Per-entry, standard-library only.
    """
    import unreal
    from pipeline.unreal import bake_lib as bl
    from elysium_pipeline.importers import physics_data

    kind = getattr(unreal, "ElysiumPhysicsData", None)
    if kind is None:
        raise RuntimeError("ElysiumPhysicsData is not compiled in this editor; the main owner must build/restart first")
    projection = physics_data.project_selected_entry(entry, selected_units, export_root, stage_root)
    dll = Path(unreal.Paths.project_dir()) / "Binaries/Win64/UnrealEditor-ElysiumUE.dll"
    tool_hash = hashlib.sha256(Path(__file__).read_bytes() + Path(physics_data.__file__).read_bytes()
                               + Path(bl.__file__).read_bytes() + dll.read_bytes()).hexdigest()
    recipe = bl.recipe_fingerprint(physics_data.PRODUCER, projection["assetPath"],
                                   {"tool": tool_hash, "projection": projection})
    asset, outcome = _publish(unreal, bl, kind, projection, recipe, force)
    for gap in projection["gaps"]:
        unreal.log_warning("[physics-source] retained source gap " + entry["assetId"] + ": " + physics_data.json_text(gap))
    return asset, {"assetId": entry["assetId"], "assetPath": projection["assetPath"],
                   "outcome": outcome, "sourceGaps": len(projection["gaps"]),
                   "hasPhysics": projection["bHasPhysics"], "recipe": recipe}
