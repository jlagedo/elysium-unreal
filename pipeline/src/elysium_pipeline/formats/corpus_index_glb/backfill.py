"""The fields no unit can write about itself, written into the target units and re-hashed.

Four of them:

| Written into | From |
|---|---|
| `vtmb:model:` `identity.roles` | `model` edges from maps, entities, vdata items, scenes |
| `vtmb:sound:` `identity.referencedBy` | `sound` edges from scenes, schemes, surface properties, sound scripts, entities |
| `vtmb:expression-table:` `selectedBy[]` | `expression-table` edges from models and scenes |
| `vtmb:shader-program:` `selectedBy[]` | `shaderResolution.programs` of materials |

`vtmb:sound:` and `vtmb:expression-table:` are the plain inverse of one dependency role, one
`{from, role}` row per referrer. `vtmb:shader-program:` is not: a material names its programs by
shader name inside `shaderResolution`, not as a dependency, so that one inverse is read from the
material units' own field.

`vtmb:model:` `identity.roles` is not a row list either. The model seam types it as the
closed vocabulary `character-body`, `animation-bank`, `wield`, `view-model`, `ground-item`,
`placed-prop`, `static-prop`, `include-only` -- "every role another unit assigns it" -- so each
inbound `model` edge is classified from the referrer's own published field:

| Referrer | Evidence | Role |
|---|---|---|
| a model's include tree | the target's `identity.shape` is `bank` | `animation-bank` |
| a model's include tree | any other target shape | `include-only` |
| a map | the path is in `staticProps.props[].asset` | `static-prop` |
| a map | the path is in `detailProps.records[].asset` | `placed-prop` |
| an engine config | a `detailTypes[]` group scatters it into the world | `placed-prop` |
| map entities | an entity's model whose family is `character` | `character-body` |
| map entities | an entity's model of any other family | `placed-prop` |
| a vdata item | the `viewmodel` field | `view-model` |
| a vdata item | the `wieldmodel_*` fields | `wield` |
| a vdata item | the `playermodel` field | `ground-item` |
| a clan table | a `ClanData.General.M_Body*` or `F_Body*` field | `character-body` |

A referrer no rule covers assigns no role; the edge is still in `references[]` and `inverse`,
which is where an unclassified referrer is read. That covers a script naming a model path, an
engine config naming one outside its scatter table, and a vdata scalar that reads as a model path
outside the item's own model fields: each is a path in a file, not a statement about what the
model is for.

The index writes those fields into the target units' JSON chunks as its last step and re-hashes
them; a unit's own export leaves them empty. Only the JSON chunk is rewritten -- the BIN chunk is
copied through byte for byte, because nothing about an inverse edge changes a unit's accessors.
"""

from __future__ import annotations

from dataclasses import replace
import hashlib
import os
from pathlib import Path
from typing import Any, Mapping, Sequence

from elysium_pipeline.formats.corpus_index_glb.graph import extension_root
from elysium_pipeline.formats.corpus_index_glb.model import Reference, Unit
from elysium_pipeline.formats.model_glb.model import (
    ROLES,
    ROLE_ANIMATION_BANK,
    ROLE_CHARACTER_BODY,
    ROLE_GROUND_ITEM,
    ROLE_INCLUDE_ONLY,
    ROLE_PLACED_PROP,
    ROLE_STATIC_PROP,
    ROLE_VIEW_MODEL,
    ROLE_WIELD,
    SHAPE_BANK,
)
from elysium_pipeline.formats.unit_contract import encode_glb, read_glb

#: `identity.family` of a model the character seam's tree holds, the segment below `models/`
#: that tells a body an entity places from a prop it places.
MODEL_CHARACTER_FAMILY = "character"

#: `(identity prefix, the dependency role that fills it, where inside the extension root it is
#: written)`, in the seam map's order. `model` and `shader-program` have no row rule: their
#: fields are filled below.
ROLE_RULES = (
    ("vtmb:sound:", "sound", ("identity", "referencedBy")),
    ("vtmb:expression-table:", "expression-table", ("selectedBy",)),
)

