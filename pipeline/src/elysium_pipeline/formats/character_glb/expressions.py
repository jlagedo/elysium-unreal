"""Readable and compiled Faceposer resources selected by a character model."""

from __future__ import annotations

import shlex
import struct
from typing import Any


class CharacterFacialError(ValueError):
    """A selected expression resource is malformed or disagrees with its twin."""


def decode_txt(data: bytes) -> dict[str, Any]:
    """Decode a Faceposer expression TXT into keys and weighted rows."""
    text = data.decode("utf-8-sig", "replace")
    keys: list[str] = []
    keys_declared = False
    weighted = False
    rows = []
    for line_number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("//"):
            continue
        try:
            tokens = shlex.split(line, posix=True)
        except ValueError as error:
            raise CharacterFacialError(
                f"expression TXT line {line_number}: {error}"
            ) from error
        if not tokens:
            continue
        directive = tokens[0].lower()
        if directive == "$keys":
            if keys_declared:
                raise CharacterFacialError("expression TXT declares $keys more than once")
            keys = tokens[1:]
            keys_declared = True
            continue
        if directive == "$hasweighting":
            weighted = True
            continue
        if directive.startswith("$"):
            raise CharacterFacialError(
                f"expression TXT line {line_number}: unsupported directive {tokens[0]}"
            )
        if not keys_declared:
            raise CharacterFacialError(
                f"expression TXT line {line_number}: row appears before $keys"
            )
        width = len(keys) * (2 if weighted else 1)
        if len(tokens) < 2 + width:
            raise CharacterFacialError(
                f"expression TXT line {line_number}: {len(tokens)} tokens, "
                f"need at least {2 + width}"
            )
        try:
            numbers = [float(value) for value in tokens[2:2 + width]]
        except ValueError as error:
            raise CharacterFacialError(
                f"expression TXT line {line_number}: non-numeric controller value"
            ) from error
        values = []
        for index, key in enumerate(keys):
            if weighted:
                values.append(
                    {
                        "controller": key,
                        "value": numbers[index * 2],
                        "weight": numbers[index * 2 + 1],
                    }
                )
            else:
                values.append({"controller": key, "value": numbers[index]})
        rows.append(
            {
                "name": tokens[0],
                "class": tokens[1],
                "values": values,
                "description": " ".join(tokens[2 + width:]),
            }
        )
    if not keys_declared:
        raise CharacterFacialError("expression TXT carries no $keys declaration")
    return {"keys": keys, "hasWeighting": weighted, "rows": rows}


def decode_vfe_header(data: bytes) -> dict[str, Any]:
    """Decode the settled VtMB VFE header without treating the compiled body as TXT."""
    if len(data) < 144:
        raise CharacterFacialError(f"VFE is only {len(data)} bytes")
    if data[:4] != b"EFV\0":
        raise CharacterFacialError(f"bad VFE id {data[:4]!r}")
    version = struct.unpack_from("<i", data, 4)[0]
    end = data.find(b"\0", 8, 136)
    if end < 0:
        raise CharacterFacialError("VFE internal name has no terminator")
    length, row_count = struct.unpack_from("<ii", data, 136)
    return {
        "id": "EFV",
        "version": version,
        "name": data[8:end].decode("ascii", "replace").replace("\\", "/"),
        "declaredLength": length,
        "actualLength": len(data),
        "rowCount": row_count,
    }


def _range(data: bytes, offset: int, length: int, label: str) -> None:
    if offset < 0 or length < 0 or offset + length > len(data):
        raise CharacterFacialError(
            f"VFE {label} range {offset}+{length} exceeds {len(data)} bytes"
        )


def _cstr(data: bytes, offset: int, label: str) -> str:
    if not 0 <= offset < len(data):
        raise CharacterFacialError(f"VFE {label} string offset {offset} is outside the file")
    end = data.find(b"\0", offset)
    if end < 0:
        raise CharacterFacialError(f"VFE {label} string has no terminator")
    return data[offset:end].decode("ascii", "replace")


