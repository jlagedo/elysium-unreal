"""Census plus parse into the two unit models, with the byte ledger that proves the decode.

A space unit's ledger partitions each of its texts into the tokens the engine's tokenizer actually
reads -- the `Schedule` keyword, the name, `Tasks`, every task name and operand, every interrupt,
every flag -- with the separators between them graded `omitted-proven`. That is the whole reason
the export runs a real tokenizer instead of a regex: a regex cannot say which bytes it did not read.

The root unit's ledger partitions the image: every unit span claimed under the unit that took it,
and every remaining byte `omitted-proven` under one named owner. No `-zero` claim is made anywhere,
so nothing is asserted about bytes the seam did not read.
"""

from __future__ import annotations

from typing import Any, Callable

from elysium_pipeline.formats.ai_schedule_glb import parser, vocabulary
from elysium_pipeline.formats.ai_schedule_glb.census import Census, CensusError, Owner, summary
from elysium_pipeline.formats.ai_schedule_glb.image import PEImage
from elysium_pipeline.formats.ai_schedule_glb.model import (
    ROOT_KEY,
    SOURCE_MEMBER,
    SpaceRow,
    SpaceUnitModel,
    VocabularyUnitModel,
    asset_id,
    member_path,
    text_file_name,
)
from elysium_pipeline.formats.unit_contract.ledger import ByteLedger
from elysium_pipeline.formats.unit_contract.origin import Origin, SourceMember
from elysium_pipeline.formats.unit_contract.references import dependency

#: The owner a ledger range takes when the seam decodes no content there.
IMAGE_RESIDUE = "image.notDecodedAsContent"


def _space_rows(owner: Owner, by_space: dict[int, str]) -> dict[str, SpaceRow]:
    rows: dict[str, SpaceRow] = {}
    for category, space in owner.spaces.items():
        rows[category] = SpaceRow(
            category=category,
            address=space.address,
            namespace=space.namespace,
            parent=space.parent,
            parent_unit=by_space.get(space.parent) if space.parent else None,
            init_va=space.init_va,
        )
    return rows


def _text_ledger(path: str, member: SourceMember, record: parser.ScheduleRecord) -> dict[str, Any]:
    """One text's gapless ledger: every token owned, every separator proven omitted."""

    ledger = ByteLedger(path, member.data, span_offset=member.span_offset)
    spans = sorted(record.spans, key=lambda row: row[1])
    cursor = 0
    for owner, offset, length in spans:
        if length <= 0:
            continue
        if offset < cursor:
            continue                      # an operand span already covered by its prefix span
        if offset > cursor:
            ledger.claim(cursor, offset - cursor, "omitted-proven", "whitespace")
        ledger.claim(offset, length, "mapped-text", f"records[0].{owner}")
        cursor = offset + length
    if cursor < len(member.data):
        ledger.claim(cursor, len(member.data) - cursor, "omitted-proven", "whitespace")
    return ledger.finish()


def _record_json(record: parser.ScheduleRecord, local_id: int | None) -> dict[str, Any]:
    return {
        "name": record.name,
        "localId": local_id,
        "tasks": [
            {
                "name": task.name,
                "operand": {
                    "form": task.operand.form,
                    "spelling": task.operand.spelling,
                    **({"prefix": task.operand.prefix} if task.operand.prefix else {}),
                    **({"resolver": task.operand.resolver} if task.operand.resolver else {}),
                    **({"rawWord": True} if task.operand.raw_word else {}),
                    "sourceOffset": task.operand.offset,
                },
            }
            for task in record.tasks
        ],
        "interrupts": [
            {"name": row.name, "inverted": row.inverted, "sourceOffset": row.offset}
            for row in record.interrupts
        ],
        "flags": list(record.flags),
        "flagWord": record.flag_word,
    }


def _typed_unidentified(record: parser.ScheduleRecord, vocab: vocabulary.Vocabulary) -> list[dict]:
    """Operands the resolver answers but whose meaning is not in the vocabulary.

    These are retail's own non-failing quirks, recorded rather than smoothed: a `MiscFlag:` name
    outside the 22 reads 0 silently, a bare token that is not a number `_atof`s to 0.0, and a
    `HintFlags:` token matching both `nearest` and `random` warns and reads as nearest.
    """

    rows: list[dict] = []
    misc = {name.lower() for name in vocab.misc_flags}
    for task in record.tasks:
        operand = task.operand
        if operand.prefix == "miscflag" and operand.spelling.lower() not in misc:
            rows.append(
                {
                    "kind": "miscflag-not-in-table",
                    "task": task.name,
                    "spelling": operand.spelling,
                    "reads": 0,
                    "sourceOffset": operand.offset,
                }
            )
        elif operand.prefix == "hintflags":
            lowered = operand.spelling.lower()
            if "nearest" in lowered and "random" in lowered:
                rows.append(
                    {
                        "kind": "hintflags-nearest-and-random",
                        "task": task.name,
                        "spelling": operand.spelling,
                        "reads": vocabulary.HINT_FLAGS["nearest"],
                        "sourceOffset": operand.offset,
                    }
                )
        elif operand.form == "number" and not parser.is_number(operand.spelling):
            rows.append(
                {
                    "kind": "operand-not-a-number",
                    "task": task.name,
                    "spelling": operand.spelling,
                    "reads": 0.0,
                    "sourceOffset": operand.offset,
                }
            )
    return rows


