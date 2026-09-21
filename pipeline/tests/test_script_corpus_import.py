"""Contract tests for `uv run elysium import scripts`.

Every fixture is a synthetic script unit exported into a temporary `export_v2` root from
hand-built bytes and a fake install index -- never the real VtMB install, and never a real
deployed corpus. The compiled companion is the one `test_script_glb.py` builds, so the two
modules agree on what a `py+pyc` unit is.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest
from typer.testing import CliRunner

from elysium_pipeline import cli
from elysium_pipeline.config import ProjectConfig
from elysium_pipeline.exporters import script_glb as exporter
from elysium_pipeline.formats.unit_contract import read_glb
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers import scripts as importer

from test_script_glb import PY_ENTRY, PYC_LOOSE_ENTRY, PYC, SOURCE

RUNNER = CliRunner()

#: Two units: a hub module with a compiled companion, and a library module with source alone.
MEMBERS = {
    "python/tutorial/tutorial.py": SOURCE,
    "python/tutorial/tutorial.pyc": PYC,
    "python/vamputil.py": SOURCE,
}

#: What the lane is expected to put in the corpus: the source of each unit and nothing else.
DEPLOYED = {
    "scripts/tutorial/tutorial.py": SOURCE,
    "scripts/vamputil.py": SOURCE,
}


def _index() -> dict:
    return {
        "python/tutorial/tutorial.py": PY_ENTRY,
        "python/tutorial/tutorial.pyc": PYC_LOOSE_ENTRY,
        "python/vamputil.py": ("loose", "C:/game/Vampire/python/vamputil.py"),
    }


def _publish(export_v2_root: Path) -> Path:
    """Export both units into `<export_v2_root>/scripts/**.glb`."""

    root = Path(export_v2_root)
    index = _index()
    read = lambda idx, name: MEMBERS.get(name)                       # noqa: E731
    for key in ("tutorial/tutorial", "vamputil"):
        exporter.export(index, key, root / "scripts", read_bytes=read)
    return root


def _deployed(corpus: Path) -> dict[str, bytes]:
    return {
        path.relative_to(corpus).as_posix(): path.read_bytes()
        for path in sorted((Path(corpus) / "scripts").rglob("*"))
        if path.is_file()
    }


# --- the mapping ---------------------------------------------------------------------------------


def test_the_source_lands_under_scripts_and_the_companion_lands_nowhere():
    assert importer.target_of("python/tutorial/tutorial.py") == ("scripts/tutorial/tutorial.py",)
    assert importer.target_of("python/lib/string.py") == ("scripts/lib/string.py",)
    # The compiled twin is declined, not refused: a `py+pyc` unit is normal and must deploy.
    assert importer.target_of("python/tutorial/tutorial.pyc") == ()


def test_a_member_of_another_tree_is_a_defect_in_the_unit():
    for refused in ("", "python/", "vdata/system/stats.txt", "python/tutorial/tutorial.txt"):
        with pytest.raises(corpus_deploy.CorpusImportError):
            importer.target_of(refused)


def test_a_unit_with_no_source_is_named_rather_than_silently_skipped():
    """A `pyc-only` unit would deploy nothing; a missing module must not be silent."""

    importer.refuse_compiled_only(["python/a/a.py", "python/a/a.pyc"])
    with pytest.raises(corpus_deploy.CorpusImportError, match="no Python source"):
        importer.refuse_compiled_only(["python/lib/orphan.pyc"])


# --- the lane ------------------------------------------------------------------------------------


def test_every_unit_deploys_its_source_to_the_path_the_vm_imports(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    result = importer.import_scripts(tmp_path / "exports_v2", corpus)

    assert result.failures == []
    assert result.units == 2 and result.written == 2 and result.verified == 2
    assert _deployed(corpus) == DEPLOYED


def test_the_capsule_the_lane_deploys_is_the_member_the_unit_published(tmp_path):
    root = _publish(tmp_path / "exports_v2")
    document, binary = read_glb(root / "scripts" / "tutorial" / "tutorial.glb")
    members = document["extensions"]["ELYSIUM_vtmb_script"]["sourceResolution"]["members"]
    source = next(row for row in members if row["role"] == "py")
    view = document["bufferViews"][source["capsule"]["bufferView"]]
    assert binary[view["byteOffset"]:view["byteOffset"] + view["byteLength"]] == SOURCE


def test_a_second_run_over_one_corpus_rewrites_nothing(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_scripts(tmp_path / "exports_v2", corpus)
    stamps = {path: path.stat().st_mtime_ns for path in corpus.rglob("*") if path.is_file()}

    again = importer.import_scripts(tmp_path / "exports_v2", corpus)
    assert again.written == 0 and again.units_current == 2 and again.verified == 0
    for path, stamp in stamps.items():
        if corpus_deploy.BOOKKEEPING_DIRECTORY not in path.parts:
            assert path.stat().st_mtime_ns == stamp


def test_a_bumped_recipe_version_discards_every_stamp(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_scripts(tmp_path / "exports_v2", corpus)

    fields = {
        name: getattr(importer.LANE_SPEC, name)
        for name in importer.LANE_SPEC.__dataclass_fields__
    }
    fields["recipe_version"] = "elysium-scripts-corpus-v2"
    again = corpus_deploy.deploy_lane(
        tmp_path / "exports_v2", corpus, corpus_deploy.Lane(**fields)
    )
    assert again.units_current == 0 and again.verified == 2


def test_a_stale_file_is_overwritten_and_an_orphan_is_pruned(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_scripts(tmp_path / "exports_v2", corpus)
    target = corpus / "scripts" / "vamputil.py"
    target.write_bytes(b"stale")
    orphan = corpus / "scripts" / "retired" / "retired.py"
    orphan.parent.mkdir(parents=True, exist_ok=True)
    orphan.write_bytes(b"no longer exported")

    result = importer.import_scripts(tmp_path / "exports_v2", corpus)
    assert result.written == 1 and target.read_bytes() == SOURCE
    assert result.pruned == 1 and not orphan.exists()


def test_the_bytecode_the_vm_compiles_is_not_an_orphan(tmp_path):
    """CPython 2.1 writes bytecode beside the source it imports and cannot be told not to.

    Those files are the runtime's, not a retired unit's, so the prune steps over them -- otherwise
    every `import scripts` would delete the whole compiled tree the running game had just built.
    """

    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_scripts(tmp_path / "exports_v2", corpus)
    compiled = corpus / "scripts" / "tutorial" / "tutorial.pyc"
    compiled.write_bytes(PYC)

    result = importer.import_scripts(tmp_path / "exports_v2", corpus)
    assert result.pruned == 0
    assert compiled.read_bytes() == PYC
    # And the lane still deploys nothing to it: the file is the interpreter's alone.
    recipes = json.loads(
        (corpus_deploy.bookkeeping_root(corpus, "scripts") / "recipes.json")
        .read_text(encoding="utf-8")
    )
    targets = recipes["units"]["scripts/tutorial/tutorial"]["targets"]
    assert list(targets) == ["scripts/tutorial/tutorial.py"]


def test_a_member_that_climbs_out_of_the_corpus_is_refused(tmp_path):
    with pytest.raises(corpus_deploy.CorpusImportError):
        corpus_deploy.safe_destination(tmp_path, "scripts/../../escape.py")


def test_an_export_root_with_no_units_says_what_to_run(tmp_path):
    with pytest.raises(corpus_deploy.CorpusImportError, match="scripts-glb"):
        importer.import_scripts(tmp_path / "exports_v2", tmp_path / "corpus")


def test_the_import_report_names_the_counts(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_scripts(tmp_path / "exports_v2", corpus)

    report = json.loads(
        (corpus_deploy.bookkeeping_root(corpus, "scripts") / "import_report.json")
        .read_text(encoding="utf-8")
    )
    assert report["lane"] == "scripts"
    assert report["units"] == 2 and report["filesWritten"] == 2
    assert report["failures"] == []


# --- the command ---------------------------------------------------------------------------------


def _configured(monkeypatch, tmp_path) -> ProjectConfig:
    work = tmp_path / "work"
    (work / "logs").mkdir(parents=True)
    repo = tmp_path / "repo"
    repo.mkdir()
    config = ProjectConfig(
        repo_root=repo,
        project=repo / "ElysiumUE.uproject",
        game_root=None,
        work_root=work,
        export_root=work / "exports",
        export_v2_root=tmp_path / "exports_v2",
        ue_root=None,
        unreal_zen_data_path=None,
        unreal_local_data_cache_path=None,
        unreal_shader_work_root=None,
        temp_root=None,
    )
    monkeypatch.setattr(cli.CliState, "resolve", lambda self, **kwargs: config)
    return config


def test_the_command_deploys_the_scripts_and_reports_one_line(monkeypatch, tmp_path):
    config = _configured(monkeypatch, tmp_path)
    _publish(tmp_path / "exports_v2")

    result = RUNNER.invoke(cli.app, ["import", "scripts"])
    assert result.exit_code == 0, result.output
    assert "scripts corpus import:" in result.output
    assert _deployed(corpus_deploy.corpus_root(config.repo_root)) == DEPLOYED


def test_the_command_fails_when_a_unit_cannot_be_imported(monkeypatch, tmp_path):
    _configured(monkeypatch, tmp_path)
    root = _publish(tmp_path / "exports_v2")
    (root / "scripts" / "vamputil.glb").write_bytes(b"junk")

    result = RUNNER.invoke(cli.app, ["import", "scripts"])
    assert result.exit_code == int(cli.ExitCode.OFFLINE_EXPORT), result.output
    assert "1 scripts unit(s) could not be imported" in result.output
