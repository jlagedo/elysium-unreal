"""Narrow allowlist for known source-corpus defects.

These entries keep a reproducible export moving without turning future decoder
or source failures into warnings. Every accepted warning is also written into
the generated NPC manifests and surfaced by ``elysium doctor``.
"""

from __future__ import annotations


KNOWN_MISSING_NPC_MODELS = frozenset(
    {
        "models/character/npc/doppleganger/doppleganger.mdl",
    }
)

KNOWN_STATIC_ANIMATED_PROP_FALLBACKS = frozenset(
    {
        "models/scenery/misc/bottles/bottleb.mdl",
        "models/scenery/misc/bottles/bottlec.mdl",
        "models/scenery/theater/stage_light.mdl",
    }
)

# Two hidden Warrens interaction records use prop_switch for its use/lock/output surface while the
# model itself is a one-frame hatch carrying only `only_sequence`. Retail's named sequence lookups
# therefore resolve no transition clips; the static rest pose remains the honest presentation.
KNOWN_SWITCHES_WITHOUT_SWITCH_CLIPS = frozenset(
    {
        "models/scenery/structural/warrens/warr_02_container_door.mdl",
    }
)


def missing_npc_warning(model: str) -> dict[str, str] | None:
    normalized = model.lower().replace("\\", "/")
    if normalized not in KNOWN_MISSING_NPC_MODELS:
        return None
    return {
        "code": "missing-source-npc-model",
        "model": normalized,
        "fallback": "unresolved",
        "detail": "the patch-first game filesystem does not contain this referenced model",
    }


def animated_prop_warning(
    model: str,
    error: BaseException,
    *,
    has_static_fallback: bool,
) -> dict[str, str] | None:
    normalized = model.lower().replace("\\", "/")
    if (
        normalized not in KNOWN_STATIC_ANIMATED_PROP_FALLBACKS
        or not has_static_fallback
    ):
        return None
    return {
        "code": "animated-prop-static-fallback",
        "model": normalized,
        "fallback": "per-map static model_mesh",
        "detail": str(error) or type(error).__name__,
    }


def missing_intrinsic_prop_clips_warning(model: str, clips: list[str]) -> dict[str, str] | None:
    normalized = model.lower().replace("\\", "/")
    if normalized not in KNOWN_SWITCHES_WITHOUT_SWITCH_CLIPS:
        return None
    return {
        "code": "missing-intrinsic-prop-clips",
        "model": normalized,
        "fallback": "authored static rest pose",
        "detail": "model does not declare runtime switch clip(s): " + ", ".join(clips),
    }
