"""Stage the referenced model corpus out of the published GLB units.

`uv run elysium import models` turns every **referenced** model unit into one `UStaticMesh` below
`/ElysiumBaked/Models/<dir>/SM_<base>`, slots bound to the landed V2 material instances,
collision cooked from VtMB's own convex hulls and complete skin families staged for the
merged static/skeletal corpus catalogue. This module is the offline stage half of that
lane; it mirrors `materials.py`'s shape:
stage every selected unit, write one provenance sidecar per unit plus one `manifest.json`, recipe
stamp, and let the editor phase (R1.4, not this module) do the headless import.

**No silent drop, and no silent guess.** Every slot, family index, LOD row, solid and
surface-property name this lane reads has a named destination or a named recorded consequence.
A unit whose input falls outside those is a **stage failure** naming the unit and the reason,
never an asset written with the unknown part quietly missing (see "Loud failures" in the design).

**Scope.** The stage refuses to run unscoped. `--maps <stems>` (`select_for_maps`) resolves exactly
the models the named maps' root units (`staticProps`/detail props) and entities units (every
`model.asset`) reference, through each unit's own `dependencies[]` role `model` rows -- the same
inverse relationship the corpus index's `identity.roles` publishes, read directly off the two map
units rather than off the whole-corpus index file, **plus the whole item ground corpus**, which no
map unit names (R5.1; see `item_ground_models`). `--all` (`select_all`) is the owner-approved
whole-corpus run: every published model unit whose own `identity.roles` (written back by the
corpus-index pass) is non-empty. Only `--all` prunes; a map-scoped run's `pruneScope` is `null`.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import configparser
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
from typing import Any, Callable, Sequence

from elysium_pipeline import paths
from elysium_pipeline.asset_names import safe_name
from elysium_pipeline.asset_paths import baked_unit, corpus_path
from elysium_pipeline.formats.bsp import source_to_unreal
from elysium_pipeline.formats.map_entities_glb import model as map_entities_model
from elysium_pipeline.formats.map_glb import model as map_model
from elysium_pipeline.formats.model_glb import model as model_unit_model
from elysium_pipeline.formats.unit_contract.container import GlbContainerError, decode_glb
from elysium_pipeline.importers import materials
from elysium_pipeline.importers import model_skins
from elysium_pipeline.importers import surface_properties
from elysium_pipeline.importers.surface_classes import _SURFACEPROP_NAMES

MODEL_EXTENSION = model_unit_model.MODEL_EXTENSION
MAP_EXTENSION = map_model.MAP_EXTENSION
MAP_ENTITIES_EXTENSION = map_entities_model.MAP_ENTITIES_EXTENSION

#: The family directory this lane reads below the export_v2 root.
FAMILY = "models"
#: Shared with characters, wield and catalogue producers; never a directory ownership claim.
PACKAGE_ROOT = "/ElysiumBaked/Models"
PRODUCER = "models"
#: The dangling-reference placeholder, authored by this lane, one shipped asset for the corpus.
MISSING_MODEL_ASSET_PATH = corpus_path("model", "SM", "Missing")
#: The sentinel material every `vtmb:missing-material:` slot binds.
MI_V2_MISSING = f"{materials.MASTER_ROOT}/MI_V2_Missing"
#: Bumped whenever this lane's mapping changes in a way that must re-stage every unit.
#: v2: bake_lib.set_phy_collision now disables GeometryScript's box/sphere/capsule
#: auto-detection so every .phy ledge reproduces as its own convex hull (the Settled
#: "not approximated" contract), instead of a
#: box- or primitive-shaped ledge being silently substituted with a fitted shape.
#: v3: unit-addressed SM products and producer-scoped publication under the shared Models root.
SETTINGS_VERSION = "elysium-model-import-v3"
MANIFEST_SCHEMA = "1.0.0"
MANIFEST_NAME = "manifest.json"
#: Written by the editor phase (R1.4), not this module; kept here so the CLI has one name to read.
IMPORT_REPORT_NAME = "import_report.json"
ROOT_FILES = frozenset({MANIFEST_NAME, IMPORT_REPORT_NAME})
PROVENANCE_SUFFIX = ".provenance.json"

_MODEL_INI_SECTION = "/Script/ElysiumUE.ElysiumModelSettings"
_LOD_SWITCH_CONSTANT_DEFAULT = 1.0
_LOD_SCREEN_FLOOR_DEFAULT = 0.001
_LOD_SCREEN_CEILING_DEFAULT = 0.9

#: The masters no bound material may resolve to and still let the mesh go Nanite.
_NANITE_VETO_MASTERS = frozenset({
    f"{materials.MASTER_ROOT}/M_V2_Refract", f"{materials.MASTER_ROOT}/M_V2_Water",
})
_NANITE_OK_BLEND_MODES = frozenset({"Opaque", "Masked"})


class ModelImportError(RuntimeError):
    """A model unit could not be staged."""


class ModelSkip(RuntimeError):
    """The unit admits no VTX topology and is skipped loudly rather than failed."""


def _read_model_settings(root: Path | None = None) -> tuple[float, float, float]:
    """`(LodSwitchConstant, LodScreenSizeFloor, LodScreenSizeCeiling)` out of
    `Config/DefaultElysium.ini`, `[/Script/ElysiumUE.ElysiumModelSettings]` -- wiring defaults when
    the ini, the section or a key is absent, never a literal at a call site."""

    constant, floor, ceiling = (
        _LOD_SWITCH_CONSTANT_DEFAULT, _LOD_SCREEN_FLOOR_DEFAULT, _LOD_SCREEN_CEILING_DEFAULT,
    )
    ini_path = (Path(root) if root is not None else paths.repo_root()) / "Config" / "DefaultElysium.ini"
    try:
        # `strict=False`: the same ini carries Unreal's `+Key=` array syntax (one line per element),
        # which a strict parser rejects as a duplicate option and which would otherwise send this
        # whole read down the `except` path to the defaults.
        parser = configparser.ConfigParser(interpolation=None, strict=False)
        parser.read(ini_path, encoding="utf-8")
        if parser.has_section(_MODEL_INI_SECTION):
            constant = parser.getfloat(_MODEL_INI_SECTION, "LodSwitchConstant", fallback=constant)
            floor = parser.getfloat(_MODEL_INI_SECTION, "LodScreenSizeFloor", fallback=floor)
            ceiling = parser.getfloat(_MODEL_INI_SECTION, "LodScreenSizeCeiling", fallback=ceiling)
    except (OSError, ValueError, configparser.Error):
        pass
    return constant, floor, ceiling


# --- roots, keys -------------------------------------------------------------------------------


def staging_root(work_root: Path) -> Path:
    return Path(work_root) / "import" / FAMILY


def unit_root(export_v2_root: Path) -> Path:
    return Path(export_v2_root) / FAMILY


def unit_key(export_v2_root: Path, unit: Path) -> str:
    relative = Path(unit).relative_to(unit_root(export_v2_root))
    return PurePosixPath(*relative.parts).with_suffix("").as_posix()


def unit_path_for(export_v2_root: Path, key: str) -> Path:
    return unit_root(export_v2_root) / model_unit_model.output_relative_path(key)


def static_stem(key: str) -> str:
    """`shared_corpus.static_stem`, over this seam's own `models/<key>.mdl` source path."""

    from elysium_pipeline import shared_corpus

    return shared_corpus.static_stem(model_unit_model.source_path(key))


def asset_path_for(key: str) -> str:
    """Resolve the unit key through the standard; a provenance stem is not an address."""
    return baked_unit(model_unit_model.asset_id(key), "SM")


def skin_set_asset_path() -> str:
    """Global catalogue address; its author must merge static AND skeletal inputs."""
    return corpus_path("model", "DA", "PropSkins")


def prune_scope() -> str:
    return PACKAGE_ROOT + "/"


# --- map-scoped selection ------------------------------------------------------------------------


def _model_keys_from_dependencies(extension: dict) -> set[str]:
    keys: set[str] = set()
    for row in extension.get("dependencies") or ():
        if not isinstance(row, dict) or row.get("role") != "model" or not row.get("resolved"):
            continue
        asset = row.get("asset")
        if isinstance(asset, str) and asset.startswith("vtmb:model:"):
            keys.add(asset[len("vtmb:model:"):])
    return keys


def _decode_unit_extension(path: Path, extension_name: str) -> dict:
    if not path.is_file():
        raise ModelImportError(f"map unit not found: {path}")
    document, _binary = decode_glb(path.read_bytes(), str(path))
    extension = (document.get("extensions") or {}).get(extension_name)
    if not isinstance(extension, dict):
        raise ModelImportError(f"{path} is not a {extension_name} unit")
    return extension


def referenced_models_for_map(export_v2_root: Path, map_stem: str) -> set[str]:
    """The model keys `vtmb:map:<map_stem>` and its entities unit reference, resolved through
    each unit's own `dependencies[]` role `model` rows (static props, detail props, entity `model`
    keyvalues) -- exactly the set `identity.roles` would name this map as a referrer of."""

    export_v2_root = Path(export_v2_root)
    root_path = export_v2_root / map_model.output_relative_path(map_stem)
    entities_path = export_v2_root / map_entities_model.output_relative_path(map_stem)
    keys = _model_keys_from_dependencies(_decode_unit_extension(root_path, MAP_EXTENSION))
    keys |= _model_keys_from_dependencies(_decode_unit_extension(entities_path, MAP_ENTITIES_EXTENSION))
    return keys


def item_ground_models(export_v2_root: Path | None = None, index=None) -> set[str]:
    """Every model key the item ground table names -- the models no map unit references.

    An `item_*` entity carries no `model` keyvalue: the runtime folds the item's own
    `vdata/items` `playermodel` into a model id and resolves it through the native model
    catalogue (`ElysiumItemContainer`/`ElysiumItemClasses`/`ElysiumLockable`/`ElysiumTerminal`
    are its four call sites). So the map units' `dependencies[]` do not name these models, and
    a map-scoped run that staged only what those units name left the running game with no mesh
    for them -- measured by R5.1 as 18 stems over 41 placements on the three-map working corpus.

    The whole table is staged rather than a per-map subset, because item placement is not a map
    fact: the player can drop any carried item on any map, so "which items can this map show" has
    no map-scoped answer. 124 models, 123 of them outside the three maps' unit-referenced set.

    R8 retired the legacy `items/ground_models.json` join with its exporter; the enumeration is
    read straight from the install's `vdata/items` definitions (`item_models.ground_models`),
    the same source the V2 vdata units and the native wield catalogue are published from. Two
    definitions declare a `playermodel` the install does not ship (`w_pistol`, the throwing
    star's ground model); the legacy join listed them under `skipped`, and here they are the
    keys with no published unit, left out of the selection as an explicit source gap.
    """

    from elysium_pipeline import item_models
    from elysium_pipeline.formats import install

    if index is None:
        index = install.build_index(dirs=("vdata",), verbose=False)
    keys = {
        map_model.model_key(path)
        for path in item_models.ground_models(index)
        if isinstance(path, str) and path
    }
    root = unit_root(export_v2_root) if export_v2_root is not None else None
    if root is None:
        return keys
    return {key for key in keys if (root / f"{key}.glb").is_file()}


