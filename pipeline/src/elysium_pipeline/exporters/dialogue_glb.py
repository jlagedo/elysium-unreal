"""Isolated one-`.dlg`/one-GLB dialogue product writer.

Builds through `formats.dialogue_glb`, writes and reads back through the shared
`formats.unit_contract` container -- one source closure yields one byte-identical product.
Export-time validation re-resolves the `.dlg` member from the index immediately before it runs,
rather than reusing the bytes the decode already held, so a member that changed under the export
fails the re-hash instead of being blessed by the copy that produced the document; the destination
is written only after that validation passes.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable

from elysium_pipeline.formats.dialogue_glb import (
    DIALOGUE_EXTENSION,
    SCHEMA_VERSION,
    decode_dialogue,
    load_source_closure,
    output_relative_path,
)
from elysium_pipeline.formats.dialogue_glb import source as _source
from elysium_pipeline.formats.dialogue_glb.model import DialogueModel
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    extension_root,
    identity_block,
    plain,
    source_resolution,
    write_glb,
)


def build_document(model: DialogueModel) -> tuple[dict, bytes]:
    """The unit's document and its (always empty) BIN payload.

    A dialogue unit is scene-less and text-only: every datum it owns is a row, a cell or a
    tokenized expression, none of which needs a buffer view, so the unit publishes one JSON
    chunk and no BIN chunk.
    """

    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity_block(model.asset, model.source_path),
        source_resolution=source_resolution([model.member]),
        dependencies=model.dependencies,
        coverage=model.coverage,
        encoding="latin-1",
        lines=model.lines,
        expressions=model.expressions,
        audio=model.audio,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document = {
        "asset": asset_block("Dialogue"),
        "extensionsUsed": [DIALOGUE_EXTENSION],
        "extensionsRequired": [DIALOGUE_EXTENSION],
        "extensions": {DIALOGUE_EXTENSION: plain(root)},
    }
    return document, b""


def source_keys(index: dict) -> list[str]:
    """Every dialogue unit key the UP-first install index resolves."""

    return _source.source_keys(index)


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
) -> Path:
    """Decode, build, validate and write one dialogue unit; return the file written."""

    closure = load_source_closure(index, key, read_bytes=read_bytes)
    model = decode_dialogue(closure)
    document, binary = build_document(model)

    from elysium_pipeline.validation import dialogue_glb as validation

    # Re-resolve the `.dlg` member rather than reusing the bytes the decode above already read,
    # so export-time validation's source-hash check can catch a real read/disk discrepancy, not
    # only a writer bug against its own in-memory copy.
    reread = load_source_closure(index, key, read_bytes=read_bytes)
    validation.validate_document(document, binary, source_members=reread.members())
    destination = Path(output_root) / Path(*output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination
