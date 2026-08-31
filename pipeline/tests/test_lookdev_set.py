"""Contract tests for `pipeline/unreal/lookdev_set.json`, SF-4.7's tracked review set.

The set names real corpus units and the `MI_` asset path the naming rule derives from each one.
The naming check goes through the importer's own `materials.asset_path_for` (not a
re-implementation here, which could drift from the real rule and still agree with a bug in it)
-- `vtmb:material:<dir>/<stem>` -> `materials.PACKAGE_ROOT/<dir>/MI_<safe stem>`. Every
unit-exists check needs the real `exports_v2` corpus on this machine and abstains (skip, not
fail) when it is absent, exactly as `test_texture_corpus_import.py`'s legacy-cube parity test
does; the shape and naming checks need no corpus and always run. The manifest cross-check is the
same shape: it needs the real staged `manifest.json` this machine's last `import materials` run
produced and abstains when that is absent, never re-deriving what "exists in the manifest" means.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

import pytest

from elysium_pipeline import paths
from elysium_pipeline.importers import materials

REPO = Path(__file__).resolve().parents[2]
SET_PATH = REPO / "pipeline/unreal/lookdev_set.json"
VALID_SHAPES = {"sphere", "plane"}


def _load_entries():
    document = json.loads(SET_PATH.read_text(encoding="utf-8"))
    return document["entries"]


def _expected_material(unit: str) -> str:
    assert unit.startswith("vtmb:material:")
    return materials.asset_path_for(unit[len("vtmb:material:"):])


def _unit_glb_path(unit: str, export_v2_root: Path) -> Path:
    return export_v2_root / "materials" / f"{unit[len('vtmb:material:'):]}.glb"


def _manifest_asset_paths():
    """`{assetPath, ...}` from the staged `manifest.json`, or None when it is not on this
    machine (no `ELYSIUM_WORK_ROOT`, or no `import materials` run yet)."""
    try:
        work_root = paths.work_root()
    except RuntimeError:
        return None
    manifest_path = materials.staging_root(work_root) / materials.MANIFEST_NAME
    if not manifest_path.is_file():
        return None
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    return {asset["assetPath"] for asset in document.get("assets", [])}


def test_set_file_exists_and_parses():
    assert SET_PATH.is_file()
    document = json.loads(SET_PATH.read_text(encoding="utf-8"))
    assert isinstance(document.get("entries"), list) and document["entries"]


@pytest.mark.parametrize("entry", _load_entries(), ids=lambda entry: entry["label"])
def test_entry_shape_is_valid(entry):
    assert entry["shape"] in VALID_SHAPES


@pytest.mark.parametrize("entry", _load_entries(), ids=lambda entry: entry["label"])
def test_entry_has_required_fields(entry):
    for key in ("label", "unit", "material", "shape"):
        assert entry.get(key), f"{entry}: missing/empty '{key}'"


@pytest.mark.parametrize("entry", _load_entries(), ids=lambda entry: entry["label"])
def test_material_path_matches_naming_rule(entry):
    assert entry["material"] == _expected_material(entry["unit"])


def test_labels_are_unique():
    labels = [entry["label"] for entry in _load_entries()]
    assert len(labels) == len(set(labels))


def test_units_are_unique():
    units = [entry["unit"] for entry in _load_entries()]
    assert len(units) == len(set(units))


def test_set_covers_at_least_twenty_entries():
    assert 20 <= len(_load_entries()) <= 24


def test_set_covers_a_translucent_fixed_cube_entry():
    assert any(
        "translucent" in entry["label"].lower() and "fixed" in entry["label"].lower()
        for entry in _load_entries()
    ), "no entry labelled as both translucent and fixed-cube -- see review finding F"


def test_set_covers_at_least_two_patched_map_instances():
    # A patched map instance's `material` sits under its own map's directory, one level below
    # the base unit's own MI_ -- e.g. `.../maps/ch_hub_1/water/MI_warrenwater_depth_20`, whose
    # *parent* MI_ is `.../water/MI_warrenwater`. `unit` carries the same tell: a patched unit's
    # key already has its own `maps/<map>/` prefix (`materials.asset_path_for`'s own docstring).
    patched = [entry for entry in _load_entries()
               if entry["unit"][len("vtmb:material:"):].startswith("maps/")]
    assert len(patched) >= 2, "fewer than two map-patched entries -- see review finding F"


@pytest.mark.parametrize("entry", _load_entries(), ids=lambda entry: entry["label"])
def test_unit_exists_in_the_corpus(entry):
    export_v2 = os.environ.get("ELYSIUM_EXPORT_V2_ROOT", "").strip()
    if not export_v2:
        pytest.skip("ELYSIUM_EXPORT_V2_ROOT is not configured on this machine")
    root = Path(export_v2)
    if not root.is_dir():
        pytest.skip(f"no exports_v2 corpus at {root}")
    glb_path = _unit_glb_path(entry["unit"], root)
    assert glb_path.is_file(), f"{entry['unit']}: no GLB at {glb_path}"


@pytest.mark.parametrize("entry", _load_entries(), ids=lambda entry: entry["label"])
def test_material_is_an_asset_path_in_the_manifest(entry):
    asset_paths = _manifest_asset_paths()
    if asset_paths is None:
        pytest.skip("no staged materials manifest.json on this machine")
    assert entry["material"] in asset_paths, (
        f"{entry['material']}: not an assetPath in {materials.MANIFEST_NAME} -- "
        f"the review set names a unit the last `import materials` run never staged"
    )


def test_set_covers_at_least_two_instance_of_instance_entries():
    """The stronger form of `test_set_covers_at_least_two_patched_map_instances`: not just a
    `maps/`-prefixed unit, but one the manifest itself records as `patched` with its `parent` a
    baked `MI_` (`materials.PACKAGE_ROOT`) rather than one of the nine `M_V2_*` masters -- the
    instance-of-instance path SF-4.5 supports. Skips, rather than failing, when there is no staged
    manifest to check against."""
    try:
        work_root = paths.work_root()
    except RuntimeError:
        pytest.skip("ELYSIUM_WORK_ROOT is not configured on this machine")
    manifest_path = materials.staging_root(work_root) / materials.MANIFEST_NAME
    if not manifest_path.is_file():
        pytest.skip("no staged materials manifest.json on this machine")
    by_asset_path = {
        asset["assetPath"]: asset
        for asset in json.loads(manifest_path.read_text(encoding="utf-8")).get("assets", [])
    }
    instance_of_instance = [
        entry for entry in _load_entries()
        if (asset := by_asset_path.get(entry["material"])) is not None
        and asset.get("patched")
        and str(asset.get("parent") or "").startswith(materials.PACKAGE_ROOT + "/")
    ]
    assert len(instance_of_instance) >= 2, (
        "fewer than two review-set entries are a manifest-patched instance of a base MI_ -- "
        "see review finding F"
    )
