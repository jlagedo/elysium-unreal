from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace

from elysium_pipeline import export_manager
from elysium_pipeline.exporters import export_all


class ProfileAndPackageContractTests(unittest.TestCase):
    GRID_MAPS = [
        "sp_tutorial_1",
        "sp_theatre",
        "sm_hub_1",
        "sm_diner_1",
        "sm_bailbonds_1",
        "sm_junkyard_1",
        "sm_asylum_1",
        "sm_gallery_1",
        "sm_pawnshop_1",
        "sm_pawnshop_2",
        "sm_medical_1",
        "sm_pier_1",
        "sm_basement_1",
        "sm_apartment_1",
        "sm_warehouse_1",
        "la_hub_1",
        "sm_tattoo",
        "sm_vamparena",
        "sm_shreknet_1",
        "sm_oceanhouse_1",
        "sm_coffee_1",
        "sm_smoke_1",
    ]

    def test_grid_profile_is_the_exact_test_and_interconnection_set(self) -> None:
        self.assertEqual(export_all.maps_for_profile("grid"), self.GRID_MAPS)

    def test_all_profile_discovers_and_sorts_patch_first_names(self) -> None:
        self.assertEqual(
            export_all.maps_for_profile(
                "all",
                available=["sm_hub_1", "sp_tutorial_1", "sm_hub_1", "sp_theatre"],
            ),
            ["sm_hub_1", "sp_theatre", "sp_tutorial_1"],
        )

    def test_complete_profiles_include_use_icons_and_every_global_bundle(self) -> None:
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
            with self.subTest(profile=profile):
                bundles = export_all.bundles_for_profile(profile)
                self.assertEqual(set(bundles), expected)
                self.assertEqual(len(bundles), len(set(bundles)))

    def test_baked_physical_path_preserves_elysium_baked_mount_layout(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            config = SimpleNamespace(repo_root=repo)
            self.assertEqual(
                export_manager._baked_package(config, "sp_theatre"),
                repo
                / "Plugins"
                / "ElysiumBaked"
                / "Content"
                / "sp_theatre"
                / "sp_theatre.umap",
            )

    def test_virtual_package_roots_remain_immutable(self) -> None:
        repo = Path(__file__).resolve().parents[2]
        engine = (repo / "Config" / "DefaultEngine.ini").read_text(encoding="utf-8")
        bake = (repo / "pipeline" / "unreal" / "bake_map.py").read_text(
            encoding="utf-8"
        )
        descriptor = json.loads(
            (repo / "Plugins" / "ElysiumBaked" / "ElysiumBaked.uplugin").read_text(
                encoding="utf-8"
            )
        )

        self.assertIn("GameDefaultMap=/Game/Elysium.Elysium", engine)
        self.assertIn('MOUNT = "/ElysiumBaked"', bake)
        self.assertIn('"/Game/VtMB/Materials', bake)
        self.assertTrue(descriptor["CanContainContent"])


if __name__ == "__main__":
    unittest.main()
