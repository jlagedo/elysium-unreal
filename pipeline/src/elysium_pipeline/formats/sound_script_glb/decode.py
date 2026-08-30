"""Decode one sound-script entry, of any of the five kinds, into its model and byte ledger."""

from __future__ import annotations

import re
from typing import Any, Callable

from elysium_pipeline.formats.sound_script_glb import dsp_tree, kv_tree, lexer, symbols
from elysium_pipeline.formats.sound_script_glb.model import (
    MANIFEST_KEY,
    TABLE_PATHS,
    Parameter,
    SoundScriptModel,
    dsp_preset_asset_id,
    manifest_asset_id,
    sentence_asset_id,
    sound_asset_id,
    sound_script_table_asset_id,
)
from elysium_pipeline.formats.unit_contract import ByteLedger

#: `*` stream, `#` music, `^` distance-variant, `@` player, `>` doppler, `<` direction,
#: `)` spatial, `(` no-spatial, `!` sentence, `?` voice.
WAVE_PREFIXES: dict[str, str] = {
    "*": "stream", "#": "music", "^": "distance-variant", "@": "player", ">": "doppler",
    "<": "direction", ")": "spatial", "(": "no-spatial", "!": "sentence", "?": "voice",
}

GAME_SOUND_KEYS = {"channel", "volume", "pitch", "soundlevel", "wave", "rndwave"}
SOUNDSCAPE_TOP_KEYS = {"dsp", "playlooping", "playrandom", "playsoundscape"}
#: `attenuation` is not in `seam_map_sound_script.md`'s soundscape vocabulary line, but the
#: shipped `cabin` and `cabin_outdoor` entries use it; see `specDeviations`.
SOUNDSCAPE_BLOCK_KEYS = {
    "volume", "pitch", "time", "wave", "rndwave", "position", "soundlevel", "name", "attenuation",
}
SENTENCE_MAX_NAME_LENGTH = 23


class SoundScriptDecodeError(RuntimeError):
    """The selected entry cannot be read as its declared kind."""


def _claim(ledger: ByteLedger, token: lexer.Token, owner: str, state: str = "mapped") -> None:
    ledger.claim(token.offset, token.length, state, owner)


def _claim_lexical(ledger: ByteLedger, tokens: list[lexer.Token], covered, prefix: str) -> list[dict[str, Any]]:
    """Claim every comment/whitespace/BOM token outside `covered` ranges; return the comments."""

    comments: list[dict[str, Any]] = []
    for token in tokens:
        if any(start <= token.offset < end for start, end in covered):
            continue
        if token.kind == "whitespace":
            ledger.claim(token.offset, token.length, "omitted-proven", f"{prefix}.insignificant-whitespace")
        elif token.kind == "comment":
            ledger.claim(token.offset, token.length, "mapped", f"{prefix}.comment[{len(comments)}]")
            comments.append({"offset": token.offset, "text": token.text})
        elif token.kind == "bom":
            ledger.claim(token.offset, token.length, "mapped", f"{prefix}.byte-order-mark")
    return comments


def _omission_if_claimed(ledger_result: dict[str, Any], role: str, reason: str) -> list[dict[str, Any]]:
    """One `omissions` row, published only when the finished ledger actually claimed
    `omitted-proven` bytes -- so a unit whose grammar left nothing insignificant to omit does not
    publish an omission it never made."""

    if int(ledger_result.get("stateBytes", {}).get("omitted-proven", 0)) > 0:
        return [{"role": role, "reason": reason}]
    return []


def _number(text: str) -> float | None:
    try:
        return float(text.strip())
    except ValueError:
        return None


def _scalar_or_range(text: str) -> dict[str, Any] | None:
    parts = [part.strip() for part in text.split(",")]
    if len(parts) == 2:
        low, high = _number(parts[0]), _number(parts[1])
        if low is None or high is None:
            return None
        return {"min": low, "max": high}
    if len(parts) == 1:
        value = _number(parts[0])
        return None if value is None else {"value": value}
    return None


def _resolve_channel(raw: str, parameter_index: int) -> dict[str, Any] | None:
    key = symbols.resolve_symbol_key(raw, symbols.CHANNELS)
    if key is not None:
        return {"sourceToken": raw, "symbol": key, "resolved": symbols.CHANNELS[key], "parameter": parameter_index}
    number = _number(raw)
    if number is not None:
        return {"sourceToken": raw, "symbol": None, "resolved": int(number), "parameter": parameter_index}
    return None


def _resolve_sound_level(raw: str, parameter_index: int) -> dict[str, Any] | None:
    key = symbols.resolve_symbol_key(raw, symbols.SOUND_LEVELS)
    if key is not None:
        return {
            "sourceToken": raw, "symbol": key, "resolved": symbols.SOUND_LEVELS[key],
            "parameter": parameter_index,
        }
    number = _number(raw)
    if number is not None:
        return {"sourceToken": raw, "symbol": None, "resolved": number, "parameter": parameter_index}
    return None


