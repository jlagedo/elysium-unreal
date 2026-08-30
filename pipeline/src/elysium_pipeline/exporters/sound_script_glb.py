"""Isolated one-entry/one-GLB sound-script product writer, for all five unit kinds."""

from __future__ import annotations

from pathlib import Path
from typing import Callable

from elysium_pipeline.formats.sound_script_glb import coverage as coverage_module
from elysium_pipeline.formats.sound_script_glb import decode, source
from elysium_pipeline.formats.sound_script_glb.model import (
    GENERATOR_TITLE,
    SCHEMA_VERSION,
    SOUND_SCRIPT_EXTENSION,
    dsp_preset_output_path,
    game_sound_output_path,
    manifest_output_path,
    sentence_output_path,
    soundscape_output_path,
)
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    extension_root,
    identity_block,
    plain,
    source_resolution,
    write_glb,
)


class SoundScriptGlbError(RuntimeError):
    pass


def _parameter_rows(model) -> list[dict]:
    return [
        {
            "index": parameter.index,
            "key": parameter.key,
            "sourceKey": parameter.source_key,
            "value": parameter.value,
            "quotedKey": parameter.quoted_key,
            "quotedValue": parameter.quoted_value,
            "offset": parameter.offset,
        }
        for parameter in model.parameters
    ]


def build_document(model) -> tuple[dict, bytes]:
    """The one GLB shape every kind shares: `kind`, `record` and the byte ledger differ; the
    extension root's key order does not."""

    source_paths = [member.path for member in model.sources]
    identity = identity_block(
        model.asset_id,
        source_paths if len(source_paths) > 1 else source_paths[0],
        name=model.name,
        sourceName=model.source_name,
    )
    kind_specific: dict = {"kind": model.kind}
    if model.kind in ("game-sound", "soundscape"):
        kind_specific["dormant"] = model.dormant
        if model.dormant_evidence:
            kind_specific["dormantEvidence"] = model.dormant_evidence
    kind_specific["parameters"] = _parameter_rows(model)
    kind_specific["record"] = plain(model.record)
    kind_specific["comments"] = model.comments
    kind_specific["anomalies"] = model.anomalies
    kind_specific["omissions"] = model.omissions

    extension = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity,
        source_resolution=source_resolution(model.sources),
        dependencies=model.dependencies,
        coverage=coverage_module.build_coverage(model),
        **kind_specific,
    )
    document = {
        "asset": asset_block(GENERATOR_TITLE),
        "extensionsUsed": [SOUND_SCRIPT_EXTENSION],
        "extensionsRequired": [SOUND_SCRIPT_EXTENSION],
        "extensions": {SOUND_SCRIPT_EXTENSION: plain(extension)},
    }
    return document, b""


def _validate_and_write(
    document: dict, binary: bytes, members, destination: Path, *, resolvers: dict | None = None
) -> Path:
    from elysium_pipeline.validation import sound_script_glb as validation

    validation.validate_document(document, binary, source_members=members, resolvers=resolvers)
    write_glb(document, binary, destination)
    return destination


ReadBytes = Callable[[dict, str], bytes | None] | None


def export_game_sound(
    index: dict, name: str, output_root: Path, *, read_bytes: ReadBytes = None, directory=None, sentences=None,
) -> Path:
    directory = directory or source.load_game_sound_directory(index, read_bytes=read_bytes)
    sentences = sentences if sentences is not None else _sentence_names(index, read_bytes=read_bytes)
    closure = source.game_sound_closure(directory, name)
    resolvers = {
        "sentence_exists": lambda n: n.lower() in sentences,
        "sound_exists": lambda path: source.sound_reference_exists(index, path),
    }
    model = decode.decode_game_sound(closure, **resolvers)
    document, binary = build_document(model)
    destination = output_root / Path(*game_sound_output_path(closure.name).parts)
    return _validate_and_write(document, binary, closure.members(), destination, resolvers=resolvers)


def export_manifest(index: dict, output_root: Path, *, read_bytes: ReadBytes = None) -> Path:
    closure = source.manifest_closure(index, read_bytes=read_bytes)
    resolvers = {"table_exists": lambda path: source.table_reference_exists(index, path)}
    model = decode.decode_manifest(closure, table_exists=resolvers["table_exists"])
    document, binary = build_document(model)
    destination = output_root / Path(*manifest_output_path().parts)
    return _validate_and_write(document, binary, closure.members(), destination, resolvers=resolvers)


