"""The `shots_diff.py --save` baseline manifest (R2.1, roadmap MP-1.1).

A baseline is only a useful regression witness if it states what it was captured against, so
`--save` writes a `baseline.json` beside every promoted map naming the build commit, the map and
the camera set. These tests pin that shape -- not a literal commit or camera census, which would
fire on every ordinary corpus or vantage-table change -- and the fact that `--save` writes it for
real when promoting a capture.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

import pytest

from elysium_pipeline.paths import repo_root
from elysium_pipeline.validation import shots_diff

COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


def test_baseline_manifest_names_commit_map_and_camera_set() -> None:
    manifest = shots_diff.baseline_manifest("sm_hub_1", ["h2", "h1"], commit="deadbeef" * 5)

    assert set(manifest) == {"commit", "map", "cameras"}
    assert manifest["commit"] == "deadbeef" * 5
    assert manifest["map"] == "sm_hub_1"
    # Sorted, not insertion order: two promotions of the same map must diff cleanly.
    assert manifest["cameras"] == ["h1", "h2"]


def test_write_baseline_manifest_round_trips_through_json(tmp_path: Path) -> None:
    written = shots_diff.write_baseline_manifest(tmp_path, "sp_tutorial_1", ["t1", "t2"])

    on_disk = json.loads((tmp_path / shots_diff.BASELINE_MANIFEST).read_text(encoding="utf-8"))
    assert on_disk == written
    assert on_disk["map"] == "sp_tutorial_1"
    assert on_disk["cameras"] == ["t1", "t2"]


def test_git_commit_reports_unknown_outside_a_checkout(tmp_path: Path) -> None:
    assert shots_diff.git_commit(tmp_path) == "unknown"


def test_git_commit_resolves_this_repositorys_head() -> None:
    commit = shots_diff.git_commit(repo_root())
    assert COMMIT_RE.match(commit), f"expected a 40-hex commit, got {commit!r}"


def _capture_dir(root: Path, map_name: str) -> Path:
    """An empty-but-valid capture directory: a manifest with no shots, so the diff loop's
    per-camera pairing has nothing to iterate and only the per-map header line is exercised.
    """
    mapdir = root / map_name
    mapdir.mkdir(parents=True)
    (mapdir / "manifest.json").write_text(
        json.dumps({"map": map_name, "shots": []}), encoding="utf-8")
    return mapdir


def test_diff_header_names_the_baseline_commit_from_the_manifest(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str],
) -> None:
    out = tmp_path / "_shots"
    baseline = out / "_baseline"
    monkeypatch.setattr(shots_diff, "OUT", out)
    monkeypatch.setattr(shots_diff, "BASELINE", baseline)
    monkeypatch.setattr(shots_diff, "DIFFDIR", out / "_diff")

    _capture_dir(out, "sm_hub_1")
    base_dir = _capture_dir(baseline, "sm_hub_1")
    (base_dir / shots_diff.BASELINE_MANIFEST).write_text(
        json.dumps({"commit": "abc123abc123abc123deadbeef", "map": "sm_hub_1",
                    "cameras": ["h1", "h2"]}),
        encoding="utf-8")

    monkeypatch.setattr("sys.argv", ["shots_diff"])
    with pytest.raises(SystemExit) as exc_info:
        shots_diff.main()

    assert exc_info.value.code == 0
    out_text = capsys.readouterr().out
    assert "sm_hub_1: (baseline abc123abc123, 2 cam)" in out_text


def test_diff_header_falls_back_when_the_baseline_predates_the_manifest(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str],
) -> None:
    out = tmp_path / "_shots"
    baseline = out / "_baseline"
    monkeypatch.setattr(shots_diff, "OUT", out)
    monkeypatch.setattr(shots_diff, "BASELINE", baseline)
    monkeypatch.setattr(shots_diff, "DIFFDIR", out / "_diff")

    _capture_dir(out, "sm_hub_1")
    _capture_dir(baseline, "sm_hub_1")  # no baseline.json beside it -- a pre-R2.1 capture

    monkeypatch.setattr("sys.argv", ["shots_diff"])
    with pytest.raises(SystemExit) as exc_info:
        shots_diff.main()

    assert exc_info.value.code == 0
    out_text = capsys.readouterr().out
    assert "sm_hub_1: (baseline commit unknown — pre-R2.1 capture, no baseline.json)" in out_text


def test_diff_header_survives_a_corrupt_baseline_manifest(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str],
) -> None:
    """A truncated `baseline.json` -- plausible after an interrupted `--save` -- must not abort
    the whole diff run with a traceback indistinguishable from a real regression exit.
    """
    out = tmp_path / "_shots"
    baseline = out / "_baseline"
    monkeypatch.setattr(shots_diff, "OUT", out)
    monkeypatch.setattr(shots_diff, "BASELINE", baseline)
    monkeypatch.setattr(shots_diff, "DIFFDIR", out / "_diff")

    _capture_dir(out, "sm_hub_1")
    base_dir = _capture_dir(baseline, "sm_hub_1")
    (base_dir / shots_diff.BASELINE_MANIFEST).write_text("{truncated", encoding="utf-8")

    monkeypatch.setattr("sys.argv", ["shots_diff"])
    with pytest.raises(SystemExit) as exc_info:
        shots_diff.main()

    assert exc_info.value.code == 0
    out_text = capsys.readouterr().out
    assert "sm_hub_1: (baseline manifest unreadable" in out_text


def test_save_writes_the_baseline_manifest_beside_the_promoted_capture(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch,
) -> None:
    out = tmp_path / "_shots"
    baseline = out / "_baseline"
    monkeypatch.setattr(shots_diff, "OUT", out)
    monkeypatch.setattr(shots_diff, "BASELINE", baseline)

    capture = out / "sm_hub_1"
    capture.mkdir(parents=True)
    (capture / "sm_hub_1_h1.png").write_bytes(b"fake-png")
    (capture / "sm_hub_1_h2.png").write_bytes(b"fake-png")
    manifest = {
        "map": "sm_hub_1",
        "sm6": True,
        "shots": [
            {"cam": "h1", "file": "sm_hub_1_h1.png", "width": 4, "height": 4, "ok": True},
            # A vantage whose capture failed never earns a place in the promoted camera set.
            {"cam": "h2", "file": "sm_hub_1_h2.png", "width": 0, "height": 0, "ok": False},
        ],
    }
    (capture / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")

    monkeypatch.setattr("sys.argv", ["shots_diff", "--save"])
    shots_diff.main()

    written = json.loads((baseline / "sm_hub_1" / shots_diff.BASELINE_MANIFEST)
                          .read_text(encoding="utf-8"))
    assert written["map"] == "sm_hub_1"
    assert written["cameras"] == ["h1"]
    assert COMMIT_RE.match(written["commit"]) or written["commit"] == "unknown"
