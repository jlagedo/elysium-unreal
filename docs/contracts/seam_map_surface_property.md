# Surface-property GLB seam

This document defines one binary glTF 2.0 unit for one named VtMB surface-property record. Format
facts remain owned by `docs/vtmb/surface_properties.md`.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> scripts/surfaceproperties.txt#<name>
  -> vtmb:surface-property:<name>
  -> $ELYSIUM_EXPORT_V2_ROOT/surface-properties/<name>.glb
```

The table member resolves UP-first. One named entry produces one surface-property GLB. The unit
key is the entry name folded to lower case, because the table spells names in mixed case and both
producers that name into it spell them freely; the source spelling is published beside it.

An entry that declares nothing but its own name is a complete unit, not an empty decode: `weapon`
is a root that exists to be inherited from.

## Unit boundary and byte accountability

A unit owns its entry's bytes — from its name token through its closing brace — and its byte
ledger is gapless over that span alone. The bytes between entries are the table's, not any one
surface's, so publishing 63 ledgers over the whole 24 KB file would make every unit authoritative
for text it does not own.

The export therefore proves the whole table partitions into entry spans, comments and insignificant
whitespace with nothing left over before it cuts any unit. A byte outside every entry, or a name
declared twice, refuses the table rather than publishing a corpus with an ambiguous identity in it.

## GLB structure

A surface-property GLB is a scene-less glTF asset whose semantic value lives in one extension. It
carries no core material, no mesh and no BIN chunk: every datum the unit owns is a name, a flag or
a number the source wrote as text, so an accessor would restate `parameters` in a weaker form.

```json
{
  "extensionsUsed": [
    "ELYSIUM_vtmb_surface_property"
  ],
  "extensionsRequired": [
    "ELYSIUM_vtmb_surface_property"
  ],
  "extensions": {
    "ELYSIUM_vtmb_surface_property": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "base": null,
      "physics": {},
      "movement": {},
      "footsteps": {},
      "impacts": {},
      "sounds": {},
      "gameMaterial": "",
      "parameters": [],
      "dependencies": [],
      "comments": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

`parameters` carries every `key value` pair the entry declares, in source order, with its source
casing, its quoting and its offset within the entry. Each semantic record names the parameter index
it came from, so a reader can walk from a footstep back to the bytes that authored it.

## Mapping

| Surface datum | Extension field |
|---|---|
| Inheritance | `base` surface-property asset ID |
| Density, elasticity, friction and thickness | `physics` |
| Speed, jump and climb values | `movement` |
| Left/right footsteps | `footsteps` audio asset IDs |
| Weapon/outcome impact matrix | `impacts` audio asset IDs |
| Pre-matrix `bulletimpact` | `impacts.legacy` audio asset IDs |
| Physics impact and scrape scripts | `sounds` dependency asset IDs |
| Compact material class | `gameMaterial` |

A unit publishes only what its own entry declares. It does not inline the values it inherits,
because those belong to the entry that declares them and duplicating them would make two units
authoritative for one value.

### Variation pools

A key that repeats inside one block is a pool the engine picks between at random, not an
overriding assignment, so `footsteps` and `impacts` publish ordered lists:

```json
{
  "footsteps": {
    "left": [
      {"path": "Surfaces/Concrete/StepLeft1.wav",
       "asset": "vtmb:sound:surfaces/concrete/stepleft1.wav", "parameter": 11},
      {"path": "Surfaces/Concrete/StepLeft2.wav",
       "asset": "vtmb:sound:surfaces/concrete/stepleft2.wav", "parameter": 12}
    ]
  },
  "impacts": {
    "bullet": {"norm": [], "soak": [], "crit": []}
  }
}
```

The impact matrix is indexed weapon-first: `impacts.<weapon>.<outcome>` over
`{bullet, metal, wood, blade, fist}` × `{soak, norm, crit}`. A repeat of a *scalar* key is not a
pool, so it resolves to its last value and records a `repeated-scalar-key` anomaly.

## Dependencies

Every reference the entry makes is declared once, whatever produced it:

```json
{
  "dependencies": [
    {"role": "surface-property", "asset": "vtmb:surface-property:flesh",
     "sourcePath": "scripts/surfaceproperties.txt#flesh"},
    {"role": "sound", "asset": "vtmb:sound:surfaces/concrete/stepleft1.wav",
     "sourcePath": "Surfaces/Concrete/StepLeft1.wav"},
    {"role": "sound-script", "asset": "vtmb:sound-script:concrete.impact",
     "sourcePath": "concrete.Impact"}
  ]
}
```

`impact` and `scrape` name entries of `scripts/game_sounds_surfaceproperties.txt`, not `.wav`
paths, so they take their own `vtmb:sound-script:` namespace and carry a `resolved` flag against
that file. An unresolved script warns rather than failing the unit: it is a join to another seam's
data, not a gap in this entry's decode. A `base` that no entry defines is different — the
inheritance chain is this table's own structure — so it enters `coverage.unresolved` and fails.

## LaCroix reference

```text
<VTMB>/Vampire/pack001.vpk
  -> scripts/surfaceproperties.txt#flesh
  -> vtmb:surface-property:flesh
  -> $ELYSIUM_EXPORT_V2_ROOT/surface-properties/flesh.glb
```

LaCroix's PHY solids reference `vtmb:surface-property:flesh`.

## Coverage

A complete surface-property GLB accounts for every byte of its entry and has zero `unresolved` and
zero `unsupported` keys. The table's vocabulary is closed — the file's own header comment documents
it — so a key with no field is published as unsupported rather than carried as an anonymous pair.

Validation is per unit: it compares the published physics, movement, footstep, impact, sound and
game-material values with the entry the unit was decoded from, and checks that every dependency was
produced by a reference the unit publishes. Resolving an inheritance chain spans units and is a
consumer's concern, not a property one GLB can be validated against.

## Import

`uv run elysium import surface-properties` turns every published surface-property unit into one
`UElysiumPhysicalMaterial` below `/ElysiumBaked/SurfaceProperties`. It is the third slice of the
seam migration (`docs/project/seam_migration.md` → "Settled", "Surface properties import"), and it
follows the texture lane's shape exactly: the unit is the only input, the asset carries everything
the unit knows, and nothing here reads the install.

### Identity and naming

```text
vtmb:surface-property:<name>  ->  /ElysiumBaked/SurfaceProperties/PM_<safe name>
```

`<safe name>` is `asset_names.safe_name` over the unit key, which `asset_path_for` first folds with
`strip().lower()` — the entry name is already lower-cased at the unit's own key, but the function
is the cross-lane contract, so it folds unconditionally rather than trusting every caller to have
folded first. The source spelling (`Kitchen_Pan`) lives on the asset and in its provenance, so
folding is never a loss.

The path is load-bearing beyond this lane: Phase 4's material import resolves a VMT's
`$surfaceprop` — spelled freely, `Metal` beside `metal` — and later a model's `SurfacePropIndex`
— to this exact path by folding the name the same way, falling back to `PM_default` on a name the
table never gained (`docs/vtmb/surface_properties.md` → "Names used but never defined").

Two distinct keys can fold to the same safe name (`foo-bar` and `foo_bar` both become
`foo_bar`) and collide on one `assetPath`. `load_manifest` refuses a manifest that lists the same
`assetPath` twice wholesale, so the stage detects a collision itself: every unit sharing the path
fails as a per-unit stage failure naming the others it collides with, the path is protected under
`keep` so the editor phase does not prune whatever it already holds, and the rest of the corpus
stages normally. The shipped table has no collision, so this is a guard, not a path.

### Identity is the asset, not an `EPhysicalSurface` row

There is one asset per entry and a hit's `PhysMaterial` **is** that asset, so nothing needs an
enum slot per entry — which is just as well, since the engine has 62 `EPhysicalSurface` rows and
the table has 63 entries. `SurfaceType` therefore carries something else: VtMB's own compact
material class, the single-letter `gamematerial` code, resolved through the base chain.

### Two phases, one command

**Phase 1 — stage (offline Python, `importers/surface_properties.py`).** For every unit under
`$ELYSIUM_EXPORT_V2_ROOT/surface-properties/` the lane hashes the GLB, reads the extension,
**resolves the `base` chain to flat values**, maps the resolved `gamematerial` to an
`EPhysicalSurface` row, and writes one provenance sidecar per unit plus one `manifest.json` under
`$ELYSIUM_WORK_ROOT/import/surface-properties/`. Pure Python, and what the pytest suite exercises
with synthetic units.

**Phase 2 — import (headless editor, `pipeline/unreal/import_surface_properties.py`).** Launched
like the corpus bake (`-run=pythonscript`), it creates each asset through
`AssetTools.create_asset(name, package, unreal.ElysiumPhysicalMaterial,
unreal.PhysicalMaterialFactoryNew())` — the stock factory builds whatever class it is handed, and
AssetTools already checks that class against the factory's supported class, so naming the Elysium
class is what makes the asset ours — applies the sidecar through
`UElysiumPhysicalMaterial.apply_json`, publishes the registry tags, stamps the recipe
(`bake_lib.RECIPE_TAG`), saves, and prunes.

There is no third phase. Nothing here is re-encoded, so there is nothing to measure.

### Flattening: the rule, and why it is the consumer's

A unit publishes only what its own entry declares — inlining an inherited value would make two
units authoritative for one number — and 42 of the 63 entries state only their deltas. Resolution
spans units, so it belongs to a consumer, and this lane is the consumer.

The rule is Source's own: the parser copies the parent's `surfacedata_t` wholesale and then
re-parses the child's keys over the copy, so

- a **scalar** a child declares (`friction`, `elasticity`, `density`, `thickness`,
  `maxspeedfactor`, `jumpfactor`, `climbable`, `gamematerial`) overrides the parent's;
- a **non-empty pool** a child declares **replaces** the parent's whole pool *for that slot*. An
  entry that names one `stepleft` supplies the entire left-footstep pool; it does not append a
  fifth alternate to the four it inherited. The slot is the key, so redeclaring `stepleft` leaves
  `stepright` inherited whole, and redeclaring `bullet_norm_impact` leaves `bullet_crit_impact`
  alone. An entry whose pool resolves empty is the same as declaring none at all: a child cannot
  *clear* a pool it inherited, only replace it with another one.

The walk is root first, so a nearer entry's declaration lands last and wins, and **which unit
supplied each field is recorded per field** in the provenance (`fieldOrigins`).

A `base` no unit defines, and a chain that closes on itself, refuse the unit rather than
publishing a half-resolved asset; every descendant of a refused unit is refused with it, naming
the ancestor that broke it. The shipped table has neither, so this is a guard, not a path.

**`default` is not an implicit parent.** It is the fallback for an unknown *name*, and no entry
bases on it, so the movement fields it alone declares are not inherited by the other 62. The asset
class's defaults (`MaxSpeedFactor` 1.0, `JumpFactor` 1.0, `bClimbable` false) are `default`'s own
values, so an asset with no movement block behaves identically — but its provenance correctly
shows no origin for those fields rather than claiming `default` authored them.

Measured over the shipped table (2026-08-31): 63 units, 21 roots, 42 with a base, deepest chain 4
units (`canister` → `metalpanel` → `metalgrate` → `metal`). `gamematerial` goes from 26 entries
declaring one to **56 assets carrying one**; 7 keep `SurfaceType_Default` (`gravel`, `player`,
`popcan`, `quiet`, `rivet`, `rubber`, `weapon`).

### `gamematerial` → `EPhysicalSurface`

One row per distinct letter, assigned by alphabetical order of the letter so the mapping is
derivable rather than remembered. The rows are declared in `Config/DefaultEngine.ini` under
`[/Script/Engine.PhysicsSettings]`; the table itself is
`importers/surface_properties.GAME_MATERIAL_SURFACE_TYPES`, which is what stages the row name into
each sidecar, and `UElysiumPhysicalMaterial::ApplyJson` only resolves that name through the
reflected enum — so there is one table, not two that can disagree. A pytest pins the ini against
it, and a letter with no row is a **stage failure** for that unit rather than a silent default.

| `gamematerial` | Row | Name | Entries declaring | Assets carrying |
|---|---|---|---|---|
| `A` | `SurfaceType1` | `VtmbGameMaterial_A` | 1 | 1 |
| `C` | `SurfaceType2` | `VtmbGameMaterial_C` | 2 | 3 |
| `D` | `SurfaceType3` | `VtmbGameMaterial_D` | 1 | 9 |
| `F` | `SurfaceType4` | `VtmbGameMaterial_F` | 4 | 4 |
| `G` | `SurfaceType5` | `VtmbGameMaterial_G` | 1 | 1 |
| `I` | `SurfaceType6` | `VtmbGameMaterial_I` | 1 | 1 |
| `M` | `SurfaceType7` | `VtmbGameMaterial_M` | 3 | 10 |
| `N` | `SurfaceType8` | `VtmbGameMaterial_N` | 1 | 1 |
| `O` | `SurfaceType9` | `VtmbGameMaterial_O` | 1 | 3 |
| `P` | `SurfaceType10` | `VtmbGameMaterial_P` | 1 | 1 |
| `S` | `SurfaceType11` | `VtmbGameMaterial_S` | 1 | 1 |
| `T` | `SurfaceType12` | `VtmbGameMaterial_T` | 1 | 4 |
| `U` | `SurfaceType13` | `VtmbGameMaterial_U` | 1 | 1 |
| `V` | `SurfaceType14` | `VtmbGameMaterial_V` | 1 | 7 |
| `W` | `SurfaceType15` | `VtmbGameMaterial_W` | 1 | 2 |
| `X` | `SurfaceType16` | `VtmbGameMaterial_X` | 3 | 3 |
| `Y` | `SurfaceType17` | `VtmbGameMaterial_Y` | 2 | 4 |

What each letter *means* is recorded nowhere in the install; the codes are Source's and the table
only uses them. They are carried so a consumer can group surfaces the way VtMB did, not because
their semantics are known.

### The staged sidecar, and what it becomes

One sidecar per unit, `<name>.provenance.json`, holding the flattened values:

```json
{
  "assetId": "vtmb:surface-property:canister",
  "name": "canister",
  "sourceName": "Canister",
  "assetPath": "/ElysiumBaked/SurfaceProperties/PM_canister",
  "unitGlb": "surface-properties/canister.glb",
  "unitSchemaVersion": "1.0.0",
  "unitSha256": "…",
  "settingsVersion": "elysium-surfaceproperty-import-v2",
  "baseChain": ["metal", "metalgrate", "metalpanel"],
  "gameMaterial": "M",
  "surfaceType": "SurfaceType7",
  "physics": {"friction": 0.8, "elasticity": 0.2, "density": 2.7, "rawDensity": 2700.0,
              "thickness": 0.1},
  "movement": {"maxSpeedFactor": null, "jumpFactor": null, "climbable": null},
  "footsteps": {"left": ["vtmb:sound:surfaces/metalgrate/stepleft1.wav"],
                "right": ["vtmb:sound:surfaces/metal/stepright1.wav"]},
  "impacts": {"bullet": {"soak": [], "norm": ["vtmb:sound:…"], "crit": []}},
  "bulletImpactLegacy": [],
  "sounds": {"impact": ["vtmb:sound-script:metalgrate.impact"], "scrape": []},
  "fieldOrigins": {"physics.friction": "metalpanel", "footsteps.left": "metalgrate",
                   "footsteps.right": "metal"},
  "anomalies": [],
  "coverage": {"percent": 100.0, "unresolved": [], "unsupported": []}
}
```

`physics` and `movement` carry every scalar key on every sidecar, `null` where no unit in the
chain declares it — `canister`'s own chain never declares a movement value, so all three are
`null` here — so a re-import can tell "still undeclared" from "the sidecar predates this key".

| Sidecar | Asset |
|---|---|
| `physics.friction` | `Friction` (native; **not** clamped — Unreal's friction is not a 0–1 quantity and the table runs to 100) |
| `physics.elasticity` | `Restitution` (native, clamped 0–1) and `RawElasticity` (the authored value, which runs to 2) |
| `physics.density` | `Density` (native), converted from the table's kg/m³ to the engine's own g/cm³ (÷1000) |
| `physics.rawDensity` | `RawDensity`, the authored kg/m³ value the conversion was computed from — mirrors `elasticity` → `RawElasticity` |
| `physics.thickness` | `Thickness` (0 = the chain declared none, the volumetrically solid case) |
| `surfaceType` | `SurfaceType` (native), resolved through the reflected `EPhysicalSurface` |
| `movement.*` | `MaxSpeedFactor`, `JumpFactor`, `bClimbable` |
| `footsteps.left` / `.right` | `FootstepsLeft` / `FootstepsRight`, ordered `vtmb:sound:` IDs |
| `impacts.<weapon>.<outcome>` | `Impacts` — `TMap<FString, FElysiumImpactOutcomes{Soak, Norm, Crit}>` |
| `bulletImpactLegacy` | `BulletImpactLegacy`, the pre-matrix `bulletimpact` key |
| `sounds.impact` / `.scrape` | `SoundScriptImpact` / `SoundScriptScrape`, `vtmb:sound-script:` names |
| `assetId`, `sourceName`, `gameMaterial`, `baseChain` | `AssetId`, `SourceName`, `GameMaterial`, `BaseChain` |

`ApplyJson` tolerates a missing key: a missing or `null` **physics/movement scalar** resets that
field to the class default (the CDO value), not to whatever the asset already carried — the stage
emits every one of those keys on every sidecar, so an absent value means no unit in the chain
declares it, and an asset a source stops declaring a value for must revert rather than keep a
stale one. (Restitution and RawElasticity reset independently to their own defaults — 0.3 and 0.0
— rather than one being derived from the other's default, because only a *declared* elasticity
ties them together.) Every **list, map and chain** is replaced by what the sidecar carries — a
stale variation pool surviving a re-import would be worse than an empty one. Only a body that is
not a JSON object, and a target that is not a `UElysiumPhysicalMaterial`, are refused.

**Sound references stay strings.** Every footstep, impact and script reference is carried as its
`vtmb:sound:` / `vtmb:sound-script:` asset ID, not as a `USoundWave` reference. The sound slice
flips them to hard references; until then nothing downstream can bind audio, and the fidelity of
the reference is preserved either way.

### Provenance

Every asset carries one `UElysiumSurfacePropertyProvenance` (`UAssetUserData`, runtime module, so
a packaged game reads it). `UPhysicalMaterial` implements no asset-user-data interface, so
`UElysiumPhysicalMaterial` implements `IInterface_AssetUserData` itself, exactly the way `UTexture`
and `UStaticMesh` do — which is also what gives `AddAssetUserData` its replace-same-class
behaviour, so a re-import replaces rather than accumulates.

| Field | From |
|---|---|
| `AssetId`, `Name`, `SourceName`, `AssetPath`, `UnitGlb`, `UnitSchemaVersion`, `UnitSha256`, `SettingsVersion` | the sidecar's identity block |
| `BaseChain` | the resolved chain, root first, excluding the unit |
| `FieldOrigins` | field path → the unit that declared the value the asset carries |
| `Anomalies` | the unit's `anomalies`, one readable line each |
| `CoveragePercent`, `Unresolved`, `Unsupported` | the unit's `coverage` |

`FieldOrigins` is what makes a flattened asset auditable. Without it an asset states a dozen values
and nothing says whether `Friction` is this entry's or came from four entries up, which is exactly
the question a fidelity check asks.

`AssetId`, `GameMaterial` and `SourceName` are also published as asset-registry tags
(`ElysiumAssetId`, `ElysiumGameMaterial`, `ElysiumSourceName`, listed under
`MetaDataTagsForAssetRegistry` in `Config/DefaultGame.ini` beside `ElysiumRecipe`), so the Content
Browser filters on them without loading the asset. Byte ledgers, omissions and the parameter list
are not copied: they are the export's proof and live in the unit.

### The staging manifest

```json
{
  "schemaVersion": "1.0.0",
  "settingsVersion": "elysium-surfaceproperty-import-v2",
  "packageRoot": "/ElysiumBaked/SurfaceProperties",
  "select": null,
  "pruneScope": "/ElysiumBaked/SurfaceProperties/",
  "keep": [],
  "stageFailures": [],
  "assets": [
    {
      "assetPath": "/ElysiumBaked/SurfaceProperties/PM_canister",
      "class": "ElysiumPhysicalMaterial",
      "unit": "vtmb:surface-property:canister",
      "unitKey": "canister",
      "unitGlb": "surface-properties/canister.glb",
      "unitSha256": "…",
      "provenance": "canister.provenance.json",
      "surfaceType": "SurfaceType7",
      "gameMaterial": "M",
      "baseChain": ["metal", "metalgrate", "metalpanel"],
      "recipe": {"settingsVersion": "elysium-surfaceproperty-import-v2",
                 "unitSha256": "…", "chainSha256": "…", "surfaceType": "SurfaceType7"}
    }
  ]
}
```

`provenance` is relative to the staging root. The recipe stamp is
`bake_lib.recipe_fingerprint("surface-properties", assetPath, recipe)`.

**The recipe carries `chainSha256` as well as the unit's own hash**, because a flattened asset's
values change when an *ancestor's* unit changes and its own bytes do not. `chainSha256` is a digest
over `<name>:<sha256>` for every unit in the chain, root first, including the unit itself, so
editing `metal` re-authors `metalgrate`, `metalpanel` and `canister` and nothing else.

`select` is always `null`: the family is 63 units in one flat directory, so there is nothing to
scope and the lane takes no `--select`.

### Idempotency, pruning and failure

The recipe stamp decides per asset; a current corpus launches, reports every asset reused, and
exits. Assets under `/ElysiumBaked/SurfaceProperties` the manifest does not name are deleted. A
unit that fails to stage or import is counted and named with its reason and the run continues; the
command exits non-zero when any unit failed, and a relaunch resumes from the stamps.

A failed unit's asset is **protected, not orphaned**: its asset path is listed under `keep`, the
editor phase never prunes a path in `keep`, and `stageFailures` names the unit with its reason. A
unit that stops resolving therefore costs one relaunch, never a previously good asset.

First full run (2026-08-31): 63 staged, 63 imported, 0 failed; rerun 63 reused, 0 pruned.
