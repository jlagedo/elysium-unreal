# SF-4.1 (part 2): authors the two data assets `UElysiumSurfaceSettings::PushToCollection` and
# every V2 master read from -- `MPC_ElysiumSurfaces` and `DA_SurfaceCalibration` -- so both exist
# with real defaults before `make_v2_materials.py` (SF-4.3) builds a master that samples them.
# Design: docs/architecture/seam_map_material.md -> "Import" -> "Knob contract". Mechanics:
# import/design/phase4_mechanics.md section 1.
#
# Registered in `build_content.py` GENERATORS ahead of `make_v2_materials.py`: that generator's
# own `make_surfaces_collection()` only ever *adds* a scalar row missing from `MPC_ElysiumSurfaces`
# and never touches one that already exists (SF-4.1's `PushToCollection` is the values' owner), so
# running this generator first means every row this lane declares lands with the real class
# default rather than whatever placeholder that generator would otherwise seed.
#
# `SCALAR_NAMES` is a pinned mirror of `UElysiumSurfaceSettings::ScalarBindings()` (Source/
# ElysiumUE/Public/ElysiumSurfaceSettings.h) -- pipeline/tests/test_make_surface_knobs.py greps the
# header text and asserts the two lists match, so a knob added to one side and not the other is a
# failing test rather than a silent drift.
#
# Normally rebuilt by the umbrella (uv run elysium export bundle policy ->
# pipeline/unreal/build_content.py, which the export runs); also runnable standalone:
#   UnrealEditor-Cmd.exe ElysiumUE.uproject -run=pythonscript -script="pipeline/unreal/make_surface_knobs.py" -unattended -nosplash -nopause
import unreal

from pipeline.unreal import _bootstrap  # noqa: F401, E402
from elysium_pipeline import mounts

# A mirrored copy of elysium_pipeline.importers.materials.SURFACE_CLASSES, not an import of it:
# materials.py pulls in textures.py, which imports numpy at module scope, and Unreal's embedded
# editor Python (this script's runtime) carries no numpy -- importing materials.py here raises
# ModuleNotFoundError before a single asset is touched. pipeline/tests/test_make_surface_knobs.py
# imports the real SURFACE_CLASSES (outside the editor, where numpy is present) and asserts this
# tuple equals it, so the two lists cannot drift silently.
SURFACE_CLASSES = (
    "default", "armorflesh", "asphalt", "blends", "bone", "bottle", "boulder", "brick",
    "cable", "can_pop", "can_pop_crushed", "canister", "cardboard", "carpet", "cloth", "computer",
    "concrete", "default_silent", "dirt", "drapery", "fish_fresh", "fish_frozen", "flesh", "gargoyle",
    "glass", "glass_shard", "glassbottle", "grass", "grates", "gravel", "grenade", "ground",
    "gunship", "ice", "kitchen_pan", "kitchen_pot", "kitchen_utensils", "ladder", "leather", "metal",
    "metal_barrel", "metalgrate", "metalpanel", "metalvent", "ming_xiao", "ming_xiao_tentacle", "mud", "paper",
    "papercup", "plaster", "plastic", "player", "player_control_clip", "popcan", "quiet", "ring",
    "rivet", "rock", "roller", "rubber", "sand", "snow", "stone", "strider",
    "tile", "tin", "wade", "water", "watermelon", "weapon", "wood", "woodpanel",
)

PKG = mounts.MATERIALS_V2
COLLECTION_NAME = "MPC_ElysiumSurfaces"
CALIBRATION_NAME = "DA_SurfaceCalibration"

tools = unreal.AssetToolsHelpers.get_asset_tools()

#: Name -> class default, mirroring `UElysiumSurfaceSettings`'s declared field defaults exactly
#: (and `Config/DefaultElysium.ini`, which restates the same values for a fresh checkout). Order
#: does not matter to the collection; it is written in declaration order for readability.
SCALAR_NAMES = {
    "DefaultSpecular": 0.5,
    "DefaultRoughness": 0.6,
    "DefaultMetallic": 0.0,
    "LightSpecularScale": 1.0,
    "Overbright": 2.0,
    "MaskRoughnessMin": 0.08,
    "MaskRoughnessMax": 0.9,
    "MaskSpecularScale": 1.0,
    "MaskMetallicMax": 1.0,
    "EnvTintScale": 1.0,
    "FixedCubeStrength": 1.0,
    "ChromaticTintStrength": 1.0,
    "ChromaThreshold": 0.02,
    "DecalDepthOffset": 0.0,
    "CaptureRadius": 1500.0,
}