def decode_vfe(data: bytes) -> dict[str, Any]:
    """Decode the VtMB 128-name-header variant and every compiled semantic table."""
    header = decode_vfe_header(data)
    if len(data) < 172:
        raise CharacterFacialError(f"VFE is only {len(data)} bytes; full header is 172")
    (
        declared_length,
        setting_count,
        setting_offset,
        table_name_offset,
        index_count,
        index_offset,
        key_count,
        key_name_offset,
        key_mapping_offset,
    ) = struct.unpack_from("<9i", data, 136)
    if declared_length != len(data):
        raise CharacterFacialError(
            f"VFE declared length {declared_length} != source length {len(data)}"
        )
    if setting_count < 0 or index_count < 0 or key_count < 0:
        raise CharacterFacialError("VFE header declares a negative table count")
    _range(data, setting_offset, setting_count * 24, "settings")
    _range(data, index_offset, index_count * 4, "indexes")
    _range(data, key_name_offset, key_count * 4, "key names")
    _range(data, key_mapping_offset, key_count * 4, "key mappings")
    key_offsets = struct.unpack_from(f"<{key_count}i", data, key_name_offset) if key_count else ()
    keys = [_cstr(data, offset, f"key {index}") for index, offset in enumerate(key_offsets)]
    mappings = (
        list(struct.unpack_from(f"<{key_count}i", data, key_mapping_offset))
        if key_count
        else []
    )
    indexes = (
        list(struct.unpack_from(f"<{index_count}i", data, index_offset))
        if index_count
        else []
    )
    settings = []
    for setting_index in range(setting_count):
        record = setting_offset + setting_index * 24
        name_offset, kind, count, source_index, current, values_offset = struct.unpack_from(
            "<6i", data, record
        )
        if count < 0:
            raise CharacterFacialError(
                f"VFE setting {setting_index} declares negative value count {count}"
            )
        value_base = record + values_offset
        stride = 12 if kind == 0 else 8 if kind == 1 else 0
        if not stride:
            raise CharacterFacialError(
                f"VFE setting {setting_index} has unsupported type {kind}"
            )
        _range(data, value_base, count * stride, f"setting {setting_index} values")
        values = []
        for value_index in range(count):
            value = value_base + value_index * stride
            if kind == 0:
                key, weight, influence = struct.unpack_from("<iff", data, value)
                if not 0 <= key < len(keys):
                    raise CharacterFacialError(
                        f"VFE setting {setting_index} value {value_index} names key {key}"
                    )
                values.append(
                    {
                        "key": key,
                        "controller": keys[key],
                        "value": weight,
                        "weight": influence,
                    }
                )
            else:
                member, weight = struct.unpack_from("<2i", data, value)
                values.append({"setting": member, "weight": weight})
        settings.append(
            {
                "name": _cstr(data, record + name_offset, f"setting {setting_index}"),
                "type": "normal" if kind == 0 else "markov",
                "index": source_index,
                "currentIndex": current,
                "values": values,
            }
        )
    return {
        **header,
        "tableName": (
            _cstr(data, table_name_offset, "table name") if table_name_offset else ""
        ),
        "keys": keys,
        "keyMappings": mappings,
        "indexes": indexes,
        "settings": settings,
    }


def _class_index(value: str) -> int | None:
    if len(value) == 1:
        return ord(value)
    if value.lower().startswith("0x"):
        try:
            return int(value, 16)
        except ValueError:
            return None
    return None


def compare_txt_vfe(txt: dict[str, Any], vfe: dict[str, Any]) -> None:
    """Require both representations to decode to the same controller table."""
    if txt["keys"] != vfe["keys"]:
        raise CharacterFacialError("TXT and VFE controller key order disagrees")
    if len(txt["rows"]) != len(vfe["settings"]):
        raise CharacterFacialError(
            f"TXT has {len(txt['rows'])} rows; VFE has {len(vfe['settings'])} settings"
        )
    for row_index, (txt_row, vfe_row) in enumerate(zip(txt["rows"], vfe["settings"])):
        if txt_row["name"] != vfe_row["name"]:
            raise CharacterFacialError(
                f"TXT/VFE row {row_index} name {txt_row['name']!r}/{vfe_row['name']!r}"
            )
        class_index = _class_index(txt_row["class"])
        if class_index is not None and class_index != vfe_row["index"]:
            raise CharacterFacialError(
                f"TXT/VFE row {row_index} class index {class_index}/{vfe_row['index']}"
            )
        if vfe_row["type"] != "normal":
            raise CharacterFacialError(
                f"TXT row {row_index} corresponds to a VFE Markov setting"
            )
        compiled = {value["controller"]: value for value in vfe_row["values"]}
        for value in txt_row["values"]:
            other = compiled.get(value["controller"], {"value": 0.0, "weight": 0.0})
            if abs(value["value"] - other["value"]) > 1e-5 or abs(
                value.get("weight", 1.0) - other["weight"]
            ) > 1e-5:
                raise CharacterFacialError(
                    f"TXT/VFE row {row_index} controller {value['controller']} disagrees"
                )


def decode_selected(members) -> dict[str, Any]:
    """Decode every selected source while treating compiled VFE as runtime authority."""
    families: dict[str, dict[str, Any]] = {}
    for member in members:
        parts = member.role.split("-")
        if len(parts) != 3 or parts[0] != "facial":
            continue
        family, kind = parts[1], parts[2]
        entry = families.setdefault(family, {})
        entry[kind] = decode_txt(member.data) if kind == "txt" else decode_vfe(member.data)
    for family, entry in families.items():
        txt, vfe = entry.get("txt"), entry.get("vfe")
        if txt is not None and vfe is not None:
            try:
                compare_txt_vfe(txt, vfe)
            except CharacterFacialError as error:
                entry["semanticAgreement"] = False
                entry["semanticDifference"] = str(error)
            else:
                entry["semanticAgreement"] = True
        else:
            entry["semanticAgreement"] = None
        entry["runtimeAuthority"] = "vfe" if vfe is not None else "txt-uncompiled"
    return families
