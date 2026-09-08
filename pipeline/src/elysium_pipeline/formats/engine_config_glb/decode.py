"""Grammar dispatch for the engine-config seam: one decode function per grammar
table, sharing the console-script parser (`console.py`), the KeyValues lexer (`lexer.py`) and the
save-fragment reader (`savefragment.py`)."""

from __future__ import annotations

import hashlib
import re
import struct
from typing import Any, Callable

from elysium_pipeline.formats.engine_config_glb import console, lexer, savefragment
from elysium_pipeline.formats.engine_config_glb.coverage import new_ledger, whitespace_omission
from elysium_pipeline.formats.engine_config_glb.model import (
    EngineConfigModel,
    dependency_asset_id,
    engine_config_source_path,
    map_asset_id,
    map_source_path,
    map_stem,
    material_asset_id,
    material_source_path,
    model_asset_id,
    model_source_path,
)
from elysium_pipeline.formats.unit_contract.ledger import ByteLedger
from elysium_pipeline.formats.unit_contract.origin import SourceMember
from elysium_pipeline.formats.unit_contract.references import asset_id as unit_asset_id

Resolver = Callable[[str], bool]


class EngineConfigDecodeError(RuntimeError):
    """The selected member cannot be read under its declared grammar."""


# --------------------------------------------------------------------------- shared line scanner

def _line_spans(text: str) -> list[tuple[int, int, int]]:
    """`(line_start, content_end, terminator_end)` for every line, terminator included.

    `content_end` excludes `\\r`/`\\n`; `terminator_end` includes them, so the terminator itself
    is always claimable as `whitespace` and never duplicated into the content span.
    """

    spans: list[tuple[int, int, int]] = []
    start = 0
    total = len(text)
    while start <= total:
        newline = text.find("\n", start)
        if newline < 0:
            if start < total:
                spans.append((start, total, total))
            break
        content_end = newline - 1 if newline > start and text[newline - 1] == "\r" else newline
        spans.append((start, content_end, newline + 1))
        start = newline + 1
    return spans


def _mixed_line_endings(text: str) -> list[dict[str, Any]]:
    """`anomalies[] mixed-line-endings`: CRLF and a lone LF both present in one file.

    The engine-config seam names this for the format generally,
    not for the console-script grammar alone, so every line-oriented text grammar in this module
    calls this, not just `console.py`'s own copy.
    """

    has_crlf = "\r\n" in text
    has_lone_lf = bool(re.search(r"(?<!\r)\n", text))
    return [{"role": "mixed-line-endings"}] if has_crlf and has_lone_lf else []


def _claim_line_layout(
    ledger: ByteLedger, text: str, spans: list[tuple[int, int, int]], is_comment: Callable[[str], bool]
) -> list[dict[str, Any]]:
    """Claim every blank line as whitespace and every comment line as a `comments[]` row.

    Returns the comment rows; the caller claims content lines with its own owner names.
    """

    comments: list[dict[str, Any]] = []
    for start, content_end, terminator_end in spans:
        content = text[start:content_end]
        if not content.strip():
            ledger.claim(start, terminator_end - start, "omitted-proven", "whitespace")
        elif is_comment(content):
            ledger.claim(start, content_end - start, "mapped-text", f"comments[{len(comments)}]")
            if terminator_end > content_end:
                ledger.claim(content_end, terminator_end - content_end, "omitted-proven", "whitespace")
            comments.append({"offset": start, "text": content})
    return comments


# ------------------------------------------------------------------------------------- rad-rows

_RAD_FIELDS = re.compile(r"\S+")