def select_for_maps(export_v2_root: Path, map_stems: Sequence[str],
                    index=None) -> dict[str, Any]:
    """`{"perMap": ..., "itemGround": ..., "keys": sorted union}` -- the map-scoped selection the
    contract's `--maps` flag names.

    `perMap` is each stem's own unit-referenced set, resolved independently; `itemGround` is the
    map-independent item corpus above. The union of both is what gets staged.
    """

    if not map_stems:
        raise ModelImportError(
            "the model stage refuses to run unscoped -- pass --maps <stems> or --all"
        )
    per_map: dict[str, list[str]] = {}
    union: set[str] = set()
    for stem in map_stems:
        normalized = map_model.normalize_key(stem)
        found = referenced_models_for_map(export_v2_root, normalized)
        per_map[normalized] = sorted(found)
        union |= found
    ground = item_ground_models(export_v2_root, index)
    union |= ground
    return {"perMap": per_map, "itemGround": sorted(ground), "keys": sorted(union)}


def select_all(export_v2_root: Path) -> list[str]:
    """Every published model unit whose own `identity.roles` (written back by the corpus-index
    pass) is non-empty -- the 3,661 "published and referenced" whole-corpus scope. Reads every
    unit's identity block; only meant for the owner-approved `--all` run, never the working mode."""

    root = unit_root(export_v2_root)
    if not root.is_dir():
        return []
    keys: list[str] = []
    for unit in sorted(root.rglob("*.glb")):
        try:
            document, _binary = decode_glb(unit.read_bytes(), str(unit))
        except (GlbContainerError, OSError, ValueError):
            continue
        extension = (document.get("extensions") or {}).get(MODEL_EXTENSION)
        if not isinstance(extension, dict):
            continue
        roles = (extension.get("identity") or {}).get("roles") or []
        if roles:
            keys.append(unit_key(export_v2_root, unit))
    return sorted(keys)


# --- results -------------------------------------------------------------------------------------


@dataclass(slots=True)
class StageResult:
    staging_root: Path
    manifest_path: Path | None = None
    staged: int = 0
    skipped: int = 0
    pruned: int = 0
    protected: int = 0
    failures: list[tuple[str, str]] = field(default_factory=list)
    skips: list[tuple[str, str]] = field(default_factory=list)
    anomaly_counts: dict[str, int] = field(default_factory=dict)
    omission_counts: dict[str, int] = field(default_factory=dict)

    def summary(self) -> str:
        rollup = ", ".join(f"{kind}={count}" for kind, count in sorted(self.anomaly_counts.items()))
        return (
            f"model staging: {self.staged} staged, {self.skipped} skipped loudly, "
            f"{self.pruned} pruned, {len(self.failures)} failed "
            f"({self.protected} asset paths protected)"
            + (f" -- anomalies: {rollup}" if rollup else "")
            + f" -> {self.staging_root}"
        )


# --- one unit's material index -------------------------------------------------------------------


def load_material_index(materials_staging_root: Path | None) -> dict[str, dict[str, Any]]:
    """`{"vtmb:material:<key>": {"assetPath", "blendMode", "master"}}` off the landed material
    lane's own `manifest.json` -- the offline surrogate for "an asset on the mount": a material
    that never staged there cannot be bound here either. Empty (never raises) when the materials
    stage has not been run yet; every slot resolution then fails loudly on its own, which is the
    correct outcome ("the material corpus is a prerequisite of this lane")."""

    if materials_staging_root is None:
        return {}
    manifest_path = Path(materials_staging_root) / materials.MANIFEST_NAME
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    index: dict[str, dict[str, Any]] = {}
    for entry in manifest.get("assets") or ():
        if not isinstance(entry, dict):
            continue
        unit = entry.get("unit")
        if not isinstance(unit, str) or not unit.startswith("vtmb:material:"):
            continue
        # Skinned/decal/etc. products share a source unit. They must not overwrite the
        # canonical static route merely because a derived entry was appended last.
        if entry.get("assetPath") != baked_unit(unit, "MI"):
            continue
        overrides = entry.get("basePropertyOverrides") or {}
        index[unit] = {
            "assetPath": entry.get("assetPath"),
            "blendMode": overrides.get("blendMode"),
            "master": entry.get("parent"),
        }
    return index


# --- LOD / screen size -----------------------------------------------------------------------------


def _lod_switch_point(row: dict, anomalies: list[dict]) -> float:
    points = [p for p in (row.get("switchPoints") or [0.0]) if isinstance(p, (int, float))]
    if not points:
        points = [0.0]
    if len(set(points)) > 1:
        anomalies.append({
            "kind": "lodSwitchPointDisagreement", "lod": row.get("index"), "values": points,
        })
        return max(points)
    return points[0]


def _build_lods(
    lod_rows: list[dict], *, constant: float, floor: float, ceiling: float,
) -> tuple[list[dict], list[dict]]:
    """Every LOD row with its resolved `switchPoint`/`dropped`, in source order, plus the active
    (non-dropped) rows' `screenSize` -- the "-1.0-row drop" and the reciprocal `switchPoints ->
    ScreenSize` rule, in one pass."""

    anomalies: list[dict] = []
    rows: list[dict] = []
    for row in sorted(lod_rows, key=lambda r: r.get("index", 0)):
        switch_point = _lod_switch_point(row, anomalies)
        rows.append({
            "index": row.get("index"),
            "mesh": row.get("mesh"),
            "switchPoint": switch_point,
            "dropped": switch_point == -1.0,
            "primitiveCount": row.get("primitiveCount"),
            "screenSize": None,
        })
    active = [r for r in rows if not r["dropped"]]
    previous: float | None = None
    for position, row in enumerate(active):
        if position == 0:
            size = 1.0
        else:
            raw = (constant / row["switchPoint"]) if row["switchPoint"] else ceiling
            size = min(max(raw, floor), ceiling)
            if previous is not None and size >= previous:
                anomalies.append({
                    "kind": "lodScreenSizeNotMonotone", "lod": row["index"], "computed": size,
                })
                size = min(previous * 0.5, size)
        row["screenSize"] = size
        previous = size
    return rows, anomalies


