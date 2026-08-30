"""Decode one expression-table unit: the compiled VFE, the readable TXT, or both.

`docs/architecture/seam_map_expression_table.md` owns the shape. The VFE is the runtime
authority and is restated twice: `vfe` carries the compiled header and setting records as
stored, and `table` reshapes them into the shared `keys[]`/`hasWeighting`/`rows[]` view the TXT's
`authoring` block also fills, so the two can be compared without ever being merged.
"""

from __future__ import annotations

import hashlib
import struct
from typing import Any

from elysium_pipeline.formats.expression_table_glb import lexer
from elysium_pipeline.formats.expression_table_glb.coverage import ledger_row
from elysium_pipeline.formats.expression_table_glb.model import (
    ExpressionTableModel,
    identity_class,
    stem_asset_id,
)
from elysium_pipeline.formats.expression_table_glb.source import ExpressionTableSourceClosure
from elysium_pipeline.formats.unit_contract import SourceMember

_HEADER_ID = b"EFV\0"
#: `struct.pack("<f", 1.0)`: what `phonemes_strong.vfe`/`phonemes_weak.vfe` store where
#: `length`/`numFlexSettings` belong -- the alternate-layout signature.
_ALTERNATE_MARK = struct.pack("<f", 1.0)

#: The seven header words from offset 144 to 172, named per Source's `flexsettinghdr_t` but not
#: yet verified against the loader for this widened-name layout -- carried as `typedUnidentified`.
_DIRECTORY_FIELDS = (
    "settingOffset",
    "tableNameOffset",
    "indexCount",
    "indexOffset",
    "keyCount",
    "keyNameOffset",
    "keyMappingOffset",
)

#: Below this, a VFE float32 and a TXT float64 parse of the same decimal text are the same
#: number -- the residue is binary32/64 conversion noise, not authored rounding.
_EXACT_TOLERANCE = 1e-6
_ROUNDING_TOLERANCE = 5e-4


class ExpressionTableDecodeError(RuntimeError):
    """The selected expression-table source cannot be decoded."""


def _range(data: bytes, offset: int, length: int, label: str, path: str) -> None:
    if offset < 0 or length < 0 or offset + length > len(data):
        raise ExpressionTableDecodeError(
            f"{path}: VFE {label} range {offset}+{length} exceeds {len(data)} bytes"
        )


def _cstr(data: bytes, offset: int, label: str, path: str) -> tuple[str, int]:
    if not 0 <= offset < len(data):
        raise ExpressionTableDecodeError(f"{path}: VFE {label} offset {offset} is outside the file")
    end = data.find(b"\0", offset)
    if end < 0:
        raise ExpressionTableDecodeError(f"{path}: VFE {label} has no terminator")
    return data[offset:end].decode("ascii", "replace"), end + 1 - offset


def _names_file(name: str, path: str) -> bool:
    trimmed = name.split("\x00", 1)[0].strip().replace("\\", "/").lower().lstrip("/")
    return trimmed == path.strip().replace("\\", "/").lower().lstrip("/")


def _fill_gaps(
    data: bytes, claims: list[tuple[int, int, str, str]]
) -> list[dict[str, Any]]:
    """Claim every byte the structural walk left unclaimed, in place, and return the omissions.

    The compiler 4-byte-aligns some of the variable-length tables (a key-name block, a settings
    array), which leaves short gaps between them that no single field owns. A gap of zero bytes
    is declared alignment; a non-zero gap past the last decoded record is `trailing-fill`, and
    everywhere else it is `interior-fill` -- both are omissions, never silently dropped bytes.
    """

    omissions: list[dict[str, Any]] = []
    ordered = sorted(claims, key=lambda claim: claim[0])
    cursor = 0
    for offset, length, _, _ in ordered:
        if offset > cursor:
            gap_length = offset - cursor
            if any(data[cursor:offset]):
                claims.append((cursor, gap_length, "omitted-proven", "vfe.alignment"))
                omissions.append(
                    {"role": "interior-fill", "sourceOffset": cursor, "length": gap_length}
                )
            else:
                claims.append((cursor, gap_length, "padding-zero", "vfe.alignment"))
        cursor = max(cursor, offset + length)
    if cursor < len(data):
        gap_length = len(data) - cursor
        if any(data[cursor:]):
            claims.append((cursor, gap_length, "omitted-proven", "vfe.trailing"))
            omissions.append(
                {"role": "trailing-fill", "sourceOffset": cursor, "length": gap_length}
            )
        else:
            claims.append((cursor, gap_length, "padding-zero", "vfe.trailing"))
    return omissions


