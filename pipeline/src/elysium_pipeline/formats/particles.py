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
    "loop", "precipitation", "spawn", "sprite", "frames", "min_frames", "max_frames",
    "fps", "movealign", "flat",
    "x_speed", "y_speed", "z_speed", "size", "height", "width", "rotation", "color",
    "mask", "collide", "red", "green", "blue", "burst",
    "parent_speed", "radius_speed", "elevation_speed", "theta_speed", "phi_speed",
    "depth_offset",
}
_SPAWN_KEYS = {
    "particle", "rate", "burst", "loop", "radius", "theta", "phi", "friction", "bounce",
    "x", "y", "z",
}
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
    # Map data spells a definition three ways: bare (`cigar_emitter`), with the directory
    # (`particles/cigar_emitter`), and with the extension too. They all name the same file.
    if name.startswith("particles/"):
        name = name[len("particles/"):]
    if name.endswith(".txt"):
        name = name[: -len(".txt")]
    if not name or not re.fullmatch(r"[a-z0-9_./-]+", name):
        raise ParticleContractError(f"{where}: invalid asset name {value!r}")
    return name


def _compile_spawn(block: dict[str, Any], where: str) -> dict[str, Any]:
    _reject_unknown(block, _SPAWN_KEYS, where)
    if "particle" not in block:
        raise ParticleContractError(f"{where}: missing particle")
    result: dict[str, Any] = {"particle": _name(block["particle"], where + ".particle")}
    # `rate` is a per-second emission rate; `burst` is a one-shot count. A block carries one or the
    # other, and both are dimensionless.
    for key in ("rate", "burst", "friction", "bounce"):
        if key in block:
            result[key] = _curve(block[key], where + "." + key)
    if "loop" in block:
        result["loop"] = _bool(block["loop"], where + ".loop")
    if "radius" in block:
        result["radius_cm"] = _curve(block["radius"], where + ".radius", scale=INCH_TO_CM)
    for key in ("theta", "phi"):
        if key in block:
            result[key + "_degrees"] = _curve(block[key], where + "." + key)
    # Cartesian spawn offset, in the same Source frame as the velocities — so `y` takes the same
    # reflection `y_speed` does.
    offset = {}
    for source_key, negate in (("x", False), ("y", True), ("z", False)):
        if source_key in block:
            offset[source_key] = _curve(
                block[source_key], where + "." + source_key, scale=INCH_TO_CM, negate=negate
            )
    if offset:
        result["offset_cm"] = offset
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
    # A fixed lifetime, or a min/max pair drawn per particle. Both are counts of frames, paced by
    # `fps` where one is given.
    for key in ("frames", "min_frames", "max_frames"):
        if key in body:
            value = int(_finite(body[key], name + "." + key))
            if value <= 0:
                raise ParticleContractError(f"{name}.{key}: must be positive")
            result[key] = value
    if "fps" in body:
        fps = _finite(body["fps"], name + ".fps")
        if fps <= 0:
            raise ParticleContractError(f"{name}.fps: must be positive")
        result["fps"] = fps
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

    # The spherical emission frame, used instead of (or alongside) the cartesian one. `radius_speed`
    # drives outward from the emitter and `elevation_speed` drives along its axis, so both are
    # lengths per second; `parent_speed` is the dimensionless fraction of the parent's velocity the
    # particle inherits (every value in the corpus is 0 or 1).
    radial = {}
    for source_key, target_key in (("radius_speed", "radius"), ("elevation_speed", "elevation")):
        if source_key in body:
            radial[target_key] = _curve(
                body[source_key], name + "." + source_key, scale=INCH_TO_CM
            )
    if radial:
        result["radial_velocity_cm_per_second"] = radial
    # The angular members of the same frame, in degrees per second like `theta`/`phi` are degrees.
    angular = {}
    for source_key, target_key in (("theta_speed", "theta"), ("phi_speed", "phi")):
        if source_key in body:
            angular[target_key] = _curve(body[source_key], name + "." + source_key)
    if angular:
        result["angular_velocity_degrees_per_second"] = angular
    if "parent_speed" in body:
        result["parent_speed"] = _curve(body["parent_speed"], name + ".parent_speed")

    for key in ("size", "height", "width"):
        if key in body:
            result[key + "_cm"] = _curve(body[key], name + "." + key, scale=INCH_TO_CM)
    for key in ("rotation", "color", "mask", "red", "green", "blue", "burst"):
        if key in body:
            result[key] = _curve(body[key], name + "." + key)
    # `depth_offset` is carried unconverted: it biases the particle's sort/draw depth and whether
    # that is a world length is not established, so a unit conversion here would be a guess.
    if "depth_offset" in body:
        result["depth_offset"] = _curve(body["depth_offset"], name + ".depth_offset")

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


