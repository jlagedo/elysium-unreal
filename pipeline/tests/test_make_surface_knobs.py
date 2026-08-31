"""Pins `pipeline/unreal/make_surface_knobs.py`'s `SCALAR_NAMES` against
`UElysiumSurfaceSettings::ScalarBindings()` (Source/ElysiumUE/Public/ElysiumSurfaceSettings.h +
Private/Visual/ElysiumSurfaceSettings.cpp), and `Config/DefaultElysium.ini`'s field values against
the header's own field initializers, so a knob added to one side and not the other, or an ini that
falls out of sync with the class defaults it restates, fails a test instead of silently drifting --
the generator creates `MPC_ElysiumSurfaces`'s rows by these names, and a mismatch means the C++
side pushes something nobody reads (or a fresh checkout's collection starts from a value the class
no longer defaults to).

`SURFACE_CLASSES` (the class table `DA_SurfaceCalibration`'s rows are seeded from) is no longer a
mirrored copy this module pins: `make_surface_knobs.py` imports it directly from
`elysium_pipeline.importers.surface_classes`, a module with no imports of its own, so there is
nothing left to drift -- `test_surface_classes_seed_order_is_default_first` below still checks the
list's own invariants (default first, no duplicate, fits `MaxRows`).

Parses the generator and the C++ header as text rather than importing/including either:
`make_surface_knobs.py` imports `unreal` at module scope (like every `pipeline/unreal/make_*.py`
generator) and is only importable inside the editor's embedded Python. The C++ side has no Python
binding to introspect from a plain pytest process either.
"""
from __future__ import annotations

import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
GENERATOR = REPO / "pipeline" / "unreal" / "make_surface_knobs.py"
SETTINGS_CPP = REPO / "Source" / "ElysiumUE" / "Private" / "Visual" / "ElysiumSurfaceSettings.cpp"
SETTINGS_H = REPO / "Source" / "ElysiumUE" / "Public" / "ElysiumSurfaceSettings.h"
CALIBRATION_H = REPO / "Source" / "ElysiumUE" / "Public" / "ElysiumSurfaceCalibration.h"
DEFAULT_INI = REPO / "Config" / "DefaultElysium.ini"


def _generator_scalar_names() -> set[str]:
    text = GENERATOR.read_text(encoding="utf-8")
    match = re.search(r"SCALAR_NAMES\s*=\s*\((.*?)\n\)", text, re.DOTALL)
    assert match, "SCALAR_NAMES tuple not found in make_surface_knobs.py"
    return set(re.findall(r'"(\w+)"', match.group(1)))


def _cpp_scalar_bindings() -> set[str]:
    text = SETTINGS_CPP.read_text(encoding="utf-8")
    match = re.search(r"ScalarBindings\(\).*?Bindings\s*=\s*\{(.*?)\n\t\};", text, re.DOTALL)
    assert match, "ScalarBindings() static array not found in ElysiumSurfaceSettings.cpp"
    return set(re.findall(r'FName\(TEXT\("(\w+)"\)\)', match.group(1)))


def _cpp_max_rows() -> int:
    text = CALIBRATION_H.read_text(encoding="utf-8")
    match = re.search(r"static constexpr int32 MaxRows = (\d+);", text)
    assert match, "MaxRows constant not found in ElysiumSurfaceCalibration.h"
    return int(match.group(1))


#: One `Name=Value` row of a `UPROPERTY(..., Config, ...) float Name = Value;` declaration, in
#: `ElysiumSurfaceSettings.h`. Matches the same 15 scalars `ScalarBindings()` pins, so this and
#: `_cpp_scalar_bindings()` agree on which fields count as a "knob" even though they read
#: different files.
_HEADER_FIELD_RE = re.compile(
    r"UPROPERTY\(EditAnywhere, Config,.*\)\s*\n\s*float\s+(\w+)\s*=\s*([-\d.]+)f?;"
)


def _cpp_header_scalar_defaults() -> dict[str, float]:
    text = SETTINGS_H.read_text(encoding="utf-8")
    fields = dict(
        (name, float(value)) for name, value in _HEADER_FIELD_RE.findall(text)
    )
    assert fields, "no `UPROPERTY(..., Config, ...) float Name = Value;` fields parsed from ElysiumSurfaceSettings.h"
    return fields


