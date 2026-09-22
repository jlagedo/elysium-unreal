"""Isolated one-space/one-GLB ai-schedule product writer.

Every unit is scene-less and declares no accessor: a schedule text names tasks, conditions and
numbers, all of which Troika wrote as text. A space unit's BIN chunk is the source capsule alone --
each of its texts' exact bytes, cut from the image as a span -- so the unit is everything the
import lane needs to reproduce the program retail's own parser read.

The root unit is the exception the contract's executable-image rule licenses: it names the image as
its member, its ledger partitions all 7.86 MB of it, and it carries NO capsule, because reproducing
an engine binary out of a GLB is not a thing any consumer of this corpus needs.
"""

from __future__ import annotations

from pathlib import Path

from elysium_pipeline.formats.ai_schedule_glb import (
    AiScheduleSourceError,
    CensusError,
    KIND_TITLE,
    SCHEMA_VERSION,
    AI_SCHEDULE_EXTENSION,
    ROOT_KEY,
    SOURCE_MEMBER,
    decode_space_unit,
    decode_vocabulary_unit,
    load_source_closure,
    output_relative_path,
)
from elysium_pipeline.formats.ai_schedule_glb import source_keys as _source_keys  # re-export
from elysium_pipeline.formats.ai_schedule_glb import vocabulary as vocabulary_module
from elysium_pipeline.formats.ai_schedule_glb.image import PEImage
from elysium_pipeline.formats.ai_schedule_glb.model import (
    SpaceUnitModel,
    VocabularyUnitModel,
    asset_id,
)
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    buffer_table,
    coverage_block,
    encapsulate,
    extension_root,
    identity_block,
    plain,
    source_resolution,
    write_glb,
)

#: Re-exported so the plural export command can enumerate this seam's units from an index without
#: importing `formats.ai_schedule_glb` directly.
source_keys = _source_keys

#: What a complete unit of each shape accounts for.
_SPACE_MAPPED = (
    "identity",
    "sourceResolution",
    "spaces",
    "registrations",
    "texts",
    "records",
    "dependencies",
)
_ROOT_MAPPED = (
    "identity",
    "sourceResolution",
    "partition",
    "namespaces",
    "squadSlots",
    "vocabulary",
    "classes",
    "order",
    "census",
)


class AiScheduleGlbError(RuntimeError):
    pass


def build_space_document(model: SpaceUnitModel) -> tuple[dict, bytes]:
    identity = identity_block(
        model.asset,
        SOURCE_MEMBER,
        className=model.class_name,
        classNames=list(model.class_names),
        initBody=f"{model.init_body:#010x}",
    )
    # Eight owners feed no text; they are published for their parent links alone, so they have no
    # member, no capsule and no BIN chunk. `encapsulate` of nothing answers exactly that.
    resolution, buffer_views, binary = encapsulate(model.members)
    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity,
        source_resolution=resolution,
        dependencies=model.dependencies,
        coverage=coverage_block(
            mapped=_SPACE_MAPPED,
            typed_unidentified=model.typed_unidentified,
            omitted_proven=model.omissions,
            byte_ledger=model.ledger,
        ),
        spaces={category: row.to_json() for category, row in model.spaces.items()},
        registrations=model.registrations,
        texts=model.texts,
        records=model.records,
        activities=model.activities,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document: dict = {
        "asset": asset_block(KIND_TITLE),
        "extensionsUsed": [AI_SCHEDULE_EXTENSION],
        "extensionsRequired": [AI_SCHEDULE_EXTENSION],
    }
    if binary:
        document["buffers"] = buffer_table(binary)
        document["bufferViews"] = buffer_views
    document["extensions"] = {AI_SCHEDULE_EXTENSION: plain(root)}
    return document, binary


def build_vocabulary_document(model: VocabularyUnitModel) -> tuple[dict, bytes]:
    assert model.member is not None                      # the decode always sets it
    identity = identity_block(model.asset, SOURCE_MEMBER, role="vocabulary")
    # No capsule: the member is the image. `sourceResolution` therefore publishes no `capsule` key,
    # which is what says this unit has not adopted the rule rather than having broken it.
    resolution = source_resolution([model.member])
    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity,
        source_resolution=resolution,
        dependencies=model.dependencies,
        coverage=coverage_block(
            mapped=_ROOT_MAPPED,
            omitted_proven=model.omissions,
            byte_ledger=model.ledger,
        ),
        partition=model.partition,
        namespaces=model.namespaces,
        squadSlots=model.squad_slots,
        vocabulary=model.vocabulary,
        classes=model.classes,
        order=model.order,
        deadDoors=model.dead_doors,
        census=model.census,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document: dict = {
        "asset": asset_block(KIND_TITLE),
        "extensionsUsed": [AI_SCHEDULE_EXTENSION],
        "extensionsRequired": [AI_SCHEDULE_EXTENSION],
        "extensions": {AI_SCHEDULE_EXTENSION: plain(root)},
    }
    return document, b""


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes=None,
) -> Path:
    """Write and validate one ai-schedule unit."""

    try:
        closure = load_source_closure(index, key, read_bytes=read_bytes)
        image = PEImage(closure.data)
        vocab = vocabulary_module.build(image)
        if closure.key == ROOT_KEY:
            model = decode_vocabulary_unit(
                image, closure.census, closure.data, closure.origin, vocab
            )
            document, binary = build_vocabulary_document(model)
            members = (model.member,)
        else:
            space = decode_space_unit(image, closure.census, closure.key, closure.origin, vocab)
            document, binary = build_space_document(space)
            members = tuple(space.members)
    except (AiScheduleSourceError, CensusError, vocabulary_module.VocabularyError) as error:
        raise AiScheduleGlbError(f"{key}: {error}") from error

    from elysium_pipeline.validation import ai_schedule_glb as validation

    validation.validate_document(document, binary, source_members=members)
    destination = output_root / Path(*Path(output_relative_path(closure.key)).parts)
    write_glb(document, binary, destination)
    return destination
