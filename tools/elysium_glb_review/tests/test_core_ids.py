"""Contract tests for asset identities and their resolution.

The three irregular rules are the point of this module: a bank resolves under
`characters/`, a material's surface property arrives as a bare name, and some identities
name nothing at all by design.
"""

from __future__ import annotations

import unittest
from pathlib import Path

from core import ids

ROOT = Path("C:/corpus") if Path("C:/").drive else Path("/corpus")


class ParseTests(unittest.TestCase):
    def test_parses_kind_and_path(self) -> None:
        asset = ids.parse("vtmb:material:models/character/teeth/molar")
        assert asset.kind == "material"
        assert asset.path == "models/character/teeth/molar"

    def test_sentinel_keeps_its_extra_field_in_the_path(self) -> None:
        asset = ids.parse("vtmb:missing-material:9:malkgirlhead")
        assert asset.kind == "missing-material"
        assert asset.path == "9:malkgirlhead"
        assert asset.is_sentinel
        assert not asset.resolvable

    def test_rejects_strings_that_are_not_identities(self) -> None:
        for text in ("", "models/foo", "vtmb:", "vtmb:material", "other:material:x"):
            with self.subTest(text=text):
                assert ids.parse(text) is None


class ResolutionTests(unittest.TestCase):
    def test_each_seam_resolves_into_its_own_directory(self) -> None:
        cases = {
            "vtmb:material:brick/aspdra": Path("materials/brick/aspdra.glb"),
            "vtmb:texture:brick/aspdra": Path("textures/brick/aspdra.glb"),
            "vtmb:surface-property:brick": Path("surface-properties/brick.glb"),
            "vtmb:character-body:monster/andrei/andrei": Path(
                "characters/monster/andrei/andrei.glb"
            ),
        }
        for identity, expected in cases.items():
            with self.subTest(identity=identity):
                assert ids.resolve(identity, ROOT) == ROOT / expected

    def test_an_animation_bank_resolves_under_characters(self) -> None:
        # Banks are exported through the character exporter, so there is no
        # animation-banks directory to look in.
        assert ids.resolve("vtmb:animation-bank:shared/female/frenzy", ROOT) == ROOT / "characters/shared/female/frenzy.glb"

    def test_identities_that_name_no_product_resolve_to_nothing(self) -> None:
        for identity in (
            "vtmb:missing-material:9:malkgirlhead",
            "vtmb:sound:surfaces/step.wav",
            "vtmb:sound-script:impact",
            "vtmb:effect:blood",
        ):
            with self.subTest(identity=identity):
                assert ids.resolve(identity, ROOT) is None

    def test_resolution_does_not_depend_on_the_file_existing(self) -> None:
        # A missing file is a corpus finding; the two are reported separately.
        path = ids.resolve("vtmb:material:does/not/exist", ROOT)
        assert path is not None
        assert not path.exists()

    def test_paths_that_differ_only_below_the_stem_stay_distinct(self) -> None:
        # 1189 stems repeat across materials/; black.glb alone exists nine times, so a
        # basename is not an identity.
        first = ids.resolve("vtmb:material:effects/black", ROOT)
        second = ids.resolve("vtmb:material:tools/black", ROOT)
        assert first != second
        assert first.name == second.name


class SurfacePropertyNameTests(unittest.TestCase):
    def test_a_bare_name_becomes_the_same_identity_a_character_writes(self) -> None:
        assert ids.surface_property_id("Glass") == "vtmb:surface-property:glass"
        assert ids.surface_property_id("  brick ") == "vtmb:surface-property:brick"


class RenderTargetTests(unittest.TestCase):
    def test_render_targets_are_recognised(self) -> None:
        for value in ("_rt_WaterRefraction", "_RT_Camera", "/_rt_x"):
            with self.subTest(value=value):
                assert ids.is_render_target(value)

    def test_ordinary_texture_paths_are_not_render_targets(self) -> None:
        for value in ("glass/glassb", "models/_rtsomething"):
            with self.subTest(value=value):
                assert not ids.is_render_target(value)


class CorpusRootTests(unittest.TestCase):
    def test_the_root_is_the_parent_of_the_seam_directory(self) -> None:
        # A unit knows its identity but not the corpus layout, so opening one file by
        # hand has to be enough to follow its references.
        found = ids.corpus_root_for(ROOT / "characters/npc/unique/x/x.glb")
        assert found == ROOT.resolve()

    def test_every_seam_directory_locates_the_same_root(self) -> None:
        for seam in set(ids.SEAM_DIRECTORY.values()):
            with self.subTest(seam=seam):
                assert ids.corpus_root_for(ROOT / seam / "a" / "b.glb") == ROOT.resolve()

    def test_the_nearest_seam_directory_wins(self) -> None:
        # A corpus nested inside a path that also mentions a seam name must not resolve
        # to the outer one.
        path = ROOT / "materials" / "inner" / "characters" / "a" / "b.glb"
        assert ids.corpus_root_for(path) == (ROOT / "materials" / "inner").resolve()

    def test_a_path_outside_any_corpus_infers_nothing(self) -> None:
        assert ids.corpus_root_for(ROOT / "elsewhere" / "b.glb") is None


class SeamDetectionTests(unittest.TestCase):
    def test_generator_identifies_the_seam(self) -> None:
        for generator, seam in ids.GENERATOR_SEAM.items():
            with self.subTest(generator=generator):
                document = {"asset": {"version": "2.0", "generator": generator}}
                assert ids.seam_of(document) == seam

    def test_an_unknown_generator_identifies_nothing(self) -> None:
        assert ids.seam_of({"asset": {"generator": "Some Other Tool"}}) is None
        assert ids.seam_of({}) is None