def _is_alternate_layout(data: bytes) -> bool:
    return (
        len(data) >= 144
        and data[136:140] == _ALTERNATE_MARK
        and data[140:144] == _ALTERNATE_MARK
    )


def _class_from_index(index: int) -> str:
    """A single character for a printable code point, `0x…` for anything else.

    `_` (0x5F) is printable and round-trips through this rule, which is exactly the class an
    expression table's rows carry; a phoneme table's printable single-letter codes and non-ASCII
    scalars both fall out of the same one rule.
    """

    if 32 <= index <= 126:
        return chr(index)
    return "0x" + format(index & 0xFFFFFFFF, "04x")


def _phoneme_code_from_class(class_text: str, index: int) -> int | None:
    return None if class_text == "_" else index


def _txt_phoneme_code(class_text: str) -> int | None:
    if class_text == "_":
        return None
    if len(class_text) == 1:
        return ord(class_text)
    if class_text.lower().startswith("0x"):
        try:
            return int(class_text, 16)
        except ValueError:
            return None
    return None


def _decode_vfe_alternate(member: SourceMember) -> tuple[dict[str, Any], list, list, list]:
    data, path = member.data, member.path
    if len(data) < 136:
        raise ExpressionTableDecodeError(f"{path}: VFE is only {len(data)} bytes")
    if data[:4] != _HEADER_ID:
        raise ExpressionTableDecodeError(f"{path}: bad VFE id {data[:4]!r}")
    version = struct.unpack_from("<i", data, 4)[0]
    name, name_length = _cstr(data, 8, "internal name", path)
    names_file = _names_file(name, path)

    #: `id`/`version`/`name` are the only fields this layout decodes; every byte past the
    #: name's own terminator -- not just past the widened-name layout's offset 136, which holds
    #: no meaning here -- is unread and is claimed only through the `typedUnidentified` row
    #: below, per the seam doc's "carry the remainder as typedUnidentified ranges".
    name_end = 8 + name_length
    claims = [
        (0, name_end, "mapped", "vfe.header"),
        (name_end, len(data) - name_end, "mapped", "vfe.body"),
    ]
    typed_unidentified = [
        {
            "field": "body",
            "sourceOffset": name_end,
            "length": len(data) - name_end,
            "hex": data[name_end:].hex(),
            "sha256": hashlib.sha256(data[name_end:]).hexdigest(),
            "reason": "phonemes_strong/weak 1,260-byte alternate layout; field order unknown",
        }
    ]
    anomalies = [{"role": "alternate-layout", "byteLength": len(data)}]
    if not names_file:
        anomalies.append({"role": "internal-name-mismatch", "name": name, "path": path})

    vfe = {
        "id": "EFV",
        "version": version,
        "name": name,
        "namesFile": names_file,
        "length": None,
        "numFlexSettings": None,
        "directory": [],
        "tableName": "",
        "keys": [],
        "keyMappings": {"sourceOffset": None, "values": []},
        "indexes": {"sourceOffset": None, "values": []},
        "settings": [],
        "alternateLayout": True,
    }
    return vfe, claims, typed_unidentified, anomalies


