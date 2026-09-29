"""Author a V2 map's place set, `UElysiumMapPlaces`, beside its baked level (0018 story 4).

The staged `places` block (`importers.map_places`) is handed to the asset's own
`AuthorJson` UFUNCTION verbatim: the C++ side owns the reading and the checks, this module only
creates the asset, stamps the recipe and saves it. Nothing is decided here.
"""
import hashlib
import json

import unreal

from pipeline.unreal import bake_lib as bl
from elysium_pipeline.asset_paths import baked_map_places

#: The recipe stage label and the producer tag on the asset.
STAGE = "map_places"
PRODUCER = "map-places"

#: The one class this lane authors.
ASSET_CLASS = "ElysiumMapPlaces"


def payload(block):
    """The exact text `AuthorJson` receives: compact, keys in staged order."""
    return json.dumps(block, separators=(",", ":"))


def digest(block):
    """The sha256 the level recipe and the asset stamp are taken over; None for no block."""
    if block is None:
        return None
    text = json.dumps(block, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def author(block):
    """Create or overwrite `/ElysiumBaked/<map>/DA_<map>_Places` from one staged block."""
    path = baked_map_places(block["map"])
    directory, name = bl.split_asset_path(path)
    # LoadObject, not EditorAssetLibrary.load_asset: the latter logs a hard Error when the
    # registry has no such asset, which is the normal first-bake path and would fail the run.
    asset = unreal.load_asset(path)
    if asset is None:
        bl.ensure_dir(directory)
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.ElysiumMapPlaces)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, directory, unreal.ElysiumMapPlaces, factory)
    if asset is None:
        raise ValueError("cannot create place set asset: " + path)
    if not asset.author_json(payload(block)):
        raise ValueError("%s: AuthorJson refused the staged place set (see the log)" % path)
    bl.stamp_recipe(asset, bl.recipe_fingerprint(STAGE, path, {"places": digest(block)}),
                    producer=PRODUCER)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise ValueError("cannot save place set asset: " + path)
    unreal.log("Place set: %s (%d places, %d bound hints, %d crosswalk pairs)" % (
        path, len(block["places"]), sum(1 for row in block["places"] if row["hint"] >= 0),
        len(block["crosswalkPairs"])))
    return path
