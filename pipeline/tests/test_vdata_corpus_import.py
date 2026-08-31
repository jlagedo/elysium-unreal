"""Contract tests for `uv run elysium import vdata`.

Every fixture is a synthetic vdata unit exported into a temporary `export_v2` root from hand-built
bytes and a fake install index -- never the real VtMB install, and never a real deployed corpus.
The one test that does read the machine's own trees is the legacy-parity check at the bottom, and
it skips itself when either tree is absent.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest
from typer.testing import CliRunner

from elysium_pipeline import cli
from elysium_pipeline.config import ProjectConfig
from elysium_pipeline.exporters import vdata_glb as exporter
from elysium_pipeline.formats.unit_contract import encode_glb, read_glb
from elysium_pipeline.formats.vdata_glb.model import VDATA_EXTENSION
from elysium_pipeline.importers import vdata as importer

RUNNER = CliRunner()

#: One body per subtree the corpus carries, spelled the way the shipped files are: DOS
#: terminators, a tab between key and value, trailing spaces, and a cp1252 byte above 0x7f.
BODIES = {
    "system/feats": b'Feats\r\n{\r\n\t"Brawl"\t"1"  \r\n}\r\n',
    "system/stats - vampire": b'Stats\r\n{\r\n\t"Strength" "3"\r\n}\r\n',
    "system/stats - hunter": b'Stats\r\n{\r\n\t"Strength" "5"\r\n}\r\n',
    "items/item_w_katana": b'WeaponData\n{\n\t"printname" "Caf\xe9 Katana"\n}\n',
    "camerashots/shot_01": b'CameraShot\n{\n\t"fov" "60"\n}\n',
    "hackterminals/haven_pc": b'Terminal\n{\n\t"Name" "Haven"\n}\n',
    "precache/precache": b'Precache\n{\n\t"a" "models/x.mdl"\n}\n',
    "signs/sign_smiling_jack": b'SignData\n{\n\t"Text" "Smiling Jack"\n}\n',
}


def _publish(export_v2_root: Path, bodies=BODIES) -> Path:
    """Export every fixture body into `<export_v2_root>/vdata/**.glb`."""

    family = Path(export_v2_root) / "vdata"
    index = {f"vdata/{key}.txt": ("loose", f"C:/game/Vampire/vdata/{key}.txt") for key in bodies}
    lookup = {f"vdata/{key}.txt": body for key, body in bodies.items()}
    for key in bodies:
        exporter.export(index, key, family, read_bytes=lambda idx, name: lookup.get(name))
    return family


def _deployed(corpus: Path) -> dict[str, bytes]:
    """Every file below the deployed corpus, keyed by its forward-slashed relative path."""

    return {
        path.relative_to(corpus).as_posix(): path.read_bytes()
        for path in sorted(corpus.rglob("*"))
        if path.is_file()
    }


# --- the importer ------------------------------------------------------------------------------


def test_every_unit_deploys_to_its_own_install_relative_path(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    result = importer.import_vdata(tmp_path / "exports_v2", corpus)

    assert result.failures == []
    assert result.written == len(BODIES) - 1                       # every subtree but signs/
    assert result.unchanged == 0
    assert result.skipped == 1
    assert _deployed(corpus) == {
        f"vdata/{key}.txt": body for key, body in BODIES.items() if not key.startswith("signs/")
    }


def test_a_file_name_keeps_its_spaces_and_its_variant_suffix(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_vdata(tmp_path / "exports_v2", corpus)

    vampire = corpus / "vdata" / "system" / "stats - vampire.txt"
    hunter = corpus / "vdata" / "system" / "stats - hunter.txt"
    assert vampire.is_file() and hunter.is_file()
    assert vampire.read_bytes() == BODIES["system/stats - vampire"]
    assert hunter.read_bytes() == BODIES["system/stats - hunter"]


def test_the_signs_subtree_is_deployed_nowhere(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_vdata(tmp_path / "exports_v2", corpus)
    assert not (corpus / "vdata" / "signs").exists()


def test_a_second_run_over_one_corpus_rewrites_nothing(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_vdata(tmp_path / "exports_v2", corpus)
    stamps = {
        path: path.stat().st_mtime_ns for path in corpus.rglob("*.txt")
    }

    again = importer.import_vdata(tmp_path / "exports_v2", corpus)
    assert again.written == 0 and again.unchanged == len(BODIES) - 1
    assert {path: path.stat().st_mtime_ns for path in corpus.rglob("*.txt")} == stamps


def test_a_stale_file_is_overwritten_with_what_the_unit_carries(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    importer.import_vdata(tmp_path / "exports_v2", corpus)
    target = corpus / "vdata" / "system" / "feats.txt"
    target.write_bytes(b"stale")

    result = importer.import_vdata(tmp_path / "exports_v2", corpus)
    assert result.written == 1 and result.unchanged == len(BODIES) - 2
    assert target.read_bytes() == BODIES["system/feats"]


def test_a_stale_orphan_is_pruned_and_a_signs_file_is_left_alone(tmp_path):
    _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"

    orphan = corpus / "vdata" / "system" / "retired_stat.txt"
    orphan.parent.mkdir(parents=True)
    orphan.write_bytes(b"no longer exported")
    signs_file = corpus / "vdata" / "signs" / "sign_smiling_jack.txt"
    signs_file.parent.mkdir(parents=True)
    signs_file.write_bytes(b"owned by another slice")

    result = importer.import_vdata(tmp_path / "exports_v2", corpus)

    assert not orphan.exists()
    assert result.pruned == 1
    assert signs_file.is_file() and signs_file.read_bytes() == b"owned by another slice"


def test_a_unit_from_before_the_capsule_is_one_failure_and_not_a_crash(tmp_path):
    family = _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"

    # An old-format unit: the JSON chunk of a 1.0.0 export, with no capsule and no BIN chunk.
    stale = family / "system" / "feats.glb"
    document, _binary = read_glb(stale)
    root = document["extensions"][VDATA_EXTENSION]
    root["schemaVersion"] = "1.0.0"
    root["sourceResolution"].pop("capsule")
    root["sourceResolution"]["members"][0].pop("capsule")
    document.pop("buffers")
    document.pop("bufferViews")
    stale.write_bytes(encode_glb(document, b""))

    result = importer.import_vdata(tmp_path / "exports_v2", corpus)
    assert [key for key, _detail in result.failures] == ["system/feats"]
    assert "capsule" in result.failures[0][1]
    assert not (corpus / "vdata" / "system" / "feats.txt").exists()
    # Every other unit still deployed.
    assert result.written == len(BODIES) - 2


def test_a_unit_whose_capsule_disagrees_with_its_digest_deploys_nothing(tmp_path):
    family = _publish(tmp_path / "exports_v2")
    corpus = tmp_path / "Content" / "ElysiumCorpus"
    unit = family / "items" / "item_w_katana.glb"
    document, binary = read_glb(unit)
    unit.write_bytes(encode_glb(document, b"X" + binary[1:]))

    result = importer.import_vdata(tmp_path / "exports_v2", corpus)
    assert [key for key, _detail in result.failures] == ["items/item_w_katana"]
    assert not (corpus / "vdata" / "items").exists()


def test_a_member_path_that_climbs_out_of_the_corpus_is_refused(tmp_path):
    with pytest.raises(importer.VdataImportError):
        importer._destination(tmp_path, "vdata/../../evil.txt")
    with pytest.raises(importer.VdataImportError):
        importer._destination(tmp_path, "scripts/system/feats.txt")
    with pytest.raises(importer.VdataImportError):
        importer._destination(tmp_path, "vdata/system/stealth.xls")


def test_a_drive_qualified_part_cannot_defeat_the_guard_on_windows(tmp_path):
    # 'vdata/C:/evil.txt' folds to two parts ('vdata', 'C:') under PurePosixPath, neither of which
    # is '..' -- but on Windows, Path(root, 'vdata', 'C:', ...) treats 'C:' as a new drive root.
    with pytest.raises(importer.VdataImportError):
        importer._destination(tmp_path, "vdata/C:/evil.txt")
    with pytest.raises(importer.VdataImportError):
        importer._destination(tmp_path, "vdata/x/C:/evil.txt")


def test_an_export_root_with_no_vdata_units_says_what_to_run(tmp_path):
    with pytest.raises(importer.VdataImportError, match="vdatas-glb"):
        importer.import_vdata(tmp_path / "exports_v2", tmp_path / "corpus")


# --- the command -------------------------------------------------------------------------------


def _configured(monkeypatch, tmp_path) -> ProjectConfig:
    """A `ProjectConfig` pointing at temporary roots, with no install and no engine."""

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

    result = RUNNER.invoke(cli.app, ["import", "vdata"])
    assert result.exit_code == 0, result.output
    assert "vdata corpus import:" in result.output
    assert "1 signs skipped" in result.output
    deployed = importer.corpus_root(config.repo_root)
    assert (deployed / "vdata" / "system" / "feats.txt").read_bytes() == BODIES["system/feats"]
    assert not (deployed / "vdata" / "signs").exists()


def test_the_command_fails_when_a_unit_cannot_be_imported(monkeypatch, tmp_path):
    _configured(monkeypatch, tmp_path)
    family = _publish(tmp_path / "exports_v2")
    (family / "items" / "item_w_katana.glb").write_bytes(b"not a glb at all")

    result = RUNNER.invoke(cli.app, ["import", "vdata"])
    assert result.exit_code == int(cli.ExitCode.OFFLINE_EXPORT), result.output
    assert "1 vdata unit(s) could not be imported" in result.output


# --- parity with the legacy mirror ---------------------------------------------------------------


#: The five subtrees slice 1 moves. `signs/` stays on the legacy flat export until its own slice,
#: and the legacy vdata mirror never carried it in the first place.
MIGRATED_SUBTREES = ("system", "items", "camerashots", "hackterminals", "precache")


def _legacy_vdata_root() -> Path | None:
    raw = os.environ.get("ELYSIUM_EXPORT_ROOT", "").strip()
    if not raw:
        return None
    root = Path(raw).expanduser() / "vdata"
    return root if root.is_dir() else None


def test_the_deployed_corpus_is_byte_identical_to_the_legacy_mirror():
    """Env-gated: runs only where both the legacy mirror and a deployed corpus already exist.

    The legacy mirror's file names carry the install's own casing and the deployed corpus carries
    the folded key, so the comparison is by folded relative path; what it asserts is the bytes.
    """

    legacy = _legacy_vdata_root()
    if legacy is None:
        pytest.skip("ELYSIUM_EXPORT_ROOT/vdata is not present on this machine")
    corpus = importer.corpus_root(Path(__file__).resolve().parents[2]) / "vdata"
    if not corpus.is_dir():
        pytest.skip("Content/ElysiumCorpus/vdata is not deployed on this machine")

    deployed = {
        path.relative_to(corpus).as_posix().lower(): path
        for path in corpus.rglob("*.txt")
    }
    compared = 0
    missing: list[str] = []
    differing: list[str] = []
    for path in legacy.rglob("*.txt"):
        key = path.relative_to(legacy).as_posix().lower()
        if not key.startswith(tuple(f"{name}/" for name in MIGRATED_SUBTREES)):
            continue
        target = deployed.get(key)
        if target is None:
            missing.append(key)
            continue
        compared += 1
        if target.read_bytes() != path.read_bytes():
            differing.append(key)
    assert not missing, f"the deployed corpus is missing {len(missing)}: {missing[:5]}"
    assert not differing, f"{len(differing)} file(s) differ: {differing[:5]}"
    assert compared, "the legacy mirror carried no file of a migrated subtree"
