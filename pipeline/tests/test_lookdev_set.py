"""Contract tests for `pipeline/unreal/lookdev_set.json`, SF-4.7's tracked review set.

The set names real corpus units and the `MI_` asset path the naming rule
(`docs/architecture/seam_map_material.md` -> "Identity and naming") derives from each one --
`vtmb:material:<dir>/<stem>` -> `/ElysiumBaked/Materials/<dir>/MI_<safe stem>`. Every unit-exists
check needs the real `exports_v2` corpus on this machine and abstains (skip, not fail) when it is
absent, exactly as `test_texture_corpus_import.py`'s legacy-cube parity test does; the shape and
naming checks need no corpus and always run.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

import pytest

from elysium_pipeline.asset_names import safe_name

REPO = Path(__file__).resolve().parents[2]
SET_PATH = REPO / "pipeline/unreal/lookdev_set.json"
PACKAGE_ROOT = "/ElysiumBaked/Materials"
VALID_SHAPES = {"sphere", "plane", "both"}


def _load_entries():
    document = json.loads(SET_PATH.read_text(encoding="utf-8"))
    return document["entries"]


def _expected_material(unit: str) -> str:
    assert unit.startswith("vtmb:material:")
    directory, _, stem = unit[len("vtmb:material:"):].rpartition("/")
    prefix = f"{PACKAGE_ROOT}/{directory}" if directory else PACKAGE_ROOT
    return f"{prefix}/MI_{safe_name(stem)}"


def _unit_glb_path(unit: str, export_v2_root: Path) -> Path:
    return export_v2_root / "materials" / f"{unit[len('vtmb:material:'):]}.glb"


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
    assert len(_load_entries()) >= 20


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
