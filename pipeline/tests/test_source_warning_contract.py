from __future__ import annotations

import unittest

from elysium_pipeline.exporters.source_warnings import (
    animated_prop_warning,
    missing_npc_warning,
)


def test_only_known_missing_npc_model_is_a_warning() -> None:
    warning = missing_npc_warning(
        "models/character/npc/doppleganger/doppleganger.mdl"
    )
    assert warning is not None
    assert warning["fallback"] == "unresolved"
    assert missing_npc_warning("models/character/npc/other/missing.mdl") is None


def test_known_malformed_prop_requires_a_static_fallback() -> None:
    model = "models/scenery/misc/bottles/bottleb.mdl"
    warning = animated_prop_warning(
        model,
        ValueError("truncated"),
        has_static_fallback=True,
    )
    assert warning is not None
    assert warning["fallback"] == "per-map static model_mesh"
    assert animated_prop_warning(
            model,
            ValueError("truncated"),
            has_static_fallback=False,
        ) is None
    assert animated_prop_warning(
            "models/scenery/misc/other.mdl",
            ValueError("truncated"),
            has_static_fallback=True,
        ) is None
