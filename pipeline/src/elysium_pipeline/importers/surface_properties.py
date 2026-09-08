"""Stage the surface-property corpus out of the published GLB units.

`uv run elysium import surface-properties` lands every
`$ELYSIUM_EXPORT_V2_ROOT/surface-properties/*.glb` as one `UElysiumPhysicalMaterial` below
`/ElysiumBaked/SurfaceProperties`.
This module is the offline half of that lane.

**Phase 1 — stage.** For each unit: hash the GLB, read the `ELYSIUM_vtmb_surface_property`
extension, **resolve its `base` chain to flat values**, map its `gamematerial` letter to an
`EPhysicalSurface` row, and write one provenance sidecar per unit plus one `manifest.json` under
`$ELYSIUM_WORK_ROOT/import/surface-properties/`. The manifest is the contract the editor phase
(`pipeline/unreal/import_surface_properties.py`) consumes.

**Why flattening happens here.** A unit publishes only what its own entry declares — that is the
seam contract, because inlining an inherited value would make two units authoritative for one
number — and 42 of the 63 entries state only their deltas. Resolution spans units, so it belongs
to a consumer, and this lane is the consumer. The rule is Source's: a child's `surfacedata_t` is a
**copy of its parent's** which the child's own keys then overwrite, so a declared scalar overrides
the parent's scalar and a declared **pool replaces** the parent's whole pool for that slot — an
entry that names one `stepleft` supplies the entire left-footstep pool, it does not append a
fifth alternate to `default`'s four. Which unit supplied each field is recorded per field.

A `base` that no unit defines, and a chain that closes on itself, refuse the unit rather than
publishing a half-resolved asset; every descendant of a refused unit is refused with it, naming
the ancestor.

Staging is idempotent (every file is written only when its bytes change) and prunes: anything
under the staging root this run did not produce, other than the root reports, is deleted. Every
file is written to a temporary sibling and renamed into place, so a run killed mid-write never
leaves a truncated product that a later run mistakes for current.

A unit that fails is one failure among many: it is named with its reason, the run goes on, and the
command exits non-zero at the end. A failed unit's asset is **protected**, not orphaned: the
manifest lists it under `keep` and the editor phase never prunes it.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path

from elysium_pipeline.asset_names import safe_name
from elysium_pipeline.formats.surface_property_glb.model import SURFACE_PROPERTY_EXTENSION
from elysium_pipeline.formats.unit_contract.container import GlbContainerError, decode_glb

#: The family directory this lane reads below the export_v2 root.
FAMILY = "surface-properties"
#: The package every asset lands under.
PACKAGE_ROOT = "/ElysiumBaked/SurfaceProperties"
#: Bumped whenever the flattening or the manifest mapping changes in a way that must re-import.
#: v2: `physics.density` converts to g/cm3 (`UPhysicalMaterial::Density`'s own unit) and the
#: authored kg/m3 value moves to `physics.rawDensity`; every physics/movement scalar is emitted
#: always, `null` when no unit in the chain declares it, so a re-import resets a scalar the source
#: stops declaring instead of leaving it stale.
SETTINGS_VERSION = "elysium-surfaceproperty-import-v2"
MANIFEST_SCHEMA = "1.0.0"
MANIFEST_NAME = "manifest.json"
#: Written by the editor phase; read back by the CLI for its summary.
IMPORT_REPORT_NAME = "import_report.json"
#: Root files that are never pruned.
ROOT_FILES = frozenset({MANIFEST_NAME, IMPORT_REPORT_NAME})
PROVENANCE_SUFFIX = ".provenance.json"
#: The Unreal class every asset is created as.
ASSET_CLASS = "ElysiumPhysicalMaterial"
ASSET_PREFIX = "PM_"

#: The compact material classes `gamematerial` uses across the shipped table: 17 distinct
#: single-letter codes on 26 of the 63 entries (`docs/vtmb/surface_properties.md`).
GAME_MATERIALS = ("A", "C", "D", "F", "G", "I", "M", "N", "O", "P",
                  "S", "T", "U", "V", "W", "X", "Y")

#: One `EPhysicalSurface` row per compact class, assigned by alphabetical order of the letter so
#: the mapping is derivable rather than remembered. The rows themselves are declared in
#: `Config/DefaultEngine.ini` as `VtmbGameMaterial_<letter>`; this table is what stages the row
#: name into each sidecar, and `UElysiumPhysicalMaterial::ApplyJson` only resolves the name through
#: the reflected enum -- so there is one table, not two that can disagree.
#:
#: The rows map the **class**, not the entry: one asset exists per entry and a hit's `PhysMaterial`
#: is that asset, so entry identity needs no enum slot (and could not have one -- 63 entries
#: against the engine's 62 rows).
GAME_MATERIAL_SURFACE_TYPES = {
    letter: f"SurfaceType{index}" for index, letter in enumerate(GAME_MATERIALS, start=1)
}
#: An entry with no `gamematerial` anywhere in its chain.
DEFAULT_SURFACE_TYPE = "SurfaceType_Default"

#: The impact matrix's two axes (`docs/vtmb/surface_properties.md`).
IMPACT_WEAPONS = ("bullet", "metal", "wood", "blade", "fist")
IMPACT_OUTCOMES = ("soak", "norm", "crit")

#: Scalar fields: field path -> (extension section, key inside it). A child that declares one
#: overrides the parent's; the value is a number, a flag or a letter.
SCALAR_FIELDS: tuple[tuple[str, str, str], ...] = (
    ("physics.friction", "physics", "friction"),
    ("physics.elasticity", "physics", "elasticity"),
    ("physics.density", "physics", "density"),
    ("physics.thickness", "physics", "thickness"),
    ("movement.maxSpeedFactor", "movement", "maxSpeedFactor"),
    ("movement.jumpFactor", "movement", "jumpFactor"),
    ("movement.climbable", "movement", "climbable"),
)


class SurfacePropertyImportError(RuntimeError):
    """The surface-property corpus could not be staged."""


def staging_root(work_root: Path) -> Path:
    return Path(work_root) / "import" / FAMILY


def unit_root(export_v2_root: Path) -> Path:
    return Path(export_v2_root) / FAMILY


def units(export_v2_root: Path) -> list[Path]:
    """Every published surface-property unit, in a stable order."""

    root = unit_root(export_v2_root)
    return sorted(root.glob("*.glb")) if root.is_dir() else []


def unit_key(unit: Path) -> str:
    """The unit's key: its file stem, which the exporter already folded to lower case."""

    return Path(unit).stem


