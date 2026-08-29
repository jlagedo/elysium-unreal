from __future__ import annotations

import json
from pathlib import Path
import tempfile
from types import SimpleNamespace

from elysium_pipeline import export_manager, mounts
from elysium_pipeline.exporters import export_all


def test_all_profile_discovers_and_sorts_patch_first_names() -> None:
    assert (export_all.maps_for_profile(
        "all",
        available=["sm_hub_1", "sp_tutorial_1", "sm_hub_1", "sp_theatre"],
    ) == ["sm_hub_1", "sp_theatre", "sp_tutorial_1"])


def test_complete_profiles_include_use_icons_and_every_global_bundle() -> None:
    expected = {
        "audio",
        "particles",
        "scripts",
        "signs",
        "vdata",
        "items",
        "cfg",
        "scenes",
        "ui",
        "use-icons",
        "npc",
    }
    for profile in ("grid", "all"):
        bundles = export_all.bundles_for_profile(profile)
        assert set(bundles) == expected, f"profile={profile}"
        assert len(bundles) == len(set(bundles)), f"profile={profile}"


def test_baked_physical_path_preserves_elysium_baked_mount_layout() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        repo = Path(temporary)
        config = SimpleNamespace(repo_root=repo)
        expected = (
            repo / "Plugins" / "ElysiumBaked" / "Content" / "sp_theatre" / "sp_theatre.umap"
        )
        assert export_manager._baked_package(config, "sp_theatre") == expected


def test_virtual_package_roots_remain_immutable() -> None:
    repo = Path(__file__).resolve().parents[2]
    engine = (repo / "Config" / "DefaultEngine.ini").read_text(encoding="utf-8")
    descriptor = json.loads(
        (repo / "Plugins" / "ElysiumBaked" / "ElysiumBaked.uplugin").read_text(
            encoding="utf-8"
        )
    )

    assert "GameDefaultMap=/Game/ElysiumGenerated/Boot.Boot" in engine
    assert mounts.BAKED == "/ElysiumBaked"
    assert mounts.MATERIALS == "/Game/ElysiumGenerated/Materials"
    assert mounts.CLOTH == "/ElysiumBaked/Characters/Cloth"
    assert descriptor["CanContainContent"]
