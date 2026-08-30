"""Independent structural validator for sound-scheme GLB products.

This re-decodes nothing through `exporters.sound_scheme_glb.build_document`: it re-parses the
published extension against the vocabulary `seam_map_sound_scheme.md` states and the byte-ledger
and container rules `formats.unit_contract` states, so a writer bug cannot pass its own check.

When `source_members` is given (the export-time path), it goes one step further and re-tokenizes
the member bytes with `formats.sound_scheme_glb.lexer` -- never through `decode_sound_scheme` or
`exporters.sound_scheme_glb.build_document` -- to rebuild the block/parameter structure the decode
step must have published, and checks the published `parameters[]` table against that re-parse
byte for byte. Independently of that (so standalone validation, with no source present, still
catches it), every `{value, raw, parameter}` record `scheme` carries is checked against the
`parameters[]` row its own `parameter` index names: the row's `value` must equal the record's
`raw`, the row's `block` must be the record's own block (when an independent re-parse can say
what that is), and re-parsing `raw` as a number must reproduce the record's own `value`.
"""

from __future__ import annotations

from collections import Counter
import math
from pathlib import Path
from typing import Any

from elysium_pipeline.formats.sound_scheme_glb import SOUND_SCHEME_EXTENSION, lexer
from elysium_pipeline.formats.sound_scheme_glb.model import (
    DSP_PRESET_PATH,
    KIND,
    SCHEMA_VERSION,
    SOURCE_ROOT,
    SOURCE_SUFFIX,
    sound_asset_id,
    sound_dependency_source_path,
)
from elysium_pipeline.formats.unit_contract import (
    UnitValidationError,
    completeness,
    read_glb,
    reject_opaque_source,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)

#: Derived from the contract rather than respelled by hand, so this validator's identity
#: vocabulary cannot drift from the one `formats.sound_scheme_glb` publishes.
ASSET_PREFIX = f"vtmb:{KIND}:"

#: The block-name canonicalization the decode publishes into `parameters[].block` and `scheme`'s
#: own keys. Restated here (rather than imported from `formats.sound_scheme_glb.decode`) so an
#: independent re-parse of the source answers to the seam's own vocabulary, not to whatever table
#: the decode step happens to be using -- the same reason `formats/surface_property_glb`'s own
#: validator restates its vocabulary rather than importing it.
KNOWN_BLOCKS = {
    "schemeparams": "SchemeParams",
    "music": "Music",
    "combat": "Combat",
    "alert": "Alert",
    "ambient": "Ambient",
    "randomsound": "RandomSound",
}


class SoundSchemeGlbValidationError(UnitValidationError):
    pass


#: The grammar and convention departures the decode is allowed to record. A row naming anything
#: else means the writer invented a tolerance the seam never agreed to.
ANOMALY_ROLES = {
    "unterminated-quoted-string",
    "valueless-key",
    "unclosed-block-at-end-of-file",
    "repeated-scalar-key",
    "repeated-block",
    "range-inverted",
    "volume-out-of-range",
    "unresolved-file",
    "trailing-content-after-root",
}

#: `Ambient`'s own column is `Filename`, `Volume` alone; three shipped schemes author `NoPause`
#: there too, and one also authors `Dry`, so `Ambient` is decoded with `Music`/`Combat`/`Alert`'s
#: own four-key vocabulary (see `specDeviations` in the seam's export manifest).
MUSIC_LIKE_FIELDS = {"file", "asset", "parameter", "volume", "dry", "noPause"}
AMBIENT_FIELDS = MUSIC_LIKE_FIELDS
#: `dry` is not in `seam_map_sound_scheme.md`'s own `RandomSound` column; the decoder accepts it
#: there too because one shipped scheme authors it with `Music`/`Combat`/`Alert`'s own meaning
#: (see `specDeviations` in the seam's export manifest).
RANDOM_SOUND_FIELDS = {
    "file", "asset", "parameter", "pitch", "volume", "frequency", "audibleRadius",
    "distance", "height", "angle", "dry",
}
RANGE_FIELDS = {"pitch", "distance", "height", "angle"}
#: The folded source-key pair each `RANGE_FIELDS` name reads from -- not simply `<name>min`/
#: `<name>max`, since `decode.py`'s own `distance`/`height` fold to `Dist`/`Height`.
RANGE_FIELD_KEYS = {
    "pitch": ("pitchmin", "pitchmax"),
    "distance": ("distmin", "distmax"),
    "height": ("heightmin", "heightmax"),
    "angle": ("anglemin", "anglemax"),
}
#: The folded source key a flag field reads from.
FLAG_FIELD_KEYS = {"dry": "dry", "noPause": "nopause"}
DEPENDENCY_ROLES = {"sound", "dsp-preset"}


