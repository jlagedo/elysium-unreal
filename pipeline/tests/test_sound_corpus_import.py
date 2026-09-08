"""Contract tests for the sound capsule and `uv run elysium import sound`.

Since AUD1 the lane deploys the `.lip` mirror and nothing else -- audio is a baked `USoundWave`
under `/ElysiumBaked/Sounds/**` -- and owns the corpus's retired `sound/` tree for the prune
alone, stepping over the `sound-schemes` lane's `sound/schemes/`.

Every fixture is a synthetic `.wav`/`.lip` exported into a temporary `export_v2` root from
hand-built bytes and a fake install index -- never the real VtMB install, and never a real
deployed corpus. The one test that reads the machine's own trees is the legacy-parity check at
the bottom, and it skips itself when either tree is absent.
"""

from __future__ import annotations

import hashlib
import json
import os
import struct
from pathlib import Path

import pytest
from typer.testing import CliRunner

from elysium_pipeline import cli
from elysium_pipeline.config import ProjectConfig
from elysium_pipeline.exporters import sound_glb as exporter
from elysium_pipeline.formats.sound_glb.model import SOUND_EXTENSION
from elysium_pipeline.formats.unit_contract import encode_glb, read_glb
from elysium_pipeline.importers import corpus_deploy
from elysium_pipeline.importers import sound as importer

RUNNER = CliRunner()


def _wav(samples: bytes) -> bytes:
    """One minimal 16-bit mono RIFF/WAVE file."""

    fmt = struct.pack("<HHIIHH", 1, 1, 22050, 44100, 2, 16)
    body = (
        b"WAVE"
        + b"fmt " + struct.pack("<I", len(fmt)) + fmt
        + b"data" + struct.pack("<I", len(samples)) + samples
    )
    return b"RIFF" + struct.pack("<I", len(body)) + body


LIP = (
    b"VERSION 1.0\r\n"
    b"PLAINTEXT\r\n"
    b"{\r\n"
    b"Hello there.\r\n"
    b"}\r\n"
    b"WORDS\r\n"
    b"{\r\n"
    b'WORD hello 0.000000 0.400000\r\n'
    b"{\r\n"
    b'104 "h" 0.000000 0.200000 1.000000\r\n'
    b'101 "e" 0.200000 0.400000 1.000000\r\n'
    b"}\r\n"
    b"}\r\n"
)

#: One line with a `.lip` beside it, one ambient without, and a stem that ships as both
#: spellings -- the case where the `.lip` is a member of two units at once.
MEMBERS = {
    "sound/character/dlg/jack/line1.wav": _wav(bytes(range(0, 64))),
    "sound/character/dlg/jack/line1.lip": LIP,
    "sound/area/chinatown/asian_chimes1.wav": _wav(bytes(range(64, 128))),
}

#: Every corpus-relative file the lane should produce from `MEMBERS`: the `.lip` mirror only.
EXPECTED = {
    "lip/character/dlg/jack/line1.lip": LIP,
}


def _publish(export_v2_root: Path) -> Path:
    """Export every audio member into `<export_v2_root>/sounds/**.glb`."""

    root = Path(export_v2_root)
    index = {path: ("loose", "C:/game/Vampire/" + path) for path in MEMBERS}
    read = lambda idx, name: MEMBERS.get(name)                       # noqa: E731
    for path in MEMBERS:
        if path.endswith(".wav"):
            exporter.export(index, path[len("sound/"):], root / "sounds", read_bytes=read)
    return root


def _deployed(corpus: Path, *directories: str) -> dict[str, bytes]:
    files: dict[str, bytes] = {}
    for directory in directories:
        for path in sorted((Path(corpus) / directory).rglob("*")):
            if path.is_file():
                files[path.relative_to(corpus).as_posix()] = path.read_bytes()
    return files


# --- the capsule upgrade -------------------------------------------------------------------------


def test_a_sound_unit_carries_its_audio_and_its_lip_verbatim(tmp_path):
    """The payload is a decode; the capsule is the file. Both travel, and they are not the same."""

    _publish(tmp_path)
    document, binary = read_glb(tmp_path / "sounds" / "character" / "dlg" / "jack" / "line1.wav.glb")
    root = document["extensions"][SOUND_EXTENSION]

    assert root["schemaVersion"] == "1.1.0"
    assert root["sourceResolution"]["capsule"] == {"encoding": "raw"}
    members = {row["path"]: row for row in root["sourceResolution"]["members"]}
    assert set(members) == {
        "sound/character/dlg/jack/line1.wav", "sound/character/dlg/jack/line1.lip"
    }
    for path, row in members.items():
        assert row["sha256"] == hashlib.sha256(MEMBERS[path]).hexdigest()
        view = document["bufferViews"][row["capsule"]["bufferView"]]
        offset, length = view["byteOffset"], view["byteLength"]
        assert binary[offset:offset + length] == MEMBERS[path]

    # The payload still occupies view 0 and is still the interleaved PCM decode, not the RIFF file.
    payload_length = root["payload"]["byteLength"]
    assert document["accessors"][0]["bufferView"] == 0
    assert binary[:payload_length] != MEMBERS["sound/character/dlg/jack/line1.wav"][:payload_length]
    assert hashlib.sha256(binary[:payload_length]).hexdigest() == root["payload"]["sha256"]


