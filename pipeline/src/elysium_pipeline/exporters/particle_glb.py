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


#: Named, single-entry provenance divergence (`water-complete.md` Phase 0 verdict F2):
#: `waterbigsplash_emitter` is authored-empty in the Unofficial Patch's loose override -- the
#: UP-first policy's own answer for this key -- with its one `spawn{}` block commented out
#: (`// removed by wesp`, 161 B, `role: "neither"`, `blocks: []`, `unbalanced-braces @68`), while
#: the retail `pack001.vpk` member (143 B, offset 70407456) still carries it live: `spawn {
#: particle "WaterBigSplash" burst "4" ... }`. Staging the UP stub would mean the water-entry big
#: splash (D1/F1: `waterLevel 0 -> >=1`, `velocity.z < -200 in/s`) spawns nothing. For this one
#: key, read the retail VPK member instead and record the divergence in `sourceResolution` (its
#: `origin.kind` is `"vpk"`, `origin.container` is `pack001.vpk`, same shape as any other member --
#: nothing marks the product specially; this set is the record of *why* the UP-first answer was
#: not trusted). Every other key resolves UP-first as usual.
from elysium_pipeline.formats.unit_contract.source_policy import RETAIL_PARTICLE_KEYS, unit_source_policy

RETAIL_PROVENANCE_DIVERGENCE_UNITS = RETAIL_PARTICLE_KEYS


def _retail_index_override(index: dict, path: str, *, retail_index: dict | None = None) -> dict:
    """`index` with `path`'s UP-first answer replaced by the retail-VPK-only answer, when one
    exists. Used only for `RETAIL_PROVENANCE_DIVERGENCE_UNITS`.

    The merged UP-first index keeps only the winning (loose) answer for a shadowed key and
    discards the VPK one it shadowed, so the retail answer has to be looked up separately.
    `retail_index` is the injection point tests use to stand up a synthetic retail answer without
    touching the real install; production callers leave it unset and pay one extra VPK directory
    read (`vpk.index_all` reads only the tail of each pack, the same read `install.build_index`
    itself performs to build the base layer).
    """

    if retail_index is None:
        from elysium_pipeline.formats import install, vpk

        retail_index = vpk.index_all(install.GAME)
    entry = retail_index.get(path)
    if entry is None:
        return index                       # nothing to override with; caller's index stands
    overridden = dict(index)
    overridden[path] = ("vpk", entry)
    return overridden


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
        source_resolution={"policy": SOURCE_POLICY, "members": model.sources,
                           **({"overridePolicy": unit_source_policy(model.asset_id, model.sources[0]["path"])}
                              if unit_source_policy(model.asset_id, model.sources[0]["path"]) else {})},
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
    retail_index: dict | None = None,
) -> Path:
    """Write and validate one particle unit.

    Without `*_exists` callables, presence is answered against `index` itself -- the same
    UP-first table the closure was resolved from -- so a corpus run costs no extra reads.
    `retail_index` only matters for a key in `RETAIL_PROVENANCE_DIVERGENCE_UNITS`; see
    `_retail_index_override`.
    """

    from elysium_pipeline.formats.particle_glb.model import normalize_particle_key, source_path

    folded = normalize_particle_key(key)
    if folded in RETAIL_PROVENANCE_DIVERGENCE_UNITS:
        index = _retail_index_override(index, source_path(folded), retail_index=retail_index)
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