def _reparsed_number(raw: Any) -> float | None:
    """The same finite-float parsing `decode.py`'s own `_number` performs, restated rather than
    imported so a bug there is not silently trusted here."""

    try:
        value = float(str(raw).strip().strip('"'))
    except ValueError:
        return None
    return value if math.isfinite(value) else None


def _assert_last_wins(parameters: list[dict[str, Any]], index: int, where: str) -> None:
    """The named row is the *last* row of its own block with its own key.

    `decode.py`'s own `scalar_field` overwrites a repeated scalar key with the later occurrence
    (recording a `repeated-scalar-key` anomaly for it), so a `scheme` field naming an earlier
    occurrence of a repeated key is a decode bug, not a legitimate choice.
    """

    row = parameters[index]
    block, key = row.get("block"), row.get("key")
    candidates = [i for i, other in enumerate(parameters) if other.get("block") == block and other.get("key") == key]
    if index != max(candidates):
        raise SoundSchemeGlbValidationError(f"{where} names a repeated key's earlier row, not its last")


def _field_record(
    value: Any,
    where: str,
    *,
    parameters: list[dict[str, Any]],
    block: str | None,
    key: str,
    kind: str = "number",
) -> None:
    """A `{value, raw, parameter}` record: `parameter` names a real row of this scheme's own
    `parameters[]`, that row's own `key` is this field's own source key, that row is the last row
    of its own block with that key, that row's `value` is the record's own `raw` verbatim, that
    row's `block` is the record's own block when an independent re-parse says what that is, and
    re-parsing `raw` reproduces `value` (as a number, or -- for `kind="flag"` -- as a boolean)."""

    if value is None:
        return
    if not isinstance(value, dict) or set(value) != {"value", "raw", "parameter"}:
        raise SoundSchemeGlbValidationError(f"{where} is not a value/raw/parameter record")
    index = value["parameter"]
    if not isinstance(index, int) or not 0 <= index < len(parameters):
        raise SoundSchemeGlbValidationError(f"{where} names no parameter of this scheme")
    parameter = parameters[index]
    if parameter.get("key") != key:
        raise SoundSchemeGlbValidationError(
            f"{where} names a parameter keyed {parameter.get('key')!r}, not {key!r}"
        )
    _assert_last_wins(parameters, index, where)
    if block is not None and parameter.get("block") != block:
        raise SoundSchemeGlbValidationError(
            f"{where} names a parameter from {parameter.get('block')!r}, not {block!r}"
        )
    if parameter.get("value") != value["raw"]:
        raise SoundSchemeGlbValidationError(f"{where} disagrees with its own source token")
    number = _reparsed_number(value["raw"])
    if number is None:
        raise SoundSchemeGlbValidationError(f"{where}'s raw token is not the number it claims")
    if kind == "flag":
        if not isinstance(value["value"], bool) or value["value"] != bool(number):
            raise SoundSchemeGlbValidationError(f"{where} disagrees with its own raw token")
    else:
        if not isinstance(value["value"], (int, float)) or isinstance(value["value"], bool):
            raise SoundSchemeGlbValidationError(f"{where} is not a number")
        if not math.isfinite(value["value"]):
            raise SoundSchemeGlbValidationError(f"{where} is not a finite number")
        if not math.isclose(float(value["value"]), number, rel_tol=1e-9, abs_tol=1e-9):
            raise SoundSchemeGlbValidationError(f"{where} disagrees with its own raw token")


