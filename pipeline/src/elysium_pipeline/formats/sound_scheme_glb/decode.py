"""Decode one `sound/schemes/*.txt` file into its complete model plus a gapless byte ledger.

The grammar is a closed vocabulary (`seam_map_sound_scheme.md`): six block names, sixteen keys
between them, nothing else. A key outside that vocabulary is `unsupported`; a block name outside
it carries its pairs into `parameters[]` (so the ledger stays gapless) but contributes nothing to
`scheme`, and is `unsupported` too.
"""

from __future__ import annotations

import math
from typing import Any, Callable

from elysium_pipeline.formats.sound_scheme_glb import lexer
from elysium_pipeline.formats.sound_scheme_glb.coverage import new_ledger
from elysium_pipeline.formats.sound_scheme_glb.model import (
    DSP_PRESET_PATH,
    Parameter,
    SoundSchemeModel,
    dsp_preset_asset_id,
    sound_asset_id,
    sound_dependency_source_path,
)

#: Canonical spelling for a block's folded name, used for the `block` path and for `scheme`'s
#: own keys. A block outside this table keeps its authored spelling.
KNOWN_BLOCKS = {
    "schemeparams": "SchemeParams",
    "music": "Music",
    "combat": "Combat",
    "alert": "Alert",
    "ambient": "Ambient",
    "randomsound": "RandomSound",
}

#: The keys each block kind accepts, folded to lower case.
SCHEME_PARAMS_KEYS = frozenset({"randomsoundcount", "roomdsp"})
#: `seam_map_sound_scheme.md`'s own `Ambient` column is `Filename`, `Volume` alone; three shipped
#: schemes (`la_abandoned_building_1.txt`, `sm_junkyard_1.txt`, `test2.txt`) author `NoPause`
#: there too, and `test2.txt` authors `Dry` there as well, both with the same meaning `Music`,
#: `Combat` and `Alert` give them, so `Ambient` is decoded with the same vocabulary as those three
#: (see `specDeviations`).
MUSIC_LIKE_KEYS = frozenset({"filename", "volume", "dry", "nopause"})
#: `Dry` is not in `seam_map_sound_scheme.md`'s own `RandomSound` key column, but one shipped
#: scheme (`test2.txt`) authors it there with the same routes-to-the-dry-bus meaning `Music`,
#: `Combat` and `Alert` give it; real data would otherwise carry an avoidable `unsupported` row
#: for a key whose meaning is not actually in question (see `specDeviations`).
RANDOM_SOUND_KEYS = frozenset(
    {
        "filename", "pitchmin", "pitchmax", "volume", "frequency", "audibleradius",
        "distmin", "distmax", "heightmin", "heightmax", "anglemin", "anglemax", "dry",
    }
)

#: `Min`/`Max` pairs whose inversion is an anomaly. Angle wraps around 360 degrees, so
#: `AngleMin > AngleMax` is a normal wrap, not an inverted range.
RANGE_PAIRS = (("pitchmin", "pitchmax", "pitch"), ("distmin", "distmax", "distance"),
               ("heightmin", "heightmax", "height"))


class SoundSchemeDecodeError(RuntimeError):
    """The selected file cannot be read as a sound scheme."""


def _collect_dependencies(
    *,
    music: dict[str, Any] | None,
    combat: dict[str, Any] | None,
    alert: dict[str, Any] | None,
    ambient: dict[str, Any] | None,
    random_sounds: list[dict[str, Any]],
    params: dict[str, Any] | None,
    sound_exists: Callable[[str], bool] | None,
) -> list[dict[str, Any]]:
    """One dependency row per reference the *published* `scheme` actually makes.

    Built from the final `scheme` rather than while walking blocks, so a `Filename` a
    `repeated-block` discards never outlives it as a dependency the unit no longer publishes --
    `dependencies` and `scheme` would otherwise disagree, which `validate_document` refuses.
    """

    dependencies: list[dict[str, Any]] = []
    seen: set[str] = set()

    def add(role: str, asset: str, source_path: str, resolved: bool) -> None:
        if asset in seen:
            return
        seen.add(asset)
        dependencies.append(
            {"role": role, "asset": asset, "sourcePath": source_path, "resolved": resolved}
        )

    for record in (music, combat, alert, ambient, *random_sounds):
        if record and record.get("file"):
            resolved = bool(sound_exists(record["file"])) if sound_exists is not None else False
            add(
                "sound", record["asset"], sound_dependency_source_path(record["file"]), resolved
            )
    room_dsp = (params or {}).get("roomDsp")
    if room_dsp:
        add(
            "dsp-preset", room_dsp["asset"], f"{DSP_PRESET_PATH}#{room_dsp['id']}",
            room_dsp["resolved"],
        )
    return dependencies


