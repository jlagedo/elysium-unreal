from __future__ import annotations


from typer.testing import CliRunner

from elysium_pipeline.cli import _CHILD_SIGNAL, _child_signal, app


RUNNER = CliRunner()


def test_root_help_exposes_the_single_public_command_families() -> None:
    result = RUNNER.invoke(app, ["--help"])
    assert result.exit_code == 0, result.output
    for command in (
        "reconstruct",
        "deps",
        "doctor",
        "build",
        "export",
        "export_v2",
        "import",
        "test",
        "run",
        "debug",
        "research",
        "ide",
        "mcp",
    ):
        assert command in result.output


def test_targeted_map_export_rejects_clean() -> None:
    result = RUNNER.invoke(
        app, ["export", "map", "sp_tutorial_1", "--clean"]
    )
    assert result.exit_code == 2, result.output
    assert "No such option" in result.output


def test_no_detached_checkout_family_is_exposed() -> None:
    for command in ("lane", "worktree"):
        result = RUNNER.invoke(app, [command, "--help"])
        assert result.exit_code == 2, result.output
        assert "No such command" in result.output


def test_export_help_retires_wield() -> None:
    result = RUNNER.invoke(app, ["export", "--help"])
    assert result.exit_code == 0, result.output
    assert "wield" not in result.output


#: Every command `export_v2` registers: one singular and one plural per unit kind, plus the
#: whole-corpus runner and the corpus index. The corpus index has no plural: there is exactly one
#: per export root.
EXPORT_V2_COMMANDS = (
    "texture-glb", "textures-glb",
    "surface-property-glb", "surface-properties-glb",
    "material-glb", "materials-glb",
    "image-glb", "images-glb",
    "sound-glb", "sounds-glb",
    "expression-table-glb", "expression-tables-glb",
    "shader-source-glb", "shader-sources-glb",
    "shader-program-glb", "shader-programs-glb",
    "particle-glb", "particles-glb",
    "font-glb", "fonts-glb", "font-list-glb",
    "sound-script-glb", "sound-scripts-glb",
    "sentence-glb", "sentences-glb",
    "dsp-preset-glb", "dsp-presets-glb",
    "sound-scheme-glb", "sound-schemes-glb",
    "scene-glb", "scenes-glb",
    "model-glb", "models-glb",
    "dialogue-glb", "dialogues-glb",
    "vdata-glb", "vdatas-glb",
    "ui-resource-glb", "ui-resources-glb",
    "script-glb", "scripts-glb",
    "map-glb", "maps-glb",
    "map-entities-glb", "map-lighting-glb", "map-visibility-glb",
    "nav-graph-glb", "nav-graphs-glb",
    "engine-config-glb", "engine-configs-glb",
    "corpus-index-glb",
    "export-all",
)


def test_export_v2_registers_exactly_the_isolated_glb_commands() -> None:
    from elysium_pipeline.cli import export_v2_app

    registered = {command.name for command in export_v2_app.registered_commands}
    assert registered == set(EXPORT_V2_COMMANDS)


def test_export_v2_help_names_every_isolated_glb_command() -> None:
    result = RUNNER.invoke(app, ["export_v2", "--help"], env={"COLUMNS": "200"})
    assert result.exit_code == 0, result.output
    for command in EXPORT_V2_COMMANDS:
        assert command in result.output, command


#: Every command the import family registers. The
#: destination is per lane, not one tree: `vdata` deploys loose bytes to `Content/ElysiumCorpus`,
#: while `textures` and `surface-properties` author `.uasset` content under `/ElysiumBaked`. The
#: last two are not corpus families at all but per-map lanes (R4.1, R4.2), which is why they refuse
#: to run unscoped where a corpus lane takes `--all`.
IMPORT_COMMANDS = ("vdata", "dialogue", "sound", "sound-schemes", "textures",
                   "surface-properties", "materials",
                   "models", "characters", "model-catalogues", "expression-tables", "cook-roots",
                   "map-entities", "map-collision", "map-environment")


def test_import_registers_exactly_the_migrated_import_lanes() -> None:
    from elysium_pipeline.cli import import_app

    registered = {command.name for command in import_app.registered_commands}
    assert registered == set(IMPORT_COMMANDS)


def test_import_help_names_every_import_lane() -> None:
    result = RUNNER.invoke(app, ["import", "--help"], env={"COLUMNS": "200"})
    assert result.exit_code == 0, result.output
    for command in IMPORT_COMMANDS:
        assert command in result.output, command


def test_the_retired_character_seam_is_exposed_nowhere() -> None:
    for family in ("export", "export_v2"):
        result = RUNNER.invoke(app, [family, "--help"], env={"COLUMNS": "200"})
        assert result.exit_code == 0, result.output
        assert "character-glb" not in result.output
        assert "characters-glb" not in result.output


def test_a_map_sub_unit_needs_one_map_or_all_but_not_both() -> None:
    for command in ("map-entities-glb", "map-lighting-glb", "map-visibility-glb"):
        neither = RUNNER.invoke(app, ["export_v2", command])
        assert neither.exit_code == 2, neither.output
        both = RUNNER.invoke(app, ["export_v2", command, "sp_tutorial_1", "--all"])
        assert both.exit_code == 2, both.output


def test_prefilter_agrees_with_the_regex_on_every_vocabulary_shape() -> None:
    samples = (
        "LogPython: baked hollywood in 12.3s",
        "Fatal error: rendering thread exception",
        "Assertion failed: Index < Num",
        "LogShaderCompiler: Warning: retrying job",
        "LogInit: Error: missing module",
        "Error: cook failed",
        "  Error: indented, so not anchored",
        "SomeError: not the anchored form",
        "Warning: at line start without the colon prefix",
        "LogStreaming: Display: loaded package",
        "plain engine chatter line",
        "",
    )
    for line in samples:
        assert _child_signal(line) == (_CHILD_SIGNAL.search(line) is not None), line