def _ledger_ranges(extension: dict[str, Any]) -> list[dict[str, Any]]:
    ledger_rows = (extension.get("coverage") or {}).get("byteLedger") or []
    ranges = ledger_rows[0].get("ranges") if ledger_rows else []
    if not isinstance(ranges, list):
        raise SoundSchemeGlbValidationError("the scheme's byte ledger carries no range table")
    return ranges


def _check_parameters(extension: dict[str, Any]) -> list[dict[str, Any]]:
    parameters = extension.get("parameters")
    if not isinstance(parameters, list):
        raise SoundSchemeGlbValidationError("the scheme declares no parameter table")
    offsets: list[int] = []
    for index, row in enumerate(parameters):
        if not isinstance(row, dict) or row.get("index") != index:
            raise SoundSchemeGlbValidationError(f"parameter {index} is not in source order")
        key, source_key = row.get("key"), row.get("sourceKey")
        if not isinstance(key, str) or not key:
            raise SoundSchemeGlbValidationError(f"parameter {index} has no key")
        if not isinstance(source_key, str) or source_key.strip().lower() != key:
            raise SoundSchemeGlbValidationError(f"parameter {index} key disagrees with its source")
        block = row.get("block")
        if not isinstance(block, str) or "[" not in block or not block.endswith("]"):
            raise SoundSchemeGlbValidationError(f"parameter {index} names no block path")
        offset = row.get("offset")
        if not isinstance(offset, int) or offset < 0 or offset in offsets:
            raise SoundSchemeGlbValidationError(f"parameter {index} has no distinct source offset")
        offsets.append(offset)
    if offsets != sorted(offsets):
        raise SoundSchemeGlbValidationError("the parameter table is not in source order")

    # `blocks[i].parameters[j]` is the owner every pair's key (and, when present, its value) is
    # claimed under (`decode.py`'s own `_claim` calls); a pair's `Parameter.offset` is always its
    # *key* token's offset, so the earliest range under a given owner is the start each published
    # row must name -- and every such owner must be claimed by exactly one row, or the reverse.
    owner_starts: dict[str, int] = {}
    for row in _ledger_ranges(extension):
        owner = str(row.get("owner", ""))
        if owner.startswith("blocks[") and "].parameters[" in owner:
            offset = row.get("offset")
            if owner not in owner_starts or offset < owner_starts[owner]:
                owner_starts[owner] = offset
    if set(offsets) != set(owner_starts.values()):
        raise SoundSchemeGlbValidationError(
            "the parameter table disagrees with the byte ledger's own blocks[*].parameters[*] ranges"
        )
    return parameters


_PARAMETER_ROW_KEYS = ("block", "key", "sourceKey", "value", "quotedKey", "quotedValue", "offset")


def _reparsed_structure(
    text: str,
) -> tuple[list[dict[str, Any]], dict[str, str], list[str], dict[str, dict[str, str]]]:
    """Re-tokenizes `text` with the shared grammar lexer, independently of
    `formats.sound_scheme_glb.decode`, and rebuilds the block/parameter structure the decode step
    must publish: the flat parameter table (in the same shape `parameters[]` carries), the block
    path the *last* occurrence of each (possibly repeated) block name owns, the block path each
    `RandomSound` occurrence owns (in source order), and -- for every block path -- the folded
    key -> raw value text the block itself authors (a valueless key's value is `""`, the same
    convention `Parameter.value` uses), so a caller can tell whether a block the decode discarded
    or never published a field for actually named a value for it.
    """

    tokens = lexer.tokenize(text)
    root = lexer.parse(tokens)
    seen: dict[str, int] = {}
    parameters: list[dict[str, Any]] = []
    last_occurrence: dict[str, str] = {}
    random_sound_paths: list[str] = []
    block_fields: dict[str, dict[str, str]] = {}
    for block in root.blocks:
        folded = block.name.text.strip().lower()
        canonical = KNOWN_BLOCKS.get(folded, block.name.text.strip())
        occurrence = seen.get(folded, 0)
        seen[folded] = occurrence + 1
        block_path = f"{canonical}[{occurrence}]"
        last_occurrence[folded] = block_path
        if folded == "randomsound":
            random_sound_paths.append(block_path)
        fields: dict[str, str] = {}
        for pair in block.pairs:
            value = "" if pair.value is None else pair.value.text
            fields[pair.key.text.strip().lower()] = value
            parameters.append(
                {
                    "block": block_path,
                    "key": pair.key.text.strip().lower(),
                    "sourceKey": pair.key.text,
                    "value": value,
                    "quotedKey": pair.key.quoted,
                    "quotedValue": bool(pair.value is not None and pair.value.quoted),
                    "offset": pair.key.offset,
                }
            )
        block_fields[block_path] = fields
    return parameters, last_occurrence, random_sound_paths, block_fields


