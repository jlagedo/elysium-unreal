# VtMB surface properties — `scripts/surfaceproperties.txt`

The physical-material table. One entry per surface names its physics constants, its footstep and
scrape sounds, and a matrix of impact sounds indexed by weapon class and damage outcome. It is the
join between what a thing is made of and how it sounds, breaks and behaves when struck.

Two producers name into it, and they answer different questions:

- a **model** names the material it *is made of*, in `MDLHeader.SurfacePropIndex` —
  `docs/vtmb/mdl_v2531.md` → `SurfacePropIndex@392`;
- a **world material** names the surface being *walked on or shot at*, as `$surfaceprop` in its
  `.vmt` — `docs/vtmb/texture_format.md`.

Footsteps therefore resolve from the world surface, impacts against a prop from the prop's own
model field. Both land in this one table.

## The two files

| File | Role |
|---|---|
| `scripts/surfaceproperties.txt` | 63 surface entries; physics, footsteps, impact matrix |
| `scripts/game_sounds_surfaceproperties.txt` | 48 sound-script entries the `impact`/`scrape` keys name |

Both are Source KeyValues: `"key" "value"` pairs inside a named block, `//` line comments, CRLF.
Entry names and keys are **mixed case** (`Kitchen_Pan` beside `metalvent`, `Blade_norm_impact`
beside `blade_norm_impact`), so a reader folds case on both.

## Inheritance — `base`

**42 of the 63 entries declare a `base`**, and the remaining **21 are roots**: `concrete`,
`default`, `default_silent`, `dirt`, `flesh`, `glass`, `glass_shard`, `gravel`, `metal`,
`metalvent`, `player`, `player_control_clip`, `popcan`, `quiet`, `rivet`, `rubber`, `tile`,
`water`, `watermelon`, `weapon`, `wood`.

Chains resolve transitively and reach depth 4 — `canister` → `metalpanel` → `metalgrate` → `metal`,
and `boulder` → `rock` → `stone` → `concrete`. Every named base is defined; there are no dangling
references and no cycles. **A reader must resolve the chain**, because most entries state only
their deltas: 24 of the 63 carry no physics field of their own at all, and 37 carry no
`gamematerial`.

`default` is the fallback root and the only entry carrying the complete key set.

## Fields

### Physics

The file's own header comment documents these, and the values bear it out.

| Key | Meaning | Entries | Range across the table |
|---|---|---|---|
| `density` | material density, kg/m³ (water is 1000) | 26 | 100 … 2700 |
| `elasticity` | collision elasticity, 0.01 soft … 1.0 hard | 34 | 0.001 … 2 |
| `friction` | physical friction, 0.01 slick … 1.0 rough | 35 | 0.05 … 100 |
| `thickness` | when present the material is **not volumetrically solid** — volume is surface area × this, and the space beneath it is air | 10 | 0.04 … 1 |

`elasticity` and `friction` exceed their documented 0–1 ranges on a few entries, so a consumer
clamps rather than assumes.

`density`'s unit is the table's own, kg/m³; `UPhysicalMaterial::Density` is g/cm³, so
`uv run elysium import surface-properties` converts on the way in and keeps the authored kg/m³
value alongside it (`docs/architecture/seam_map_surface_property.md` → "Import" → "The staged
sidecar, and what it becomes").

### Movement

`maxspeedfactor` (`1.0`), `jumpfactor` (`1.0`) and `climbable` (`0`) appear on **`default` alone**,
so every surface inherits them and no shipped surface modifies movement. They are declared
capability rather than authored content.

### Footsteps

`stepleft` and `stepright`, on **17 entries**, each a direct `.wav` path under `Surfaces/`.

### Impacts — a weapon × outcome matrix

Fifteen keys, the cross product of five weapon classes and three damage outcomes:

```
{ bullet, metal, wood, blade, fist } × { soak, norm, crit }
```

for example `blade_crit_impact`, `bullet_soak_impact`. The outcome axis is VtMB's own damage
resolution — a soaked hit, an ordinary hit, a critical — so this table is where the combat model
meets its audio. Values are direct `.wav` paths.

`bulletimpact` (no underscore) is a separate legacy key on three entries.

### Physics-system sounds

`impact` and `scrape` hold **sound-script names**, not paths — `Bottle.Impact`, `Default.Scrape`.
46 distinct names are referenced and 45 resolve in `game_sounds_surfaceproperties.txt`; the one
that does not is the literal `null.wav`, used as a silence placeholder.

### `gamematerial`

A single-character code on 26 entries, 17 distinct values (`A`, `C`, `D`, `F`, `G`, `I`, `M`, `N`,
`O`, `P`, `S`, `T`, `U`, `V`, `W`, `X`, `Y`). Source's compact material class.

## Repeated keys are variation pools

A key may appear **several times in one block**, and the repeats are alternates chosen at random
rather than an overriding assignment — up to 4 for `bullet_norm_impact`, `stepleft` and
`stepright`, and 2–3 for most of the impact matrix. A KeyValues reader that collapses duplicates
into a dictionary silently discards the variation and makes every footstep identical.

## What names into the table

**Models.** 1,923 of 4,445 carry a name; 2,522 leave it genuinely unset. 98.3% of the names used
are members of the 63. Details and the absent-vs-unset evidence: `docs/vtmb/mdl_v2531.md`.

**World materials.** 4,606 of 11,627 `.vmt` files (39.6%) declare `$surfaceprop`. The distribution
is the built environment: `metal` 1,068, `plaster` 744, `wood` 625, `concrete` 449, `glass` 334,
`brick` 298, `stone` 160, `default` 160, `tile` 155, `plastic` 128, `carpet` 99, `dirt` 84,
`flesh` 40, `water` 35.

**Three entries are VtMB's own**, with no Source ancestry: `gargoyle`, `ming_xiao` and
`ming_xiao_tentacle`. The gargoyle models carry `gargoyle`.

## Names used but never defined

Authoring outran the table on both sides. World materials name `asphalt`, `bone`, `cloth`,
`leather` and `defualt` — the last a misspelling of `default`. Models add `organic`, `railing`,
`sign`, and QC entries where the model's own name was typed into the field
(`cement_mixer`, `metaldetector`, `floorblock`). `cloth` and `leather` appear on both sides, so
they are vocabulary the table never gained rather than isolated typos.

A resolver therefore falls back to `default` on an unknown name, and folding case first is what
makes `Metal` and `kitchen_pans` resolve at all.