def _default_ini_values() -> dict[str, float]:
    text = DEFAULT_INI.read_text(encoding="utf-8")
    section = re.search(
        r"\[/Script/ElysiumUE\.ElysiumSurfaceSettings\]\n(.*?)(?:\n\[|\Z)", text, re.DOTALL
    )
    assert section, "[/Script/ElysiumUE.ElysiumSurfaceSettings] section not found in DefaultElysium.ini"
    values = dict(
        (name, float(value))
        for name, value in re.findall(r"^(\w+)=([-\d.]+)\s*$", section.group(1), re.MULTILINE)
    )
    assert values, "no Name=Value rows parsed from DefaultElysium.ini's ElysiumSurfaceSettings section"
    return values


def test_default_ini_matches_header_field_initializers():
    """`Config/DefaultElysium.ini` restates `UElysiumSurfaceSettings`'s 15 field initializers so a
    fresh checkout (no ini edit yet) reads the same values the header's C++ defaults would give it
    anyway -- this pins the two against each other as text, so an owner tuning a class default in
    the header without restating it in the tracked ini (or vice versa) is a failing test, not a
    silent divergence between "what a fresh checkout ships" and "what the class says its default
    is"."""
    header_defaults = _cpp_header_scalar_defaults()
    ini_values = _default_ini_values()
    assert header_defaults == ini_values, (
        "DefaultElysium.ini and ElysiumSurfaceSettings.h's field initializers disagree: "
        "ini-only=%r header-only=%r value-mismatches=%r"
        % (
            set(ini_values) - set(header_defaults),
            set(header_defaults) - set(ini_values),
            {
                name: (header_defaults[name], ini_values[name])
                for name in set(header_defaults) & set(ini_values)
                if header_defaults[name] != ini_values[name]
            },
        )
    )


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


def test_generator_imports_the_real_surface_classes():
    """`make_surface_knobs.py` imports `SURFACE_CLASSES` from
    `elysium_pipeline.importers.surface_classes` rather than carrying a mirrored copy -- assert the
    import statement is actually there (a text check, since the generator itself is only importable
    inside the editor's embedded Python) so a future edit cannot quietly reintroduce a hand-mirrored
    list with nothing left to pin it against.
    """
    text = GENERATOR.read_text(encoding="utf-8")
    assert "from elysium_pipeline.importers.surface_classes import SURFACE_CLASSES" in text, (
        "make_surface_knobs.py no longer imports SURFACE_CLASSES from "
        "elysium_pipeline.importers.surface_classes"
    )


def test_surface_classes_seed_order_is_default_first():
    """`SeedDefaultRows` assigns Index 0..N-1 in the given order for a brand-new row, and
    `make_surface_knobs.py` passes `SURFACE_CLASSES` straight through -- so `default` (the master's
    fallback lookup for an unresolved SurfaceClassIndex) must be first in that list, and the list
    must fit `UElysiumSurfaceCalibration::MaxRows` (128, `docs/architecture/seam_map_material.md`
    revision 2026-08-31: the class table grew to 69-72 entries, over the original 64-row cap).
    """
    from elysium_pipeline.importers.surface_classes import SURFACE_CLASSES

    assert SURFACE_CLASSES[0] == "default"
    assert len(SURFACE_CLASSES) == len(set(SURFACE_CLASSES)), "SURFACE_CLASSES has a duplicate name"
    assert len(SURFACE_CLASSES) <= _cpp_max_rows(), (
        "SURFACE_CLASSES (%d entries) exceeds UElysiumSurfaceCalibration::MaxRows (%d)"
        % (len(SURFACE_CLASSES), _cpp_max_rows())
    )


def test_materials_reexports_surface_classes():
    """`elysium_pipeline.importers.materials` re-exports the shared table rather than defining its
    own, so every existing call site in that module keeps resolving the same names."""
    from elysium_pipeline.importers import materials, surface_classes

    for name in ("SURFACE_CLASSES", "SURFACE_CLASS_INDEX", "TOP_DIRECTORY_CLASSES", "FAMILY_DEFAULT_CLASS"):
        assert getattr(materials, name) is getattr(surface_classes, name), (
            "materials.%s is not the same object as surface_classes.%s" % (name, name)
        )
