"""The typed `projection` layer: the tree restated under one root key's own vocabulary.

`seam_map_vdata.md` names a vocabulary -- `closed` or `open` -- per root key. This module
implements the ones the spec spells out in enough depth to type without guessing:

* `WeaponData` (`items/*`, 244 files) -- `docs/vtmb/wielded_weapons.md` §1 and the seam spec's own
  "WeaponData" section give the four model roles and the scalar fields either side of them.
* `PreCacheData` (`precache/entities.txt`, 1 file) -- "the one row shape the file authors".
* `TerminalDefinition` / `keypad_strings` (`hackterminals/*`, 57 files) -- the seam spec's own
  "TerminalDefinition" section and the shipped `hack_example.txt`/`prop_keypad.txt`.

Every other root key -- including several the spec marks `closed` without giving a key list
(`StatData`, `FeatData`, `ClanDataTables`, `QuestTable`, and others named in `specDeviations`) --
is published `kind: "open"`: a faithful reshape of the tree, folded to plain values, with no
completeness claim beyond the tree's. This is honest about what this implementation covers rather
than fabricating an unverified closed vocabulary; the tree itself is unaffected and stays the
complete, lossless record for every root key alike.
"""

from __future__ import annotations

import re
from typing import Any, Callable

from elysium_pipeline.formats.unit_contract.references import asset_id as _ref_asset_id
from elysium_pipeline.formats.unit_contract.references import dependency
from elysium_pipeline.formats.vdata_glb.lexer import decode_escapes

PathExists = Callable[[str], bool]
SoundGroupLookup = Callable[[str], list[str]]

_CAMERA_CLASS_ENUM = {
    "ranged": "0x2",
    "thrown": "0x4",
    "force_1st": "0x8",
    "melee": "0x10",
    "force_3rd": "0x10",
}

#: `docs/vtmb/wielded_weapons.md` §1, "The keys either side of the model block": the loader's
#: own default for a field never authored. Only the fields the doc actually pins a default for.
_WEAPON_DEFAULTED_FIELDS: dict[str, tuple[str, str, Any]] = {
    "item_flags": ("itemFlags", "int", 8),
    "is_visible_in_hud": ("isVisibleInHud", "bool", True),
    "shows_view_model": ("showsViewModel", "bool", True),
    "hides_hands_model": ("hidesHandsModel", "bool", False),
    "zoomswaydeltamagnitudemin": ("zoomSwayDeltaMagnitudeMin", "float", 0.6),
    "zoomswaydeltamagnitudemax": ("zoomSwayDeltaMagnitudeMax", "float", 3.0),
    "zoomswaytimermin": ("zoomSwayTimerMin", "float", 0.5),
    "zoomswaytimermax": ("zoomSwayTimerMax", "float", 2.0),
}

#: The remaining scalar keys the spec names with no stated engine default: published as
#: `{"value", "authored"}` so a projection value always says whether it was authored, without
#: inventing a number the doc does not give.
_WEAPON_PLAIN_FIELDS: dict[str, str] = {
    "anim_prefix": "animPrefix",
    "impact_snd_group": "impactSndGroup",
    "bucket": "bucket",
    "bucket_position": "bucketPosition",
    "weight": "weight",
    "item_type": "itemType",
    "item_worth": "worth",
}

#: `+0x242c`'s three synthesized bits (`docs/vtmb/wielded_weapons.md` §1): one authored key per
#: bit, zero-default because the field itself is zero-initialised before any key ORs into it.
_WEAPON_BIT_FLAGS: dict[str, str] = {
    "bitflag_cantbelast": "bitFlagCantBeLast",
    "bitflag_discipline_tgt": "bitFlagDisciplineTgt",
    "reload_single": "reloadSingle",
}

_WEAPON_MODEL_ROLES = ("viewmodel", "playermodel", "wieldmodel_f", "wieldmodel_m", "infomodel")