def _block_has_nonempty(
    block_fields: dict[str, dict[str, str]] | None, block_path: str | None, key: str
) -> bool | None:
    """Whether `block_path`'s own re-parsed fields name a non-empty value for `key`.

    `None` means "unknown" -- either no source was available to re-parse, or the block itself is
    unknown -- so the caller skips the check rather than treating an absence as a `False`.
    """

    if block_fields is None or block_path is None:
        return None
    raw = block_fields.get(block_path, {}).get(key)
    return bool(raw is not None and str(raw).strip())


def _check_reparse(
    parameters: list[dict[str, Any]], source_members
) -> tuple[dict[str, str] | None, list[str] | None, dict[str, dict[str, str]] | None]:
    """Proves the published `parameters[]` table is exactly what an independent re-parse of the
    one source member produces, when a member is available to re-parse.

    Returns `(None, None, None)` when `source_members` is absent (standalone validation, no
    install present) -- the caller then skips the block-identity and block-presence cross-checks
    `_field_record`, `_check_sound_record` and `_check_scheme` would otherwise run, since nothing
    here can say what those *should* be without the source; every other check in this module
    still runs regardless.
    """

    if source_members is None:
        return None, None, None
    members = list(source_members)
    if len(members) != 1:
        raise SoundSchemeGlbValidationError("a sound-scheme unit owns exactly one source member")
    text = lexer.decode_text(members[0].data)
    try:
        expected_parameters, last_occurrence, random_sound_paths, block_fields = _reparsed_structure(text)
    except lexer.SoundSchemeLexError as error:
        raise SoundSchemeGlbValidationError(
            f"the source no longer re-parses as a sound scheme: {error}"
        ) from error
    published = [{key: row.get(key) for key in _PARAMETER_ROW_KEYS} for row in parameters]
    if published != expected_parameters:
        raise SoundSchemeGlbValidationError(
            "the published parameter table disagrees with an independent re-parse of the source"
        )
    return last_occurrence, random_sound_paths, block_fields


