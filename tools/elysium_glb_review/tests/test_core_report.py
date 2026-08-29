"""Contract tests for the corpus sweep.

The sweep is the tool's QA product, so the tests care most about what it refuses to
call a problem: a sentinel identity and a reference outside the corpus are complete
statements about the source, and reporting them as breakage would bury the 138 genuine
missing references the real corpus contains.
"""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from core import report

from . import support


class ScanTests(unittest.TestCase):
    def setUp(self) -> None:
        self._scratch = tempfile.TemporaryDirectory()
        self.root = Path(self._scratch.name)
        self.addCleanup(self._scratch.cleanup)

    def _scan(self, units: dict[str, bytes]) -> report.Report:
        support.write_corpus(self.root, units)
        return report.scan(self.root)

    def test_a_corpus_that_holds_together_reports_nothing(self) -> None:
        result = self._scan(
            {
                "materials/a/b.glb": support.material_unit(
                    "vtmb:material:a/b", textures={"$basetexture": "a/t"},
                    surface_property="brick",
                ),
                "textures/a/t.glb": support.texture_unit(),
                "surface-properties/brick.glb": support.surface_property_unit("brick"),
            }
        )
        self.assertTrue(result.ok, [str(f) for f in result.findings])
        self.assertEqual(result.files["materials"], 1)

    def test_a_reference_to_a_unit_that_does_not_exist_is_a_finding(self) -> None:
        result = self._scan(
            {
                "materials/a/b.glb": support.material_unit(
                    "vtmb:material:a/b", textures={"$basetexture": "a/gone"}
                )
            }
        )
        kinds = {finding.kind for finding in result.findings}
        self.assertEqual(kinds, {"missing-reference"})
        self.assertTrue(
            any("vtmb:texture:a/gone" in finding.detail for finding in result.findings)
        )

    def test_one_missing_unit_named_twice_is_one_finding(self) -> None:
        # A material names each texture in both dependencies and textureBindings.
        result = self._scan(
            {
                "materials/a/b.glb": support.material_unit(
                    "vtmb:material:a/b", textures={"$basetexture": "a/gone"}
                )
            }
        )
        self.assertEqual(len(result.findings), 1)
        detail = result.findings[0].detail
        self.assertIn("dependencies[0]", detail)
        self.assertIn("textureBindings[0]", detail)

    def test_a_sentinel_is_counted_as_a_fact_not_reported_as_breakage(self) -> None:
        # 336 of 484 bodies carry sentinels; treating them as findings would drown the
        # report in the normal case.
        result = self._scan(
            {
                "characters/npc/body.glb": support.character_unit(
                    "vtmb:character-body:npc/body",
                    materials=["vtmb:missing-material:9:glint"],
                )
            }
        )
        self.assertTrue(result.ok, [str(f) for f in result.findings])
        self.assertEqual(result.counts["sentinel-slots"], 1)
        self.assertEqual(result.counts["bodies-with-sentinels"], 1)

    def test_a_reference_to_a_seam_that_exports_nothing_is_a_fact(self) -> None:
        # Surface properties name sound files; no seam exports sounds.
        units = {"surface-properties/brick.glb": support.surface_property_unit("brick")}
        support.write_corpus(self.root, units)
        path = self.root / "surface-properties/brick.glb"
        document = support.document_of(path.read_bytes())
        payload = document["extensions"]["ELYSIUM_vtmb_surface_property"]
        payload["dependencies"] = [
            {"role": "sound", "asset": "vtmb:sound:surfaces/step.wav", "sourcePath": ""}
        ]
        path.write_bytes(support.build_glb(document))

        result = report.scan(self.root)
        self.assertTrue(result.ok, [str(f) for f in result.findings])
        self.assertEqual(result.counts["reference-outside-corpus"], 1)

    def test_a_material_anomaly_is_reported(self) -> None:
        result = self._scan(
            {
                "materials/a/b.glb": support.material_unit(
                    "vtmb:material:a/b", anomalies=[{"role": "valueless-key", "key": "nomip"}]
                )
            }
        )
        self.assertEqual([f.kind for f in result.findings], ["material-anomaly"])
        self.assertIn("valueless-key", result.findings[0].detail)

    def test_an_untranscribed_shader_is_counted_not_reported(self) -> None:
        result = self._scan(
            {
                "materials/a/b.glb": support.material_unit(
                    "vtmb:material:a/b", shader="worlddiffusebumpmap", resolved=False
                )
            }
        )
        self.assertTrue(result.ok, [str(f) for f in result.findings])
        self.assertEqual(result.counts["shader-unresolved"], 1)
        self.assertEqual(result.counts["shader:worlddiffusebumpmap"], 1)

    def test_a_unit_declaring_its_own_gaps_is_reported(self) -> None:
        units = {"materials/a/b.glb": support.material_unit("vtmb:material:a/b")}
        support.write_corpus(self.root, units)
        path = self.root / "materials/a/b.glb"
        document = support.document_of(path.read_bytes())
        document["extensions"]["ELYSIUM_vtmb_material"]["coverage"] = {
            "unresolved": [{"path": "x"}],
            "unsupported": [{"key": "y"}],
        }
        path.write_bytes(support.build_glb(document))

        result = report.scan(self.root)
        self.assertEqual(
            {finding.kind for finding in result.findings},
            {"coverage-unresolved", "coverage-unsupported"},
        )

    def test_a_texture_smaller_than_its_source_declared_is_reported(self) -> None:
        units = {"textures/a/t.glb": support.texture_unit()}
        support.write_corpus(self.root, units)
        path = self.root / "textures/a/t.glb"
        payload_bytes = path.read_bytes()
        document = support.document_of(payload_bytes)
        extension = document["extensions"]["ELYSIUM_vtmb_texture"]
        extension["dimensions"] = {"width": 128, "height": 128}
        extension["sourceFormat"] = {"sourceWidth": 512, "sourceHeight": 512}
        path.write_bytes(support.build_glb(document))

        result = report.scan(self.root)
        self.assertEqual([f.kind for f in result.findings], ["texture-below-declared-size"])

    def test_a_unit_that_will_not_parse_is_itself_the_finding(self) -> None:
        support.write_corpus(self.root, {"materials/a/b.glb": b"not a glb"})
        result = report.scan(self.root)
        self.assertEqual([f.kind for f in result.findings], ["unreadable"])

    def test_a_glb_without_a_seam_extension_is_reported(self) -> None:
        support.write_corpus(
            self.root, {"materials/a/b.glb": support.build_glb({"asset": {"version": "2.0"}})}
        )
        result = report.scan(self.root)
        self.assertEqual([f.kind for f in result.findings], ["no-seam-extension"])

    def test_a_unit_filed_under_the_wrong_seam_is_reported(self) -> None:
        support.write_corpus(
            self.root, {"materials/a/b.glb": support.surface_property_unit("brick")}
        )
        result = report.scan(self.root)
        self.assertIn("seam-mismatch", {finding.kind for finding in result.findings})

    def test_an_empty_corpus_scans_cleanly(self) -> None:
        result = report.scan(self.root)
        self.assertTrue(result.ok)
        self.assertEqual(sum(result.files.values()), 0)


