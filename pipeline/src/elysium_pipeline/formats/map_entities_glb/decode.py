"""The ENTITIES lump read as an unfiltered, addressable table.

Every block the lump authors becomes one row, every pair of every block becomes one keyvalue with
the byte span it was read from, and nothing is filtered: a class handler's field usage is a
consumer fact checked against this table, not a filter applied to it. What the decode adds on top
of the pairs is only what the engine itself derives from them -- the number a keyvalue resolves to
under C `atof`, the six fields an output's value splits into, the unit another keyvalue names --
and every one of those carries the raw string it came from beside it.
"""

from __future__ import annotations

from bisect import bisect_right
from collections import Counter
import dataclasses
import hashlib
from typing import Any, Callable

from elysium_pipeline.formats.map_entities_glb import lexer
from elysium_pipeline.formats.map_entities_glb import model as entity_model
from elysium_pipeline.formats.map_entities_glb import references as entity_references
from elysium_pipeline.formats.map_entities_glb.model import (
    Claim,
    Entity,
    KeyValue,
    MapEntitiesModel,
    Output,
    Reference,
)
from elysium_pipeline.formats.unit_contract import dependency

WORLDSPAWN = "worldspawn"


class MapEntitiesDecodeError(ValueError):
    """The lump does not tokenize into the entity table this seam publishes."""


def _is_output(classname: str | None, source_key: str) -> bool:
    """Whether the class's datamap types this key as an output.

    Two rules answer it. A key spelled the way an output is spelled is one unless documented
    evidence says the datamap does not declare it, and a key the datamap declares under another
    spelling is one because that class's table says so (`OUTPUT_KEYS_BY_CLASS`). A key written
    the way an output is written that the datamap does not declare is kept as a plain keyvalue
    with `outputLike: true`: retail resolves the external name through the datamap first and
    silently drops what it does not find.
    """

    folded = source_key.lower()
    if folded.endswith(entity_model.DISABLED_KEY_SUFFIX):
        return False
    folded_class = (classname or "").strip().lower()
    if (folded_class, folded) in entity_model.NOT_OUTPUT_KEYS:
        return False
    if entity_model.OUTPUT_KEY.match(source_key) is not None:
        return True
    return folded in entity_model.OUTPUT_KEYS_BY_CLASS.get(folded_class, frozenset())


def _vector(raw: str) -> tuple[list[float], list[str], list[str]]:
    """The three `atof` components of a vector keyvalue, its tokens and what `atof` dropped."""

    tokens = str(raw).split()
    values: list[float] = []
    dropped: list[str] = []
    for token in tokens[:3]:
        value, _ = entity_model.atof(token)
        values.append(value)
        remainder = entity_model.dropped_suffix(token)
        if remainder:
            dropped.append(remainder)
    while len(values) < 3:
        values.append(0.0)
    return values, tokens, dropped


def _output_row(index: int, pair: KeyValue) -> Output:
    """One output keyvalue split the way `FUN_100ccf90` splits it."""

    raw = str(pair.value)
    fields = raw.split(",")

    def field(name: str) -> str:
        position = entity_model.OUTPUT_FIELDS.index(name)
        return fields[position] if position < len(fields) else ""

    residue = entity_model.OUTPUT_FIELDS.index("extra")
    authored = entity_model.atoi(field("times"))
    times = entity_model.UNLIMITED_TIMES if authored == 0 else authored
    return Output(
        index=index,
        key_value=pair.index,
        key=pair.source_key,
        target=field("target"),
        input=field("input"),
        parameter=field("parameter"),
        delay=entity_model.number_row(field("delay")),
        times={
            "raw": field("times"),
            "value": times,
            "unlimited": times == entity_model.UNLIMITED_TIMES,
        },
        python=field("python"),
        extra=",".join(fields[residue:]) if len(fields) > residue else None,
        field_count=len(fields),
        raw=raw,
    )


