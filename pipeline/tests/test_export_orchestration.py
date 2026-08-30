from __future__ import annotations

import contextlib
from unittest import mock

import pytest

from elysium_pipeline import export_manager, wield_corpus
from elysium_pipeline.exporters import export_all


def test_texture_glb_corpus_admits_each_tth_identity_once() -> None:
    index = {
        "materials/a/brick.tth": object(),
        "materials/a/brick.ttz": object(),
        "materials/b/glass.tth": object(),
        "materials/b/glass.vmt": object(),
        "models/not-a-texture.tth": object(),
    }
    assert export_manager._texture_glb_sources(index) == ["a/brick", "b/glass"]


def test_all_texture_glbs_reuses_one_patch_first_index() -> None:
    from pathlib import Path
    from types import SimpleNamespace

    from elysium_pipeline.exporters import texture_glb
    from elysium_pipeline.formats import install
    from elysium_pipeline.validation import texture_glb as validation

    index = {
        "materials/a/brick.tth": object(),
        "materials/a/brick.ttz": object(),
        "materials/b/glass.tth": object(),
    }
    config = SimpleNamespace(
        game_root=Path("C:/game"),
        work_root=Path("C:/work"),
        export_root=Path("C:/export"),
        export_v2_root=Path("C:/export_v2"),
    )

    def write(_index, texture, output_root):
        assert _index is index
        return output_root / (texture.replace("/", "_") + ".glb")

    summary = {
        "asset": "vtmb:texture:test",
        "accountedBytes": 10,
        "sourceBytes": 10,
    }
    with (
        mock.patch.object(install, "build_index", return_value=index) as build_index,
        mock.patch.object(texture_glb, "export", side_effect=write) as export,
        mock.patch.object(validation, "validate", return_value=summary) as validate,
    ):
        destinations = export_manager.export_all_texture_glbs(
            config, object(), jobs=1
        )

    assert len(destinations) == 2
    build_index.assert_called_once_with()
    assert export.call_count == 2
    assert validate.call_count == 2


def test_material_glb_corpus_admits_each_addressable_vmt_once() -> None:
    index = {
        "materials/a/brick.vmt": object(),
        "materials/a/brick.tth": object(),
        "materials/b/glass.vmt": object(),
        # The engine composes `materials/<search path><name>.vmt`, so a VMT packed outside
        # `materials/` names no material and is not a unit.
        "models/character/monster/hengeyokai/hengeyokai_frozen.vmt": object(),
    }
    assert export_manager._material_glb_sources(index) == ["a/brick", "b/glass"]


def test_all_material_glbs_reuses_one_patch_first_index() -> None:
    from pathlib import Path
    from types import SimpleNamespace

    from elysium_pipeline.exporters import material_glb
    from elysium_pipeline.formats import install
    from elysium_pipeline.validation import material_glb as validation

    index = {
        "materials/a/brick.vmt": object(),
        "materials/b/glass.vmt": object(),
    }
    config = SimpleNamespace(
        game_root=Path("C:/game"),
        work_root=Path("C:/work"),
        export_root=Path("C:/export"),
        export_v2_root=Path("C:/export_v2"),
    )

    def write(_index, material, output_root):
        assert _index is index
        return output_root / (material.replace("/", "_") + ".glb")

    summary = {
        "asset": "vtmb:material:test",
        "accountedBytes": 10,
        "sourceBytes": 10,
        "anomalies": [],
        "missingTextures": [],
    }
    with (
        mock.patch.object(install, "build_index", return_value=index) as build_index,
        mock.patch.object(material_glb, "export", side_effect=write) as export,
        mock.patch.object(validation, "validate", return_value=summary) as validate,
    ):
        destinations = export_manager.export_all_material_glbs(
            config, object(), jobs=1
        )

    assert len(destinations) == 2
    build_index.assert_called_once_with()
    assert export.call_count == 2
    assert validate.call_count == 2