def decode_lights_rad(member: SourceMember, *, material_exists: Resolver | None = None) -> dict[str, Any]:
    text = lexer.decode_text(member.data)
    spans = _line_spans(text)
    ledger = new_ledger(member)
    comments = _claim_line_layout(ledger, text, spans, lambda content: content.lstrip().startswith("//"))

    texture_lights: list[dict[str, Any]] = []
    anomalies: list[dict[str, Any]] = list(_mixed_line_endings(text))
    dependencies: dict[str, dict[str, Any]] = {}

    for start, content_end, terminator_end in spans:
        content = text[start:content_end]
        if not content.strip() or content.lstrip().startswith("//"):
            continue
        fields = list(_RAD_FIELDS.finditer(content))
        row_index = len(texture_lights)
        ledger.claim(start, content_end - start, "mapped-text", f"textureLights[{row_index}]")
        if terminator_end > content_end:
            ledger.claim(content_end, terminator_end - content_end, "omitted-proven", "whitespace")

        texture = fields[0].group() if fields else ""
        numbers = [f.group() for f in fields[1:]]
        malformed = len(fields) != 5
        if malformed:
            anomalies.append({"role": "malformed-rad-row", "offset": start, "raw": content})

        def as_int(token: str) -> int | None:
            try:
                return int(float(token))
            except ValueError:
                return None

        r = as_int(numbers[0]) if len(numbers) > 0 else None
        g = as_int(numbers[1]) if len(numbers) > 1 else None
        b = as_int(numbers[2]) if len(numbers) > 2 else None
        intensity = as_int(numbers[3]) if len(numbers) > 3 else None

        material = material_asset_id(texture) if texture else None
        if texture:
            source_path = material_source_path(texture)
            resolved = bool(material_exists(source_path)) if material_exists is not None else False
            dependencies.setdefault(
                material,
                {
                    "role": "material",
                    "asset": material,
                    "sourcePath": source_path,
                    "resolved": resolved,
                },
            )
        texture_lights.append(
            {
                "index": row_index,
                "texture": texture,
                "material": material,
                "r": r,
                "g": g,
                "b": b,
                "intensity": intensity,
                "raw": content,
                "offset": start,
            }
        )

    ledger_row = ledger.finish()
    omissions = [] if member.data else [{"role": "empty-member"}]
    omissions.extend(whitespace_omission(ledger_row, member.data))
    return {
        "textureLights": texture_lights,
        "dependencies": list(dependencies.values()),
        "comments": comments,
        "anomalies": anomalies,
        "omissions": omissions,
        "ledgerRow": ledger_row,
    }


# ------------------------------------------------------------------------------------ keyvalues

def _own_pairs(node: lexer.KVNode) -> dict[str, lexer.Token]:
    return {pair.key.text.strip().lower(): pair.value for pair in node.pairs if pair.value is not None}


def _effective_children(node: lexer.KVNode) -> list[lexer.KVNode]:
    """`node.children`, with an anonymous (nameless) wrapper transparently unwrapped.

    Retail `detail.vbsp` carries one unnamed `{` with no name to attach to (an anomaly the
    format does not otherwise name; see `specDeviations`); unwrapping it here
    is what keeps the type it interrupts (`branches`) three levels deep like every other one.
    """

    result: list[lexer.KVNode] = []
    for child in node.children:
        if child.name.text == "":
            result.extend(_effective_children(child))
        else:
            result.append(child)
    return result


def _model_dict(node: lexer.KVNode, dependencies: dict[str, dict[str, Any]]) -> dict[str, Any]:
    pairs = _own_pairs(node)
    model_path = pairs["model"].text if "model" in pairs else None
    if model_path:
        asset = model_asset_id(model_path)
        dependencies.setdefault(
            asset,
            {"role": "model", "asset": asset, "sourcePath": model_source_path(model_path), "resolved": False},
        )
    return {
        "model": model_path,
        "modelNormalized": model_source_path(model_path) if model_path else None,
        "amount": pairs["amount"].text if "amount" in pairs else None,
    }


def _group_dict(node: lexer.KVNode, dependencies: dict[str, dict[str, Any]]) -> dict[str, Any]:
    pairs = _own_pairs(node)
    return {
        "name": node.name.text,
        "alpha": pairs["alpha"].text if "alpha" in pairs else None,
        "models": [_model_dict(child, dependencies) for child in _effective_children(node)],
    }


def _detail_type_dict(node: lexer.KVNode, dependencies: dict[str, dict[str, Any]]) -> dict[str, Any]:
    pairs = _own_pairs(node)
    return {
        "name": node.name.text,
        "density": pairs["density"].text if "density" in pairs else None,
        "groups": [_group_dict(child, dependencies) for child in _effective_children(node)],
    }