def check_key(key: str) -> str:
    """`key`, case-folded, if it can name an asset, else a refusal.

    The family is one flat directory, so folding happens once, here: every caller that turns a
    name into a unit key or an asset path goes through this, including `asset_path_for`, which is
    the cross-lane contract a VMT's `$surfaceprop` (`Metal`, mixed case) resolves through.
    """

    folded = key.strip().lower() if isinstance(key, str) else key
    if not folded or "/" in folded or "\\" in folded or ":" in folded or folded in (".", ".."):
        raise SurfacePropertyImportError(f"{key!r} is not a surface-property key")
    return folded


def asset_path_for(key: str) -> str:
    """`canister` -> `/ElysiumBaked/SurfaceProperties/PM_canister`.

    Load-bearing beyond this lane: the material slice resolves a VMT's `$surfaceprop` and a
    model's `SurfacePropIndex` to this path by folding the name the same way -- `check_key` folding
    case first is what makes `Metal` and `metal` name the same asset.
    """

    from elysium_pipeline.asset_paths import baked_path
    return baked_path("surface-property", check_key(key), ASSET_PREFIX.rstrip("_"))


def prune_scope() -> str:
    """The package folder the editor phase may prune, always the whole root for this lane."""

    return PACKAGE_ROOT + "/"


# --- results -------------------------------------------------------------------------------------


@dataclass(slots=True)
class StageResult:
    staging_root: Path
    manifest_path: Path | None = None
    staged: int = 0
    unchanged: int = 0
    pruned: int = 0
    assets: int = 0
    roots: int = 0
    inherited: int = 0
    deepest_depth: int = 0
    failures: list[tuple[str, str]] = field(default_factory=list)
    protected: int = 0

    def summary(self) -> str:
        return (
            f"surface-property staging: {self.assets} assets ({self.roots} roots, "
            f"{self.inherited} inherited, deepest chain {self.deepest_depth} units) from "
            f"{self.staged} staged + {self.unchanged} unchanged units, {self.pruned} pruned, "
            f"{len(self.failures)} failed ({self.protected} asset paths protected) "
            f"-> {self.staging_root}"
        )


