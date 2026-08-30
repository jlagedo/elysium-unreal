"""Contract tests for the corpus sweep.

The sweep is the tool's QA product, so the tests care most about what it refuses to
call a problem: a sentinel identity and a reference outside the corpus are complete
statements about the source, and reporting them as breakage would bury the 138 genuine
missing references the real corpus contains.
"""

from __future__ import annotations

import tempfile
from pathlib import Path

from core import report

from . import support


def _scan(root: Path, units: dict[str, bytes]) -> report.Report:
    support.write_corpus(root, units)
    return report.scan(root)


def test_a_corpus_that_holds_together_reports_nothing(tmp_path: Path) -> None:
    result = _scan(tmp_path,
        {
            "materials/a/b.glb": support.material_unit(
                "vtmb:material:a/b", textures={"$basetexture": "a/t"},
                surface_property="brick",
            ),
            "textures/a/t.glb": support.texture_unit(),
            "surface-properties/brick.glb": support.surface_property_unit("brick"),
        }
    )
    assert result.ok, [str(f) for f in result.findings]
    assert result.files["materials"] == 1


def test_a_reference_to_a_unit_that_does_not_exist_is_a_finding(tmp_path: Path) -> None:
    result = _scan(tmp_path,
        {
            "materials/a/b.glb": support.material_unit(
                "vtmb:material:a/b", textures={"$basetexture": "a/gone"}
            )
        }
    )
    kinds = {finding.kind for finding in result.findings}
    assert kinds == {"missing-reference"}
    assert any("vtmb:texture:a/gone" in finding.detail for finding in result.findings)


def test_one_missing_unit_named_twice_is_one_finding(tmp_path: Path) -> None:
    # A material names each texture in both dependencies and textureBindings.
    result = _scan(tmp_path,
        {
            "materials/a/b.glb": support.material_unit(
                "vtmb:material:a/b", textures={"$basetexture": "a/gone"}
            )
        }
    )
    assert len(result.findings) == 1
    detail = result.findings[0].detail
    assert "dependencies[0]" in detail
    assert "textureBindings[0]" in detail


def test_a_sentinel_is_counted_as_a_fact_not_reported_as_breakage(tmp_path: Path) -> None:
    # 336 of 484 bodies carry sentinels; treating them as findings would drown the
    # report in the normal case.
    result = _scan(tmp_path,
        {
            "models/npc/body.glb": support.model_unit(
                "vtmb:model:npc/body",
                materials=["vtmb:missing-material:9:glint"],
            )
        }
    )
    assert result.ok, [str(f) for f in result.findings]
    assert result.counts["sentinel-slots"] == 1
    assert result.counts["bodies-with-sentinels"] == 1


def test_a_reference_to_a_seam_that_exports_nothing_is_a_fact(tmp_path: Path) -> None:
    # Surface properties name sound files; no seam exports sounds.
    units = {"surface-properties/brick.glb": support.surface_property_unit("brick")}
    support.write_corpus(tmp_path, units)
    path = tmp_path / "surface-properties/brick.glb"
    document = support.document_of(path.read_bytes())
    payload = document["extensions"]["ELYSIUM_vtmb_surface_property"]
    payload["dependencies"] = [
        {"role": "sound", "asset": "vtmb:sound:surfaces/step.wav", "sourcePath": ""}
    ]
    path.write_bytes(support.build_glb(document))

    result = report.scan(tmp_path)
    assert result.ok, [str(f) for f in result.findings]
    assert result.counts["reference-outside-corpus"] == 1


def test_a_material_anomaly_is_reported(tmp_path: Path) -> None:
    result = _scan(tmp_path,
        {
            "materials/a/b.glb": support.material_unit(
                "vtmb:material:a/b", anomalies=[{"role": "valueless-key", "key": "nomip"}]
            )
        }
    )
    assert [f.kind for f in result.findings] == ["material-anomaly"]
    assert "valueless-key" in result.findings[0].detail


