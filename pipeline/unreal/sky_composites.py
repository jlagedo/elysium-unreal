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
LOOKDEV_PACKAGE = "/Game/ElysiumGenerated/Lookdev"
LOOKDEV_MAP_PATH = LOOKDEV_PACKAGE + "/Materials"


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