class _Decoder:
    """One pass over one lump: claims, rows, references and evidence for every departure."""

    def __init__(self, closure, member_exists: Callable[[str], bool]) -> None:
        self.closure = closure
        self.member_exists = member_exists
        self.data = closure.entities.data
        self.claims: list[Claim] = []
        self.anomalies: list[dict[str, Any]] = []
        self.omissions: list[dict[str, Any]] = []
        self.comments: list[dict[str, Any]] = []
        self.comment_spans: list[tuple[int, int]] = []      # ascending, from `text_regions`
        self.unresolved: list[dict[str, Any]] = []
        self.script_expressions: list[dict[str, Any]] = []
        self.dependencies: dict[tuple[str, str], dict[str, Any]] = {}
        self.brush_models = closure.brush_models()

    # -- claims ----------------------------------------------------------------------------

    def claim(self, offset: int, length: int, state: str, owner: str) -> None:
        if length > 0:
            self.claims.append(Claim(int(offset), int(length), state, owner))

    def claim_around_comments(self, offset: int, end: int, owner: str) -> None:
        """Claim `offset..end` for one record, minus any comment the span encloses.

        The grammar admits a comment between a key and its value, and a comment is its own
        record with its own claim. So a pair that straddles one pays for the bytes on either
        side of it as two ranges under the one owner, and `fill_whitespace` covers the blanks
        the comment is separated from its neighbours by.
        """

        position = bisect_right(self.comment_spans, (offset, end))
        cursor = offset
        for start, stop in self.comment_spans[max(position - 1, 0):]:
            if start >= end:
                break
            if stop <= cursor:
                continue
            if start > cursor:
                self.claim(cursor, start - cursor, "mapped-text", owner)
            cursor = stop
        if cursor < end:
            self.claim(cursor, end - cursor, "mapped-text", owner)

    def fill_whitespace(self, limit: int) -> None:
        """Claim every byte below `limit` no record claimed: the insignificant whitespace.

        The tokenizer tiles the text, so a byte no token-owning record claimed is a byte of a
        whitespace token. Filling the gaps here rather than claiming whitespace tokens directly
        is what lets a keyvalue own the blank between its key and its value without two owners
        meeting over one byte.
        """

        cursor = 0
        for row in sorted(self.claims, key=lambda claim: claim.offset):
            if row.offset >= limit:
                break
            if row.offset > cursor:
                self._whitespace(cursor, row.offset)
            cursor = max(cursor, row.offset + row.length)
        if cursor < limit:
            self._whitespace(cursor, limit)

    def _whitespace(self, start: int, end: int) -> None:
        chunk = self.data[start:end]
        if any(byte > 0x20 for byte in chunk):
            raise MapEntitiesDecodeError(
                f"{self.closure.entities.path}: byte {start} belongs to no record"
            )
        self.claim(start, end - start, "mapped-text", "whitespace")

    # -- dependencies ----------------------------------------------------------------------

    def declare(self, spec: entity_references.ReferenceSpec, **optional: Any) -> bool:
        """Record one dependency row and answer whether the install resolves it.

        One referenced unit is declared once, under the first spelling the lump authors for it:
        the row's `sourcePath` keeps that spelling, while the install -- whose index is folded --
        is asked for `lookup_path`.
        """

        key = (spec.role, spec.asset)
        if key not in self.dependencies:
            self.dependencies[key] = dependency(
                spec.role,
                spec.asset,
                spec.source_path,
                bool(self.member_exists(spec.lookup_path)),
                **optional,
            )
        return bool(self.dependencies[key]["resolved"])

    # -- the lump --------------------------------------------------------------------------

    def terminator(self) -> str:
        """Claim the trailing NUL and anything past it; answer the body the blocks are in.

        On every one of the 108 maps the lump holds exactly one NUL, at its last byte; a lump
        that departs is reported rather than repaired, and the engine's own stopping rule -- its
        tokenizer ends at the first NUL -- decides what the body is.
        """

        data = self.data
        if not data:
            # The contract's rule for a selecting member with no bytes: publish, and warn.
            self.omissions.append(
                {
                    "role": "empty-member",
                    "reason": "the map's ENTITIES lump is empty",
                    "sourceOffset": 0,
                    "byteLength": 0,
                }
            )
            return ""
        count = data.count(0)
        first = data.find(0)
        if count != 1 or first != len(data) - 1:
            self.anomalies.append(
                {
                    "role": "terminator-count",
                    "count": count,
                    "expected": 1,
                    "sourceOffset": first if first >= 0 else None,
                    "byteLength": len(data),
                }
            )
        if first < 0:
            return lexer.decode_text(data)
        self.claim(first, 1, "reserved-zero", "trailing-null")
        tail = data[first + 1:]
        if tail:
            self.omissions.append(
                {
                    "role": "bytes-after-terminator",
                    "reason": "the lump's NUL terminator ends the text the engine reads",
                    "sourceOffset": first + 1,
                    "byteLength": len(tail),
                    "sha256": hashlib.sha256(tail).hexdigest(),
                }
            )
            self.claim(
                first + 1, len(tail), "omitted-proven", "omissions[bytes-after-terminator]"
            )
        return lexer.decode_text(data[:first])

    def census(self, body: str) -> None:
        """The per-map facts the seam verifies: no CR, and no byte above 127."""

        raw = body.encode("latin-1")
        carriage = raw.count(b"\r")
        if carriage:
            self.anomalies.append(
                {"role": "line-end-cr", "count": carriage, "sourceOffset": raw.find(b"\r")}
            )
        high = [index for index, byte in enumerate(raw) if byte > 127]
        if high:
            self.anomalies.append(
                {
                    "role": "non-ascii-byte",
                    "count": len(high),
                    "sourceOffset": high[0],
                    "byte": raw[high[0]],
                }
            )

    def text_regions(self, document: lexer.Document) -> None:
        """Claim the comments the format is not supposed to have, and any token outside a block."""

        for index, token in enumerate(document.comments):
            self.comments.append(
                {
                    "index": index,
                    "text": token.text,
                    "sourceOffset": token.offset,
                    "byteLength": token.length,
                }
            )
            self.anomalies.append(
                {"role": "comment-line", "sourceOffset": token.offset, "text": token.text}
            )
            self.claim(token.offset, token.length, "mapped-text", f"comments[{index}]")
            self.comment_spans.append((token.offset, token.end))
        for token in document.strays:
            self.anomalies.append(
                {"role": "stray-token", "sourceOffset": token.offset, "text": token.text}
            )
            self.claim(token.offset, token.length, "mapped-text", "anomalies.stray-token")

    # -- one block -------------------------------------------------------------------------

    def entity(self, block: lexer.Block) -> Entity:
        index = block.index
        self.claim(block.open_token.offset, 1, "mapped-text", f"entities[{index}].braces.open")
        if block.close_token is not None:
            self.claim(
                block.close_token.offset, 1, "mapped-text", f"entities[{index}].braces.close"
            )
        for row in block.anomalies:
            self.anomalies.append(dict(row, entity=index))
            if row["role"] == "unterminated-block":
                self.unresolved.append(
                    {
                        "role": "unterminated-block",
                        "entity": index,
                        "sourceOffset": block.open_token.offset,
                        "reason": "the tokenizer reached the end of the lump inside this block",
                    }
                )

        pairs: list[KeyValue] = []
        classname: str | None = None
        for position, pair in enumerate(block.pairs):
            key_token, value_token = pair.key, pair.value
            end = (value_token or key_token).end
            self.claim_around_comments(
                key_token.offset, end, f"entities[{index}].keyValues[{position}]"
            )
            for token, role in ((key_token, "key"), (value_token, "value")):
                if token is None:
                    continue
                if token.anomaly:
                    self.anomalies.append(
                        {
                            "role": token.anomaly,
                            "entity": index,
                            "sourceOffset": token.offset,
                            "position": role,
                        }
                    )
                if not token.quoted:
                    self.anomalies.append(
                        {
                            "role": "unquoted-token",
                            "entity": index,
                            "sourceOffset": token.offset,
                            "text": token.text,
                            "position": role,
                        }
                    )
            folded = key_token.text.strip().lower()
            if folded == "classname" and classname is None and value_token is not None:
                # The engine finds the classname by first match, before it constructs anything.
                classname = value_token.text
            pairs.append(
                KeyValue(
                    index=position,
                    key=folded,
                    source_key=key_token.text,
                    value=value_token.text if value_token is not None else None,
                    quoted_key=key_token.quoted,
                    quoted_value=bool(value_token is not None and value_token.quoted),
                    offset=key_token.offset,
                    length=end - key_token.offset,
                )
            )
        if classname is None:
            self.anomalies.append(
                {"role": "missing-classname", "entity": index, "sourceOffset": block.offset}
            )
        braces = {
            "openSourceOffset": block.open_token.offset,
            "closeSourceOffset": (
                block.close_token.offset if block.close_token is not None else None
            ),
        }
        return self.fields(index, classname, braces, pairs)

    def fields(
        self,
        index: int,
        classname: str | None,
        braces: dict[str, Any],
        pairs: list[KeyValue],
    ) -> Entity:
        """Everything the engine derives from one block's pairs, in authored order."""

        outputs: list[Output] = []
        references: list[Reference] = []
        origin: dict[str, Any] | None = None
        angles: dict[str, Any] | None = None
        model_field: dict[str, Any] | None = None
        occurrences: dict[str, list[int]] = {}

        for pair in pairs:
            key, value = pair.key, pair.value
            output_shaped = entity_model.OUTPUT_KEY.match(pair.source_key) is not None
            declared = _is_output(classname, pair.source_key)
            if declared and value is not None:
                outputs.append(self.output(index, len(outputs), pair))
            elif output_shaped:
                # Written like an output, not declared as one: kept as a plain keyvalue.
                pairs[pair.index] = dataclasses.replace(pair, output_like=True)
            else:
                # A list key is authored many times by design; only a scalar key is not.
                occurrences.setdefault(key, []).append(pair.index)

            if value is None:
                continue

            if key in ("origin", "angles"):
                row = self.vector(index, pair)
                if key == "origin":
                    origin = row
                else:
                    angles = row
            elif key == "model":
                model_field, spec = entity_references.model_field(
                    value, int(self.brush_models["count"])
                )
                if model_field["kind"] == "brush":
                    references.append(self.brush_reference(index, pair, model_field))
                    continue
                if spec is not None:
                    references.append(
                        Reference(pair.index, spec.role, spec.asset, self.declare(spec))
                    )
                    continue
                # A `model` naming a file the vocabulary does not type -- a `.spr` sprite, or an
                # extension no map authors -- takes the same growth-by-evidence path every other
                # key takes rather than being dropped, so it falls through to the guard below.
            elif (
                (classname or "").strip().lower() == entity_model.PYTHON_CHECK_CLASS
                and key == entity_model.PYTHON_CHECK_KEY
                and value.strip()
            ):
                self.script_expressions.append(
                    {
                        "index": len(self.script_expressions),
                        "entity": index,
                        "output": None,
                        "keyValue": pair.index,
                        "role": "python-script",
                        "expression": value,
                    }
                )

            spec = entity_references.reference_for(key, value)
            if spec is not None:
                references.append(
                    Reference(pair.index, spec.role, spec.asset, self.declare(spec))
                )
            elif entity_references.extension_of(value):
                self.anomalies.append(
                    {
                        "role": "untyped-file-reference",
                        "entity": index,
                        "key": pair.source_key,
                        "value": value,
                        "extension": entity_references.extension_of(value),
                        "sourceOffset": pair.offset,
                    }
                )

        for key, rows in occurrences.items():
            if len(rows) > 1:
                self.anomalies.append(
                    {
                        "role": "duplicate-scalar-key",
                        "entity": index,
                        "classname": classname,
                        "key": key,
                        "occurrences": rows,
                    }
                )

        return Entity(
            index=index,
            classname=classname,
            braces=braces,
            key_values=pairs,
            outputs=outputs,
            references=references,
            origin=origin,
            angles=angles,
            model=model_field,
        )

    def output(self, index: int, position: int, pair: KeyValue) -> Output:
        row = _output_row(position, pair)
        # `delay` is the one output field the engine reads with `atof`, so it answers to the same
        # truncation rule an `origin` component answers to.
        dropped = entity_model.dropped_suffix(str(row.delay["raw"]))
        if dropped:
            self.anomalies.append(
                {
                    "role": "atof-truncated-number",
                    "entity": index,
                    "key": pair.source_key,
                    "field": "delay",
                    "raw": row.delay["raw"],
                    "dropped": dropped,
                    "sourceOffset": pair.offset,
                }
            )
        if row.field_count != entity_model.OUTPUT_FIELD_COUNT:
            self.anomalies.append(
                {
                    "role": "output-field-count",
                    "entity": index,
                    "key": pair.source_key,
                    "fieldCount": row.field_count,
                    "expected": entity_model.OUTPUT_FIELD_COUNT,
                    "sourceOffset": pair.offset,
                }
            )
        if row.python.strip():
            self.script_expressions.append(
                {
                    "index": len(self.script_expressions),
                    "entity": index,
                    "output": row.index,
                    "keyValue": pair.index,
                    "role": "output-python",
                    "expression": row.python,
                }
            )
        return row

    def vector(self, index: int, pair: KeyValue) -> dict[str, Any]:
        values, tokens, dropped = _vector(str(pair.value))
        for suffix in dropped:
            self.anomalies.append(
                {
                    "role": "atof-truncated-number",
                    "entity": index,
                    "key": pair.source_key,
                    "field": pair.key,
                    "raw": pair.value,
                    "dropped": suffix,
                    "sourceOffset": pair.offset,
                }
            )
        if len(tokens) != 3:
            self.anomalies.append(
                {
                    "role": "vector-field-count",
                    "entity": index,
                    "key": pair.source_key,
                    "raw": pair.value,
                    "count": len(tokens),
                    "expected": 3,
                    "sourceOffset": pair.offset,
                }
            )
        if pair.key == "origin":
            return {"raw": pair.value, "source": values, "gltf": entity_model.position(*values)}
        return {
            "raw": pair.value,
            "source": values,
            "gltf": entity_model.quaternion_from_angles(*values),
        }

    def brush_reference(self, index: int, pair: KeyValue, field: dict[str, Any]) -> Reference:
        """A `*N` names this map's own root; the index is proven against the MODELS lump."""

        spec = entity_references.ReferenceSpec(
            "brush-model",
            entity_model.map_asset_id(self.closure.key),
            self.closure.source_path,
            index=int(field["index"]),
        )
        # The pin states the bytes the row's own `sourcePath` names -- the whole BSP -- the way
        # the sibling map sub-units pin theirs. What this unit reads out of that file is lump
        # 14's index space, published with its own length and digest under `map.brushModels`.
        resolved = self.declare(
            spec,
            byteLength=self.closure.byte_length,
            sha256=self.closure.sha256,
        )
        within = bool(field["withinModelCount"])
        if not within:
            self.anomalies.append(
                {
                    "role": "brush-model-index",
                    "entity": index,
                    "key": pair.source_key,
                    "value": pair.value,
                    "index": int(field["index"]),
                    "modelCount": int(self.brush_models["count"]),
                    "sourceOffset": pair.offset,
                }
            )
        return Reference(
            pair.index, spec.role, spec.asset, resolved and within, index=int(field["index"])
        )


