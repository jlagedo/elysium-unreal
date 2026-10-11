"""The complete physics surface source table, retaining byte and token order.

Unlike per-surface visual/audio units this identity owns the whole parser input, so duplicate
entry names and token-position base overrides are never folded away before runtime replay.
"""
from dataclasses import asdict
from pathlib import Path

from elysium_pipeline.formats import install
from elysium_pipeline.formats.surface_property_glb import lexer
from elysium_pipeline.formats.physics_surfaces import decode_semantics
from elysium_pipeline.formats.unit_contract import (
    ByteLedger, SourceMember, asset_block, buffer_table, coverage_block, encapsulate,
    extension_root, identity_block, origin_of, read_member, source_capsules, write_glb, read_glb,
)

EXTENSION = "ELYSIUM_vtmb_physics_surfaces"
FAMILY = "physics-surfaces"
SOURCE = "scripts/surfaceproperties.txt"


def export_table(export_v2_root: Path) -> Path:
    index = install.build_index()
    data = read_member(index, SOURCE)
    if data is None:
        raise ValueError(f"missing {SOURCE}")
    # Lossless lexical decode is inspection metadata, never the runtime payload. Capsule is raw.
    tokens = lexer.tokenize(data.decode("latin-1"))
    if sum(token.length for token in tokens) != len(data):
        raise ValueError("surface source token coverage is incomplete")
    ledger = ByteLedger(SOURCE, data)
    for ordinal, token in enumerate(tokens):
        ledger.claim(token.offset, token.length, "mapped-text", f"lexical span {ordinal}: {token.kind}")
    member = SourceMember("table", SOURCE, data, origin_of(index[SOURCE]))
    resolution, views, binary = encapsulate([member])
    root = extension_root(
        schema_version="1.0.0",
        identity=identity_block("vtmb:physics-surfaces:table", SOURCE),
        source_resolution=resolution, dependencies=[],
        coverage=coverage_block(mapped=["ordered lexical spans; complete raw parser input"],
                                byte_ledger=[ledger.finish()]),
        tokens=[asdict(token) for token in tokens],
        semantics=decode_semantics(data),
    )
    document = {"asset": asset_block("Physics surface parser source"),
                "extensionsUsed": [EXTENSION], "extensionsRequired": [EXTENSION],
                "extensions": {EXTENSION: root}, "buffers": buffer_table(binary),
                "bufferViews": views}
    destination = Path(export_v2_root) / FAMILY / "table.glb"
    write_glb(document, binary, destination)
    captured, payload = read_glb(destination)
    if source_capsules(captured, payload, captured["extensions"][EXTENSION])[SOURCE] != data:
        raise ValueError("published physics source capsule differs from the install")
    return destination
