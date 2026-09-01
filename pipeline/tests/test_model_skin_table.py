"""Contract tests for `importers.models.build_skin_table` (R1.5): the diff-against-family-0 fold
that turns a staged `manifest.json`'s per-unit `skinFamilies`/`familyCount` into
`/ElysiumBaked/Meshes/DA_ElysiumPropSkins`'s own rows
(`docs/architecture/seam_map_model.md` -> "Import" -> "Skins table").

The fixtures are manifest `assets[]` entries in the exact shape `stage_unit` writes -- `stem`,
`familyCount`, and `skinFamilies` as `[{"family": n, "slots": [...], "materials": [...]}]`,
undiffed and covering every column -- built by hand here rather than run through the whole GLB
staging pipeline, mirroring `test_importers_models.py`'s own synthetic-fixture style.
"""

from __future__ import annotations

from elysium_pipeline.importers import models as importer


def _entry(stem: str, family_count: int, families: list[dict]) -> dict:
    return {"stem": stem, "familyCount": family_count, "skinFamilies": families}


def _family(index: int, slots: list[str], materials: list[str]) -> dict:
    return {"family": index, "slots": slots, "materials": materials}


def test_family_zero_never_gets_a_row():
    """Family 0 is the baseline every other family is diffed against, never a row of its own --
    even though it is technically "identical to itself" and would otherwise pass the same test
    every other family does."""

    entry = _entry("scenery/lamp", 2, [
        _family(0, ["glass"], ["/ElysiumBaked/Materials/x/MI_glass_a"]),
        _family(1, ["glass"], ["/ElysiumBaked/Materials/x/MI_glass_b"]),
    ])

    rows = importer.build_skin_table([entry])

    assert len(rows) == 1
    families = rows[0]["families"]
    assert [row["family"] for row in families] == [1]


def test_family_identical_to_family_zero_produces_no_row():
    """"A family identical to family 0 produces no row at all" -- only the families (and only the
    slots within them) that actually differ survive the fold."""

    entry = _entry("scenery/lamp", 3, [
        _family(0, ["glass", "base"], ["/M/glass_a", "/M/base_a"]),
        _family(1, ["glass", "base"], ["/M/glass_b", "/M/base_a"]),  # differs on "glass" only
        _family(2, ["glass", "base"], ["/M/glass_a", "/M/base_a"]),  # identical to family 0
    ])

    rows = importer.build_skin_table([entry])

    assert len(rows) == 1
    families = rows[0]["families"]
    assert [row["family"] for row in families] == [1]
    assert families[0]["overrides"] == [("glass", "/M/glass_b")]


def test_stem_with_no_differing_family_is_dropped_entirely():
    """A stem whose every family (other than 0) is identical to family 0 gets no `Models` entry at
    all -- "the shorter table is the cheaper route to that exact same outcome" as an empty row."""

    entry = _entry("scenery/static_twin", 2, [
        _family(0, ["metal"], ["/M/metal_a"]),
        _family(1, ["metal"], ["/M/metal_a"]),
    ])

    rows = importer.build_skin_table([entry])

    assert rows == []


def test_family_count_rides_beside_the_kept_rows_not_the_row_count():
    """`familyCount` is the stage's own family count, independent of how many rows the fold keeps
    -- the runtime clamp target, not a row tally."""

    entry = _entry("scenery/many_families", 5, [
        _family(0, ["metal"], ["/M/metal_a"]),
        _family(1, ["metal"], ["/M/metal_b"]),
        _family(2, ["metal"], ["/M/metal_a"]),  # identical -- no row
        _family(3, ["metal"], ["/M/metal_a"]),  # identical -- no row
        _family(4, ["metal"], ["/M/metal_c"]),
    ])

    rows = importer.build_skin_table([entry])

    assert len(rows) == 1
    assert rows[0]["familyCount"] == 5
    assert [row["family"] for row in rows[0]["families"]] == [1, 4]


def test_slot_name_keying_uses_the_folded_names_verbatim():
    """Rows are slot-name keyed using the same disambiguated names the stage already folded
    ("Duplicate folded slot names get a suffix") -- this fold does not re-derive or re-fold them,
    it only reads `slots[]`/`materials[]` positionally and passes the name straight through."""

    entry = _entry("scenery/fancybed", 2, [
        _family(0, ["bed_col1", "null", "null_2"],
                ["/M/bed_col1_a", "/M/null_a", "/M/null2_a"]),
        _family(1, ["bed_col1", "null", "null_2"],
                ["/M/bed_col1_a", "/M/null_b", "/M/null2_a"]),
    ])

    rows = importer.build_skin_table([entry])

    assert len(rows) == 1
    overrides = rows[0]["families"][0]["overrides"]
    assert overrides == [("null", "/M/null_b")]


def test_rows_are_sorted_by_stem():
    entries = [
        _entry("scenery/zeta", 2, [
            _family(0, ["a"], ["/M/a0"]), _family(1, ["a"], ["/M/a1"]),
        ]),
        _entry("scenery/alpha", 2, [
            _family(0, ["a"], ["/M/a0"]), _family(1, ["a"], ["/M/a1"]),
        ]),
    ]

    rows = importer.build_skin_table(entries)

    assert [row["stem"] for row in rows] == ["scenery/alpha", "scenery/zeta"]


def test_skin_set_asset_path_is_the_v2_root_sibling_of_the_legacy_asset():
    assert importer.skin_set_asset_path() == "/ElysiumBaked/Meshes/DA_ElysiumPropSkins"


def test_model_skins_package_root_is_pinned_to_the_stage_modules_own():
    """`importers.model_skins` restates `PACKAGE_ROOT` rather than importing it from
    `importers.models` (whose import chain reaches `numpy`, unavailable inside Unreal's embedded
    Python -- the reason `model_skins` exists as a separate module at all); the two must agree."""

    from elysium_pipeline.importers import model_skins

    assert model_skins.PACKAGE_ROOT == importer.PACKAGE_ROOT
    assert importer.build_skin_table is model_skins.build_skin_table
    assert importer.skin_set_asset_path is model_skins.skin_set_asset_path
