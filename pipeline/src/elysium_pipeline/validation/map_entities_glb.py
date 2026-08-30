"""Independent validator for Map-entities GLB products.

Structural checks (container shape, extension-root key order, the byte ledger, the absence of an
opaque source mirror) run through `elysium_pipeline.formats.unit_contract`, the shared owner of
those rules. Everything semantic to the ENTITIES lump -- which blocks it holds, which pairs each
block authors, where each pair's bytes are, what an output's value splits into and which brush
model a `*N` names -- is re-derived here from a fresh tokenize-and-walk of the source bytes,
written independently of `formats.map_entities_glb` and never by calling the exporter.
"""

from __future__ import annotations

from collections import Counter
import math
from pathlib import Path
import re
from typing import Any

from elysium_pipeline.formats.map_entities_glb.model import (
    ANOMALY_ROLES,
    DEPENDENCY_ROLES,
    DISABLED_KEY_SUFFIX,
    ENTITIES_LUMP,
    INCH_TO_METRE,
    KIND_TITLE,
    KNOWN_EXTENSIONS,
    MAP_ENTITIES_EXTENSION,
    MODELS_LUMP,
    MODEL_STRIDE,
    NOT_OUTPUT_KEYS,
    OMISSION_ROLES,
    OUTPUT_FIELD_COUNT,
    OUTPUT_KEY,
    OUTPUT_KEYS_BY_CLASS,
    SCHEMA_VERSION,
    asset_id,
)
from elysium_pipeline.formats.unit_contract import (
    UnitValidationError,
    completeness,
    generator,
    read_glb,
    reject_opaque_source,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)
from elysium_pipeline.formats.unit_contract import warnings_for as _contract_warnings_for

__all__ = [
    "MapEntitiesGlbValidationError",
    "read_glb",
    "validate",
    "validate_document",
    "warnings_for",
]


class MapEntitiesGlbValidationError(ValueError):
    """A published map-entities unit contradicts the seam's contract."""


_BREAK = "{}()'"
#: `maps/<map>.bsp#lump<n>` -- the identity a span member cut from one BSP is published under.
_MEMBER_PATH = re.compile(r"^maps/(?P<stem>[^/]+)\.bsp#lump(?P<lump>\d+)$")
#: `*N` names a brush model of this map's own root.
_BRUSH_MODEL = re.compile(r"^\*(\d+)$")
_ATOF = re.compile(r"[ \t\n\r\v\f]*[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?")
_ATOI = re.compile(r"[ \t\n\r\v\f]*[+-]?[0-9]+")


# --- an independent tokenize-and-walk of the raw span, for export-time comparison --------------


def _atof(text: str) -> float:
    """C `atof`, with the seam's rule for a magnitude the format cannot carry.

    A prefix that overflows a double reads as `HUGE_VAL` in C; the unit publishes `0.0` there
    rather than a JSON number no reader can express, so this independent reader applies the same
    rule -- otherwise a value no map authors would fail the export instead of publishing.
    """

    match = _ATOF.match(str(text))
    if match is None or not match.group(0).strip():
        return 0.0
    value = float(match.group(0))
    return value if math.isfinite(value) else 0.0


def _is_output_key(classname: Any, key: str) -> bool:
    """The datamap rule the unit's outputs are selected by, restated from the shared table."""

    folded = str(key).lower()
    if folded.endswith(DISABLED_KEY_SUFFIX):
        return False
    folded_class = str(classname or "").strip().lower()
    if (folded_class, folded) in NOT_OUTPUT_KEYS:
        return False
    if OUTPUT_KEY.match(str(key)) is not None:
        return True
    return folded in OUTPUT_KEYS_BY_CLASS.get(folded_class, frozenset())


def _position(values: list[float]) -> list[float]:
    x, y, z = values
    return [x * INCH_TO_METRE, z * INCH_TO_METRE, -y * INCH_TO_METRE]