#: Every plural `export_v2` command that publishes one kind through the uniform seam workflow,
#: with the seam it drives. The composite seams -- map, sound-script, shader -- publish several
#: kinds each and are covered on their own below.
SINGLE_KIND_CORPORA = [
    (export_manager.export_all_image_glbs, export_manager.IMAGE_GLB),
    (export_manager.export_all_sound_glbs, export_manager.SOUND_GLB),
    (export_manager.export_all_expression_table_glbs, export_manager.EXPRESSION_TABLE_GLB),
    (export_manager.export_all_shader_source_glbs, export_manager.SHADER_SOURCE_GLB),
    (export_manager.export_all_shader_program_glbs, export_manager.SHADER_PROGRAM_GLB),
    (export_manager.export_all_particle_glbs, export_manager.PARTICLE_GLB),
    (export_manager.export_all_font_glbs, export_manager.FONT_GLB),
    (export_manager.export_all_sentence_glbs, export_manager.SENTENCE_GLB),
    (export_manager.export_all_dsp_preset_glbs, export_manager.DSP_PRESET_GLB),
    (export_manager.export_all_sound_scheme_glbs, export_manager.SOUND_SCHEME_GLB),
    (export_manager.export_all_scene_glbs, export_manager.SCENE_GLB),
    (export_manager.export_all_model_glbs, export_manager.MODEL_GLB),
    (export_manager.export_all_dialogue_glbs, export_manager.DIALOGUE_GLB),
    (export_manager.export_all_vdata_glbs, export_manager.VDATA_GLB),
    (export_manager.export_all_ui_resource_glbs, export_manager.UI_RESOURCE_GLB),
    (export_manager.export_all_script_glbs, export_manager.SCRIPT_GLB),
    (export_manager.export_all_map_entities_glbs, export_manager.MAP_ENTITIES_GLB),
    (export_manager.export_all_map_lighting_glbs, export_manager.MAP_LIGHTING_GLB),
    (export_manager.export_all_map_visibility_glbs, export_manager.MAP_VISIBILITY_GLB),
    (export_manager.export_all_nav_graph_glbs, export_manager.NAV_GRAPH_GLB),
    (export_manager.export_all_engine_config_glbs, export_manager.ENGINE_CONFIG_GLB),
]

#: Every singular `export_v2` command, with the seam it drives.
SINGLE_KIND_UNITS = [
    (export_manager.export_image_glb, export_manager.IMAGE_GLB),
    (export_manager.export_sound_glb, export_manager.SOUND_GLB),
    (export_manager.export_expression_table_glb, export_manager.EXPRESSION_TABLE_GLB),
    (export_manager.export_shader_source_glb, export_manager.SHADER_SOURCE_GLB),
    (export_manager.export_shader_program_glb, export_manager.SHADER_PROGRAM_GLB),
    (export_manager.export_particle_glb, export_manager.PARTICLE_GLB),
    (export_manager.export_font_glb, export_manager.FONT_GLB),
    (export_manager.export_sound_script_glb, export_manager.SOUND_SCRIPT_GLB),
    (export_manager.export_sentence_glb, export_manager.SENTENCE_GLB),
    (export_manager.export_dsp_preset_glb, export_manager.DSP_PRESET_GLB),
    (export_manager.export_sound_scheme_glb, export_manager.SOUND_SCHEME_GLB),
    (export_manager.export_scene_glb, export_manager.SCENE_GLB),
    (export_manager.export_model_glb, export_manager.MODEL_GLB),
    (export_manager.export_dialogue_glb, export_manager.DIALOGUE_GLB),
    (export_manager.export_vdata_glb, export_manager.VDATA_GLB),
    (export_manager.export_ui_resource_glb, export_manager.UI_RESOURCE_GLB),
    (export_manager.export_script_glb, export_manager.SCRIPT_GLB),
    (export_manager.export_map_entities_glb, export_manager.MAP_ENTITIES_GLB),
    (export_manager.export_map_lighting_glb, export_manager.MAP_LIGHTING_GLB),
    (export_manager.export_map_visibility_glb, export_manager.MAP_VISIBILITY_GLB),
    (export_manager.export_nav_graph_glb, export_manager.NAV_GRAPH_GLB),
    (export_manager.export_engine_config_glb, export_manager.ENGINE_CONFIG_GLB),
]


