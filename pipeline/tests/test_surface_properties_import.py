"""Contract tests for `uv run elysium import surface-properties` (the offline stage).

Every fixture is a synthetic surface-property unit built into a temporary `export_v2` root from a
hand-written extension body -- never the real install, never the machine's staging tree. The GLB
is the real container (`unit_contract.container.encode_glb`), so the stage reads these exactly the
way it reads a published unit.

What is under test is the flattening rule, not the decode: the decode has its own tests beside the
exporter, and the only thing this lane adds is resolving a `base` chain the seam deliberately does
not resolve.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from elysium_pipeline.formats.surface_property_glb.model import (
    SCHEMA_VERSION,
    SURFACE_PROPERTY_EXTENSION,
)
from elysium_pipeline.formats.unit_contract.container import encode_glb
from elysium_pipeline.importers import surface_properties as importer

REPO = Path(__file__).resolve().parents[2]


# --- fixtures ------------------------------------------------------------------------------------


def _sound(path: str) -> dict:
    return {"path": path, "asset": "vtmb:sound:" + path.lower(), "parameter": 0}


def _script(name: str) -> dict:
    return {"script": name, "asset": "vtmb:sound-script:" + name.lower(), "resolved": True,
            "parameter": 0}


def _unit(root: Path, key: str, *, base: str | None = None, physics: dict | None = None,
          movement: dict | None = None, footsteps: dict | None = None,
          impacts: dict | None = None, sounds: dict | None = None,
          game_material: str | None = None, anomalies: list | None = None,
          source_name: str | None = None, coverage_percent: float = 100.0) -> Path:
    """One synthetic unit on disk, carrying only what its own entry declares."""

    extension = {
        "schemaVersion": SCHEMA_VERSION,
        "identity": {"asset": f"vtmb:surface-property:{key}", "name": key,
                     "sourceName": source_name or key},
        "sourceResolution": {"policy": "up-first", "members": []},
        "base": None if base is None else {
            "name": base, "sourceName": base, "asset": f"vtmb:surface-property:{base}",
            "resolved": True, "parameter": 0},
        "physics": physics or {},
        "movement": movement or {},
        "footsteps": footsteps or {},
        "impacts": impacts or {},
        "sounds": sounds or {},
        "gameMaterial": game_material,
        "parameters": [],
        "dependencies": [],
        "comments": [],
        "anomalies": anomalies or [],
        "omissions": [],
        "coverage": {
            "mapped": [], "unresolved": [], "unsupported": [],
            "byteLedger": [{"sourcePath": f"scripts/surfaceproperties.txt#{key}",
                            "coveragePercent": coverage_percent}],
        },
    }
    document = {
        "asset": {"version": "2.0", "generator": "test"},
        "extensionsUsed": [SURFACE_PROPERTY_EXTENSION],
        "extensionsRequired": [SURFACE_PROPERTY_EXTENSION],
        "extensions": {SURFACE_PROPERTY_EXTENSION: extension},
    }
    root.mkdir(parents=True, exist_ok=True)
    path = root / f"{key}.glb"
    path.write_bytes(encode_glb(document))
    return path


def _corpus(tmp_path: Path) -> Path:
    return tmp_path / "exports_v2" / importer.FAMILY


def _sidecar(result, key: str) -> dict:
    return json.loads((result.staging_root / (key + importer.PROVENANCE_SUFFIX))
                      .read_text(encoding="utf-8"))


def _manifest(result) -> dict:
    return json.loads(result.manifest_path.read_text(encoding="utf-8"))


def _stage(tmp_path: Path):
    return importer.stage_surface_properties(
        tmp_path / "exports_v2", importer.staging_root(tmp_path / "work"))


# --- the empty root ------------------------------------------------------------------------------


def test_an_entry_that_declares_nothing_but_its_name_is_a_complete_asset(tmp_path):
    """`weapon` exists to be inherited from: no physics, no sounds, no game material."""

    units = _corpus(tmp_path)
    _unit(units, "weapon")
    result = _stage(tmp_path)

    assert result.failures == []
    assert result.assets == 1 and result.roots == 1 and result.inherited == 0
    sidecar = _sidecar(result, "weapon")
    assert sidecar["assetId"] == "vtmb:surface-property:weapon"
    assert sidecar["assetPath"] == "/ElysiumBaked/SurfaceProperties/PM_weapon"
    assert sidecar["baseChain"] == []
    # Every physics/movement scalar is emitted always, `null` when no unit in the chain declares
    # it, so a re-import can reset a scalar the source stops declaring (S2).
    assert sidecar["physics"] == {"friction": None, "elasticity": None, "density": None,
                                  "rawDensity": None, "thickness": None}
    assert sidecar["movement"] == {"maxSpeedFactor": None, "jumpFactor": None, "climbable": None}
    assert sidecar["footsteps"] == {"left": [], "right": []}
    assert sidecar["impacts"] == {} and sidecar["bulletImpactLegacy"] == []
    assert sidecar["fieldOrigins"] == {}
    # No `gamematerial` anywhere in the chain keeps the engine's own default row.
    assert sidecar["gameMaterial"] == "" and sidecar["surfaceType"] == "SurfaceType_Default"


# --- the chain -----------------------------------------------------------------------------------


def _metal_chain(tmp_path: Path) -> Path:
    """`canister -> metalpanel -> metalgrate -> metal`, the table's deepest chain."""

    units = _corpus(tmp_path)
    _unit(units, "metal",
          physics={"friction": 0.8, "elasticity": 0.5, "density": 2700.0},
          movement={"maxSpeedFactor": 1.0, "jumpFactor": 1.0, "climbable": False},
          footsteps={"left": [_sound("Surfaces/Metal/StepLeft1.wav"),
                              _sound("Surfaces/Metal/StepLeft2.wav")],
                     "right": [_sound("Surfaces/Metal/StepRight1.wav")]},
          impacts={"bullet": {"norm": [_sound("Surfaces/Metal/bullet1.wav"),
                                       _sound("Surfaces/Metal/bullet2.wav")]}},
          sounds={"impact": [_script("Metal.Impact")], "scrape": [_script("Metal.Scrape")]},
          game_material="M")
    _unit(units, "metalgrate", base="metal", physics={"elasticity": 0.2})
    _unit(units, "metalpanel", base="metalgrate",
          footsteps={"left": [_sound("Surfaces/Metal/PanelLeft1.wav")]})
    _unit(units, "canister", base="metalpanel",
          physics={"thickness": 0.1},
          impacts={"bullet": {"norm": [_sound("Surfaces/Metal/CanisterHit.wav")]}},
          source_name="Canister")
    return units


