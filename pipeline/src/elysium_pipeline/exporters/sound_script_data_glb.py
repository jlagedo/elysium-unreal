"""The live manifest and its whole selected tables, without entry slicing or name folding."""
from dataclasses import asdict
from pathlib import Path, PurePosixPath
import base64
import hashlib
import re
import struct

from elysium_pipeline.formats import install
from elysium_pipeline.formats.sound_script_glb import lexer
from elysium_pipeline.formats.unit_contract import (
    ByteLedger, SourceMember, asset_block, buffer_table, coverage_block, encapsulate,
    extension_root, identity_block, origin_of, read_member, source_capsules, write_glb, read_glb,
)

EXTENSION = "ELYSIUM_vtmb_sound_script_data"
FAMILY = "sound-script-data"
SOURCE = "scripts/game_sounds_manifest.txt"


def ordered_roots(data: bytes):
    """Independent 10247280/102480f0 ordered source projection.

    This selects whole source members; it does not interpret sound descriptors. Unlike the
    entry seam's grammar, successful empty/malformed text is permitted, root peek tests its
    first byte, block values test exact '{', and token delimiters include ()':.
    """
    text = data.decode("latin-1")
    cursor = 0
    delimiters = "{}()':"
    quoted = False

    def at(index):
        return text[index] if index < len(text) else "\0"

    def space(char):
        return ord(char) <= 32 or ord(char) >= 128

    def read():
        nonlocal cursor, quoted
        quoted = False
        if cursor is None:
            return ""
        while True:
            while space(at(cursor)):
                if at(cursor) == "\0":
                    cursor = None
                    return ""
                cursor += 1
            if text.startswith("//", cursor):
                while at(cursor) not in ("\n", "\0"):
                    cursor += 1
                continue
            if text.startswith("/*", cursor):
                cursor += 2
                while at(cursor) != "\0" and not text.startswith("*/", cursor):
                    cursor += 1
                if text.startswith("*/", cursor):
                    cursor += 2
                continue
            break
        char = at(cursor)
        if char == '"':
            quoted = True
            cursor += 1
            output = []
            while True:
                char = at(cursor)
                cursor += 1
                if char == "\\" and at(cursor) == '"':
                    output.append('"')
                    cursor += 1
                elif char in ('"', "\0"):
                    return "".join(output)
                else:
                    output.append(char)
        if char in delimiters:
            cursor += 1
            return char
        start = cursor
        while True:
            cursor += 1
            char = at(cursor)
            if space(char) or char in delimiters:
                return text[start:cursor]

    def block():
        children = []
        while True:
            key = read()
            if key in ("", "}"):
                return children
            value = read()
            nested = []
            if value == "{":
                nested = block()
                value = ""
            elif not quoted:
                # GetString formats loader-typed numeric values, including prefix parses.
                integer = re.match(r'[ \t\n\v\f\r]*[+-]?[0-9]+', value)
                decimal = re.match(r'[ \t\n\r]*[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eEdD][+-]?[0-9]+)?', value)
                if decimal and decimal.end() > (integer.end() if integer else 0):
                    number = float(decimal.group().replace('D', 'e').replace('d', 'e'))
                    value = format(struct.unpack('<f', struct.pack('<f', number))[0], '.6f')
                elif integer:
                    value = str(max(-2147483648, min(2147483647, int(integer.group()))))
            children.append((key, value, nested))

    roots = []
    while True:
        name = read()
        if cursor is None:
            return roots
        peek = read()
        roots.append((name, "", block() if name and peek.startswith("{") else []))


def manifest_children(data: bytes) -> list[tuple[str, str]]:
    roots = ordered_roots(data)
    return [(key, value) for key, value, _ in roots[0][2]] if roots else []


