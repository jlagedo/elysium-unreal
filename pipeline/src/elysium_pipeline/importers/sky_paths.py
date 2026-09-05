"""D1 sky addresses; safe to import in Unreal's embedded Python (no pixel libraries)."""
from elysium_pipeline.asset_paths import baked_path


class SkyCompositeError(ValueError):
    """A sky cannot be projected without losing or inventing source data."""


def normalize_sky_name(sky: str) -> str:
    sky = sky.strip().lower()
    if not sky or any(c not in "abcdefghijklmnopqrstuvwxyz0123456789_-" for c in sky):
        raise SkyCompositeError(f"invalid sky name: {sky!r}")
    return sky


def sky_cube_path(sky: str) -> str:
    return baked_path("texture", "skybox/" + normalize_sky_name(sky), "TC", role="Sky")


def sky_material_path(sky: str) -> str:
    return baked_path("material", "skybox/" + normalize_sky_name(sky), "MI", role="Sky")