def _decode_vfe_normal(
    member: SourceMember,
) -> tuple[dict[str, Any], dict[str, Any], list, list, list, list, list]:
    data, path = member.data, member.path
    if len(data) < 172:
        raise ExpressionTableDecodeError(f"{path}: VFE is only {len(data)} bytes; header is 172")
    if data[:4] != _HEADER_ID:
        raise ExpressionTableDecodeError(f"{path}: bad VFE id {data[:4]!r}")
    version = struct.unpack_from("<i", data, 4)[0]
    name, _ = _cstr(data, 8, "internal name", path)
    length, num_flex_settings = struct.unpack_from("<ii", data, 136)
    directory_values = struct.unpack_from("<7i", data, 144)
    (
        setting_offset,
        table_name_offset,
        index_count,
        index_offset,
        key_count,
        key_name_offset,
        key_mapping_offset,
    ) = directory_values

    if num_flex_settings < 0 or index_count < 0 or key_count < 0:
        raise ExpressionTableDecodeError(f"{path}: VFE header declares a negative table count")

    claims: list[tuple[int, int, str, str]] = [(0, 172, "mapped", "vfe.header")]
    anomalies: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    typed_unidentified = [
        {
            "field": field_name,
            "sourceOffset": 144 + index * 4,
            "value": value,
            "reason": "widened-name flexsettinghdr_t field order unverified against the loader",
        }
        for index, (field_name, value) in enumerate(zip(_DIRECTORY_FIELDS, directory_values))
    ]

    if length != len(data):
        anomalies.append(
            {"role": "declared-length-mismatch", "declaredLength": length, "actualLength": len(data)}
        )
    names_file = _names_file(name, path)
    if not names_file:
        anomalies.append({"role": "internal-name-mismatch", "name": name, "path": path})

    _range(data, setting_offset, num_flex_settings * 24, "settings", path)
    if index_count:
        _range(data, index_offset, index_count * 4, "indexes", path)
        claims.append((index_offset, index_count * 4, "mapped", "vfe.indexes"))
    table_name = ""
    if table_name_offset:
        table_name, table_name_length = _cstr(data, table_name_offset, "table name", path)
        claims.append((table_name_offset, table_name_length, "mapped-string", "vfe.tableName"))
    keys: list[str] = []
    if key_count:
        _range(data, key_name_offset, key_count * 4, "key names", path)
        _range(data, key_mapping_offset, key_count * 4, "key mappings", path)
        claims.append((key_name_offset, key_count * 4, "mapped", "vfe.keys.offsets"))
        claims.append((key_mapping_offset, key_count * 4, "mapped", "vfe.keys.mapping"))
        key_offsets = struct.unpack_from(f"<{key_count}i", data, key_name_offset)
        for key_index, key_offset in enumerate(key_offsets):
            key_name, key_length = _cstr(data, key_offset, f"key {key_index}", path)
            claims.append((key_offset, key_length, "mapped-string", f"vfe.keys[{key_index}].name"))
            keys.append(key_name)
    key_mappings = {
        "sourceOffset": key_mapping_offset if key_count else None,
        "values": list(struct.unpack_from(f"<{key_count}i", data, key_mapping_offset)) if key_count else [],
    }
    indexes = {
        "sourceOffset": index_offset if index_count else None,
        "values": list(struct.unpack_from(f"<{index_count}i", data, index_offset)) if index_count else [],
    }

    settings: list[dict[str, Any]] = []
    table_rows: list[dict[str, Any]] = []
    for setting_index in range(num_flex_settings):
        record = setting_offset + setting_index * 24
        claims.append((record, 24, "mapped", f"vfe.settings[{setting_index}]"))
        name_offset, kind, count, class_index, current, values_offset = struct.unpack_from(
            "<6i", data, record
        )
        if count < 0:
            raise ExpressionTableDecodeError(
                f"{path}: VFE setting {setting_index} has negative value count {count}"
            )
        setting_name, setting_name_length = _cstr(
            data, record + name_offset, f"setting {setting_index} name", path
        )
        claims.append(
            (record + name_offset, setting_name_length, "mapped-string", f"vfe.settings[{setting_index}].name")
        )

        value_base = record + values_offset
        stride = 12 if kind == 0 else 8
        value_length = count * stride
        _range(data, value_base, value_length, f"setting {setting_index} values", path)

        row_values: list[tuple[str, float, float]] = []
        raw_values: list[dict[str, Any]] = []
        if kind == 0:
            if count:
                claims.append((value_base, value_length, "mapped", f"vfe.values[{setting_index}]"))
            for value_index in range(count):
                offset = value_base + value_index * stride
                key_index, value, weight = struct.unpack_from("<iff", data, offset)
                if not 0 <= key_index < len(keys):
                    raise ExpressionTableDecodeError(
                        f"{path}: setting {setting_index} value {value_index} names key {key_index}"
                    )
                row_values.append((keys[key_index], value, weight))
                raw_values.append({"controller": keys[key_index], "value": value, "weight": weight})
        else:
            unsupported.append(
                {
                    "field": f"settings[{setting_index}]",
                    "sourceOffset": record,
                    "reason": "markov-setting-is-not-a-table-row",
                }
            )
            # `raw_values`/`settings[i].values` stays empty for a markov record -- nothing in the
            # extension represents its value bytes -- so the ledger claim over that range is only
            # `mapped` because the bytes are also published verbatim in `typedUnidentified`,
            # matching the alternate-layout VFE's precedent for a typed-but-unrepresented range.
            if value_length:
                claims.append((value_base, value_length, "mapped", f"vfe.values[{setting_index}]"))
                typed_unidentified.append(
                    {
                        "field": f"settings[{setting_index}].values",
                        "sourceOffset": value_base,
                        "length": value_length,
                        "hex": data[value_base:value_base + value_length].hex(),
                        "sha256": hashlib.sha256(data[value_base:value_base + value_length]).hexdigest(),
                        "reason": "markov-setting-is-not-a-table-row",
                    }
                )

        settings.append(
            {
                "index": setting_index,
                "name": setting_name,
                "sourceOffset": record,
                "nameOffset": record + name_offset,
                "type": "normal" if kind == 0 else "markov",
                "classIndex": class_index,
                "currentIndex": current,
                "valuesOffset": value_base,
                "values": raw_values,
            }
        )
        if kind == 0:
            class_text = _class_from_index(class_index)
            by_key = {controller: (value, weight) for controller, value, weight in row_values}
            table_rows.append(
                {
                    "index": setting_index,
                    "name": setting_name,
                    "class": class_text,
                    "phonemeCode": _phoneme_code_from_class(class_text, class_index),
                    "values": [by_key.get(key, (0.0, 0.0))[0] for key in keys],
                    "weights": [by_key.get(key, (0.0, 0.0))[1] for key in keys],
                    "description": "",
                }
            )

    omissions = _fill_gaps(data, claims)

    vfe = {
        "id": "EFV",
        "version": version,
        "name": name,
        "namesFile": names_file,
        "length": length,
        "numFlexSettings": num_flex_settings,
        "directory": [
            {"field": field_name, "sourceOffset": 144 + index * 4, "value": value}
            for index, (field_name, value) in enumerate(zip(_DIRECTORY_FIELDS, directory_values))
        ],
        "tableName": table_name,
        "keys": keys,
        "keyMappings": key_mappings,
        "indexes": indexes,
        "settings": settings,
        "alternateLayout": False,
    }
    table = {"keys": list(keys), "hasWeighting": True, "rows": table_rows}
    return vfe, table, claims, typed_unidentified, anomalies, omissions, unsupported


