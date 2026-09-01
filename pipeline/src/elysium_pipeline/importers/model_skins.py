"""The corpus skin table's own data (R1.5): the diff-against-family-0 fold that turns a staged
`manifest.json`'s per-unit `skinFamilies`/`familyCount` into
`/ElysiumBaked/Meshes/DA_ElysiumPropSkins`'s own rows
(`docs/architecture/seam_map_model.md` -> "Import" -> "Skins table").

Kept apart from `importers.models` (the offline stage, R1.3) on purpose: that module's own imports
reach into the MDL/VTX decoders (`formats.model_glb` -> ... -> `numpy`), fine for the offline stage
process but fatal for `pipeline/unreal/import_models.py`, which runs inside Unreal's embedded
Python and has no `numpy`. This module's only import is `elysium_pipeline.shared_corpus`
(itself dependency-free besides `elysium_pipeline.mounts`), so the editor phase can import it
directly for the finalize step that authors the table, and `importers.models` re-exports the same
two names so a caller that already imports the stage module sees no difference.

`PACKAGE_ROOT` is intentionally restated rather than imported from `importers.models` (which would
reintroduce the heavy import chain this module exists to avoid); the two are pinned equal by
`pipeline/tests/test_model_skin_table.py`.
"""

from __future__ import annotations

from typing import Sequence

from elysium_pipeline import shared_corpus

#: The package every static mesh -- and this table -- lands under, the V2 sibling of the legacy
#: shared bake ("Identity and naming"). Must equal `importers.models.PACKAGE_ROOT`.
PACKAGE_ROOT = "/ElysiumBaked/Meshes"


def skin_set_asset_path() -> str:
    """`/ElysiumBaked/Meshes/DA_ElysiumPropSkins` -- the corpus-wide table's own path, beside the
    legacy shared bake's asset of the same name under a different root ("Identity and naming")."""

    return f"{PACKAGE_ROOT}/{shared_corpus.PROP_SKINS_ASSET}"


def build_skin_table(entries: Sequence[dict]) -> list[dict]:
    """The corpus skin table's own rows, folded from the staged manifest's `assets[]` --
    `(stem, family index) -> [(slot name, MI_ asset) ...]` ("Skins table"). Pure data, no Unreal
    dependency, so the editor phase (`pipeline/unreal/import_models.py`) only has to turn each row
    into `unreal.ElysiumPropSkinModel`/`ElysiumSkinFamily`/`ElysiumSkinOverride` objects.

    Each staged entry's own `skinFamilies` already resolved every column of the model's *whole*
    skin table to a `MI_` path (or `MI_V2_Missing`) for *every* family, undiffed
    (`importers.models._build_skin_families`'s own docstring: "before the diff-only fold the
    corpus-wide `DA_ElysiumPropSkins` table applies (R1.5's job, not this stage's)") -- this is
    that fold.

    - **Family 0 gets no row.** It is the baseline every other family is diffed against, never a
      row of its own.
    - **A family identical to family 0 produces no row at all** -- only the slots (and only the
      families) that actually differ are kept, so "the resolution then finds nothing and the body
      draws its own materials, which is the same picture by a cheaper route."
    - A stem with no differing family anywhere is dropped from the table entirely: with nothing to
      substitute, a lookup would find nothing either way, and the shorter table is the cheaper
      route to that exact same outcome.
    - `familyCount` is carried through from the stage's own count (`skinFamilies` before the fold),
      not the count of rows this fold keeps, so `UElysiumPropSkinSet::Find` can clamp a
      placement's out-of-range index to the model's actual last family.
    """

    rows: list[dict] = []
    for entry in entries:
        families = entry.get("skinFamilies") or []
        if not families:
            continue
        baseline_family = next((row for row in families if row.get("family") == 0), families[0])
        baseline = dict(zip(
            baseline_family.get("slots") or [], baseline_family.get("materials") or [],
        ))

        overrides_by_family: dict[int, list[tuple[str, str]]] = {}
        for family in families:
            family_index = family.get("family")
            if not isinstance(family_index, int) or family_index <= 0:
                continue
            overrides = [
                (slot, material)
                for slot, material in zip(family.get("slots") or [], family.get("materials") or [])
                if baseline.get(slot) != material
            ]
            if overrides:
                overrides_by_family[family_index] = overrides

        if not overrides_by_family:
            continue

        rows.append({
            "stem": entry["stem"],
            "familyCount": int(entry.get("familyCount") or 0),
            "families": [
                {"family": family_index, "overrides": overrides_by_family[family_index]}
                for family_index in sorted(overrides_by_family)
            ],
        })

    rows.sort(key=lambda row: row["stem"])
    return rows
