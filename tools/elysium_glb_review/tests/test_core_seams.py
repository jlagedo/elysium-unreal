"""Contract tests for the seam extension readers."""

from __future__ import annotations

import pytest

from core import seams

from . import support


@pytest.mark.parametrize(
    ("expected", "payload"),
    [
        (seams.MODEL_EXTENSION, support.model_unit("vtmb:model:a/b")),
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


def test_a_model_names_its_materials_through_the_reference_extension() -> None:
    document = support.document_of(
        support.model_unit(
            "vtmb:model:npc/body",
            materials=["vtmb:material:a/one", "vtmb:material:a/two"],
        )
    )
    references = seams.material_references(document)
    identities = [reference.identity for reference in references]
    assert identities == ["vtmb:material:a/one", "vtmb:material:a/two"]
    assert references[0].origin == "materials[0]"


def test_the_reference_reader_reads_the_shape_the_exporters_write() -> None:
    # The fixtures are hand-built, so pin them to the writer the pipeline actually uses:
    # a reader keyed on any other field would find nothing in the exported corpus.
    from elysium_pipeline.formats.unit_contract import references as contract

    for name in seams.REFERENCE_EXTENSIONS:
        binding = contract.reference_extension(name, "vtmb:material:a/one")
        assert seams.reference_identity(binding, name) == "vtmb:material:a/one"
    document = support.document_of(
        support.model_unit("vtmb:model:npc/body", materials=["vtmb:material:a/one"])
    )
    assert document["materials"][0]["extensions"] == contract.reference_extension(
        seams.MATERIAL_REFERENCE_EXTENSION, "vtmb:material:a/one"
    )["extensions"]


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
    # A material writes the bare name where a model writes the full identity.
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


def test_every_declared_family_admits_only_root_extensions() -> None:
    for family, admitted in seams.SEAM_EXTENSION.items():
        assert admitted, family
        for name in admitted:
            assert name in seams.ROOT_EXTENSIONS, (family, name)


def test_all_extensions_covers_the_roots_and_the_cross_references() -> None:
    assert set(seams.ALL_EXTENSIONS) == set(seams.ROOT_EXTENSIONS) | set(
        seams.REFERENCE_EXTENSIONS
    )
    assert len(set(seams.ALL_EXTENSIONS)) == len(seams.ALL_EXTENSIONS)


def test_a_cross_reference_extension_is_never_mistaken_for_a_unit() -> None:
    # A material reference binds one glTF material; it does not make the file a unit.
    document = {"extensions": {seams.MATERIAL_REFERENCE_EXTENSION: {"asset": "vtmb:material:a"}}}
    assert seams.extension_of(document) is None


@pytest.mark.parametrize(
    ("directory", "expected"),
    [
        ("models", (seams.MODEL_EXTENSION,)),
        ("fonts", (seams.FONT_EXTENSION, seams.FONT_LIST_EXTENSION)),
        ("shader-programs/source", (seams.SHADER_SOURCE_EXTENSION,)),
        ("shader-programs/psh", (seams.SHADER_PROGRAM_EXTENSION,)),
        ("sentences", (seams.SOUND_SCRIPT_EXTENSION,)),
        ("dsp-presets", (seams.SOUND_SCRIPT_EXTENSION,)),
    ],
)
def test_a_family_directory_names_the_extensions_it_admits(
    directory: str, expected: tuple[str, ...]
) -> None:
    assert seams.expected_extensions(directory) == expected


def test_a_nested_family_answers_for_a_whole_unit_path() -> None:
    # The lookup takes the longest declared family that prefixes the path, so a unit
    # named by its own subtree resolves as readily as one named by its family alone.
    assert seams.expected_extensions("vdata/items/weapons") == (seams.VDATA_EXTENSION,)
    assert seams.expected_extensions("models/character/npc") == (seams.MODEL_EXTENSION,)


def test_all_four_map_units_share_one_family_directory() -> None:
    # A BSP is cut into four units written side by side under `maps/`, told apart by the
    # unit suffix in the file name rather than by directory.
    assert seams.expected_extensions("maps") == (
        seams.MAP_EXTENSION,
        seams.MAP_ENTITIES_EXTENSION,
        seams.MAP_LIGHTING_EXTENSION,
        seams.MAP_VISIBILITY_EXTENSION,
    )


@pytest.mark.parametrize(
    ("filename", "expected"),
    [
        ("ch_cloud_1.glb", seams.MAP_EXTENSION),
        ("ch_cloud_1.entities.glb", seams.MAP_ENTITIES_EXTENSION),
        ("ch_cloud_1.lighting.glb", seams.MAP_LIGHTING_EXTENSION),
        ("ch_cloud_1.visibility.glb", seams.MAP_VISIBILITY_EXTENSION),
    ],
)
def test_a_map_file_name_narrows_the_family_to_one_unit(
    filename: str, expected: str
) -> None:
    # Without the suffix the four kinds would cover for each other and a sub-unit written
    # under the wrong name would pass the seam check.
    assert seams.expected_extensions("maps", filename) == (expected,)


def test_an_undeclared_directory_carries_no_expectation() -> None:
    assert seams.expected_extensions("nowhere") == ()
    assert seams.expected_extensions("nowhere/nested") == ()


def test_the_export_root_itself_admits_the_corpus_index() -> None:
    # The corpus index is the one unit published under no family directory.
    assert seams.expected_extensions("") == (seams.CORPUS_INDEX_EXTENSION,)
