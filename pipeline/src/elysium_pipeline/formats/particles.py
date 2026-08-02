"""Strict VtMB particle-definition compiler for the outdoor-rain slice.

The raw ``particles/*.txt`` files remain the source of truth.  This module
resolves only the dependency closure requested by a map and converts every
coordinate-bearing value to Unreal centimetres.  A live key outside the known
contract is an export error; silently dropping one would produce a different
effect.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
import re
from typing import Any, Callable

from elysium_pipeline.formats import kv
from elysium_pipeline.formats.bsp import INCH_TO_CM


class ParticleContractError(ValueError):
    pass


_PARTICLE_KEYS = {
    "loop", "precipitation", "spawn", "sprite", "frames", "movealign", "flat",
    "x_speed", "y_speed", "z_speed", "size", "height", "rotation", "color",
    "mask", "collide",
}
_SPAWN_KEYS = {"particle", "rate", "radius", "theta", "phi", "friction", "bounce"}
_COLLIDE_KEYS = {"spawn", "decal"}
_DECAL_KEYS = {"particle"}
_NUMBER = re.compile(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?")


def _blocks(value: Any) -> list[dict[str, Any]]:
    values = value if isinstance(value, list) else [value]
    if not all(isinstance(item, dict) for item in values):
        raise ParticleContractError("expected a KeyValues block")
    return values


def _reject_unknown(block: dict[str, Any], allowed: set[str], where: str) -> None:
    unknown = sorted(set(block) - allowed)
    if unknown:
        raise ParticleContractError(f"{where}: unsupported live field(s): {', '.join(unknown)}")


def _bool(value: Any, where: str) -> bool:
    raw = str(value).strip()
    if raw not in {"0", "1"}:
        raise ParticleContractError(f"{where}: expected 0 or 1, got {raw!r}")
    return raw == "1"


def _finite(value: Any, where: str) -> float:
    try:
        result = float(str(value).strip())
    except ValueError as exc:
        raise ParticleContractError(f"{where}: invalid number {value!r}") from exc
    if not math.isfinite(result):
        raise ParticleContractError(f"{where}: number must be finite")
    return result


def _curve(value: Any, where: str, *, scale: float = 1.0, negate: bool = False) -> dict[str, Any]:
    """Preserve VtMB's scalar/range/keyframe spelling and expose converted numbers.

    Parenthesised values such as ``0,80(10)`` are timing annotations in the
    original particle language.  The native renderer needs the spelling while
    the generated Niagara builder consumes the converted numeric sequence.
    """

    raw = str(value).strip()
    numbers = [_finite(match.group(0), where) for match in _NUMBER.finditer(raw)]
    if not numbers:
        raise ParticleContractError(f"{where}: expected a numeric value")
    factor = -scale if negate else scale
    return {
        "source": raw,
        "values": [number * factor for number in numbers],
        "kind": "range" if "~" in raw else "curve" if "," in raw or "(" in raw else "scalar",
    }


def _name(value: Any, where: str) -> str:
    name = str(value).strip().replace("\\", "/").lower()
    if not name or not re.fullmatch(r"[a-z0-9_./-]+", name):
        raise ParticleContractError(f"{where}: invalid asset name {value!r}")
    return name


def _compile_spawn(block: dict[str, Any], where: str) -> dict[str, Any]:
    _reject_unknown(block, _SPAWN_KEYS, where)
    if "particle" not in block:
        raise ParticleContractError(f"{where}: missing particle")
    result: dict[str, Any] = {"particle": _name(block["particle"], where + ".particle")}
    for key in ("rate", "friction", "bounce"):
        if key in block:
            result[key] = _curve(block[key], where + "." + key)
    if "radius" in block:
        result["radius_cm"] = _curve(block["radius"], where + ".radius", scale=INCH_TO_CM)
    for key in ("theta", "phi"):
        if key in block:
            result[key + "_degrees"] = _curve(block[key], where + "." + key)
    return result


def compile_definition(name: str, text: str) -> tuple[dict[str, Any], set[str], set[str]]:
    """Compile one definition and return ``(json, particle refs, sprite refs)``."""

    body = kv.parse(text)
    if not isinstance(body, dict):
        raise ParticleContractError(f"{name}: Particle root must be a block")
    _reject_unknown(body, _PARTICLE_KEYS, name)
    result: dict[str, Any] = {
        "loop": _bool(body.get("loop", "0"), name + ".loop"),
        "precipitation": _bool(body.get("precipitation", "0"), name + ".precipitation"),
    }
    particle_refs: set[str] = set()
    sprite_refs: set[str] = set()

    if "sprite" in body:
        sprite = _name(body["sprite"], name + ".sprite")
        result["sprite"] = sprite
        sprite_refs.add(sprite)
    for key in ("frames",):
        if key in body:
            value = int(_finite(body[key], name + "." + key))
            if value <= 0:
                raise ParticleContractError(f"{name}.{key}: must be positive")
            result[key] = value
    for key in ("movealign", "flat"):
        if key in body:
            result[key] = _bool(body[key], name + "." + key)

    velocity = {}
    for source_key, target_key, negate in (
        ("x_speed", "x", False), ("y_speed", "y", True), ("z_speed", "z", False)
    ):
        if source_key in body:
            velocity[target_key] = _curve(
                body[source_key], name + "." + source_key, scale=INCH_TO_CM, negate=negate
            )
    if velocity:
        result["velocity_cm_per_second"] = velocity
    for key in ("size", "height"):
        if key in body:
            result[key + "_cm"] = _curve(body[key], name + "." + key, scale=INCH_TO_CM)
    for key in ("rotation", "color", "mask"):
        if key in body:
            result[key] = _curve(body[key], name + "." + key)

    if "spawn" in body:
        spawns = []
        for index, block in enumerate(_blocks(body["spawn"])):
            spawn = _compile_spawn(block, f"{name}.spawn[{index}]")
            spawns.append(spawn)
            particle_refs.add(spawn["particle"])
        result["spawns"] = spawns

    if "collide" in body:
        collide_blocks = _blocks(body["collide"])
        if len(collide_blocks) != 1:
            raise ParticleContractError(f"{name}.collide: expected one block")
        collide = collide_blocks[0]
        _reject_unknown(collide, _COLLIDE_KEYS, name + ".collide")
        compiled: dict[str, Any] = {}
        if "spawn" in collide:
            blocks = _blocks(collide["spawn"])
            if len(blocks) != 1:
                raise ParticleContractError(f"{name}.collide.spawn: expected one block")
            spawn = _compile_spawn(blocks[0], name + ".collide.spawn")
            compiled["spawn"] = spawn
            particle_refs.add(spawn["particle"])
        if "decal" in collide:
            blocks = _blocks(collide["decal"])
            if len(blocks) != 1:
                raise ParticleContractError(f"{name}.collide.decal: expected one block")
            decal = blocks[0]
            _reject_unknown(decal, _DECAL_KEYS, name + ".collide.decal")
            if "particle" not in decal:
                raise ParticleContractError(f"{name}.collide.decal: missing particle")
            decal_name = _name(decal["particle"], name + ".collide.decal.particle")
            compiled["decal"] = {"particle": decal_name}
            particle_refs.add(decal_name)
        result["collision"] = compiled

    return result, particle_refs, sprite_refs


def compile_closure(
    roots: list[str],
    read_text: Callable[[str], str | None],
    has_sprite: Callable[[str], bool],
) -> dict[str, Any]:
    """Resolve a deterministic, cycle-safe definition closure."""

    pending = [_name(root, "particle root") for root in roots]
    definitions: dict[str, Any] = {}
    sprites: set[str] = set()
    while pending:
        name = pending.pop(0)
        if name in definitions:
            continue
        text = read_text(name)
        if text is None:
            raise ParticleContractError(f"missing particle definition particles/{name}.txt")
        compiled, refs, sprite_refs = compile_definition(name, text)
        definitions[name] = compiled
        pending.extend(sorted(refs - set(definitions) - set(pending)))
        sprites.update(sprite_refs)
    missing_sprites = sorted(sprite for sprite in sprites if not has_sprite(sprite))
    if missing_sprites:
        raise ParticleContractError(
            "missing particle sprite(s): " + ", ".join(f"particles/{x}.tga" for x in missing_sprites)
        )
    return {
        "schema": "elysium.particle-closure",
        "version": 1,
        "roots": [_name(root, "particle root") for root in roots],
        "definitions": {name: definitions[name] for name in sorted(definitions)},
        "sprites": [f"particles/{name}.tga" for name in sorted(sprites)],
    }