#: The family directory each `GlbUnitSeam` publishes below, written out. `_glb_seam_root` is
#: what builds that path, so a test that calls it cannot pin the name; these are the literal
#: directories the corpus is published under. `""` is the export root itself, which is where a
#: seam whose own `output_relative_path` already names its family directory publishes.
GLB_SEAM_FAMILY = {
    "IMAGE_GLB": "images",
    "SOUND_GLB": "sounds",
    "EXPRESSION_TABLE_GLB": "",
    "SHADER_SOURCE_GLB": "shader-programs",
    "SHADER_PROGRAM_GLB": "shader-programs",
    "PARTICLE_GLB": "particles",
    "FONT_GLB": "fonts",
    "FONT_LIST_GLB": "fonts",
    "SOUND_SCRIPT_GLB": "",
    "SOUND_SCRIPT_MANIFEST_GLB": "",
    "SOUNDSCAPE_GLB": "",
    "SENTENCE_GLB": "",
    "DSP_PRESET_GLB": "",
    "SOUND_SCHEME_GLB": "sound-schemes",
    "SCENE_GLB": "scenes",
    "MODEL_GLB": "models",
    "DIALOGUE_GLB": "dialogues",
    "VDATA_GLB": "vdata",
    "UI_RESOURCE_GLB": "",
    "SCRIPT_GLB": "scripts",
    "MAP_GLB": "",
    "MAP_ENTITIES_GLB": "",
    "MAP_LIGHTING_GLB": "",
    "MAP_VISIBILITY_GLB": "",
    "NAV_GRAPH_GLB": "nav-graphs",
    "ENGINE_CONFIG_GLB": "engine-config",
}


def _seam_name(seam) -> str:
    """The module-level name a seam is declared under, which is how the table above keys it."""

    for name, value in _declared_glb_seams().items():
        if value is seam:
            return name
    raise AssertionError(f"{seam.label} is declared under no module-level name")


def _declared_glb_seams():
    """Every `GlbUnitSeam` the module declares, by the name it is published under."""

    return {
        name: value
        for name, value in vars(export_manager).items()
        if isinstance(value, export_manager.GlbUnitSeam)
    }


def _review_seam_directories() -> set[str]:
    """The corpus directories the GLB reviewer knows, keyed the way it keys them."""

    import sys
    from pathlib import Path

    tools = Path(__file__).resolve().parents[2] / "tools"
    if str(tools) not in sys.path:
        sys.path.insert(0, str(tools))
    from elysium_glb_review.core.seams import SEAM_EXTENSION

    return set(SEAM_EXTENSION)


def test_every_glb_seam_names_the_family_directory_it_publishes_below() -> None:
    config = _glb_config()
    seams = _declared_glb_seams()
    assert sorted(seams) == sorted(GLB_SEAM_FAMILY)
    for name, seam in sorted(seams.items()):
        family = GLB_SEAM_FAMILY[name]
        assert seam.family == family, f"{name} publishes below {seam.family!r}"
        expected = config.export_v2_root / family if family else config.export_v2_root
        assert export_manager._glb_seam_root(config, seam) == expected


def test_the_family_directories_are_the_ones_the_glb_reviewer_reads() -> None:
    # `tools/elysium_glb_review/core/seams.SEAM_EXTENSION` is the corpus's only other copy of
    # these directory names, and it keys a nested family (`shader-programs/psh`) on the whole
    # path. A family renamed on one side only would make the reviewer expect no extension at
    # all for every unit of the kind rather than flagging the ones that carry the wrong root.
    directories = _review_seam_directories()
    for name, family in sorted(GLB_SEAM_FAMILY.items()):
        if not family:
            continue
        assert any(
            key == family or key.startswith(family + "/") for key in directories
        ), f"{name} publishes below {family!r}, which the GLB reviewer does not know"


def _glb_config():
    from pathlib import Path
    from types import SimpleNamespace

    return SimpleNamespace(
        game_root=Path("C:/game"),
        work_root=Path("C:/work"),
        export_root=Path("C:/work/exports"),
        export_v2_root=Path("C:/work/exports_v2"),
    )