def test_a_three_deep_chain_flattens_root_first_with_local_overrides(tmp_path):
    _metal_chain(tmp_path)
    result = _stage(tmp_path)

    assert result.failures == []
    assert result.assets == 4 and result.roots == 1 and result.inherited == 3
    assert result.deepest_depth == 4

    sidecar = _sidecar(result, "canister")
    assert sidecar["baseChain"] == ["metal", "metalgrate", "metalpanel"]
    # Inherited from the root, overridden one step up, and declared locally. `density` converts
    # from the table's kg/m3 (2700.0, authored) to the engine's g/cm3 (2.7); `rawDensity` keeps
    # the authored value.
    assert sidecar["physics"] == {"friction": 0.8, "elasticity": 0.2, "density": 2.7,
                                  "rawDensity": 2700.0, "thickness": 0.1}
    assert sidecar["movement"] == {"maxSpeedFactor": 1.0, "jumpFactor": 1.0, "climbable": False}
    origins = sidecar["fieldOrigins"]
    assert origins["physics.friction"] == "metal"
    assert origins["physics.elasticity"] == "metalgrate"
    assert origins["physics.thickness"] == "canister"
    assert origins["movement.jumpFactor"] == "metal"

    # A unit halfway up carries the values it inherited at that point, not the leaf's; `thickness`
    # is not declared anywhere in its own chain, so it stays `null`.
    grate = _sidecar(result, "metalgrate")
    assert grate["baseChain"] == ["metal"]
    assert grate["physics"] == {"friction": 0.8, "elasticity": 0.2, "density": 2.7,
                                "rawDensity": 2700.0, "thickness": None}
    assert "physics.thickness" not in grate["fieldOrigins"]


def test_a_declared_pool_replaces_the_inherited_pool_rather_than_appending(tmp_path):
    """Source copies the parent `surfacedata_t` and re-parses the child's keys, so a child that
    names `stepleft` at all supplies the whole left pool -- and only that slot."""

    _metal_chain(tmp_path)
    result = _stage(tmp_path)

    panel = _sidecar(result, "metalpanel")
    assert panel["footsteps"]["left"] == ["vtmb:sound:surfaces/metal/panelleft1.wav"]
    assert panel["fieldOrigins"]["footsteps.left"] == "metalpanel"
    # The right pool was never redeclared, so it is still the root's, whole.
    assert panel["footsteps"]["right"] == ["vtmb:sound:surfaces/metal/stepright1.wav"]
    assert panel["fieldOrigins"]["footsteps.right"] == "metal"

    # The matrix replaces per weapon/outcome slot, not per weapon and not per matrix: the leaf's
    # one bullet_norm_impact stands for the root's two.
    canister = _sidecar(result, "canister")
    assert canister["impacts"]["bullet"]["norm"] == ["vtmb:sound:surfaces/metal/canisterhit.wav"]
    assert canister["impacts"]["bullet"]["soak"] == []
    assert canister["fieldOrigins"]["impacts.bullet.norm"] == "canister"
    # Sound scripts are inherited whole and stay strings until the sound slice.
    assert canister["sounds"] == {"impact": ["vtmb:sound-script:metal.impact"],
                                  "scrape": ["vtmb:sound-script:metal.scrape"]}


