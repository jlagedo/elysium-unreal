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


#: One `UPROPERTY(<specifiers>) float Name = Value;` declaration, in `ElysiumSurfaceSettings.h`.
#: `(?:[^()]|\([^()]*\))*` captures the specifier list allowing exactly one level of nested
#: parens (`meta = (ClampMin = "0.0", ...)`) without matching past the macro's own closing paren;
#: `re.DOTALL` lets the specifier list -- or the gap between `UPROPERTY(...)` and `float` -- wrap
#: across lines, and `EditAnywhere`/`Config` are checked as separate substrings below rather than
#: pinned to one order, so reordering the specifier list (`Config, EditAnywhere, ...`) or wrapping
#: the macro onto two lines still parses. Matches the same 15 scalars `ScalarBindings()` pins, so
#: this and `_cpp_scalar_bindings()` agree on which fields count as a "knob" even though they read
#: different files.
_HEADER_FIELD_RE = re.compile(
    r"UPROPERTY\(((?:[^()]|\([^()]*\))*)\)\s*float\s+(\w+)\s*=\s*([-\d.]+)f?;", re.DOTALL,
)
_CONFIG_SPECIFIER_RE = re.compile(r"(?<![\w])Config(?![\w])")


def _cpp_header_scalar_defaults() -> dict[str, float]:
    text = SETTINGS_H.read_text(encoding="utf-8")
    fields = {}
    for specifiers, name, value in _HEADER_FIELD_RE.findall(text):
        if "EditAnywhere" in specifiers and _CONFIG_SPECIFIER_RE.search(specifiers):
            fields[name] = float(value)
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


#: `ScalarBindings()` names that `make_v2_materials.py` legitimately never reads through
#: `g.mpc("<Name>", ...)` -- each one is consumed somewhere other than a `M_V2_*` material graph
#: node, so its absence from that file is not the same defect a genuinely dropped knob would be.
_MAKE_V2_MATERIALS_UNREAD_SCALAR_BINDINGS = {
    "LightSpecularScale": (
        "applied by the runtime lighting path (`FElysiumSurfaceSettings`'s C++ consumer), never "
        "sampled as a material-graph node"
    ),
    "ChromaThreshold": (
        "read offline, in Python, by `importers/materials.py::_read_chroma_threshold` (the "
        "envmaptint grey/chromatic split happens at stage time, not in the material graph)"
    ),
    "DecalDepthOffset": (
        "consumed by the decal placement lane (SF placement code), not a `M_V2_Decal` graph node"
    ),
    "CaptureRadius": (
        "consumed by the reflection-capture actor placement lane (SF-6.2), not a material graph "
        "node"
    ),
}


def test_every_scalar_binding_is_read_by_make_v2_materials_or_named_as_unread():
    """Every `ScalarBindings()` name is either an `mpc("<Name>", ...)` node in
    `make_v2_materials.py` (the nine generated masters' own file, not owned by this test file's
    review pass) or is named in `_MAKE_V2_MATERIALS_UNREAD_SCALAR_BINDINGS` with a reason -- so a
    knob silently dropped from every master's graph fails here instead of only showing up as an
    inert `MPC_ElysiumSurfaces` row nothing samples."""
    make_v2_materials = REPO / "pipeline" / "unreal" / "make_v2_materials.py"
    text = make_v2_materials.read_text(encoding="utf-8")
    read_names = set(re.findall(r'g\.mpc\("(\w+)"', text))
    cpp_names = _cpp_scalar_bindings()
    assert cpp_names, "no scalar bindings parsed from ElysiumSurfaceSettings.cpp"
    unaccounted = cpp_names - read_names - set(_MAKE_V2_MATERIALS_UNREAD_SCALAR_BINDINGS)
    assert not unaccounted, (
        "ScalarBindings() name(s) %r are neither read via g.mpc(...) in make_v2_materials.py nor "
        "named in _MAKE_V2_MATERIALS_UNREAD_SCALAR_BINDINGS" % sorted(unaccounted)
    )
    # And the skip-list itself never grows stale: every name in it is a real binding, and it
    # never claims a name that actually is read.
    stale = set(_MAKE_V2_MATERIALS_UNREAD_SCALAR_BINDINGS) - cpp_names
    assert not stale, "skip-listed name(s) %r are not ScalarBindings() names at all" % sorted(stale)
    wrongly_skipped = set(_MAKE_V2_MATERIALS_UNREAD_SCALAR_BINDINGS) & read_names
    assert not wrongly_skipped, (
        "skip-listed name(s) %r are actually read via g.mpc(...) -- remove them from the skip list"
        % sorted(wrongly_skipped)
    )


