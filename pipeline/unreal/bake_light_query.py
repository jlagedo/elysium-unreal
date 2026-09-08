"""Author the native gameplay light service beside a V2 map's baked level."""
import json

import unreal

from elysium_pipeline.asset_paths import map_package


def author(row):
    name = "DA_%s_LightQuery" % row["map"]
    directory = map_package(row["map"])
    path = directory + "/" + name
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.ElysiumMapLightQueryData)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, directory, unreal.ElysiumMapLightQueryData, factory)
    if asset is None:
        raise ValueError("cannot create gameplay light query asset: " + path)
    error = asset.author_json(json.dumps(row, separators=(",", ":")))
    if error:
        raise ValueError("%s: %s" % (path, error))
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise ValueError("cannot save gameplay light query asset: " + path)
    unreal.log("Gameplay light query: %s (%d worldlights)" % (path, len(row["records"]["lights"])))
