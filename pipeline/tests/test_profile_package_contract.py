from __future__ import annotations

import json
from pathlib import Path
import tempfile
from types import SimpleNamespace

from elysium_pipeline import export_manager, mounts
from elysium_pipeline.exporters import export_all


def test_no_profile_carries_a_map_half() -> None:
    # 0018 story 21-5 deleted the BSP decoder, so a profile decodes no map: a map is published
    # as `export_v2` units and goes to a level through `bake map`. `maps`/`discover` left
    # `profiles.toml` with the pass that read them.
    for name in ("grid", "all"):
        profile = export_all.load_profiles()[name]
        assert "maps" not in profile and "discover" not in profile
        assert export_all.maps_for_profile(name, available=["sm_hub_1"]) == []


def test_complete_profiles_include_every_global_bundle() -> None:
    # R6.6: `use-icons` is gone -- the HUD draws the 72 context icons off the texture lane's
    # `T_` assets, so no profile composites an atlas any more. R8: `npc` and `items` are gone --
    # characters and wield are native import lanes (`import characters`), not export bundles.
    # AUD0.4: `audio` is gone -- the sound family is `export_v2` units deployed by
    # `import sound` / `import sound-schemes`. 0018 story 21-6: `scripts`, `signs`, `cfg` and `ui`
    # are gone with their extractors -- those four trees are the game's, and the game reads them
    # out of `Content/ElysiumCorpus` where `import scripts` / `... vdata` / `... engine-config` /
    # `... ui-strings` deploy them from the published units. What is left here writes nothing the
    # running game opens.
    expected = {
        "particles",
        "vdata",
        "scenes",
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