# --- geometry / slots --------------------------------------------------------------------------


def _mesh_primitives(document: dict, mesh_index: int | None) -> list[dict]:
    if mesh_index is None:
        return []
    meshes = document.get("meshes") or ()
    if not (0 <= mesh_index < len(meshes)):
        return []
    return list(meshes[mesh_index].get("primitives") or ())


def _primitive_ext(primitive: dict) -> dict:
    return ((primitive.get("extensions") or {}).get(MODEL_EXTENSION)) or {}


def _submodel0_primitives(document: dict, mesh_index: int | None) -> list[dict]:
    out = []
    for primitive in _mesh_primitives(document, mesh_index):
        ext = _primitive_ext(primitive)
        if int(ext.get("model") or 0) == 0:
            out.append(primitive)
    return out


def _triangle_count(document: dict, primitives: list[dict]) -> int:
    accessors = document.get("accessors") or ()
    total = 0
    for primitive in primitives:
        index = primitive.get("indices")
        if isinstance(index, int) and 0 <= index < len(accessors):
            total += int(accessors[index].get("count") or 0) // 3
    return total


def _slots_by_index(slots: list[dict]) -> dict[int, dict]:
    return {row["slot"]: row for row in slots if isinstance(row, dict) and "slot" in row}


def _disambiguate_slot_names(slots: list[dict]) -> tuple[dict[int, str], list[str]]:
    """`({skin-table slot index -> disambiguated name}, [collided base names])`, over the
    **whole** skin table (`materialBindings.slots[]`), not only the columns a LOD's primitives
    happen to draw -- "Duplicate folded slot names get a suffix": occurrence 0 (table order) keeps
    `safe_name(sourceName)` verbatim, occurrence n>0 becomes `<name>_<slot index>`, and "the skin
    table is written with the same disambiguated names, so a family now reaches every slot" names
    every column, drawn or not. Computed once and shared by the Unreal slot list (which only ever
    surfaces the drawn subset) and the skin-families table (which states every column)."""

    ordered = sorted(
        (row for row in slots if isinstance(row, dict) and "slot" in row),
        key=lambda row: row["slot"],
    )
    name_counts: dict[str, int] = {}
    names: dict[int, str] = {}
    for row in ordered:
        base_name = safe_name(str(row.get("sourceName") or ""))
        seen = name_counts.get(base_name, 0)
        names[row["slot"]] = base_name if seen == 0 else f"{base_name}_{row['slot']}"
        name_counts[base_name] = seen + 1
    collided = sorted(name for name, count in name_counts.items() if count > 1)
    return names, collided


def _build_slot_table(
    key: str, document: dict, extension: dict, lod0_mesh_index: int | None,
    *, slot_names: dict[int, str], material_index: dict[str, dict], anomalies: list[dict],
) -> tuple[list[dict], list[int]]:
    """`(slot rows, skinReference order)` -- the Unreal `UStaticMesh` material slot list, built
    from LOD 0's primitives in first-use order ("Geometry"), named from `slot_names`."""

    material_bindings = extension.get("materialBindings") or {}
    slots = material_bindings.get("slots") or []
    families = material_bindings.get("skinFamilies") or []
    by_index = _slots_by_index(slots)

    order: list[int] = []
    for primitive in _submodel0_primitives(document, lod0_mesh_index):
        skin_reference = _primitive_ext(primitive).get("skinReference")
        if isinstance(skin_reference, int) and skin_reference not in order:
            order.append(skin_reference)
    if not order:
        # No primitive named a skinReference (a unit with no admitted topology never reaches
        # here -- see `ModelSkip` -- so this is every slot the skin table itself declares, in
        # source order, the fallback the design's "0 units" claim never needs but a defensive
        # stage should still name something for.
        order = sorted(by_index)

    rows: list[dict] = []
    default_family = families[0] if families else []
    for position, skin_reference in enumerate(order):
        source = by_index.get(skin_reference)
        if source is None:
            raise ModelImportError(
                f"{key}: LOD 0 primitive names skinReference {skin_reference} with no "
                f"materialBindings.slots row"
            )
        slot_name = slot_names.get(skin_reference) or safe_name(str(source.get("sourceName") or ""))

        material_id = default_family[skin_reference] if skin_reference < len(default_family) else None
        if not isinstance(material_id, str) or not material_id:
            raise ModelImportError(
                f"{key}: slot {slot_name!r} (skinReference {skin_reference}) names neither a "
                f"vtmb:material: id nor a vtmb:missing-material: sentinel"
            )
        is_sentinel = material_id.startswith("vtmb:missing-material:")
        if is_sentinel:
            material_asset = MI_V2_MISSING
            anomalies.append({
                "kind": "missingMaterialSentinel", "slot": slot_name,
                "sourceName": source.get("sourceName"), "sentinel": material_id,
            })
        elif material_id.startswith("vtmb:material:"):
            resolved = material_index.get(material_id)
            if resolved is None or not resolved.get("assetPath"):
                raise ModelImportError(
                    f"{key}: slot {slot_name!r} material {material_id!r} has no asset on the "
                    f"materials mount -- run `uv run elysium import materials` first"
                )
            material_asset = resolved["assetPath"]
        else:
            raise ModelImportError(f"{key}: slot {slot_name!r} names an unrecognised id {material_id!r}")

        rows.append({
            "index": position,
            "slotName": slot_name,
            "sourceName": source.get("sourceName"),
            "sourcePath": source.get("sourcePath"),
            "skinReference": skin_reference,
            "mdlSlotIndex": source.get("slot"),
            "materialId": material_id,
            "materialAsset": material_asset,
            "isSentinel": is_sentinel,
            "resolved": bool(source.get("resolved", True)),
            "surfaceProperty": source.get("surfaceProperty"),
        })

    return rows, order