def decode_detail_vbsp(member: SourceMember, *, model_exists: Resolver | None = None) -> dict[str, Any]:
    text = lexer.decode_text(member.data)
    tokens = lexer.tokenize_keyvalues(text)
    ledger = new_ledger(member)

    comments: list[dict[str, Any]] = []
    for token in tokens:
        if token.kind == "whitespace":
            ledger.claim(token.offset, token.length, "omitted-proven", "whitespace")
        elif token.kind == "comment":
            ledger.claim(token.offset, token.length, "mapped-text", f"comments[{len(comments)}]")
            comments.append({"offset": token.offset, "text": token.text})

    parsed = lexer.parse_keyvalues_tree(tokens)
    roots = parsed.roots
    if len(roots) != 1 or roots[0].name.text.strip().lower() != "detail":
        raise EngineConfigDecodeError(f"{member.path}: does not open with a single 'detail' block")

    anomalies: list[dict[str, Any]] = list(parsed.file_anomalies) + _mixed_line_endings(text)

    # The owner table spells this `tree.nodes[i].keys[j]` with an
    # ordinal `i`, not the byte offset the node's name happens to sit at; `_node_ordinal` counts
    # every node this pre-order walk visits, parent before child, so `i` is stable and matches
    # the spec's own spelling.
    _node_ordinal = [0]

    def claim_node(node: lexer.KVNode) -> None:
        index = _node_ordinal[0]
        _node_ordinal[0] += 1
        anomalies.extend(node.anomalies)
        if node.name.length:
            ledger.claim(node.name.offset, node.name.length, "mapped-text", f"tree.nodes[{index}].name")
        ledger.claim(node.open_token.offset, node.open_token.length, "mapped-text", "tree.braces")
        if node.close_token is not None:
            ledger.claim(node.close_token.offset, node.close_token.length, "mapped-text", "tree.braces")
        for pair_index, pair in enumerate(node.pairs):
            ledger.claim(
                pair.key.offset, pair.key.length, "mapped-text",
                f"tree.nodes[{index}].keys[{pair_index}]",
            )
            if pair.value is not None:
                ledger.claim(
                    pair.value.offset, pair.value.length, "mapped-text",
                    f"tree.nodes[{index}].keys[{pair_index}]",
                )
        for child in node.children:
            claim_node(child)

    for root in roots:
        claim_node(root)
    for pair_index, pair in enumerate(parsed.orphan_pairs):
        anomalies.append({"role": "orphan-key-value-pair", "offset": pair.key.offset, "key": pair.key.text})
        ledger.claim(pair.key.offset, pair.key.length, "mapped-text", f"tree.orphans[{pair_index}]")
        if pair.value is not None:
            ledger.claim(pair.value.offset, pair.value.length, "mapped-text", f"tree.orphans[{pair_index}]")

    dependencies: dict[str, dict[str, Any]] = {}
    detail_types = [_detail_type_dict(child, dependencies) for child in _effective_children(roots[0])]
    for row in dependencies.values():
        model_path = row["sourcePath"]
        resolved = bool(model_exists(model_path)) if model_exists is not None else False
        row["resolved"] = resolved

    ledger_row = ledger.finish()
    return {
        "detailTypes": detail_types,
        "dependencies": list(dependencies.values()),
        "comments": comments,
        "anomalies": anomalies,
        "omissions": whitespace_omission(ledger_row, member.data),
        "ledgerRow": ledger_row,
    }


# ----------------------------------------------------------------------------------- line-list