# --- reading the units ----------------------------------------------------------------------------


@dataclass(slots=True)
class Unit:
    """One published unit, read once: its bytes' hash and its decoded extension."""

    key: str
    path: Path
    sha256: str
    extension: dict
    base: str | None


def _fold(name: object) -> str | None:
    if not isinstance(name, str):
        return None
    folded = name.strip().strip('"').lower()
    return folded or None


def read_unit(key: str, data: bytes, path: Path | str = "the unit") -> Unit:
    """Decode one unit's GLB into the record the flattener walks."""

    document, _ = decode_glb(data, str(path))
    extension = (document.get("extensions") or {}).get(SURFACE_PROPERTY_EXTENSION)
    if not isinstance(extension, dict):
        raise SurfacePropertyImportError(f"not a {SURFACE_PROPERTY_EXTENSION} unit")
    identity = extension.get("identity") or {}
    asset_id = identity.get("asset")
    if not isinstance(asset_id, str) or not asset_id.startswith("vtmb:surface-property:"):
        raise SurfacePropertyImportError("unit declares no surface-property identity")
    base = extension.get("base")
    base_name = _fold((base or {}).get("name")) if isinstance(base, dict) else None
    if isinstance(base, dict) and base_name is None:
        raise SurfacePropertyImportError("unit declares a base with no name")
    return Unit(key=check_key(key), path=Path(path), sha256=hashlib.sha256(data).hexdigest(),
                extension=extension, base=base_name)


def resolve_chain(key: str, bases: dict[str, str | None]) -> list[str]:
    """The `base` chain of `key`, **root first**, excluding `key` itself.

    `canister` -> `[metal, metalgrate, metalpanel]`. A base no unit defines, and a chain that
    closes on itself, raise: an asset flattened over a chain that does not resolve would carry
    values nobody authored.
    """

    chain: list[str] = []
    seen = {key}
    current = bases.get(key)
    while current is not None:
        if current in seen:
            cycle = " -> ".join([key, *chain[::-1], current])
            raise SurfacePropertyImportError(f"base chain closes on itself: {cycle}")
        if current not in bases:
            raise SurfacePropertyImportError(f"base {current!r} is defined by no unit")
        seen.add(current)
        chain.append(current)
        current = bases.get(current)
    chain.reverse()
    return chain


# --- flattening ------------------------------------------------------------------------------------


def _line(row: object) -> str:
    """One anomaly, unresolved key or unsupported key as a readable line."""

    if isinstance(row, str):
        return row
    if isinstance(row, dict):
        role = row.get("role")
        rest = " ".join(f"{key}={row[key]}" for key in sorted(row) if key != "role")
        return f"{role} {rest}".strip() if role else rest
    return str(row)


def _pool(rows: object) -> list[str] | None:
    """A published variation pool as its ordered asset IDs, or None when the unit declares none.

    The rule this feeds is "a non-empty declared pool replaces": an empty `rows` returns None, the
    same as never declaring the slot at all, so a child cannot clear a pool it inherited by
    redeclaring the key with nothing in it -- it can only replace the pool with another one.
    """

    if not isinstance(rows, list) or not rows:
        return None
    pool = []
    for row in rows:
        asset = row.get("asset") if isinstance(row, dict) else None
        if isinstance(asset, str) and asset:
            pool.append(asset)
    return pool or None


def _pool_slots(extension: dict) -> dict[str, list[str]]:
    """Every pool this unit's own entry declares, keyed by field path."""

    slots: dict[str, list[str]] = {}
    footsteps = extension.get("footsteps") or {}
    for side in ("left", "right"):
        pool = _pool(footsteps.get(side))
        if pool is not None:
            slots[f"footsteps.{side}"] = pool
    impacts = extension.get("impacts") or {}
    for weapon in IMPACT_WEAPONS:
        row = impacts.get(weapon) or {}
        for outcome in IMPACT_OUTCOMES:
            pool = _pool(row.get(outcome))
            if pool is not None:
                slots[f"impacts.{weapon}.{outcome}"] = pool
    legacy = _pool(impacts.get("legacy"))
    if legacy is not None:
        slots["impacts.legacy"] = legacy
    sounds = extension.get("sounds") or {}
    for kind in ("impact", "scrape"):
        pool = _pool(sounds.get(kind))
        if pool is not None:
            slots[f"sounds.{kind}"] = pool
    return slots