def decode_vfe(
    member: SourceMember,
) -> tuple[dict[str, Any], dict[str, Any] | None, list, list, list, list, list]:
    """Decode one VFE member. Returns `(vfe, table, claims, typedUnidentified, anomalies,
    omissions, unsupported)`; `table` is `None` for the two alternate-layout members."""

    data, path = member.data, member.path
    if len(data) < 8:
        raise ExpressionTableDecodeError(f"{path}: VFE is only {len(data)} bytes")
    if data[:4] != _HEADER_ID:
        raise ExpressionTableDecodeError(f"{path}: bad VFE id {data[:4]!r}")
    if _is_alternate_layout(data):
        vfe, claims, typed_unidentified, anomalies = _decode_vfe_alternate(member)
        return vfe, None, claims, typed_unidentified, anomalies, [], []
    return _decode_vfe_normal(member)


def _txt_row(path: str, index: int, line: lexer.Line, keys: list[str], has_weighting: bool) -> dict[str, Any]:
    tokens = line.tokens
    width = len(keys) * (2 if has_weighting else 1)
    if len(tokens) < 2 + width:
        raise ExpressionTableDecodeError(
            f"{path}: row at byte {line.offset} has {len(tokens)} tokens, needs at least {2 + width}"
        )
    try:
        numbers = [float(value) for value in tokens[2:2 + width]]
    except ValueError as error:
        raise ExpressionTableDecodeError(f"{path}: row at byte {line.offset}: {error}") from error
    if has_weighting:
        values, weights = numbers[0::2], numbers[1::2]
    else:
        values, weights = numbers, []
    class_text = tokens[1]
    return {
        "index": index,
        "name": tokens[0],
        "class": class_text,
        "phonemeCode": _txt_phoneme_code(class_text),
        "values": values,
        "weights": weights,
        "description": " ".join(tokens[2 + width:]),
    }


