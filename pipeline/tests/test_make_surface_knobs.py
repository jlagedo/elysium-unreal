"""Pins `pipeline/unreal/make_surface_knobs.py`'s `SCALAR_NAMES` against
`UElysiumSurfaceSettings::ScalarBindings()` (Source/ElysiumUE/Public/ElysiumSurfaceSettings.h +
Private/Visual/ElysiumSurfaceSettings.cpp), and its mirrored `SURFACE_CLASSES` copy against the
real `elysium_pipeline.importers.materials.SURFACE_CLASSES`, so a knob or a class name added to one
side and not the other fails a test instead of silently drifting -- the generator creates
`MPC_ElysiumSurfaces`'s rows and `DA_SurfaceCalibration`'s rows by these names, and a mismatch
means the C++ side pushes or seeds something nobody reads.

Parses the generator and the C++ header as text rather than importing/including either:
`make_surface_knobs.py` imports `unreal` at module scope (like every `pipeline/unreal/make_*.py`
generator) and is only importable inside the editor's embedded Python -- which is also *why* it
carries a mirrored `SURFACE_CLASSES` tuple rather than importing the real one:
`elysium_pipeline.importers.materials` pulls in `textures.py`, which imports `numpy` at module
scope, and Unreal's embedded editor Python carries no `numpy` (confirmed: importing it raises
`ModuleNotFoundError` before `make_surface_knobs.py` touches a single asset). The C++ side has no
Python binding to introspect from a plain pytest process either.
"""
from __future__ import annotations

import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
GENERATOR = REPO / "pipeline" / "unreal" / "make_surface_knobs.py"
SETTINGS_CPP = REPO / "Source" / "ElysiumUE" / "Private" / "Visual" / "ElysiumSurfaceSettings.cpp"
CALIBRATION_H = REPO / "Source" / "ElysiumUE" / "Public" / "ElysiumSurfaceCalibration.h"


def _generator_scalar_names() -> set[str]:
    text = GENERATOR.read_text(encoding="utf-8")
    match = re.search(r"SCALAR_NAMES\s*=\s*\{(.*?)\n\}", text, re.DOTALL)
    assert match, "SCALAR_NAMES dict not found in make_surface_knobs.py"
    return set(re.findall(r'"(\w+)":\s*[-\d.]+', match.group(1)))


def _cpp_scalar_bindings() -> set[str]:
    text = SETTINGS_CPP.read_text(encoding="utf-8")
    match = re.search(r"ScalarBindings\(\).*?Bindings\s*=\s*\{(.*?)\n\t\};", text, re.DOTALL)
    assert match, "ScalarBindings() static array not found in ElysiumSurfaceSettings.cpp"
    return set(re.findall(r'FName\(TEXT\("(\w+)"\)\)', match.group(1)))


def _generator_surface_classes() -> tuple[str, ...]:
    text = GENERATOR.read_text(encoding="utf-8")
    match = re.search(r"SURFACE_CLASSES\s*=\s*\((.*?)\n\)", text, re.DOTALL)
    assert match, "SURFACE_CLASSES tuple not found in make_surface_knobs.py"
    return tuple(re.findall(r'"(\w+)"', match.group(1)))


def _cpp_max_rows() -> int:
    text = CALIBRATION_H.read_text(encoding="utf-8")
    match = re.search(r"static constexpr int32 MaxRows = (\d+);", text)
    assert match, "MaxRows constant not found in ElysiumSurfaceCalibration.h"
    return int(match.group(1))


def test_generator_scalar_names_match_cpp_bindings():
    generator_names = _generator_scalar_names()
    cpp_names = _cpp_scalar_bindings()
    assert generator_names, "no scalar names parsed from make_surface_knobs.py"
    assert cpp_names, "no scalar bindings parsed from ElysiumSurfaceSettings.cpp"
    assert generator_names == cpp_names, (
        "make_surface_knobs.SCALAR_NAMES and UElysiumSurfaceSettings::ScalarBindings() disagree: "
        "generator-only=%r cpp-only=%r"
        % (generator_names - cpp_names, cpp_names - generator_names)
    )


def test_generator_surface_classes_mirrors_the_real_list():
    """`make_surface_knobs.py` carries a *mirrored copy* of
    `elysium_pipeline.importers.materials.SURFACE_CLASSES` (importing the real module needs numpy,
    unavailable in the embedded editor interpreter the generator actually runs under) -- so the
    mirror is asserted equal, in order, against the real list here, outside the editor, where numpy
    is present.
    """
    from elysium_pipeline.importers.materials import SURFACE_CLASSES

    generator_classes = _generator_surface_classes()
    assert generator_classes, "no SURFACE_CLASSES tuple parsed from make_surface_knobs.py"
    assert generator_classes == SURFACE_CLASSES, (
        "make_surface_knobs.SURFACE_CLASSES has drifted from "
        "elysium_pipeline.importers.materials.SURFACE_CLASSES"
    )


def test_surface_classes_seed_order_is_default_first():
    """`SeedDefaultRows` assigns Index 0..N-1 in the given order, and `make_surface_knobs.py`
    passes `SURFACE_CLASSES` straight through -- so `default` (the master's fallback lookup for an
    unresolved SurfaceClassIndex) must be first in that list, and the list must fit
    `UElysiumSurfaceCalibration::MaxRows` (128, `docs/architecture/seam_map_material.md` revision
    2026-08-31: the class table grew to 69-72 entries, over the original 64-row cap).
    """
    from elysium_pipeline.importers.materials import SURFACE_CLASSES

    assert SURFACE_CLASSES[0] == "default"
    assert len(SURFACE_CLASSES) == len(set(SURFACE_CLASSES)), "SURFACE_CLASSES has a duplicate name"
    assert len(SURFACE_CLASSES) <= _cpp_max_rows(), (
        "SURFACE_CLASSES (%d entries) exceeds UElysiumSurfaceCalibration::MaxRows (%d)"
        % (len(SURFACE_CLASSES), _cpp_max_rows())
    )
