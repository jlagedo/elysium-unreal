"""Contract tests for the sound-scheme capsule and `uv run elysium import sound-schemes`.

Every fixture is a synthetic `sound/schemes/*.txt` exported into a temporary `export_v2` root
from hand-built bytes and a fake install index -- never the real VtMB install, and never a real
deployed corpus.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest
from typer.testing import CliRunner

from elysium_pipeline import cli
from elysium_pipeline.config import ProjectConfig
from elysium_pipeline.exporters import sound_scheme_glb as exporter
from elysium_pipeline.formats.sound_scheme_glb import SOUND_SCHEME_EXTENSION
from elysium_pipeline.formats.unit_contract import encode_glb, read_glb
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers import sound as sound_importer
from elysium_pipeline.importers import sound_schemes as importer

RUNNER = CliRunner()


def _scheme(music: str) -> bytes:
    return (
        b"SoundScheme\r\n"
        b"{\r\n"
        b"\tMusic\r\n"
        b"\t{\r\n"
        b'\t\t"Filename"\t"' + music.encode("ascii") + b'"\r\n'
        b'\t\t"Volume"\t"50"\r\n'
        b"\t}\r\n"
        b"}\r\n"
    )


#: The install spells the directory and the stem in mixed case; the seam's key is the fold, so
#: `sp_tutorial_city` is what both the unit and the deployed leaf are named.
MEMBERS = {
    "sound/schemes/sp_tutorial_city.txt": _scheme("music/theme.mp3"),
    "sound/schemes/ch_cloud.txt": _scheme("music/cloud.mp3"),
    "sound/music/theme.mp3": b"\xff\xfb\x90\x00",
    "sound/music/cloud.mp3": b"\xff\xfb\x90\x00",
}

SCHEMES = {
    path: data for path, data in MEMBERS.items() if path.startswith("sound/schemes/")
}


def _publish(export_v2_root: Path) -> Path:
    """Export every scheme member into `<export_v2_root>/sound-schemes/*.glb`."""

    root = Path(export_v2_root)
    index = {path: ("loose", "C:/game/Vampire/" + path) for path in MEMBERS}
    read = lambda idx, name: MEMBERS.get(name)                       # noqa: E731
    for path in SCHEMES:
        exporter.export(index, path, root / "sound-schemes", read_bytes=read, dsp_preset_ids=frozenset())
    return root


def _deployed(corpus: Path, *directories: str) -> dict[str, bytes]:
    files: dict[str, bytes] = {}
    for directory in directories:
        for path in sorted((Path(corpus) / directory).rglob("*")):
            if path.is_file():
                files[path.relative_to(corpus).as_posix()] = path.read_bytes()
    return files


# --- the capsule upgrade -------------------------------------------------------------------------


def test_a_scheme_unit_carries_its_txt_verbatim(tmp_path):
    """Before 1.1.0 the kind published no BIN chunk at all, so no importer could exist."""

    _publish(tmp_path)
    document, binary = read_glb(tmp_path / "sound-schemes" / "sp_tutorial_city.glb")
    root = document["extensions"][SOUND_SCHEME_EXTENSION]

    assert root["schemaVersion"] == "1.1.0"
    assert root["sourceResolution"]["capsule"] == {"encoding": "raw"}
    (member,) = root["sourceResolution"]["members"]
    source = SCHEMES["sound/schemes/sp_tutorial_city.txt"]
    assert member["path"] == "sound/schemes/sp_tutorial_city.txt"
    assert member["sha256"] == hashlib.sha256(source).hexdigest()
    view = document["bufferViews"][member["capsule"]["bufferView"]]
    assert binary[view["byteOffset"]:view["byteOffset"] + view["byteLength"]] == source
    # The capsule is a file, not typed elements: the unit stays scene-less and accessor-less.
    assert "accessors" not in document
    assert not any(document.get(name) for name in ("scenes", "nodes", "meshes"))


def test_the_decode_is_unchanged_by_the_capsule(tmp_path):
    _publish(tmp_path)
    document, binary = read_glb(tmp_path / "sound-schemes" / "ch_cloud.glb")
    from elysium_pipeline.validation import sound_scheme_glb as validation

    summary = validation.validate_document(document, binary)
    assert summary["asset"] == "vtmb:sound-scheme:ch_cloud"
    assert summary["byteCoveragePercent"] == 100.0
    for row in document["extensions"][SOUND_SCHEME_EXTENSION]["coverage"]["byteLedger"]:
        assert row["coveragePercent"] == 100.0


def test_a_unit_that_drops_its_capsule_declaration_is_refused(tmp_path):
    _publish(tmp_path)
    document, binary = read_glb(tmp_path / "sound-schemes" / "ch_cloud.glb")
    root = document["extensions"][SOUND_SCHEME_EXTENSION]
    root["sourceResolution"].pop("capsule")
    for member in root["sourceResolution"]["members"]:
        member.pop("capsule")
    from elysium_pipeline.validation import sound_scheme_glb as validation

    with pytest.raises(validation.SoundSchemeGlbValidationError, match="capsule"):
        validation.validate_document(document, binary)


# --- the lane ------------------------------------------------------------------------------------


def test_every_scheme_deploys_to_the_path_the_runtime_reads(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    result = importer.import_sound_schemes(tmp_path / "exports_v2", corpus)

    assert result.failures == []
    assert result.units == 2
    assert result.written == 2
    assert _deployed(corpus, "sound") == SCHEMES


def test_the_deployed_leaf_is_the_lower_cased_key(tmp_path):
    assert importer.target_of("sound/schemes/sp_tutorial_city.txt") == (
        "sound/schemes/sp_tutorial_city.txt",
    )
    # The runtime folds before it looks, so a unit that kept the install's spelling still lands
    # under the key `SchemeFile(Rel)` opens.
    assert importer.target_of("sound/Schemes/SP_Tutorial_City.txt") == (
        "sound/schemes/sp_tutorial_city.txt",
    )
    for refused in ("sound/schemes/", "vdata/system/sndscheme_wpn.txt", "sound/a/b.wav"):
        with pytest.raises(corpus_deploy.CorpusImportError):
            importer.target_of(refused)


def test_a_second_run_over_one_corpus_rewrites_nothing(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_sound_schemes(tmp_path / "exports_v2", corpus)
    stamps = {path: path.stat().st_mtime_ns for path in corpus.rglob("*") if path.is_file()}

    again = importer.import_sound_schemes(tmp_path / "exports_v2", corpus)
    assert again.written == 0 and again.units_current == 2 and again.unchanged == 2
    for path, stamp in stamps.items():
        if corpus_deploy.BOOKKEEPING_DIRECTORY not in path.parts:
            assert path.stat().st_mtime_ns == stamp


def test_a_stale_file_is_overwritten_and_an_orphan_is_pruned(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_sound_schemes(tmp_path / "exports_v2", corpus)
    target = corpus / "sound" / "schemes" / "ch_cloud.txt"
    target.write_bytes(b"stale")
    orphan = corpus / "sound" / "schemes" / "retired.txt"
    orphan.write_bytes(b"no longer exported")

    result = importer.import_sound_schemes(tmp_path / "exports_v2", corpus)
    assert result.written == 1
    assert target.read_bytes() == SCHEMES["sound/schemes/ch_cloud.txt"]
    assert result.pruned == 1 and not orphan.exists()


def test_the_two_sound_lanes_do_not_prune_each_other(tmp_path):
    """`sound/schemes/` is inside the `sound` lane's own root and belongs to this one."""

    assert "sound/schemes" in sound_importer.LANE_SPEC.foreign_directories
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_sound_schemes(tmp_path / "exports_v2", corpus)

    # The audio lane, run with one unrelated unit deployed by hand, keeps every scheme.
    audio = corpus / "sound" / "area" / "chime.wav"
    audio.parent.mkdir(parents=True, exist_ok=True)
    audio.write_bytes(b"RIFF")
    pruned = corpus_deploy._prune(corpus, sound_importer.LANE_SPEC, kept=set())
    assert pruned == 1 and not audio.exists()
    assert _deployed(corpus, "sound") == SCHEMES

    # And the scheme lane owns `sound/schemes` alone: an orphan elsewhere under `sound/` is the
    # audio lane's business, not this one's.
    audio.parent.mkdir(parents=True, exist_ok=True)
    audio.write_bytes(b"RIFF")
    assert corpus_deploy._prune(corpus, importer.LANE_SPEC, kept=set()) == 2
    assert audio.is_file()


