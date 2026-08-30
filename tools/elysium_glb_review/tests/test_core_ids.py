"""Contract tests for asset identities and their resolution.

The two irregular rules are the point of this module: a material's surface property
arrives as a bare name, and some identities name nothing at all by design.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from core import ids

ROOT = Path("C:/corpus") if Path("C:/").drive else Path("/corpus")


def test_parses_kind_and_path() -> None:
    asset = ids.parse("vtmb:material:models/character/teeth/molar")
    assert asset.kind == "material"
    assert asset.path == "models/character/teeth/molar"


def test_sentinel_keeps_its_extra_field_in_the_path() -> None:
    asset = ids.parse("vtmb:missing-material:9:malkgirlhead")
    assert asset.kind == "missing-material"
    assert asset.path == "9:malkgirlhead"
    assert asset.is_sentinel
    assert not asset.resolvable


@pytest.mark.parametrize("text", ["", "models/foo", "vtmb:", "vtmb:material", "other:material:x"])
def test_rejects_strings_that_are_not_identities(text: str) -> None:
    assert ids.parse(text) is None


@pytest.mark.parametrize(
    ("identity", "expected"),
    [
        ("vtmb:material:brick/aspdra", "materials/brick/aspdra.glb"),
        ("vtmb:texture:brick/aspdra", "textures/brick/aspdra.glb"),
        ("vtmb:surface-property:brick", "surface-properties/brick.glb"),
        ("vtmb:model:monster/andrei/andrei", "models/monster/andrei/andrei.glb"),
    ],
)
def test_each_seam_resolves_into_its_own_directory(identity: str, expected: str) -> None:
    assert ids.resolve(identity, ROOT) == ROOT / expected


@pytest.mark.parametrize(
    "identity",
    [
        "vtmb:missing-material:9:malkgirlhead",
        "vtmb:sound:surfaces/step.wav",
        "vtmb:sound-script:impact",
        "vtmb:effect:blood",
    ],
)
def test_identities_that_name_no_product_resolve_to_nothing(identity: str) -> None:
    assert ids.resolve(identity, ROOT) is None


def test_resolution_does_not_depend_on_the_file_existing() -> None:
    # A missing file is a corpus finding; the two are reported separately.
    path = ids.resolve("vtmb:material:does/not/exist", ROOT)
    assert path is not None
    assert not path.exists()


def test_paths_that_differ_only_below_the_stem_stay_distinct() -> None:
    # 1189 stems repeat across materials/; black.glb alone exists nine times, so a
    # basename is not an identity.
    first = ids.resolve("vtmb:material:effects/black", ROOT)
    second = ids.resolve("vtmb:material:tools/black", ROOT)
    assert first != second
    assert first.name == second.name


def test_a_bare_name_becomes_the_same_identity_a_model_writes() -> None:
    assert ids.surface_property_id("Glass") == "vtmb:surface-property:glass"
    assert ids.surface_property_id("  brick ") == "vtmb:surface-property:brick"


@pytest.mark.parametrize("value", ["_rt_WaterRefraction", "_RT_Camera", "/_rt_x"])
def test_render_targets_are_recognised(value: str) -> None:
    assert ids.is_render_target(value)


@pytest.mark.parametrize("value", ["glass/glassb", "models/_rtsomething"])
def test_ordinary_texture_paths_are_not_render_targets(value: str) -> None:
    assert not ids.is_render_target(value)


def test_the_root_is_the_parent_of_the_seam_directory() -> None:
    # A unit knows its identity but not the corpus layout, so opening one file by
    # hand has to be enough to follow its references.
    found = ids.corpus_root_for(ROOT / "models/npc/unique/x/x.glb")
    assert found == ROOT.resolve()


@pytest.mark.parametrize("seam", sorted(set(ids.SEAM_DIRECTORY.values())))
def test_every_seam_directory_locates_the_same_root(seam: str) -> None:
    assert ids.corpus_root_for(ROOT / seam / "a" / "b.glb") == ROOT.resolve()


def test_the_nearest_seam_directory_wins() -> None:
    # A corpus nested inside a path that also mentions a seam name must not resolve
    # to the outer one.
    path = ROOT / "materials" / "inner" / "models" / "a" / "b.glb"
    assert ids.corpus_root_for(path) == (ROOT / "materials" / "inner").resolve()


def test_a_path_outside_any_corpus_infers_nothing() -> None:
    assert ids.corpus_root_for(ROOT / "elsewhere" / "b.glb") is None


@pytest.mark.parametrize(("generator", "seam"), sorted(ids.GENERATOR_SEAM.items()))
def test_generator_identifies_the_seam(generator: str, seam: str) -> None:
    document = {"asset": {"version": "2.0", "generator": generator}}
    assert ids.seam_of(document) == seam


def test_an_unknown_generator_identifies_nothing() -> None:
    assert ids.seam_of({"asset": {"generator": "Some Other Tool"}}) is None
    assert ids.seam_of({}) is None