def test_materials_reexports_surface_classes():
    """`elysium_pipeline.importers.materials` re-exports the shared table rather than defining its
    own, so every existing call site in that module keeps resolving the same names."""
    from elysium_pipeline.importers import materials, surface_classes

    for name in ("SURFACE_CLASSES", "SURFACE_CLASS_INDEX", "TOP_DIRECTORY_CLASSES", "FAMILY_DEFAULT_CLASS"):
        assert getattr(materials, name) is getattr(surface_classes, name), (
            "materials.%s is not the same object as surface_classes.%s" % (name, name)
        )


# --- C-1: make_collection() heals duplicate rows against a fake unreal.Name hash bug ------------


def _fake_name_hash_unreal():
    """A minimal fake `unreal` module: a `Name`-like value whose `__hash__` deliberately diverges
    from `hash(str(self))`, the same shape of divergence a real `unreal.Name` has against a plain
    Python str (`unreal.Name.__hash__` is not `str.__hash__`). Building `have` from these objects
    directly (the pre-C-1 code) makes `name not in have` always true for a `SCALAR_NAMES` str, so
    every run appended all 16 rows again regardless of what already existed on disk."""
    from types import SimpleNamespace

    class FakeName:
        def __init__(self, text):
            self._text = text

        def __str__(self):
            return self._text

        def __eq__(self, other):
            return str(other) == self._text

        def __hash__(self):
            return id(self)  # deliberately never equal to hash(str(self))

    class FakeScalarParameter:
        def __init__(self, name=None, value=0.0):
            self.props = {}
            if name is not None:
                self.props["parameter_name"] = FakeName(name)
                self.props["default_value"] = value

        def get_editor_property(self, key):
            return self.props[key]

        def set_editor_property(self, key, value):
            self.props[key] = value

    class FakeCollection:
        def __init__(self, rows):
            self.props = {"scalar_parameters": list(rows)}

        def get_editor_property(self, key):
            return self.props[key]

        def set_editor_property(self, key, value):
            self.props[key] = value

    saves = []
    fake = SimpleNamespace(
        log=lambda *a, **k: None,
        log_warning=lambda *a, **k: None,
        log_error=lambda *a, **k: None,
        AssetToolsHelpers=SimpleNamespace(get_asset_tools=lambda: SimpleNamespace(create_asset=None)),
        CollectionScalarParameter=FakeScalarParameter,
        EditorAssetLibrary=SimpleNamespace(
            save_asset=lambda path, only_if_is_dirty=False: (saves.append(path) or True),
            does_asset_exist=lambda path: False,
            delete_asset=lambda path: True,
        ),
    )
    return fake, FakeName, FakeScalarParameter, FakeCollection, saves


def _load_make_surface_knobs(fake_unreal):
    import importlib.util
    import sys
    from unittest import mock

    spec = importlib.util.spec_from_file_location(
        "elysium_test_make_surface_knobs", GENERATOR)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"unreal": fake_unreal}):
        spec.loader.exec_module(module)  # guarded by `if __name__ == "__main__":`; main() does not run
    module.unreal = fake_unreal
    return module


def test_c1_make_collection_heals_duplicate_and_suffixed_rows():
    fake, FakeName, FakeScalarParameter, FakeCollection, saves = _fake_name_hash_unreal()
    module = _load_make_surface_knobs(fake)

    rows = []
    for name in module.SCALAR_NAMES:
        rows.append(FakeScalarParameter(name, 1.0))
    # A duplicate row for a known knob, from an earlier buggy run -- first occurrence's value
    # (1.0) must survive, this later duplicate's (9.0) must not.
    rows.append(FakeScalarParameter(module.SCALAR_NAMES[0], 9.0))
    # A numbered-suffix ghost row for a different known knob.
    rows.append(FakeScalarParameter(module.SCALAR_NAMES[1] + "1", 5.0))
    # A row this generator does not own -- must survive untouched.
    rows.append(FakeScalarParameter("SomeUnrelatedScalar", 42.0))

    collection = FakeCollection(rows)
    asset_path = "%s/%s" % (module.PKG, module.COLLECTION_NAME)
    fake.load_asset = lambda path: collection if path == asset_path else None

    result = module.make_collection()
    assert result is collection

    healed = list(collection.get_editor_property("scalar_parameters"))
    names = [str(p.get_editor_property("parameter_name")) for p in healed]

    # Exactly one row per known knob, no duplicates and no numbered-suffix ghosts.
    for name in module.SCALAR_NAMES:
        assert names.count(name) == 1, "expected exactly one row for %s, found %r" % (name, names)
    assert (module.SCALAR_NAMES[1] + "1") not in names

    # The first occurrence's value survived, the duplicate's did not.
    first_row = healed[names.index(module.SCALAR_NAMES[0])]
    assert first_row.get_editor_property("default_value") == 1.0

    # An unrelated row is untouched.
    assert "SomeUnrelatedScalar" in names

    # The heal pass actually saved the asset (there was something to heal).
    assert asset_path in saves
