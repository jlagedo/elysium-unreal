"""The surface-class table: `$surfaceprop`/top-directory/family-default resolution's shared data.

Extracted out of `elysium_pipeline.importers.materials` so `pipeline/unreal/make_surface_knobs.py`
-- which runs inside Unreal's embedded editor Python, an interpreter with no `numpy` -- can import
the real list directly instead of carrying a hand-mirrored copy that a pytest has to pin against
this module. **No imports of `elysium_pipeline.importers.materials` or anything that pulls in
`numpy`** (`textures.py` does, at module scope): that is the whole reason this module exists
separately rather than being read straight out of `materials.py`. `materials.py` re-exports every
name below so existing call sites keep working unchanged.

Design: `docs/architecture/seam_map_material.md` -> "Import" -> "Identity and naming".
`seam_migration.md` 2026-08-31, "Revised after review".
"""

from __future__ import annotations

#: The 63 `scripts/surfaceproperties.txt` entry names this corpus's `$surfaceprop` values resolve
#: against (`uv run elysium import surface-properties`, 2026-08-31 run), `default` first. Every
#: name in this tuple gets a real `PM_<name>` physical-material asset (SF-2); everything else in
#: `SURFACE_CLASSES` below falls back to `PM_default` (`physMaterialFallback`).
_SURFACEPROP_NAMES = (
    "default", "armorflesh", "bottle", "boulder", "brick", "can_pop", "can_pop_crushed",
    "canister", "cardboard", "carpet", "computer", "concrete", "default_silent", "dirt",
    "fish_fresh", "fish_frozen", "flesh", "gargoyle", "glass", "glass_shard", "glassbottle",
    "grass", "gravel", "grenade", "gunship", "ice", "kitchen_pan", "kitchen_pot",
    "kitchen_utensils", "ladder", "metal", "metal_barrel", "metalgrate", "metalpanel",
    "metalvent", "ming_xiao", "ming_xiao_tentacle", "mud", "paper", "papercup", "plaster",
    "plastic", "player", "player_control_clip", "popcan", "quiet", "ring", "rivet", "rock",
    "roller", "rubber", "sand", "snow", "stone", "strider", "tile", "tin", "wade", "water",
    "watermelon", "weapon", "wood", "woodpanel",
)
#: VMT top-directory names the design's tier-2 fallback names (`seam_map_material.md` -> "Identity
#: and naming") that are not already a `$surfaceprop` entry name.
_TOP_DIRECTORY_ONLY = ("asphalt", "blends", "cable", "drapery", "grates", "ground")
#: The four `$surfaceprop` values the 63-entry table does not define, each getting a class row of
#: its own; `asphalt` is already counted via `_TOP_DIRECTORY_ONLY` above, so only three are new
#: here -- the revision's own accounting (`seam_migration.md` -> "Revised after review").
_TIER1_ADDITIONS = ("bone", "cloth", "leather")
#: The ordered class table: `default` at index 0 (the class LUT's row 0), everything else sorted so
#: the table is stable across regeneration. 72 rows: 63 (`_SURFACEPROP_NAMES`, `default` included)
#: + 6 (`_TOP_DIRECTORY_ONLY`) + 3 (`_TIER1_ADDITIONS`). SF-4.1's `UElysiumSurfaceCalibration` seeds
#: its rows from this same list, so the index this lane writes as `SurfaceClassIndex` is the row
#: that class asset defines.
SURFACE_CLASSES: tuple[str, ...] = ("default",) + tuple(
    sorted(set(_SURFACEPROP_NAMES[1:]) | set(_TOP_DIRECTORY_ONLY) | set(_TIER1_ADDITIONS))
)
SURFACE_CLASS_INDEX = {name: index for index, name in enumerate(SURFACE_CLASSES)}
#: The curated 16-name top-directory allowlist (tier 2), stated verbatim rather than derived, per
#: "Identity and naming": `plaster`, `wood`, `stone`, `blends`, `brick`, `ground`, `metal`, `tile`,
#: `drapery`, `carpet`, `cable`, `glass`, `grates`, `asphalt`, `water`, `grass`.
TOP_DIRECTORY_CLASSES: frozenset[str] = frozenset({
    "plaster", "wood", "stone", "blends", "brick", "ground", "metal", "tile", "drapery",
    "carpet", "cable", "glass", "grates", "asphalt", "water", "grass",
})
#: Tier-3 per-family default class row, keyed by the resolved master.
FAMILY_DEFAULT_CLASS: dict[str, str] = {
    "M_V2_Lit": "default", "M_V2_LitTranslucent": "default", "M_V2_Unlit": "default",
    "M_V2_Eyes": "flesh", "M_V2_Water": "water", "M_V2_Sprite": "default",
    "M_V2_Decal": "default", "M_V2_TwoTexture": "default", "M_V2_Refract": "glass",
}