def _resolve_pitch(raw: str, parameter_index: int) -> dict[str, Any] | None:
    key = symbols.resolve_symbol_key(raw, symbols.PITCHES)
    if key is not None:
        return {
            "sourceToken": raw, "symbol": key, "resolved": {"value": float(symbols.PITCHES[key])},
            "parameter": parameter_index,
        }
    scalar = _scalar_or_range(raw)
    if scalar is not None:
        return {"sourceToken": raw, "symbol": None, "resolved": scalar, "parameter": parameter_index}
    return None


def _resolve_volume(raw: str, parameter_index: int) -> dict[str, Any] | None:
    key = symbols.resolve_symbol_key(raw, symbols.VOLUME_SYMBOLS)
    if key is not None:
        return {
            "sourceToken": raw, "symbol": key, "resolved": {"value": float(symbols.VOLUME_SYMBOLS[key])},
            "parameter": parameter_index,
        }
    scalar = _scalar_or_range(raw)
    if scalar is not None:
        return {"sourceToken": raw, "symbol": None, "resolved": scalar, "parameter": parameter_index}
    return None


def strip_wave_prefix(raw: str) -> tuple[str, str | None]:
    if raw and raw[0] in WAVE_PREFIXES:
        return raw[1:], WAVE_PREFIXES[raw[0]]
    return raw, None


def _claim_block_tree(
    ledger: ByteLedger, block: kv_tree.Block, owner_prefix: str, covered: list[tuple[int, int]]
) -> None:
    """Claim every byte of `block` -- name, braces, and every item however deep -- as `mapped`.

    Used for a block whose name is outside the kind's vocabulary: the record has nothing typed
    to say about it, but the ledger still owes every one of its bytes an owner.
    """

    def token(t: lexer.Token, suffix: str) -> None:
        ledger.claim(t.offset, t.length, "mapped", f"{owner_prefix}.{suffix}")
        covered.append((t.offset, t.end))

    token(block.name, "name")
    token(block.open_token, "open")
    if block.close_token is not None:
        token(block.close_token, "close")
    for index, (kind, item) in enumerate(block.items):
        if kind == "pair":
            token(item.key, f"parameter[{index}].key")
            if item.value is not None:
                token(item.value, f"parameter[{index}].value")
        else:
            _claim_block_tree(ledger, item, f"{owner_prefix}.block[{index}]", covered)


# --- game sound -----------------------------------------------------------------------------


