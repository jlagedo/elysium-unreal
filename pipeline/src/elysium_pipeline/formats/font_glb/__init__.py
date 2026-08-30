"""Isolated lossless Font GLB format aggregation.

Two identities: `vtmb:font:<stem>` for one `.fnt` glyph table, and the single
`vtmb:font-list:fontlist` registry unit. Both build on `elysium_pipeline.formats.unit_contract`.
"""

from elysium_pipeline.formats.font_glb.coverage import (
    FONT_LIST_MAPPED_SECTIONS,
    FONT_MAPPED_SECTIONS,
    font_coverage,
    font_list_coverage,
)
from elysium_pipeline.formats.font_glb.decode import (
    FontDecodeError,
    decode_font,
    decode_font_list,
    parse_registry_rows,
)
from elysium_pipeline.formats.font_glb.model import (
    FONT_EXTENSION,
    FONT_LIST_EXTENSION,
    FONT_LIST_KEY,
    FONT_LIST_PATH,
    FONTS_DIR,
    SCHEMA_VERSION,
    FontListModel,
    FontModel,
    NameParts,
    asset_id,
    font_list_asset_id,
    font_list_output_relative_path,
    font_path,
    normalize_font_key,
    output_relative_path,
    page_material_path,
    page_texture_path,
    parse_name,
    registry_key,
)
from elysium_pipeline.formats.font_glb.source import (
    FontListSourceClosure,
    FontSourceClosure,
    FontSourceError,
    PageAvailability,
    font_keys,
    load_font_list_closure,
    load_source_closure,
)

__all__ = [
    "FONT_EXTENSION",
    "FONT_LIST_EXTENSION",
    "FONT_LIST_KEY",
    "FONT_LIST_MAPPED_SECTIONS",
    "FONT_LIST_PATH",
    "FONT_MAPPED_SECTIONS",
    "FONTS_DIR",
    "SCHEMA_VERSION",
    "FontDecodeError",
    "FontListModel",
    "FontListSourceClosure",
    "FontModel",
    "FontSourceClosure",
    "FontSourceError",
    "NameParts",
    "PageAvailability",
    "asset_id",
    "decode_font",
    "decode_font_list",
    "font_coverage",
    "font_keys",
    "font_list_asset_id",
    "font_list_coverage",
    "font_list_output_relative_path",
    "font_path",
    "load_font_list_closure",
    "load_source_closure",
    "normalize_font_key",
    "output_relative_path",
    "page_material_path",
    "page_texture_path",
    "parse_name",
    "parse_registry_rows",
    "registry_key",
]