def _check_sound_record(
    record: Any,
    fields: set[str],
    where: str,
    referenced: dict[str, str],
    source_paths: dict[str, str],
    *,
    parameters: list[dict[str, Any]],
    expected_block: str | None,
    expect_file: bool | None = None,
) -> None:
    if not isinstance(record, dict) or set(record) != fields:
        raise SoundSchemeGlbValidationError(f"{where} does not carry the scheme's own fields")
    file_value, asset = record.get("file"), record.get("asset")
    if (file_value is None) != (asset is None):
        raise SoundSchemeGlbValidationError(f"{where} names a file without an asset, or the reverse")
    if expect_file and file_value is None:
        raise SoundSchemeGlbValidationError(
            f"{where} carries no file though an independent re-parse of its own source names one"
        )
    if file_value is not None:
        if not isinstance(asset, str) or not asset.startswith("vtmb:sound:"):
            raise SoundSchemeGlbValidationError(f"{where} carries no stable sound id")
        slot = record.get("parameter")
        if not isinstance(slot, int) or not 0 <= slot < len(parameters):
            raise SoundSchemeGlbValidationError(f"{where} names no source parameter for its file")
        parameter = parameters[slot]
        if parameter.get("key") != "filename":
            raise SoundSchemeGlbValidationError(
                f"{where} names a parameter keyed {parameter.get('key')!r}, not 'filename'"
            )
        _assert_last_wins(parameters, slot, where)
        if expected_block is not None and parameter.get("block") != expected_block:
            raise SoundSchemeGlbValidationError(
                f"{where} names a parameter from {parameter.get('block')!r}, not {expected_block!r}"
            )
        if str(parameter.get("value", "")).strip() != str(file_value):
            raise SoundSchemeGlbValidationError(f"{where} disagrees with its own source token")
        if sound_asset_id(file_value) != asset:
            raise SoundSchemeGlbValidationError(f"{where}'s asset id disagrees with its own path")
        referenced[asset] = "sound"
        # First-authored spelling wins on a dedupe, the same rule `decode.py`'s own
        # `_collect_dependencies` uses, so two occurrences of one case-folded asset (only their
        # spelling differing) do not fight over which one names the published `sourcePath`.
        source_paths.setdefault(asset, sound_dependency_source_path(file_value))
    elif record.get("parameter") is not None:
        raise SoundSchemeGlbValidationError(f"{where} names a parameter with no file")
    for field_name in ("volume", "frequency", "audibleRadius"):
        if field_name in fields:
            _field_record(
                record.get(field_name), f"{where}.{field_name}",
                parameters=parameters, block=expected_block, key=field_name.lower(),
            )
    for field_name in RANGE_FIELDS & fields:
        pair = record.get(field_name)
        if not isinstance(pair, dict) or set(pair) != {"min", "max"}:
            raise SoundSchemeGlbValidationError(f"{where}.{field_name} is not a min/max pair")
        min_key, max_key = RANGE_FIELD_KEYS[field_name]
        _field_record(
            pair.get("min"), f"{where}.{field_name}.min",
            parameters=parameters, block=expected_block, key=min_key,
        )
        _field_record(
            pair.get("max"), f"{where}.{field_name}.max",
            parameters=parameters, block=expected_block, key=max_key,
        )
    for field_name in ("dry", "noPause"):
        if field_name in fields:
            value = record.get(field_name)
            if value is not None:
                _field_record(
                    value, f"{where}.{field_name}",
                    parameters=parameters, block=expected_block, key=FLAG_FIELD_KEYS[field_name],
                    kind="flag",
                )