def wave_candidates(members):
    """Accepted descriptor and appended wave order, before availability facts are applied."""
    result, seen = [], set()
    for key, value in manifest_children(members[SOURCE]):
        if key.lower() not in ('precache_file', 'declare_file'):
            continue
        path = source_path(value)
        if path not in members:
            continue
        for name, _, children in ordered_roots(members[path]):
            folded = name.translate(str.maketrans('ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'abcdefghijklmnopqrstuvwxyz'))
            if not children or folded in seen:
                continue
            entry = len(seen)
            seen.add(folded)
            waves = []
            for field, text, nested in children:
                if field.lower() == 'wave':
                    waves.append(text)
                elif field.lower() == 'rndwave':
                    waves.extend(child[1] for child in nested)
            ordinal = 0
            for authored in waves:
                gender_at = authored.lower().find('$gender')
                expanded = [(authored, 0)]
                if gender_at >= 0:
                    prefix = authored[:gender_at].encode('utf-8')[:255].decode('utf-8', errors='ignore')
                    suffix = authored[gender_at + 7:].encode('utf-8')[:255-len(prefix.encode('utf-8'))].decode('utf-8', errors='ignore')
                    expanded = [((prefix + gender + suffix).encode('utf-8')[:255].decode('utf-8', errors='ignore'), tag)
                                for gender, tag in (('male', 1), ('female', 2))]
                for wave, gender in expanded:
                    stripped = wave
                    for _ in range(2):
                        if stripped[:1] and stripped[0] in '*?!#@><^)':
                            stripped = stripped[1:]
                        else:
                            break
                    result.append({'entry': entry, 'name': name, 'source': path,
                                   'sourceSha256': hashlib.sha256(members[path]).hexdigest(),
                                   'wave': ordinal, 'authored': authored, 'value': wave, 'gender': gender,
                                   'filesystemPath': 'sound/' + stripped.replace('\\', '/').lower()
                                   if wave and not wave.startswith('!') else None})
                    ordinal += 1
    return result


def source_bindings(members):
    return [{'path': path, 'sha256': hashlib.sha256(data).hexdigest(), 'byteLength': len(data),
             'bytesBase64': base64.b64encode(data).decode('ascii')} for path, data in members.items()]


def source_path(path: str) -> str:
    # Preserve authored spelling in declaration metadata; member resolution uses the install key.
    normalized = path.replace("\\", "/")
    parts = normalized.split("/")
    if (not normalized.lower().startswith("scripts/") or any(p in ("", ".", "..") or ":" in p
                                                     for p in parts)):
        raise ValueError(f"unsafe sound script source path {path!r}")
    return str(PurePosixPath(normalized)).lower()


def export_table(export_v2_root: Path, *, index=None, read_bytes=None, name_coverage=None) -> Path:
    injected = index is not None
    index = install.build_index() if index is None else index
    read_bytes = read_member if read_bytes is None else read_bytes
    manifest = read_bytes(index, SOURCE)
    if manifest is None:
        raise ValueError(f"missing required {SOURCE}")
    members = [SourceMember("manifest", SOURCE, manifest, origin_of(index[SOURCE]))]
    declarations = []
    seen = {SOURCE}
    for key, value in manifest_children(manifest):
        accepted = key.lower() in ("precache_file", "declare_file")
        row = {"key": key, "path": value, "accepted": accepted,
               "precache": key.lower() == "precache_file"}
        if accepted:
            path = source_path(value)
            data = read_bytes(index, path) if path in index else None
            row.update(member=path, missing=data is None)
            if data is not None and path not in seen:
                seen.add(path)
                members.append(SourceMember("table", path, data, origin_of(index[path])))
        declarations.append(row)
    if name_coverage is None:
        if injected:
            raise ValueError('injected source index requires explicit sound name coverage')
        name_coverage = install.sound_name_coverage()
    names, availability_coverage = name_coverage
    raw_members = {member.path: member.data for member in members}
    availability = wave_candidates(raw_members)
    for row in availability:
        path = row['filesystemPath']
        row['availability'] = ('nonfile' if path is None else 'present' if path in names else
                               'absent' if availability_coverage['complete'] else 'unknown')
        if path in names:
            row['origin'] = origin_of(names[path]).to_json()
    ledgers, lexical = [], []
    for member in members:
        tokens = lexer.tokenize(lexer.decode_text(member.data))
        ledger = ByteLedger(member.path, member.data)
        for ordinal, token in enumerate(tokens):
            ledger.claim(token.offset, token.length, "mapped-text", f"lexical span {ordinal}: {token.kind}")
        ledgers.append(ledger.finish())
        lexical.append({"path": member.path, "tokens": [asdict(t) for t in tokens]})
    resolution, views, binary = encapsulate(members)
    root = extension_root(schema_version="1.0.0",
                          identity=identity_block("vtmb:sound-script-data:manifest", SOURCE),
                          source_resolution=resolution, dependencies=[],
                          coverage=coverage_block(mapped=["whole ordered live parser sources"], byte_ledger=ledgers),
                          declarations=declarations, lexical=lexical,
                          sourceBindings=source_bindings(raw_members), waveAvailability=availability,
                          availabilityCoverage=availability_coverage)
    document = {"asset": asset_block("Live SoundEmitter parser sources"),
                "extensionsUsed": [EXTENSION], "extensionsRequired": [EXTENSION],
                "extensions": {EXTENSION: root}, "buffers": buffer_table(binary), "bufferViews": views}
    destination = Path(export_v2_root) / FAMILY / "manifest.glb"
    write_glb(document, binary, destination)
    captured, payload = read_glb(destination)
    capsules = source_capsules(captured, payload, captured["extensions"][EXTENSION])
    if capsules != {m.path: m.data for m in members}:
        raise ValueError("published sound-script sources differ from selected install members")
    return destination