def test_an_untranscribed_shader_is_counted_not_reported(tmp_path: Path) -> None:
    result = _scan(tmp_path,
        {
            "materials/a/b.glb": support.material_unit(
                "vtmb:material:a/b", shader="worlddiffusebumpmap", resolved=False
            )
        }
    )
    assert result.ok, [str(f) for f in result.findings]
    assert result.counts["shader-unresolved"] == 1
    assert result.counts["shader:worlddiffusebumpmap"] == 1


def test_a_unit_declaring_its_own_gaps_is_reported(tmp_path: Path) -> None:
    units = {"materials/a/b.glb": support.material_unit("vtmb:material:a/b")}
    support.write_corpus(tmp_path, units)
    path = tmp_path / "materials/a/b.glb"
    document = support.document_of(path.read_bytes())
    document["extensions"]["ELYSIUM_vtmb_material"]["coverage"] = {
        "unresolved": [{"path": "x"}],
        "unsupported": [{"key": "y"}],
    }
    path.write_bytes(support.build_glb(document))

    result = report.scan(tmp_path)
    assert {finding.kind for finding in result.findings} == {"coverage-unresolved", "coverage-unsupported"}


def test_a_texture_smaller_than_its_source_declared_is_reported(tmp_path: Path) -> None:
    units = {"textures/a/t.glb": support.texture_unit()}
    support.write_corpus(tmp_path, units)
    path = tmp_path / "textures/a/t.glb"
    payload_bytes = path.read_bytes()
    document = support.document_of(payload_bytes)
    extension = document["extensions"]["ELYSIUM_vtmb_texture"]
    extension["dimensions"] = {"width": 128, "height": 128}
    extension["sourceFormat"] = {"sourceWidth": 512, "sourceHeight": 512}
    path.write_bytes(support.build_glb(document))

    result = report.scan(tmp_path)
    assert [f.kind for f in result.findings] == ["texture-below-declared-size"]


def test_a_unit_that_will_not_parse_is_itself_the_finding(tmp_path: Path) -> None:
    support.write_corpus(tmp_path, {"materials/a/b.glb": b"not a glb"})
    result = report.scan(tmp_path)
    assert [f.kind for f in result.findings] == ["unreadable"]


def test_a_glb_without_a_seam_extension_is_reported(tmp_path: Path) -> None:
    support.write_corpus(
        tmp_path, {"materials/a/b.glb": support.build_glb({"asset": {"version": "2.0"}})}
    )
    result = report.scan(tmp_path)
    assert [f.kind for f in result.findings] == ["no-seam-extension"]


def test_a_unit_filed_under_the_wrong_seam_is_reported(tmp_path: Path) -> None:
    support.write_corpus(
        tmp_path, {"materials/a/b.glb": support.surface_property_unit("brick")}
    )
    result = report.scan(tmp_path)
    assert "seam-mismatch" in {finding.kind for finding in result.findings}


def test_an_empty_corpus_scans_cleanly(tmp_path: Path) -> None:
    result = report.scan(tmp_path)
    assert result.ok
    assert sum(result.files.values()) == 0


def test_the_summary_names_the_facts_even_when_nothing_is_wrong(tmp_path: Path) -> None:
    support.write_corpus(
        tmp_path, {"materials/a/b.glb": support.material_unit("vtmb:material:a/b")}
    )
    text = report.summary(report.scan(tmp_path))
    assert "no findings" in text
    assert "sentinel slots" in text


def test_the_summary_truncates_a_long_finding_list(tmp_path: Path) -> None:
    units = {
        "materials/a/b%d.glb" % index: support.material_unit(
            "vtmb:material:a/b%d" % index, textures={"$basetexture": "a/gone"}
        )
        for index in range(6)
    }
    support.write_corpus(tmp_path, units)
    text = report.summary(report.scan(tmp_path), max_findings=2)
    assert "... and 4 more" in text


def test_the_dictionary_form_is_json_shaped(tmp_path: Path) -> None:
    support.write_corpus(
        tmp_path,
        {"materials/a/b.glb": support.material_unit(
            "vtmb:material:a/b", textures={"$basetexture": "a/gone"})},
    )
    data = report.to_dict(report.scan(tmp_path))
    assert set(data) == {"root", "seconds", "files", "counts", "findings"}
    assert data["findings"][0]["kind"] == "missing-reference"