def _seam_modules(seam):
    import importlib

    return (
        importlib.import_module("elysium_pipeline.exporters." + seam.module),
        importlib.import_module("elysium_pipeline.validation." + seam.module),
    )


_GLB_SUMMARY = {"asset": "vtmb:test:one", "accountedBytes": 10, "sourceBytes": 10}


@contextlib.contextmanager
def _stubbed_seam(seam, index, keys):
    """Stand in for one seam's exporter and validator, leaving the workflow real."""
    from pathlib import Path

    from elysium_pipeline.formats import install

    exporter, validation = _seam_modules(seam)
    from elysium_pipeline.exporters import corpus_index_glb

    def write(_index, key, output_root, **_kwargs):
        assert _index is index
        return Path(output_root) / (str(key).replace("/", "_") + ".glb")

    def write_keyless(_index, output_root, **_kwargs):
        assert _index is index
        return Path(output_root) / "one.glb"

    with (
        mock.patch.object(install, "build_index", return_value=index) as build_index,
        mock.patch.object(exporter, seam.keys_attr, return_value=list(keys)),
        mock.patch.object(
            exporter, seam.export_attr,
            side_effect=write_keyless if seam.keyless else write,
        ) as export,
        mock.patch.object(validation, "validate", return_value=_GLB_SUMMARY) as validate,
        mock.patch.object(validation, "warnings_for", return_value=[]),
        mock.patch.object(corpus_index_glb, "refresh_unit", return_value=True) as refresh,
    ):
        yield build_index, export, validate, refresh


@pytest.mark.parametrize(
    ("corpus", "seam"), SINGLE_KIND_CORPORA, ids=[seam.label for _, seam in SINGLE_KIND_CORPORA]
)
def test_each_plural_glb_command_builds_one_index_and_writes_every_key(corpus, seam) -> None:
    index = {"one": object()}
    with _stubbed_seam(seam, index, ["a/one", "b/two"]) as (
        build_index, export, validate, _refresh
    ):
        destinations = corpus(_glb_config(), object(), jobs=1)

    assert len(destinations) == 2
    build_index.assert_called_once_with()
    assert export.call_count == 2
    assert validate.call_count == 2


@pytest.mark.parametrize(
    ("corpus", "seam"), SINGLE_KIND_CORPORA, ids=[seam.label for _, seam in SINGLE_KIND_CORPORA]
)
def test_a_plural_glb_command_publishes_below_its_own_family(corpus, seam) -> None:
    config = _glb_config()
    index = {"one": object()}
    with _stubbed_seam(seam, index, ["a/one"]):
        destinations = corpus(config, object(), jobs=1)

    # The expected directory is the spelled-out one, not `_glb_seam_root`'s own answer: a
    # family typed `imagez` would satisfy the function against itself and publish the whole
    # image corpus somewhere nothing reads.
    family = GLB_SEAM_FAMILY[_seam_name(seam)]
    expected = config.export_v2_root / family if family else config.export_v2_root
    assert destinations[0].parent == expected


@pytest.mark.parametrize(
    ("unit", "seam"), SINGLE_KIND_UNITS, ids=[seam.label for _, seam in SINGLE_KIND_UNITS]
)
def test_each_singular_glb_command_writes_the_key_it_was_given(unit, seam) -> None:
    config = _glb_config()
    index = {"one": object()}
    with _stubbed_seam(seam, index, []) as (build_index, export, _validate, refresh):
        destination = unit(config, object(), "a/one")

    build_index.assert_called_once_with()
    assert export.call_args.args[1] == "a/one"
    assert destination.name == "a_one.glb"
    # `seam_map_corpus_index.md` ("Unit identity"): the index is "rewritten by any single-unit
    # command so that its `units[]` row for that unit is current", so every singular command
    # ends by handing the corpus index the file it just wrote.
    refresh.assert_called_once_with(config.export_v2_root, destination)


