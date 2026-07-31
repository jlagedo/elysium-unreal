from __future__ import annotations

import unittest

from elysium_pipeline.exporters.source_warnings import (
    animated_prop_warning,
    missing_npc_warning,
)


class SourceWarningContractTests(unittest.TestCase):
    def test_only_known_missing_npc_model_is_a_warning(self) -> None:
        warning = missing_npc_warning(
            "models/character/npc/doppleganger/doppleganger.mdl"
        )
        self.assertIsNotNone(warning)
        self.assertEqual(warning["fallback"], "unresolved")
        self.assertIsNone(missing_npc_warning("models/character/npc/other/missing.mdl"))

    def test_known_malformed_prop_requires_a_static_fallback(self) -> None:
        model = "models/scenery/misc/bottles/bottleb.mdl"
        warning = animated_prop_warning(
            model,
            ValueError("truncated"),
            has_static_fallback=True,
        )
        self.assertIsNotNone(warning)
        self.assertEqual(warning["fallback"], "per-map static model_mesh")
        self.assertIsNone(
            animated_prop_warning(
                model,
                ValueError("truncated"),
                has_static_fallback=False,
            )
        )
        self.assertIsNone(
            animated_prop_warning(
                "models/scenery/misc/other.mdl",
                ValueError("truncated"),
                has_static_fallback=True,
            )
        )


if __name__ == "__main__":
    unittest.main()