def decode_game_sound(
    closure,
    *,
    sentence_exists: Callable[[str], bool] | None = None,
    sound_exists: Callable[[str], bool] | None = None,
) -> SoundScriptModel:
    member = closure.entry
    text = lexer.decode_text(member.data)
    try:
        tokens = lexer.tokenize(text)
        blocks = kv_tree.parse(tokens)
    except lexer.SoundScriptLexError as error:
        raise SoundScriptDecodeError(f"{member.path}: {error}") from error
    if len(blocks) != 1:
        raise SoundScriptDecodeError(f"{member.path}: the unit's bytes declare {len(blocks)} entries, not one")
    block = blocks[0]

    ledger = ByteLedger(member.path, member.data, span_offset=member.span_offset)
    covered: list[tuple[int, int]] = []
    _claim(ledger, block.name, "entry.name")
    _claim(ledger, block.open_token, "entry.open")
    if block.close_token is not None:
        _claim(ledger, block.close_token, "entry.close")
    covered.append((block.name.offset, block.name.end))
    covered.append((block.open_token.offset, block.open_token.end))
    if block.close_token is not None:
        covered.append((block.close_token.offset, block.close_token.end))

    parameters: list[Parameter] = []
    anomalies: list[dict[str, Any]] = list(block.anomalies)
    #: A live entry that shares its folded name with a `sounds.txt` entry takes the identity;
    #: the dormant duplicate is real corpus data this unit drops, so its span is recorded here
    #: as evidence rather than left to vanish -- see `specDeviations`.
    shadowed_dormant = getattr(closure, "shadowed_dormant", None)
    if shadowed_dormant:
        anomalies.append({"role": "shadowed-dormant-entry", **shadowed_dormant})
    unresolved: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    dependencies: list[dict[str, Any]] = []
    seen_assets: set[str] = set()

    def depend(role: str, asset: str, source_path: str, resolved: bool = True) -> None:
        if asset in seen_assets:
            return
        seen_assets.add(asset)
        dependencies.append({"role": role, "asset": asset, "sourcePath": source_path, "resolved": resolved})

    def add_parameter(pair: kv_tree.Pair) -> Parameter:
        slot = len(parameters)
        _claim(ledger, pair.key, f"entry.parameter[{slot}].key")
        if pair.value is not None:
            _claim(ledger, pair.value, f"entry.parameter[{slot}].value")
            covered.append((pair.value.offset, pair.value.end))
        covered.append((pair.key.offset, pair.key.end))
        parameter = Parameter(
            index=slot,
            key=pair.key.text.strip().lower(),
            source_key=pair.key.text,
            value="" if pair.value is None else pair.value.text,
            quoted_key=pair.key.quoted,
            quoted_value=bool(pair.value is not None and pair.value.quoted),
            offset=pair.key.offset,
        )
        parameters.append(parameter)
        return parameter

    channel = volume = pitch = sound_level = None
    waves: list[dict[str, Any]] = []

    def add_wave(parameter: Parameter, origin: str) -> None:
        raw = parameter.value
        path, prefix = strip_wave_prefix(raw)
        if prefix == "sentence":
            asset = sentence_asset_id(path)
            record = {
                "path": path, "prefix": prefix, "origin": origin, "asset": asset,
                "parameter": parameter.index,
            }
            waves.append(record)
            exists = sentence_exists(path) if sentence_exists is not None else True
            depend("sentence", asset, path, exists)
            if sentence_exists is not None and not exists:
                unresolved.append({"path": path, "reason": "wave-names-no-defined-sentence"})
            return
        asset = sound_asset_id(path)
        record = {"path": path, "prefix": prefix, "origin": origin, "asset": asset, "parameter": parameter.index}
        waves.append(record)
        #: `sound` names a member of the sibling `sound_glb` seam's corpus; a miss there is an
        #: absence in another seam's asset space, not a defect in this entry, so it is carried as
        #: `resolved: false` and warned about rather than failing the unit. See `specDeviations`.
        exists = sound_exists(path) if sound_exists is not None else True
        depend("sound", asset, "sound/" + path, exists)

    for kind, item in block.items:
        if kind == "pair":
            parameter = add_parameter(item)
            key, raw = parameter.key, parameter.value.strip()
            if key == "channel":
                resolved = _resolve_channel(raw, parameter.index)
                if resolved is None:
                    unsupported.append({"key": key, "offset": parameter.offset, "reason": "value-is-not-recognized"})
                elif channel is not None:
                    anomalies.append({"role": "repeated-scalar-key", "offset": parameter.offset, "key": key})
                else:
                    channel = resolved
            elif key == "volume":
                resolved = _resolve_volume(raw, parameter.index)
                if resolved is None:
                    unsupported.append({"key": key, "offset": parameter.offset, "reason": "value-is-not-recognized"})
                elif volume is not None:
                    anomalies.append({"role": "repeated-scalar-key", "offset": parameter.offset, "key": key})
                else:
                    volume = resolved
            elif key == "pitch":
                resolved = _resolve_pitch(raw, parameter.index)
                if resolved is None:
                    unsupported.append({"key": key, "offset": parameter.offset, "reason": "value-is-not-recognized"})
                elif pitch is not None:
                    anomalies.append({"role": "repeated-scalar-key", "offset": parameter.offset, "key": key})
                else:
                    pitch = resolved
            elif key == "soundlevel":
                resolved = _resolve_sound_level(raw, parameter.index)
                if resolved is None:
                    unsupported.append({"key": key, "offset": parameter.offset, "reason": "value-is-not-recognized"})
                elif sound_level is not None:
                    anomalies.append({"role": "repeated-scalar-key", "offset": parameter.offset, "key": key})
                else:
                    sound_level = resolved
            elif key == "wave":
                add_wave(parameter, "direct")
            else:
                unsupported.append({"key": key, "offset": parameter.offset, "reason": "key-outside-the-game-sound-vocabulary"})
        else:  # nested block
            nested: kv_tree.Block = item
            nested_key = nested.name.text.strip().lower()
            if nested_key != "rndwave":
                unsupported.append({"key": nested_key, "offset": nested.offset, "reason": "key-outside-the-game-sound-vocabulary"})
                _claim_block_tree(ledger, nested, "entry.block[unknown]", covered)
                continue
            _claim(ledger, nested.name, "entry.block[rndwave].name")
            _claim(ledger, nested.open_token, "entry.block[rndwave].open")
            if nested.close_token is not None:
                _claim(ledger, nested.close_token, "entry.block[rndwave].close")
            covered.append((nested.name.offset, nested.name.end))
            covered.append((nested.open_token.offset, nested.open_token.end))
            if nested.close_token is not None:
                covered.append((nested.close_token.offset, nested.close_token.end))
            for inner_kind, inner_item in nested.items:
                if inner_kind != "pair":
                    _claim_block_tree(ledger, inner_item, "entry.block[rndwave].block[unknown]", covered)
                    continue
                parameter = add_parameter(inner_item)
                if parameter.key == "wave":
                    add_wave(parameter, "rndwave")
                else:
                    unsupported.append({"key": parameter.key, "offset": parameter.offset, "reason": "key-outside-the-rndwave-vocabulary"})

    comments = _claim_lexical(ledger, tokens, covered, "entry")
    ledger_result = ledger.finish()
    omissions = _omission_if_claimed(
        ledger_result, "keyvalues-insignificant-whitespace", "separator-bytes-carry-no-keyvalues-meaning"
    )

    record = {
        "channel": channel, "volume": volume, "pitch": pitch, "soundLevel": sound_level, "waves": waves,
    }
    return SoundScriptModel(
        kind="game-sound",
        name=closure.name,
        source_name=closure.source_name,
        asset_id=closure.asset_id,
        dormant=closure.dormant,
        dormant_evidence=(
            "sounds.txt is never precached by game_sounds_manifest.txt, and no map spawns "
            "env_soundscape or references this table"
        ) if closure.dormant else None,
        sources=[member],
        parameters=parameters,
        record=record,
        dependencies=dependencies,
        comments=comments,
        anomalies=anomalies,
        omissions=omissions,
        unresolved=unresolved,
        unsupported=unsupported,
        byte_coverage=[ledger_result],
    )