def _check_scheme(
    extension: dict[str, Any],
    parameters: list[dict[str, Any]],
    last_occurrence: dict[str, str] | None,
    random_sound_paths: list[str] | None,
    block_fields: dict[str, dict[str, str]] | None,
) -> tuple[dict[str, str], dict[str, str], dict[str, int]]:
    """Every field `scheme` carries is checked against the seam's own vocabulary, and -- when
    `last_occurrence`/`block_fields` are available -- the reverse direction too: a folded
    singleton block an independent re-parse finds must have a non-null `scheme` field, and a
    `Filename` it names non-empty must survive as that field's own `file`/`asset`. Without that
    (standalone validation, no source to re-parse) only the forward direction runs.

    Returns the assets the scheme references (asset -> role), the `sourcePath` each one is
    expected to carry, and a small census used by the validation summary.
    """

    scheme = extension.get("scheme")
    if not isinstance(scheme, dict) or set(scheme) != {
        "params", "music", "combat", "alert", "ambient", "randomSounds",
    }:
        raise SoundSchemeGlbValidationError("the scheme does not carry its own six fields")

    referenced: dict[str, str] = {}
    source_paths: dict[str, str] = {}
    census = {"music": 0, "combat": 0, "alert": 0, "ambient": 0, "randomSounds": 0}

    params = scheme.get("params")
    if last_occurrence is not None and "schemeparams" in last_occurrence and params is None:
        raise SoundSchemeGlbValidationError(
            "an independent re-parse of the source finds a SchemeParams block but scheme.params "
            "is missing"
        )
    if params is not None:
        if not isinstance(params, dict) or set(params) != {"randomSoundCount", "roomDsp"}:
            raise SoundSchemeGlbValidationError("scheme.params does not carry its own two fields")
        if last_occurrence is not None and "schemeparams" not in last_occurrence:
            raise SoundSchemeGlbValidationError(
                "scheme.params is published but an independent re-parse of the source finds no "
                "SchemeParams block"
            )
        expected_block = None if last_occurrence is None else last_occurrence["schemeparams"]
        if _block_has_nonempty(block_fields, expected_block, "randomsoundcount") and (
            params.get("randomSoundCount") is None
        ):
            raise SoundSchemeGlbValidationError(
                "scheme.params.randomSoundCount is missing though an independent re-parse of its "
                "own source names a value"
            )
        _field_record(
            params.get("randomSoundCount"), "scheme.params.randomSoundCount",
            parameters=parameters, block=expected_block, key="randomsoundcount",
        )
        room_dsp = params.get("roomDsp")
        if _block_has_nonempty(block_fields, expected_block, "roomdsp") and room_dsp is None:
            raise SoundSchemeGlbValidationError(
                "scheme.params.roomDsp is missing though an independent re-parse of its own "
                "source names a value"
            )
        if room_dsp is not None:
            if not isinstance(room_dsp, dict) or set(room_dsp) != {
                "id", "asset", "resolved", "parameter",
            }:
                raise SoundSchemeGlbValidationError("scheme.params.roomDsp is not a preset record")
            asset = room_dsp.get("asset")
            if not isinstance(asset, str) or asset != "vtmb:dsp-preset:" + str(room_dsp.get("id")):
                raise SoundSchemeGlbValidationError("scheme.params.roomDsp names no stable preset id")
            if not isinstance(room_dsp.get("resolved"), bool):
                raise SoundSchemeGlbValidationError("scheme.params.roomDsp does not state resolution")
            slot = room_dsp.get("parameter")
            if not isinstance(slot, int) or not 0 <= slot < len(parameters):
                raise SoundSchemeGlbValidationError("scheme.params.roomDsp names no source parameter")
            parameter = parameters[slot]
            if parameter.get("key") != "roomdsp":
                raise SoundSchemeGlbValidationError(
                    f"scheme.params.roomDsp names a parameter keyed {parameter.get('key')!r}, "
                    "not 'roomdsp'"
                )
            _assert_last_wins(parameters, slot, "scheme.params.roomDsp")
            if expected_block is not None and parameter.get("block") != expected_block:
                raise SoundSchemeGlbValidationError(
                    "scheme.params.roomDsp names a parameter outside SchemeParams"
                )
            if str(parameter.get("value", "")).strip().strip('"') != str(room_dsp.get("id")):
                raise SoundSchemeGlbValidationError(
                    "scheme.params.roomDsp disagrees with its own source token"
                )
            referenced[asset] = "dsp-preset"
            source_paths[asset] = f"{DSP_PRESET_PATH}#{room_dsp.get('id')}"

    for key, fields in (("music", MUSIC_LIKE_FIELDS), ("combat", MUSIC_LIKE_FIELDS),
                        ("alert", MUSIC_LIKE_FIELDS), ("ambient", AMBIENT_FIELDS)):
        record = scheme.get(key)
        if last_occurrence is not None and key in last_occurrence and record is None:
            raise SoundSchemeGlbValidationError(
                f"an independent re-parse of the source finds a {key} block but scheme.{key} is "
                "missing"
            )
        if record is not None:
            if last_occurrence is not None and key not in last_occurrence:
                raise SoundSchemeGlbValidationError(
                    f"scheme.{key} is published but an independent re-parse of the source finds "
                    f"no {key} block"
                )
            expected_block = None if last_occurrence is None else last_occurrence[key]
            expect_file = _block_has_nonempty(block_fields, expected_block, "filename")
            _check_sound_record(
                record, fields, f"scheme.{key}", referenced, source_paths,
                parameters=parameters, expected_block=expected_block, expect_file=expect_file,
            )
            census[key] = 1

    random_sounds = scheme.get("randomSounds")
    if not isinstance(random_sounds, list):
        raise SoundSchemeGlbValidationError("scheme.randomSounds is not a list")
    if random_sound_paths is not None and len(random_sounds) != len(random_sound_paths):
        raise SoundSchemeGlbValidationError(
            "scheme.randomSounds count disagrees with an independent re-parse of the source"
        )
    for ordinal, record in enumerate(random_sounds):
        expected_block = None if random_sound_paths is None else random_sound_paths[ordinal]
        expect_file = _block_has_nonempty(block_fields, expected_block, "filename")
        _check_sound_record(
            record, RANDOM_SOUND_FIELDS, f"scheme.randomSounds[{ordinal}]", referenced, source_paths,
            parameters=parameters, expected_block=expected_block, expect_file=expect_file,
        )
    census["randomSounds"] = len(random_sounds)

    return referenced, source_paths, census