def _quaternion(values: list[float]) -> list[float]:
    """A Source `QAngle` as a glTF rotation, written from `AngleQuaternion` and the contract."""

    pitch, yaw, roll = (math.radians(angle) * 0.5 for angle in values)
    sy, cy = math.sin(yaw), math.cos(yaw)
    sp, cp = math.sin(pitch), math.cos(pitch)
    sr, cr = math.sin(roll), math.cos(roll)
    quaternion = [
        sr * cp * cy - cr * sp * sy,
        cr * cp * sy - sr * sp * cy,
        -(cr * sp * cy + sr * cp * sy),
        cr * cp * cy + sr * sp * sy,
    ]
    length = math.sqrt(sum(value * value for value in quaternion))
    return [value / length for value in quaternion]


def _close(published: Any, expected: list[float]) -> bool:
    if not isinstance(published, list) or len(published) != len(expected):
        return False
    return all(
        isinstance(value, (int, float)) and math.isclose(value, want, rel_tol=1e-9, abs_tol=1e-12)
        for value, want in zip(published, expected)
    )


def _tokens(text: str) -> list[tuple[str, str, int, int]]:
    """`(kind, text, offset, length)` for every token, tiling the input.

    Written from the grammar `MapEntity_ParseToken` implements, not from the seam's own lexer.
    """

    out: list[tuple[str, str, int, int]] = []
    index, total = 0, len(text)
    while index < total:
        char = text[index]
        if char <= " ":
            start = index
            while index < total and text[index] <= " ":
                index += 1
            out.append(("whitespace", text[start:index], start, index - start))
        elif char == "/" and text[index + 1:index + 2] == "/":
            start = index
            stop = text.find("\n", index)
            index = total if stop < 0 else stop
            out.append(("comment", text[start:index], start, index - start))
        elif char == '"':
            start = index
            index += 1
            buffer: list[str] = []
            while index < total:
                current = text[index]
                if current == "\\" and index + 1 < total:
                    buffer.append("\n" if text[index + 1] == "n" else text[index + 1])
                    index += 2
                    continue
                if current == '"':
                    index += 1
                    break
                buffer.append(current)
                index += 1
            out.append(("string", "".join(buffer), start, index - start))
        elif char in _BREAK:
            out.append(("break", char, index, 1))
            index += 1
        else:
            start = index
            while index < total and text[index] > " " and text[index] not in _BREAK:
                index += 1
            out.append(("bare", text[start:index], start, index - start))
    return out


def _body(data: bytes) -> str:
    """The text the engine reads: everything below the lump's first NUL."""

    first = data.find(0)
    return data.decode("latin-1") if first < 0 else data[:first].decode("latin-1")


def _reference_blocks(tokens: list[tuple[str, str, int, int]]) -> list[dict[str, Any]]:
    """Every `{ … }` block of the lump span, with the pairs and spans it authors."""

    significant = [token for token in tokens if token[0] in ("string", "bare", "break")]
    blocks: list[dict[str, Any]] = []
    cursor = 0
    while cursor < len(significant):
        kind, text, offset, length = significant[cursor]
        if not (kind == "break" and text == "{"):
            cursor += 1
            continue
        block: dict[str, Any] = {"open": offset, "close": None, "pairs": []}
        cursor += 1
        while cursor < len(significant):
            key = significant[cursor]
            if key[0] == "break" and key[1] == "}":
                block["close"] = key[2]
                cursor += 1
                break
            cursor += 1
            value = significant[cursor] if cursor < len(significant) else None
            if value is not None and value[0] == "break" and value[1] == "}":
                block["close"] = value[2]
                block["pairs"].append(
                    {
                        "key": key[1],
                        "value": None,
                        "offset": key[2],
                        "length": key[3],
                        "quotedKey": key[0] == "string",
                        "quotedValue": False,
                    }
                )
                cursor += 1
                break
            if value is None:
                block["pairs"].append(
                    {
                        "key": key[1],
                        "value": None,
                        "offset": key[2],
                        "length": key[3],
                        "quotedKey": key[0] == "string",
                        "quotedValue": False,
                    }
                )
                break
            block["pairs"].append(
                {
                    "key": key[1],
                    "value": value[1],
                    "offset": key[2],
                    "length": value[2] + value[3] - key[2],
                    "quotedKey": key[0] == "string",
                    "quotedValue": value[0] == "string",
                }
            )
            cursor += 1
        blocks.append(block)
    return blocks