#: Where the model seam's closed role vocabulary is written, and the edge role that fills it.
MODEL_PREFIX = "vtmb:model:"
MODEL_FIELD = ("identity", "roles")
MODEL_ROLE = "model"

#: The vdata weapon fields whose role the model seam's vocabulary names outright; every other
#: model field of a vdata item is the model the item shows in the world.
VDATA_WIELD_FIELDS = ("wieldmodel_f", "wieldmodel_m")
VDATA_VIEW_FIELD = "viewmodel"
VDATA_GROUND_FIELD = "playermodel"

#: Where the shader-program inverse is written, and the two program columns a material declares.
SHADER_PROGRAM_PREFIX = "vtmb:shader-program:"
SHADER_PROGRAM_FIELD = ("selectedBy",)
SHADER_COLUMNS = (("pixelShader", ("psh", "fxc")), ("vertexShader", ("vsh", "fxc")))

#: Every identity prefix this module writes into.
BACKFILL_PREFIXES = (
    (MODEL_PREFIX,) + tuple(prefix for prefix, _, _ in ROLE_RULES) + (SHADER_PROGRAM_PREFIX,)
)


class BackfillError(RuntimeError):
    """A unit's JSON chunk cannot carry the field the index writes into it."""


def _field_of(asset: str) -> tuple[str, ...] | None:
    if asset.startswith(MODEL_PREFIX):
        return MODEL_FIELD
    for prefix, _, where in ROLE_RULES:
        if asset.startswith(prefix):
            return where
    if asset.startswith(SHADER_PROGRAM_PREFIX):
        return SHADER_PROGRAM_FIELD
    return None


def _add(table: dict[str, list[Any]], target: str, row: dict[str, str]) -> None:
    rows = table.setdefault(target, [])
    if row not in rows:
        rows.append(row)


def _map_prop_assets(root: Mapping[str, Any]) -> tuple[set[str], set[str]]:
    """The models one map places through the static prop lump, and through the detail prop lump.

    Both lumps publish the identity they resolved to on the prop row itself, so which lump placed
    a model is read off the map rather than guessed from the dependency row the two share.
    """

    static = {
        str(prop.get("asset"))
        for prop in ((root.get("staticProps") or {}).get("props") or ())
        if isinstance(prop, Mapping) and prop.get("asset")
    }
    detail = {
        str(record.get("asset"))
        for record in ((root.get("detailProps") or {}).get("records") or ())
        if isinstance(record, Mapping) and record.get("asset")
    }
    return static, detail


def _scattered_model_paths(root: Mapping[str, Any]) -> set[str]:
    """The model paths one engine config's `detailTypes[]` scatters into the world.

    The engine-config seam emits a `model` edge for any localized path too -- cinematic rigs
    among them -- so the scatter table is read off the referrer rather than the edge being
    classified from the referrer's kind.
    """

    paths: set[str] = set()
    for detail in root.get("detailTypes") or ():
        if not isinstance(detail, Mapping):
            continue
        for group in detail.get("groups") or ():
            if not isinstance(group, Mapping):
                continue
            for model in group.get("models") or ():
                if isinstance(model, Mapping) and model.get("modelNormalized"):
                    paths.add(str(model["modelNormalized"]))
    return paths


def _entity_model_assets(root: Mapping[str, Any]) -> set[str]:
    """Every model identity an entity of this lump names through its `model` key."""

    assets: set[str] = set()
    for entity in root.get("entities") or ():
        if not isinstance(entity, Mapping):
            continue
        for reference in entity.get("references") or ():
            if isinstance(reference, Mapping) and reference.get("role") == MODEL_ROLE:
                assets.add(str(reference.get("asset")))
    return assets


def _vdata_model_fields(root: Mapping[str, Any]) -> dict[str, set[str]]:
    """Which authored field of a vdata item named each model identity, folded."""

    fields: dict[str, set[str]] = {}
    models = ((root.get("projection") or {}).get("fields") or {}).get("models")
    if isinstance(models, Mapping):
        for source_key, field in models.items():
            if isinstance(field, Mapping) and field.get("asset"):
                fields.setdefault(str(field["asset"]), set()).add(str(source_key).lower())
    return fields


