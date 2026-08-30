"""Isolated one-file/one-GLB ui-resource product writer.

Every unit is scene-less and carries no BIN chunk: a ui-resource member names strings, flags,
numbers and geometry-free layout data, all of which the source wrote as text
(`docs/architecture/seam_map_ui_resource.md`).
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable

from elysium_pipeline.formats.unit_contract import (
    asset_block,
    extension_root,
    identity_block,
    plain,
    source_resolution,
    write_glb,
)
from elysium_pipeline.formats.ui_resource_glb import (
    KIND_TITLE,
    SCHEMA_VERSION,
    UI_RESOURCE_EXTENSION,
    decode_ui_resource,
    load_source_closure,
    output_relative_path,
)
from elysium_pipeline.formats.ui_resource_glb import source_keys as _source_keys  # re-export
from elysium_pipeline.formats.ui_resource_glb.coverage import coverage_block, mapped_fields
from elysium_pipeline.formats.ui_resource_glb.decode import Resolvers
from elysium_pipeline.formats.ui_resource_glb.model import UiResourceModel

#: Re-exported so the plural export command can enumerate this seam's units from an index without
#: importing `formats.ui_resource_glb` directly.
source_keys = _source_keys


class UiResourceGlbError(RuntimeError):
    pass


def build_document(model: UiResourceModel) -> tuple[dict, bytes]:
    identity_extra: dict = {"category": model.category, "dormant": model.dormant}
    if model.dormant_evidence:
        identity_extra["dormantEvidence"] = model.dormant_evidence
    identity = identity_block(model.asset, model.member.path, **identity_extra)
    root = extension_root(
        schema_version=SCHEMA_VERSION,
        identity=identity,
        source_resolution=source_resolution([model.member]),
        dependencies=model.dependencies,
        coverage=coverage_block(
            mapped=mapped_fields(model),
            ledger_row=model.ledger_row,
            typed_unidentified=model.typed_unidentified,
            omitted_proven=model.omitted_proven,
            unresolved=model.unresolved,
            unsupported=model.unsupported,
        ),
        encoding=model.encoding,
        grammar=model.grammar,
        tree=model.tree,
        scheme=model.scheme,
        layout=model.layout,
        menu=model.menu,
        hud=model.hud,
        titles=model.titles,
        rows=model.rows,
        substitutions=model.substitutions,
        strings=model.strings,
        options=model.options,
        menuScene=model.menu_scene,
        comments=model.comments,
        anomalies=model.anomalies,
        omissions=model.omissions,
    )
    document = {
        "asset": asset_block(KIND_TITLE),
        "extensionsUsed": [UI_RESOURCE_EXTENSION],
        "extensionsRequired": [UI_RESOURCE_EXTENSION],
        "extensions": {UI_RESOURCE_EXTENSION: plain(root)},
    }
    # A ui-resource unit carries no binary payload: every datum it owns is a name, a flag, a
    # number or layout text the source wrote as plain bytes. One JSON chunk, no BIN chunk.
    return document, b""


def _material_resolver(index: dict) -> Callable[[str], bool]:
    def resolve(normalized: str) -> bool:
        if not normalized:
            return False
        return any(f"materials/{normalized}.{ext}" in index for ext in ("vmt", "vtf"))

    return resolve


def _font_resolver(index: dict) -> Callable[[str], bool]:
    """Whether the UP-first index holds the `.fnt` a scheme tier's composed `vtmb:font:` key names.

    The font seam owns both the key rule and the file layout, so this asks it: a tier that matches
    no `.fnt` is `resolved: false` and warns (`seam_map_ui_resource.md`, "Dependencies"), which is
    the engine rasterizing from the installed system face.
    """

    from elysium_pipeline.formats.font_glb.model import FontModelError, font_path

    def resolve(stem: str) -> bool:
        try:
            return font_path(stem) in index
        except FontModelError:
            return False

    return resolve


def _texture_resolver(index: dict) -> Callable[[str], bool]:
    def resolve(normalized: str) -> bool:
        return bool(normalized) and f"materials/{normalized}.vmt" in index

    return resolve


def _sound_resolver(index: dict) -> Callable[[str], bool]:
    def resolve(normalized: str) -> bool:
        if not normalized:
            return False
        return normalized in index or f"sound/{normalized}" in index

    return resolve


def _particle_resolver(index: dict) -> Callable[[str], bool]:
    """Whether the UP-first index holds the `particles/<emitter>.txt` an emitter names -- the
    particle seam's own source layout."""

    def resolve(normalized: str) -> bool:
        return bool(normalized) and f"particles/{normalized}.txt" in index

    return resolve