@pytest.mark.parametrize(
    ("unit", "seam"), SINGLE_KIND_UNITS, ids=[seam.label for _, seam in SINGLE_KIND_UNITS]
)
def test_a_singular_glb_command_reports_a_failure_as_an_offline_export_failure(unit, seam) -> None:
    exporter, _validation = _seam_modules(seam)
    from elysium_pipeline.formats import install

    with (
        mock.patch.object(install, "build_index", return_value={}),
        mock.patch.object(exporter, seam.export_attr, side_effect=ValueError("no such key")),
    ):
        with pytest.raises(export_manager.OfflineExportFailure) as raised:
            unit(_glb_config(), object(), "a/one")
    assert "a/one" in str(raised.value)
    assert "no such key" in str(raised.value)


def test_a_singular_command_reports_a_corpus_index_refresh_it_could_not_make(capsys) -> None:
    # The unit on disk is what the command was asked for, so a refresh that fails is reported
    # and the export still succeeds; the index is made current again by the next `export-all`.
    config = _glb_config()
    index = {"one": object()}
    with _stubbed_seam(export_manager.VDATA_GLB, index, []) as (_build, _export, _v, refresh):
        refresh.side_effect = RuntimeError("index.glb carries no corpus-index extension root")
        destination = export_manager.export_vdata_glb(config, object(), "a/one")

    assert destination.name == "a_one.glb"
    printed = capsys.readouterr().out
    assert "corpus index not refreshed" in printed
    assert "index.glb carries no corpus-index extension root" in printed


def test_a_config_that_names_no_export_v2_root_refreshes_no_corpus_index() -> None:
    from types import SimpleNamespace

    from elysium_pipeline.exporters import corpus_index_glb

    with mock.patch.object(corpus_index_glb, "refresh_unit") as refresh:
        export_manager.refresh_corpus_index_row(
            SimpleNamespace(export_v2_root=None), _glb_config().export_v2_root / "one.glb"
        )
    refresh.assert_not_called()


def test_a_keyless_seam_writes_its_one_unit_without_a_key() -> None:
    # The font registry and the game-sound manifest are each one unit the install either has
    # or has not, so their writers take no key.
    index = {"one": object()}
    with _stubbed_seam(export_manager.FONT_LIST_GLB, index, []) as (
        _build, export, _validate, _refresh
    ):
        destination = export_manager.export_font_list_glb(_glb_config(), object())

    assert export.call_args.args == (index, destination.parent)
    assert destination.name == "one.glb"


def test_the_font_corpus_publishes_the_registry_with_the_faces() -> None:
    # `font_glb.source_keys` appends the registry's sentinel key and `font_glb.export`
    # recognises it, so the registry needs no pass of its own.
    index = {"one": object()}
    keys = ["arial_10_400_0", "fontlist"]
    with _stubbed_seam(export_manager.FONT_GLB, index, keys) as (
        _build, export, _validate, _refresh
    ):
        destinations = export_manager.export_all_font_glbs(_glb_config(), object(), jobs=1)

    assert [call.args[1] for call in export.call_args_list] == keys
    assert len(destinations) == 2


def test_the_map_command_publishes_all_four_units_of_one_bsp() -> None:
    config = _glb_config()
    index = {"one": object()}
    written = []

    with contextlib.ExitStack() as stack:
        for seam in export_manager.MAP_GLB_UNITS:
            stack.enter_context(_stubbed_seam(seam, index, []))
            written.append(seam)
        destinations = export_manager.export_map_glb(config, object(), "sp_tutorial_1")

    assert len(destinations) == 4
    assert {path.parent for path in destinations} == {config.export_v2_root}


def test_the_map_corpus_builds_one_index_for_all_four_units() -> None:
    # The four units are cut from one member; rebuilding the index per sub-unit would pay for
    # the same read four times.
    index = {"one": object()}
    with contextlib.ExitStack() as stack:
        builds = [
            stack.enter_context(_stubbed_seam(seam, index, ["sp_tutorial_1"]))[0]
            for seam in export_manager.MAP_GLB_UNITS
        ]
        destinations = export_manager.export_all_map_glbs(_glb_config(), object(), jobs=1)

    assert len(destinations) == 4
    # The four seams share one `install.build_index`, so the innermost patch is the live one
    # and its single call is the whole corpus run's index build.
    assert sum(build.call_count for build in builds) == 1


