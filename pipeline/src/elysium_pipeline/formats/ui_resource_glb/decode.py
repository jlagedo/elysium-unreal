"""Decode one ui-resource unit: the `keyvalues`-grammar categories.

`seam_map_ui_resource.md` names seven grammars; the four line/row grammars (`tab-rows`,
`titles`, `settings-scr`, `line-list`, `won-lists`, `key-value-lines`) are decoded by
`grammars.py`. This module owns the `keyvalues` grammar, which every `.res` scheme and layout,
the VGUI1 dialog scripts, the HUD sprite tables, the launcher/game substitution scripts, the
localized string table and the main-menu particle scene all share -- and dispatches to
`grammars.py` for the rest.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable

from elysium_pipeline.formats.unit_contract.references import asset_id as _ref_asset_id
from elysium_pipeline.formats.unit_contract.references import dependency
from elysium_pipeline.formats.ui_resource_glb import grammars, lexer
from elysium_pipeline.formats.ui_resource_glb import coverage as coverage_module
from elysium_pipeline.formats.ui_resource_glb.coverage import new_ledger
from elysium_pipeline.formats.ui_resource_glb.model import (
    UiResourceModel,
    asset_id as ui_resource_asset_id,
)
from elysium_pipeline.formats.ui_resource_glb.source import UiResourceSourceClosure

PathExists = Callable[[str], bool]
SubstitutionLookup = Callable[[str], dict[str, str] | None]


class UiResourceDecodeError(RuntimeError):
    """The selected ui-resource source cannot be decoded."""


@dataclass(frozen=True, slots=True)
class Resolvers:
    """The install-dependent lookups a decode may use to state whether a reference resolves.

    Every field defaults to `None`: without a resolver a reference still publishes its
    dependency row, just with `resolved: False`, which is exactly what an isolated test (no
    install present) should see.
    """

    material_exists: PathExists | None = None
    font_exists: PathExists | None = None
    texture_exists: PathExists | None = None
    particle_exists: PathExists | None = None
    sound_exists: PathExists | None = None
    ui_resource_exists: PathExists | None = None
    gameui_token_exists: PathExists | None = None
    substitution_defs: SubstitutionLookup | None = None


# --------------------------------------------------------------------------------------------
# Generic tree helpers (the borrowed vdata tree shape: `kind` is `scalar`, `block` or `directive`)
# --------------------------------------------------------------------------------------------


def find_block(nodes: list[dict], key: str) -> dict | None:
    folded = key.strip().lower()
    for node in nodes:
        if node["kind"] == "block" and node["key"] == folded:
            return node
    return None


def root_children(top_nodes: list[dict], expected_key: str) -> list[dict]:
    """The children of the named wrapper block, falling back to the first top-level block, and
    to the bare top-level node list when the file opens with no block at all (a malformed or
    minimal fixture) -- so a typed projection never indexes into a scalar node's missing
    `children` key."""

    root = find_block(top_nodes, expected_key)
    if root is None and top_nodes and top_nodes[0]["kind"] == "block":
        root = top_nodes[0]
    return root["children"] if root is not None else top_nodes


def scalars_last(nodes: list[dict]) -> dict[str, dict]:
    """Every direct scalar child, keyed by its folded name, last occurrence winning (KeyValues'
    own resolution rule for a repeated key)."""

    result: dict[str, dict] = {}
    for node in nodes:
        if node["kind"] == "scalar":
            result[node["key"]] = node
    return result


def blocks_by_key(nodes: list[dict]) -> dict[str, list[dict]]:
    result: dict[str, list[dict]] = {}
    for node in nodes:
        if node["kind"] == "block":
            result.setdefault(node["key"], []).append(node)
    return result


def scalar_text(node: dict | None) -> str | None:
    if node is None:
        return None
    return lexer.decode_escapes(node["value"], node["escapes"])


def scalar_int(node: dict | None) -> int | None:
    text = scalar_text(node)
    if text is None:
        return None
    try:
        return int(float(text.strip().strip('"')))
    except ValueError:
        return None


def _reshape(nodes: list[dict]) -> Any:
    """A faithful, plain reshape of a sibling list: repeated keys collect into a list."""

    result: dict[str, Any] = {}
    for node in nodes:
        if node["kind"] == "directive":
            continue
        value: Any = _reshape(node["children"]) if node["kind"] == "block" else scalar_text(node)
        key = node["sourceKey"]
        if key in result:
            existing = result[key]
            if isinstance(existing, list):
                existing.append(value)
            else:
                result[key] = [existing, value]
        else:
            result[key] = value
    return result


# --------------------------------------------------------------------------------------------
# Universal cross-references any keyvalues-grammar tree may carry
# --------------------------------------------------------------------------------------------


def _material_dependency(raw: str) -> tuple[str, str] | None:
    """`(asset, normalizedPath)` for a `material`/`image`/`file` value naming art below
    `materials/`, or `None` for an empty or already-symbolic value."""

    if not raw or raw.startswith("#"):
        return None
    normalized = raw.replace("\\", "/").strip().lower()
    if normalized.startswith("materials/"):
        normalized = normalized[len("materials/"):]
    if not normalized:
        return None
    return _ref_asset_id("material", normalized), normalized


def _scan_material_and_token_references(
    top_nodes: list[dict],
    *,
    resolve_material: PathExists | None,
    resolve_gameui_token: PathExists | None,
) -> list[dict[str, Any]]:
    """Every `material`/`image`/`file` key and every `#GameUI_*` token value in the whole tree.

    `seam_map_ui_resource.md`'s "Dependencies" table states both rules with no category
    qualifier, so this scan runs once over the whole tree rather than once per typed projection.
    """

    dependencies: list[dict[str, Any]] = []
    gameui_asset = ui_resource_asset_id("resource/gameui_english.txt")
    for _, node in lexer.walk(top_nodes):
        if node["kind"] != "scalar":
            continue
        raw = scalar_text(node)
        if raw is None:
            continue
        if node["key"] in ("material", "image", "file"):
            found = _material_dependency(raw)
            if found is not None:
                asset, normalized = found
                resolved = bool(resolve_material(normalized)) if resolve_material else False
                dependencies.append(dependency("material", asset, raw, resolved))
        stripped = raw.strip().strip('"')
        if stripped.startswith("#") and len(stripped) > 1:
            token = stripped[1:]
            resolved = bool(resolve_gameui_token(token)) if resolve_gameui_token else False
            dependencies.append(dependency("ui-resource", gameui_asset, stripped, resolved))
    return dependencies


def _scan_directives(
    top_nodes: list[dict], *, resolve_ui_resource: PathExists | None
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]]]:
    """`#include`/`#base` -> a `ui-resource` dependency on the named unit, which is this unit's
    own structure and so enters `unresolved` (failing the unit) when the target is absent. Any
    other directive name is carried in `typedUnidentified` rather than guessed at."""

    dependencies: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []
    typed_unidentified: list[dict[str, Any]] = []
    for path, node in lexer.walk(top_nodes):
        if node["kind"] != "directive":
            continue
        name = node.get("name")
        argument = node.get("argument")
        if name in ("include", "base") and argument:
            normalized = argument.replace("\\", "/").strip().lower()
            asset = ui_resource_asset_id(normalized)
            resolved = bool(resolve_ui_resource(normalized)) if resolve_ui_resource else False
            dependencies.append(dependency("ui-resource", asset, argument, resolved))
            if resolve_ui_resource is not None and not resolved:
                unresolved.append({"path": path, "directive": name, "target": argument})
        else:
            typed_unidentified.append(
                {"field": node.get("sourceKey"), "offset": node["offset"], "path": path,
                 "reason": "unrecognized-directive"}
            )
    return dependencies, unresolved, typed_unidentified


# --------------------------------------------------------------------------------------------
# Typed projections
# --------------------------------------------------------------------------------------------


def _font_join_key(fields: dict[str, Any]) -> str:
    """The face/size/weight/flags join key a scheme `Fonts` tier states
    (`seam_map_ui_resource.md`, "Dependencies": "joined to a `vtmb:font:` unit by face, size,
    weight and flags")."""

    face = str(fields.get("name") or "").strip().lower()
    tall = str(fields.get("tall") or "").strip()
    weight = str(fields.get("weight") or "").strip()
    flags = "u" if str(fields.get("underline") or "0").strip() not in ("", "0") else ""
    flags += "i" if str(fields.get("italic") or "0").strip() not in ("", "0") else ""
    return f"{face}:{tall}:{weight}:{flags}"


def _project_scheme(
    top_nodes: list[dict], *, resolve_font: PathExists | None
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    children = root_children(top_nodes, "scheme")
    blocks = blocks_by_key(children)
    dependencies: list[dict[str, Any]] = []

    colors = []
    for scalar in (blocks.get("colors", [{"children": []}])[0]).get("children", []):
        if scalar["kind"] != "scalar":
            continue
        raw = scalar_text(scalar) or ""
        parts = raw.split()
        rgba = None
        if len(parts) == 4 and all(part.lstrip("-").isdigit() for part in parts):
            rgba = [int(part) for part in parts]
        colors.append({"name": scalar["sourceKey"], "raw": raw, "rgba": rgba, "offset": scalar["offset"]})
    color_by_name = {row["name"].strip().lower(): row for row in colors}

    base_settings = []
    for scalar in (blocks.get("basesettings", [{"children": []}])[0]).get("children", []):
        if scalar["kind"] != "scalar":
            continue
        raw = scalar_text(scalar) or ""
        resolved_color = color_by_name.get(raw.strip().lower())
        base_settings.append(
            {
                "name": scalar["sourceKey"],
                "value": raw,
                "resolvesTo": resolved_color,
                "offset": scalar["offset"],
            }
        )

    fonts = []
    for alias_block in blocks.get("fonts", [{"children": []}])[0].get("children", []):
        if alias_block["kind"] != "block":
            continue
        tiers = []
        for tier_block in alias_block["children"]:
            if tier_block["kind"] != "block":
                continue
            fields = _reshape(tier_block["children"])
            join_key = _font_join_key(fields)
            asset = _ref_asset_id("font", join_key)
            resolved = bool(resolve_font(join_key)) if resolve_font else False
            tiers.append(
                {"tier": tier_block["sourceKey"], "fields": fields, "asset": asset,
                 "resolved": resolved}
            )
            dependencies.append(dependency("font", asset, join_key, resolved))
        fonts.append({"alias": alias_block["sourceKey"], "tiers": tiers})

    borders = []
    for border_block in blocks.get("borders", [{"children": []}])[0].get("children", []):
        if border_block["kind"] != "block":
            continue
        borders.append({"name": border_block["sourceKey"], "fields": _reshape(border_block["children"])})

    scheme = {"colors": colors, "baseSettings": base_settings, "fonts": fonts, "borders": borders}
    return scheme, dependencies


def _control(node: dict) -> dict[str, Any]:
    scalars = scalars_last(node["children"])
    known = {"controlname", "fieldname", "xpos", "ypos", "wide", "tall"}
    fields = {
        child["sourceKey"]: scalar_text(child)
        for child in node["children"]
        if child["kind"] == "scalar" and child["key"] not in known
    }
    children = [_control(child) for child in node["children"] if child["kind"] == "block"]
    return {
        "name": node["sourceKey"],
        "fieldName": scalar_text(scalars.get("fieldname")),
        "controlName": scalar_text(scalars.get("controlname")),
        "position": {"x": scalar_int(scalars.get("xpos")), "y": scalar_int(scalars.get("ypos"))},
        "size": {"wide": scalar_int(scalars.get("wide")), "tall": scalar_int(scalars.get("tall"))},
        "fields": fields,
        "children": children,
        "offset": node["offset"],
    }


def _project_layout(top_nodes: list[dict]) -> dict[str, Any]:
    if len(top_nodes) == 1 and top_nodes[0]["kind"] == "block":
        wrapper = top_nodes[0]
        control_nodes = wrapper["children"]
        wrapper_name = wrapper["sourceKey"]
    else:
        control_nodes = top_nodes
        wrapper_name = None
    controls = [_control(node) for node in control_nodes if node["kind"] == "block"]
    return {"wrapperName": wrapper_name, "controls": controls}


def _menu_items(nodes: list[dict]) -> list[dict[str, Any]]:
    items = []
    for node in nodes:
        if node["kind"] != "block":
            continue
        scalars = scalars_last(node["children"])
        submenu = find_block(node["children"], "submenu")
        items.append(
            {
                "index": node["sourceKey"],
                "name": scalar_text(scalars.get("name")),
                "label": scalar_text(scalars.get("label")),
                "command": scalar_text(scalars.get("command")),
                "subMenu": _menu_items(submenu["children"]) if submenu is not None else None,
                "offset": node["offset"],
            }
        )
    return items


def _project_menu(top_nodes: list[dict]) -> dict[str, Any]:
    children = root_children(top_nodes, "gamemenu")
    return {"items": _menu_items(children)}


def _project_hud(top_nodes: list[dict]) -> dict[str, Any]:
    """The HUD sprite table. A sprite's `file` key is a `material`/`image`/`file` scalar like any
    other in the tree, so `_scan_material_and_token_references`'s whole-tree scan already
    publishes its dependency row; this projection only names the resolved asset on the sprite
    itself, rather than publishing a second, duplicate row for the same reference."""

    if len(top_nodes) == 1 and top_nodes[0]["kind"] == "block":
        top_nodes = top_nodes[0]["children"]
    sprite_data = find_block(top_nodes, "spritedata")
    sprites: list[dict[str, Any]] = []
    for name_block in (sprite_data["children"] if sprite_data is not None else []):
        if name_block["kind"] != "block":
            continue
        for resolution_block in name_block["children"]:
            if resolution_block["kind"] != "block":
                continue
            scalars = scalars_last(resolution_block["children"])
            file_value = scalar_text(scalars.get("file"))
            found = _material_dependency(file_value) if file_value else None
            asset = found[0] if found else None
            sprites.append(
                {
                    "name": name_block["sourceKey"],
                    "resolution": resolution_block["sourceKey"],
                    "file": file_value,
                    "asset": asset,
                    "x": scalar_int(scalars.get("x")),
                    "y": scalar_int(scalars.get("y")),
                    "width": scalar_int(scalars.get("width")),
                    "height": scalar_int(scalars.get("height")),
                    "offset": resolution_block["offset"],
                }
            )
    return {"sprites": sprites}


_SKYBOX_FACES = ("bk", "dn", "ft", "lf", "rt", "up")


def _project_menu_scene(
    top_nodes: list[dict],
    *,
    resolve_texture: PathExists | None,
    resolve_sound: PathExists | None,
    resolve_particle: PathExists | None,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    children = root_children(top_nodes, "mainmenuparticles")
    scalars = scalars_last(children)
    dependencies: list[dict[str, Any]] = []

    skybox_raw = scalar_text(scalars.get("default_skybox"))
    skybox_faces = []
    if skybox_raw:
        base = skybox_raw.replace("\\", "/").strip().lower()
        for face in _SKYBOX_FACES:
            path = f"skybox/{base}{face}"
            asset = _ref_asset_id("texture", path)
            resolved = bool(resolve_texture(path)) if resolve_texture else False
            skybox_faces.append({"face": face, "path": path, "asset": asset, "resolved": resolved})
            # `dependencies[].sourcePath` is the install-relative path the reference resolves to
            # (seam_map_unit_contract.md, "References between units"), not the bare join key `path`
            # already carries in `skyboxFaces[].path`/`asset` -- the skybox face convention names a
            # `.vmt` under `materials/`.
            dependencies.append(dependency("texture", asset, f"materials/{path}.vmt", resolved))

    music_raw = scalar_text(scalars.get("music"))
    music = None
    if music_raw:
        normalized = music_raw.replace("\\", "/").strip().lower()
        asset = _ref_asset_id("sound", normalized)
        resolved = bool(resolve_sound(normalized)) if resolve_sound else False
        music = {"path": music_raw, "asset": asset, "resolved": resolved}
        dependencies.append(dependency("sound", asset, music_raw, resolved))

    particles = []
    for block in blocks_by_key(children).get("particle", []):
        block_scalars = scalars_last(block["children"])
        emitter_raw = scalar_text(block_scalars.get("emitter"))
        entry = {
            "emitter": emitter_raw,
            "origin": scalar_text(block_scalars.get("origin")),
            "angle": scalar_text(block_scalars.get("angle")),
            "offset": block["offset"],
        }
        if emitter_raw:
            normalized = emitter_raw.strip().lower()
            asset = _ref_asset_id("particle", normalized)
            resolved = bool(resolve_particle(normalized)) if resolve_particle else False
            entry["asset"] = asset
            dependencies.append(dependency("particle", asset, emitter_raw, resolved))
        particles.append(entry)

    scene = {
        "cameraFov": scalar_text(scalars.get("camera_fov")),
        "cameraNear": scalar_text(scalars.get("camera_near")),
        "cameraFar": scalar_text(scalars.get("camera_far")),
        "cameraRotation": scalar_text(scalars.get("camera_rotation")),
        "defaultSkybox": skybox_raw,
        "skyboxFaces": skybox_faces,
        "music": music,
        "particles": particles,
    }
    return scene, dependencies


def _project_substitutions(top_nodes: list[dict], *, resolvers: Resolvers) -> dict[str, Any]:
    definitions: dict[str, str] = {}
    entries: list[dict[str, Any]] = []
    for node in top_nodes:
        if node["kind"] != "scalar":
            continue
        raw_key = node["sourceKey"]
        value = scalar_text(node) or ""
        if raw_key.startswith("$"):
            definitions[raw_key] = value
        entries.append({"sourceKey": raw_key, "value": value, "offset": node["offset"]})

    merged = dict(definitions)
    # Pull in `#include`d definitions (`scripts/launcher.txt` includes `scripts/game.txt`, which
    # defines `$game`) so a substituted value is complete without a second export pass.
    for _, node in lexer.walk(top_nodes):
        if node["kind"] == "directive" and node.get("name") in ("include", "base") and node.get("argument"):
            included = resolvers.substitution_defs(node["argument"]) if resolvers.substitution_defs else None
            if included:
                merged = {**included, **merged}

    strings = []
    for entry in entries:
        if entry["sourceKey"].startswith("$"):
            continue
        substituted = entry["value"]
        for def_key, def_value in merged.items():
            substituted = substituted.replace(def_key, def_value)
        strings.append(
            {
                "key": entry["sourceKey"],
                "raw": entry["value"],
                "substituted": substituted,
                "offset": entry["offset"],
            }
        )

    definitions_list = [{"key": k, "value": v} for k, v in definitions.items()]
    return {"definitions": definitions_list, "strings": strings}


def _project_strings(top_nodes: list[dict]) -> dict[str, Any]:
    children = root_children(top_nodes, "lang")
    scalars = scalars_last(children)
    language = scalar_text(scalars.get("language"))
    tokens_block = find_block(children, "tokens")
    tokens = []
    for node in (tokens_block["children"] if tokens_block is not None else []):
        if node["kind"] != "scalar":
            continue
        tokens.append(
            {
                "name": node["sourceKey"],
                "text": scalar_text(node),
                "byteOffset": node["offset"],
                "byteLength": node["length"],
                "codeUnitOffset": node["offset"] // 2,
                "codeUnitLength": node["length"] // 2,
            }
        )
    return {"language": language, "tokens": tokens}


# --------------------------------------------------------------------------------------------
# Top-level decode
# --------------------------------------------------------------------------------------------


def _dedupe_dependencies(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """One dependency row per distinct `(role, asset, sourcePath)`, first occurrence's `resolved`
    kept (every occurrence is computed from the same resolver, so they always agree).

    A reference scanned once per occurrence in the source (a repeated `#GameUI_*` token, a HUD
    sprite's `file` key that is also picked up by the whole-tree material/token scan) would
    otherwise publish the same dependency many times over, which `seam_map_unit_contract.md`'s
    "Every reference the unit makes is declared once in `dependencies`" forbids.
    """

    seen: dict[tuple[str, str, str], dict[str, Any]] = {}
    for row in rows:
        key = (row["role"], row["asset"], row["sourcePath"])
        if key not in seen:
            seen[key] = row
    return list(seen.values())


def _decode_keyvalues(
    closure: UiResourceSourceClosure, resolvers: Resolvers
) -> UiResourceModel:
    member = closure.member
    text = lexer.decode_text(member.data, closure.encoding)
    tokens = lexer.tokenize(text, closure.encoding)
    top_nodes, tree_anomalies, claims, unparsed_offset = lexer.parse_tree(tokens, text)

    ledger = new_ledger(member)
    for offset, length, state, owner in claims:
        ledger.claim(offset, length, state, owner)

    comments = []
    whitespace_total = 0
    for token in tokens:
        if token.kind == "comment":
            ledger.claim(token.offset, token.length, "mapped-text", f"comments[{len(comments)}]")
            comments.append({"index": len(comments), "offset": token.offset, "length": token.length,
                              "text": token.raw})
        elif token.kind == "whitespace":
            ledger.claim(token.offset, token.length, "omitted-proven", "whitespace")
            whitespace_total += token.length
        elif token.kind == "bom":
            ledger.claim(token.offset, token.length, "mapped", "bom")

    anomalies = list(tree_anomalies)
    anomalies.extend(lexer.source_anomalies(member.data, text, closure.encoding))

    material_deps = _scan_material_and_token_references(
        top_nodes,
        resolve_material=resolvers.material_exists,
        resolve_gameui_token=resolvers.gameui_token_exists,
    )
    directive_deps, unresolved, directive_typed = _scan_directives(
        top_nodes, resolve_ui_resource=resolvers.ui_resource_exists
    )
    dependencies = directive_deps + material_deps
    typed_unidentified = list(directive_typed)

    scheme = layout = menu = hud = substitutions = strings = menu_scene = None
    unsupported: list[dict[str, Any]] = []

    if closure.category == "scheme":
        scheme, scheme_deps = _project_scheme(top_nodes, resolve_font=resolvers.font_exists)
        dependencies = dependencies + scheme_deps
    elif closure.category == "layout":
        layout = _project_layout(top_nodes)
    elif closure.category == "menu":
        menu = _project_menu(top_nodes)
    elif closure.category == "hud":
        hud = _project_hud(top_nodes)
    elif closure.category == "menuScene":
        menu_scene, scene_deps = _project_menu_scene(
            top_nodes,
            resolve_texture=resolvers.texture_exists,
            resolve_sound=resolvers.sound_exists,
            resolve_particle=resolvers.particle_exists,
        )
        dependencies = dependencies + scene_deps
    elif closure.category == "substitutions":
        substitutions = _project_substitutions(top_nodes, resolvers=resolvers)
    elif closure.category == "strings":
        strings = _project_strings(top_nodes)
    else:  # pragma: no cover -- `classify` never returns another category for this grammar
        raise UiResourceDecodeError(f"{closure.key}: unknown ui-resource category {closure.category!r}")

    if unparsed_offset is not None:
        typed_unidentified.append({"reason": "unparsed-tail", "offset": unparsed_offset})

    return UiResourceModel(
        key=closure.key,
        asset=closure.asset,
        category=closure.category,
        grammar=closure.grammar,
        encoding=closure.encoding,
        member=member,
        tree={"children": top_nodes, "unparsedOffset": unparsed_offset},
        scheme=scheme,
        layout=layout,
        menu=menu,
        hud=hud,
        titles=None,
        rows=None,
        substitutions=substitutions,
        strings=strings,
        options=None,
        menu_scene=menu_scene,
        dependencies=_dedupe_dependencies(dependencies),
        comments=comments,
        anomalies=anomalies,
        omissions=[],
        typed_unidentified=typed_unidentified,
        unresolved=unresolved,
        unsupported=unsupported,
        ledger_row=ledger.finish(),
        omitted_proven=coverage_module.omitted_proven_rows(whitespace_total),
        dormant=closure.dormant,
        dormant_evidence=closure.dormant_evidence,
    )


def decode_ui_resource(
    closure: UiResourceSourceClosure, *, resolvers: Resolvers | None = None
) -> UiResourceModel:
    """The complete ui-resource unit for one source closure."""

    resolvers = resolvers or Resolvers()
    member = closure.member

    if not member.data:
        empty_ledger = new_ledger(member).finish()
        return UiResourceModel(
            key=closure.key, asset=closure.asset, category=closure.category,
            grammar=closure.grammar, encoding=closure.encoding, member=member,
            tree=None, scheme=None, layout=None, menu=None, hud=None, titles=None, rows=None,
            substitutions=None, strings=None, options=None, menu_scene=None,
            dependencies=[], comments=[], anomalies=[], omissions=[{"role": "empty-member"}],
            typed_unidentified=[], unresolved=[], unsupported=[], ledger_row=empty_ledger,
            dormant=closure.dormant, dormant_evidence=closure.dormant_evidence,
        )

    if closure.grammar == "keyvalues":
        return _decode_keyvalues(closure, resolvers)
    if closure.grammar == "titles":
        return grammars.decode_titles(closure)
    if closure.grammar == "settings-scr":
        return grammars.decode_settings_scr(closure)
    if closure.grammar in ("tab-rows", "key-value-lines"):
        return grammars.decode_row_grammar(closure)
    if closure.grammar == "line-list":
        return grammars.decode_line_list(closure)
    if closure.grammar == "won-lists":
        return grammars.decode_won_lists(closure)
    raise UiResourceDecodeError(f"{closure.key}: unknown grammar {closure.grammar!r}")
