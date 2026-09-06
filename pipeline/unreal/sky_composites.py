"""Editor adapters for D1 skies. Import-safe: no editor work at module scope.

Texture import attaches the full six-unit provenance after checking the imported
source mips. Map bake only resolves the finished cube and its stored mean. It must
never build a replacement cube when a dependency is absent.
"""
from __future__ import annotations

import math

from elysium_pipeline.importers.sky_paths import sky_cube_path, sky_material_path

SKY_DOME_PACKAGE = "/Game/ElysiumGenerated/Sky"
SKY_DOME_PATH = SKY_DOME_PACKAGE + "/SM_SkyDome"
DEFAULT_SKY_CUBE_PATH = SKY_DOME_PACKAGE + "/TC_DefaultSky"
ENGINE_DEFAULT_CUBE = "/Engine/EngineResources/DefaultTextureCube.DefaultTextureCube"
LOOKDEV_PACKAGE = "/Game/ElysiumGenerated/Lookdev"
LOOKDEV_MAP_PATH = LOOKDEV_PACKAGE + "/Materials"


def ensure_default_sky_cube():
    """Bootstrap M_Sky independently of imports, without changing an engine asset.

    Each baked sky MI supplies its own cube. This generated default only gives the
    master a valid linear cube while content generators run before texture import.
    """
    import unreal

    library = unreal.EditorAssetLibrary
    # `load_asset` logs an editor error for an absent asset, and a logged error fails the
    # commandlet; the first bootstrap on a fresh mount is the expected absent case.
    cube = library.load_asset(DEFAULT_SKY_CUBE_PATH) if library.does_asset_exist(DEFAULT_SKY_CUBE_PATH) else None
    if cube is None:
        source = library.load_asset(ENGINE_DEFAULT_CUBE)
        if source is None or source.get_class().get_name() != "TextureCube":
            raise RuntimeError("M_Sky bootstrap requires engine DefaultTextureCube")
        if not library.does_directory_exist(SKY_DOME_PACKAGE) and not library.make_directory(SKY_DOME_PACKAGE):
            raise RuntimeError("could not create %s" % SKY_DOME_PACKAGE)
        cube = library.duplicate_asset(ENGINE_DEFAULT_CUBE, DEFAULT_SKY_CUBE_PATH)
    if cube is None or cube.get_class().get_name() != "TextureCube":
        raise RuntimeError("M_Sky bootstrap could not create TextureCube %s" % DEFAULT_SKY_CUBE_PATH)
    cube.set_editor_properties({
        "srgb": False,
        "compression_settings": unreal.TextureCompressionSettings.TC_HDR_F32,
        "mip_gen_settings": unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS,
        "never_stream": True,
    })
    if not library.save_asset(DEFAULT_SKY_CUBE_PATH):
        raise RuntimeError("could not save M_Sky default cube %s" % DEFAULT_SKY_CUBE_PATH)
    return cube


def attach_provenance(cube, sidecar):
    import unreal

    record, error = unreal.ElysiumSkyProvenance.apply_json(cube, sidecar)
    if record is None:
        raise RuntimeError("sky provenance/source conservation rejected: %s" % error)
    return record


def load_sky_cube(sky):
    import unreal

    path = sky_cube_path(sky)
    cube = unreal.EditorAssetLibrary.load_asset(path)
    if cube is None or cube.get_class().get_name() != "TextureCube":
        raise RuntimeError("sky '%s': missing TextureCube %s; import textures first" % (sky, path))
    record = unreal.ElysiumSkyProvenance.find(cube)
    if record is None:
        raise RuntimeError("sky '%s': %s lacks composite provenance; re-import textures" % (sky, path))
    if sky_cube_path(record.get_editor_property("sky_name")) != path:
        raise RuntimeError("sky '%s': composite provenance names another sky" % sky)
    mean = float(record.get_editor_property("upper_hemisphere_mean"))
    if not math.isfinite(mean) or mean < 0:
        raise RuntimeError("sky '%s': invalid upper-hemisphere mean" % sky)
    return cube, mean


def material_address(sky):
    """The package and name to pass to bake_lib.make_material_instance."""
    return tuple(sky_material_path(sky).rsplit("/", 1))