class OutputTests(unittest.TestCase):
    def setUp(self) -> None:
        self._scratch = tempfile.TemporaryDirectory()
        self.root = Path(self._scratch.name)
        self.addCleanup(self._scratch.cleanup)

    def test_the_summary_names_the_facts_even_when_nothing_is_wrong(self) -> None:
        support.write_corpus(
            self.root, {"materials/a/b.glb": support.material_unit("vtmb:material:a/b")}
        )
        text = report.summary(report.scan(self.root))
        self.assertIn("no findings", text)
        self.assertIn("sentinel slots", text)

    def test_the_summary_truncates_a_long_finding_list(self) -> None:
        units = {
            "materials/a/b%d.glb" % index: support.material_unit(
                "vtmb:material:a/b%d" % index, textures={"$basetexture": "a/gone"}
            )
            for index in range(6)
        }
        support.write_corpus(self.root, units)
        text = report.summary(report.scan(self.root), max_findings=2)
        self.assertIn("... and 4 more", text)

    def test_the_dictionary_form_is_json_shaped(self) -> None:
        support.write_corpus(
            self.root,
            {"materials/a/b.glb": support.material_unit(
                "vtmb:material:a/b", textures={"$basetexture": "a/gone"})},
        )
        data = report.to_dict(report.scan(self.root))
        self.assertEqual(set(data), {"root", "seconds", "files", "counts", "findings"})
        self.assertEqual(data["findings"][0]["kind"], "missing-reference")


if __name__ == "__main__":
    unittest.main()
