"""Contract tests for `pipeline/unreal/lookdev_props_set.json`, the R1.6 props row
(`docs/project/seam_migration.md` -> Roadmap -> "R1.6 Verify + lookdev").

Same shape as `test_lookdev_set.py`'s material-grid contract: the set names real corpus model
units and the `SM_` asset path the props importer's own naming rule derives from each one, going
through `elysium_pipeline.importers.models.asset_path_for` rather than re-deriving the rule here.
The unit-exists and manifest-membership checks need the real corpus/staged manifest this machine
last produced and abstain (skip, not fail) when either is absent.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

import pytest

from elysium_pipeline import paths
from elysium_pipeline.importers import models

REPO = Path(__file__).resolve().parents[2]
SET_PATH = REPO / "pipeline/unreal/lookdev_props_set.json"


def _load_entries():
    document = json.loads(SET_PATH.read_text(encoding="utf-8"))
    return document["entries"]


def _expected_mesh(unit: str) -> str:
    assert unit.startswith("vtmb:model:")
    return models.asset_path_for(unit[len("vtmb:model:"):])


def _unit_glb_path(unit: str, export_v2_root: Path) -> Path:
    return export_v2_root / "models" / f"{unit[len('vtmb:model:'):]}.glb"


def _manifest_assets():
    """`{stem: {assetPath, collision, shape, lods, slots, ...}}` from the staged
    `import/models/manifest.json`, or None when it is not on this machine (no
    `ELYSIUM_WORK_ROOT`, or no `import models` run yet)."""
    try:
        work_root = paths.work_root()
    except RuntimeError:
        return None
    manifest_path = models.staging_root(work_root) / "manifest.json"
    if not manifest_path.is_file():
        return None
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    return {asset["stem"]: asset for asset in document.get("assets", [])}


def test_set_file_exists_and_parses():
    assert SET_PATH.is_file()
    document = json.loads(SET_PATH.read_text(encoding="utf-8"))
    assert isinstance(document.get("entries"), list) and document["entries"]


@pytest.mark.parametrize("entry", _load_entries(), ids=lambda entry: entry["label"])
def test_entry_has_required_fields(entry):
    for key in ("label", "unit", "mesh"):
        assert entry.get(key), f"{entry}: missing/empty '{key}'"


@pytest.mark.parametrize("entry", _load_entries(), ids=lambda entry: entry["label"])
def test_mesh_path_matches_naming_rule(entry):
    assert entry["mesh"] == _expected_mesh(entry["unit"])


def test_labels_are_unique():
    labels = [entry["label"] for entry in _load_entries()]
    assert len(labels) == len(set(labels))


def test_units_are_unique():
    units = [entry["unit"] for entry in _load_entries()]
    assert len(units) == len(set(units))


def test_set_covers_a_handful_of_entries():
    # "A handful" (R1.6's own wording), not the whole 414-unit test corpus.
    assert 3 <= len(_load_entries()) <= 12


def test_set_covers_the_sentinel_unit():
    # docs/project/seam_migration.md R1.6 notes name this exact stem: the unit whose slot binds
    # the vtmb:missing-material: sentinel (MI_V2_Missing), so the props row proves the model and
    # material lanes join correctly, not just that a mesh exists.
    assert any(
        entry["mesh"] == "/ElysiumBaked/Meshes/SM_models_scenery_structural_warrens_floorblock"
        for entry in _load_entries()
    ), "no entry for the sentinel unit SM_models_scenery_structural_warrens_floorblock"


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
def test_mesh_is_an_asset_path_in_the_manifest(entry):
    assets = _manifest_assets()
    if assets is None:
        pytest.skip("no staged models manifest.json on this machine")
    stem = entry["mesh"].rsplit("/SM_", 1)[-1]
    assert stem in assets and assets[stem]["assetPath"] == entry["mesh"], (
        f"{entry['mesh']}: not a staged assetPath in models manifest.json -- the props row names "
        f"a unit the last `import models` run never staged"
    )


def test_set_covers_at_least_one_multi_lod_unit():
    assets = _manifest_assets()
    if assets is None:
        pytest.skip("no staged models manifest.json on this machine")
    multi_lod = [
        entry for entry in _load_entries()
        if len(assets.get(entry["mesh"].rsplit("/SM_", 1)[-1], {}).get("lods", [])) > 1
    ]
    assert multi_lod, "no props-row entry resolves to a multi-LOD baked mesh"


def test_set_covers_at_least_one_bbox_collision_unit():
    assets = _manifest_assets()
    if assets is None:
        pytest.skip("no staged models manifest.json on this machine")
    bbox = [
        entry for entry in _load_entries()
        if assets.get(entry["mesh"].rsplit("/SM_", 1)[-1], {}).get("collision", {}).get("mode")
        == "bbox"
    ]
    assert bbox, "no props-row entry resolves to a bbox-collision baked mesh"