def _model_asset_id(path: str) -> str:
    """`vtmb:model:<key>` for a `models/<key>.mdl` path (`seam_map_model.md`: "the normalized
    path below `models/` without `.mdl`"). The caller keeps the un-stripped path for `raw`/
    `resolved` lookups against the install index; only the published identity is stripped."""

    key = path
    if key.startswith("models/"):
        key = key[len("models/"):]
    if key.endswith(".mdl"):
        key = key[: -len(".mdl")]
    return _ref_asset_id("model", key)


def _sound_asset_id(path: str) -> str:
    """`vtmb:sound:<key>` for a `sound/<key>.<ext>` path (`seam_map_sound.md`: "the normalized
    path below `sound/` with its extension")."""

    key = path
    if key.startswith("sound/"):
        key = key[len("sound/"):]
    return _ref_asset_id("sound", key)


_SOUND_EXTENSIONS = (".wav", ".mp3")


def _looks_like_model_path(raw: str) -> bool:
    """Any authored `.mdl` value, matched by shape rather than a hand-listed key set.

    The corpus authors `.mdl` scalars under many keys this seam never lists by name --
    `ClanDataTables`' numbered `M_Body0`..`M_Body5`/`F_Body0`..`F_Body5`, `StatData`'s
    `InfoModel`/`AmmoInfoModel`, NPC templates' `SpawnModel`, `deathgib`, `model`,
    `projectile_model` -- and every one of them is either empty or spelled below `models/`
    (verified against the real install), so matching the value's own shape catches all of them
    without guessing at the key vocabulary.
    """

    if not raw.lower().endswith(".mdl"):
        return False
    normalized = raw.replace("\\", "/").strip().lower()
    return normalized == "" or normalized.startswith("models/")


def _looks_like_sound_path(raw: str) -> bool:
    return raw.lower().endswith(_SOUND_EXTENSIONS)


def _sound_dependency(raw: str, *, resolve_asset: PathExists | None) -> dict[str, Any]:
    """A `sound` dependency row for a bare `.wav`/`.mp3` value.

    Every real occurrence (`CharEditor.Music`, `RadioData.Show.filename`, `WeaponData`'s
    `Magazine`/`Activation` `SoundData` blocks, …) is spelled below `sound/` without the root
    itself, so the join is always the same: normalize slashes/case and prefix `sound/` unless the
    value already names it.
    """

    normalized = raw.replace("\\", "/").strip().lower()
    if not normalized.startswith("sound/"):
        normalized = "sound/" + normalized
    asset = _sound_asset_id(normalized)
    resolved = bool(resolve_asset(normalized)) if resolve_asset is not None else False
    return dependency("sound", asset, raw, resolved)


