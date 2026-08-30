"""Isolated lossless vdata GLB format aggregation.

One unit is one `vdata/<subtree>/<name>.txt` file (`docs/architecture/seam_map_vdata.md`).
Identity, source resolution, the container and the kind-independent half of validation are
`elysium_pipeline.formats.unit_contract`'s; this package states only what is specific to the kind.
"""

from elysium_pipeline.formats.vdata_glb.decode import (
    VdataDecodeError,
    decode_vdata,
)
from elysium_pipeline.formats.vdata_glb.model import (
    KIND,
    KIND_TITLE,
    SCHEMA_VERSION,
    SOURCE_ROOT,
    SOURCE_SUFFIX,
    VDATA_EXTENSION,
    VdataModel,
    VdataModelError,
    asset_id,
    is_delimited,
    name_of,
    normalize_key,
    output_relative_path,
    source_path,
    subtree_of,
    variant_of,
)
from elysium_pipeline.formats.vdata_glb.source import (
    VdataSourceClosure,
    VdataSourceError,
    load_source_closure,
    source_keys,
)

__all__ = [
    "KIND",
    "KIND_TITLE",
    "SCHEMA_VERSION",
    "SOURCE_ROOT",
    "SOURCE_SUFFIX",
    "VDATA_EXTENSION",
    "VdataDecodeError",
    "VdataModel",
    "VdataModelError",
    "VdataSourceClosure",
    "VdataSourceError",
    "asset_id",
    "decode_vdata",
    "is_delimited",
    "load_source_closure",
    "name_of",
    "normalize_key",
    "output_relative_path",
    "source_keys",
    "source_path",
    "subtree_of",
    "variant_of",
]
