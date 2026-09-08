"""Isolated ui-resource GLB format aggregation.

One unit is one VGUI2 `.res` layout/scheme, one VGUI1 dialog script, one HUD sprite table, one
key-binding table, the launcher/options scripts, the localized string table or the main-menu
particle scene. Identity, source resolution, the
container and the kind-independent half of validation are `elysium_pipeline.formats.unit_contract`'s;
this package states only what is specific to the kind.
"""

from elysium_pipeline.formats.ui_resource_glb.decode import (
    Resolvers,
    UiResourceDecodeError,
    decode_ui_resource,
)
from elysium_pipeline.formats.ui_resource_glb.model import (
    FAMILY_DIR,
    KIND,
    KIND_TITLE,
    SCHEMA_VERSION,
    UI_RESOURCE_EXTENSION,
    UiResourceModel,
    UiResourceModelError,
    asset_id,
    classify,
    dormant_evidence,
    encoding_of,
    grammar_of,
    is_dormant,
    normalize_key,
    output_relative_path,
)
from elysium_pipeline.formats.ui_resource_glb.source import (
    UiResourceSourceClosure,
    UiResourceSourceError,
    load_source_closure,
    source_keys,
)

__all__ = [
    "FAMILY_DIR",
    "KIND",
    "KIND_TITLE",
    "SCHEMA_VERSION",
    "UI_RESOURCE_EXTENSION",
    "Resolvers",
    "UiResourceDecodeError",
    "UiResourceModel",
    "UiResourceModelError",
    "UiResourceSourceClosure",
    "UiResourceSourceError",
    "asset_id",
    "classify",
    "decode_ui_resource",
    "dormant_evidence",
    "encoding_of",
    "grammar_of",
    "is_dormant",
    "load_source_closure",
    "normalize_key",
    "output_relative_path",
    "source_keys",
]