def decode_space_unit(
    image: PEImage,
    census: Census,
    key: str,
    origin: Origin,
    vocab: vocabulary.Vocabulary,
) -> SpaceUnitModel:
    """One space unit: its members, records, registrations and ledger."""

    owner = census.by_key().get(key)
    if owner is None:
        raise CensusError(f"no ai-schedule owner named {key!r} in this image")

    by_space = {
        row.schedule_space: row.key for row in census.owners if row.schedule_space is not None
    }
    local_of = {row.name: row.local_id for row in owner.registrations_of("schedule")}

    model = SpaceUnitModel(
        key=key,
        class_name=owner.class_name,
        class_names=sorted(set(owner.class_names or [owner.class_name])),
        init_body=owner.init_body,
        spaces=_space_rows(owner, by_space),
    )
    model.registrations = {
        category: [
            {"name": row.name, "localId": row.local_id, "sourceVa": f"{row.source_va:#010x}"}
            for row in owner.registrations_of(category)
        ]
        for category in ("schedule", "task", "condition", "squadSlot")
    }
    model.registrations["squadSlot"] = [
        {"name": row.name, "localId": row.local_id, "sourceVa": f"{row.source_va:#010x}"}
        for row in owner.registrations_of("squadslot")
    ]

    activities: set[str] = set()
    for text in owner.texts:
        records = parser.parse(text.body)
        if not records:
            raise CensusError(
                f"{key}: the text at {text.string_va:#010x} declares no record; the region rule "
                "admitted a run the parser does not read"
            )
        record = records[0]
        path = member_path(key, record.name)
        member = SourceMember(
            role="schedule-text",
            path=path,
            data=text.body,
            origin=origin,
            span=(text.source_offset, text.byte_length),
        )
        model.members.append(member)
        model.texts.append(
            {
                "order": text.order,
                "name": record.name,
                "file": text_file_name(record.name),
                "stringVa": f"{text.string_va:#010x}",
                "sourceOffset": text.source_offset,
                "byteLength": text.byte_length,
                "feedVa": f"{text.feed_va:#010x}",
            }
        )
        model.records.append(_record_json(record, local_of.get(record.name)))
        model.ledger.append(_text_ledger(path, member, record))
        model.typed_unidentified.extend(_typed_unidentified(record, vocab))
        for task in record.tasks:
            if task.operand.prefix == "activity":
                activities.add(task.operand.spelling)

    model.activities = sorted(activities)

    # Dependencies: the parent of each space, and the vocabulary every unit resolves through.
    for category, row in model.spaces.items():
        if row.parent_unit:
            model.dependencies.append(
                dependency(
                    role=f"{category}-space-parent",
                    source_path=f"{SOURCE_MEMBER}#space:{row.parent:#010x}",
                    asset=asset_id(row.parent_unit),
                    resolved=True,
                )
            )
    model.dependencies.append(
        dependency(
            role="vocabulary",
            source_path=SOURCE_MEMBER,
            asset=asset_id(ROOT_KEY),
            resolved=True,
        )
    )

    for row in census.anomalies:
        if row.get("unit") == key:
            model.anomalies.append(row)
    model.omissions.append(
        {"role": "whitespace", "reason": "token separators the engine tokenizer consumes"}
    )
    return model


def decode_vocabulary_unit(
    image: PEImage,
    census: Census,
    data: bytes,
    origin: Origin,
    vocab: vocabulary.Vocabulary,
) -> VocabularyUnitModel:
    """The root unit: the vocabulary, the class map, the order, and the image partition."""

    model = VocabularyUnitModel()
    model.member = SourceMember(role="image", path=SOURCE_MEMBER, data=data, origin=origin)

    ledger = ByteLedger(SOURCE_MEMBER, data)
    spans: list[tuple[int, int, str, str]] = []
    for owner in census.owners:
        for text in owner.texts:
            records = parser.parse(text.body)
            name = records[0].name if records else ""
            spans.append((text.source_offset, text.byte_length, owner.key, name))
    spans.sort()

    cursor = 0
    for offset, length, key, name in spans:
        if offset < cursor:
            raise CensusError(
                f"two unit spans overlap at file offset {offset}; a text is cut once"
            )
        if offset > cursor:
            ledger.claim(cursor, offset - cursor, "omitted-proven", IMAGE_RESIDUE)
        ledger.claim(offset, length, "mapped-text", f"vtmb:ai-schedule:{key}")
        model.partition.append(
            {
                "unit": key,
                "member": member_path(key, name) if name else None,
                "offset": offset,
                "length": length,
            }
        )
        cursor = offset + length
    if cursor < len(data):
        ledger.claim(cursor, len(data) - cursor, "omitted-proven", IMAGE_RESIDUE)
    model.ledger.append(ledger.finish())

    model.namespaces = vocabulary.namespaces_json()
    model.squad_slots = vocabulary.squad_slots_json()
    model.vocabulary = vocab.to_json()
    model.classes = [
        {
            "className": name,
            "unit": owner.key,
            "scheduleSpace": f"{owner.schedule_space:#010x}" if owner.schedule_space else None,
        }
        for owner in census.owners
        for name in sorted(set(owner.class_names or [owner.class_name]))
    ]
    model.order = {
        "rule": "base, then CAI_BaseNPCTroika, then the species on first touch",
        "unrecovered": (
            "the run-time order the first-touch species bodies fire in; nothing observable "
            "depends on it, because no space searches a sibling"
        ),
    }
    model.dead_doors = list(census.dead_doors)
    model.census = summary(census)
    model.anomalies = [row for row in census.anomalies if "unit" not in row]
    model.omissions.append(
        {
            "role": IMAGE_RESIDUE,
            "reason": "the seam decodes no content outside the schedule texts",
        }
    )
    return model