def _number(text: str) -> float | None:
    """A finite number, or `None` -- `"nan"`/`"inf"` parse under bare `float()` but the container's
    JSON chunk rejects them (`seam_map_unit_contract.md`, Container), so a non-finite result is
    graded `value-is-not-a-number` here rather than surfacing as a write-time crash."""

    try:
        value = float(text.strip().strip('"'))
    except ValueError:
        return None
    return value if math.isfinite(value) else None


def _claim(ledger, token: lexer.Token, owner: str) -> None:
    ledger.claim(token.offset, token.length, "mapped", owner)


class _BlockCursor:
    """Assigns each block occurrence its `Name[index]` path and its ledger index."""

    def __init__(self) -> None:
        self._seen: dict[str, int] = {}

    def path_for(self, folded: str, authored: str) -> str:
        canonical = KNOWN_BLOCKS.get(folded, authored.strip())
        occurrence = self._seen.get(folded, 0)
        self._seen[folded] = occurrence + 1
        return f"{canonical}[{occurrence}]"


def decode_sound_scheme(
    closure,
    *,
    sound_exists: Callable[[str], bool] | None = None,
    dsp_preset_exists: Callable[[str], bool] | None = None,
) -> SoundSchemeModel:
    """The complete sound-scheme unit for one source closure.

    `sound_exists` and `dsp_preset_exists` answer whether a named target is present in the
    install. Without them a reference publishes unresolved rather than asserting a target this
    decode never looked for.
    """

    member = closure.member
    text = lexer.decode_text(member.data)
    try:
        tokens = lexer.tokenize(text)
        root = lexer.parse(tokens)
    except lexer.SoundSchemeLexError as error:
        raise SoundSchemeDecodeError(f"{member.path}: {error}") from error

    ledger = new_ledger(member)
    comments: list[dict[str, Any]] = []
    for token in tokens:
        if token.kind == "whitespace":
            ledger.claim(token.offset, token.length, "omitted-proven", "whitespace")
        elif token.kind == "comment":
            ledger.claim(token.offset, token.length, "mapped", f"comments[{len(comments)}]")
            comments.append({"offset": token.offset, "text": token.text})
        elif token.kind == "bom":
            ledger.claim(token.offset, token.length, "mapped", "root.byte-order-mark")

    _claim(ledger, root.name, "root.name")
    _claim(ledger, root.open_token, "root.braces")
    if root.close_token is not None:
        _claim(ledger, root.close_token, "root.braces")
    # A significant token after the root's own closing brace names nothing this grammar has a
    # field for; its bytes are still claimed (as `omitted-proven`, with a matching `omissions` row
    # below) and the anomaly the parser recorded for it survives into `anomalies` via `root.anomalies`.
    for token in root.trailing:
        ledger.claim(token.offset, token.length, "omitted-proven", "root.trailing")

    anomalies: list[dict[str, Any]] = list(root.anomalies)
    # This seam's only unresolvable references (`Filename`/`RoomDSP`) are other seams' data, which
    # `dependencies[].resolved` states without failing the unit -- never a coverage row -- so this
    # stays the empty list `coverage_block` always publishes for a kind with no `unresolved` rule.
    unresolved: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    parameters: list[Parameter] = []

    params: dict[str, Any] | None = None
    music: dict[str, Any] | None = None
    combat: dict[str, Any] | None = None
    alert: dict[str, Any] | None = None
    ambient: dict[str, Any] | None = None
    random_sounds: list[dict[str, Any]] = []

    cursor = _BlockCursor()

    for block_index, block in enumerate(root.blocks):
        anomalies.extend(block.anomalies)
        folded = block.name.text.strip().lower()
        block_path = cursor.path_for(folded, block.name.text)

        _claim(ledger, block.name, f"blocks[{block_index}].name")
        _claim(ledger, block.open_token, f"blocks[{block_index}].braces")
        if block.close_token is not None:
            _claim(ledger, block.close_token, f"blocks[{block_index}].braces")

        block_parameters: list[Parameter] = []
        for pair in block.pairs:
            slot = len(parameters) + len(block_parameters)
            _claim(ledger, pair.key, f"blocks[{block_index}].parameters[{len(block_parameters)}]")
            if pair.value is not None:
                _claim(
                    ledger, pair.value,
                    f"blocks[{block_index}].parameters[{len(block_parameters)}]",
                )
            block_parameters.append(
                Parameter(
                    index=slot,
                    block=block_path,
                    key=pair.key.text.strip().lower(),
                    source_key=pair.key.text,
                    value="" if pair.value is None else pair.value.text,
                    quoted_key=pair.key.quoted,
                    quoted_value=bool(pair.value is not None and pair.value.quoted),
                    offset=pair.key.offset,
                )
            )
        parameters.extend(block_parameters)

        if folded not in KNOWN_BLOCKS:
            unsupported.append(
                {
                    "block": block_path,
                    "offset": block.offset,
                    "reason": "block-outside-the-scheme-vocabulary",
                }
            )
            continue

        seen_keys: set[str] = set()

        def scalar_field(target: dict[str, Any], field_name: str, parameter: Parameter) -> None:
            if field_name in seen_keys:
                anomalies.append(
                    {"role": "repeated-scalar-key", "offset": parameter.offset, "key": parameter.key}
                )
            seen_keys.add(field_name)
            target[field_name] = parameter

        if folded == "schemeparams":
            if params is not None:
                anomalies.append({"role": "repeated-block", "offset": block.offset, "block": "SchemeParams"})
            fresh: dict[str, Parameter] = {}
            for parameter in block_parameters:
                if parameter.key not in SCHEME_PARAMS_KEYS:
                    unsupported.append(
                        {"key": parameter.key, "block": block_path, "offset": parameter.offset,
                         "reason": "key-outside-the-block-vocabulary"}
                    )
                    continue
                scalar_field(fresh, parameter.key, parameter)
            random_sound_count = None
            room_dsp = None
            if "randomsoundcount" in fresh and fresh["randomsoundcount"].value.strip():
                parameter = fresh["randomsoundcount"]
                number = _number(parameter.value)
                if number is None:
                    unsupported.append(
                        {"key": parameter.key, "offset": parameter.offset,
                         "reason": "value-is-not-a-number"}
                    )
                else:
                    random_sound_count = {
                        "value": number, "raw": parameter.value, "parameter": parameter.index,
                    }
            if "roomdsp" in fresh and fresh["roomdsp"].value.strip().strip('"'):
                parameter = fresh["roomdsp"]
                preset_id = parameter.value.strip().strip('"')
                asset = dsp_preset_asset_id(preset_id)
                resolved = bool(dsp_preset_exists(preset_id)) if dsp_preset_exists is not None else False
                room_dsp = {
                    "id": preset_id, "asset": asset, "resolved": resolved,
                    "parameter": parameter.index,
                }
            params = {"randomSoundCount": random_sound_count, "roomDsp": room_dsp}
            continue

        if folded in ("music", "combat", "alert", "ambient"):
            existing = {"music": music, "combat": combat, "alert": alert, "ambient": ambient}[folded]
            if existing is not None:
                anomalies.append(
                    {"role": "repeated-block", "offset": block.offset,
                     "block": KNOWN_BLOCKS[folded]}
                )
            fresh = {}
            for parameter in block_parameters:
                if parameter.key not in MUSIC_LIKE_KEYS:
                    unsupported.append(
                        {"key": parameter.key, "block": block_path, "offset": parameter.offset,
                         "reason": "key-outside-the-block-vocabulary"}
                    )
                    continue
                scalar_field(fresh, parameter.key, parameter)

            record: dict[str, Any] = {"file": None, "asset": None, "parameter": None}
            if "filename" in fresh:
                parameter = fresh["filename"]
                raw = parameter.value.strip()
                if raw:
                    asset = sound_asset_id(raw)
                    record["file"] = raw
                    record["asset"] = asset
                    record["parameter"] = parameter.index
                    # `unresolved-file` is raised once below, from the *final* Music/Combat/Alert/
                    # Ambient record only -- not here, per occurrence -- so a `repeated-block`
                    # overwrite's discarded `Filename` (which owns no `dependencies` row; see
                    # `_collect_dependencies`) never raises an anomaly for a reference the
                    # published `scheme` no longer makes (see `specDeviations`).
            for key in ("volume", "dry", "nopause"):
                out_key = "noPause" if key == "nopause" else key
                record[out_key] = None
                if key not in fresh or not fresh[key].value.strip():
                    continue
                parameter = fresh[key]
                number = _number(parameter.value)
                if number is None:
                    unsupported.append(
                        {"key": parameter.key, "offset": parameter.offset,
                         "reason": "value-is-not-a-number"}
                    )
                    continue
                if key == "volume" and not 0 <= number <= 100:
                    anomalies.append(
                        {"role": "volume-out-of-range", "offset": parameter.offset, "value": number,
                         "block": block_path}
                    )
                value: Any = bool(number) if key in ("dry", "nopause") else number
                record[out_key] = {"value": value, "raw": parameter.value, "parameter": parameter.index}
            if folded == "music":
                music = record
            elif folded == "combat":
                combat = record
            elif folded == "alert":
                alert = record
            else:
                ambient = record
            continue

        # folded == "randomsound"
        fresh = {}
        for parameter in block_parameters:
            if parameter.key not in RANDOM_SOUND_KEYS:
                unsupported.append(
                    {"key": parameter.key, "block": block_path, "offset": parameter.offset,
                     "reason": "key-outside-the-block-vocabulary"}
                )
                continue
            scalar_field(fresh, parameter.key, parameter)

        sound_record: dict[str, Any] = {"file": None, "asset": None, "parameter": None}
        if "filename" in fresh:
            parameter = fresh["filename"]
            raw = parameter.value.strip()
            if raw:
                asset = sound_asset_id(raw)
                sound_record["file"] = raw
                sound_record["asset"] = asset
                sound_record["parameter"] = parameter.index
                resolved = bool(sound_exists(raw)) if sound_exists is not None else False
                if sound_exists is not None and not resolved:
                    anomalies.append(
                        {"role": "unresolved-file", "offset": parameter.offset, "path": raw}
                    )

        def numeric(key: str) -> dict[str, Any] | None:
            if key not in fresh or not fresh[key].value.strip():
                return None
            parameter = fresh[key]
            number = _number(parameter.value)
            if number is None:
                unsupported.append(
                    {"key": parameter.key, "offset": parameter.offset, "reason": "value-is-not-a-number"}
                )
                return None
            return {"value": number, "raw": parameter.value, "parameter": parameter.index}

        sound_record["volume"] = numeric("volume")
        if sound_record["volume"] is not None and not 0 <= sound_record["volume"]["value"] <= 100:
            anomalies.append(
                {"role": "volume-out-of-range", "offset": fresh["volume"].offset,
                 "value": sound_record["volume"]["value"], "block": block_path}
            )
        sound_record["frequency"] = numeric("frequency")
        sound_record["audibleRadius"] = numeric("audibleradius")
        dry_parameter = numeric("dry")
        sound_record["dry"] = (
            None if dry_parameter is None
            else {**dry_parameter, "value": bool(dry_parameter["value"])}
        )
        sound_record["pitch"] = {"min": numeric("pitchmin"), "max": numeric("pitchmax")}
        sound_record["distance"] = {"min": numeric("distmin"), "max": numeric("distmax")}
        sound_record["height"] = {"min": numeric("heightmin"), "max": numeric("heightmax")}
        sound_record["angle"] = {"min": numeric("anglemin"), "max": numeric("anglemax")}

        for low_key, high_key, label in RANGE_PAIRS:
            low, high = sound_record[label]["min"], sound_record[label]["max"]
            if low is not None and high is not None and low["value"] > high["value"]:
                anomalies.append(
                    {"role": "range-inverted", "offset": fresh[low_key].offset, "pair": label,
                     "block": block_path}
                )
        random_sounds.append(sound_record)

    # One `unresolved-file` anomaly per reference the *published* scheme still makes -- the same
    # final-state rule `_collect_dependencies` uses -- so a `repeated-block` overwrite's discarded
    # `Filename` never raises an anomaly `dependencies` carries no row for (see `specDeviations`).
    if sound_exists is not None:
        for record in (music, combat, alert, ambient):
            if record and record.get("file") and not sound_exists(record["file"]):
                anomalies.append(
                    {
                        "role": "unresolved-file",
                        "offset": parameters[record["parameter"]].offset,
                        "path": record["file"],
                    }
                )

    ledger_row = ledger.finish()
    omissions: list[dict[str, Any]] = []
    omission_owners = {
        str(row["owner"]) for row in ledger_row["ranges"] if row["state"] == "omitted-proven"
    }
    if "whitespace" in omission_owners:
        omissions.append(
            {"role": "keyvalues-insignificant-whitespace",
             "reason": "separator-bytes-carry-no-keyvalues-meaning"}
        )
    if "root.trailing" in omission_owners:
        omissions.append(
            {"role": "trailing-content-after-root",
             "reason": "content-outside-the-root-block-has-no-keyvalues-meaning"}
        )

    dependencies = _collect_dependencies(
        music=music, combat=combat, alert=alert, ambient=ambient, random_sounds=random_sounds,
        params=params, sound_exists=sound_exists,
    )

    return SoundSchemeModel(
        stem=closure.stem,
        asset_id=closure.asset,
        member=member,
        parameters=parameters,
        params=params,
        music=music,
        combat=combat,
        alert=alert,
        ambient=ambient,
        random_sounds=random_sounds,
        dependencies=dependencies,
        comments=comments,
        anomalies=anomalies,
        omissions=omissions,
        unresolved=unresolved,
        unsupported=unsupported,
        ledger_row=ledger_row,
    )