def decode_loadorder(member: SourceMember, *, map_exists: Resolver | None = None) -> dict[str, Any]:
    text = lexer.decode_text(member.data)
    spans = _line_spans(text)
    ledger = new_ledger(member)

    _NOT_INCLUDED = re.compile(r"^//\s*not included:\s*(?P<name>\S+)\s*$", re.IGNORECASE)

    comments: list[dict[str, Any]] = []
    maps: list[dict[str, Any]] = []
    dependencies: dict[str, dict[str, Any]] = {}

    for start, content_end, terminator_end in spans:
        content = text[start:content_end]
        stripped = content.strip()
        if not stripped:
            ledger.claim(start, terminator_end - start, "omitted-proven", "whitespace")
            continue
        not_included = _NOT_INCLUDED.match(stripped)
        if stripped.startswith("//") and not not_included:
            ledger.claim(start, content_end - start, "mapped-text", f"comments[{len(comments)}]")
            if terminator_end > content_end:
                ledger.claim(content_end, terminator_end - content_end, "omitted-proven", "whitespace")
            comments.append({"offset": start, "text": content})
            continue

        row_index = len(maps)
        ledger.claim(start, content_end - start, "mapped-text", f"maps[{row_index}]")
        if terminator_end > content_end:
            ledger.claim(content_end, terminator_end - content_end, "omitted-proven", "whitespace")
        if not_included:
            comments.append({"offset": start, "text": content})
            name = not_included.group("name")
            included = False
        else:
            name = stripped
            included = True
        stem = map_stem(name)
        asset = map_asset_id(stem)
        source_path = map_source_path(stem)
        resolved = bool(map_exists(source_path)) if map_exists is not None else False
        maps.append(
            {
                "index": row_index,
                "name": name,
                "asset": asset,
                "included": included,
                "resolved": resolved,
                "offset": start,
            }
        )
        dependencies.setdefault(
            asset, {"role": "map", "asset": asset, "sourcePath": source_path, "resolved": resolved}
        )

    ledger_row = ledger.finish()
    omissions = [] if member.data else [{"role": "empty-member"}]
    omissions.extend(whitespace_omission(ledger_row, member.data))
    return {
        "maps": maps,
        "dependencies": list(dependencies.values()),
        "comments": comments,
        "anomalies": _mixed_line_endings(text),
        "omissions": omissions,
        "ledgerRow": ledger_row,
    }


# ---------------------------------------------------------------------------- key-equals-value

_PACKER_LIST_KEYS = {"exclude", "skip", "separate"}


def decode_pack_values(member: SourceMember, *, engine_config_exists: Resolver | None = None) -> dict[str, Any]:
    text = lexer.decode_text(member.data)
    spans = _line_spans(text)
    ledger = new_ledger(member)
    comments = _claim_line_layout(ledger, text, spans, lambda content: content.lstrip().startswith("//"))

    keys: list[dict[str, Any]] = []
    packer: dict[str, Any] = {
        "pack_folder": None, "max_size": None, "exclude": [], "skip": [], "separate": [],
        "localized_file": None,
    }
    anomalies: list[dict[str, Any]] = list(_mixed_line_endings(text))
    dependencies: dict[str, dict[str, Any]] = {}

    for start, content_end, terminator_end in spans:
        content = text[start:content_end]
        if not content.strip() or content.lstrip().startswith("//"):
            continue
        row_index = len(keys)
        # `packerKeys[i]`, the extension-root field this row is actually published under
        # -- the unit contract's "owner" is the decoder's path to the record that paid for
        # the range.
        ledger.claim(start, content_end - start, "mapped-text", f"packerKeys[{row_index}]")
        if terminator_end > content_end:
            ledger.claim(content_end, terminator_end - content_end, "omitted-proven", "whitespace")
        if "=" not in content:
            anomalies.append({"role": "malformed-key-equals-value-row", "offset": start, "raw": content})
            continue
        raw_key, _, raw_value = content.partition("=")
        key, value = raw_key.strip(), raw_value.strip()
        keys.append({"index": row_index, "key": key, "value": value, "offset": start})
        folded = key.lower()
        if folded in _PACKER_LIST_KEYS:
            packer[folded] = value.split()
        elif folded in ("pack_folder", "max_size", "localized_file"):
            packer[folded] = value
            if folded == "localized_file" and value:
                # `localized_file` names another unit of this same seam; the contract's own rule
                # ("References between units": "Every reference the unit makes is declared once
                # in dependencies, whatever produced it") applies whether or not the seam doc's
                # own Dependencies table names a role for it.
                source_path = value.replace("\\", "/").strip().lower()
                asset = dependency_asset_id(source_path)
                resolved = bool(engine_config_exists(source_path)) if engine_config_exists is not None else False
                dependencies.setdefault(
                    asset,
                    {"role": "engine-config", "asset": asset, "sourcePath": source_path, "resolved": resolved},
                )

    ledger_row = ledger.finish()
    return {
        "packer": packer,
        "packerKeys": keys,
        "dependencies": list(dependencies.values()),
        "comments": comments,
        "anomalies": anomalies,
        "omissions": whitespace_omission(ledger_row, member.data),
        "ledgerRow": ledger_row,
    }


# --------------------------------------------------------------------------------- localized-list