def test_the_sound_script_command_publishes_the_manifest_and_the_soundscapes() -> None:
    # `seam_map_sound_script.md` gives the manifest and the soundscapes no singular command of
    # their own, so `sound-scripts-glb` is the only place they publish.
    index = {"one": object()}
    seams = (
        export_manager.SOUND_SCRIPT_GLB,
        export_manager.SOUND_SCRIPT_MANIFEST_GLB,
        export_manager.SOUNDSCAPE_GLB,
    )
    with contextlib.ExitStack() as stack:
        for seam in seams:
            stack.enter_context(_stubbed_seam(seam, index, ["one"]))
        destinations = export_manager.export_all_sound_script_glbs(
            _glb_config(), object(), jobs=1
        )

    assert len(destinations) == 3


def test_a_corpus_with_no_key_fails_rather_than_publishing_nothing() -> None:
    index = {"one": object()}
    with _stubbed_seam(export_manager.VDATA_GLB, index, []):
        with pytest.raises(export_manager.OfflineExportFailure) as raised:
            export_manager.export_all_vdata_glbs(_glb_config(), object(), jobs=1)
    assert "vdata GLB" in str(raised.value)


def test_a_corpus_whose_source_will_not_load_fails_with_the_reason() -> None:
    exporter, _validation = _seam_modules(export_manager.SENTENCE_GLB)
    from elysium_pipeline.formats import install

    with (
        mock.patch.object(install, "build_index", return_value={}),
        mock.patch.object(
            exporter, "sentence_keys", side_effect=RuntimeError("sentences.txt is absent")
        ),
    ):
        with pytest.raises(export_manager.OfflineExportFailure) as raised:
            export_manager.export_all_sentence_glbs(_glb_config(), object(), jobs=1)
    assert "sentences.txt is absent" in str(raised.value)


def test_a_failing_unit_fails_the_corpus_and_names_what_failed() -> None:
    exporter, validation = _seam_modules(export_manager.SCENE_GLB)
    from elysium_pipeline.formats import install

    with (
        mock.patch.object(install, "build_index", return_value={}),
        mock.patch.object(exporter, "source_keys", return_value=["a/one", "b/two"]),
        mock.patch.object(exporter, "export", side_effect=ValueError("truncated")),
        mock.patch.object(validation, "validate", return_value=_GLB_SUMMARY),
        mock.patch.object(validation, "warnings_for", return_value=[]),
    ):
        with pytest.raises(export_manager.OfflineExportFailure) as raised:
            export_manager.export_all_scene_glbs(_glb_config(), object(), jobs=1)
    assert "0/2" in str(raised.value)
    assert "a/one" in str(raised.value)


def test_a_seam_publishes_below_the_export_v2_root_it_names() -> None:
    config = _glb_config()
    # A seam whose exporter names its own family directory is handed the root itself; one
    # whose exporter does not is handed the family directory.
    assert export_manager._glb_seam_root(config, export_manager.MAP_GLB) == config.export_v2_root
    assert (
        export_manager._glb_seam_root(config, export_manager.VDATA_GLB)
        == config.export_v2_root / "vdata"
    )


@pytest.mark.parametrize(
    ("summary", "expected"),
    [
        ({"accountedBytes": 7, "sourceBytes": 9}, " (7/9 bytes)"),
        ({"byteCoveragePercent": 100.0}, " (100.0% of source bytes)"),
        ({"byteCoveragePercent": [100.0, 98.5]}, " (98.5% of source bytes)"),
        ({"byteCoveragePercent": []}, ""),
        ({"sourceBytes": 9}, ""),
        ({}, ""),
    ],
)
def test_the_progress_line_reports_whichever_byte_account_a_seam_gives_it(
    summary: dict, expected: str
) -> None:
    # Summaries differ per seam: some lift both totals out of the byte ledger, some only the
    # coverage percent. A seam that reports neither still gets a progress line.
    assert export_manager._glb_source_bytes(summary) == expected