def _model_role(
    edge: Reference,
    source_root: Mapping[str, Any] | None,
    target_root: Mapping[str, Any] | None,
    maps: Mapping[str, tuple[set[str], set[str]]],
) -> set[str]:
    """The documented role one `model` edge assigns its target, as the table above states it."""

    kind = edge.source.split(":")[1] if edge.source.count(":") >= 2 else ""
    if kind == "model":
        shape = str(((target_root or {}).get("identity") or {}).get("shape", ""))
        return {ROLE_ANIMATION_BANK if shape == SHAPE_BANK else ROLE_INCLUDE_ONLY}
    if kind == "map":
        static, detail = maps.get(edge.source, (set(), set()))
        roles = set()
        if edge.target in static:
            roles.add(ROLE_STATIC_PROP)
        if edge.target in detail:
            roles.add(ROLE_PLACED_PROP)
        return roles
    if kind == "engine-config":
        # `sourcePath` is the normalized model path the scatter table itself publishes, so the
        # two are the same string for an edge the `detail` block produced and for no other.
        if edge.source_path in _scattered_model_paths(source_root or {}):
            return {ROLE_PLACED_PROP}
        return set()
    if kind == "map-entities":
        # `identity.family` is the segment below `models/`, which is the first segment of the
        # unit key; reading it from the key classifies the edge whether or not the target's own
        # unit is part of the corpus being indexed.
        family = edge.target[len(MODEL_PREFIX):].split("/")[0]
        if family == MODEL_CHARACTER_FAMILY:
            return {ROLE_CHARACTER_BODY}
        return {ROLE_PLACED_PROP}
    if kind == "vdata":
        projection = (source_root or {}).get("projection") or {}
        if projection.get("rootKey", "").lower() == "clandatatables":
            if any(field.get("asset") == edge.target
                   for clan in projection.get("clans", ())
                   for field in clan.get("bodies", {}).values()):
                return {ROLE_CHARACTER_BODY}
        # A vdata file also emits a `model` edge for any scalar that reads as a model path,
        # anywhere in the file; only the item's own model fields say what the model is for, so an
        # edge none of them named assigns no role.
        named = _vdata_model_fields(source_root or {}).get(edge.target, set())
        roles = set()
        for field in named:
            if field == VDATA_VIEW_FIELD:
                roles.add(ROLE_VIEW_MODEL)
            elif field in VDATA_WIELD_FIELDS:
                roles.add(ROLE_WIELD)
            elif field == VDATA_GROUND_FIELD:
                roles.add(ROLE_GROUND_ITEM)
        return roles
    return set()


def model_roles(
    edges: Sequence[Reference],
    roots: Mapping[str, Mapping[str, Any]],
) -> dict[str, list[str]]:
    """`identity.roles` for every model an edge names, in the vocabulary's own order."""

    maps = {
        asset: _map_prop_assets(root)
        for asset, root in roots.items()
        if asset.startswith("vtmb:map:")
    }
    assigned: dict[str, set[str]] = {}
    for edge in edges:
        if edge.role != MODEL_ROLE or not edge.target.startswith(MODEL_PREFIX):
            continue
        roles = _model_role(edge, roots.get(edge.source), roots.get(edge.target), maps)
        if roles:
            assigned.setdefault(edge.target, set()).update(roles)
    return {
        asset: [role for role in ROLES if role in roles] for asset, roles in assigned.items()
    }