def decode_txt(
    member: SourceMember,
) -> tuple[dict[str, Any], dict[str, Any], list, list, list]:
    """Decode one TXT member. Returns `(txt, authoring, claims, unsupported, omissions)`."""

    data, path = member.data, member.path
    try:
        lines = lexer.tokenize(lexer.decode_text(data))
    except lexer.ExpressionTableLexError as error:
        raise ExpressionTableDecodeError(f"{path}: {error}") from error
    claims: list[tuple[int, int, str, str]] = []
    directives: list[dict[str, Any]] = []
    comments: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    keys_line: dict[str, Any] | None = None
    keys: list[str] = []
    has_weighting = False
    keys_declared = False
    rows: list[dict[str, Any]] = []
    directive_index = row_index = 0

    for line in lines:
        if line.kind == "bom":
            claims.append((line.offset, line.length, "omitted-proven", "txt.bom"))
            omissions.append({"role": "bom", "sourceOffset": line.offset, "length": line.length})
        elif line.kind == "blank":
            claims.append((line.offset, line.length, "omitted-proven", "txt.whitespace"))
            omissions.append(
                {"role": "whitespace", "sourceOffset": line.offset, "length": line.length}
            )
        elif line.kind == "comment":
            claims.append((line.offset, line.length, "mapped-text", "txt.comments"))
            comments.append({"sourceOffset": line.offset, "text": line.content})
        elif line.kind == "keys":
            if keys_declared:
                raise ExpressionTableDecodeError(f"{path}: $keys declared more than once")
            keys = list(line.tokens[1:])
            keys_declared = True
            keys_line = {"sourceOffset": line.offset, "keys": keys}
            claims.append((line.offset, line.length, "mapped-text", "txt.keys"))
        elif line.kind == "directive":
            claims.append((line.offset, line.length, "mapped-text", f"txt.directives[{directive_index}]"))
            directive_name = line.tokens[0]
            if directive_name.lower() == "$hasweighting":
                has_weighting = True
                directives.append({"name": "$hasweighting", "sourceOffset": line.offset})
            else:
                directives.append({"name": directive_name, "sourceOffset": line.offset})
                unsupported.append(
                    {
                        "field": directive_name,
                        "sourceOffset": line.offset,
                        "reason": "directive-outside-the-expression-table-vocabulary",
                    }
                )
            directive_index += 1
        else:  # "row"
            if not keys_declared:
                raise ExpressionTableDecodeError(f"{path}: row at byte {line.offset} appears before $keys")
            claims.append((line.offset, line.length, "mapped-text", f"txt.rows[{row_index}]"))
            rows.append(_txt_row(path, row_index, line, keys, has_weighting))
            row_index += 1

    if not keys_declared:
        raise ExpressionTableDecodeError(f"{path}: TXT carries no $keys declaration")

    txt = {
        "lineCount": len(lines),
        "keysLine": keys_line,
        "directives": directives,
        "comments": comments,
    }
    authoring = {"keys": keys, "hasWeighting": has_weighting, "rows": rows}
    return txt, authoring, claims, unsupported, omissions


def _row_value(row: dict[str, Any], index_of: dict[str, int], key: str) -> float:
    index = index_of.get(key)
    if index is None or index >= len(row["values"]):
        return 0.0
    return row["values"][index]


def _row_weight(row: dict[str, Any], index_of: dict[str, int], key: str) -> float:
    weights = row.get("weights") or []
    index = index_of.get(key)
    if index is None or index >= len(weights):
        return 0.0
    return weights[index]


