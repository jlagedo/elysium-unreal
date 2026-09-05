"""Authored-key migration must preserve values and refuse ambiguous keys before mutation."""
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace as NS
from unittest import mock

import pytest


@pytest.fixture
def migration(tmp_path):
    class Value:
        def __init__(self, text):
            self.text = text

        def export_text(self):
            return self.text

    class Asset:
        def __init__(self, values):
            self.values = values
            self.writes = 0

        def get_editor_property(self, field):
            return self.values.copy()

        def set_editor_property(self, field, values):
            self.writes += 1
            self.values = values.copy()

    hair = "/Game/ElysiumAuthored/Hair/DA_HairDynamics"
    cloth = "/Game/ElysiumAuthored/Cloth/DA_ClothTuning"
    assets = {hair: Asset({"hero": Value('(Chains=((Stiffness=0.875)),Enabled=False)')}),
              cloth: Asset({"villain": Value('(Material="silk_panel",Scale=1.125)')})}
    for path in assets:
        local = tmp_path / (path.removeprefix("/Game/") + ".uasset")
        local.parent.mkdir(parents=True, exist_ok=True)
        local.write_bytes(b"original authored package")
    unreal = NS(load_asset=assets.get, Name=str, Paths=NS(project_content_dir=lambda: str(tmp_path)))
    saved = []
    bl = NS(save=lambda path: saved.append(path) or True)
    spec = importlib.util.spec_from_file_location("tuning_migration_test", Path(__file__).parents[1] / "unreal/migrate_character_tuning.py")
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"unreal": unreal, "pipeline.unreal": NS(bake_lib=bl)}):
        spec.loader.exec_module(module)
    cast = {"models": {"vtmb:model:a/hero": {}, "vtmb:model:b/villain": {}},
            "aliases": {"hero": "vtmb:model:a/hero", "villain": "vtmb:model:b/villain"}}
    return NS(module=module, assets=assets, cast=cast, root=tmp_path / "stage", saved=saved, hair=hair, cloth=cloth)


def test_rekey_preserves_authored_values_and_fresh_verification_detects_changes(migration):
    m = migration
    before = {path: [v.export_text() for v in a.values.values()] for path, a in m.assets.items()}
    result = m.module.migrate(m.cast, m.root)
    assert all(row["changed"] for row in result)
    assert len(m.saved) == 2
    assert before == {path: [v.export_text() for v in a.values.values()] for path, a in m.assets.items()}
    assert all(row["valuesPreserved"] for row in m.module.verify(m.cast, m.root))
    backups = m.root / "authored_tuning_before_ids"
    assert len(list(backups.glob("*.uasset"))) == 2
    original = json.loads((backups / "DA_ClothTuning.json").read_text())
    assert original["keys"] == {"villain": "vtmb:model:b/villain"}
    assert not any(row["changed"] for row in m.module.migrate(m.cast, m.root))
    assert len(m.saved) == 2
    next(iter(m.assets[m.cloth].values.values())).text = "changed material"
    with pytest.raises(RuntimeError, match="values differ"):
        m.module.verify(m.cast, m.root)


def test_second_table_ambiguity_leaves_first_table_untouched(migration):
    m = migration
    del m.cast["aliases"]["villain"]
    with pytest.raises(ValueError, match="no unique cast model"):
        m.module.migrate(m.cast, m.root)
    assert not m.saved
    assert all(asset.writes == 0 for asset in m.assets.values())
    assert "hero" in m.assets[m.hair].values
