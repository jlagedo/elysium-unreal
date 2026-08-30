"""Isolated one-scene/one-GLB choreographed-scene product writer."""

from __future__ import annotations

from pathlib import Path

from elysium_pipeline.formats.scene_glb import (
    SCENE_EXTENSION,
    SCHEMA_VERSION,
    decode_scene,
    load_source_closure,
    output_relative_path,
)
from elysium_pipeline.formats.scene_glb.coverage import build_coverage
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    extension_root,
    identity_block,
    plain,
    source_resolution,
    write_glb,
)


def build_document(model) -> tuple[dict, bytes]:
    """The extension root and the GLB document for one decoded scene. No BIN chunk: a scene's
    timeline is an event list keyed by actor name, not a sampled animation, so it lives entirely
    in the extension."""

    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity_block(model.asset_id, model.source_path, key=model.key),
        source_resolution=source_resolution([model.member]),
        dependencies=model.dependencies,
        coverage=build_coverage(
            byte_ledger_row=model.byte_ledger[0],
            unresolved=model.unresolved,
            unsupported=model.unsupported,
            omissions=model.omissions,
        ),
        version=model.version,
        fps=model.fps,
        snap=model.snap,
        actors=model.actors,
        scriptExpressions=model.script_expressions,
        comments=model.comments,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document = {
        "asset": asset_block("Scene"),
        "extensionsUsed": [SCENE_EXTENSION],
        "extensionsRequired": [SCENE_EXTENSION],
        "extensions": {SCENE_EXTENSION: plain(root)},
    }
    return document, b""


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes=None,
    sound_exists=None,
    expression_table_exists=None,
) -> Path:
    """Write and validate one choreographed-scene unit."""

    closure = load_source_closure(index, key, read_bytes=read_bytes)
    if sound_exists is None:
        def sound_exists(candidate: str) -> bool:
            return candidate in index
    if expression_table_exists is None:
        def expression_table_exists(stem: str) -> bool:
            folded = stem.strip().lower()
            return f"expressions/{folded}.vfe" in index or f"expressions/{folded}.txt" in index

    model = decode_scene(
        closure, sound_exists=sound_exists, expression_table_exists=expression_table_exists
    )
    document, binary = build_document(model)
    from elysium_pipeline.validation import scene_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = Path(output_root) / Path(*output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination


def source_keys(index: dict) -> list[str]:
    """Every `.vcd` below `sound/` the install resolves, as `export`-ready normalized keys."""

    prefix, suffix = "sound/", ".vcd"
    return sorted(
        path[len(prefix):-len(suffix)]
        for path in index
        if path.startswith(prefix) and path.endswith(suffix)
    )
