"""Pure-Python worker checks; no Unreal process, packages, or generated assets."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace

import pytest


@pytest.fixture
def worker():
    path = Path(__file__).parents[1] / "unreal" / "import_expression_tables.py"
    spec = importlib.util.spec_from_file_location("expression_worker_under_test", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize("command", [
    '-ImportExpressionTables="E:/scratch space/manifest.json" -ImportForce=1',
    '"-ImportExpressionTables=E:/scratch space/manifest.json" -ImportForce=1',
    '-IMPORTEXPRESSIONTABLES=E:/scratch/manifest.json -ImportForce=1',
])
def test_worker_arguments_survive_windows_paths(worker, command):
    expected = "E:/scratch space/manifest.json" if "space" in command else "E:/scratch/manifest.json"
    assert worker.argument(command, "ImportExpressionTables") == expected
    assert worker.argument(command, "ImportForce") == "1"
    assert worker.argument(command, "Absent") == ""


@pytest.fixture
def native_stub():
    calls = []

    class Data:
        def __init__(self):
            self.owner, self.payload = "expression-tables", "expected"

        @staticmethod
        def verify(asset, encoded):
            calls.append("verify")
            return "" if asset.payload == encoded else "values changed"

        @staticmethod
        def apply_json(asset, encoded):
            calls.append("apply")
            asset.payload = encoded
            return asset, ""

    data = Data()
    unreal = SimpleNamespace(load_asset=lambda path: data,
        EditorAssetLibrary=SimpleNamespace(get_metadata_tag=lambda asset, tag: asset.owner),
        log_warning=lambda message: calls.append("warning"))
    bl = SimpleNamespace(PRODUCER_TAG="producer", stored_recipe=lambda *a, **k: "recipe",
        stamp_recipe=lambda *a, **k: calls.append("stamp"), save=lambda path: calls.append("save") or True)
    return unreal, bl, Data, data, calls


def test_current_recipe_also_verifies_native_payload(worker, native_stub):
    unreal, bl, kind, data, calls = native_stub
    assert worker._publish(unreal, bl, kind, "/asset", "expected", "recipe", False, "expression-tables") == "reused"
    assert calls == ["verify"]
    calls.clear()
    data.payload = "a row was silently dropped"
    assert worker._publish(unreal, bl, kind, "/asset", "expected", "recipe", False, "expression-tables") == "imported"
    assert calls == ["verify", "warning", "apply", "stamp", "save", "verify"]


@pytest.mark.parametrize("owner", ["", "another-lane"])
def test_foreign_or_unstamped_asset_is_never_overwritten(worker, native_stub, owner):
    unreal, bl, kind, data, calls = native_stub
    data.owner = owner
    with pytest.raises(RuntimeError, match="foreign or unstamped"):
        worker._publish(unreal, bl, kind, "/asset", "expected", "recipe", True, "expression-tables")
    assert not calls


def test_save_failure_prevents_success(worker, native_stub):
    unreal, bl, kind, data, calls = native_stub
    bl.save = lambda path: False
    with pytest.raises(RuntimeError, match="save failed"):
        worker._publish(unreal, bl, kind, "/asset", "expected", "recipe", True, "expression-tables")