def test_a_unit_from_before_the_capsule_is_one_failure_and_not_a_crash(tmp_path):
    root = _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"

    stale = root / "sound-schemes" / "ch_cloud.glb"
    document, _ = read_glb(stale)
    scheme_root = document["extensions"][SOUND_SCHEME_EXTENSION]
    scheme_root["schemaVersion"] = "1.0.0"
    scheme_root["sourceResolution"].pop("capsule")
    for member in scheme_root["sourceResolution"]["members"]:
        member.pop("capsule")
    document.pop("bufferViews")
    document.pop("buffers")
    stale.write_bytes(encode_glb(document, b""))

    result = importer.import_sound_schemes(tmp_path / "exports_v2", corpus)
    assert [key for key, _ in result.failures] == ["sound-schemes/ch_cloud"]
    assert "capsule" in result.failures[0][1]
    assert result.written == 1


def test_the_import_report_names_the_counts(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_sound_schemes(tmp_path / "exports_v2", corpus)

    report = json.loads(
        (corpus_deploy.bookkeeping_root(corpus, "sound-schemes") / "import_report.json")
        .read_text(encoding="utf-8")
    )
    assert report["lane"] == "sound-schemes"
    assert report["units"] == 2
    assert report["filesWritten"] == 2
    assert report["failures"] == []


def test_an_export_root_with_no_units_says_what_to_run(tmp_path):
    with pytest.raises(corpus_deploy.CorpusImportError, match="sound-schemes-glb"):
        importer.import_sound_schemes(tmp_path / "exports_v2", tmp_path / "corpus")


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


def test_the_command_deploys_the_schemes_and_reports_one_line(monkeypatch, tmp_path):
    config = _configured(monkeypatch, tmp_path)
    _publish(tmp_path / "exports_v2")

    result = RUNNER.invoke(cli.app, ["import", "sound-schemes"])
    assert result.exit_code == 0, result.output
    assert "sound-schemes corpus import:" in result.output
    assert _deployed(corpus_deploy.corpus_root(config.repo_root), "sound") == SCHEMES


def test_the_command_fails_when_a_unit_cannot_be_imported(monkeypatch, tmp_path):
    _configured(monkeypatch, tmp_path)
    root = _publish(tmp_path / "exports_v2")
    (root / "sound-schemes" / "ch_cloud.glb").write_bytes(b"junk")

    result = RUNNER.invoke(cli.app, ["import", "sound-schemes"])
    assert result.exit_code == int(cli.ExitCode.OFFLINE_EXPORT), result.output
    assert "1 sound-schemes unit(s) could not be imported" in result.output