def export_soundscape(
    index: dict, name: str, output_root: Path, *, read_bytes: ReadBytes = None, table=None, dsp_ids=None,
    sentences=None,
) -> Path:
    from elysium_pipeline.formats.sound_script_glb.model import TABLE_PATHS

    table = table or source.load_kv_table(index, TABLE_PATHS["soundscape"], read_bytes=read_bytes)
    dsp_ids = dsp_ids if dsp_ids is not None else _dsp_ids(index, read_bytes=read_bytes)
    sentences = sentences if sentences is not None else _sentence_names(index, read_bytes=read_bytes)
    closure = source.soundscape_closure(table, name)
    resolvers = {
        "dsp_exists": lambda i: i in dsp_ids,
        "sentence_exists": lambda n: n.lower() in sentences,
        "sound_exists": lambda path: source.sound_reference_exists(index, path),
    }
    model = decode.decode_soundscape(closure, **resolvers)
    document, binary = build_document(model)
    destination = output_root / Path(*soundscape_output_path(closure.name).parts)
    return _validate_and_write(document, binary, closure.members(), destination, resolvers=resolvers)


def export_sentence(
    index: dict, name: str, output_root: Path, *, read_bytes: ReadBytes = None, table=None,
) -> Path:
    table = table or source.load_sentence_table(index, read_bytes=read_bytes)
    closure = source.sentence_closure(table, name)
    resolvers = {"sound_exists": lambda path: source.sound_reference_exists(index, path)}
    model = decode.decode_sentence(closure, sound_exists=resolvers["sound_exists"])
    document, binary = build_document(model)
    destination = output_root / Path(*sentence_output_path(closure.name).parts)
    return _validate_and_write(document, binary, closure.members(), destination, resolvers=resolvers)


def export_dsp_preset(
    index: dict, preset_id: int | str, output_root: Path, *, read_bytes: ReadBytes = None, table=None,
) -> Path:
    table = table or source.load_dsp_table(index, read_bytes=read_bytes)
    closure = source.dsp_preset_closure(table, int(preset_id))
    model = decode.decode_dsp_preset(closure)
    document, binary = build_document(model)
    destination = output_root / Path(*dsp_preset_output_path(closure.preset_id).parts)
    return _validate_and_write(document, binary, closure.members(), destination)


#: `sound-script-glb <name>` addresses the game-sound kind, the seam's namesake identity.
export = export_game_sound


def _sentence_names(index: dict, *, read_bytes: ReadBytes = None) -> frozenset[str]:
    try:
        table = source.load_sentence_table(index, read_bytes=read_bytes)
    except source.SoundScriptSourceError:
        return frozenset()
    return frozenset(table.names)


def _dsp_ids(index: dict, *, read_bytes: ReadBytes = None) -> frozenset[int]:
    try:
        table = source.load_dsp_table(index, read_bytes=read_bytes)
    except source.SoundScriptSourceError:
        return frozenset()
    return frozenset(table.ids)


def game_sound_keys(index: dict, *, read_bytes: ReadBytes = None) -> list[str]:
    return list(source.load_game_sound_directory(index, read_bytes=read_bytes).names)


def manifest_keys(index: dict, *, read_bytes: ReadBytes = None) -> list[str]:
    del index, read_bytes
    from elysium_pipeline.formats.sound_script_glb.model import MANIFEST_KEY

    return [MANIFEST_KEY]


def soundscape_keys(index: dict, *, read_bytes: ReadBytes = None) -> list[str]:
    from elysium_pipeline.formats.sound_script_glb.model import TABLE_PATHS

    table = source.load_kv_table(index, TABLE_PATHS["soundscape"], read_bytes=read_bytes)
    return list(table.names)


def sentence_keys(index: dict, *, read_bytes: ReadBytes = None) -> list[str]:
    return list(source.load_sentence_table(index, read_bytes=read_bytes).names)


def dsp_preset_keys(index: dict, *, read_bytes: ReadBytes = None) -> list[str]:
    return [str(preset_id) for preset_id in source.load_dsp_table(index, read_bytes=read_bytes).ids]


def source_keys(index: dict, *, read_bytes: ReadBytes = None) -> list[str]:
    """Every identity key this seam publishes, across all five kinds.

    The three plural commands each address one slice of this set
    (`sound-scripts-glb` -> game sound + manifest + soundscape, `sentences-glb` -> sentence,
    `dsp-presets-glb` -> DSP preset); the kind-specific `*_keys` functions above serve those
    directly, and this one is for a corpus-wide enumeration.
    """

    keys = list(game_sound_keys(index, read_bytes=read_bytes))
    keys += manifest_keys(index, read_bytes=read_bytes)
    keys += soundscape_keys(index, read_bytes=read_bytes)
    keys += sentence_keys(index, read_bytes=read_bytes)
    keys += dsp_preset_keys(index, read_bytes=read_bytes)
    return keys