MAP_PARTICLE_SCHEMA = "elysium.map-particles"
MAP_PARTICLE_VERSION = 1


def _int_key(keys: dict[str, Any], name: str, default: int = 0) -> int:
    raw = str(keys.get(name, "")).strip()
    if not raw:
        return default
    try:
        return int(float(raw))
    except ValueError as exc:
        raise ParticleContractError(f"env_particle: invalid {name} {raw!r}") from exc


def _env_particles(document: dict) -> list[dict]:
    return [
        entity for entity in document.get("entities", [])
        if entity.get("classname", "").lower() == "env_particle"
        and str(entity.get("keys", {}).get("particle_definition", "")).strip()
    ]


def build_particle_document(
    map_name: str,
    entity_document: dict,
    read_text: Callable[[str], str | None],
    has_sprite: Callable[[str], bool],
) -> dict | None:
    """Every ``env_particle`` a map places, plus the definition closure they resolve to.

    Sibling of the rain-only ``<map>.weather.json``: that document owns wetness, the height
    texture and the rain timers, and stays the authority for those.  This one covers the
    general emitter set, including the attachment keys the rain slice never needed.
    """

    entities = _env_particles(entity_document)
    if not entities:
        return None

    roots = sorted({
        _name(entity["keys"]["particle_definition"], "env_particle.particle_definition")
        for entity in entities
    })

    # Compile one root at a time. A map places emitters the rain slice never exercised, and a single
    # definition that is malformed in the user's install, absent from it, or uses a key this
    # contract has not established must not take the whole map's export down with it — the emitter
    # is recorded as unresolved and the rest still compile. `compile_closure` itself stays strict,
    # because the rain closure is a fixed set that has to be exact.
    definitions: dict[str, Any] = {}
    sprites: set[str] = set()
    unresolved: list[dict[str, str]] = []
    for root in roots:
        try:
            compiled = compile_closure([root], read_text, has_sprite)
        except ParticleContractError as error:
            unresolved.append({"definition": root, "reason": str(error)})
            continue
        definitions.update(compiled["definitions"])
        sprites.update(compiled["sprites"])
    closure = {
        "schema": "elysium.particle-closure",
        "version": 1,
        "roots": [root for root in roots if root in definitions],
        "definitions": {name: definitions[name] for name in sorted(definitions)},
        "sprites": sorted(sprites),
    }

    emitters = []
    for ordinal, entity in enumerate(entities):
        keys = entity.get("keys", {})
        emitters.append({
            "ordinal": ordinal,
            "targetname": entity.get("targetname", ""),
            "origin_cm": entity.get("origin", [0.0, 0.0, 0.0]),
            "particle_definition": _name(
                keys["particle_definition"], "env_particle.particle_definition"
            ),
            "active": _int_key(keys, "active") != 0,
            "start_hidden": bool(entity.get("start_hidden", False)),
            # `attach_type` 2 is `point` — attach to the named point on `parentname`'s model.
            # The definition-side parser names 0..3 origin/tree/point/treecolor; higher values
            # are used but unresolved, so they are carried verbatim.
            "attach_type": _int_key(keys, "attach_type"),
            "parentname": str(keys.get("parentname", "")).strip(),
            "bone": str(keys.get("bone", "")).strip(),
            "bounds_cm": _finite(keys.get("bounds", 0.0), "env_particle.bounds") * INCH_TO_CM,
        })

    return {
        "schema": MAP_PARTICLE_SCHEMA,
        "version": MAP_PARTICLE_VERSION,
        "map": map_name,
        "emitters": emitters,
        "particles": closure,
        "unresolved": unresolved,
    }


def write_particles(map_name: str, out_dir, entity_document: dict, idx):
    """Write ``<map>.particles.json``, or nothing when the map places no ``env_particle``."""

    import json
    from pathlib import Path

    from elysium_pipeline.formats import install

    def read_definition(name: str) -> str | None:
        raw = install.read(idx, f"particles/{name}.txt")
        return raw.decode("latin-1") if raw is not None else None

    document = build_particle_document(
        map_name,
        entity_document,
        read_definition,
        lambda name: install.read(idx, f"particles/{name}.tga") is not None,
    )
    if document is None:
        return None
    destination = Path(out_dir) / f"{map_name}.particles.json"
    destination.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return destination