def _scalar_slots(extension: dict) -> dict[str, object]:
    """Every scalar this unit's own entry declares, keyed by field path."""

    slots: dict[str, object] = {}
    for path, section, key in SCALAR_FIELDS:
        block = extension.get(section) or {}
        if isinstance(block, dict) and key in block and block[key] is not None:
            slots[path] = block[key]
    letter = extension.get("gameMaterial")
    if isinstance(letter, str) and letter.strip():
        slots["gameMaterial"] = letter.strip()
    return slots


def flatten(key: str, chain: list[str], by_key: dict[str, Unit]) -> tuple[dict, dict[str, str]]:
    """Resolve one unit over its chain. Returns `(values, origins)` keyed by field path.

    Walked **root first** so a nearer entry's declaration lands last and wins. A declared scalar
    overrides the parent's scalar; a declared pool **replaces** the parent's whole pool for that
    slot, because Source copies the parent `surfacedata_t` and then re-parses the child's keys, so
    a child that names `stepleft` at all supplies the entire left-footstep pool.
    """

    values: dict[str, object] = {}
    origins: dict[str, str] = {}
    for member in [*chain, key]:
        extension = by_key[member].extension
        for path, value in _scalar_slots(extension).items():
            values[path] = value
            origins[path] = member
        for path, pool in _pool_slots(extension).items():
            values[path] = pool
            origins[path] = member
    return values, origins


def surface_type_for(letter: str | None) -> str:
    """The `EPhysicalSurface` row a compact material class maps to.

    A letter with no row is a refusal rather than a silent `SurfaceType_Default`: the row has to
    exist in `Config/DefaultEngine.ini` for the class to mean anything, and a corpus that grew a
    letter should say so once rather than land 63 assets with one quietly wrong.
    """

    if not letter:
        return DEFAULT_SURFACE_TYPE
    row = GAME_MATERIAL_SURFACE_TYPES.get(letter)
    if row is None:
        raise SurfacePropertyImportError(
            f"gameMaterial {letter!r} has no EPhysicalSurface row; add one to "
            "GAME_MATERIAL_SURFACE_TYPES and to Config/DefaultEngine.ini"
        )
    return row


def chain_fingerprint(chain: list[str], key: str, by_key: dict[str, Unit]) -> str:
    """A digest over every unit the flattened values could have come from, root first.

    The asset's recipe carries this as well as its own unit hash, because an ancestor's GLB
    changing changes this asset's values without changing its own bytes.
    """

    digest = hashlib.sha256()
    for member in [*chain, key]:
        digest.update(f"{member}:{by_key[member].sha256}\n".encode("utf-8"))
    return digest.hexdigest()