#: Extension -> the seam family this seam declares a reference to. `.vcd` and `.lip` do not
#: resolve through `vtmb:dialogue:` the way the spec's own prose reads: the dialogue seam
#: keys a dialogue unit by a path below `dlg/`, which a `sound/...` path never has, and `.vcd`
#: and `.lip` are the scene and sound seams' own keys respectively (see
#: `specDeviations`). `.mdl`/`.vtx` are `vtmb:model:`; `.res`/`.lst`/`.txt`
#: are `vtmb:ui-resource:`.
_ROLE_BY_EXTENSION = {
    ".vmt": "material",
    ".tth": "texture",
    ".ttz": "texture",
    ".vcd": "scene",
    ".lip": "sound",
    ".mdl": "model",
    ".vtx": "model",
    ".res": "ui-resource",
    ".lst": "ui-resource",
    ".txt": "ui-resource",
}

#: The two `.vtx` topology variants the model seam names, each a companion of the same-stem
#: `.mdl` rather than a reference of its own.
_VTX_SUFFIXES = (".dx80.vtx", ".dx7_2bone.vtx")


def _category_role(path: str) -> tuple[str, str] | None:
    lowered = path.lower()
    for suffix, role in _ROLE_BY_EXTENSION.items():
        if lowered.endswith(suffix):
            return role, suffix
    return None


def _normalize_localized_path(path: str) -> str:
    normalized = path.replace("\\", "/").strip().lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    return normalized.strip("/")


def _model_reference(path: str) -> tuple[str, str]:
    """`(sourcePath, key)` for a `.mdl` or `.vtx` path: the `.vtx` variant strips to its `.mdl`
    companion, the model seam's own unit -- the one this project actually publishes for
    both, since no seam exports a `.vtx` on its own."""

    normalized = _normalize_localized_path(path)
    for suffix in _VTX_SUFFIXES:
        if normalized.endswith(suffix):
            normalized = normalized[: -len(suffix)]
            break
    else:
        if normalized.endswith(".mdl"):
            normalized = normalized[: -len(".mdl")]
    if normalized.startswith("models/"):
        normalized = normalized[len("models/"):]
    return f"models/{normalized}.mdl", normalized


def _scene_reference(path: str) -> tuple[str, str]:
    """`(sourcePath, key)` for a `.vcd`: the scene seam's own key, a path below `sound/`."""

    normalized = _normalize_localized_path(path)
    if normalized.startswith("sound/"):
        normalized = normalized[len("sound/"):]
    if normalized.endswith(".vcd"):
        normalized = normalized[: -len(".vcd")]
    return f"sound/{normalized}.vcd", normalized


def _sound_reference(path: str, path_exists: Resolver | None) -> dict[str, Any]:
    """The `vtmb:sound:` reference a `.lip` companion names.

    The sound seam keys a sound unit by its audio path *with* extension and resolves
    mp3-first; a `.lip` alone does not say which of the two the install ships, so both candidates
    are checked in that order and the identity is spelled after whichever one answered. Neither
    resolving is an ordinary reference whose target merely fails to resolve, per the unit
    contract's rule on references between units -- the sound seam's own rules do not
    make the reference unreachable, the install simply lacks the file, so this is `resolved:
    false`, not the `vtmb:missing-<kind>:` sentinel (that namespace is reserved for a reference
    the *referenced kind's own rules* make unreachable to the engine).
    """

    normalized = _normalize_localized_path(path)
    if normalized.startswith("sound/"):
        normalized = normalized[len("sound/"):]
    if normalized.endswith(".lip"):
        normalized = normalized[: -len(".lip")]
    mp3_path = f"sound/{normalized}.mp3"
    wav_path = f"sound/{normalized}.wav"
    has_mp3 = bool(path_exists(mp3_path)) if path_exists is not None else False
    has_wav = bool(path_exists(wav_path)) if path_exists is not None else False
    if has_mp3:
        return {"asset": unit_asset_id("sound", f"{normalized}.mp3"), "sourcePath": mp3_path, "resolved": True}
    if has_wav:
        return {"asset": unit_asset_id("sound", f"{normalized}.wav"), "sourcePath": wav_path, "resolved": True}
    return {
        "asset": unit_asset_id("sound", f"{normalized}.wav"),
        "sourcePath": wav_path,
        "resolved": False,
    }