def _compare_with_source(root: dict[str, Any], data: bytes) -> None:
    tokens = _tokens(_body(data))
    reference = _reference_blocks(tokens)
    entities = root.get("entities")
    if not isinstance(entities, list) or len(entities) != len(reference):
        raise MapEntitiesGlbValidationError(
            f"the unit publishes {len(entities or [])} entities against "
            f"{len(reference)} blocks in the lump"
        )
    brush_count = int(((root.get("map") or {}).get("brushModels") or {}).get("count", 0))
    untyped = {
        row.get("sourceOffset")
        for row in root.get("anomalies") or []
        if row.get("role") == "untyped-file-reference"
    }
    for index, (published, expected) in enumerate(zip(entities, reference)):
        braces = published.get("braces") or {}
        if (
            braces.get("openSourceOffset") != expected["open"]
            or braces.get("closeSourceOffset") != expected["close"]
        ):
            raise MapEntitiesGlbValidationError(f"entity {index} braces disagree with the lump")
        pairs = published.get("keyValues") or []
        if len(pairs) != len(expected["pairs"]):
            raise MapEntitiesGlbValidationError(
                f"entity {index} publishes {len(pairs)} pairs against "
                f"{len(expected['pairs'])} in the lump"
            )
        for position, (row, want) in enumerate(zip(pairs, expected["pairs"])):
            where = f"entity {index} keyValue {position}"
            if row.get("index") != position:
                raise MapEntitiesGlbValidationError(f"{where} is out of order")
            if row.get("sourceKey") != want["key"] or row.get("value") != want["value"]:
                raise MapEntitiesGlbValidationError(f"{where} text disagrees with the lump")
            if row.get("key") != str(want["key"]).strip().lower():
                raise MapEntitiesGlbValidationError(f"{where} folded key disagrees")
            if (
                row.get("sourceOffset") != want["offset"]
                or row.get("byteLength") != want["length"]
            ):
                raise MapEntitiesGlbValidationError(f"{where} span disagrees with the lump")
            if (
                row.get("quotedKey") != want["quotedKey"]
                or row.get("quotedValue") != want["quotedValue"]
            ):
                raise MapEntitiesGlbValidationError(f"{where} quoting disagrees with the lump")
        classname = next(
            (
                pair["value"]
                for pair in expected["pairs"]
                if str(pair["key"]).strip().lower() == "classname" and pair["value"] is not None
            ),
            None,
        )
        if published.get("classname") != classname:
            raise MapEntitiesGlbValidationError(f"entity {index} classname disagrees")
        _compare_output_membership(index, published, classname, expected["pairs"])
        _compare_outputs(index, published, pairs)
        _compare_vectors(index, published)
        _compare_brush_models(index, published, brush_count)
        _compare_reference_accounting(index, published, expected["pairs"], untyped)
    _compare_anomaly_evidence(root, reference, tokens)


def _compare_output_membership(
    index: int, published: dict[str, Any], classname: Any, pairs: list[dict[str, Any]]
) -> None:
    """Every pair the datamap rule types as an output produced a row, and nothing else did.

    The fresh walk answers which pairs those are, so an output that vanished from `outputs[]` --
    or one the writer invented -- is caught rather than merely being back-linked consistently.
    """

    expected = [
        position
        for position, pair in enumerate(pairs)
        if pair["value"] is not None and _is_output_key(classname, pair["key"])
    ]
    produced = [row.get("keyValue") for row in published.get("outputs") or []]
    if produced != expected:
        raise MapEntitiesGlbValidationError(
            f"entity {index} publishes outputs for {produced} against {expected} in the lump"
        )
    rows = published.get("keyValues") or []
    for position, pair in enumerate(pairs):
        if position in expected or position >= len(rows):
            continue
        shaped = OUTPUT_KEY.match(str(pair["key"])) is not None
        if bool(rows[position].get("outputLike")) != shaped:
            raise MapEntitiesGlbValidationError(
                f"entity {index} keyValue {position} disagrees about being output-like"
            )


