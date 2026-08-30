"""Decode one surface-property entry into its complete model plus a gapless byte ledger.

Unlike a VMT, this table has a closed vocabulary: the file's own header comment documents the
keys, and the shipped entries use 29 of them and nothing else. The decode therefore maps every
key it reads onto a named field and records anything else as unsupported rather than carrying it
as an anonymous pair, because a key with no rule is a key whose meaning the unit cannot publish.
"""

from __future__ import annotations

from typing import Any, Callable

from elysium_pipeline.formats.surface_property_glb import lexer
from elysium_pipeline.formats.surface_property_glb.coverage import ByteLedger
from elysium_pipeline.formats.surface_property_glb.model import (
    TABLE_PATH,
    Parameter,
    SurfacePropertyModel,
    asset_id,
    sound_asset_id,
    sound_script_asset_id,
)
from elysium_pipeline.formats.unit_contract import dependency


class SurfacePropertyDecodeError(RuntimeError):
    """The selected entry cannot be read as a surface property."""


#: Physics constants, in the units the table's header comment documents.
PHYSICS_KEYS = {
    "density": "density",
    "elasticity": "elasticity",
    "friction": "friction",
    "thickness": "thickness",
}

#: Movement modifiers. Only `default` declares them, so every other surface inherits them.
MOVEMENT_KEYS = {
    "maxspeedfactor": "maxSpeedFactor",
    "jumpfactor": "jumpFactor",
    "climbable": "climbable",
}

FOOTSTEP_KEYS = {"stepleft": "left", "stepright": "right"}

#: The impact matrix is the cross product of five weapon classes and three damage outcomes.
WEAPON_CLASSES = ("bullet", "metal", "wood", "blade", "fist")
DAMAGE_OUTCOMES = ("soak", "norm", "crit")
IMPACT_KEYS = {
    f"{weapon}_{outcome}_impact": (weapon, outcome)
    for weapon in WEAPON_CLASSES
    for outcome in DAMAGE_OUTCOMES
}

#: The pre-matrix bullet key, still carried by three entries.
LEGACY_IMPACT_KEY = "bulletimpact"

#: Keys whose value names a sound script rather than a `.wav` path.
SOUND_SCRIPT_KEYS = ("impact", "scrape")

BASE_KEY = "base"
GAME_MATERIAL_KEY = "gamematerial"


def _number(text: str) -> float | None:
    try:
        return float(text.strip().strip('"'))
    except ValueError:
        return None


def _claim(ledger: ByteLedger, token: lexer.Token, owner: str) -> None:
    ledger.claim(token.offset, token.length, "mapped", owner)


