"""Isolated one-definition/one-GLB particle product writer."""

from __future__ import annotations

from pathlib import Path
from typing import Callable

from elysium_pipeline.formats.particle_glb import (
    PARTICLE_EXTENSION,
    SCHEMA_VERSION,
    decode_particle,
    load_source_closure,
    output_relative_path,
)
from elysium_pipeline.formats.particle_glb.coverage import particle_coverage
from elysium_pipeline.formats.unit_contract import (
    SOURCE_POLICY,
    asset_block,
    extension_root,
    identity_block,
    plain,
    write_glb,
)


class ParticleGlbError(RuntimeError):
    pass


#: The extension-root keys this seam publishes past the five the contract fixes, in the order
#: `docs/architecture/seam_map_particle.md` -> "GLB structure" states them.
_MAPPED_FIELDS = (
    "identity", "sourceResolution", "root", "keys", "blocks", "projection", "role",
    "precipitation", "comments", "dependencies",
)


def build_document(model) -> tuple[dict, bytes]:
    """The complete GLB document for one decoded particle unit. Scene-less, no BIN chunk: every
    datum the source holds is a name, a flag or a number written as text."""

    keys = [
        {
            "index": entry.index,
            "block": entry.block,
            "key": entry.key,
            "sourceKey": entry.source_key,
            "value": entry.value,
            "parsed": entry.parsed.to_json(),
            "offset": entry.offset,
            "length": entry.length,
            "quotedKey": entry.quoted_key,
            "quotedValue": entry.quoted_value,
        }
        for entry in model.keys
    ]
    blocks = [
        {
            "index": block.index,
            "kind": block.kind,
            "parent": block.parent,
            "offset": block.offset,
            "length": block.length,
            "keys": list(block.keys),
        }
        for block in model.blocks
    ]
    extension = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity_block(
            model.asset_id, [source["path"] for source in model.sources], key=model.key,
            particlePath=model.sources[0]["path"],
        ),
        source_resolution={"policy": SOURCE_POLICY, "members": model.sources},
        dependencies=model.dependencies,
        coverage=particle_coverage(
            mapped=_MAPPED_FIELDS,
            typed_unidentified=model.typed_unidentified,
            omissions=model.omissions,
            ledger_row=model.byte_ledger[0],
            unresolved=model.unresolved,
            unsupported=model.unsupported,
        ),
        root=model.root,
        keys=keys,
        blocks=blocks,
        projection=model.projection,
        role=model.role,
        precipitation=model.precipitation,
        comments=model.comments,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document = {
        "asset": asset_block("Particle"),
        "extensionsUsed": [PARTICLE_EXTENSION],
        "extensionsRequired": [PARTICLE_EXTENSION],
        "extensions": {PARTICLE_EXTENSION: plain(extension)},
    }
    return document, b""


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes: Callable[[dict, str], bytes | None] | None = None,
    particle_exists: Callable[[str], bool] | None = None,
    sprite_exists: Callable[[str], bool] | None = None,
    material_exists: Callable[[str], bool] | None = None,
) -> Path:
    """Write and validate one particle unit.

    Without `*_exists` callables, presence is answered against `index` itself -- the same
    UP-first table the closure was resolved from -- so a corpus run costs no extra reads.
    """

    closure = load_source_closure(index, key, read_bytes=read_bytes)
    if particle_exists is None:
        particle_exists = lambda candidate: f"particles/{candidate}.txt" in index  # noqa: E731
    if sprite_exists is None:
        sprite_exists = lambda candidate: f"particles/{candidate}.tga" in index  # noqa: E731
    if material_exists is None:
        material_exists = lambda candidate: f"materials/{candidate}.vmt" in index  # noqa: E731
    model = decode_particle(
        closure,
        particle_exists=particle_exists,
        sprite_exists=sprite_exists,
        material_exists=material_exists,
    )
    document, binary = build_document(model)

    from elysium_pipeline.validation import particle_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = output_root / Path(*output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination


def source_keys(index: dict) -> list[str]:
    """Every particle-definition key the UP-first install resolves, folded and sorted."""

    from elysium_pipeline.formats.particle_glb.model import normalize_particle_key

    return sorted(
        normalize_particle_key(path)
        for path in index
        if path.startswith("particles/") and path.endswith(".txt")
    )
