"""Isolated lossless sound-scheme GLB format aggregation.

One unit is one `sound/schemes/*.txt` KeyValues file (`docs/architecture/seam_map_sound_scheme.md`).
Identity, source resolution, the container and the kind-independent half of validation are
`elysium_pipeline.formats.unit_contract`'s; this package states only what is specific to the kind.
"""

from elysium_pipeline.formats.sound_scheme_glb.decode import (
    SoundSchemeDecodeError,
    decode_sound_scheme,
)
from elysium_pipeline.formats.sound_scheme_glb.model import (
    DSP_PRESET_PATH,
    KIND,
    KIND_TITLE,
    SCHEMA_VERSION,
    SOURCE_ROOT,
    SOURCE_SUFFIX,
    Parameter,
    SoundSchemeModel,
    SoundSchemeModelError,
    asset_id,
    dsp_preset_asset_id,
    normalize_stem,
    output_relative_path,
    sound_asset_id,
    sound_dependency_source_path,
    sound_source_path,
    source_path,
)
from elysium_pipeline.formats.sound_scheme_glb.source import (
    SoundSchemeSourceClosure,
    SoundSchemeSourceError,
    load_dsp_preset_ids,
    load_source_closure,
    sound_resolved,
    source_keys,
)
from elysium_pipeline.formats.unit_contract.coverage import extension_name

#: `ELYSIUM_vtmb_sound_scheme` -- derived from `KIND` through the shared contract helper rather
#: than respelled here, so the extension name this seam declares cannot drift from it.
SOUND_SCHEME_EXTENSION = extension_name(KIND)

__all__ = [
    "DSP_PRESET_PATH",
    "KIND",
    "KIND_TITLE",
    "SCHEMA_VERSION",
    "SOUND_SCHEME_EXTENSION",
    "SOURCE_ROOT",
    "SOURCE_SUFFIX",
    "Parameter",
    "SoundSchemeDecodeError",
    "SoundSchemeModel",
    "SoundSchemeModelError",
    "SoundSchemeSourceClosure",
    "SoundSchemeSourceError",
    "asset_id",
    "decode_sound_scheme",
    "dsp_preset_asset_id",
    "load_dsp_preset_ids",
    "load_source_closure",
    "normalize_stem",
    "output_relative_path",
    "sound_asset_id",
    "sound_dependency_source_path",
    "sound_resolved",
    "sound_source_path",
    "source_keys",
    "source_path",
]