def _compare_outputs(index: int, published: dict[str, Any], pairs: list[Any]) -> None:
    for position, output in enumerate(published.get("outputs") or []):
        where = f"entity {index} output {position}"
        pair_index = output.get("keyValue")
        if not isinstance(pair_index, int) or not 0 <= pair_index < len(pairs):
            raise MapEntitiesGlbValidationError(f"{where} names no keyValue of this entity")
        raw = pairs[pair_index].get("value")
        if output.get("raw") != raw:
            raise MapEntitiesGlbValidationError(f"{where} raw disagrees with its keyValue")
        fields = str(raw).split(",")

        def field(at: int) -> str:
            return fields[at] if at < len(fields) else ""

        if output.get("fieldCount") != len(fields):
            raise MapEntitiesGlbValidationError(f"{where} fieldCount disagrees with a fresh split")
        for name, at in (("target", 0), ("input", 1), ("parameter", 2), ("python", 5)):
            if output.get(name) != field(at):
                raise MapEntitiesGlbValidationError(f"{where} {name} disagrees with a fresh split")
        delay = output.get("delay") or {}
        if delay.get("raw") != field(3) or delay.get("value") != _atof(field(3)):
            raise MapEntitiesGlbValidationError(f"{where} delay disagrees with a fresh split")
        times = output.get("times") or {}
        authored = _ATOI.match(field(4))
        value = int(authored.group(0)) if authored else 0
        value = -1 if value == 0 else value
        if times.get("raw") != field(4) or times.get("value") != value:
            raise MapEntitiesGlbValidationError(f"{where} times disagrees with a fresh split")
        if times.get("unlimited") != (value == -1):
            raise MapEntitiesGlbValidationError(f"{where} unlimited disagrees with times")
        extra = ",".join(fields[6:]) if len(fields) > 6 else None
        if output.get("extra", None) != extra:
            raise MapEntitiesGlbValidationError(f"{where} extra disagrees with a fresh split")


def _compare_vectors(index: int, published: dict[str, Any]) -> None:
    for name in ("origin", "angles"):
        row = published.get(name)
        if row is None:
            continue
        tokens = str(row.get("raw", "")).split()[:3]
        values = [_atof(token) for token in tokens]
        while len(values) < 3:
            values.append(0.0)
        if list(row.get("source") or []) != values:
            raise MapEntitiesGlbValidationError(
                f"entity {index} {name} disagrees with a fresh atof of its raw string"
            )
        expected = _position(values) if name == "origin" else _quaternion(values)
        if not _close(row.get("gltf"), expected):
            raise MapEntitiesGlbValidationError(
                f"entity {index} {name} gltf disagrees with the transform of its source values"
            )


def _compare_brush_models(index: int, published: dict[str, Any], brush_count: int) -> None:
    model = published.get("model")
    if not isinstance(model, dict) or model.get("kind") != "brush":
        return
    number = model.get("index")
    if not isinstance(number, int) or number < 0:
        raise MapEntitiesGlbValidationError(f"entity {index} names a brush model {number!r}")
    if model.get("withinModelCount") != (number < brush_count):
        raise MapEntitiesGlbValidationError(
            f"entity {index} brush model *{number} disagrees with the MODELS lump's "
            f"{brush_count} records"
        )


def _names_a_file(value) -> str:
    """The known file extension a value carries, folded, or the empty string for none."""

    folded = str(value or "").strip().strip('"').lower().replace("\\", "/")
    for suffix in KNOWN_EXTENSIONS:
        if folded.endswith(suffix):
            return suffix
    return ""


def _compare_reference_accounting(
    index: int, published: dict[str, Any], pairs: list[dict[str, Any]], evidence: set
) -> None:
    """Every pair of the fresh walk that names another file is accounted for.

    A value carrying a known extension, or a `*N` brush model, is either joined -- one
    `references[]` row on that keyvalue, which `_check_back_links` then ties to a `dependencies`
    row -- or recorded as an `untyped-file-reference` anomaly on that keyvalue's own offset. A
    reference dropped together with its dependency row leaves the pair stated by neither, and
    fails here.
    """

    joined = {
        row.get("keyValue")
        for row in published.get("references") or []
        if isinstance(row.get("keyValue"), int)
    }
    for position, pair in enumerate(pairs):
        value = pair["value"]
        if value is None or position in joined:
            continue
        text = str(value).strip()
        if not _names_a_file(text) and _BRUSH_MODEL.match(text) is None:
            continue
        if pair["offset"] in evidence:
            continue
        raise MapEntitiesGlbValidationError(
            f"entity {index} keyValue {position} names {text!r} and the unit publishes "
            f"neither a reference nor an untyped-file-reference for it"
        )