# --- manifest ---------------------------------------------------------------------------------


def decode_manifest(
    closure,
    *,
    table_exists: Callable[[str], bool] | None = None,
) -> SoundScriptModel:
    member = closure.entry
    text = lexer.decode_text(member.data)
    try:
        tokens = lexer.tokenize(text)
        blocks = kv_tree.parse(tokens)
    except lexer.SoundScriptLexError as error:
        raise SoundScriptDecodeError(f"{member.path}: {error}") from error
    if len(blocks) != 1 or blocks[0].name.text.strip().lower() != MANIFEST_KEY:
        raise SoundScriptDecodeError(f"{member.path}: expected one {MANIFEST_KEY!r} block")
    block = blocks[0]

    ledger = ByteLedger(member.path, member.data)
    covered: list[tuple[int, int]] = []
    _claim(ledger, block.name, "entry.name")
    _claim(ledger, block.open_token, "entry.open")
    if block.close_token is not None:
        _claim(ledger, block.close_token, "entry.close")
    covered += [(block.name.offset, block.name.end), (block.open_token.offset, block.open_token.end)]
    if block.close_token is not None:
        covered.append((block.close_token.offset, block.close_token.end))

    parameters: list[Parameter] = []
    dependencies: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    precache_files: list[dict[str, Any]] = []

    for kind, item in block.items:
        if kind != "pair":
            continue
        slot = len(parameters)
        _claim(ledger, item.key, f"entry.parameter[{slot}].key")
        if item.value is not None:
            _claim(ledger, item.value, f"entry.parameter[{slot}].value")
            covered.append((item.value.offset, item.value.end))
        covered.append((item.key.offset, item.key.end))
        key = item.key.text.strip().lower()
        parameter = Parameter(
            index=slot, key=key, source_key=item.key.text,
            value="" if item.value is None else item.value.text,
            quoted_key=item.key.quoted, quoted_value=bool(item.value is not None and item.value.quoted),
            offset=item.key.offset,
        )
        parameters.append(parameter)
        if key == "precache_file":
            path = parameter.value.strip()
            asset = sound_script_table_asset_id(path)
            #: The precached table is another of this seam's own tables; a miss is an absence in
            #: the install, not a defect in the entry, so it is carried as `resolved: false` and
            #: warned about rather than failing the unit.
            exists = table_exists(path) if table_exists is not None else True
            record = {"path": path, "asset": asset, "parameter": slot}
            precache_files.append(record)
            dependencies.append({"role": "sound-script-table", "asset": asset, "sourcePath": path, "resolved": exists})
        else:
            unsupported.append({"key": key, "offset": parameter.offset, "reason": "key-outside-the-manifest-vocabulary"})

    comments = _claim_lexical(ledger, tokens, covered, "entry")
    ledger_result = ledger.finish()
    omissions = _omission_if_claimed(
        ledger_result, "keyvalues-insignificant-whitespace", "separator-bytes-carry-no-keyvalues-meaning"
    )
    return SoundScriptModel(
        kind="manifest", name=MANIFEST_KEY, source_name=block.name.text, asset_id=closure.asset_id,
        dormant=False, dormant_evidence=None, sources=[member], parameters=parameters,
        record={"precacheFiles": precache_files}, dependencies=dependencies, comments=comments,
        anomalies=list(block.anomalies), omissions=omissions, unresolved=[], unsupported=unsupported,
        byte_coverage=[ledger_result],
    )


# --- soundscape -------------------------------------------------------------------------------


def decode_soundscape(
    closure,
    *,
    dsp_exists: Callable[[int], bool] | None = None,
    sentence_exists: Callable[[str], bool] | None = None,
    sound_exists: Callable[[str], bool] | None = None,
) -> SoundScriptModel:
    member = closure.entry
    text = lexer.decode_text(member.data)
    try:
        tokens = lexer.tokenize(text)
        blocks = kv_tree.parse(tokens)
    except lexer.SoundScriptLexError as error:
        raise SoundScriptDecodeError(f"{member.path}: {error}") from error
    if len(blocks) != 1:
        raise SoundScriptDecodeError(f"{member.path}: the unit's bytes declare {len(blocks)} entries, not one")
    block = blocks[0]

    ledger = ByteLedger(member.path, member.data, span_offset=member.span_offset)
    covered: list[tuple[int, int]] = []

    def claim_token(token: lexer.Token, owner: str) -> None:
        _claim(ledger, token, owner)
        covered.append((token.offset, token.end))

    claim_token(block.name, "entry.name")
    claim_token(block.open_token, "entry.open")
    if block.close_token is not None:
        claim_token(block.close_token, "entry.close")

    parameters: list[Parameter] = []
    anomalies: list[dict[str, Any]] = list(block.anomalies)
    unresolved: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    dependencies: list[dict[str, Any]] = []
    seen_assets: set[str] = set()

    def depend(role: str, asset: str, source_path: str, resolved: bool) -> None:
        if asset in seen_assets:
            return
        seen_assets.add(asset)
        dependencies.append({"role": role, "asset": asset, "sourcePath": source_path, "resolved": resolved})

    def add_parameter(pair: kv_tree.Pair) -> Parameter:
        slot = len(parameters)
        claim_token(pair.key, f"entry.parameter[{slot}].key")
        if pair.value is not None:
            claim_token(pair.value, f"entry.parameter[{slot}].value")
        parameter = Parameter(
            index=slot, key=pair.key.text.strip().lower(), source_key=pair.key.text,
            value="" if pair.value is None else pair.value.text,
            quoted_key=pair.key.quoted, quoted_value=bool(pair.value is not None and pair.value.quoted),
            offset=pair.key.offset,
        )
        parameters.append(parameter)
        return parameter

    dsp_dependency: dict[str, Any] | None = None
    play_blocks: list[dict[str, Any]] = []

    def decode_pool_block(sub: kv_tree.Block, sub_index: int) -> dict[str, Any]:
        sub_key = sub.name.text.strip().lower()
        claim_token(sub.name, f"entry.block[{sub_key}#{sub_index}].name")
        claim_token(sub.open_token, f"entry.block[{sub_key}#{sub_index}].open")
        if sub.close_token is not None:
            claim_token(sub.close_token, f"entry.block[{sub_key}#{sub_index}].close")
        fields: dict[str, Any] = {}
        waves: list[dict[str, Any]] = []

        def add_wave(parameter: Parameter, origin: str) -> None:
            raw = parameter.value
            path, prefix = strip_wave_prefix(raw)
            role = "sentence" if prefix == "sentence" else "sound"
            asset = sentence_asset_id(path) if role == "sentence" else sound_asset_id(path)
            record = {"path": path, "prefix": prefix, "origin": origin, "asset": asset, "parameter": parameter.index}
            waves.append(record)
            if role == "sentence":
                exists = sentence_exists(path) if sentence_exists is not None else True
                depend(role, asset, path, exists)
                if sentence_exists is not None and not exists:
                    unresolved.append({"path": path, "reason": "wave-names-no-defined-sentence"})
                return
            #: `sound` names a member of the sibling `sound_glb` seam's corpus; a miss there is an
            #: absence in another seam's asset space, not a defect in this entry, so it is carried
            #: as `resolved: false` and warned about rather than failing the unit.
            exists = sound_exists(path) if sound_exists is not None else True
            depend(role, asset, "sound/" + path, exists)

        for inner_kind, inner_item in sub.items:
            if inner_kind == "pair":
                parameter = add_parameter(inner_item)
                key, raw = parameter.key, parameter.value.strip()
                if key == "wave":
                    add_wave(parameter, "direct")
                elif key in SOUNDSCAPE_BLOCK_KEYS:
                    if key in ("volume", "pitch", "time"):
                        resolved = _scalar_or_range(raw)
                        fields[key] = {"value": resolved if resolved is not None else raw, "parameter": parameter.index}
                    elif key == "soundlevel":
                        resolved = _resolve_sound_level(raw, parameter.index)
                        fields[key] = resolved if resolved is not None else {"value": raw, "parameter": parameter.index}
                    elif key in ("position", "attenuation"):
                        number = _number(raw)
                        fields[key] = {"value": number if number is not None else raw, "parameter": parameter.index}
                    else:  # name
                        fields[key] = {"value": raw, "parameter": parameter.index}
                else:
                    unsupported.append({"key": key, "offset": parameter.offset, "reason": "key-outside-the-soundscape-block-vocabulary"})
            else:
                nested = inner_item
                nested_key = nested.name.text.strip().lower()
                if nested_key != "rndwave":
                    unsupported.append({"key": nested_key, "offset": nested.offset, "reason": "key-outside-the-soundscape-block-vocabulary"})
                    _claim_block_tree(ledger, nested, f"entry.block[{sub_key}#{sub_index}].block[unknown]", covered)
                    continue
                claim_token(nested.name, f"entry.block[{sub_key}#{sub_index}].block[rndwave].name")
                claim_token(nested.open_token, f"entry.block[{sub_key}#{sub_index}].block[rndwave].open")
                if nested.close_token is not None:
                    claim_token(nested.close_token, f"entry.block[{sub_key}#{sub_index}].block[rndwave].close")
                for rk, ri in nested.items:
                    if rk != "pair":
                        _claim_block_tree(
                            ledger, ri, f"entry.block[{sub_key}#{sub_index}].block[rndwave].block[unknown]", covered
                        )
                        continue
                    parameter = add_parameter(ri)
                    if parameter.key == "wave":
                        add_wave(parameter, "rndwave")
                    else:
                        unsupported.append({"key": parameter.key, "offset": parameter.offset, "reason": "key-outside-the-rndwave-vocabulary"})
        return {"type": sub_key, "parameters": fields, "waves": waves}

    occurrence_counts: dict[str, int] = {}
    for kind, item in block.items:
        if kind == "pair":
            parameter = add_parameter(item)
            key, raw = parameter.key, parameter.value.strip()
            if key == "dsp":
                number = _number(raw)
                if number is None:
                    unsupported.append({"key": key, "offset": parameter.offset, "reason": "value-is-not-a-number"})
                else:
                    preset_id = int(number)
                    asset = dsp_preset_asset_id(preset_id)
                    resolved = bool(dsp_exists(preset_id)) if dsp_exists is not None else True
                    dsp_dependency = {"id": preset_id, "asset": asset, "resolved": resolved, "parameter": parameter.index}
                    #: The dependency's target is the table the id addresses, not the bare id
                    #: itself -- `sourcePath` is an install-relative path everywhere else in this
                    #: seam, and a preset id is not a path. The authored id still lives at
                    #: `record.dsp.id`.
                    depend("dsp-preset", asset, TABLE_PATHS["dsp-preset"], resolved)
                    if dsp_exists is not None and not resolved:
                        unresolved.append({"path": "dsp", "id": preset_id, "reason": "dsp-key-names-no-defined-preset"})
            else:
                unsupported.append({"key": key, "offset": parameter.offset, "reason": "key-outside-the-soundscape-vocabulary"})
        else:
            nested = item
            sub_key = nested.name.text.strip().lower()
            if sub_key not in SOUNDSCAPE_TOP_KEYS:
                unsupported.append({"key": sub_key, "offset": nested.offset, "reason": "key-outside-the-soundscape-vocabulary"})
                _claim_block_tree(ledger, nested, "entry.block[unknown]", covered)
                continue
            occurrence = occurrence_counts.get(sub_key, 0)
            occurrence_counts[sub_key] = occurrence + 1
            play_blocks.append(decode_pool_block(nested, occurrence))

    comments = _claim_lexical(ledger, tokens, covered, "entry")
    ledger_result = ledger.finish()
    omissions = _omission_if_claimed(
        ledger_result, "keyvalues-insignificant-whitespace", "separator-bytes-carry-no-keyvalues-meaning"
    )

    record = {"dsp": dsp_dependency, "blocks": play_blocks}
    return SoundScriptModel(
        kind="soundscape", name=closure.name, source_name=closure.source_name, asset_id=closure.asset_id,
        dormant=True,
        dormant_evidence=(
            "Source's env_soundscape and scripts/soundscapes.txt are dead code and dead data in "
            "VtMB: zero env_soundscape/env_speaker/env_microphone/trigger_soundscape entities "
            "exist in any shipped map"
        ),
        sources=[member], parameters=parameters, record=record, dependencies=dependencies,
        comments=comments, anomalies=anomalies, omissions=omissions, unresolved=unresolved,
        unsupported=unsupported, byte_coverage=[ledger_result],
    )


# --- sentence ---------------------------------------------------------------------------------


_LEN_PATTERN = re.compile(r"\{\s*Len\s+([-+0-9.]+)\s*\}\s*$", re.IGNORECASE)
_MODIFIER_PATTERN = re.compile(r"\(([pvset])(\d+)\)", re.IGNORECASE)
_MODIFIER_NAMES = {"p": "pitch", "v": "volume", "s": "start", "e": "end", "t": "time"}


def decode_sentence(
    closure,
    *,
    sound_exists: Callable[[str], bool] | None = None,
) -> SoundScriptModel:
    member = closure.entry
    text = lexer.decode_text(member.data)
    line = text  # the member's data IS the line's own extent

    ledger = ByteLedger(member.path, member.data, span_offset=member.span_offset)
    anomalies: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    dependencies: list[dict[str, Any]] = []
    parameters: list[Parameter] = []

    def add_parameter(offset: int, length: int, state: str, key: str, source_text: str, value: str) -> Parameter:
        index = len(parameters)
        ledger.claim(offset, length, state, f"entry.parameter[{index}]")
        parameter = Parameter(
            index=index, key=key, source_key=source_text, value=value,
            quoted_key=False, quoted_value=False, offset=offset,
        )
        parameters.append(parameter)
        return parameter

    name_end = 0
    while name_end < len(line) and line[name_end] not in " \t":
        name_end += 1
    name_text = line[:name_end]
    if len(name_text) > SENTENCE_MAX_NAME_LENGTH:
        anomalies.append({"role": "name-exceeds-23-characters", "offset": 0, "length": name_end})
    if "\t" in line:
        anomalies.append({"role": "line-contains-a-tab", "offset": 0})

    cursor = name_end
    if cursor < len(line) and line[cursor] == " ":
        ledger.claim(cursor, 1, "omitted-proven", "entry.whitespace")
        cursor += 1
    elif cursor < len(line):
        anomalies.append({"role": "irregular-name-separator", "offset": cursor})

    remainder = line[cursor:]
    length_seconds: float | None = None
    length_source: str | None = None
    length_span: tuple[int, int] | None = None
    match = _LEN_PATTERN.search(remainder)
    if match:
        block_start = cursor + match.start()
        pre_ws_start = block_start
        while pre_ws_start > cursor and line[pre_ws_start - 1] == " ":
            pre_ws_start -= 1
        if pre_ws_start > cursor:
            ledger.claim(pre_ws_start, block_start - pre_ws_start, "omitted-proven", "entry.whitespace")
        path_text = line[cursor:pre_ws_start]
        length_span = (block_start, len(line))
        length_source = match.group(1)
        length_seconds = _number(length_source)
        path_end = pre_ws_start
    else:
        path_text = remainder
        path_end = len(line)

    #: Built in source order -- name, path, optional length -- so `parameters[]` answers the same
    #: "every token the entry declares, in source order" contract the other four kinds do.
    add_parameter(0, name_end, "mapped", "name", name_text, name_text)
    if path_end > cursor:
        add_parameter(cursor, path_end - cursor, "mapped-text", "path", path_text, path_text)
    if length_span is not None:
        start, end = length_span
        add_parameter(start, end - start, "mapped", "length", line[start:end], length_source or "")

    is_hl1 = "," in path_text
    tokens: list[dict[str, Any]] = []
    directory = None
    path = None
    if is_hl1:
        directory_match = re.match(r"^([^,]*?/)", path_text)
        rest = path_text
        if directory_match:
            directory = directory_match.group(1)
            tokens.append({"type": "directory", "value": directory})
            rest = path_text[len(directory):]
        for chunk in re.split(r"(,|\.(?=\s|$))", rest):
            if chunk == "":
                continue
            if chunk in (",", "."):
                tokens.append({"type": "pause", "value": chunk})
                continue
            modifiers = [
                {"code": code.lower(), "name": _MODIFIER_NAMES[code.lower()], "value": int(value)}
                for code, value in _MODIFIER_PATTERN.findall(chunk)
            ]
            word = _MODIFIER_PATTERN.sub("", chunk).strip()
            if word:
                tokens.append({"type": "word", "value": word, "modifiers": modifiers})
    else:
        path = path_text

    if not is_hl1 and path is not None and not path.lower().endswith(".wav"):
        unsupported.append({"key": "path", "offset": cursor, "reason": "path-has-no-recognized-extension"})

    #: The modern grammar's path names a member of the sibling `sound_glb` seam's corpus, exactly
    #: as a game-sound `wave` value does; the hl1 grammar's phrase is not a file path at all, so it
    #: names nothing to depend on.
    if not is_hl1 and path is not None:
        asset = sound_asset_id(path)
        exists = sound_exists(path) if sound_exists is not None else True
        dependencies.append({"role": "sound", "asset": asset, "sourcePath": "sound/" + path, "resolved": exists})

    record = {
        "path": path,
        "directory": directory,
        "tokens": tokens,
        "length": length_seconds,
        "lengthSource": length_source,
        "grammar": "hl1" if is_hl1 else "modern",
    }
    ledger_result = ledger.finish()
    omissions = _omission_if_claimed(
        ledger_result, "sentence-separator-whitespace", "separator-bytes-carry-no-grammar-meaning"
    )
    return SoundScriptModel(
        kind="sentence", name=closure.name, source_name=closure.source_name, asset_id=closure.asset_id,
        dormant=False, dormant_evidence=None, sources=[member], parameters=parameters, record=record,
        dependencies=dependencies, comments=[], anomalies=anomalies, omissions=omissions, unresolved=unresolved,
        unsupported=unsupported, byte_coverage=[ledger_result],
    )