def test_the_root_keeps_its_own_pools_in_source_order(tmp_path):
    _metal_chain(tmp_path)
    result = _stage(tmp_path)

    metal = _sidecar(result, "metal")
    assert metal["footsteps"]["left"] == ["vtmb:sound:surfaces/metal/stepleft1.wav",
                                          "vtmb:sound:surfaces/metal/stepleft2.wav"]
    assert metal["impacts"]["bullet"]["norm"] == ["vtmb:sound:surfaces/metal/bullet1.wav",
                                                  "vtmb:sound:surfaces/metal/bullet2.wav"]


# --- game material -> surface type ------------------------------------------------------------------


def test_game_material_is_inherited_and_decides_the_surface_type(tmp_path):
    _metal_chain(tmp_path)
    result = _stage(tmp_path)

    for key in ("metal", "metalgrate", "metalpanel", "canister"):
        sidecar = _sidecar(result, key)
        assert sidecar["gameMaterial"] == "M", key
        assert sidecar["surfaceType"] == importer.GAME_MATERIAL_SURFACE_TYPES["M"], key
        assert sidecar["fieldOrigins"]["gameMaterial"] == "metal", key

    entries = {entry["unitKey"]: entry for entry in _manifest(result)["assets"]}
    assert entries["canister"]["surfaceType"] == importer.GAME_MATERIAL_SURFACE_TYPES["M"]
    assert entries["canister"]["gameMaterial"] == "M"


def test_a_nearer_entry_overrides_the_inherited_game_material(tmp_path):
    units = _corpus(tmp_path)
    _unit(units, "metal", game_material="M")
    _unit(units, "tin", base="metal", game_material="T")
    result = _stage(tmp_path)

    tin = _sidecar(result, "tin")
    assert tin["gameMaterial"] == "T"
    assert tin["fieldOrigins"]["gameMaterial"] == "tin"
    assert tin["surfaceType"] == importer.GAME_MATERIAL_SURFACE_TYPES["T"]


def test_a_game_material_with_no_surface_row_fails_that_unit_only(tmp_path):
    units = _corpus(tmp_path)
    _unit(units, "metal", game_material="M")
    _unit(units, "novel", game_material="Z")
    result = _stage(tmp_path)

    assert result.assets == 1
    assert [key for key, _ in result.failures] == ["novel"]
    assert "'Z'" in result.failures[0][1]
    # The failed unit's asset path is protected rather than pruned by the editor phase.
    assert _manifest(result)["keep"] == ["/ElysiumBaked/SurfaceProperties/PM_novel"]
    assert result.protected == 1


def test_every_surface_row_the_table_names_is_declared_in_default_engine_ini():
    """The ini rows and the staging table are one mapping; a letter added to one needs the other.

    The pin is the block immediately under `[/Script/Engine.PhysicsSettings]`, not any
    `PhysicalSurfaces=` line anywhere in the file: another section could carry a stray line with
    the same text and a substring search over the whole file would not catch a row landing in the
    wrong section, or a row this section does not declare because another section's line matched
    instead.
    """

    lines = (REPO / "Config" / "DefaultEngine.ini").read_text(encoding="utf-8").splitlines()
    heading = lines.index("[/Script/Engine.PhysicsSettings]")
    block: list[str] = []
    for line in lines[heading + 1:]:
        stripped = line.strip()
        if not stripped or stripped.startswith(";"):
            continue   # blank lines and comments may separate the heading from its first row
        if not stripped.startswith("+PhysicalSurfaces="):
            break   # the block ends at the first line that is neither: another key, or a section
        block.append(stripped)

    expected = [f'+PhysicalSurfaces=(Type={row},Name="{name}")'
                for row, name in importer.surface_type_rows()]
    assert block == expected
    assert len(expected) == len(importer.GAME_MATERIALS) == 17