def _dropped_suffix(text: str) -> str:
    """What C `atof` refused to read of a component, or the empty string when it read it all."""

    match = _ATOF.match(str(text))
    consumed = match.group(0) if match is not None and match.group(0).strip() else ""
    remainder = str(text)[len(consumed):]
    return remainder if remainder.strip() else ""


def _fresh_anomalies(
    blocks: list[dict[str, Any]], tokens: list[tuple[str, str, int, int]]
) -> Counter:
    """The `(role, sourceOffset)` evidence a fresh walk of the lump decides by itself.

    Only the roles this walk can decide are re-derived; a published row of another role is left
    to the vocabulary check. What the derived roles buy is that an anomaly cannot be dropped from
    the table to make a departure from the format disappear.
    """

    expected: Counter = Counter()
    for kind, _, offset, _ in tokens:
        if kind == "comment":
            expected[("comment-line", offset)] += 1
    for block in blocks:
        classname = next(
            (
                pair["value"]
                for pair in block["pairs"]
                if str(pair["key"]).strip().lower() == "classname" and pair["value"] is not None
            ),
            None,
        )
        for pair in block["pairs"]:
            value = pair["value"]
            if value is None:
                continue
            key = str(pair["key"]).strip().lower()
            if key in ("origin", "angles"):
                components = str(value).split()
                if len(components) != 3:
                    expected[("vector-field-count", pair["offset"])] += 1
                for token in components[:3]:
                    if _dropped_suffix(token):
                        expected[("atof-truncated-number", pair["offset"])] += 1
            if _is_output_key(classname, pair["key"]):
                fields = str(value).split(",")
                if len(fields) != OUTPUT_FIELD_COUNT:
                    expected[("output-field-count", pair["offset"])] += 1
                if _dropped_suffix(fields[3] if len(fields) > 3 else ""):
                    expected[("atof-truncated-number", pair["offset"])] += 1
    return expected


#: The anomaly roles a fresh walk decides, and which therefore may not be dropped from the table.
_DERIVED_ANOMALIES = frozenset(
    {"comment-line", "vector-field-count", "output-field-count", "atof-truncated-number"}
)


def _compare_anomaly_evidence(
    root: dict[str, Any],
    blocks: list[dict[str, Any]],
    tokens: list[tuple[str, str, int, int]],
) -> None:
    expected = _fresh_anomalies(blocks, tokens)
    published: Counter = Counter()
    for row in root.get("anomalies") or []:
        role = str(row.get("role"))
        if role in _DERIVED_ANOMALIES:
            published[(role, row.get("sourceOffset"))] += 1
    if published != expected:
        missing = sorted(str(row) for row in (expected - published).elements())[:3]
        invented = sorted(str(row) for row in (published - expected).elements())[:3]
        raise MapEntitiesGlbValidationError(
            f"the anomaly table disagrees with a fresh walk of the lump: missing {missing}, "
            f"unexpected {invented}"
        )
    fresh = [
        (offset, length) for kind, _, offset, length in tokens if kind == "comment"
    ]
    published_comments = [
        (row.get("sourceOffset"), row.get("byteLength")) for row in root.get("comments") or []
    ]
    if published_comments != fresh:
        raise MapEntitiesGlbValidationError("the comment table disagrees with a fresh walk")


# --- structural / cross-reference checks on the published document alone -----------------------