def sidecar_for(unit: Unit, chain: list[str], by_key: dict[str, Unit]) -> dict:
    """The provenance sidecar one unit stages: its flattened values and where each came from."""

    extension = unit.extension
    identity = extension.get("identity") or {}
    values, origins = flatten(unit.key, chain, by_key)
    letter = values.get("gameMaterial")
    letter = letter if isinstance(letter, str) else None

    # Every physics/movement scalar is emitted always -- `null` when no unit in the chain
    # declares it -- so a re-import can tell "still not declared" from "the sidecar predates this
    # field" and `ApplyJson` resets the asset's scalar to the class default rather than leaving a
    # value the source stopped declaring.
    physics = {name: values.get(f"physics.{name}")
               for name in ("friction", "elasticity", "density", "thickness")}
    # `density` in the table is kg/m3 (water 1000); `UPhysicalMaterial::Density` is g/cm3
    # (`BodySetup.cpp` multiplies by 0.001 to reach kg). The authored value survives as
    # `rawDensity` -- mirroring `elasticity` -> `Restitution`/`RawElasticity` -- and `density`
    # becomes the engine's own unit so a solid crate does not weigh 1000x too much.
    raw_density = physics["density"]
    physics["rawDensity"] = raw_density
    physics["density"] = raw_density / 1000.0 if raw_density is not None else None
    movement = {name: values.get(f"movement.{name}")
                for name in ("maxSpeedFactor", "jumpFactor", "climbable")}
    footsteps = {side: values.get(f"footsteps.{side}", []) for side in ("left", "right")}
    impacts: dict[str, dict[str, list[str]]] = {}
    for weapon in IMPACT_WEAPONS:
        row = {outcome: values[f"impacts.{weapon}.{outcome}"]
               for outcome in IMPACT_OUTCOMES if f"impacts.{weapon}.{outcome}" in values}
        if row:
            impacts[weapon] = {outcome: row.get(outcome, []) for outcome in IMPACT_OUTCOMES}
    sounds = {kind: values.get(f"sounds.{kind}", []) for kind in ("impact", "scrape")}

    coverage = extension.get("coverage") or {}
    ledger = coverage.get("byteLedger") or []
    percent = float(ledger[0].get("coveragePercent") or 0.0) if ledger else 0.0

    return {
        "assetId": identity.get("asset"),
        "name": unit.key,
        "sourceName": identity.get("sourceName") or unit.key,
        "assetPath": asset_path_for(unit.key),
        "unitGlb": f"{FAMILY}/{unit.key}.glb",
        "unitSchemaVersion": extension.get("schemaVersion"),
        "unitSha256": unit.sha256,
        "settingsVersion": SETTINGS_VERSION,
        "baseChain": list(chain),
        "gameMaterial": letter or "",
        "surfaceType": surface_type_for(letter),
        "physics": physics,
        "movement": movement,
        "footsteps": footsteps,
        "impacts": impacts,
        "bulletImpactLegacy": values.get("impacts.legacy", []),
        "sounds": sounds,
        "fieldOrigins": dict(sorted(origins.items())),
        "anomalies": [_line(row) for row in extension.get("anomalies") or ()],
        "coverage": {
            "percent": percent,
            "unresolved": [_line(row) for row in coverage.get("unresolved") or ()],
            "unsupported": [_line(row) for row in coverage.get("unsupported") or ()],
        },
    }


# --- staging -------------------------------------------------------------------------------------


def _write_if_changed(path: Path, data: bytes) -> bool:
    """Write `data` to `path` atomically unless the file already holds exactly these bytes."""

    if path.is_file() and path.stat().st_size == len(data) and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)
    return True


def _json_bytes(content: dict) -> bytes:
    return (json.dumps(content, indent=1, sort_keys=True) + "\n").encode("utf-8")


def _prune_stale(root: Path, produced: set[Path]) -> int:
    """Delete every file under `root` this run did not produce, then the folders left empty."""

    if not root.is_dir():
        return 0
    pruned = 0
    for path in list(root.rglob("*")):
        if path.is_dir() or path in produced:
            continue
        if path.parent == root and path.name in ROOT_FILES:
            continue
        path.unlink()
        pruned += 1
    for directory in sorted((p for p in root.rglob("*") if p.is_dir()),
                            key=lambda p: len(p.parts), reverse=True):
        try:
            directory.rmdir()
        except OSError:
            pass
    return pruned