def _check_higher_lods(
    key: str, document: dict, extension: dict, active_lods: list[dict], skin_reference_order: list[int],
) -> None:
    known = set(skin_reference_order)
    for row in active_lods:
        if row["index"] == active_lods[0]["index"]:
            continue
        for primitive in _submodel0_primitives(document, row["mesh"]):
            skin_reference = _primitive_ext(primitive).get("skinReference")
            if skin_reference not in known:
                raise ModelImportError(
                    f"{key}: LOD {row['index']} names skinReference {skin_reference}, "
                    f"absent from LOD 0's slot list"
                )


def _build_skin_families(
    key: str, extension: dict, slot_names: dict[int, str],
    *, material_index: dict[str, dict],
) -> tuple[list[dict], int]:
    """Every family, resolved to per-slot `MI_` paths over the **whole** skin table (every column
    of `materialBindings.slots[]`, drawn by LOD 0 or not) -- "Skins table" ->
    `(stem, family index) -> [(slot name, MI_ asset) ...]`, before the diff-only fold the
    corpus-wide `DA_ElysiumPropSkins` table applies (R1.5's job, not this stage's)."""

    material_bindings = extension.get("materialBindings") or {}
    families = material_bindings.get("skinFamilies") or []
    columns = sorted(slot_names)

    def resolve(material_id: Any) -> str:
        if not isinstance(material_id, str):
            return MI_V2_MISSING
        if material_id.startswith("vtmb:missing-material:"):
            return MI_V2_MISSING
        resolved = material_index.get(material_id)
        if resolved is None or not resolved.get("assetPath"):
            raise ModelImportError(
                f"{key}: skin family material {material_id!r} has no asset on the materials mount"
            )
        return resolved["assetPath"]

    rows: list[dict] = []
    for family_index, family in enumerate(families):
        materials_by_slot = []
        material_ids = []
        for column in columns:
            material_id = family[column] if column < len(family) else None
            materials_by_slot.append(resolve(material_id))
            material_ids.append(material_id if isinstance(material_id, str) else None)
        rows.append({
            "family": family_index, "materials": materials_by_slot, "materialIds": material_ids,
            "slots": [slot_names[column] for column in columns],
        })
    return rows, len(families)


# --- collision / surface property / Nanite -------------------------------------------------------


def _build_collision(extension: dict) -> dict:
    physics = extension.get("physics")
    header = (extension.get("mdl") or {}).get("header") or {}
    if isinstance(physics, dict) and physics.get("solids"):
        solids = physics["solids"]
        hull_count = sum(len(solid.get("hulls") or ()) for solid in solids)
        properties = solids[0].get("properties") or {}
        return {
            "mode": "phy",
            "solidCount": len(solids),
            "hullCount": hull_count,
            "massKg": properties.get("mass"),
            "collisionTraceFlag": "CTF_UseSimpleAndComplex",
            "hullBounds": None,
        }
    hull_min = header.get("hullMin")
    hull_max = header.get("hullMax")
    bounds = None
    if isinstance(hull_min, (list, tuple)) and isinstance(hull_max, (list, tuple)):
        a = source_to_unreal(*hull_min)
        b = source_to_unreal(*hull_max)
        bounds = {
            "min": [min(a[i], b[i]) for i in range(3)],
            "max": [max(a[i], b[i]) for i in range(3)],
        }
    return {
        "mode": "bbox",
        "solidCount": 0,
        "hullCount": 0,
        "massKg": None,
        "collisionTraceFlag": "CTF_UseSimpleAndComplex",
        "hullBounds": bounds,
    }


def _resolve_surface_property(extension: dict) -> tuple[str, str]:
    physics = extension.get("physics")
    if isinstance(physics, dict) and physics.get("solids"):
        value = ((physics["solids"][0].get("properties") or {}).get("surfaceprop") or "").strip()
        if value:
            return value.lower(), "physSolid"
    header_value = (((extension.get("mdl") or {}).get("header") or {}).get("surfaceProperty") or "").strip()
    if header_value:
        return header_value.lower(), "mdlHeader"
    return "default", "default"


def _nanite_decision(
    skin_families: list[dict], *, material_index: dict[str, dict],
) -> tuple[bool, str | None, str | None]:
    """"Nanite is on iff every material the model can ever wear is opaque or masked" -- tested
    across every slot of every skin family, not just family 0 (`skin_families` already carries
    each family's resolved `materialIds` alongside its `MI_` asset paths)."""

    for family in skin_families:
        for slot_name, asset_path, material_id in zip(
            family["slots"], family["materials"], family["materialIds"],
        ):
            if asset_path == MI_V2_MISSING or material_id is None:
                continue  # the sentinel is opaque and never vetoes
            entry = material_index.get(material_id)
            if entry is None:
                continue
            if entry.get("blendMode") not in _NANITE_OK_BLEND_MODES or entry.get("master") in _NANITE_VETO_MASTERS:
                return False, slot_name, asset_path
    return True, None, None


# --- staging one unit --------------------------------------------------------------------------