def decode_map_entities(
    closure,
    *,
    member_exists: Callable[[str], bool] | None = None,
) -> MapEntitiesModel:
    """Decode one map's ENTITIES lump into the table the unit publishes.

    `member_exists` answers whether the UP-first index holds a member for a referenced path; a
    join that fails is `resolved: false` and warns, because it is another seam's data rather than
    this unit's own structure.
    """

    decoder = _Decoder(closure, member_exists or (lambda path: False))
    body = decoder.terminator()
    decoder.census(body)
    try:
        document = lexer.parse(lexer.tokenize(body))
    except lexer.MapEntitiesLexError as error:
        raise MapEntitiesDecodeError(f"{closure.entities.path}: {error}") from error
    decoder.text_regions(document)

    entities = [decoder.entity(block) for block in document.blocks]
    decoder.fill_whitespace(len(body))

    worldspawn_index: int | None = None
    for entity in entities:
        if (entity.classname or "").strip().lower() == WORLDSPAWN:
            worldspawn_index = entity.index
            break
    if worldspawn_index is not None:
        world = entities[worldspawn_index]
        sky = next((pair for pair in reversed(world.key_values) if pair.key == "skyname"), None)
        if sky is not None and sky.value:
            for spec in entity_references.skybox_references(sky.value):
                world.references.append(
                    Reference(sky.index, spec.role, spec.asset, decoder.declare(spec))
                )

    census = Counter(entity.classname for entity in entities if entity.classname is not None)
    span = closure.lump_span
    return MapEntitiesModel(
        key=closure.key,
        asset_id=closure.asset_id,
        source_path=closure.source_path,
        member=closure.entities,
        map={
            "stem": closure.key,
            "mapRevision": closure.map_revision,
            "offset": span.offset,
            "length": span.length,
            # Every `offset` this unit publishes is relative to the span's first byte, which is
            # the byte the ledger's first range starts at; `offset` above places that byte.
            "offsetBase": "lump-span",
            "entityCount": len(entities),
            "file": {
                "path": closure.source_path,
                "byteLength": closure.byte_length,
                "sha256": closure.sha256,
            },
            "brushModels": decoder.brush_models,
        },
        entities=entities,
        worldspawn_index=worldspawn_index,
        class_census=[
            {"classname": name, "count": count} for name, count in sorted(census.items())
        ],
        script_expressions=decoder.script_expressions,
        dependencies=[decoder.dependencies[key] for key in sorted(decoder.dependencies)],
        comments=decoder.comments,
        anomalies=decoder.anomalies,
        omissions=decoder.omissions,
        claims=decoder.claims,
        unresolved=decoder.unresolved,
        unsupported=[],
    )


__all__ = ["MapEntitiesDecodeError", "decode_map_entities"]