def decode_surface_property(
    closure,
    *,
    base_exists: Callable[[str], bool] | None = None,
    sound_script_exists: Callable[[str], bool] | None = None,
    sound_exists: Callable[[str], bool] | None = None,
) -> SurfacePropertyModel:
    """The complete surface-property unit for one source closure.

    `base_exists`, `sound_script_exists` and `sound_exists` answer whether a named target is
    present in the install. Without them a reference publishes unresolved rather than asserting a
    target this decode never looked for.
    """

    member = closure.entry
    text = lexer.decode_text(member.data)
    try:
        tokens = lexer.tokenize(text)
        entries = lexer.parse(tokens)
    except lexer.SurfacePropertyLexError as error:
        raise SurfacePropertyDecodeError(f"{member.path}: {error}") from error
    if len(entries) != 1:
        raise SurfacePropertyDecodeError(
            f"{member.path}: the unit's bytes declare {len(entries)} surfaces, not one"
        )
    entry = entries[0]

    ledger = ByteLedger(member.path, member.data)
    comments: list[dict[str, Any]] = []
    for token in tokens:
        if token.kind == "whitespace":
            ledger.claim(
                token.offset, token.length, "omitted-proven", "entry.insignificant-whitespace"
            )
        elif token.kind == "comment":
            ledger.claim(token.offset, token.length, "mapped", f"entry.comment[{len(comments)}]")
            comments.append({"offset": token.offset, "text": token.text})
        elif token.kind == "bom":
            ledger.claim(token.offset, token.length, "mapped", "entry.byte-order-mark")

    _claim(ledger, entry.name, "entry.name")
    _claim(ledger, entry.open_token, "entry.open")
    if entry.close_token is not None:
        _claim(ledger, entry.close_token, "entry.close")

    parameters: list[Parameter] = []
    for pair in entry.pairs:
        slot = len(parameters)
        _claim(ledger, pair.key, f"entry.parameter[{slot}].key")
        if pair.value is not None:
            _claim(ledger, pair.value, f"entry.parameter[{slot}].value")
        parameters.append(
            Parameter(
                index=slot,
                key=pair.key.text.strip().lower(),
                source_key=pair.key.text,
                value="" if pair.value is None else pair.value.text,
                quoted_key=pair.key.quoted,
                quoted_value=bool(pair.value is not None and pair.value.quoted),
                offset=pair.key.offset,
            )
        )

    base: dict[str, Any] | None = None
    physics: dict[str, Any] = {}
    movement: dict[str, Any] = {}
    footsteps: dict[str, list[dict[str, Any]]] = {}
    impacts: dict[str, Any] = {}
    sounds: dict[str, Any] = {}
    game_material: str | None = None
    anomalies: list[dict[str, Any]] = list(entry.anomalies)
    unresolved: list[dict[str, Any]] = []
    unsupported: list[dict[str, Any]] = []
    dependencies: list[dict[str, Any]] = []
    seen_assets: set[str] = set()

    def depend(role: str, asset: str, source_path: str, resolved: bool) -> None:
        # Every row states its resolution, because the unit contract's dependency row does: the
        # corpus index reads `resolved` to tell a reference the install answers from one it does
        # not, and a row that omits the key would read as unresolved.
        if asset in seen_assets:
            return
        seen_assets.add(asset)
        dependencies.append(dependency(role, asset, source_path, resolved))

    def sound_resolves(value: str) -> bool:
        """Whether the install ships the `.wav` a footstep or impact key names.

        The value is authored relative to `sound/`, which is where the sound seam keys its
        units from.
        """

        if sound_exists is None:
            return False
        key = value.replace("\\", "/").strip().strip('"').lower().strip("/")
        while "//" in key:
            key = key.replace("//", "/")
        return bool(key) and bool(sound_exists(f"sound/{key}"))

    def scalar(target: dict[str, Any], field: str, parameter: Parameter, value: Any) -> None:
        # KeyValues resolves a repeated scalar to its last value; a variation pool is a property
        # of the sound keys alone, so a repeat here is a departure worth naming.
        if field in target:
            anomalies.append(
                {"role": "repeated-scalar-key", "offset": parameter.offset, "key": parameter.key}
            )
        target[field] = value

    def sound_record(parameter: Parameter) -> dict[str, Any]:
        return {
            "path": parameter.value,
            "asset": sound_asset_id(parameter.value),
            "parameter": parameter.index,
        }

    for parameter in parameters:
        key, raw = parameter.key, parameter.value.strip()
        if key == BASE_KEY:
            if not raw:
                unsupported.append(
                    {"key": key, "offset": parameter.offset, "reason": "base-names-no-surface"}
                )
                continue
            if parameter.index != 0:
                # The table's header comment states `base` must come first, and every shipped
                # entry obeys it; a later `base` is a shape a reader cannot assume.
                anomalies.append(
                    {"role": "base-is-not-the-first-key", "offset": parameter.offset}
                )
            name = raw.strip('"').lower()
            present = bool(base_exists(name)) if base_exists is not None else False
            base = {
                "name": name,
                "sourceName": raw,
                "asset": asset_id(name),
                "resolved": present,
                "parameter": parameter.index,
            }
            depend("surface-property", base["asset"], f"{TABLE_PATH}#{name}", present)
            if base_exists is not None and not present:
                unresolved.append(
                    {"path": "base", "name": name, "reason": "base-names-no-defined-surface"}
                )
        elif key in PHYSICS_KEYS:
            number = _number(raw)
            if number is None:
                unsupported.append(
                    {"key": key, "offset": parameter.offset, "reason": "value-is-not-a-number"}
                )
                continue
            scalar(physics, PHYSICS_KEYS[key], parameter, number)
        elif key in MOVEMENT_KEYS:
            number = _number(raw)
            if number is None:
                unsupported.append(
                    {"key": key, "offset": parameter.offset, "reason": "value-is-not-a-number"}
                )
                continue
            scalar(
                movement,
                MOVEMENT_KEYS[key],
                parameter,
                bool(number) if key == "climbable" else number,
            )
        elif key in FOOTSTEP_KEYS:
            record = sound_record(parameter)
            footsteps.setdefault(FOOTSTEP_KEYS[key], []).append(record)
            depend("sound", record["asset"], parameter.value, sound_resolves(parameter.value))
        elif key in IMPACT_KEYS:
            weapon, outcome = IMPACT_KEYS[key]
            record = sound_record(parameter)
            impacts.setdefault(weapon, {}).setdefault(outcome, []).append(record)
            depend("sound", record["asset"], parameter.value, sound_resolves(parameter.value))
        elif key == LEGACY_IMPACT_KEY:
            record = sound_record(parameter)
            impacts.setdefault("legacy", []).append(record)
            depend("sound", record["asset"], parameter.value, sound_resolves(parameter.value))
        elif key in SOUND_SCRIPT_KEYS:
            name = raw.strip('"')
            resolved = (
                bool(sound_script_exists(name)) if sound_script_exists is not None else False
            )
            record = {
                "script": name,
                "asset": sound_script_asset_id(name),
                "resolved": resolved,
                "parameter": parameter.index,
            }
            sounds.setdefault(key, []).append(record)
            depend("sound-script", record["asset"], name, resolved)
        elif key == GAME_MATERIAL_KEY:
            if not raw:
                unsupported.append(
                    {"key": key, "offset": parameter.offset, "reason": "value-is-empty"}
                )
                continue
            if game_material is not None:
                anomalies.append(
                    {"role": "repeated-scalar-key", "offset": parameter.offset, "key": key}
                )
            game_material = raw.strip('"')
        else:
            unsupported.append(
                {
                    "key": key,
                    "offset": parameter.offset,
                    "reason": "key-outside-the-surface-property-vocabulary",
                }
            )

    omissions = [{
        "role": "keyvalues-insignificant-whitespace",
        "reason": "separator-bytes-carry-no-keyvalues-meaning",
    }]

    return SurfacePropertyModel(
        name=closure.name,
        source_name=closure.source_name,
        asset_id=closure.asset_id,
        sources=[member.identity()],
        base=base,
        physics=physics,
        movement=movement,
        footsteps=footsteps,
        impacts=impacts,
        sounds=sounds,
        game_material=game_material,
        parameters=parameters,
        dependencies=dependencies,
        comments=comments,
        anomalies=anomalies,
        omissions=omissions,
        unresolved=unresolved,
        unsupported=unsupported,
        byte_coverage=[ledger.finish()],
    )