def stage_unit(
    key: str, document: dict, unit_sha256: str, *, material_index: dict[str, dict],
    model_settings: tuple[float, float, float] | None = None,
) -> tuple[dict, dict]:
    """`(manifest entry, provenance sidecar)` for one unit; raises `ModelImportError` on a stage
    failure or `ModelSkip` when the unit admits no VTX topology."""

    extension = (document.get("extensions") or {}).get(MODEL_EXTENSION)
    if not isinstance(extension, dict):
        raise ModelImportError(f"{key}: not a {MODEL_EXTENSION} unit")
    identity = extension.get("identity") or {}
    asset_id = identity.get("asset")
    if not isinstance(asset_id, str) or not asset_id.startswith("vtmb:model:"):
        raise ModelImportError(f"{key}: unit declares no model identity")
    if asset_id != model_unit_model.asset_id(key):
        raise ModelImportError(f"{key}: published identity {asset_id!r} disagrees with the selected unit")
    shape = identity.get("shape")
    family = identity.get("family")
    roles = list(identity.get("roles") or ())

    mdl = extension.get("mdl") or {}
    body_parts = mdl.get("bodyParts") or []
    if body_parts and not document.get("meshes"):
        raise ModelSkip(f"{key}: declares body parts but publishes no meshes -- no admitted VTX topology")

    vtx = extension.get("vtx") or {}
    lod_rows = list(vtx.get("lods") or ())
    if not lod_rows:
        raise ModelSkip(f"{key}: publishes no VTX LODs -- no admitted VTX topology")

    if model_settings is None:
        model_settings = _read_model_settings()
    constant, floor, ceiling = model_settings
    lods, lod_anomalies = _build_lods(lod_rows, constant=constant, floor=floor, ceiling=ceiling)
    active_lods = [row for row in lods if not row["dropped"]]
    if not active_lods:
        raise ModelSkip(f"{key}: every LOD row is a dropped shadow LOD")

    anomalies: list[dict] = list(lod_anomalies)
    omissions: list[dict] = []

    material_bindings = extension.get("materialBindings") or {}
    slot_names, duplicate_names = _disambiguate_slot_names(material_bindings.get("slots") or [])
    if duplicate_names:
        anomalies.append({"kind": "duplicateSlotName", "names": duplicate_names})

    lod0_mesh_index = active_lods[0]["mesh"]
    slot_rows, skin_reference_order = _build_slot_table(
        key, document, extension, lod0_mesh_index,
        slot_names=slot_names, material_index=material_index, anomalies=anomalies,
    )
    _check_higher_lods(key, document, extension, active_lods, skin_reference_order)

    skin_families, family_count = _build_skin_families(
        key, extension, slot_names, material_index=material_index,
    )
    nanite, veto_slot, veto_material = _nanite_decision(skin_families, material_index=material_index)

    # Geometry stats, submodel-0 selection and the multi-submodel anomaly.
    for row in lods:
        primitives = _submodel0_primitives(document, row["mesh"])
        row["sections"] = len(primitives)
        row["triangles"] = _triangle_count(document, primitives)
    multi_submodel_bodyparts = [
        {"bodyPart": bp.get("index"), "droppedSubmodels": list(range(1, len(bp.get("models") or ())))}
        for bp in body_parts if len(bp.get("models") or ()) > 1
    ]
    if multi_submodel_bodyparts:
        anomalies.append({"kind": "multiSubmodelBakedZero", "bodyParts": multi_submodel_bodyparts})

    # The one-bone-skin drop: joint 0 must be identity on a static-shape unit.
    joint_identity_ok = True
    joint_transform = None
    if shape == model_unit_model.SHAPE_STATIC:
        nodes = document.get("nodes") or ()
        node0 = nodes[0] if nodes else {}
        translation = node0.get("translation") or [0.0, 0.0, 0.0]
        rotation = node0.get("rotation") or [0.0, 0.0, 0.0, 1.0]
        joint_transform = {"translation": translation, "rotation": rotation}
        joint_identity_ok = (
            all(abs(v) < 1e-4 for v in translation)
            and all(abs(v) < 1e-4 for v in rotation[:3]) and abs(rotation[3] - 1.0) < 1e-4
        )
        if not joint_identity_ok:
            raise ModelImportError(
                f"{key}: joint 0 is not identity on a static-shape unit -- {joint_transform}"
            )

    collision = _build_collision(extension)
    if collision["mode"] == "bbox":
        omissions.append({
            "kind": "noPhysicsSolidsBoxFallback", "hullBounds": collision["hullBounds"],
        })
    surface_class, surface_class_source = _resolve_surface_property(extension)
    surface_known = surface_class in _SURFACEPROP_NAMES
    if not surface_known:
        anomalies.append({"kind": "surfacePropertyUnknown", "value": surface_class})
    phys_material_key = surface_class if surface_known else "default"
    phys_material_path = surface_properties.asset_path_for(phys_material_key)

    stem = static_stem(key)
    asset_path = asset_path_for(key)
    sources = extension.get("sourceResolution", {}).get("members") or ()
    source_sha256 = [
        {"role": member.get("role"), "sha256": member.get("sha256")}
        for member in sources if isinstance(member, dict)
    ]

    entry_anomalies = anomalies + list(extension.get("anomalies") or ())
    entry_omissions = omissions + list(extension.get("omissions") or ())

    entry = {
        "assetPath": asset_path,
        "unit": asset_id,
        "unitGlb": f"{FAMILY}/{key}.glb",
        "unitSha256": unit_sha256,
        "stem": stem,
        "shape": shape,
        "family": family,
        "roles": roles,
        "slots": slot_rows,
        "skinFamilies": skin_families,
        "familyCount": family_count,
        "lods": lods,
        "collision": collision,
        "surfaceClass": surface_class,
        "surfaceClassSource": surface_class_source,
        "surfaceClassKnown": surface_known,
        "physMaterial": phys_material_path,
        "nanite": nanite,
        "naniteVetoSlot": veto_slot,
        "naniteVetoMaterial": veto_material,
        "recipe": {
            "unitSha256": unit_sha256,
            "settingsVersion": SETTINGS_VERSION,
            "modelSettings": {"lodSwitchConstant": constant, "lodScreenSizeFloor": floor,
                               "lodScreenSizeCeiling": ceiling},
            "slots": [(row["slotName"], row["materialAsset"]) for row in slot_rows],
            "skinFamilies": [(row["family"], row["materials"]) for row in skin_families],
            "lodScreenSizes": [(row["index"], row["screenSize"]) for row in active_lods],
            "collision": collision,
            "nanite": nanite,
            "physMaterial": phys_material_path,
            "provenanceSha256": None,
        },
    }

    provenance = {
        "assetId": asset_id,
        "modelPath": identity.get("modelPath"),
        "stem": stem,
        "unitSchemaVersion": extension.get("schemaVersion"),
        "unitSha256": unit_sha256,
        "sourceSha256": source_sha256,
        "settingsVersion": SETTINGS_VERSION,
        "shape": shape,
        "family": family,
        "roles": roles,
        "slots": slot_rows,
        "skinFamilies": skin_families,
        "familyCount": family_count,
        "lods": lods,
        "bNanite": nanite,
        "naniteVetoSlot": veto_slot,
        "naniteVetoMaterial": veto_material,
        "collisionMode": collision["mode"],
        "hullCount": collision["hullCount"],
        "solidCount": collision["solidCount"],
        "massKg": collision["massKg"],
        "hullBounds": collision["hullBounds"],
        "collisionTraceFlag": collision["collisionTraceFlag"],
        "surfaceProperty": surface_class,
        "surfacePropertySource": surface_class_source,
        "physMaterial": phys_material_path,
        "jointIdentity": joint_transform,
        "anomalies": entry_anomalies,
        "omissions": entry_omissions,
        "coverage": {"unmappedKeys": []},
    }
    entry["recipe"]["provenanceSha256"] = hashlib.sha256(_json_bytes(provenance)).hexdigest()
    return entry, provenance