def decode_localized_list(member: SourceMember, *, path_exists: Resolver | None = None) -> dict[str, Any]:
    text = lexer.decode_text(member.data)
    spans = _line_spans(text)
    ledger = new_ledger(member)

    categories: list[dict[str, Any]] = []
    dependencies: dict[str, dict[str, Any]] = {}
    anomalies: list[dict[str, Any]] = list(_mixed_line_endings(text))
    current: dict[str, Any] | None = None

    for start, content_end, terminator_end in spans:
        content = text[start:content_end]
        stripped = content.strip()
        if not stripped:
            ledger.claim(start, terminator_end - start, "omitted-proven", "whitespace")
            continue
        is_header = "/" not in stripped and "\\" not in stripped
        if is_header:
            ledger.claim(start, content_end - start, "mapped-text", f"categories[{len(categories)}].header")
            current = {"index": len(categories), "name": stripped, "offset": start, "paths": []}
            categories.append(current)
        else:
            if current is None:
                raise EngineConfigDecodeError(
                    f"{member.path}: path {stripped!r} at byte {start} precedes any category header"
                )
            path_index = len(current["paths"])
            ledger.claim(
                start, content_end - start, "mapped-text",
                f"categories[{current['index']}].paths[{path_index}]",
            )
            row: dict[str, Any] = {"index": path_index, "path": stripped}
            role_info = _category_role(stripped)
            if role_info is None:
                # No seam this project publishes owns this extension: the reference is disclosed
                # (`resolved` is always a bool this seam states, per the contract) but not
                # claimed, since inventing a dependency role or a `vtmb:missing-<kind>:` sentinel
                # for a kind that has no seam at all would not be literal either.
                anomalies.append(
                    {"role": "unclassified-localized-path", "offset": start, "path": stripped}
                )
                row["resolved"] = False
            else:
                role, _ = role_info
                if role == "model":
                    source_path, key = _model_reference(stripped)
                    asset = model_asset_id(source_path)
                    resolved = bool(path_exists(source_path)) if path_exists is not None else False
                elif role == "scene":
                    source_path, key = _scene_reference(stripped)
                    asset = unit_asset_id("scene", key)
                    resolved = bool(path_exists(source_path)) if path_exists is not None else False
                elif role == "sound":
                    reference = _sound_reference(stripped, path_exists)
                    asset = reference["asset"]
                    source_path = reference["sourcePath"]
                    resolved = reference["resolved"]
                elif role == "ui-resource":
                    source_path = _normalize_localized_path(stripped)
                    asset = unit_asset_id("ui-resource", source_path)
                    resolved = bool(path_exists(source_path)) if path_exists is not None else False
                else:  # material / texture: unchanged from this decode's original rule
                    resolved = bool(path_exists(stripped)) if path_exists is not None else False
                    asset = _localized_asset_id(role, stripped)
                    source_path = stripped.replace("\\", "/")
                row["role"] = role
                row["asset"] = asset
                row["resolved"] = resolved
                # A reference whose target is another seam's data and merely fails to resolve
                # (a `.lip` companion the install lacks, a texture the corpus never shipped) is
                # an ordinary `dependencies` row, `resolved: false` -- per the unit contract's
                # rule on references between units, the `vtmb:missing-
                # <kind>:` sentinel is reserved for a reference the *referenced kind's own rules*
                # make unreachable to the engine, which none of this seam's roles ever produce.
                dependencies.setdefault(
                    asset, {"role": role, "asset": asset, "sourcePath": source_path, "resolved": resolved}
                )
            current["paths"].append(row)
        if terminator_end > content_end:
            ledger.claim(content_end, terminator_end - content_end, "omitted-proven", "whitespace")

    ledger_row = ledger.finish()
    return {
        "categories": categories,
        "dependencies": list(dependencies.values()),
        "comments": [],
        "anomalies": anomalies,
        "omissions": whitespace_omission(ledger_row, member.data),
        "ledgerRow": ledger_row,
    }


def _localized_asset_id(role: str, path: str) -> str:
    normalized = path.replace("\\", "/").strip().lower()
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    normalized = normalized.strip("/")
    if role == "material" and normalized.startswith("materials/"):
        normalized = normalized[len("materials/"):]
    if role == "material" and normalized.endswith(".vmt"):
        normalized = normalized[: -len(".vmt")]
    if role == "texture":
        for suffix in (".tth", ".ttz"):
            if normalized.endswith(suffix):
                normalized = normalized[: -len(suffix)]
        if normalized.startswith("materials/"):
            normalized = normalized[len("materials/"):]
    return f"vtmb:{role}:{normalized}"