def compare_tables(
    table: dict[str, Any] | None,
    authoring: dict[str, Any] | None,
    *,
    vfe_present: bool = True,
    txt_present: bool = True,
) -> tuple[dict[str, Any], list, list, list]:
    """Grade the VFE-derived `table` against the TXT-derived `authoring`.

    Returns `(comparison, anomalies, unresolved, omissions)`. Comparison never corrects either
    side; a state other than `equivalent` is also recorded as an `anomalies[]` (or, for a missing
    twin, `omissions[]`) row so a reader who only checks those lists still finds it.

    `vfe_present`/`txt_present` distinguish a genuinely missing twin (`no-twin`) from the two
    alternate-layout VFEs, which carry a TXT twin but decode no `table` to compare it against;
    that case is not one of the doc's six named states, so it is published as `not-comparable`.
    """

    if not vfe_present or not txt_present:
        missing = "txt" if vfe_present else "vfe"
        return {"state": "no-twin"}, [], [], [{"role": "no-twin", "missingMember": missing}]
    if table is None or authoring is None:
        return {"state": "not-comparable"}, [{"role": "not-comparable"}], [], []

    table_keys, authoring_keys = table["keys"], authoring["keys"]
    extra_keys: list[str] = []
    anomalies: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    if table_keys == authoring_keys:
        keys_state = "equivalent"
    else:
        common = [key for key in table_keys if key in authoring_keys]
        if common == authoring_keys and len(table_keys) > len(authoring_keys):
            extra_keys = [key for key in table_keys if key not in authoring_keys]
            keys_state = "extra-keys"
        else:
            keys_state = "unresolved"
            unresolved.append(
                {
                    "field": "keys",
                    "reason": "table-and-authoring-key-sets-disagree",
                    "tableKeys": table_keys,
                    "authoringKeys": authoring_keys,
                }
            )

    table_rows, authoring_rows = table["rows"], authoring["rows"]
    if len(table_rows) != len(authoring_rows):
        unresolved.append(
            {
                "field": "rows",
                "reason": "row-count-disagrees",
                "tableRows": len(table_rows),
                "authoringRows": len(authoring_rows),
            }
        )
        state = "extra-keys" if keys_state == "extra-keys" else "unresolved"
        result: dict[str, Any] = {"state": state}
        if extra_keys:
            result["extraKeys"] = extra_keys
            anomalies.append({"role": "extra-keys", "keys": extra_keys})
        if not anomalies:
            anomalies.append({"role": state})
        return result, anomalies, unresolved, []

    table_index = {key: index for index, key in enumerate(table_keys)}
    authoring_index = {key: index for index, key in enumerate(authoring_keys)}
    common_keys = [key for key in table_keys if key in authoring_index]

    alignment = list(range(len(table_rows)))
    row_order_state: str | None = None
    row_order_indices: list[int] = []
    mismatches = [
        i for i in range(len(table_rows)) if table_rows[i]["name"] != authoring_rows[i]["name"]
    ]
    if mismatches:
        if len(mismatches) == 1:
            row_order_state = "row-renamed"
            index = mismatches[0]
            anomalies.append(
                {
                    "role": "row-renamed",
                    "index": index,
                    "tableName": table_rows[index]["name"],
                    "authoringName": authoring_rows[index]["name"],
                }
            )
        elif sorted(row["name"] for row in table_rows) == sorted(
            row["name"] for row in authoring_rows
        ):
            row_order_state = "row-order"
            row_order_indices = mismatches
            anomalies.append({"role": "row-order", "indices": mismatches})
            authoring_by_name = {row["name"]: index for index, row in enumerate(authoring_rows)}
            alignment = [
                authoring_by_name.get(row["name"], position)
                for position, row in enumerate(table_rows)
            ]
        else:
            unresolved.append({"field": "rows", "reason": "row-names-disagree", "indices": mismatches})

    has_weighting = bool(table.get("hasWeighting")) and bool(authoring.get("hasWeighting"))
    max_delta = 0.0
    max_weight_delta = 0.0
    exact = True
    for table_position, authoring_position in enumerate(alignment):
        table_row, authoring_row = table_rows[table_position], authoring_rows[authoring_position]
        for key in common_keys:
            delta = abs(
                _row_value(table_row, table_index, key) - _row_value(authoring_row, authoring_index, key)
            )
            if delta > _EXACT_TOLERANCE:
                exact = False
                max_delta = max(max_delta, delta)
            if has_weighting:
                weight_delta = abs(
                    _row_weight(table_row, table_index, key) - _row_weight(authoring_row, authoring_index, key)
                )
                if weight_delta > _EXACT_TOLERANCE:
                    exact = False
                    max_weight_delta = max(max_weight_delta, weight_delta)

    # A value/weight delta beyond the rounding tolerance is a real disagreement between the two
    # candidate interpretations, so it is disclosed in `coverage.unresolved` (and carried on
    # `comparison` as `maxDelta`/`maxWeightDelta`) no matter which state -- `extra-keys`,
    # `row-order`, `row-renamed`, or a plain values disagreement -- already claims the slot; a
    # structural anomaly elsewhere in the row never excuses silently dropping a values fact.
    worst_delta = max(max_delta, max_weight_delta)
    value_delta_exceeds_tolerance = not exact and worst_delta > _ROUNDING_TOLERANCE
    if value_delta_exceeds_tolerance:
        extra = {"maxWeightDelta": max_weight_delta} if max_weight_delta else {}
        unresolved.append(
            {
                "field": "values",
                "reason": "value-delta-exceeds-rounding-tolerance",
                "maxDelta": max_delta,
                **extra,
            }
        )
        anomalies.append(
            {"role": "value-delta-exceeds-rounding-tolerance", "maxDelta": max_delta, **extra}
        )

    if row_order_state:
        state = row_order_state
    elif keys_state == "extra-keys":
        state = "extra-keys"
        anomalies.append({"role": "extra-keys", "keys": extra_keys})
    elif keys_state == "unresolved" or value_delta_exceeds_tolerance:
        state = "unresolved"
    elif any(row.get("reason") == "row-names-disagree" for row in unresolved):
        state = "unresolved"
    elif not exact:
        state = "rounding-only"
        anomalies.append({"role": "rounding-only", "maxDelta": max_delta, **(
            {"maxWeightDelta": max_weight_delta} if max_weight_delta else {}
        )})
    else:
        state = "equivalent"

    # Every state other than `equivalent` is also an `anomalies[]` row (seam doc, "Comparison");
    # the branches above each add their own specific row except a bare `unresolved` classification
    # reached only through the keys/rows disagreements above, which gets this fallback.
    if state != "equivalent" and not anomalies:
        anomalies.append({"role": state})

    result: dict[str, Any] = {"state": state}
    if extra_keys:
        result["extraKeys"] = extra_keys
    if state == "rounding-only" or value_delta_exceeds_tolerance:
        result["maxDelta"] = max_delta
        if max_weight_delta:
            result["maxWeightDelta"] = max_weight_delta
    if row_order_state == "row-order":
        result["indices"] = row_order_indices
    if row_order_state == "row-renamed":
        result["index"] = mismatches[0]
    return result, anomalies, unresolved, []