def _ui_resource_resolver(index: dict) -> Callable[[str], bool]:
    from elysium_pipeline.formats.ui_resource_glb.model import classify

    def resolve(normalized: str) -> bool:
        return normalized in index and classify(normalized) is not None

    return resolve


def _gameui_token_resolver(index: dict, read_bytes) -> Callable[[str], bool]:
    from elysium_pipeline.formats.ui_resource_glb import lexer

    def load_tokens() -> frozenset[str]:
        path = "resource/gameui_english.txt"
        if path not in index:
            return frozenset()
        data = read_bytes(index, path) if read_bytes else None
        if data is None:
            from elysium_pipeline.formats import install

            data = install.read(index, path)
        if data is None:
            return frozenset()
        text = lexer.decode_text(data, "utf-16-le")
        tokens = lexer.tokenize(text, "utf-16-le")
        top_nodes, _, _, _ = lexer.parse_tree(tokens, text)
        names: set[str] = set()
        for _, node in lexer.walk(top_nodes):
            if node["kind"] == "scalar" and node["key"] != "language":
                names.add(node["key"])
        return frozenset(names)

    tokens_holder: dict[str, frozenset[str]] = {}

    def resolve(name: str) -> bool:
        if "tokens" not in tokens_holder:
            tokens_holder["tokens"] = load_tokens()
        return name.strip().lower() in tokens_holder["tokens"]

    return resolve


def _substitution_defs_resolver(index: dict, read_bytes) -> Callable[[str], dict[str, str] | None]:
    from elysium_pipeline.formats.ui_resource_glb import lexer
    from elysium_pipeline.formats.ui_resource_glb.model import normalize_key

    cache: dict[str, dict[str, str] | None] = {}

    def resolve(argument: str) -> dict[str, str] | None:
        key = normalize_key(argument)
        if key in cache:
            return cache[key]
        if key not in index:
            cache[key] = None
            return None
        data = read_bytes(index, key) if read_bytes else None
        if data is None:
            from elysium_pipeline.formats import install

            data = install.read(index, key)
        if data is None:
            cache[key] = None
            return None
        text = lexer.decode_text(data, "latin-1")
        tokens = lexer.tokenize(text, "latin-1")
        top_nodes, _, _, _ = lexer.parse_tree(tokens, text)
        defs = {
            node["sourceKey"]: lexer.decode_escapes(node["value"], node["escapes"])
            for node in top_nodes
            if node["kind"] == "scalar" and node["sourceKey"].startswith("$")
        }
        cache[key] = defs
        return defs

    return resolve


def _resolvers(index: dict, read_bytes) -> Resolvers:
    return Resolvers(
        material_exists=_material_resolver(index),
        font_exists=_font_resolver(index),
        texture_exists=_texture_resolver(index),
        particle_exists=_particle_resolver(index),
        sound_exists=_sound_resolver(index),
        ui_resource_exists=_ui_resource_resolver(index),
        gameui_token_exists=_gameui_token_resolver(index, read_bytes),
        substitution_defs=_substitution_defs_resolver(index, read_bytes),
    )


def export(
    index: dict,
    key: str,
    output_root: Path,
    *,
    read_bytes=None,
) -> Path:
    """Write and validate one ui-resource unit."""

    closure = load_source_closure(index, key, read_bytes=read_bytes)
    model = decode_ui_resource(closure, resolvers=_resolvers(index, read_bytes))
    document, binary = build_document(model)
    from elysium_pipeline.validation import ui_resource_glb as validation

    validation.validate_document(document, binary, source_members=closure.members())
    destination = output_root / Path(*output_relative_path(closure.key).parts)
    write_glb(document, binary, destination)
    return destination
