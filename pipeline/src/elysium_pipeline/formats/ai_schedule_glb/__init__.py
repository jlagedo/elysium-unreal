"""The ai-schedule GLB seam: VtMB's 691 NPC behaviour programs, their id spaces and the parser's
vocabulary, recovered from `Vampire/dlls/vampire.dll`.

Specified by `docs/contracts/seam_map_ai_schedule.md`; the retail facts are owned by
`docs/vtmb/npc-ai/schedule-kernel.md`. This is the first seam whose source is CODE rather than
content, which is why it carries an image mapper and an instruction walk of its own.
"""

from elysium_pipeline.formats.ai_schedule_glb.census import (
    ANCHORS,
    CATEGORIES,
    Census,
    CensusError,
    Owner,
    Registration,
    ScheduleText,
    Space,
    build as build_census,
    summary,
)
from elysium_pipeline.formats.ai_schedule_glb.decode import (
    IMAGE_RESIDUE,
    decode_space_unit,
    decode_vocabulary_unit,
)
from elysium_pipeline.formats.ai_schedule_glb.image import (
    IMAGE_BASE,
    PINNED_BYTE_LENGTH,
    PINNED_SHA256,
    ImageError,
    PEImage,
)
from elysium_pipeline.formats.ai_schedule_glb.model import (
    AI_SCHEDULE_EXTENSION,
    AiScheduleModelError,
    CORPUS_ROOT,
    KIND,
    KIND_TITLE,
    ROOT_KEY,
    SCHEMA_VERSION,
    SOURCE_MEMBER,
    TEXT_SUFFIX,
    SpaceRow,
    SpaceUnitModel,
    VocabularyUnitModel,
    asset_id,
    corpus_directory,
    member_path,
    normalize_key,
    output_relative_path,
    split_member_path,
    text_file_name,
)
from elysium_pipeline.formats.ai_schedule_glb.parser import (
    MAX_TASKS,
    OPERAND_PREFIXES,
    RAW_WORD_PREFIXES,
    ScheduleRecord,
    ScheduleTextError,
    parse,
    tokenize,
)
from elysium_pipeline.formats.ai_schedule_glb.source import (
    AiScheduleSourceClosure,
    AiScheduleSourceError,
    load_source_closure,
    source_keys,
)
from elysium_pipeline.formats.ai_schedule_glb.walk import WalkError, body_bounds, walk_body

__all__ = [
    "AI_SCHEDULE_EXTENSION",
    "ANCHORS",
    "AiScheduleModelError",
    "AiScheduleSourceClosure",
    "AiScheduleSourceError",
    "CATEGORIES",
    "CORPUS_ROOT",
    "Census",
    "CensusError",
    "IMAGE_BASE",
    "IMAGE_RESIDUE",
    "ImageError",
    "KIND",
    "KIND_TITLE",
    "MAX_TASKS",
    "OPERAND_PREFIXES",
    "Owner",
    "PEImage",
    "PINNED_BYTE_LENGTH",
    "PINNED_SHA256",
    "RAW_WORD_PREFIXES",
    "ROOT_KEY",
    "Registration",
    "SCHEMA_VERSION",
    "SOURCE_MEMBER",
    "ScheduleRecord",
    "ScheduleText",
    "ScheduleTextError",
    "Space",
    "SpaceRow",
    "SpaceUnitModel",
    "TEXT_SUFFIX",
    "VocabularyUnitModel",
    "WalkError",
    "asset_id",
    "body_bounds",
    "build_census",
    "corpus_directory",
    "decode_space_unit",
    "decode_vocabulary_unit",
    "load_source_closure",
    "member_path",
    "normalize_key",
    "output_relative_path",
    "parse",
    "source_keys",
    "split_member_path",
    "summary",
    "text_file_name",
    "tokenize",
    "walk_body",
]