def rows_for(
    units: Sequence[Unit],
    edges: Sequence[Reference],
    roots: Mapping[str, Mapping[str, Any]],
) -> dict[str, list[Any]]:
    """What the index writes into every back-fill target, keyed by identity.

    A target with no inbound edge still gets an empty list: the difference between "nothing
    references this" and "this was never indexed" is exactly what a reader of `identity.roles`
    needs, and the unit's own export left the field empty.
    """

    table: dict[str, list[Any]] = {
        unit.asset: [] for unit in units if _field_of(unit.asset) is not None
    }
    for asset, roles in model_roles(edges, roots).items():
        if asset in table:
            table[asset] = list(roles)
    for prefix, role, _ in ROLE_RULES:
        for edge in edges:
            if edge.role == role and edge.target.startswith(prefix) and edge.target in table:
                _add(table, edge.target, {"from": edge.source, "role": edge.role})

    published = {asset for asset in table if asset.startswith(SHADER_PROGRAM_PREFIX)}
    for asset in sorted(roots):
        if not asset.startswith("vtmb:material:"):
            continue
        resolution = roots[asset].get("shaderResolution")
        if not isinstance(resolution, Mapping):
            continue
        for program in resolution.get("programs") or ():
            if not isinstance(program, Mapping):
                continue
            for column, subdirectories in SHADER_COLUMNS:
                name = program.get(column)
                if not name:
                    continue
                for subdirectory in subdirectories:
                    target = f"{SHADER_PROGRAM_PREFIX}{subdirectory}/{str(name).lower()}"
                    if target in published:
                        _add(table, target, {"from": asset, "role": column})
                        break

    for asset, rows in table.items():
        if not asset.startswith(MODEL_PREFIX):
            # `identity.roles` is already in the vocabulary's order; every other field is a row
            # list, sorted so a second run over the same corpus writes the same bytes.
            rows.sort(key=lambda row: (row["from"], row["role"]))
    return table


def _write(root: dict[str, Any], where: Sequence[str], rows: list[Any]) -> bool:
    """Set `where` inside the extension root; True when the value actually changed."""

    target = root
    for key in where[:-1]:
        nested = target.get(key)
        if not isinstance(nested, dict):
            raise BackfillError(f"the unit carries no {'.'.join(where[:-1])} object")
        target = nested
    if target.get(where[-1]) == rows:
        return False
    target[where[-1]] = rows
    return True


def _replace(path: Path, data: bytes) -> None:
    """Write the rewritten unit to a temporary sibling and rename it over the published file.

    A rewrite is a publication, so it lands the way `write_glb` lands one: the destination holds
    either the bytes it held before or the whole new unit, never a truncated file whose hash the
    index has already recorded. The bytes are encoded once and handed here because the caller
    hashes exactly what it wrote.
    """

    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)


def rewrite(path: Path, asset: str, rows: list[Any]) -> tuple[int, str] | None:
    """Rewrite one published unit's JSON chunk with its inverse rows and re-hash it.

    Returns the new `(byteLength, sha256)`, or None when the unit already carried these rows and
    the file was left untouched -- a corpus index run twice over one corpus writes nothing the
    second time and the `units[]` hashes stay put.
    """

    where = _field_of(asset)
    if where is None:
        return None
    document, binary = read_glb(path)
    name, root = extension_root(document)
    if not _write(root, where, rows):
        return None
    document["extensions"][name] = root
    data = encode_glb(document, binary)
    _replace(Path(path), data)
    return len(data), hashlib.sha256(data).hexdigest()


def apply(
    export_root: Path,
    units: Sequence[Unit],
    table: Mapping[str, list[Any]],
    roots: Mapping[str, dict[str, Any]] | None = None,
) -> list[Unit]:
    """Write every back-fill field and return `units[]` with the rewritten rows re-hashed."""

    updated: list[Unit] = []
    for unit in units:
        rows = table.get(unit.asset)
        if rows is None:
            updated.append(unit)
            continue
        result = rewrite(Path(export_root) / unit.path, unit.asset, rows)
        if result is None:
            updated.append(unit)
            continue
        where = _field_of(unit.asset)
        if roots is not None and unit.asset in roots and where is not None:
            _write(roots[unit.asset], where, rows)
        byte_length, digest = result
        updated.append(replace(unit, byte_length=byte_length, sha256=digest))
    return updated