# --- anomalies and coverage -------------------------------------------------------------------------


def test_a_repeated_scalar_anomaly_reaches_the_sidecar(tmp_path):
    """A repeated *scalar* key is not a variation pool: the decode resolves it to its last value
    and records the anomaly, and this lane carries the record rather than judging it."""

    units = _corpus(tmp_path)
    _unit(units, "odd", physics={"friction": 0.9},
          anomalies=[{"role": "repeated-scalar-key", "offset": 42, "key": "friction"}])
    result = _stage(tmp_path)

    assert result.failures == []
    sidecar = _sidecar(result, "odd")
    assert sidecar["anomalies"] == ["repeated-scalar-key key=friction offset=42"]
    assert sidecar["physics"] == {"friction": 0.9, "elasticity": None, "density": None,
                                  "rawDensity": None, "thickness": None}
    assert sidecar["coverage"] == {"percent": 100.0, "unresolved": [], "unsupported": []}


# --- refusals ---------------------------------------------------------------------------------------


def test_a_cycle_refuses_every_unit_in_it(tmp_path):
    units = _corpus(tmp_path)
    _unit(units, "loopa", base="loopb")
    _unit(units, "loopb", base="loopa")
    _unit(units, "clean")
    result = _stage(tmp_path)

    assert result.assets == 1
    assert sorted(key for key, _ in result.failures) == ["loopa", "loopb"]
    for _, reason in result.failures:
        assert "closes on itself" in reason
    assert _manifest(result)["assets"][0]["unitKey"] == "clean"
    assert sorted(_manifest(result)["keep"]) == [
        "/ElysiumBaked/SurfaceProperties/PM_loopa", "/ElysiumBaked/SurfaceProperties/PM_loopb"]


def test_a_dangling_base_refuses_the_unit_and_its_descendants(tmp_path):
    units = _corpus(tmp_path)
    _unit(units, "orphan", base="ghost")
    _unit(units, "child", base="orphan")
    result = _stage(tmp_path)

    assert result.assets == 0
    failures = dict(result.failures)
    assert "'ghost' is defined by no unit" in failures["orphan"]
    assert "'ghost' is defined by no unit" in failures["child"]


def test_a_unit_that_is_not_a_surface_property_fails_alone(tmp_path):
    units = _corpus(tmp_path)
    _unit(units, "metal", game_material="M")
    units.mkdir(parents=True, exist_ok=True)
    (units / "stranger.glb").write_bytes(encode_glb({"asset": {"version": "2.0"}}))
    result = _stage(tmp_path)

    assert result.assets == 1
    assert [key for key, _ in result.failures] == ["stranger"]
    assert SURFACE_PROPERTY_EXTENSION in result.failures[0][1]


def test_an_empty_corpus_refuses_the_run(tmp_path):
    (tmp_path / "exports_v2" / importer.FAMILY).mkdir(parents=True)
    with pytest.raises(importer.SurfacePropertyImportError, match="no surface-property units"):
        _stage(tmp_path)


# --- the manifest and idempotency ----------------------------------------------------------------


def test_the_manifest_is_the_contract_the_editor_phase_reads(tmp_path):
    _metal_chain(tmp_path)
    result = _stage(tmp_path)
    manifest = _manifest(result)

    assert manifest["schemaVersion"] == importer.MANIFEST_SCHEMA
    assert manifest["settingsVersion"] == importer.SETTINGS_VERSION
    assert manifest["packageRoot"] == importer.PACKAGE_ROOT
    assert manifest["pruneScope"] == importer.PACKAGE_ROOT + "/"
    assert manifest["keep"] == [] and manifest["stageFailures"] == []
    paths = [entry["assetPath"] for entry in manifest["assets"]]
    assert paths == sorted(paths, key=str.lower)

    entry = next(row for row in manifest["assets"] if row["unitKey"] == "canister")
    assert entry["class"] == "ElysiumPhysicalMaterial"
    assert entry["unit"] == "vtmb:surface-property:canister"
    assert entry["unitGlb"] == "surface-properties/canister.glb"
    assert entry["provenance"] == "canister.provenance.json"
    assert entry["baseChain"] == ["metal", "metalgrate", "metalpanel"]
    assert set(entry["recipe"]) == {"settingsVersion", "unitSha256", "chainSha256", "surfaceType"}


def test_a_second_run_over_an_unchanged_corpus_writes_nothing(tmp_path):
    _metal_chain(tmp_path)
    first = _stage(tmp_path)
    assert first.staged == 4 and first.unchanged == 0

    second = _stage(tmp_path)
    assert second.staged == 0 and second.unchanged == 4
    assert second.pruned == 0
    assert _manifest(first) == _manifest(second)