def _dedupe_dependencies(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """The same `(role, asset, sourcePath)` triple, once.

    A weapon that binds four model roles to the same `models/weapons/w_null.mdl` placeholder (a
    real, common shape in the corpus) would otherwise publish the identical reference four times;
    the contract states a reference is "declared once in `dependencies`, whatever produced it".
    """

    seen: set[tuple[str, str, str]] = set()
    result: list[dict[str, Any]] = []
    for row in rows:
        key = (row.get("role"), row.get("asset"), row.get("sourcePath"))
        if key in seen:
            continue
        seen.add(key)
        result.append(row)
    return result


def _dotted(path: tuple[int, ...]) -> str:
    return ".".join(str(part) for part in path)


def find_root(nodes: list[dict], key: str) -> dict | None:
    folded = key.strip().lower()
    for node in nodes:
        if node.get("key") == folded:
            return node
    return None


def last_scalars_by_key(nodes: list[dict]) -> dict[str, dict]:
    """Every direct scalar child, keyed by its folded name, last occurrence winning.

    KeyValues resolves a repeated scalar leaf to its last value (`seam_map_vdata.md`, "Generic
    decode"); the tree keeps every occurrence, this view is what a typed field reads from.
    """

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


def _reshape_open(nodes: list[dict]) -> Any:
    """A faithful, plain reshape of a sibling list: repeated keys collect into a list, matching
    `formats/kv.py`'s convention for the rest of the pipeline's KeyValues readers."""

    result: dict[str, Any] = {}
    for node in nodes:
        if node["kind"] == "directive":
            continue                    # carried in `tree` and `typedUnidentified`, not reshaped
        if node["kind"] == "block":
            value: Any = _reshape_open(node["children"])
        else:
            value = decode_escapes(node["value"], node["escapes"])
        key = node["key"]
        if key in result:
            existing = result[key]
            if isinstance(existing, list):
                existing.append(value)
            else:
                result[key] = [existing, value]
        else:
            result[key] = value
    return result


def _open_projection(root_key: str | None, nodes: list[dict], *, evidence: str) -> dict[str, Any]:
    root = find_root(nodes, root_key) if root_key else None
    reshaped = _reshape_open(root["children"] if root is not None else nodes)
    return {
        "kind": "open",
        "vocabulary": "open",
        "rootKey": root_key,
        "evidence": evidence,
        "sections": reshaped,
    }


def _scan_open_dependencies(
    nodes: list[dict],
    *,
    resolve_model: PathExists | None,
    resolve_asset: PathExists | None,
    parent_key: str | None = None,
) -> list[dict[str, Any]]:
    """The dependency-bearing fields an open (unvocabularied) root still authors: any `.mdl`
    value (`ClanDataTables`' per-clan body/hand models, `StatData`'s `InfoModel`/`SpawnModel`,
    …), any `.wav`/`.mp3` value (`CharEditor.Music`, `RadioData.Show.filename`, …) and
    `SignData.BackgroundImage.Name`. Every other field in an open root stays untyped in
    `projection.sections`; this pre-pass only recovers the `dependencies` rows the contract
    requires for the ones real files author, so a `material`/`sound`/`model` reference from an
    open root is never silently dropped."""

    dependencies: list[dict[str, Any]] = []
    for node in nodes:
        if node["kind"] == "block":
            dependencies.extend(
                _scan_open_dependencies(
                    node["children"],
                    resolve_model=resolve_model,
                    resolve_asset=resolve_asset,
                    parent_key=node["key"],
                )
            )
            continue
        if node["kind"] != "scalar":
            continue
        raw = decode_escapes(node["value"], node["escapes"])
        if not raw:
            continue
        key = node["key"]
        if key == "name" and parent_key == "backgroundimage":
            # `SignData.BackgroundImage.Name` authors a bare material name (no `materials/` root,
            # no `.vmt`), the same join `WeaponData.sound_group` needs for its own bare name.
            normalized = raw.replace("\\", "/").strip().lower()
            asset = _ref_asset_id("material", normalized)
            joined = f"materials/{normalized}.vmt"
            resolved = bool(resolve_asset(joined)) if resolve_asset is not None else False
            dependencies.append(dependency("material", asset, raw, resolved))
        elif _looks_like_model_path(raw):
            normalized = raw.replace("\\", "/").strip().lower()
            asset = _model_asset_id(normalized)
            resolved = bool(resolve_model(normalized)) if resolve_model is not None else False
            dependencies.append(dependency("model", asset, raw, resolved))
        elif _looks_like_sound_path(raw):
            dependencies.append(_sound_dependency(raw, resolve_asset=resolve_asset))
    return dependencies


def _model_field(
    node: dict | None, *, resolve: PathExists | None
) -> dict[str, Any]:
    authored = node is not None
    raw = decode_escapes(node["value"], node["escapes"]) if authored else ""
    normalized = raw.replace("\\", "/").strip().lower()
    present = authored and raw != ""
    asset = _model_asset_id(normalized) if present else None
    resolved = bool(resolve(normalized)) if (present and resolve is not None) else False
    return {
        "raw": raw if authored else None,
        "path": normalized if present else None,
        "asset": asset,
        "authored": authored,
        "present": present,
        "resolved": resolved,
    }


def _scan_wav_mp3_dependencies(
    nodes: list[dict], *, resolve_asset: PathExists | None
) -> list[dict[str, Any]]:
    """Every `.wav`/`.mp3` scalar anywhere below `nodes`, however deep.

    `WeaponData`'s `Magazine.SoundData` and each `Activation.SoundData` block nest their sound
    references two and three levels below the root (`{"reload": {"sound1": "weapons/mac_10/
    reload.wav"}}`); neither block is otherwise walked field-by-field (`Magazine` is published as
    `{"present": bool}`, `Activation` is untyped), so this is a dedicated recursive pass rather
    than a byproduct of the scalar-field loops above.
    """

    dependencies: list[dict[str, Any]] = []
    for node in nodes:
        if node["kind"] == "block":
            dependencies.extend(
                _scan_wav_mp3_dependencies(node["children"], resolve_asset=resolve_asset)
            )
            continue
        if node["kind"] != "scalar":
            continue
        raw = decode_escapes(node["value"], node["escapes"])
        if raw and _looks_like_sound_path(raw):
            dependencies.append(_sound_dependency(raw, resolve_asset=resolve_asset))
    return dependencies


def _project_weapon_data(
    root: dict,
    *,
    resolve_model: PathExists | None,
    resolve_sound_group: SoundGroupLookup | None,
    resolve_asset: PathExists | None = None,
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    scalars = last_scalars_by_key(root["children"])
    dependencies: list[dict[str, Any]] = []
    typed_unidentified: list[dict[str, Any]] = []

    models: dict[str, Any] = {}
    for source_key in _WEAPON_MODEL_ROLES:
        field = _model_field(scalars.get(source_key), resolve=resolve_model)
        models[source_key] = field
        if field["present"]:
            dependencies.append(
                dependency("model", field["asset"], field["raw"], field["resolved"])
            )

    defaulted: dict[str, Any] = {}
    for source_key, (field_name, kind, default) in _WEAPON_DEFAULTED_FIELDS.items():
        node = scalars.get(source_key)
        authored = node is not None
        if authored:
            raw = decode_escapes(node["value"], node["escapes"]).strip()
            if kind == "bool":
                value = raw not in ("", "0")
            elif kind == "float":
                try:
                    value = float(raw)
                except ValueError:
                    value = default
            else:
                try:
                    value = int(float(raw))
                except ValueError:
                    value = default
        else:
            value = default
        defaulted[field_name] = {"value": value, "authored": authored, "default": default}

    plain_fields: dict[str, Any] = {}
    for source_key, field_name in _WEAPON_PLAIN_FIELDS.items():
        node = scalars.get(source_key)
        authored = node is not None
        raw = decode_escapes(node["value"], node["escapes"]) if authored else None
        plain_fields[field_name] = {"value": raw, "authored": authored}

    bit_flags: dict[str, Any] = {}
    for source_key, field_name in _WEAPON_BIT_FLAGS.items():
        node = scalars.get(source_key)
        authored = node is not None
        raw = decode_escapes(node["value"], node["escapes"]).strip() if authored else ""
        bit_flags[field_name] = {"value": authored and raw not in ("", "0"), "authored": authored}

    camera_node = scalars.get("camera_class")
    camera_source = decode_escapes(camera_node["value"], camera_node["escapes"]) if camera_node else None
    camera_folded = camera_source.strip().strip('"').lower() if camera_source is not None else None
    camera_class = {
        "value": camera_source,
        "enum": _CAMERA_CLASS_ENUM.get(camera_folded, "0x0"),
        "authored": camera_node is not None,
    }

    sound_group_node = scalars.get("sound_group")
    sound_group: dict[str, Any] | None = None
    if sound_group_node is not None:
        raw = decode_escapes(sound_group_node["value"], sound_group_node["escapes"])
        folded = raw.strip().lower()
        members = list(resolve_sound_group(folded)) if resolve_sound_group is not None else []
        sound_group = {
            "value": raw,
            "resolved": bool(members),
            "members": [
                {"path": member, "asset": _sound_asset_id(member)} for member in members
            ],
        }
        if members:
            for member in members:
                dependencies.append(
                    dependency("sound-group", _sound_asset_id(member), member, True)
                )
        else:
            typed_unidentified.append(
                {
                    "field": "sound_group",
                    "value": raw,
                    "reason": "no-directory-member-resolved",
                    "offset": sound_group_node["offset"],
                }
            )

    magazine_blocks = blocks_by_key(root["children"]).get("magazine", [])
    magazine = {"present": bool(magazine_blocks)}

    dependencies.extend(_scan_wav_mp3_dependencies(root["children"], resolve_asset=resolve_asset))

    known_scalar_keys = (
        set(_WEAPON_MODEL_ROLES)
        | set(_WEAPON_DEFAULTED_FIELDS)
        | set(_WEAPON_PLAIN_FIELDS)
        | set(_WEAPON_BIT_FLAGS)
        | {"camera_class", "sound_group"}
    )
    known_block_keys = {"magazine"}
    for node in root["children"]:
        if node["kind"] == "scalar" and node["key"] not in known_scalar_keys:
            typed_unidentified.append(
                {"field": node["sourceKey"], "offset": node["offset"], "path": _dotted((node["index"],))}
            )
        elif node["kind"] == "block" and node["key"] not in known_block_keys:
            typed_unidentified.append(
                {"field": node["sourceKey"], "offset": node["offset"], "path": _dotted((node["index"],))}
            )

    fields: dict[str, Any] = {"models": models}
    fields.update(defaulted)
    fields.update(plain_fields)
    fields.update(bit_flags)
    fields["cameraClass"] = camera_class
    fields["soundGroup"] = sound_group
    fields["magazine"] = magazine

    projection = {
        "kind": "closed",
        "vocabulary": "closed",
        "rootKey": "WeaponData",
        "evidence": "FUN_10259f80, FUN_1025b930, docs/vtmb/wielded_weapons.md #1",
        "fields": fields,
    }
    return projection, _dedupe_dependencies(dependencies), typed_unidentified


def _project_precache_data(
    root: dict, *, resolve_asset: PathExists | None
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    inner_blocks = blocks_by_key(root["children"]).get("precachedata", [])
    entries: list[dict[str, Any]] = []
    dependencies: list[dict[str, Any]] = []
    for block in inner_blocks:
        for node in block["children"]:
            if node["kind"] != "scalar":
                continue
            classname = node["sourceKey"]
            path = decode_escapes(node["value"], node["escapes"])
            normalized = path.replace("\\", "/").strip().lower()
            asset = _ref_asset_id("asset", normalized) if normalized else None
            resolved = bool(resolve_asset(normalized)) if (normalized and resolve_asset) else False
            entries.append(
                {"classname": classname, "path": path, "asset": asset, "resolved": resolved}
            )
            if asset:
                dependencies.append(dependency("asset", asset, path, resolved))
    projection = {
        "kind": "closed",
        "vocabulary": "closed",
        "rootKey": "PreCacheData",
        "evidence": "the one row shape vdata/precache/entities.txt authors",
        "entries": entries,
    }
    return projection, dependencies, []


def _terminal_scalar(scalars: dict[str, dict], key: str) -> dict[str, Any] | None:
    node = scalars.get(key)
    if node is None:
        return None
    return {"value": decode_escapes(node["value"], node["escapes"]), "offset": node["offset"]}


def _project_terminal_definition(root: dict) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    scalars = last_scalars_by_key(root["children"])
    blocks = blocks_by_key(root["children"])
    typed_unidentified: list[dict[str, Any]] = []

    def logon_lines(block: dict) -> list[dict[str, Any]]:
        lines = []
        for node in block["children"]:
            if node["kind"] == "scalar" and node["key"].startswith("line"):
                lines.append(
                    {"key": node["sourceKey"], "value": decode_escapes(node["value"], node["escapes"])}
                )
            elif node["kind"] == "scalar":
                typed_unidentified.append({"field": node["sourceKey"], "offset": node["offset"]})
        return lines

    _SUBDIR_KEYS = ("name", "password", "description", "dependency")
    _FUNCTION_KEYS = ("name", "description", "runtext", "trigger", "runscript", "dependency")
    _EMAIL_KEYS = ("subject", "sender", "body", "runscript", "dependency", "autodelete")

    def scalar_fields(block: dict, known: tuple[str, ...]) -> dict[str, Any]:
        block_scalars = last_scalars_by_key(block["children"])
        fields = {}
        for key in known:
            node = block_scalars.get(key)
            fields[key] = None if node is None else decode_escapes(node["value"], node["escapes"])
        for node in block["children"]:
            if node["kind"] == "scalar" and node["key"] not in known:
                typed_unidentified.append({"field": node["sourceKey"], "offset": node["offset"]})
        return fields

    subdirs = []
    for subdir in blocks.get("subdir", []):
        fields = scalar_fields(subdir, _SUBDIR_KEYS)
        functions = []
        for function in blocks_by_key(subdir["children"]).get("function", []):
            functions.append(scalar_fields(function, _FUNCTION_KEYS))
        fields["functions"] = functions
        subdirs.append(fields)

    emails = []
    for email in blocks.get("email", []):
        fields = scalar_fields(email, _EMAIL_KEYS)
        fields["autodelete"] = {"value": fields["autodelete"], "readByLoader": False}
        emails.append(fields)

    logon = blocks.get("logonscreen", [])

    known_scalar_keys = {"screen saver", "brackets", "email_password", "email_username"}
    known_block_keys = {"logonscreen", "subdir", "email"}
    for node in root["children"]:
        if node["kind"] == "scalar" and node["key"] not in known_scalar_keys:
            typed_unidentified.append({"field": node["sourceKey"], "offset": node["offset"]})
        elif node["kind"] == "block" and node["key"] not in known_block_keys:
            typed_unidentified.append({"field": node["sourceKey"], "offset": node["offset"]})

    fields = {
        "screenSaver": _terminal_scalar(scalars, "screen saver"),
        "brackets": _terminal_scalar(scalars, "brackets"),
        "emailPassword": _terminal_scalar(scalars, "email_password"),
        "emailUsername": _terminal_scalar(scalars, "email_username"),
        "logonScreen": logon_lines(logon[0]) if logon else [],
        "subDirs": subdirs,
        "emails": emails,
    }
    projection = {
        "kind": "closed",
        "vocabulary": "closed",
        "rootKey": "TerminalDefinition",
        "evidence": "CPropHacking::LoadFromFile 0x1021cba0, docs/vtmb/computer-terminals.md #5",
        "fields": fields,
    }
    return projection, [], typed_unidentified


def _project_keypad_strings(root: dict) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    typed_unidentified: list[dict[str, Any]] = []
    keypads = []
    for block in blocks_by_key(root["children"]).get("keypad", []):
        scalars = last_scalars_by_key(block["children"])
        text_id = scalars.get("textid")
        title = scalars.get("titletext")
        keypads.append(
            {
                "textId": decode_escapes(text_id["value"], text_id["escapes"]) if text_id else None,
                "titleText": decode_escapes(title["value"], title["escapes"]) if title else None,
            }
        )
        for node in block["children"]:
            if node["kind"] == "directive":
                continue
            if node["key"] not in ("textid", "titletext"):
                typed_unidentified.append({"field": node["sourceKey"], "offset": node["offset"]})
    for node in root["children"]:
        if node["kind"] != "block" or node["key"] != "keypad":
            if node["kind"] != "directive":
                typed_unidentified.append(
                    {"field": node["sourceKey"], "offset": node["offset"]}
                )
    projection = {
        "kind": "closed",
        "vocabulary": "closed",
        "rootKey": "keypad_strings",
        "evidence": "CPropKeypad::LoadTextStrings 0x1021da40",
        "keypads": keypads,
    }
    return projection, [], typed_unidentified


def _project_clan_data(root, top_nodes, *, resolve_model, resolve_asset):
    """Ordered clan rows with typed body references and the full open table beside them.

    General's body keys select player/NPC bodies; DeathGib and unrelated model-valued fields
    remain dependencies without acquiring that role. Repeated clan blocks are not collapsed.
    """
    projection = _open_projection("ClanDataTables", top_nodes, evidence="clan-body-fields")
    clans = []
    for clan in blocks_by_key(root["children"]).get("clandata", []):
        bodies = {}
        for general in blocks_by_key(clan["children"]).get("general", []):
            for key, node in last_scalars_by_key(general["children"]).items():
                if re.fullmatch(r"[mf]_body\d*", key):
                    bodies[key] = _model_field(node, resolve=resolve_model)
        clans.append({"index": len(clans), "bodies": bodies})
    projection["clans"] = clans
    dependencies = _scan_open_dependencies(
        root["children"], resolve_model=resolve_model, resolve_asset=resolve_asset,
        parent_key="clandatatables")
    return projection, _dedupe_dependencies(dependencies), []


def build_projection(
    root_key: str | None,
    top_nodes: list[dict],
    *,
    resolve_model: PathExists | None = None,
    resolve_sound_group: SoundGroupLookup | None = None,
    resolve_asset: PathExists | None = None,
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    """`(projection, dependencies, typedUnidentified)` for one document's top-level nodes."""

    if not root_key:
        deps = _scan_open_dependencies(top_nodes, resolve_model=resolve_model, resolve_asset=resolve_asset)
        return _open_projection(None, top_nodes, evidence="empty-document"), _dedupe_dependencies(deps), []
    folded = root_key.strip().lower()
    root = find_root(top_nodes, folded)
    if root is None or root["kind"] != "block":
        # A directive, a stray scalar, or a root the walk could not attribute a block to: the
        # tree already carries it faithfully, so the projection is the open reshape of the whole
        # top level rather than a fabricated closed section.
        deps = _scan_open_dependencies(top_nodes, resolve_model=resolve_model, resolve_asset=resolve_asset)
        return _open_projection(root_key, top_nodes, evidence="root-not-a-block"), _dedupe_dependencies(deps), []
    if folded == "weapondata":
        return _project_weapon_data(
            root,
            resolve_model=resolve_model,
            resolve_sound_group=resolve_sound_group,
            resolve_asset=resolve_asset,
        )
    if folded == "clandatatables":
        return _project_clan_data(
            root, top_nodes, resolve_model=resolve_model, resolve_asset=resolve_asset)
    if folded == "precachedata":
        return _project_precache_data(root, resolve_asset=resolve_asset)
    if folded == "terminaldefinition":
        return _project_terminal_definition(root)
    if folded == "keypad_strings":
        return _project_keypad_strings(root)
    deps = _scan_open_dependencies(
        root["children"], resolve_model=resolve_model, resolve_asset=resolve_asset, parent_key=folded
    )
    return _open_projection(root_key, top_nodes, evidence="tree-reshape"), _dedupe_dependencies(deps), []


_WS_FIX_PATTERN = re.compile(r"^\s*(.+?)\s*,\s*ws-fix\b", re.IGNORECASE)


def ws_fix_value(comment_text: str) -> str | None:
    """The residual value a `//<old value>, ws-fix` trailing comment preserves, or `None`.

    `seam_map_vdata.md`'s `SignData` section demonstrates the pattern (`"XPos" "" //1, ws-fix`);
    nothing in the grammar ties it to one root key, so this is checked against every trailing
    comment regardless of root.
    """

    body = comment_text[2:] if comment_text.startswith("//") else comment_text
    match = _WS_FIX_PATTERN.match(body)
    return match.group(1) if match else None


__all__ = [
    "build_projection",
    "blocks_by_key",
    "find_root",
    "last_scalars_by_key",
    "ws_fix_value",
]