def test_every_glb_seam_is_named_once_and_runs_a_callable() -> None:
    names = [seam for seam, _corpus in export_manager.GLB_SEAMS]
    assert len(names) == len(set(names))
    assert all(callable(corpus) for _seam, corpus in export_manager.GLB_SEAMS)


def test_the_corpus_index_is_the_last_seam_export_all_runs() -> None:
    # It is written over the published corpus, so every other seam has to have run first
    # (`seam_map_corpus_index.md`, "Unit identity").
    assert export_manager.GLB_SEAMS[-1][0] == "corpus-index"
    assert export_manager.GLB_SEAMS[-1][1] is export_manager.export_all_corpus_index_glbs


def test_export_all_runs_every_seam_and_reports_a_failing_one() -> None:
    # A seam is an independent corpus costing its own hours; one failure must not cancel the
    # seams still to run, and must not be swallowed either.
    from pathlib import Path
    from types import SimpleNamespace

    config = SimpleNamespace(
        game_root=Path("C:/game"),
        work_root=Path("C:/work"),
        export_root=Path("C:/work/exports"),
        export_v2_root=Path("C:/work/exports_v2"),
    )
    ran = []

    def corpus(name, result):
        def run(_config, _runner, *, jobs=None):
            ran.append(name)
            if isinstance(result, Exception):
                raise result
            return result

        return run

    seams = (
        ("texture", corpus("texture", [Path("a.glb")])),
        ("material", corpus(
            "material", export_manager.OfflineExportFailure("2 of 9 failed"))),
        ("map", corpus("map", [Path("b.glb"), Path("c.glb")])),
        ("engine-config", corpus("engine-config", [Path("d.glb")])),
    )
    with mock.patch.object(export_manager, "GLB_SEAMS", seams):
        with pytest.raises(export_manager.OfflineExportFailure) as raised:
            export_manager.export_all_glb_seams(config, object())

    assert ran == ["texture", "material", "map", "engine-config"]
    assert "material" in str(raised.value)
    assert "2 of 9 failed" in str(raised.value)


def test_export_all_returns_every_seam_that_published() -> None:
    from pathlib import Path
    from types import SimpleNamespace

    config = SimpleNamespace(
        game_root=Path("C:/game"),
        work_root=Path("C:/work"),
        export_root=Path("C:/work/exports"),
        export_v2_root=Path("C:/work/exports_v2"),
    )

    def corpus(destinations):
        return lambda _config, _runner, *, jobs=None: destinations

    seams = (
        ("texture", corpus([Path("a.glb")])),
        ("material", corpus([Path("b.glb"), Path("c.glb")])),
        ("nav-graph", corpus([])),
    )
    with mock.patch.object(export_manager, "GLB_SEAMS", seams):
        published = export_manager.export_all_glb_seams(config, object())

    assert {seam: len(paths) for seam, paths in published.items()} == {
        "texture": 1, "material": 2, "nav-graph": 0}


def test_glb_seams_publish_under_the_export_v2_root() -> None:
    # The isolated seams feed the new bake pipeline, so they must never land inside the
    # bake corpus `elysium clean` and the manifest own.
    from pathlib import Path
    from types import SimpleNamespace

    config = SimpleNamespace(
        game_root=Path("C:/game"),
        work_root=Path("C:/work"),
        export_root=Path("C:/work/exports"),
        export_v2_root=Path("C:/work/exports_v2"),
    )
    for seam in ("models", "textures", "materials", "surface-properties"):
        root = export_manager._export_v2_root(config, seam)
        assert root == Path("C:/work/exports_v2") / seam
        assert config.export_root not in root.parents


def test_glb_seams_reject_a_config_without_an_export_v2_root() -> None:
    from pathlib import Path
    from types import SimpleNamespace

    config = SimpleNamespace(
        game_root=Path("C:/game"),
        work_root=Path("C:/work"),
        export_root=Path("C:/work/exports"),
        export_v2_root=None,
    )
    with pytest.raises(ValueError):
        export_manager._require_export_v2_config(config)


