"""Isolated lossless engine-config GLB format aggregation.

One unit is one console script, table or binary state file the seam names
explicitly. Identity, source resolution, the container and the kind-independent half of
validation are `elysium_pipeline.formats.unit_contract`'s; this package states only what is
specific to the kind.
"""

from elysium_pipeline.formats.engine_config_glb.decode import (
    EngineConfigDecodeError,
    decode_engine_config,
)
from elysium_pipeline.formats.engine_config_glb.model import (
    ALL_GRAMMAR_KEYS,
    CONSOLE_SCRIPT_KEYS,
    ENGINE_CONFIG_EXTENSION,
    GRAMMAR_BINARY,
    GRAMMAR_CONSOLE_SCRIPT,
    GRAMMAR_FIELDS,
    GRAMMAR_KEY_EQUALS_VALUE,
    GRAMMAR_KEYVALUES,
    GRAMMAR_LINE_LIST,
    GRAMMAR_LOCALIZED_LIST,
    GRAMMAR_RAD_ROWS,
    GRAMMAR_SAVE_FRAGMENT,
    GRAMMARS,
    KEY_GRAMMAR,
    KIND,
    KIND_TITLE,
    KNOWN_KEYS,
    RESIDUE_KEYS,
    SCHEMA_VERSION,
    EngineConfigModel,
    EngineConfigModelError,
    asset_id,
    dependency_asset_id,
    grammar_for,
    is_residue,
    normalize_key,
    output_relative_path,
)
from elysium_pipeline.formats.engine_config_glb.source import (
    EngineConfigSourceClosure,
    EngineConfigSourceError,
    load_source_closure,
    member_resolved,
    source_keys,
)

__all__ = [
    "ALL_GRAMMAR_KEYS",
    "CONSOLE_SCRIPT_KEYS",
    "ENGINE_CONFIG_EXTENSION",
    "GRAMMAR_BINARY",
    "GRAMMAR_CONSOLE_SCRIPT",
    "GRAMMAR_FIELDS",
    "GRAMMAR_KEY_EQUALS_VALUE",
    "GRAMMAR_KEYVALUES",
    "GRAMMAR_LINE_LIST",
    "GRAMMAR_LOCALIZED_LIST",
    "GRAMMAR_RAD_ROWS",
    "GRAMMAR_SAVE_FRAGMENT",
    "GRAMMARS",
    "KEY_GRAMMAR",
    "KIND",
    "KIND_TITLE",
    "KNOWN_KEYS",
    "RESIDUE_KEYS",
    "SCHEMA_VERSION",
    "EngineConfigDecodeError",
    "EngineConfigModel",
    "EngineConfigModelError",
    "EngineConfigSourceClosure",
    "EngineConfigSourceError",
    "asset_id",
    "decode_engine_config",
    "dependency_asset_id",
    "grammar_for",
    "is_residue",
    "load_source_closure",
    "member_resolved",
    "normalize_key",
    "output_relative_path",
    "source_keys",
]
