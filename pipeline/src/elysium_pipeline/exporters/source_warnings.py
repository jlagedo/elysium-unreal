"""Narrow allowlist for known source-corpus defects.

These entries keep a reproducible export moving without turning future decoder
or source failures into warnings. Every accepted warning is also written into
the generated NPC manifests and surfaced by ``elysium doctor``.
"""

from __future__ import annotations


KNOWN_MISSING_NPC_MODELS = frozenset(
    {
        "models/character/npc/doppleganger/doppleganger.mdl",
        # la_library_1's npc_VVampire "Stuntman" ships the literal placeholder name;
        # no file of this name exists anywhere in the install.
        "models/missing.mdl",
    }
)

KNOWN_STATIC_ANIMATED_PROP_FALLBACKS = frozenset(
    {
        "models/scenery/misc/bottles/bottleb.mdl",
        "models/scenery/misc/bottles/bottlec.mdl",
        "models/scenery/theater/stage_light.mdl",
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


def missing_intrinsic_prop_clips_warning(
    model: str, clips: list[str], *, declared: int = 0
) -> dict[str, str]:
    """A switch-class entity placed on a model without its full switch vocabulary.

    Map data, not an export defect: 18 shipped models are used this way, from partial
    sets (`curcuitbreaker` lacks only `deactivate`) down to one-frame `only_sequence`
    props (the temple pedestals, the Warrens hatches). Retail's named sequence lookup
    resolves -1 for an absent clip and the transition simply does not animate, and the
    runtime's `InputSetAnimation` no-ops the same way, so the export ships the clips
    the model declares and reports the gap."""
    normalized = model.lower().replace("\\", "/")
    return {
        "code": "missing-intrinsic-prop-clips",
        "model": normalized,
        "fallback": "the declared clip subset" if declared else "authored static rest pose",
        "detail": "model does not declare runtime switch clip(s): " + ", ".join(clips),
    }