def _json_bytes(content: dict) -> bytes:
    return (json.dumps(content, indent=1, sort_keys=True) + "\n").encode("utf-8")


def _write_if_changed(path: Path, data: bytes) -> bool:
    if path.is_file() and path.stat().st_size == len(data) and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)
    return True


def _sidecar_path(root: Path, key: str) -> Path:
    parts = PurePosixPath(key).parts
    return root.joinpath(*parts[:-1], parts[-1] + PROVENANCE_SUFFIX)


def _prune_stale(root: Path, produced: set[Path]) -> int:
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


def _fold_owner_collisions(entries: list[dict], failed: Callable[[str, str], None]) -> list[dict]:
    """Two units folding to one `SM_` path is a defect in the fold, not a race one wins.

    Takes the whole entry list a manifest is about to name -- this run's own staged rows the
    first time it runs, and again over the merged list a scoped run's `_merge_prior_manifest`
    hands back, so a unit this run kept from a prior run can still collide with one it staged
    itself.
    """

    owners: dict[str, list[str]] = {}
    for entry in entries:
        owners.setdefault(entry["assetPath"].lower(), []).append(entry["unit"])
    collided_paths = {path for path, units in owners.items() if len(units) > 1}
    if not collided_paths:
        return entries
    kept: list[dict] = []
    for entry in entries:
        lowered = entry["assetPath"].lower()
        if lowered in collided_paths:
            others = sorted(set(owners[lowered]) - {entry["unit"]})
            failed(entry["unit"][len("vtmb:model:"):], f"asset path collides with {', '.join(others)}")
        else:
            kept.append(entry)
    return kept


def _load_prior_manifest_for_merge(manifest_path: Path) -> dict | None:
    """Refuse a scoped migration before writing: old rows cannot be silently relabelled/lost."""

    if not manifest_path.is_file():
        return None
    try:
        document = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ModelImportError(f"cannot merge prior model manifest; restage --all: {manifest_path}: {exc}") from exc
    if (not isinstance(document, dict) or document.get("schemaVersion") != MANIFEST_SCHEMA
            or document.get("settingsVersion") != SETTINGS_VERSION
            or document.get("producer") != PRODUCER or document.get("packageRoot") != PACKAGE_ROOT):
        raise ModelImportError("prior model stage has different settings/root/ownership; restage --all before a scoped run")
    for entry in document.get("assets", ()):
        id = entry.get("unit", "")
        if not id.startswith("vtmb:model:") or entry.get("assetPath") != baked_unit(id, "SM"):
            raise ModelImportError(f"noncanonical prior model entry {id!r}; restage --all")
    if any(not isinstance(path, str) or not path.startswith(PACKAGE_ROOT + "/") for path in document.get("keep", ())):
        raise ModelImportError("prior model keep set contains a foreign root; restage --all")
    return document


def _merge_prior_manifest(
    prior_manifest: dict, entries: list[dict], keys: Sequence[str],
) -> tuple[list[dict], set[str]]:
    """Carry forward the rows and `keep` protection a scoped run's narrower selection leaves out.

    `manifest.json` for a `--maps` run only ever lists this run's own `keys` -- the disk still
    holds every sidecar an earlier, wider run staged (a scoped run never prunes), so a manifest
    that dropped their rows would name a set the disk no longer matches. A prior row survives if
    its unit is not in `keys` -- this run neither staged nor failed it, so the prior row is still
    the only description of it there is. `keep` is unioned the same way, minus the units this run
    named itself: this run's own `failed()` calls below are authoritative for those.
    """

    keys_set = set(keys)
    carried = [
        entry for entry in prior_manifest.get("assets") or ()
        if isinstance(entry, dict) and isinstance(entry.get("unit"), str)
        and entry["unit"].startswith("vtmb:model:")
        and entry["unit"][len("vtmb:model:"):] not in keys_set
    ]
    prior_keep = {
        path for path in prior_manifest.get("keep") or () if isinstance(path, str)
    } - {asset_path_for(key) for key in keys}
    return entries + carried, prior_keep


