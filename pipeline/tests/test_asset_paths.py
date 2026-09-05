"""The resolver fixture is also executed by Elysium.Substrate.ModelNames.BakedUnit."""
import json
from pathlib import Path

import pytest

from elysium_pipeline import asset_paths as paths

ROWS = json.loads((Path(__file__).parent / "fixtures/baked_paths.json").read_text())


@pytest.mark.parametrize("row", ROWS, ids=lambda row: row["id"] + ":" + row["prefix"])
def test_baked_unit_golden(row):
    args = row["id"], row["prefix"], row.get("role"), row.get("label")
    if row["path"] is None:
        with pytest.raises(paths.AssetPathError):
            paths.baked_unit(*args)
    else:
        assert paths.baked_unit(*args) == row["path"]


def test_collision_guard_includes_case_folding_and_composites():
    with pytest.raises(paths.AssetPathError, match="collision"):
        paths.assert_unique_paths([
            ("a-b", paths.baked_path("model", "a-b", "SK")),
            ("a_b", paths.baked_path("model", "a_b", "SK"))])
    with pytest.raises(paths.AssetPathError, match="collision"):
        paths.assert_unique_paths([("a", "/ElysiumBaked/Models/SK_A"),
                                   ("b", "/elysiumbaked/models/sk_a")])
    with pytest.raises(paths.AssetPathError, match="collision"):
        paths.assert_unique_paths([
            ("unit", paths.baked_path("texture", "skybox/hav_Sky", "TC")),
            ("composite", paths.baked_path("texture", "skybox/hav", "TC", "Sky"))])


def test_corpus_assets_use_reserved_root():
    assert paths.corpus_path("model", "DA", "Cast") == "/ElysiumBaked/Models/_Corpus/DA_Cast"


def test_physical_path_limit_counts_content_root(tmp_path):
    package = paths.baked_path("model", "a" * 230, "SK")
    with pytest.raises(paths.AssetPathError, match="240"):
        paths.validate_landing(package, tmp_path)
    with pytest.raises(paths.AssetPathError):
        paths.validate_landing("/ElysiumBaked/../authored", tmp_path)