def test_glb_corpus_workers_are_spawn_importable() -> None:
    import pickle

    from elysium_pipeline import workers

    named = [name for name in dir(workers) if name.endswith("_glb_worker")]
    assert named
    for name in named:
        worker = getattr(workers, name)
        restored = pickle.loads(pickle.dumps(worker))
        assert restored.__name__ == worker.__name__
    # A seam that names a worker it cannot spawn fails only once the pool is wide.
    for worker in export_manager.MAP_GLB_WORKERS:
        assert getattr(workers, worker.__name__, None) is worker


def test_every_named_worker_delegates_to_a_declared_seam() -> None:
    # The wrapper names its seam by string, and the inline branch every other test takes
    # never reads it: a typo would surface only in a real pooled run of that one kind.
    import importlib
    from unittest import mock

    from elysium_pipeline import workers

    declared = {
        (seam.module, seam.export_attr)
        for seam in vars(export_manager).values()
        if isinstance(seam, export_manager.GlbUnitSeam)
    }
    assert declared

    delegating = 0
    for name in sorted(name for name in dir(workers) if name.endswith("_glb_worker")):
        worker = getattr(workers, name)
        with mock.patch.object(workers, "_glb_unit_worker") as delegate:
            worker("some/key", "C:/out")
        if not delegate.call_args:
            continue  # a bespoke worker: it writes the unit itself
        delegating += 1
        module = delegate.call_args.args[0]
        export_attr = delegate.call_args.kwargs.get("export_attr", "export")
        assert (module, export_attr) in declared, name
        exporter = importlib.import_module("elysium_pipeline.exporters." + module)
        assert hasattr(exporter, export_attr), name
    assert delegating == 15  # every wrapper but the three that write the unit themselves


def test_only_ents_consuming_bundles_wait_on_the_maps() -> None:
    # `audio` and `npc` read the exported per-map `.ents`; every other bundle reads the
    # install (or the pre-graph shared corpus) and starts immediately. `npc` keeps its one
    # inter-bundle edge on the exported vdata mirror.
    from pathlib import Path
    from types import SimpleNamespace

    bundles = [
        "audio", "particles", "scripts", "signs", "vdata", "items",
        "cfg", "scenes", "ui", "use-icons", "npc",
    ]
    config = SimpleNamespace(export_root=Path("/fake/export/root"))
    tasks = export_manager._bundle_tasks(
        config, bundles, ["m1", "m2"], {}, {bundle: "fp" for bundle in bundles})
    by_name = {task.name: task for task in tasks}
    map_edges = ("map:m1", "map:m2")
    assert by_name["bundle:audio"].dependencies == map_edges
    assert by_name["bundle:npc"].dependencies == (*map_edges, "bundle:vdata")
    for bundle in bundles:
        if bundle in ("audio", "npc"):
            continue
        assert by_name[f"bundle:{bundle}"].dependencies == (), f"bundle={bundle}"


def test_npc_bundle_drops_the_vdata_edge_when_vdata_is_not_requested() -> None:
    from pathlib import Path
    from types import SimpleNamespace

    config = SimpleNamespace(export_root=Path("/fake/export/root"))
    tasks = export_manager._bundle_tasks(
        config, ["npc"], ["m1"], {}, {"npc": "fp"})
    assert tasks[0].dependencies == ("map:m1",)


def test_items_bundle_outputs_include_both_manifests() -> None:
    from pathlib import Path

    export_root = Path("/fake/export/root")
    outputs = export_manager._bundle_outputs(export_root, "items")
    assert export_root / "items" / "ground_models.json" in outputs
    assert wield_corpus.manifest_path(export_root) in outputs


def test_structured_failure_is_strict() -> None:
    result = export_all.ExportBatchResult(
        maps=[
            export_all.ExportTaskResult(
                name="missing_map",
                status="failed",
                error="not installed",
            )
        ]
    )
    with pytest.raises(export_all.ExportFailed) as caught:
        result.require_success()
    assert caught.value.result is result
