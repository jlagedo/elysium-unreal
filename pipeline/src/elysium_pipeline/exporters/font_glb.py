"""Isolated Font GLB product writer.

Two units, one module: `export` writes one `vtmb:font:<stem>` unit and `export_font_list` writes
the single `vtmb:font-list:fontlist` registry unit. Both are scene-less and carry no BIN chunk.
`fonts-glb`'s plural entry point publishes both: `source_keys` appends the registry's own sentinel
key to the `.fnt` stems it enumerates, and `export` recognizes that sentinel and dispatches to
`export_font_list` -- so the join the seam doc requires ("the join is checked in both directions")
is checked in one corpus run, without touching the shared per-key runner in `workers.py`.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from elysium_pipeline.formats.font_glb import (
    FONT_EXTENSION,
    FONT_LIST_EXTENSION,
    FONT_LIST_KEY,
    FONT_LIST_PATH,
    SCHEMA_VERSION,
    decode_font,
    decode_font_list,
    font_coverage,
    font_keys,
    font_list_asset_id,
    font_list_coverage,
    font_list_output_relative_path,
    load_font_list_closure,
    load_source_closure,
    output_relative_path,
    parse_registry_rows,
)
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    identity_block,
    extension_root,
    plain,
    source_resolution,
    write_glb,
)

#: Every spelling `fonts-glb`'s sentinel, or a direct `font-list-glb`/`font-glb` call, may name the
#: registry unit with.
_FONT_LIST_KEY_ALIASES = frozenset({FONT_LIST_KEY, FONT_LIST_PATH.lower()})


def _is_font_list_key(key: str) -> bool:
    return str(key).replace("\\", "/").strip().strip("/").lower() in _FONT_LIST_KEY_ALIASES


def build_document(model) -> tuple[dict, bytes]:
    """The font unit: `vtmb:font:<stem>` decoded from one `.fnt` glyph table."""

    extra: dict[str, Any] = {}
    if model.name is not None:
        extra = {
            "face": model.name.face,
            "size": model.name.size,
            "weight": model.name.weight,
            "flags": model.name.flags,
        }
    identity = identity_block(model.asset_id, model.members[0].path, **extra)
    coverage = font_coverage(
        typed_unidentified=model.typed_unidentified,
        unresolved=model.unresolved,
        unsupported=model.unsupported,
        byte_ledger=model.byte_ledger,
        omitted_proven=model.omissions,
    )
    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity,
        source_resolution=source_resolution(model.members),
        dependencies=model.dependencies,
        coverage=coverage,
        header=model.header,
        charMap=model.char_map,
        glyphs=model.glyphs,
        pages=model.pages,
        fontList=model.font_list,
        trailer=model.trailer,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document = {
        "asset": asset_block("Font"),
        "extensionsUsed": [FONT_EXTENSION],
        "extensionsRequired": [FONT_EXTENSION],
        "extensions": {FONT_EXTENSION: plain(root)},
    }
    return document, b""


def build_font_list_document(model) -> tuple[dict, bytes]:
    """The font-list unit: the single `vtmb:font-list:fontlist` registry.

    Its own `<kind>` is `font-list`, so it declares its own extension, `ELYSIUM_vtmb_font_list`,
    distinct from the font unit's -- mirroring the sibling two-identity seam, `shader_program_glb`.
    """

    identity = identity_block(font_list_asset_id(), model.members[0].path)
    coverage = font_list_coverage(
        unresolved=model.unresolved,
        unsupported=model.unsupported,
        byte_ledger=model.byte_ledger,
        omitted_proven=model.omitted_proven,
    )
    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity,
        source_resolution=source_resolution(model.members),
        dependencies=model.dependencies,
        coverage=coverage,
        rows=model.rows,
        comments=model.comments,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document = {
        "asset": asset_block("Font List"),
        "extensionsUsed": [FONT_LIST_EXTENSION],
        "extensionsRequired": [FONT_LIST_EXTENSION],
        "extensions": {FONT_LIST_EXTENSION: plain(root)},
    }
    return document, b""


def export(index: dict, key: str, output_root: Path, *, read_bytes=None) -> Path:
    """Write and validate one font unit. Tolerates the `materials/fonts/` root and `.fnt`.

    `key` naming the font-list registry (the sentinel `fonts-glb`'s `source_keys` appends, or the
    bare `fontlist`/`materials/fonts/fontlist.txt` spellings) dispatches to `export_font_list`
    instead, so the one plural corpus command publishes both this seam's units.
    """

    if _is_font_list_key(key):
        return export_font_list(index, output_root, read_bytes=read_bytes)

    closure = load_source_closure(index, key, read_bytes=read_bytes)
    font_list_rows = None
    if closure.font_list_present:
        list_closure = load_font_list_closure(index, read_bytes=read_bytes)
        font_list_rows = parse_registry_rows(list_closure.source.data)
    model = decode_font(closure, font_list_rows=font_list_rows)
    document, binary = build_document(model)
    from elysium_pipeline.validation import font_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = Path(output_root) / Path(*output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination


def export_font_list(index: dict, output_root: Path, *, read_bytes=None) -> Path:
    """Write and validate the single font-list unit."""

    closure = load_font_list_closure(index, read_bytes=read_bytes)
    available = set(font_keys(index))
    model = decode_font_list(closure, font_keys=available)
    document, binary = build_font_list_document(model)
    from elysium_pipeline.validation import font_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = Path(output_root) / Path(*font_list_output_relative_path().parts)
    write_glb(document, binary, destination)
    return destination


def source_keys(index: dict) -> list[str]:
    """Every `.fnt` stem the install resolves, plus the font-list registry sentinel when the
    install carries one, for the `fonts-glb` corpus command."""

    keys = font_keys(index)
    if FONT_LIST_PATH in index:
        keys = [*keys, FONT_LIST_KEY]
    return keys