def test_the_decode_is_unchanged_by_the_capsule(tmp_path):
    _publish(tmp_path)
    unit = tmp_path / "sounds" / "character" / "dlg" / "jack" / "line1.wav.glb"
    document, binary = read_glb(unit)
    from elysium_pipeline.validation import sound_glb as validation

    validation.validate_document(document, binary)
    root = document["extensions"][SOUND_EXTENSION]
    assert root["lip"]["plaintext"] == "Hello there."
    for row in root["coverage"]["byteLedger"]:
        assert row["coveragePercent"] == 100.0


def test_a_unit_that_drops_its_capsule_declaration_is_refused(tmp_path):
    _publish(tmp_path)
    unit = tmp_path / "sounds" / "area" / "chinatown" / "asian_chimes1.wav.glb"
    document, binary = read_glb(unit)
    root = document["extensions"][SOUND_EXTENSION]
    root["sourceResolution"].pop("capsule")
    for member in root["sourceResolution"]["members"]:
        member.pop("capsule")
    from elysium_pipeline.validation import sound_glb as validation

    with pytest.raises(validation.SoundGlbValidationError, match="capsule"):
        validation.validate_document(document, binary)


# --- the lane ------------------------------------------------------------------------------------


def test_every_member_deploys_to_the_path_the_runtime_reads(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    result = importer.import_sound(tmp_path / "exports_v2", corpus)

    assert result.failures == []
    assert result.units == 2
    assert result.written == len(EXPECTED)
    assert _deployed(corpus, "sound", "lip") == EXPECTED
    # Not one audio byte lands loose: the corpus's own `sound/` tree is never created.
    assert not (corpus / "sound").exists()


def test_a_lip_lands_under_the_lip_root_and_audio_lands_nowhere(tmp_path):
    """`LipFile` is the only loose reader left; audio is addressed as a baked `USoundWave`."""

    assert importer.target_of("sound/character/dlg/jack/line1.lip") == (
        "lip/character/dlg/jack/line1.lip",
    )
    assert importer.target_of("sound/a/b.mp3") == ()
    assert importer.target_of("sound/a/b.wav") == ()
    with pytest.raises(corpus_deploy.CorpusImportError):
        importer.target_of("sound/a/b.vcd")
    with pytest.raises(corpus_deploy.CorpusImportError):
        importer.target_of("dlg/a/b.dlg")


def test_a_second_run_over_one_corpus_rewrites_nothing(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_sound(tmp_path / "exports_v2", corpus)
    stamps = {path: path.stat().st_mtime_ns for path in corpus.rglob("*") if path.is_file()}

    again = importer.import_sound(tmp_path / "exports_v2", corpus)
    assert again.written == 0 and again.units_current == 2
    assert again.unchanged == len(EXPECTED)
    unchanged = {path: path.stat().st_mtime_ns for path in corpus.rglob("*") if path.is_file()}
    # The lane's own report is rewritten every run; every deployed file is untouched.
    for path, stamp in stamps.items():
        if corpus_deploy.BOOKKEEPING_DIRECTORY not in path.parts:
            assert unchanged[path] == stamp


def test_a_stale_file_is_overwritten_and_an_orphan_is_pruned(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_sound(tmp_path / "exports_v2", corpus)
    target = corpus / "lip" / "character" / "dlg" / "jack" / "line1.lip"
    target.write_bytes(b"stale")
    orphan = corpus / "sound" / "area" / "chinatown" / "retired.wav"
    orphan.parent.mkdir(parents=True, exist_ok=True)
    orphan.write_bytes(b"no longer exported")

    result = importer.import_sound(tmp_path / "exports_v2", corpus)
    assert result.written == 1
    assert target.read_bytes() == LIP
    assert result.pruned == 1 and not orphan.exists()


def test_the_retired_loose_audio_is_pruned_and_the_schemes_survive(tmp_path):
    """The AUD1 sweep: a deployment written by the old recipe loses its audio, keeps the schemes.

    `sound` stays in `owned_directories` for exactly this -- the lane writes nothing there any
    more, so every file left below it is an orphan of the retired recipe -- and `sound/schemes`
    stays foreign, so the `sound-schemes` lane's deploy is stepped over rather than swept.
    """

    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    legacy = {
        "sound/character/dlg/jack/line1.wav": MEMBERS["sound/character/dlg/jack/line1.wav"],
        "sound/character/dlg/jack/line1.lip": LIP,
        "sound/area/chinatown/asian_chimes1.wav": b"RIFF",
        "sound/schemes/sp_tutorial_city.txt": b"scheme text",
    }
    for relative, data in legacy.items():
        path = corpus / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    result = importer.import_sound(tmp_path / "exports_v2", corpus)

    assert result.failures == []
    assert result.pruned == 3
    assert _deployed(corpus, "sound", "lip") == {
        "sound/schemes/sp_tutorial_city.txt": b"scheme text", **EXPECTED,
    }
    # The emptied audio directories go with their files; the schemes' own parents stay.
    assert not (corpus / "sound" / "character").exists()
    assert (corpus / "sound" / "schemes").is_dir()


def test_a_unit_from_before_the_capsule_is_one_failure_and_not_a_crash(tmp_path):
    root = _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"

    stale = root / "sounds" / "area" / "chinatown" / "asian_chimes1.wav.glb"
    document, binary = read_glb(stale)
    sound_root = document["extensions"][SOUND_EXTENSION]
    payload_length = sound_root["payload"]["byteLength"]
    sound_root["schemaVersion"] = "1.0.0"
    sound_root["sourceResolution"].pop("capsule")
    for member in sound_root["sourceResolution"]["members"]:
        member.pop("capsule")
    document["bufferViews"] = document["bufferViews"][:1]
    document["buffers"] = [{"byteLength": payload_length}]
    stale.write_bytes(encode_glb(document, binary[:payload_length]))

    result = importer.import_sound(tmp_path / "exports_v2", corpus)
    assert [key for key, _ in result.failures] == ["sounds/area/chinatown/asian_chimes1.wav"]
    assert "capsule" in result.failures[0][1]
    assert not (corpus / "sound").exists()
    # The failed unit carried no `.lip`, so the good unit's mirror is still every file expected.
    assert result.written == len(EXPECTED)


def test_the_import_report_names_the_counts(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_sound(tmp_path / "exports_v2", corpus)

    report = json.loads(
        (corpus_deploy.bookkeeping_root(corpus, "sound") / "import_report.json")
        .read_text(encoding="utf-8")
    )
    assert report["lane"] == "sound"
    assert report["units"] == 2
    assert report["filesWritten"] == len(EXPECTED)
    assert report["failures"] == []


def test_an_export_root_with_no_units_says_what_to_run(tmp_path):
    with pytest.raises(corpus_deploy.CorpusImportError, match="sounds-glb"):
        importer.import_sound(tmp_path / "exports_v2", tmp_path / "corpus")


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


def test_the_command_deploys_the_corpus_and_reports_one_line(monkeypatch, tmp_path):
    config = _configured(monkeypatch, tmp_path)
    _publish(tmp_path / "exports_v2")

    result = RUNNER.invoke(cli.app, ["import", "sound"])
    assert result.exit_code == 0, result.output
    assert "sound corpus import:" in result.output
    assert _deployed(corpus_deploy.corpus_root(config.repo_root), "sound", "lip") == EXPECTED


def test_the_command_fails_when_a_unit_cannot_be_imported(monkeypatch, tmp_path):
    _configured(monkeypatch, tmp_path)
    root = _publish(tmp_path / "exports_v2")
    (root / "sounds" / "area" / "chinatown" / "asian_chimes1.wav.glb").write_bytes(b"junk")

    result = RUNNER.invoke(cli.app, ["import", "sound"])
    assert result.exit_code == int(cli.ExitCode.OFFLINE_EXPORT), result.output
    assert "1 sound unit(s) could not be imported" in result.output


# --- parity with the legacy mirror ---------------------------------------------------------------


def _legacy_root() -> Path | None:
    raw = os.environ.get("ELYSIUM_EXPORT_ROOT", "").strip()
    if not raw:
        work = os.environ.get("ELYSIUM_WORK_ROOT", "").strip()
        raw = str(Path(work) / "exports") if work else ""
    root = Path(raw).expanduser() if raw else None
    return root if root and root.is_dir() else None


@pytest.mark.parametrize("directory, suffix", [("lip", ".lip")])
def test_the_deployed_lip_tree_matches_the_legacy_mirror_byte_for_byte(directory, suffix):
    """The deployed `lip/` mirror is the legacy `lip/` mirror's own bytes, under the legacy key.

    Skipped unless both this machine's legacy export and its deployed corpus exist.
    """

    legacy_root = _legacy_root()
    corpus = Path(__file__).resolve().parents[2] / "Content" / "ElysiumCorpus" / directory
    if legacy_root is None or not (legacy_root / "lip").is_dir() or not corpus.is_dir():
        pytest.skip("the legacy export or the deployed corpus is absent on this machine")

    legacy = {
        path.relative_to(legacy_root / "lip").as_posix().lower(): path
        for path in (legacy_root / "lip").rglob("*" + suffix)
    }
    deployed = {
        path.relative_to(corpus).as_posix().lower(): path for path in corpus.rglob("*" + suffix)
    }
    # 31 of the 7,136 `.lip` files ship with no sibling audio, so no unit carries them: the
    # deployed set is a subset of the legacy one and every shared key is byte-identical.
    assert set(deployed) <= set(legacy)
    for key, path in deployed.items():
        assert path.read_bytes() == legacy[key].read_bytes(), key