# -------------------------------------------------------------------------------------- binary

def decode_vidcfg(member: SourceMember) -> dict[str, Any]:
    data = member.data
    ledger = new_ledger(member)
    typed_unidentified: list[dict[str, Any]] = []
    if len(data) == 20:
        ledger.claim(0, 20, "mapped", "binary.bytes")
        guid = data[:16].hex()
        (trailing,) = struct.unpack_from("<I", data, 16)
        candidate = {"guid": guid, "trailingValue": trailing}
        typed_unidentified.append(
            {
                "offset": 0, "length": 20, "sha256": hashlib.sha256(data).hexdigest(),
                "reason": "vidcfg.bin: candidate 16-byte GUID + 32-bit value; the engine.dll "
                          "writer is not yet identified",
            }
        )
    else:
        ledger.claim(0, len(data), "mapped", "binary.bytes")
        candidate = None
        typed_unidentified.append(
            {
                "offset": 0, "length": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                "reason": "vidcfg.bin: unexpected length; the seam's 20-byte reading does not apply",
            }
        )
    ledger_row = ledger.finish()
    return {
        "binary": {"bytes": data.hex(), "byteLength": len(data), "candidateReading": candidate},
        "dependencies": [],
        "comments": [],
        "anomalies": [],
        "omissions": [] if data else [{"role": "empty-member"}],
        "typedUnidentified": typed_unidentified,
        "ledgerRow": ledger_row,
    }


def decode_voice_ban(member: SourceMember) -> dict[str, Any]:
    data = member.data
    ledger = new_ledger(member)
    typed_unidentified: list[dict[str, Any]] = []
    omissions: list[dict[str, Any]] = []
    if len(data) >= 4:
        (count,) = struct.unpack_from("<i", data, 0)
        ledger.claim(0, 4, "mapped", "binary.bytes")
        trailing = data[4:]
        if trailing:
            if any(trailing):
                # Mirrors `savefragment.claim_trailing_fill`: non-zero bytes past the last
                # decoded record are `omitted-proven`, with the evidence on the omissions row
                # itself, not `mapped`+`typedUnidentified`.
                ledger.claim(4, len(trailing), "omitted-proven", "binary.trailing")
                omissions.append(
                    {
                        "role": "trailing-fill", "offset": 4, "length": len(trailing),
                        "sha256": hashlib.sha256(trailing).hexdigest(),
                    }
                )
            else:
                ledger.claim(4, len(trailing), "padding-zero", "binary.trailing")
        candidate = {"count": count}
        typed_unidentified.append(
            {
                "offset": 0, "length": 4, "sha256": hashlib.sha256(data[:4]).hexdigest(),
                "reason": "voice_ban.dt: candidate int32 count; the writer is not yet identified",
            }
        )
    else:
        ledger.claim(0, len(data), "mapped", "binary.bytes")
        candidate = None
        if data:
            typed_unidentified.append(
                {
                    "offset": 0, "length": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                    "reason": "voice_ban.dt: shorter than the declared count field",
                }
            )
        else:
            omissions.append({"role": "empty-member"})
    ledger_row = ledger.finish()
    return {
        "binary": {"bytes": data.hex(), "byteLength": len(data), "candidateReading": candidate},
        "dependencies": [],
        "comments": [],
        "anomalies": [],
        "omissions": omissions,
        "typedUnidentified": typed_unidentified,
        "ledgerRow": ledger_row,
    }


# --------------------------------------------------------------------------------- save-fragment

def decode_hl2_tmp(member: SourceMember, *, map_exists: Resolver | None = None) -> dict[str, Any]:
    try:
        result = savefragment.decode_save_fragment(member.path, member.data)
    except savefragment.SaveFragmentDecodeError as error:
        raise EngineConfigDecodeError(str(error)) from error

    dependencies = []
    for row in result["dependencyCandidates"]:
        stem = map_stem(row["sourcePath"])
        asset = map_asset_id(stem)
        resolved = bool(map_exists(row["sourcePath"])) if map_exists is not None else False
        dependencies.append(
            {"role": "map", "asset": asset, "sourcePath": row["sourcePath"], "resolved": resolved}
        )

    return {
        "saveFragment": result["saveFragment"],
        "dependencies": dependencies,
        "comments": [],
        "anomalies": [],
        "omissions": result["omissions"],
        "typedUnidentified": result["typedUnidentified"],
        "ledgerRow": result["ledgerRow"],
    }