def stage_models(
    export_v2_root: Path, staging_root_path: Path, *,
    maps: Sequence[str] | None = None, all_models: bool = False, units: Sequence[str] | None = None,
    materials_staging_root: Path | None = None,
) -> StageResult:
    """Phase 1: stage every model the selection names and write the manifest.

    Exactly one of `maps` or `all_models` must be given -- the stage refuses to run unscoped.

    A `--maps` run's manifest is a merge onto the prior one, not a replacement: `_prune_stale`
    never runs for a scoped selection, so the disk still holds every sidecar an earlier, wider
    run staged, and the manifest has to keep naming them or it describes a set the disk no longer
    matches. `_merge_prior_manifest` carries forward the rows and `keep` entries this run's own
    `keys` do not name. A schema/settings/root/producer mismatch refuses the scoped run before
    writing; the canonical migration requires an explicit whole-corpus restage.
    """

    if sum(map(bool, (maps, all_models, units))) != 1:
        raise ModelImportError(
            "pass exactly one of --maps <stems>, --units <model IDs> or --all -- the model stage refuses to run unscoped"
        )

    export_v2_root = Path(export_v2_root)
    root = Path(staging_root_path)
    result = StageResult(staging_root=root)
    prior_manifest = _load_prior_manifest_for_merge(root / MANIFEST_NAME) if not all_models else None

    if all_models:
        keys = select_all(export_v2_root)
        selection = {"perMap": None, "keys": keys}
        prune_scope_value: str | None = prune_scope()
    elif units:
        keys = []
        for identity in units:
            if not identity.startswith("vtmb:model:"):
                raise ModelImportError("--units requires full vtmb:model: identities: " + identity)
            key = identity.removeprefix("vtmb:model:")
            if model_unit_model.asset_id(key) != identity:
                raise ModelImportError("--units requires a canonical model identity: " + identity)
            keys.append(key)
        keys = sorted(set(keys))
        selection = {"perMap": {}, "keys": keys, "explicitUnits": sorted(set(units))}
        prune_scope_value = None
    else:
        selection = select_for_maps(export_v2_root, list(maps or ()))
        keys = selection["keys"]
        prune_scope_value = None

    if not keys:
        raise ModelImportError(
            f"no model units resolved for this selection under {unit_root(export_v2_root)}"
        )

    material_index = load_material_index(materials_staging_root)
    model_settings = _read_model_settings()

    entries: list[dict] = []
    produced: set[Path] = set()
    failed_keys: list[str] = []

    def failed(key: str, reason: str) -> None:
        result.failures.append((key, reason))
        failed_keys.append(key)

    for key in keys:
        unit_path = unit_path_for(export_v2_root, key)
        try:
            data = unit_path.read_bytes()
            unit_sha256 = hashlib.sha256(data).hexdigest()
            document, _binary = decode_glb(data, str(unit_path))
            entry, provenance = stage_unit(
                key, document, unit_sha256, material_index=material_index,
                model_settings=model_settings,
            )
        except ModelSkip as skip:
            result.skipped += 1
            result.skips.append((key, str(skip)))
            continue
        except (GlbContainerError, ModelImportError, OSError, ValueError, KeyError) as error:
            failed(key, str(error))
            continue

        result.staged += 1
        for row in provenance["anomalies"]:
            kind = row.get("kind") if isinstance(row, dict) else None
            if kind:
                result.anomaly_counts[kind] = result.anomaly_counts.get(kind, 0) + 1
        for row in provenance["omissions"]:
            kind = row.get("kind") if isinstance(row, dict) else None
            if kind:
                result.omission_counts[kind] = result.omission_counts.get(kind, 0) + 1
        entries.append(entry)

        sidecar_path = _sidecar_path(root, key)
        try:
            _write_if_changed(sidecar_path, _json_bytes(provenance))
        except OSError as error:
            failed(key, str(error))
            entries.pop()
            continue
        produced.add(sidecar_path)

    entries = _fold_owner_collisions(entries, failed)

    manifest_path = root / MANIFEST_NAME
    prior_keep: set[str] = set()
    if not all_models:
        if prior_manifest is not None:
            entries, prior_keep = _merge_prior_manifest(prior_manifest, entries, keys)
            entries = _fold_owner_collisions(entries, failed)

    keep: set[str] = {MISSING_MODEL_ASSET_PATH} | prior_keep
    for key in failed_keys:
        keep.add(asset_path_for(key))
        stale_sidecar = _sidecar_path(root, key)
        if stale_sidecar.exists():
            produced.add(stale_sidecar)
    named = {entry["assetPath"] for entry in entries}
    keep -= named
    result.protected = len(keep)

    entries.sort(key=lambda entry: entry["assetPath"].lower())
    manifest = {
        "schemaVersion": MANIFEST_SCHEMA,
        "settingsVersion": SETTINGS_VERSION,
        "producer": PRODUCER,
        "packageRoot": PACKAGE_ROOT,
        "materialMasterRoot": materials.MASTER_ROOT,
        "missingModelAsset": MISSING_MODEL_ASSET_PATH,
        "missingMaterialAsset": MI_V2_MISSING,
        "skinCatalogueAsset": skin_set_asset_path(),
        "selection": selection,
        "pruneScope": prune_scope_value,
        "keep": sorted(keep),
        "stageFailures": [{"unit": key, "reason": reason} for key, reason in result.failures],
        "skipped": [{"unit": key, "reason": reason} for key, reason in result.skips],
        "anomalyCounts": dict(sorted(result.anomaly_counts.items())),
        "omissionCounts": dict(sorted(result.omission_counts.items())),
        "assets": entries,
    }
    root.mkdir(parents=True, exist_ok=True)
    _write_if_changed(manifest_path, _json_bytes(manifest))
    produced.add(manifest_path)
    result.manifest_path = manifest_path
    result.pruned = _prune_stale(root, produced) if all_models else 0
    return result


# --- skins table (R1.5) -------------------------------------------------------------------------
#
# The legacy fold remains available until replacement acceptance. Canonical publication uses
# prop_skin_catalogue over merged static/skeletal inputs, not this stem-keyed partial fold.
build_skin_table = model_skins.build_skin_table
