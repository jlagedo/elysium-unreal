"""Contract tests for the browsable index.

The index derives identities from paths so that listing the corpus does not mean parsing
it. That shortcut is only sound because the export writes each unit at the path its
identity names, so these tests pin the mapping.
"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from core import index

from . import support


class ScanTests(unittest.TestCase):
    def setUp(self) -> None:
        self._scratch = tempfile.TemporaryDirectory()
        self.root = Path(self._scratch.name)
        self.addCleanup(self._scratch.cleanup)

    def _corpus(self, units: dict[str, bytes]) -> None:
        support.write_corpus(self.root, units)

    def test_derives_an_identity_from_each_path(self) -> None:
        self._corpus(
            {
                "characters/npc/body.glb": support.character_unit("vtmb:character-body:npc/body"),
                "materials/brick/aspdra.glb": support.material_unit("vtmb:material:brick/aspdra"),
                "textures/brick/aspdra.glb": support.texture_unit(),
                "surface-properties/brick.glb": support.surface_property_unit("brick"),
            }
        )
        found = {unit.identity for unit in index.scan(self.root)}
        self.assertEqual(
            found,
            {
                "vtmb:character-body:npc/body",
                "vtmb:material:brick/aspdra",
                "vtmb:texture:brick/aspdra",
                "vtmb:surface-property:brick",
            },
        )

    def test_a_bank_is_listed_as_the_character_body_it_is_stored_as(self) -> None:
        # A bank is exported through the character exporter, so its file identity says
        # character-body where a reference to it says animation-bank.
        self._corpus(
            {
                "characters/shared/female/frenzy.glb": support.character_unit(
                    "vtmb:character-body:shared/female/frenzy", animations=13
                )
            }
        )
        unit = index.scan(self.root)[0]
        self.assertEqual(unit.identity, "vtmb:character-body:shared/female/frenzy")
        self.assertEqual(unit.stem, "shared/female/frenzy")

    def test_a_seam_can_be_listed_alone(self) -> None:
        self._corpus(
            {
                "characters/npc/body.glb": support.character_unit("vtmb:character-body:npc/body"),
                "materials/a/b.glb": support.material_unit("vtmb:material:a/b"),
            }
        )
        units = index.scan(self.root, "materials")
        self.assertEqual([unit.seam for unit in units], ["materials"])

    def test_units_carry_their_size_without_being_parsed(self) -> None:
        self._corpus({"materials/a/b.glb": support.material_unit("vtmb:material:a/b")})
        unit = index.scan(self.root)[0]
        self.assertEqual(unit.byte_size, (self.root / unit.relative).stat().st_size)

    def test_the_listing_is_ordered_so_a_browser_is_stable(self) -> None:
        self._corpus(
            {
                "materials/z/last.glb": support.material_unit("vtmb:material:z/last"),
                "materials/a/first.glb": support.material_unit("vtmb:material:a/first"),
                "characters/npc/body.glb": support.character_unit("vtmb:character-body:npc/body"),
            }
        )
        units = index.scan(self.root)
        self.assertEqual(
            [unit.identity for unit in units],
            [
                "vtmb:character-body:npc/body",
                "vtmb:material:a/first",
                "vtmb:material:z/last",
            ],
        )

    def test_groups_are_the_first_path_segment(self) -> None:
        self._corpus(
            {
                "characters/npc/a.glb": support.character_unit("vtmb:character-body:npc/a"),
                "characters/monster/b.glb": support.character_unit("vtmb:character-body:monster/b"),
                "surface-properties/brick.glb": support.surface_property_unit("brick"),
            }
        )
        units = index.scan(self.root)
        self.assertEqual(index.groups(units), ["monster", "npc"])
        # A flat seam has no subtrees to group by.
        flat = [unit for unit in units if unit.seam == "surface-properties"][0]
        self.assertEqual(flat.group, "")

    def test_a_missing_seam_directory_is_not_an_error(self) -> None:
        self._corpus({"materials/a/b.glb": support.material_unit("vtmb:material:a/b")})
        self.assertEqual(len(index.scan(self.root)), 1)

    def test_an_empty_corpus_lists_nothing(self) -> None:
        self.assertEqual(index.scan(self.root), [])


class DetailTests(unittest.TestCase):
    def setUp(self) -> None:
        self._scratch = tempfile.TemporaryDirectory()
        self.root = Path(self._scratch.name)
        self.addCleanup(self._scratch.cleanup)

    def _detail(self, units: dict[str, bytes], relative: str) -> index.Details:
        support.write_corpus(self.root, units)
        unit = next(u for u in index.scan(self.root) if u.relative == relative)
        return index.details(unit, self.root)

    def _rows(self, detail: index.Details) -> dict:
        return dict(detail.rows)

    def test_a_character_reports_what_it_carries(self) -> None:
        detail = self._detail(
            {
                "characters/npc/body.glb": support.character_unit(
                    "vtmb:character-body:npc/body",
                    materials=["vtmb:material:a/one", "vtmb:material:a/two"],
                    animations=7,
                    bones=["Bip01", "Bip01 Pelvis"],
                )
            },
            "characters/npc/body.glb",
        )
        rows = self._rows(detail)
        self.assertEqual(rows["bones"], "2")
        self.assertEqual(rows["clips"], "7")
        self.assertEqual(rows["materials"], "2")

    def test_sentinel_slots_are_counted_as_a_fact(self) -> None:
        detail = self._detail(
            {
                "characters/npc/body.glb": support.character_unit(
                    "vtmb:character-body:npc/body",
                    materials=["vtmb:missing-material:9:glint", "vtmb:material:a/one"],
                )
            },
            "characters/npc/body.glb",
        )
        self.assertEqual(self._rows(detail)["slots with no VMT"], "1")
        self.assertEqual(detail.warnings, ())

    def test_a_bank_that_carries_no_clips_is_called_a_stub(self) -> None:
        detail = self._detail(
            {
                "characters/shared/all.glb": support.character_unit(
                    "vtmb:character-body:shared/all",
                    banks=["vtmb:animation-bank:shared/real"],
                    animations=0,
                )
            },
            "characters/shared/all.glb",
        )
        self.assertTrue(any("include stub" in warning for warning in detail.warnings))

    def test_a_material_reports_its_shader_and_bindings(self) -> None:
        detail = self._detail(
            {
                "materials/a/b.glb": support.material_unit(
                    "vtmb:material:a/b",
                    shader="lightmappedgeneric",
                    textures={"$basetexture": "a/t"},
                    surface_property="glass",
                )
            },
            "materials/a/b.glb",
        )
        rows = self._rows(detail)
        self.assertEqual(rows["shader"], "lightmappedgeneric")
        self.assertEqual(rows["textures"], "1")
        self.assertEqual(rows["surface property"], "glass")

    def test_an_untranscribed_shader_warns(self) -> None:
        detail = self._detail(
            {"materials/a/b.glb": support.material_unit("vtmb:material:a/b", resolved=False)},
            "materials/a/b.glb",
        )
        self.assertTrue(any("selector" in w for w in detail.warnings))

    def test_a_material_anomaly_warns(self) -> None:
        detail = self._detail(
            {
                "materials/a/b.glb": support.material_unit(
                    "vtmb:material:a/b", anomalies=[{"role": "valueless-key"}]
                )
            },
            "materials/a/b.glb",
        )
        self.assertTrue(any("valueless-key" in w for w in detail.warnings))

    def test_a_surface_property_that_inherits_says_so(self) -> None:
        detail = self._detail(
            {"surface-properties/brick.glb": support.surface_property_unit("brick", base="concrete")},
            "surface-properties/brick.glb",
        )
        rows = self._rows(detail)
        self.assertEqual(rows["base"], "concrete")
        self.assertEqual(rows["physics"], "declares none; inherited")

    def test_a_unit_that_vanished_reports_nothing_rather_than_raising(self) -> None:
        unit = index.Unit("materials", "materials/gone.glb", "vtmb:material:gone", 0)
        self.assertIsNone(index.details(unit, self.root))


if __name__ == "__main__":
    unittest.main()