def _check_back_links(root: dict[str, Any]) -> dict[str, int]:
    entities = root.get("entities")
    if not isinstance(entities, list):
        raise MapEntitiesGlbValidationError("the unit publishes no entity table")
    dependencies = root.get("dependencies") or []
    declared = {(row.get("role"), row.get("asset")) for row in dependencies}
    referenced: set[tuple[Any, Any]] = set()
    outputs = 0
    census: Counter[str] = Counter()
    brush_count = int(((root.get("map") or {}).get("brushModels") or {}).get("count", 0))

    for index, entity in enumerate(entities):
        if not isinstance(entity, dict) or entity.get("index") != index:
            raise MapEntitiesGlbValidationError(f"entity {index} is out of order")
        pairs = entity.get("keyValues")
        if not isinstance(pairs, list):
            raise MapEntitiesGlbValidationError(f"entity {index} publishes no keyValues")
        if entity.get("classname") is not None:
            census[str(entity.get("classname"))] += 1
        for position, output in enumerate(entity.get("outputs") or []):
            outputs += 1
            pair_index = output.get("keyValue")
            if not isinstance(pair_index, int) or not 0 <= pair_index < len(pairs):
                raise MapEntitiesGlbValidationError(
                    f"entity {index} output {position} back-links to no keyValue"
                )
            if pairs[pair_index].get("sourceKey") != output.get("key"):
                raise MapEntitiesGlbValidationError(
                    f"entity {index} output {position} back-links to another key"
                )
            if pairs[pair_index].get("outputLike"):
                raise MapEntitiesGlbValidationError(
                    f"entity {index} keyValue {pair_index} is both an output and output-like"
                )
            if not _is_output_key(entity.get("classname"), str(output.get("key"))):
                raise MapEntitiesGlbValidationError(
                    f"entity {index} output {position} types a key no datamap declares"
                )
        for position, reference in enumerate(entity.get("references") or []):
            pair_index = reference.get("keyValue")
            if not isinstance(pair_index, int) or not 0 <= pair_index < len(pairs):
                raise MapEntitiesGlbValidationError(
                    f"entity {index} reference {position} back-links to no keyValue"
                )
            role, asset = reference.get("role"), reference.get("asset")
            if role not in DEPENDENCY_ROLES:
                raise MapEntitiesGlbValidationError(f"unknown reference role {role!r}")
            if (role, asset) not in declared:
                raise MapEntitiesGlbValidationError(
                    f"entity {index} reference {position} has no dependencies row"
                )
            if role == "brush-model" and not isinstance(reference.get("index"), int):
                raise MapEntitiesGlbValidationError(
                    f"entity {index} reference {position} names no brush model index"
                )
            if role == "brush-model" and reference.get("index") >= brush_count:
                if reference.get("resolved"):
                    raise MapEntitiesGlbValidationError(
                        f"entity {index} resolves a brush model outside the MODELS lump"
                    )
            referenced.add((role, asset))

    if declared != referenced:
        raise MapEntitiesGlbValidationError(
            "the dependency set disagrees with the references that produced it"
        )
    published_census = [
        {"classname": name, "count": count} for name, count in sorted(census.items())
    ]
    if (root.get("classCensus") or []) != published_census:
        raise MapEntitiesGlbValidationError("classCensus disagrees with the entity table")
    for position, expression in enumerate(root.get("scriptExpressions") or []):
        entity_index = expression.get("entity")
        if not isinstance(entity_index, int) or not 0 <= entity_index < len(entities):
            raise MapEntitiesGlbValidationError(
                f"scriptExpressions {position} names no entity of this unit"
            )
        entity = entities[entity_index]
        pair_index = expression.get("keyValue")
        if not isinstance(pair_index, int) or not 0 <= pair_index < len(entity["keyValues"]):
            raise MapEntitiesGlbValidationError(
                f"scriptExpressions {position} names no keyValue of entity {entity_index}"
            )
        output_index = expression.get("output")
        if output_index is not None:
            rows = entity.get("outputs") or []
            if not isinstance(output_index, int) or not 0 <= output_index < len(rows):
                raise MapEntitiesGlbValidationError(
                    f"scriptExpressions {position} names no output of entity {entity_index}"
                )
            if rows[output_index].get("python") != expression.get("expression"):
                raise MapEntitiesGlbValidationError(
                    f"scriptExpressions {position} disagrees with the output it names"
                )
    return {"entities": len(entities), "outputs": outputs}