def test_an_ancestor_changing_changes_the_descendants_recipe(tmp_path):
    """The values a leaf carries come from its ancestors, so its own GLB hash cannot decide reuse."""

    units = _metal_chain(tmp_path)
    before = {entry["unitKey"]: entry["recipe"]
              for entry in _manifest(_stage(tmp_path))["assets"]}

    _unit(units, "metal",
          physics={"friction": 0.1, "elasticity": 0.5, "density": 2700.0},
          movement={"maxSpeedFactor": 1.0, "jumpFactor": 1.0, "climbable": False},
          footsteps={"left": [_sound("Surfaces/Metal/StepLeft1.wav"),
                              _sound("Surfaces/Metal/StepLeft2.wav")],
                     "right": [_sound("Surfaces/Metal/StepRight1.wav")]},
          impacts={"bullet": {"norm": [_sound("Surfaces/Metal/bullet1.wav"),
                                       _sound("Surfaces/Metal/bullet2.wav")]}},
          sounds={"impact": [_script("Metal.Impact")], "scrape": [_script("Metal.Scrape")]},
          game_material="M")
    after = {entry["unitKey"]: entry["recipe"]
             for entry in _manifest(_stage(tmp_path))["assets"]}

    assert after["canister"]["unitSha256"] == before["canister"]["unitSha256"]
    assert after["canister"]["chainSha256"] != before["canister"]["chainSha256"]
    assert _sidecar(_stage(tmp_path), "canister")["physics"]["friction"] == 0.1


def test_a_unit_leaving_the_corpus_prunes_its_staged_sidecar(tmp_path):
    units = _metal_chain(tmp_path)
    result = _stage(tmp_path)
    stray = result.staging_root / "gone.provenance.json"
    stray.write_text("{}", encoding="utf-8")
    (units / "canister.glb").unlink()

    second = _stage(tmp_path)
    assert second.assets == 3
    assert not stray.exists()
    assert not (second.staging_root / "canister.provenance.json").exists()
    # The root reports are never pruned.
    assert (second.staging_root / importer.MANIFEST_NAME).is_file()


# --- density units (S1) ---------------------------------------------------------------------------


def test_density_converts_from_kg_per_cubic_metre_to_grams_per_cubic_centimetre(tmp_path):
    """`density` in the table is kg/m3 (water 1000); `UPhysicalMaterial::Density` is g/cm3
    (`BodySetup.cpp` multiplies by 0.001), so the stage divides by 1000 and keeps the authored
    value as `rawDensity` -- otherwise a 700 kg/m3 crate imports as a 700 g/cm3 one, ~700x too
    dense."""

    units = _corpus(tmp_path)
    _unit(units, "wood", physics={"density": 700.0})
    result = _stage(tmp_path)

    sidecar = _sidecar(result, "wood")
    assert sidecar["physics"]["rawDensity"] == 700.0
    assert sidecar["physics"]["density"] == pytest.approx(0.7)


# --- case-folded lookup (S4) ------------------------------------------------------------------------


def test_asset_path_for_folds_case():
    """The cross-lane contract a VMT's `$surfaceprop` resolves through: the table spells names in
    mixed case (`Metal`), so `asset_path_for` must fold before naming the asset."""

    assert importer.asset_path_for("Metal") == importer.asset_path_for("metal")
    assert importer.asset_path_for(" Metal ") == importer.asset_path_for("metal")


# --- assetPath collisions (S5) ----------------------------------------------------------------------


def test_two_keys_that_fold_to_the_same_asset_path_both_fail(tmp_path):
    """`safe_name` collapses `foo-bar` and `foo_bar` to the same object name. `load_manifest`
    refuses a manifest that lists one assetPath twice wholesale, so this lane fails both units
    that collide rather than silently picking one, and keeps staging the rest of the corpus."""

    units = _corpus(tmp_path)
    _unit(units, "foo-bar", physics={"friction": 0.5})
    _unit(units, "foo_bar", physics={"friction": 0.6})
    _unit(units, "clean")
    result = _stage(tmp_path)

    assert result.assets == 1
    assert sorted(key for key, _ in result.failures) == ["foo-bar", "foo_bar"]
    for _, reason in result.failures:
        assert "collides" in reason
    manifest = _manifest(result)
    assert manifest["assets"][0]["unitKey"] == "clean"
    assert manifest["keep"] == ["/ElysiumBaked/SurfaceProperties/PM_foo_bar"]
    assert result.protected == 1