def _fail(msg):
    unreal.log_error("[make_surface_knobs] %s" % msg)
    raise SystemExit(1)


def make_collection():
    """Create `MPC_ElysiumSurfaces` (idempotent) with every `SCALAR_NAMES` row, at its class
    default. Never overwrites a row that already exists, so a value the owner has since tuned in
    Project Settings and pushed here survives a rerun of this generator."""
    asset = "%s/%s" % (PKG, COLLECTION_NAME)
    collection = unreal.load_asset(asset)
    if not collection:
        collection = tools.create_asset(
            COLLECTION_NAME, PKG, unreal.MaterialParameterCollection,
            unreal.MaterialParameterCollectionFactoryNew())
    if not collection:
        _fail("could not create %s" % asset)

    existing = list(collection.get_editor_property("scalar_parameters"))
    have = {p.get_editor_property("parameter_name") for p in existing}
    missing = [name for name in SCALAR_NAMES if name not in have]
    if missing:
        for name in missing:
            parameter = unreal.CollectionScalarParameter()
            parameter.set_editor_property("parameter_name", name)
            parameter.set_editor_property("default_value", SCALAR_NAMES[name])
            existing.append(parameter)
        collection.set_editor_property("scalar_parameters", existing)
        if not unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False):
            _fail("could not save %s" % asset)
    return collection


def make_calibration():
    """Create `DA_SurfaceCalibration` (idempotent), seed its rows from `SURFACE_CLASSES` (`default`
    first) and bake the LUT. Reseeding on every run keeps the asset's row order in lockstep with
    the pipeline's class table; `SeedDefaultRows`/`RegenerateLut` are pure functions of that list,
    so a rerun over an unchanged `SURFACE_CLASSES` writes back the same bytes."""
    asset = "%s/%s" % (PKG, CALIBRATION_NAME)
    calibration = unreal.load_asset(asset)
    if not calibration:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.ElysiumSurfaceCalibration)
        calibration = tools.create_asset(
            CALIBRATION_NAME, PKG, unreal.ElysiumSurfaceCalibration, factory)
    if not calibration:
        _fail("could not create %s" % asset)

    ok, error = unreal.ElysiumSurfaceCalibration.seed_default_rows(calibration, list(SURFACE_CLASSES))
    if not ok:
        _fail("SeedDefaultRows failed: %s" % error)

    ok, error = calibration.regenerate_lut()
    if not ok:
        _fail("RegenerateLut failed: %s" % error)

    if not unreal.EditorAssetLibrary.save_asset(asset, only_if_is_dirty=False):
        _fail("could not save %s" % asset)
    lut = calibration.get_editor_property("lut")
    if lut:
        unreal.EditorAssetLibrary.save_asset(lut.get_path_name(), only_if_is_dirty=False)
    return calibration


def main():
    collection = make_collection()
    calibration = make_calibration()
    # make_collection() only ever adds a *missing* row -- UElysiumSurfaceSettings is the single
    # writer of a row's stored value, so a knob whose class default changed since an earlier run
    # (or since make_v2_materials.py's own row-creation dict) needs this call to actually land.
    # PushToCollection mutates the loaded UMaterialParameterCollection in memory but does not
    # save it, so this generator (the thing that is supposed to leave the asset on disk in sync)
    # saves it itself afterward -- unconditionally, not `only_if_is_dirty`, because the object's
    # own dirty flag was set by a C++-side property write the asset-save path does not special-case.
    settings = unreal.get_default_object(unreal.ElysiumSurfaceSettings)
    settings.push_to_collection()
    collection_asset = "%s/%s" % (PKG, COLLECTION_NAME)
    if not unreal.EditorAssetLibrary.save_asset(collection_asset, only_if_is_dirty=False):
        _fail("could not save %s after push_to_collection" % collection_asset)
    unreal.log("[make_surface_knobs] %s: %d scalar rows" % (
        COLLECTION_NAME, len(collection.get_editor_property("scalar_parameters"))))
    unreal.log("[make_surface_knobs] %s: %d class rows" % (
        CALIBRATION_NAME, len(calibration.get_editor_property("rows"))))


if __name__ == "__main__":
    main()