# ------------------------------------------------------------------------------------ dispatch

def decode_console_script(member: SourceMember, *, exec_exists: Resolver | None = None) -> dict[str, Any]:
    result = console.parse_console_script(member.path, member.data)
    dependencies: dict[str, dict[str, Any]] = {}
    for command in result["commands"]:
        if command["name"].strip().lower() != "exec" or not command["args"]:
            continue
        target = command["args"][0]["text"]
        source_path = engine_config_source_path(target)
        asset = dependency_asset_id(source_path)
        resolved = bool(exec_exists(source_path)) if exec_exists is not None else False
        dependencies.setdefault(
            asset, {"role": "engine-config", "asset": asset, "sourcePath": source_path, "resolved": resolved}
        )
    result["dependencies"] = list(dependencies.values())
    return result


def decode_engine_config(
    closure,
    *,
    resolver: Resolver | None = None,
) -> "EngineConfigModel":
    """The complete decode of one engine-config unit, dispatched on its grammar.

    `resolver` answers whether the UP-first install index holds a member for an install-relative
    path; every dependency role in this seam resolves through the one callback.
    """

    from elysium_pipeline.formats.engine_config_glb.model import (
        GRAMMAR_BINARY,
        GRAMMAR_CONSOLE_SCRIPT,
        GRAMMAR_KEY_EQUALS_VALUE,
        GRAMMAR_KEYVALUES,
        GRAMMAR_LINE_LIST,
        GRAMMAR_LOCALIZED_LIST,
        GRAMMAR_RAD_ROWS,
        GRAMMAR_SAVE_FRAGMENT,
        EngineConfigModel,
    )

    member = closure.member
    grammar = closure.grammar

    if grammar == GRAMMAR_CONSOLE_SCRIPT:
        result = decode_console_script(member, exec_exists=resolver)
        kwargs = dict(
            commands=result["commands"], bindings=result["bindings"], aliases=result["aliases"],
            cvars=result["cvars"], script_expressions=result["scriptExpressions"],
        )
    elif grammar == GRAMMAR_RAD_ROWS:
        result = decode_lights_rad(member, material_exists=resolver)
        kwargs = dict(texture_lights=result["textureLights"])
    elif grammar == GRAMMAR_KEYVALUES:
        result = decode_detail_vbsp(member, model_exists=resolver)
        kwargs = dict(detail_types=result["detailTypes"])
    elif grammar == GRAMMAR_LINE_LIST:
        result = decode_loadorder(member, map_exists=resolver)
        kwargs = dict(maps=result["maps"])
    elif grammar == GRAMMAR_KEY_EQUALS_VALUE:
        result = decode_pack_values(member, engine_config_exists=resolver)
        kwargs = dict(packer=result["packer"], packer_keys=result["packerKeys"])
    elif grammar == GRAMMAR_LOCALIZED_LIST:
        result = decode_localized_list(member, path_exists=resolver)
        kwargs = dict(categories=result["categories"])
    elif grammar == GRAMMAR_BINARY:
        result = (decode_vidcfg if closure.key.endswith("vidcfg.bin") else decode_voice_ban)(member)
        kwargs = dict(binary=result["binary"])
    elif grammar == GRAMMAR_SAVE_FRAGMENT:
        result = decode_hl2_tmp(member, map_exists=resolver)
        kwargs = dict(save_fragment=result["saveFragment"])
    else:  # pragma: no cover - `grammar_for` already validated the key
        raise EngineConfigDecodeError(f"unknown engine-config grammar {grammar!r}")

    return EngineConfigModel(
        key=closure.key,
        asset=closure.asset,
        grammar=grammar,
        member=member,
        ledger_row=result["ledgerRow"],
        dependencies=result.get("dependencies", []),
        comments=result.get("comments", []),
        anomalies=result.get("anomalies", []),
        omissions=result.get("omissions", []),
        typed_unidentified=result.get("typedUnidentified", []),
        **kwargs,
    )