def decode_expression_table(closure: ExpressionTableSourceClosure) -> ExpressionTableModel:
    """Decode the complete unit for one source closure: the VFE, the TXT, or both."""

    vfe_block = table_block = txt_block = authoring_block = None
    anomalies: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    typed_unidentified: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    byte_ledger: list[dict[str, Any]] = []

    if closure.vfe is not None:
        (
            vfe_block,
            table_block,
            vfe_claims,
            vfe_typed,
            vfe_anomalies,
            vfe_omissions,
            vfe_unsupported,
        ) = decode_vfe(closure.vfe)
        byte_ledger.append(ledger_row(closure.vfe.path, closure.vfe.data, vfe_claims))
        typed_unidentified.extend(vfe_typed)
        anomalies.extend(vfe_anomalies)
        omissions.extend(vfe_omissions)
        unsupported.extend(vfe_unsupported)

    if closure.txt is not None:
        txt_block, authoring_block, txt_claims, txt_unsupported, txt_omissions = decode_txt(
            closure.txt
        )
        byte_ledger.append(ledger_row(closure.txt.path, closure.txt.data, txt_claims))
        unsupported.extend(txt_unsupported)
        omissions.extend(txt_omissions)

    comparison, compare_anomalies, compare_unresolved, compare_omissions = compare_tables(
        table_block,
        authoring_block,
        vfe_present=closure.vfe is not None,
        txt_present=closure.txt is not None,
    )
    anomalies.extend(compare_anomalies)
    unresolved.extend(compare_unresolved)
    omissions.extend(compare_omissions)

    if closure.vfe is not None and closure.txt is not None:
        source_kind = "vfe+txt"
    elif closure.vfe is not None:
        source_kind = "vfe-only"
    else:
        source_kind = "txt-only"

    return ExpressionTableModel(
        stem=closure.stem,
        asset=stem_asset_id(closure.stem),
        identity_class_=identity_class(closure.stem),
        source_kind=source_kind,
        runtime_loadable=closure.vfe is not None,
        members=closure.members(),
        vfe=vfe_block,
        table=table_block,
        txt=txt_block,
        authoring=authoring_block,
        comparison=comparison,
        anomalies=anomalies,
        omissions=omissions,
        typed_unidentified=typed_unidentified,
        unresolved=unresolved,
        unsupported=unsupported,
        byte_ledger=byte_ledger,
    )
