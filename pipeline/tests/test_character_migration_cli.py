"""Migration checks stay strict about missing products; fidelity is an explicit extra pass."""
import json
from types import SimpleNamespace

import pytest
from typer.testing import CliRunner

from elysium_pipeline import cli, unreal
from elysium_pipeline.importers.characters import staging_root
from elysium_pipeline.validation import native_geometry_stage


@pytest.mark.parametrize("fidelity", [False, True])
def test_native_default_and_explicit_fidelity(tmp_path, monkeypatch, fidelity):
    config = SimpleNamespace(work_root=tmp_path, export_v2_root=tmp_path / "glb")
    root = staging_root(tmp_path)
    root.mkdir(parents=True)
    calls = []

    def execute(state, command, category, action, **options):
        assert options["require_game"] is False
        assert options["require_ue"] is True
        action(config, None)

    def verify(config, runner, manifest, **options):
        calls.append(options["fidelity"])
        (root / "native_verify_report.json").write_text(json.dumps({
            "failed": [], "meshes": 1, "skeletons": 1, "clips": 1, "blendSpaces": 0}))

    def geometry(*args):
        assert fidelity, "ordinary migration verification must not run deferred geometry parity"
        calls.append("geometry")
        return {"passed": False}

    monkeypatch.setattr(cli, "_execute", execute)
    monkeypatch.setattr(unreal, "verify_character_stage", verify)
    monkeypatch.setattr(native_geometry_stage, "verify_geometry_stage", geometry)
    result = CliRunner().invoke(cli.app, ["verify", "characters", *(["--fidelity"] if fidelity else [])])
    assert result.exit_code == (1 if fidelity else 0), result.output
    assert calls == ([True, "geometry"] if fidelity else [False])


def test_missing_native_product_still_fails_without_fidelity(tmp_path, monkeypatch):
    config = SimpleNamespace(work_root=tmp_path, export_v2_root=tmp_path / "glb")
    root = staging_root(tmp_path)
    root.mkdir(parents=True)
    monkeypatch.setattr(cli, "_execute", lambda state, command, category, action, **kw: action(config, None))

    def verify(*args, **kwargs):
        (root / "native_verify_report.json").write_text(json.dumps({"failed": [{"reason": "missing mesh"}]}))

    monkeypatch.setattr(unreal, "verify_character_stage", verify)
    result = CliRunner().invoke(cli.app, ["verify", "characters"])
    assert result.exit_code == 1
    assert "native character verification failed" in str(result.exception)


@pytest.mark.parametrize("lane", ["characters", "wield"])
def test_retired_export_commands_are_not_available(lane):
    result = CliRunner().invoke(cli.app, ["export", lane, "--help"])
    assert result.exit_code == 2