def _check_dependencies(
    extension: dict[str, Any], referenced: dict[str, str], source_paths: dict[str, str]
) -> None:
    declared: dict[str, str] = {}
    for index, row in enumerate(extension.get("dependencies") or []):
        if not isinstance(row, dict):
            raise SoundSchemeGlbValidationError(f"dependency {index} is not a record")
        role, asset = str(row.get("role") or ""), str(row.get("asset") or "")
        if role not in DEPENDENCY_ROLES:
            raise SoundSchemeGlbValidationError(f"dependency {index} names an unknown role {role!r}")
        if asset in declared:
            raise SoundSchemeGlbValidationError(f"dependency {asset} is declared twice")
        if not isinstance(row.get("resolved"), bool):
            raise SoundSchemeGlbValidationError(f"dependency {asset} does not state a boolean resolution")
        expected_path = source_paths.get(asset)
        if expected_path is not None and row.get("sourcePath") != expected_path:
            raise SoundSchemeGlbValidationError(
                f"dependency {asset} names {row.get('sourcePath')!r}, not its own {expected_path!r}"
            )
        declared[asset] = role
    if declared != referenced:
        raise SoundSchemeGlbValidationError(
            "the dependency set disagrees with the references scheme actually makes"
        )


def _check_anomalies(extension: dict[str, Any]) -> list[str]:
    roles: list[str] = []
    for row in extension.get("anomalies") or []:
        if not isinstance(row, dict) or row.get("role") not in ANOMALY_ROLES:
            raise SoundSchemeGlbValidationError(f"unknown source anomaly {row!r}")
        roles.append(str(row["role"]))
    return roles


#: The `omissions` role each byte-ledger range owner justifies. `whitespace` is the ordinary
#: separator-byte case; `root.trailing` is a significant token stranded after the root's own
#: closing brace (see `formats.sound_scheme_glb.lexer.parse`).
_OMISSION_OWNER_ROLES = {
    "whitespace": "keyvalues-insignificant-whitespace",
    "root.trailing": "trailing-content-after-root",
}


def _check_comments_and_omissions(extension: dict[str, Any]) -> None:
    """`comments[]` and `omissions[]` are both published and both paid for by the byte ledger, so
    both are checked against it rather than left unexamined."""

    ranges = _ledger_ranges(extension)
    owner_ranges = {str(row.get("owner")): row for row in ranges if isinstance(row, dict)}

    comments = extension.get("comments")
    if not isinstance(comments, list):
        raise SoundSchemeGlbValidationError("the scheme declares no comment table")
    previous_offset = -1
    comment_owners = 0
    for index, row in enumerate(comments):
        if not isinstance(row, dict) or set(row) != {"offset", "text"}:
            raise SoundSchemeGlbValidationError(f"comment {index} is not an offset/text record")
        offset = row["offset"]
        if not isinstance(offset, int) or offset <= previous_offset:
            raise SoundSchemeGlbValidationError(f"comment {index} is not in increasing source order")
        previous_offset = offset
        owner = f"comments[{index}]"
        ledger_range = owner_ranges.get(owner)
        if ledger_range is None or ledger_range.get("offset") != offset:
            raise SoundSchemeGlbValidationError(f"comment {index} owns no matching byte-ledger range")
        comment_owners += 1
    if sum(1 for owner in owner_ranges if owner.startswith("comments[")) != comment_owners:
        raise SoundSchemeGlbValidationError(
            "the byte ledger claims a comment range that owns no comments[] row, or the reverse"
        )

    omitted_reasons = {
        _OMISSION_OWNER_ROLES[owner]
        for owner, row in owner_ranges.items()
        if owner in _OMISSION_OWNER_ROLES and row.get("state") == "omitted-proven"
    }
    omissions = extension.get("omissions")
    if not isinstance(omissions, list):
        raise SoundSchemeGlbValidationError("the scheme declares no omissions table")
    published_reasons = set()
    for index, row in enumerate(omissions):
        if not isinstance(row, dict) or not row.get("role"):
            raise SoundSchemeGlbValidationError(f"omission {index} is not a role record")
        if row["role"] not in _OMISSION_OWNER_ROLES.values():
            raise SoundSchemeGlbValidationError(f"omission {index} names an unknown role {row['role']!r}")
        published_reasons.add(str(row["role"]))
    if published_reasons != omitted_reasons:
        raise SoundSchemeGlbValidationError(
            "omissions[] disagrees with the byte ledger's own omitted-proven ranges"
        )


