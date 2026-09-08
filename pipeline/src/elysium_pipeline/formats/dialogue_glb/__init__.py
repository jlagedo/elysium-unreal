"""The Dialogue GLB seam: one `.dlg` conversation, one binary glTF unit.

The dialogue seam owns the rules; the unit contract owns the shared shape every export_v2
unit follows.
"""

from elysium_pipeline.formats.dialogue_glb import exprlex, rules
from elysium_pipeline.formats.dialogue_glb.decode import decode_dialogue
from elysium_pipeline.formats.dialogue_glb.model import (
    DIALOGUE_EXTENSION,
    SCHEMA_VERSION,
    DialogueModel,
    asset_id,
    audio_dir_for,
    normalize_dlg_key,
    output_relative_path,
    source_path_for,
)
from elysium_pipeline.formats.dialogue_glb.source import (
    DialogueSourceClosure,
    DialogueSourceError,
    load_source_closure,
    source_keys,
)

__all__ = [
    "DIALOGUE_EXTENSION",
    "SCHEMA_VERSION",
    "DialogueModel",
    "DialogueSourceClosure",
    "DialogueSourceError",
    "asset_id",
    "audio_dir_for",
    "decode_dialogue",
    "load_source_closure",
    "normalize_dlg_key",
    "output_relative_path",
    "source_keys",
    "source_path_for",
    "exprlex",
    "rules",
]