def _check_worldspawn(root: dict[str, Any]) -> None:
    world = root.get("worldspawn")
    entities = root.get("entities") or []
    if world is None:
        return
    index = world.get("index")
    if not isinstance(index, int) or not 0 <= index < len(entities):
        raise MapEntitiesGlbValidationError("worldspawn names no entity of this unit")
    if world != entities[index]:
        raise MapEntitiesGlbValidationError("worldspawn disagrees with the entity it names")
    if str(world.get("classname") or "").strip().lower() != "worldspawn":
        raise MapEntitiesGlbValidationError("worldspawn names an entity of another class")


def _check_span_member(root: dict[str, Any], key: str) -> dict[str, Any]:
    """The one source member is this map's ENTITIES span, named the way a lump span is named."""

    members = list(((root.get("sourceResolution") or {}).get("members") or []))
    if len(members) != 1:
        raise MapEntitiesGlbValidationError(
            f"a map-entities unit resolves one source member, not {len(members)}"
        )
    member = members[0]
    match = _MEMBER_PATH.match(str(member.get("path", "")))
    if match is None:
        raise MapEntitiesGlbValidationError(
            f"source member {member.get('path')!r} is not a lump span of a BSP"
        )
    if match["stem"] != key:
        raise MapEntitiesGlbValidationError("the source member is cut from another map's BSP")
    if int(match["lump"]) != ENTITIES_LUMP:
        raise MapEntitiesGlbValidationError(
            f"the source member is lump {match['lump']}, not the ENTITIES lump"
        )
    span = member.get("span")
    if not isinstance(span, dict):
        raise MapEntitiesGlbValidationError("the ENTITIES member carries no span")
    if int(span.get("length", -1)) != int(member.get("byteLength", -2)):
        raise MapEntitiesGlbValidationError("the span's length disagrees with its member")
    return member


def _check_map_block(root: dict[str, Any], member: dict[str, Any], entities: int) -> None:
    """The `map` block states the span the member was cut from, and the counts it is read by."""

    block = root.get("map") or {}
    span = member["span"]
    if int(block.get("offset", -1)) != int(span["offset"]) or int(
        block.get("length", -1)
    ) != int(span["length"]):
        raise MapEntitiesGlbValidationError("the map block disagrees with the span it names")
    if block.get("offsetBase") != "lump-span":
        raise MapEntitiesGlbValidationError(
            "the map block does not declare the base its offsets are relative to"
        )
    if int(block.get("entityCount", -1)) != entities:
        raise MapEntitiesGlbValidationError("map.entityCount disagrees with the entity table")
    brush = block.get("brushModels") or {}
    if int(brush.get("lump", -1)) != MODELS_LUMP or int(brush.get("stride", -1)) != MODEL_STRIDE:
        raise MapEntitiesGlbValidationError("map.brushModels does not name the MODELS lump")
    length = int(brush.get("length", -1))
    if length < 0:
        raise MapEntitiesGlbValidationError("map.brushModels states no lump length")
    if int(brush.get("count", -1)) != length // MODEL_STRIDE:
        raise MapEntitiesGlbValidationError(
            "map.brushModels.count disagrees with the lump length it is derived from"
        )
    if int(brush.get("remainder", -1)) != length % MODEL_STRIDE:
        raise MapEntitiesGlbValidationError(
            "map.brushModels.remainder disagrees with the lump length it is derived from"
        )


