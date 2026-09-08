"""The isolated Expression-table GLB seam.

One unit is one Faceposer expression or phoneme table selected by its stem below
`expressions/`: the compiled `.vfe` the runtime loads and, where it ships, the readable `.txt`
it was compiled from. `formats.unit_contract` owns identity, source resolution, the container,
references, the coverage vocabulary and the byte ledger; this package states only what is
specific to this unit kind.
"""

from elysium_pipeline.formats.expression_table_glb.decode import (
    ExpressionTableDecodeError,
    compare_tables,
    decode_expression_table,
    decode_txt,
    decode_vfe,
)
from elysium_pipeline.formats.expression_table_glb.model import (
    EXPRESSION_TABLE_EXTENSION,
    FAMILY,
    KIND,
    SCHEMA_VERSION,
    SOURCE_DIRECTORY,
    ExpressionTableModel,
    identity_class,
    normalize_stem,
    output_relative_path,
    stem_asset_id,
)
from elysium_pipeline.formats.expression_table_glb.source import (
    ExpressionTableSourceClosure,
    ExpressionTableSourceError,
    load_source_closure,
    source_keys,
)

__all__ = [
    "EXPRESSION_TABLE_EXTENSION",
    "FAMILY",
    "KIND",
    "SCHEMA_VERSION",
    "SOURCE_DIRECTORY",
    "ExpressionTableDecodeError",
    "ExpressionTableModel",
    "ExpressionTableSourceClosure",
    "ExpressionTableSourceError",
    "compare_tables",
    "decode_expression_table",
    "decode_txt",
    "decode_vfe",
    "identity_class",
    "load_source_closure",
    "normalize_stem",
    "output_relative_path",
    "source_keys",
    "stem_asset_id",
]