def validate_document(document: dict, binary: bytes, *, source_members=None) -> dict[str, Any]:
    root = validate_extension_root(
        document, SOUND_SCHEME_EXTENSION, asset_prefix=ASSET_PREFIX, schema_version=SCHEMA_VERSION
    )
    validate_container(document, binary)
    validate_sceneless(document)
    validate_ledgers(root, source_members)
    reject_opaque_source(document, binary, source_members)

    incomplete = completeness(root)
    if incomplete["unresolved"] or incomplete["unsupported"]:
        raise SoundSchemeGlbValidationError("sound-scheme extension is incomplete")

    identity = root.get("identity") or {}
    asset = str(identity.get("asset", ""))
    stem = asset[len(ASSET_PREFIX):]
    source_path = str(identity.get("sourcePath", ""))
    if source_path != f"{SOURCE_ROOT}{stem}{SOURCE_SUFFIX}":
        raise SoundSchemeGlbValidationError("the identity disagrees with its own source path")

    members = (root.get("sourceResolution") or {}).get("members") or []
    if len(members) != 1 or members[0].get("role") != "unit-selecting" or "span" in members[0]:
        raise SoundSchemeGlbValidationError("a sound-scheme unit owns exactly one whole file")
    if members[0].get("path") != source_path:
        raise SoundSchemeGlbValidationError("the source member is not this scheme's own file")

    parameters = _check_parameters(root)
    last_occurrence, random_sound_paths, block_fields = _check_reparse(parameters, source_members)
    referenced, source_paths, census = _check_scheme(
        root, parameters, last_occurrence, random_sound_paths, block_fields
    )
    _check_dependencies(root, referenced, source_paths)
    anomaly_roles = _check_anomalies(root)
    _check_comments_and_omissions(root)

    return {
        "asset": asset,
        "stem": stem,
        "parameters": len(parameters),
        "music": bool(census["music"]),
        "combat": bool(census["combat"]),
        "alert": bool(census["alert"]),
        "ambient": bool(census["ambient"]),
        "randomSounds": census["randomSounds"],
        "dependencies": len(root.get("dependencies") or []),
        "unresolvedDependencies": sorted(
            str(row.get("asset")) for row in root.get("dependencies") or [] if not row.get("resolved")
        ),
        "anomalies": anomaly_roles,
        "sourceBytes": members[0].get("byteLength", 0),
        "byteCoveragePercent": 100.0,
    }


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: dict[str, Any]) -> list[str]:
    """What a published unit could not resolve, phrased for the operator.

    A filename or DSP preset the install lacks warns rather than failing the unit -- the target
    belongs to another seam's data (`seam_map_unit_contract.md`, References) -- so this is the one
    place that gap surfaces to an operator.
    """

    warnings: list[str] = []
    missing = summary.get("unresolvedDependencies") or []
    if missing:
        warnings.append(
            "the install carries no member for "
            + ", ".join(missing[:4])
            + (f" and {len(missing) - 4} more" if len(missing) > 4 else "")
        )
    anomalies = summary.get("anomalies") or []
    if anomalies:
        counted = ", ".join(f"{role}x{count}" for role, count in sorted(Counter(anomalies).items()))
        warnings.append(f"the source departs from the scheme's conventions: {counted}")
    return warnings