def validate_document(
    document: dict[str, Any], binary: bytes, *, source_members=None
) -> dict[str, Any]:
    try:
        root = validate_extension_root(
            document,
            MAP_ENTITIES_EXTENSION,
            asset_prefix="vtmb:map-entities:",
            schema_version=SCHEMA_VERSION,
        )
        validate_container(document, binary)
        validate_sceneless(document)
        validate_ledgers(root, source_members)
        reject_opaque_source(document, binary, source_members)
    except UnitValidationError as error:
        raise MapEntitiesGlbValidationError(str(error)) from error
    if document.get("asset", {}).get("generator") != generator(KIND_TITLE):
        raise MapEntitiesGlbValidationError("asset.generator does not name this seam's exporter")
    if binary:
        raise MapEntitiesGlbValidationError("a map-entities unit carries no BIN chunk")

    identity = root.get("identity") or {}
    key = identity.get("map")
    if not isinstance(key, str) or not key or key != key.lower():
        raise MapEntitiesGlbValidationError("the identity carries no normalized map key")
    if identity.get("asset") != asset_id(key):
        raise MapEntitiesGlbValidationError("the identity disagrees with its map key")
    map_block = root.get("map") or {}
    if map_block.get("stem") != key:
        raise MapEntitiesGlbValidationError("the map block disagrees with the identity")

    for row in root.get("anomalies") or []:
        if not isinstance(row, dict) or row.get("role") not in ANOMALY_ROLES:
            raise MapEntitiesGlbValidationError(f"unknown source anomaly {row!r}")
    for row in root.get("omissions") or []:
        if not isinstance(row, dict) or row.get("role") not in OMISSION_ROLES:
            raise MapEntitiesGlbValidationError(f"unknown source omission {row!r}")
    for row in root.get("dependencies") or []:
        if row.get("role") not in DEPENDENCY_ROLES:
            raise MapEntitiesGlbValidationError(f"unknown dependency role {row.get('role')!r}")

    member = _check_span_member(root, key)
    counts = _check_back_links(root)
    _check_map_block(root, member, counts["entities"])
    _check_worldspawn(root)

    stats = completeness(root)
    if stats["unresolved"] or stats["unsupported"]:
        raise MapEntitiesGlbValidationError("the map-entities extension is incomplete")

    if source_members is not None:
        for member in source_members:
            _compare_with_source(root, member.data)

    ledger = ((root.get("coverage") or {}).get("byteLedger") or [{}])[0]
    unresolved_dependencies = [
        str(row.get("sourcePath") or row.get("asset"))
        for row in root.get("dependencies") or []
        if not row.get("resolved")
    ]
    warnings = _warnings(root, unresolved_dependencies)
    return {
        "asset": identity.get("asset"),
        "key": key,
        "mapRevision": map_block.get("mapRevision"),
        "entities": counts["entities"],
        "outputs": counts["outputs"],
        "classes": len(root.get("classCensus") or []),
        "scriptExpressions": len(root.get("scriptExpressions") or []),
        "dependencies": len(root.get("dependencies") or []),
        "comments": len(root.get("comments") or []),
        "anomalies": [str(row.get("role")) for row in root.get("anomalies") or []],
        "omissions": [row for row in root.get("omissions") or [] if isinstance(row, dict)],
        "unresolvedDependencies": unresolved_dependencies,
        "sourceBytes": ledger.get("byteLength", 0),
        "accountedBytes": ledger.get("accountedBytes", 0),
        "byteCoveragePercent": ledger.get("coveragePercent", 0.0),
        "stateBytes": ledger.get("stateBytes", {}),
        "warnings": warnings,
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def _warnings(root: dict[str, Any], unresolved_dependencies: list[str]) -> list[str]:
    """The contract's per-row warnings, plus the two this seam aggregates.

    A map's lump can hold thousands of rows the contract would speak one line each for, so the
    unresolved joins and the anomaly roles are also counted into one line apiece; nothing the
    shared helper says is dropped.
    """

    warnings = list(_contract_warnings_for(root))
    if unresolved_dependencies:
        unique = sorted(set(unresolved_dependencies))
        shown = unique[:4]
        warnings.append(
            "the install carries no member for " + ", ".join(shown)
            + (f" and {len(unique) - len(shown)} more" if len(unique) > len(shown) else "")
        )
    roles = Counter(str(row.get("role")) for row in root.get("anomalies") or [])
    if roles:
        counted = ", ".join(f"{role}x{count}" for role, count in sorted(roles.items()))
        warnings.append(f"the source departs from the entity lump's conventions: {counted}")
    return warnings


def warnings_for(summary: dict[str, Any]) -> list[str]:
    """What a published unit could not resolve, phrased for the operator."""

    return list(summary.get("warnings") or [])
