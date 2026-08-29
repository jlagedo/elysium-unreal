"""Contract tests for the seam extension readers."""

from __future__ import annotations

import pytest

from core import seams

from . import support


@pytest.mark.parametrize(
    ("expected", "payload"),
    [
        (seams.CHARACTER_EXTENSION, support.character_unit("vtmb:character-body:a/b")),
        (seams.MATERIAL_EXTENSION, support.material_unit("vtmb:material:a/b")),
        (seams.TEXTURE_EXTENSION, support.texture_unit()),
        (seams.SURFACE_PROPERTY_EXTENSION, support.surface_property_unit("brick")),
    ],
)
def test_each_seam_is_recognised_by_its_root_extension(expected: str, payload: bytes) -> None:
    document = support.document_of(payload)
    name, block = seams.extension_of(document)
    assert name == expected
    assert isinstance(block, dict)


def test_a_document_with_no_seam_extension_is_reported_as_such() -> None:
    assert seams.extension_of({"asset": {"version": "2.0"}}) is None
    assert seams.asset_id({"asset": {"version": "2.0"}}) is None


def test_the_identity_comes_from_the_extension_not_the_filename() -> None:
    document = support.document_of(support.material_unit("vtmb:material:brick/aspdra"))
    assert seams.asset_id(document) == "vtmb:material:brick/aspdra"


def test_a_character_names_its_materials_through_the_reference_extension() -> None:
    document = support.document_of(
        support.character_unit(
            "vtmb:character-body:npc/body",
            materials=["vtmb:material:a/one", "vtmb:material:a/two"],
        )
    )
    references = seams.material_references(document)
    identities = [reference.identity for reference in references]
    assert identities == ["vtmb:material:a/one", "vtmb:material:a/two"]
    assert references[0].origin == "materials[0]"


def test_a_material_names_its_textures_and_its_surface_property() -> None:
    document = support.document_of(
        support.material_unit(
            "vtmb:material:glass/pane",
            textures={"$basetexture": "glass/glassb", "$bumpmap": "glass/glassn"},
            surface_property="glass",
        )
    )
    references = seams.outgoing_references(document)
    by_role = {}
    for reference in references:
        by_role.setdefault(reference.role, []).append(reference.identity)
    assert "vtmb:texture:glass/glassb" in by_role["texture"]
    assert "vtmb:surface-property:glass" in by_role["surface-property"]


def test_a_bare_surface_property_name_is_promoted_to_an_identity() -> None:
    # A material writes the bare name where a character writes the full identity.
    document = support.document_of(
        support.material_unit("vtmb:material:a/b", surface_property="Glass")
    )
    _name, payload = seams.extension_of(document)
    reference = seams.surface_property_reference(payload)
    assert reference.identity == "vtmb:surface-property:glass"


def test_a_material_with_no_surface_property_names_none() -> None:
    document = support.document_of(support.material_unit("vtmb:material:a/b"))
    _name, payload = seams.extension_of(document)
    assert seams.surface_property_reference(payload) is None


def test_a_binding_with_no_asset_is_not_a_reference() -> None:
    # An engine-supplied render target never had a source file, so a binding that
    # names one is complete rather than dangling.
    document = support.document_of(support.material_unit("vtmb:material:a/b"))
    _name, payload = seams.extension_of(document)
    payload["textureBindings"] = [
        {"parameter": "$refracttexture", "value": "_rt_WaterRefraction",
         "kind": "render-target", "asset": None, "resolved": True}
    ]
    assert seams.texture_bindings(payload) == []


def test_export_time_resolution_is_carried_through() -> None:
    document = support.document_of(support.material_unit("vtmb:material:a/b"))
    _name, payload = seams.extension_of(document)
    payload["textureBindings"] = [
        {"parameter": "$basetexture", "value": "x", "kind": "texture",
         "asset": "vtmb:texture:x", "resolved": False}
    ]
    assert not seams.texture_bindings(payload)[0].resolved


@pytest.mark.parametrize("parameter", ["$bumpmap", "$NormalMap", "  $envmapmask "])
def test_data_parameters_are_recognised_as_non_colour(parameter: str) -> None:
    assert seams.is_non_color(parameter)


@pytest.mark.parametrize("parameter", ["$basetexture", "$iris", None, ""])
def test_colour_parameters_are_not(parameter: str | None) -> None:
    assert not seams.is_non_color(parameter)


def test_a_unit_that_accounted_for_everything_is_clean() -> None:
    coverage = seams.coverage({"coverage": {"mapped": [1, 2], "unresolved": [], "unsupported": []}})
    assert coverage.clean
    assert len(coverage.mapped) == 2


def test_unresolved_or_unsupported_rows_make_a_unit_unclean() -> None:
    assert not seams.coverage({"coverage": {"unresolved": [{}]}}).clean
    assert not seams.coverage({"coverage": {"unsupported": [{}]}}).clean


def test_a_missing_coverage_block_reads_as_empty_rather_than_failing() -> None:
    coverage = seams.coverage({})
    assert coverage.clean
    assert coverage.mapped == []
