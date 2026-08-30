"""One script, one GLB: the writer for the `vtmb:script:` seam.

A level script holds nothing a general consumer can draw or play, so the unit is scene-less and
carries no BIN chunk: every datum it owns is text, a token or a small integer table the extension
states directly. Validation runs against the members the closure resolved before anything is
written, because the ledger's claims can only be weighed against the bytes while they are in hand.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Callable

from elysium_pipeline.formats.script_glb import (
    SCHEMA_VERSION,
    SCRIPT_EXTENSION,
    ScriptModel,
    decode_script,
    load_source_closure,
    output_relative_path,
)
from elysium_pipeline.formats.script_glb.model import KIND_TITLE
from elysium_pipeline.formats.script_glb.source import source_keys as _source_keys
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    extension_root,
    identity_block,
    source_resolution,
    write_glb,
)


def source_keys(index: dict) -> list[str]:
    """Every script key the install index resolves, in sorted order."""

    return _source_keys(index)


def _members_block(model: ScriptModel, executed: dict[str, bool]) -> dict[str, Any]:
    """The member table, each row saying whether the interpreter can execute that member.

    CPython 2.1 has no `zipimport`, so a companion that ships only inside a VPK never runs; the
    row states it so a reader does not have to know the rule.
    """

    block = source_resolution(model.members)
    for row in block["members"]:
        row["executed"] = bool(executed.get(str(row["role"]), False))
    return block


def build_document(
    model: ScriptModel, executed: dict[str, bool] | None = None
) -> tuple[dict, bytes]:
    """The unit's JSON document and its (always empty) binary payload."""

    executed = executed or {}
    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity_block(
            model.asset,
            [member.path for member in model.members],
            scriptPath=model.key,
            sourceKind=model.source_kind,
        ),
        source_resolution=_members_block(model, executed),
        dependencies=list(model.dependencies),
        coverage=model.coverage,
        source=model.source,
        tokens=[token.to_json() for token in model.tokens],
        structure=model.structure,
        references=[reference.to_json() for reference in model.references],
        entityNames=[record.to_json() for record in model.entity_names],
        pyc=model.pyc,
        anomalies=list(model.anomalies),
        omissions=list(model.omissions),
    )
    document = {
        "asset": asset_block(KIND_TITLE),
        "extensionsUsed": [SCRIPT_EXTENSION],
        "extensionsRequired": [SCRIPT_EXTENSION],
        "extensions": {SCRIPT_EXTENSION: root},
    }
    # A script carries no geometry, no image and no sampled signal, so there is nothing for a
    # core accessor to hold and no BIN chunk to hold it in.
    return document, b""


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
    closure=None,
) -> Path:
    """Decode, validate against the resolved members, then write one script unit."""

    if closure is None:
        closure = load_source_closure(index, key, read_bytes=read_bytes)
    model = decode_script(closure, member_exists=lambda path: path in index)
    document, binary = build_document(model, closure.executed)
    from elysium_pipeline.validation import script_glb as validation

    validation.validate_document(
        document,
        binary,
        source_members=closure.members(),
        member_exists=lambda path: path in index,
    )
    destination = Path(output_root) / Path(*output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination
