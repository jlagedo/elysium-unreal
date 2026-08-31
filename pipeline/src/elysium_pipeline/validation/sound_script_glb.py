"""Independent structural validator for Sound-script GLB products, all five kinds.

This re-checks the emitted document's own internal consistency -- identity, parameter ordering,
dependency/reference agreement, and the byte ledger against the contract's shared verifier. At
export time, when `source_members` is supplied, it also re-decodes those members through
`formats.sound_script_glb.decode` -- the format's own decoder, not the writer -- and compares the
result against the published `record`/`parameters`/`comments`/`anomalies`/`omissions`/
`unsupported`, so a document tampered with after decoding is caught rather than only re-checked
against itself. It never imports `exporters.sound_script_glb.build_document`.
"""

from __future__ import annotations

from collections import Counter
from pathlib import Path
from typing import Any

from elysium_pipeline.formats.sound_script_glb.model import (
    SCHEMA_VERSION,
    SOUND_SCRIPT_EXTENSION,
    sound_asset_id,
)
from elysium_pipeline.formats.unit_contract import (
    UnitValidationError,
    completeness,
    plain,
    read_glb,
    validate_capsules,
    validate_container,
    validate_extension_root,
    validate_ledgers,
    validate_sceneless,
)

KINDS = {"game-sound", "manifest", "soundscape", "sentence", "dsp-preset"}
WAVE_ORIGINS = {"direct", "rndwave"}
WAVE_PREFIXES = {
    "stream", "music", "distance-variant", "player", "doppler", "direction", "spatial",
    "no-spatial", "sentence", "voice", None,
}
DEPENDENCY_ROLES = {"sound", "sentence", "dsp-preset", "sound-script-table"}


class SoundScriptGlbValidationError(UnitValidationError):
    pass


def _fail(message: str) -> None:
    raise SoundScriptGlbValidationError(message)


def _check_parameters(root: dict[str, Any]) -> list[dict[str, Any]]:
    parameters = root.get("parameters")
    if not isinstance(parameters, list):
        _fail("the unit declares no parameter table")
    for index, row in enumerate(parameters):
        if not isinstance(row, dict) or row.get("index") != index:
            _fail(f"parameter {index} is not in source order")
        for key in ("key", "sourceKey", "value", "quotedKey", "quotedValue", "offset"):
            if key not in row:
                _fail(f"parameter {index} is missing {key}")
    return parameters


def _check_indexed(value: Any, parameters: list[dict], where: str) -> None:
    """A resolved scalar record names the `parameters[]` slot it came from.

    Applied to every field this seam attaches a `parameter` index to: the game-sound/soundscape
    scalars, a soundscape block field, and a dsp-preset header/positional value.
    """

    if not isinstance(value, dict):
        return
    slot = value.get("parameter")
    if not isinstance(slot, int) or not 0 <= slot < len(parameters):
        _fail(f"{where} names no parameter of this entry")


def _check_wave_records(waves: Any, parameters: list[dict], where: str) -> list[dict[str, Any]]:
    if not isinstance(waves, list):
        _fail(f"{where} carries no wave pool")
    for ordinal, record in enumerate(waves):
        if not isinstance(record, dict):
            _fail(f"{where}[{ordinal}] is not a record")
        asset = str(record.get("asset") or "")
        if not (asset.startswith("vtmb:sound:") or asset.startswith("vtmb:sentence:")):
            _fail(f"{where}[{ordinal}] carries no stable identity")
        if record.get("origin") not in WAVE_ORIGINS:
            _fail(f"{where}[{ordinal}] names an origin outside direct/rndwave")
        if record.get("prefix") not in WAVE_PREFIXES:
            _fail(f"{where}[{ordinal}] names a prefix outside the sound-char grammar")
        slot = record.get("parameter")
        if not isinstance(slot, int) or not 0 <= slot < len(parameters):
            _fail(f"{where}[{ordinal}] names no parameter of this entry")
    return waves


def _check_dependencies(root: dict[str, Any], referenced: dict[str, str]) -> None:
    declared: dict[str, str] = {}
    for row in root.get("dependencies") or []:
        if not isinstance(row, dict):
            _fail("a dependency row is not a record")
        role, asset = str(row.get("role") or ""), str(row.get("asset") or "")
        if role not in DEPENDENCY_ROLES:
            _fail(f"unknown dependency role {role!r}")
        if not isinstance(row.get("resolved"), bool):
            _fail(f"dependency {asset} does not state whether it resolved")
        if asset in declared:
            _fail(f"dependency {asset} is declared twice")
        declared[asset] = role
    if declared != referenced:
        _fail("the dependency set disagrees with the references that produced it")


def _validate_game_sound(root: dict[str, Any]) -> dict[str, Any]:
    identity = root["identity"]
    name = str(identity.get("name", ""))
    asset = str(identity.get("asset", ""))
    if asset != "vtmb:sound-script:" + name or name != name.lower():
        _fail("game-sound identity disagrees with its name")
    if str(identity.get("sourceName", "")).strip().lower() != name:
        _fail("the name disagrees with its source spelling")
    if not isinstance(root.get("dormant"), bool):
        _fail("a game-sound unit does not state whether it is dormant")
    parameters = _check_parameters(root)
    record = root.get("record") or {}
    for field in ("channel", "volume", "pitch", "soundLevel"):
        value = record.get(field)
        if value is not None and not isinstance(value, dict):
            _fail(f"record.{field} is neither a resolved record nor absent")
        _check_indexed(value, parameters, f"record.{field}")
    waves = _check_wave_records(record.get("waves"), parameters, "record.waves")
    referenced = {str(row["asset"]): ("sentence" if row["asset"].startswith("vtmb:sentence:") else "sound") for row in waves}
    _check_dependencies(root, referenced)
    return {"asset": asset, "name": name, "dormant": root["dormant"], "waves": len(waves), "parameters": len(parameters)}


def _validate_manifest(root: dict[str, Any]) -> dict[str, Any]:
    identity = root["identity"]
    asset = str(identity.get("asset", ""))
    if asset != "vtmb:sound-script-manifest:game_sounds_manifest":
        _fail("manifest identity is not the fixed sound-script-manifest asset")
    parameters = _check_parameters(root)
    record = root.get("record") or {}
    files = record.get("precacheFiles")
    if not isinstance(files, list) or not files:
        _fail("the manifest precaches nothing")
    referenced: dict[str, str] = {}
    for ordinal, row in enumerate(files):
        if not isinstance(row, dict):
            _fail(f"precacheFiles[{ordinal}] is not a record")
        asset_row = str(row.get("asset") or "")
        if not asset_row.startswith("vtmb:sound-script-table:"):
            _fail(f"precacheFiles[{ordinal}] carries no stable identity")
        slot = row.get("parameter")
        if not isinstance(slot, int) or not 0 <= slot < len(parameters):
            _fail(f"precacheFiles[{ordinal}] names no parameter of this entry")
        referenced[asset_row] = "sound-script-table"
    _check_dependencies(root, referenced)
    return {"asset": asset, "precacheFiles": len(files)}


def _validate_soundscape(root: dict[str, Any]) -> dict[str, Any]:
    identity = root["identity"]
    name = str(identity.get("name", ""))
    asset = str(identity.get("asset", ""))
    if asset != "vtmb:soundscape:" + name or name != name.lower():
        _fail("soundscape identity disagrees with its name")
    if root.get("dormant") is not True:
        _fail("a soundscape unit must publish dormant: true")
    parameters = _check_parameters(root)
    record = root.get("record") or {}
    referenced: dict[str, str] = {}
    dsp = record.get("dsp")
    if dsp is not None:
        if not isinstance(dsp, dict) or not str(dsp.get("asset", "")).startswith("vtmb:dsp-preset:"):
            _fail("record.dsp carries no stable dsp-preset identity")
        referenced[str(dsp["asset"])] = "dsp-preset"
    blocks = record.get("blocks")
    if not isinstance(blocks, list):
        _fail("record.blocks is missing")
    for ordinal, block in enumerate(blocks):
        if not isinstance(block, dict) or block.get("type") not in ("playlooping", "playrandom", "playsoundscape"):
            _fail(f"record.blocks[{ordinal}] names an unrecognized block type")
        for key, value in (block.get("parameters") or {}).items():
            _check_indexed(value, parameters, f"record.blocks[{ordinal}].parameters.{key}")
        waves = _check_wave_records(block.get("waves"), parameters, f"record.blocks[{ordinal}].waves")
        for row in waves:
            referenced[str(row["asset"])] = "sentence" if row["asset"].startswith("vtmb:sentence:") else "sound"
    _check_dependencies(root, referenced)
    return {"asset": asset, "name": name, "blocks": len(blocks)}


def _validate_sentence(root: dict[str, Any]) -> dict[str, Any]:
    identity = root["identity"]
    name = str(identity.get("name", ""))
    asset = str(identity.get("asset", ""))
    if asset != "vtmb:sentence:" + name or name != name.lower():
        _fail("sentence identity disagrees with its name")
    parameters = _check_parameters(root)
    record = root.get("record") or {}
    if record.get("grammar") not in ("modern", "hl1"):
        _fail("record.grammar names an unrecognized sentence grammar")
    if not isinstance(record.get("tokens"), list):
        _fail("record.tokens is missing")
    referenced: dict[str, str] = {}
    path = record.get("path")
    if record.get("grammar") == "modern" and path is not None:
        referenced[sound_asset_id(path)] = "sound"
    _check_dependencies(root, referenced)
    return {"asset": asset, "name": name, "grammar": record.get("grammar"), "parameters": len(parameters)}


def _validate_dsp_preset(root: dict[str, Any]) -> dict[str, Any]:
    identity = root["identity"]
    name = str(identity.get("name", ""))
    asset = str(identity.get("asset", ""))
    if asset != "vtmb:dsp-preset:" + name:
        _fail("dsp-preset identity disagrees with its id")
    parameters = _check_parameters(root)
    record = root.get("record") or {}
    declared_id = record.get("id")
    _check_indexed(declared_id, parameters, "record.id")
    if not isinstance(declared_id, dict) or declared_id.get("value") != int(name):
        _fail("record.id disagrees with the unit's own key")
    configuration = record.get("configuration")
    _check_indexed(configuration, parameters, "record.configuration")
    mix = record.get("mix")
    if not isinstance(mix, dict) or "min" not in mix or "max" not in mix:
        _fail("record.mix is not a {min, max} pair")
    _check_indexed(mix.get("min"), parameters, "record.mix.min")
    _check_indexed(mix.get("max"), parameters, "record.mix.max")
    for key, value in (record.get("parameters") or {}).items():
        _check_indexed(value, parameters, f"record.parameters.{key}")
    processors = record.get("processors")
    if not isinstance(processors, list):
        _fail("record.processors is missing")
    for ordinal, processor in enumerate(processors):
        if not isinstance(processor, dict) or not isinstance(processor.get("parameters"), list):
            _fail(f"record.processors[{ordinal}] carries no parameter list")
        slot = processor.get("parameter")
        if not isinstance(slot, int) or not 0 <= slot < len(parameters):
            _fail(f"record.processors[{ordinal}] names no parameter of this entry")
        for param in processor["parameters"]:
            if not isinstance(param.get("offset"), int):
                _fail(f"record.processors[{ordinal}] carries a parameter with no source offset")
            param_slot = param.get("parameter")
            if not isinstance(param_slot, int) or not 0 <= param_slot < len(parameters):
                _fail(f"record.processors[{ordinal}] carries a parameter that names no entry parameter")
    return {
        "asset": asset, "id": declared_id.get("value"), "processors": len(processors),
        "parameters": len(parameters),
    }


_VALIDATORS = {
    "game-sound": _validate_game_sound,
    "manifest": _validate_manifest,
    "soundscape": _validate_soundscape,
    "sentence": _validate_sentence,
    "dsp-preset": _validate_dsp_preset,
}


def _parameter_rows(model) -> list[dict[str, Any]]:
    return [
        {
            "index": parameter.index, "key": parameter.key, "sourceKey": parameter.source_key,
            "value": parameter.value, "quotedKey": parameter.quoted_key,
            "quotedValue": parameter.quoted_value, "offset": parameter.offset,
        }
        for parameter in model.parameters
    ]


def _redecode(kind: str, root: dict[str, Any], entry, resolvers: dict[str, Any] | None = None) -> Any:
    """Re-decode the selected member through this format's own decoder -- not the writer.

    Reconstructed from the published identity plus the re-read member, so a document tampered
    with after decoding disagrees with this the same way a corrupted source would. `resolvers`
    carries the same `sound_exists`/`sentence_exists`/`table_exists`/`dsp_exists` callables the
    exporter used, so a `resolved` value the export-time install actually observed is
    reproducible rather than defaulting to "everything resolves".
    """

    from elysium_pipeline.formats.sound_script_glb import decode as decode_module
    from elysium_pipeline.formats.sound_script_glb import source as source_module

    resolvers = resolvers or {}
    identity = root.get("identity") or {}
    asset = str(identity.get("asset", ""))
    name = str(identity.get("name", ""))
    source_name = str(identity.get("sourceName", ""))
    try:
        if kind == "game-sound":
            #: The shared/shadowed-dormant fact comes from the two KeyValues tables together,
            #: not from this one entry's own bytes; reconstructed from the unit's own published
            #: `shadowed-dormant-entry` anomaly, the same way `dormant` is taken from the
            #: unit's own published flag rather than re-derived.
            shadowed_dormant = None
            for row in root.get("anomalies") or []:
                if isinstance(row, dict) and row.get("role") == "shadowed-dormant-entry":
                    shadowed_dormant = {k: v for k, v in row.items() if k != "role"}
                    break
            closure = source_module.GameSoundClosure(
                name=name, source_name=source_name, asset_id=asset,
                dormant=bool(root.get("dormant")), entry=entry, shadowed_dormant=shadowed_dormant,
            )
            return decode_module.decode_game_sound(
                closure,
                sentence_exists=resolvers.get("sentence_exists"),
                sound_exists=resolvers.get("sound_exists"),
            )
        if kind == "manifest":
            return decode_module.decode_manifest(
                source_module.ManifestClosure(asset_id=asset, entry=entry),
                table_exists=resolvers.get("table_exists"),
            )
        if kind == "soundscape":
            closure = source_module.SoundscapeClosure(
                name=name, source_name=source_name, asset_id=asset, entry=entry,
            )
            return decode_module.decode_soundscape(
                closure,
                dsp_exists=resolvers.get("dsp_exists"),
                sentence_exists=resolvers.get("sentence_exists"),
                sound_exists=resolvers.get("sound_exists"),
            )
        if kind == "sentence":
            closure = source_module.SentenceClosure(
                name=name, source_name=source_name, asset_id=asset, entry=entry,
            )
            return decode_module.decode_sentence(closure, sound_exists=resolvers.get("sound_exists"))
        if kind == "dsp-preset":
            closure = source_module.DspPresetClosure(preset_id=int(name), asset_id=asset, entry=entry)
            return decode_module.decode_dsp_preset(closure)
    except decode_module.SoundScriptDecodeError as error:
        _fail(f"independent re-decode failed: {error}")
    _fail(f"unknown sound-script kind {kind!r}")


def _check_independent_decode(
    root: dict[str, Any], source_members, resolvers: dict[str, Any] | None = None
) -> None:
    kind = str(root.get("kind"))
    entry = next(iter(source_members), None)
    if entry is None:
        _fail("prepublication source members omit this unit's entry")
    model = _redecode(kind, root, entry, resolvers)

    if plain(model.record) != root.get("record"):
        _fail(f"{kind} record disagrees with an independent re-decode")
    if _parameter_rows(model) != (root.get("parameters") or []):
        _fail(f"{kind} parameters disagree with an independent re-decode")
    if list(plain(model.comments)) != list(root.get("comments") or []):
        _fail(f"{kind} comments disagree with an independent re-decode")
    if list(plain(model.anomalies)) != list(root.get("anomalies") or []):
        _fail(f"{kind} anomalies disagree with an independent re-decode")
    if list(plain(model.omissions)) != list(root.get("omissions") or []):
        _fail(f"{kind} omissions disagree with an independent re-decode")
    if list(plain(model.dependencies)) != list(root.get("dependencies") or []):
        _fail(f"{kind} dependencies disagree with an independent re-decode")
    coverage = root.get("coverage") or {}
    published_unsupported = coverage.get("unsupported") or []
    if list(plain(model.unsupported)) != list(published_unsupported):
        _fail(f"{kind} unsupported entries disagree with an independent re-decode")
    published_unresolved = coverage.get("unresolved") or []
    if list(plain(model.unresolved)) != list(published_unresolved):
        _fail(f"{kind} unresolved entries disagree with an independent re-decode")
    published_typed_unidentified = coverage.get("typedUnidentified") or []
    if list(plain(model.typed_unidentified)) != list(published_typed_unidentified):
        _fail(f"{kind} typed-unidentified entries disagree with an independent re-decode")
    published_ledger = coverage.get("byteLedger") or []
    if list(plain(model.byte_coverage)) != list(published_ledger):
        _fail(f"{kind} byte ledger disagrees with an independent re-decode")


def validate_document(
    document: dict, binary: bytes, *, source_members=None, resolvers: dict[str, Any] | None = None
) -> dict[str, Any]:
    validate_container(document, binary)
    validate_sceneless(document)
    root = validate_extension_root(
        document, SOUND_SCRIPT_EXTENSION, asset_prefix="vtmb:", schema_version=SCHEMA_VERSION
    )
    validate_ledgers(root, source_members)
    validate_capsules(document, binary, root, source_members)

    kind = root.get("kind")
    if kind not in KINDS:
        _fail(f"unknown sound-script kind {kind!r}")
    incomplete = completeness(root)
    if incomplete["unresolved"] or incomplete["unsupported"]:
        _fail("sound-script extension is incomplete")
    for row in root.get("anomalies") or []:
        if not isinstance(row, dict) or not row.get("role"):
            _fail(f"unknown source anomaly {row!r}")

    if source_members is not None:
        _check_independent_decode(root, source_members, resolvers)

    summary = _VALIDATORS[kind](root)
    summary["kind"] = kind
    summary["dependencies"] = len(root.get("dependencies") or [])
    summary["anomalies"] = [str(row.get("role")) for row in root.get("anomalies") or []]
    summary["typedUnidentified"] = len((root.get("coverage") or {}).get("typedUnidentified") or [])
    #: A dependency the install could not resolve is a fact about another seam's corpus, not a
    #: defect in this entry -- surfaced as a warning (`warnings_for`), never a validation failure.
    summary["unresolvedDependencies"] = [
        str(row.get("asset")) for row in root.get("dependencies") or [] if not row.get("resolved")
    ]
    ledgers = (root.get("coverage") or {}).get("byteLedger") or []
    summary["sourceBytes"] = sum(int(row.get("byteLength", 0)) for row in ledgers)
    summary["accountedBytes"] = sum(int(row.get("accountedBytes", 0)) for row in ledgers)
    summary["byteCoveragePercent"] = (
        100.0 * summary["accountedBytes"] / summary["sourceBytes"] if summary["sourceBytes"] else 100.0
    )
    if summary["byteCoveragePercent"] != 100.0:
        _fail(
            f"{kind} accounts for {summary['accountedBytes']}/{summary['sourceBytes']} source bytes, "
            "not 100%"
        )
    return summary


def validate(path: Path) -> dict[str, Any]:
    document, binary = read_glb(path)
    return validate_document(document, binary)


def warnings_for(summary: dict[str, Any]) -> list[str]:
    warnings: list[str] = []
    typed_unidentified = summary.get("typedUnidentified") or 0
    if typed_unidentified:
        warnings.append(f"{typed_unidentified} typed but unidentified value(s) survived the export")
    unresolved = summary.get("unresolvedDependencies") or []
    if unresolved:
        shown = sorted(set(unresolved))
        counted = ", ".join(shown[:4]) + (f" and {len(shown) - 4} more" if len(shown) > 4 else "")
        warnings.append(f"the install carries no target for: {counted}")
    anomalies = summary.get("anomalies") or []
    if anomalies:
        counted = ", ".join(f"{role}x{count}" for role, count in sorted(Counter(anomalies).items()))
        warnings.append(f"the source departs from the table's conventions: {counted}")
    return warnings
