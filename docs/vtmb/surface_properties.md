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
Entry names, keys and values are lowercased by the retail physics pair reader `26024fd0`.
The underlying symbol table and ordinary `GetSurfaceIndex` queries remain case-sensitive;
uppercase lookup of an already parsed lowercase entry returns -1 (`26014ed0`).

## Inheritance — `base`

**42 of the 63 entries declare a `base`**, and the remaining **21 are roots**: `concrete`,
`default`, `default_silent`, `dirt`, `flesh`, `glass`, `glass_shard`, `gravel`, `metal`,
`metalvent`, `player`, `player_control_clip`, `popcan`, `quiet`, `rivet`, `rubber`, `tile`,
`water`, `watermelon`, `weapon`, `wood`.

The authored chains reach depth 4 — `canister` → `metalpanel` → `metalgrate` → `metal`,
and `boulder` → `rock` → `stone` → `concrete`. Retail does not recursively resolve a chain:
`26015170` starts each row with zero bytes, copies the currently stored `default` if present,
then processes keys in source order. `base` calls `26015140` to copy all 116 payload bytes from
an already stored row at that exact token position. A missing or forward base leaves the row
untouched. A late base overwrites earlier child fields; later child fields overwrite that copy.
No explicit `base default` is required for the initial default copy.

## Fields

### Physics

The file's own header comment documents these, and the values bear it out.

| Key | Meaning | Entries | Range across the table |
|---|---|---|---|
| `density` | material density, kg/m³ (water is 1000) | 26 | 100 … 2700 |
| `elasticity` | collision elasticity, 0.01 soft … 1.0 hard | 34 | 0.001 … 2 |
| `friction` | physical friction, 0.01 slick … 1.0 rough | 35 | 0.05 … 100 |
| `thickness` | when present the material is **not volumetrically solid** — volume is surface area × this, and the space beneath it is air | 10 | 0.04 … 1 |

`elasticity` and `friction` exceed their documented 0–1 ranges on a few entries. Retail's IVP
material reads the raw floats without clamping (`26015b20/26015b30`). The existing Unreal
physical-material bake clamps elasticity as a presentation/Chaos lane divergence; it is not the
constructor's retail material producer.

`density`'s unit is the table's own, kg/m³; `UPhysicalMaterial::Density` is g/cm³, so
`uv run elysium import surface-properties` converts on the way in and keeps the authored kg/m³
value alongside it in the staged sidecar.

### Movement

`maxspeedfactor` (`1.0`), `jumpfactor` (`1.0`) and `climbable` (`0`) appear on **`default` alone**,
so every surface inherits them and no shipped surface modifies movement. They are declared
capability rather than authored content.

### Footsteps

`stepleft` and `stepright`, on **17 entries**, each a direct `.wav` path under `Surfaces/`.

### Impacts — a weapon × outcome matrix

The shipped text uses fifteen keys. The executable also accepts the claw row, for eighteen slots:

```
{ metal, wood, blade, fist, bullet, claw } × { norm, soak, crit }
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

## Repeated keys overwrite one symbol

`26015170` interns every sound value and writes one 16-bit symbol per slot. Repeated `stepleft`,
`stepright` or matrix keys overwrite that symbol; the last assignment survives. They are not
variation pools in SurfaceProps001. Duplicate entry definitions are still fully parsed and
intern their values, but the first stored entry wins (`26015917`). Existing flattened Unreal
surface/audio assets retain pools and root-first explicit-base resolution; that consumer lane
remains a known divergence and is not migrated by the independent physics table packet.

The full executable layout, signed lookups, numeric grammar, shadow row, source lifetime and
constructor-material capture are recorded in `physics-interaction.md`, "Independent physics
surface material producer". The dedicated whole-table deployment contract is
`docs/contracts/seam_physics_surfaces.md`; it retains source bytes and all entry/key order.

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