def stage_surface_properties(export_v2_root: Path, staging_root_path: Path) -> StageResult:
    """Phase 1: flatten every unit over its base chain and write the manifest.

    See the module docstring. Refusals are per unit and never partial: a unit whose chain does not
    resolve is named with the ancestor that broke it, its asset path is protected from the editor
    phase's prune, and the rest of the corpus stages normally.
    """

    export_v2_root = Path(export_v2_root)
    root = Path(staging_root_path)
    result = StageResult(staging_root=root)
    found = units(export_v2_root)
    if not found:
        raise SurfacePropertyImportError(
            f"no surface-property units under {unit_root(export_v2_root)}; run "
            "`uv run elysium export_v2 surface-properties-glb` first"
        )

    failed_keys: list[str] = []

    def failed(key: str, reason: str) -> None:
        result.failures.append((key, reason))
        failed_keys.append(key)

    by_key: dict[str, Unit] = {}
    for path in found:
        key = unit_key(path)
        try:
            by_key[key] = read_unit(key, path.read_bytes(), path)
        except (GlbContainerError, SurfacePropertyImportError, OSError, ValueError, KeyError) as error:
            failed(key, str(error))

    bases = {key: unit.base for key, unit in by_key.items()}
    produced: set[Path] = set()
    assets: list[dict] = []
    for key in sorted(by_key):
        unit = by_key[key]
        try:
            chain = resolve_chain(key, bases)
            broken = [member for member in chain if member in failed_keys]
            if broken:
                raise SurfacePropertyImportError(
                    f"base {broken[0]!r} could not be read; the chain does not resolve")
            sidecar = sidecar_for(unit, chain, by_key)
            recipe = {
                "settingsVersion": SETTINGS_VERSION,
                "unitSha256": unit.sha256,
                "chainSha256": chain_fingerprint(chain, key, by_key),
                "surfaceType": sidecar["surfaceType"],
            }
        except (SurfacePropertyImportError, TypeError, ValueError, KeyError) as error:
            failed(key, str(error))
            continue

        relative = key + PROVENANCE_SUFFIX
        try:
            path = root / relative
            if _write_if_changed(path, _json_bytes(sidecar)):
                result.staged += 1
            else:
                result.unchanged += 1
            produced.add(path)
        except OSError as error:
            failed(key, str(error))
            continue

        assets.append({
            "assetPath": sidecar["assetPath"],
            "class": ASSET_CLASS,
            "unit": sidecar["assetId"],
            "unitKey": key,
            "unitGlb": sidecar["unitGlb"],
            "unitSha256": unit.sha256,
            "provenance": relative,
            "surfaceType": sidecar["surfaceType"],
            "gameMaterial": sidecar["gameMaterial"],
            "baseChain": list(chain),
            "recipe": recipe,
        })

    # Two keys can fold to the same `safe_name` (`Foo-Bar` and `foo_bar`) and land on one
    # assetPath. `load_manifest` refuses a manifest with the same assetPath listed twice wholesale,
    # so a collision here is a per-unit failure for every unit in it, not a silent pick of one --
    # the rest of the corpus stages normally and the colliding paths are protected from the prune
    # below rather than left orphaned.
    by_path: dict[str, list[str]] = {}
    for entry in assets:
        by_path.setdefault(entry["assetPath"], []).append(entry["unitKey"])
    colliding = {path: keys for path, keys in by_path.items() if len(keys) > 1}
    if colliding:
        assets = [entry for entry in assets if entry["assetPath"] not in colliding]
        for asset_path, keys in colliding.items():
            for key in sorted(keys):
                others = ", ".join(other for other in sorted(keys) if other != key)
                failed(key, f"assetPath {asset_path} collides with {others}")

    for entry in assets:
        result.roots += 1 if not entry["baseChain"] else 0
        result.inherited += 1 if entry["baseChain"] else 0
        result.deepest_depth = max(result.deepest_depth, len(entry["baseChain"]) + 1)

    # A failed unit keeps whatever it already has: its asset path stays out of the editor prune.
    keep: set[str] = set()
    for key in failed_keys:
        try:
            keep.add(asset_path_for(key))
        except SurfacePropertyImportError:
            continue   # an invalid key never owned an asset
    keep -= {entry["assetPath"] for entry in assets}
    result.protected = len(keep)

    assets.sort(key=lambda entry: entry["assetPath"].lower())
    manifest = {
        "schemaVersion": MANIFEST_SCHEMA,
        "settingsVersion": SETTINGS_VERSION,
        "packageRoot": PACKAGE_ROOT,
        "select": None,
        "pruneScope": prune_scope(),
        "keep": sorted(keep),
        "stageFailures": [{"unit": key, "reason": reason} for key, reason in result.failures],
        "assets": assets,
    }
    root.mkdir(parents=True, exist_ok=True)
    manifest_path = root / MANIFEST_NAME
    _write_if_changed(manifest_path, _json_bytes(manifest))
    result.manifest_path = manifest_path
    result.assets = len(assets)
    result.pruned = _prune_stale(root, produced)
    return result


def surface_type_rows() -> list[tuple[str, str]]:
    """`(SurfaceTypeN, VtmbGameMaterial_X)` for every compact class, in row order.

    What `Config/DefaultEngine.ini` must declare under `[/Script/Engine.PhysicsSettings]`; a
    pytest pins the file against this.
    """

    return [(GAME_MATERIAL_SURFACE_TYPES[letter], f"VtmbGameMaterial_{letter}")
            for letter in GAME_MATERIALS]