# --- DSP preset -------------------------------------------------------------------------------


def decode_dsp_preset(closure) -> SoundScriptModel:
    member = closure.entry
    text = lexer.decode_text(member.data)
    try:
        blocks, stray = dsp_tree.parse(lexer.tokenize(text))
    except lexer.SoundScriptLexError as error:
        raise SoundScriptDecodeError(f"{member.path}: {error}") from error
    if len(blocks) != 1:
        raise SoundScriptDecodeError(f"{member.path}: the unit's bytes declare {len(blocks)} presets, not one")
    if stray:
        raise SoundScriptDecodeError(f"{member.path}: the unit's span carries stray top-level text")
    block = blocks[0]

    ledger = ByteLedger(member.path, member.data, span_offset=member.span_offset)
    covered: list[tuple[int, int]] = []
    anomalies: list[dict[str, Any]] = list(block.anomalies)
    unsupported: list[dict[str, Any]] = []
    typed_unidentified: list[dict[str, Any]] = []

    def claim_token(token: lexer.Token, owner: str, state: str = "mapped") -> None:
        _claim(ledger, token, owner, state)
        covered.append((token.offset, token.end))

    claim_token(block.open_token, "entry.open")
    if block.close_token is not None:
        claim_token(block.close_token, "entry.close")

    parameters: list[Parameter] = []

    def add_token_parameter(token: lexer.Token, key: str) -> Parameter:
        slot = len(parameters)
        claim_token(token, f"entry.parameter[{slot}]")
        parameter = Parameter(
            index=slot, key=key, source_key=token.text, value=token.text,
            quoted_key=False, quoted_value=False, offset=token.offset,
        )
        parameters.append(parameter)
        return parameter

    header_tokens = block.tokens()
    fields = list(symbols.PRESET_FIELDS)
    header_values: dict[str, Any] = {}
    for index, token in enumerate(header_tokens[: len(fields)]):
        name = fields[index]
        header_parameter = add_token_parameter(token, name)
        if name == "configuration":
            resolved = symbols.resolve_symbol(token.text, symbols.PRESET_CONFIGURATIONS)
            if resolved is None:
                unsupported.append({"key": name, "offset": token.offset, "reason": "unrecognized-configuration-symbol"})
            header_values[name] = {
                "sourceToken": token.text, "resolved": resolved, "parameter": header_parameter.index,
            }
        elif name == "id":
            header_values[name] = {"value": int(token.text.strip()), "parameter": header_parameter.index}
        else:
            number = _number(token.text)
            if number is None:
                unsupported.append({"key": name, "offset": token.offset, "reason": "value-is-not-a-number"})
            header_values[name] = {"value": number, "parameter": header_parameter.index}
    if len(header_tokens) < len(fields):
        anomalies.append({
            "role": "preset-header-field-count-mismatch", "offset": block.offset,
            "declaredCount": len(fields), "actualCount": len(header_tokens),
        })

    processors: list[dict[str, Any]] = []
    for proc_index, sub in enumerate(block.blocks()):
        claim_token(sub.open_token, f"entry.processor[{proc_index}].open")
        if sub.close_token is not None:
            claim_token(sub.close_token, f"entry.processor[{proc_index}].close")
        proc_tokens = sub.tokens()
        if not proc_tokens:
            anomalies.append({"role": "empty-processor-block", "offset": sub.offset})
            continue
        type_token = proc_tokens[0]
        type_parameter = add_token_parameter(type_token, "processorType")
        type_name = type_token.text.strip().upper()
        is_null = type_name == "0" or type_name == symbols.NULL_PROCESSOR
        declared = () if is_null else symbols.PROCESSOR_PARAMETERS.get(type_name)
        if not is_null and declared is None:
            unsupported.append({"key": "processorType", "offset": type_token.offset, "reason": "unrecognized-processor-type", "sourceToken": type_token.text})
        params: list[dict[str, Any]] = []
        for slot, token in enumerate(proc_tokens[1:]):
            name = declared[slot] if declared and slot < len(declared) else f"param{slot}"
            token_parameter = add_token_parameter(token, name)
            number = _number(token.text)
            symbol_key: str | None = None
            value: Any = number
            if number is None:
                table = symbols.PROCESSOR_FIELD_SYMBOLS.get((type_name, name))
                if table is not None:
                    symbol_key = symbols.resolve_symbol_key(token.text, table)
                    if symbol_key is not None:
                        value = table[symbol_key]
                if symbol_key is None:
                    typed_unidentified.append({
                        "field": f"processors[{proc_index}].parameters[{slot}]",
                        "sourceToken": token.text,
                        "offset": token.offset,
                    })
            params.append({
                "index": slot,
                "sourceToken": token.text,
                "symbol": symbol_key,
                "value": value,
                "name": declared[slot] if declared and slot < len(declared) else None,
                "offset": token.offset,
                #: The entry-level `parameters[]` slot this positional token came from, distinct
                #: from `index`, which is this token's slot within its own processor block.
                "parameter": token_parameter.index,
            })
        actual = len(proc_tokens) - 1
        declared_count = len(declared) if declared else 0
        if not is_null and actual != declared_count:
            anomalies.append({
                "role": "parameter-count-mismatch", "offset": sub.offset, "type": type_name,
                "declaredCount": declared_count, "actualCount": actual,
            })
        processors.append({
            "type": "pass-through" if is_null else type_name,
            "sourceToken": type_token.text,
            "declaredCount": declared_count,
            "parameters": params,
            "parameter": type_parameter.index,
        })

    comments = _claim_lexical(ledger, lexer.tokenize(text), covered, "entry")
    record = {
        "id": header_values.get("id"),
        "configuration": header_values.get("configuration"),
        "mix": {"min": header_values.get("mixMin"), "max": header_values.get("mixMax")},
        "parameters": {
            "duration": header_values.get("duration"),
            "fadeTime": header_values.get("fadeTime"),
            "dbMin": header_values.get("dbMin"),
            "dbMixdrop": header_values.get("dbMixdrop"),
        },
        "processors": processors,
    }
    ledger_result = ledger.finish()
    omissions = _omission_if_claimed(
        ledger_result, "keyvalues-insignificant-whitespace", "separator-bytes-carry-no-keyvalues-meaning"
    )
    return SoundScriptModel(
        kind="dsp-preset", name=str(closure.preset_id), source_name=str(closure.preset_id),
        asset_id=closure.asset_id, dormant=False, dormant_evidence=None, sources=[member],
        parameters=parameters, record=record, dependencies=[], comments=comments, anomalies=anomalies,
        omissions=omissions, unresolved=[], unsupported=unsupported,
        typed_unidentified=typed_unidentified, byte_coverage=[ledger_result],
    )
