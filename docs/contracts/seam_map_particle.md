# Particle GLB seam

This document defines one binary glTF 2.0 unit for one VtMB particle definition below
`particles/`. Shared rules are owned by `seam_map_unit_contract.md`; the definition grammar by
`docs/vtmb/weather.md` → "The particle-definition format" and the corpus census by
`docs/vtmb/effects.md` §2.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> particles/<name>.txt
  -> vtmb:particle:<name>
  -> $ELYSIUM_EXPORT_V2_ROOT/particles/<name>.glb
```

The key is the lower-cased file stem below `particles/`, spaces preserved: the install ships
`particles/tz_ bloodtrickle_emitter.txt` and the engine resolves it like any other file. A
`spawn`, `collide` or `decal` block names a definition bare, with the directory, or with the
extension; all three spell one key. The member resolves UP-first.

```text
uv run elysium export_v2 particle-glb particles/<name>.txt
uv run elysium export_v2 particles-glb
```

The merged install resolves about 1,698 definitions.

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `particles/<name>.txt` | unit-selecting | `keys`, `blocks`, `projection` |
| `particles/<sprite>.tga` | sprite dependency | `vtmb:image:` ID |
| `particles/<child>.txt` | child-definition dependency | `vtmb:particle:` ID |

The sprite is resolved as `particles/<sprite>.tga`, case-insensitively, exactly as the compiler in
`formats/particles.py` resolves it; no shipped definition names a `materials/` path, and one that
does would resolve to a `vtmb:material:` ID under role `material`.

## GLB structure

The unit is scene-less and has no BIN chunk.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_particle"],
  "extensionsRequired": ["ELYSIUM_vtmb_particle"],
  "extensions": {
    "ELYSIUM_vtmb_particle": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "root": "Particle",
      "keys": [],
      "blocks": [],
      "projection": {},
      "role": "drawing",
      "comments": [],
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

## Mapping

| Definition datum | Extension |
|---|---|
| Every key in source order | `keys[]` — `index`, `block`, `key`, `sourceKey`, `value`, `parsed`, `offset`, `length`, `quoted` |
| `spawn`, `collide`, `decal` sub-blocks | `blocks[]` — `index`, `kind`, `parent`, `offset`, `length`, `keys[]` indexes |
| Value grammar | `parsed` — `{"kind": "scalar" \| "range" \| "ramp", "values": [...], "positions": [...]}` |
| Role by content | `role` — `drawing` (`sprite`, no `spawn`), `emitter` (`spawn`, no `sprite`), `both`, `neither`; `precipitation` as a separate boolean |
| Meaning of a key | `projection` — the closed vocabulary below applied to `keys[]` |

`parsed` keeps the spelling beside the numbers: `a~b` is a `range`, `a,b,…` a `ramp`, and `v(n)`
a ramp keyframe whose explicit position is stated in `positions`. The forms compose
(`rate "15,5~50,20,5~50,15"`), so a ramp's `values` may hold ranges. A value with no number is
published as a string with `kind: "text"`.

### Vocabulary

`projection` states a closed vocabulary; every key is in it or in `typedUnidentified[]` with its
offset. The vocabulary is the runtime's own — the two key tables and the flag set `CParticleManager`
(`engine.dll`, vtable `0x201751a4`) reads, dumped from the binary (`docs/vtmb/effects.md` §2.4) —
plus the spellings the corpus authors that the runtime never reads, kept so the unit stays gapless:

| Scope | Keys | Meaning |
|---|---|---|
| definition | `loop`, `precipitation`, `fps`, `frames`, `min_frames`, `max_frames` | lifetime and looping |
| drawing particle | `sprite`, `normal`, `movealign`, `flat`, `sortfront`, `no_z_test`, `lighting` | sprite binding, facing, draw order |
| drawing particle | `x_speed`, `y_speed`, `z_speed`, `parent_speed`, `radius_speed`, `elevation_speed`, `theta_speed`, `phi_speed` | motion |
| drawing particle | `size`, `height`, `width`, `rotation`, `depth_offset` | extent, roll and view-depth bias |
| drawing particle | `color`, `red`, `green`, `blue`, `mask`, `refract` | colour ramps, the blend interpolant, the DUDV strength |
| drawing particle | `surface_color`, `use_surface_color`, `ignore_surface_color` | three spellings of one tint opt-out |
| drawing particle | `collide` | collision behaviour |
| `spawn` | `particle`, `rate`, `burst`, `distance`, `radius`, `theta`, `phi`, `x`, `y`, `z`, `elevation`, `rotation`, `width`, `height`, `size`, `red`, `green`, `blue`, `color`, `mask`, `refract`, `timescale` | child emission |
| `collide` | `spawn`, `decal`, `self`, `bounce`, `friction`, `gravity`, `drag`, `vdecal_first`, `vdecal_last`, `vdecal_angle_spread` | impact response |
| `decal` | `particle` | the decal definition |
| authored, unread | `rotate` and `radius` on a particle body, `loop`, `frames` and `depth_offset` inside `spawn`, `burst` on a particle body | carried with `meaning: "unread"`; the runtime's tables have no such row, so the key does nothing |

Where the meaning is not established the projection carries the key with `meaning: null` and a
`typedUnidentified` row; the unit is complete when every key is accounted for, not when every key
is understood. The legacy compiler (`formats/particles.py`) rejects `normal`, `refract`,
`timescale`, an enabled `sortfront`, `lighting` and malformed braces; this unit decodes them like
every other key, which is why the R7.3 map stage reads this unit and not that compiler.

## Semantics (the projection contract, R7.3)

`projection` names each key; this section states what the engine does with it, in the units a
consumer publishes. The rules are `CParticleManager`'s, read off the shipped binary
(`docs/vtmb/effects.md` §2.4 owns the facts and their addresses; this section owns the unit
rules). The one consumer today is the R7.3 map stage (`seam_map_map.md` → "Import — effects
(R7.3)"), which converts on these rules and on no other reading. Source units: 1 unit = 2.54 cm,
angles in degrees, colours 0..255.

### Key tables and defaults

Every key is `{name, default, isAngle}`. A key the definition omits takes its default.

| Particle key | Default | Unit (authored → staged) |
|---|---|---|
| `red` `green` `blue` `color` `mask` | 255 | 0..255 → 0..1 (`/255`) |
| `refract` | 1 | dimensionless (DUDV strength) |
| `width` `height` | 1 | dimensionless multiplier |
| `size` | 1 | inches → cm (the quad edge for a square sprite) |
| `rotation` | 0 (angle) | degrees, shortest-arc interpolation |
| `radius_speed` | 0 | in/s → cm/s |
| `theta_speed` `phi_speed` | 0 | deg/s |
| `x_speed` `y_speed` `z_speed` | 0 | in/s → cm/s, in the emitter basis |
| `elevation_speed` | 0 | in/s → cm/s, world up |
| `parent_speed` | 1 | fraction of the parent's movement live particles inherit |

| Spawn key | Default | Unit (authored → staged) |
|---|---|---|
| `red` `green` `blue` `color` `mask` | 255 | 0..255 → 0..1, multiplied into the child's channels |
| `refract` `width` `height` `size` | 1 | dimensionless multipliers into the child's |
| `rotation` | 0 (angle) | degrees, added to the child's initial roll |
| `radius` | 0 | inches → cm |
| `theta` `phi` | 0 (angle) | degrees |
| `x` `y` `z` | 0 | inches → cm, offset in the emitter basis |
| `elevation` | 0 | inches → cm, world up |
| `timescale` | 1 | divides the child's lifetime |
| `rate` | 0 | particles per second (per cm travelled under `distance "1"`) |
| `burst` | 0 | particles, added at each keyframe crossing |
| `distance` | 0 | flag: `rate` becomes particles per unit travelled (`/2.54` per cm) |

Scalars and flags on a definition: `fps` (default **30**), `frames` (default `fps` — one second),
`min_frames` / `max_frames` (default `frames`), `loop`, `flat`, `sortfront`, `movealign`,
`lighting`, `no_z_test`, `depth_offset` (inches → cm), `precipitation`, the three surface-colour
spellings, `sprite`, `normal`. Collide keys: `bounce` 1, `friction` 1, `gravity` 0, `drag` 1,
`self` 0, `vdecal_first` / `vdecal_last` / `vdecal_angle_spread`.

### Lifetime

`lifetime = frames / fps` seconds. Each particle rolls an inverse age rate in
`[fps / max_frames, fps / min_frames]` at spawn and advances a **normalized age in [0, 1]**;
`loop` wraps the age; `timescale` on the spawn block divides the child's lifetime. Staged as
`lifetime_s`, `lifetime_min_s = min_frames / fps`, `lifetime_max_s = max_frames / fps`. A wrapper
authored `frames "0"` (the `frames-zero-on-wrapper` anomaly) is staged at 0 and the consumer treats
the emitter as looping over the one-second default — the natural reading, INFERRED, and named for
the R7.3 exploration check.

### Ramps

`a,b,…` is a keyframe list over normalized age, **linear** between keyframes, the value held after
the last. `v(n)` pins a keyframe: `n` is a **frame index normalized by `frames`** (`t = n /
frames`), a negative `n` wraps from the end (`t = (frames + n) / frames`); the first keyframe must
sit at 0. Keyframes without an explicit `(n)` are spaced evenly over `[0, 1]` (`k` keyframes at `i
/ (k − 1)`) — the reading the pinned form implies, INFERRED for the unpinned one; `raindrops2`'s
`color "0,80(10)"` at `frames 15` (second keyframe at `t = 0.667`) is the witness. `a~b` inside a
keyframe is rolled per particle at spawn. The angle keys (`rotation`, `theta`, `phi`) interpolate
the shortest way round. The corpus maximum is five keyframes (`rate "15,5~50,20,5~50,15"`).

Staged form, every rampable key: a list `[[t, lo, hi], …]` in normalized age with the unit above;
a scalar is the one keyframe `[0, v, v]`, a range `[0, a, b]`. A particle-key ramp is evaluated at
the particle's own age; a spawn-key ramp at the **spawning particle's** age when the child spawns
(the root's own clock for a root's spawn blocks).

### Emission

`rate` is particles per second through an accumulator, **sub-frame interpolated** along the
emitter's movement. `burst` adds `RandomFloat(lo, hi)` to the accumulator when its keyframe fires,
and is the one key allowed a first keyframe at `t ≠ 0`. One per-emitter float multiplies **both**
`rate` and `burst`; `SetRateScale`, `func_particle`'s volume scalar and the rain follow all drive
that one float. There is no per-frame cap.

### Motion and the emitter basis

Every particle carries a spherical offset `(radius, theta, phi)` **around the emitter origin**,
rebuilt each frame from `radius_speed` / `theta_speed` / `phi_speed`; `x/y/z_speed` are velocities
in the **emitter's basis — X forward, Y up, Z right**; `elevation_speed` is world up. The spawn
offsets `x y z` are in the same basis, `elevation` world up, `radius theta phi` the initial
spherical offset. `theta` is the azimuth about the basis up axis and `phi` the elevation off the
forward/right plane — the convention `rain_follow_emitter`'s `rainfog` block (`radius 800`, `theta
0~360`, `phi 0`: a horizontal disc at the emitter's height) authors, INFERRED beyond that witness.
`parent_speed` (default 1) is the fraction of the parent's per-frame movement **live** particles
inherit, so live particles follow a moving parent.

The consumer's frame: the entity's `angles` set the basis; particle X → the actor's forward (+X),
particle Y → its up (+Z), particle Z → its right (+Y), `elevation` → world +Z. Lengths `× 2.54`.

### Size and the sprite aspect

The atlas stores each sprite's normalized aspect half-extents (long axis 0.5): a `w × h` image has
`(ax, ay) = (0.5 · w / max(w, h), 0.5 · h / max(w, h))`. The quad's half-extents in inches are

```
halfX = ax × width  × size × spawn.width
halfY = ay × height × size × spawn.height
```

so for a square sprite `size` is the quad edge in inches, and `raindrops2` (`DropletFast` 5 × 25,
`size 3`, `height 10`) is a **1.5 cm × 76 cm** streak. Staged: `size_cm` (`× 2.54`), `width` and
`height` dimensionless, the sprite's `aspect` beside its id.

### Colour and the mode-8 blend

Every particle draws on one material (`engine/particlenormal.vmt`, shader `Sprite`,
`$spriterendermode 8`), vertex colour modulated. Mode 8 is `BlendFunc(ONE, ONE_MINUS_SRC_ALPHA)`,
depth test on, depth write off:

```
dst = tex.rgb × vcol.rgb  +  dst × (1 − tex.a × vcol.a)
```

Vertex alpha never multiplies the source colour; it only erases the destination. **`mask 0` is pure
additive, `mask 255` an occluding alpha-blended card**, every value between a continuous knob.
The channel intensity is the product of the particle's `red/green/blue`, its `color`, the spawn
block's channel and colour keys and the creator's `m_fRed/Green/BlueScale`, each at unity for 255
(or 1), so the staged 0..1 folding is loss-free whatever the consumer multiplies; the alpha is
`mask × spawn.mask × m_fMaskScale` the same way. `surface_color 0`, `use_surface_color 0` and
`ignore_surface_color 1` are one opt-out that pins the creator's tint to white; there is no
lightmap or surface sample anywhere in the particle code. `normal` + `refract` draw the batch on
`engine/particlerefract` (a DUDV card).

The consumer's blend is Unreal's `BLEND_AlphaComposite` (`src + dst × (1 − srcAlpha)`) with
emissive `tex.rgb × colour` and opacity `tex.a × mask` — the same equation, no second material.

### Facing, sort and depth

`movealign` aligns the quad's vertical axis to the velocity; `flat` uses the particle's own basis
(also the forced mode of a collide decal); `sortfront` forces the particle to the front of the
sort; `no_z_test` is a second render list drawn after the first, without the depth test;
`depth_offset` is world units of view depth applied to the sort key and the quad centre (staged
`depth_offset_cm`); `lighting` takes a per-particle branch sampling at the particle position
(INFERRED that the sample is light; `0x200d3840` is the open item).

### Collision

With `collide {}` authored, each frame's movement is traced with the engine ray trace, mask
`SOLID | WINDOW | GRATE | MOVEABLE` — the world **and** brush entities. On a hit every record runs:
each `spawn { particle }` spawns its child at the impact, each `decal { particle }` lays that
definition's sprite as a decal, `vdecal_first` / `vdecal_last` / `vdecal_angle_spread` lay a
random decal from a numbered range (0 placed uses), and `self` keeps the particle alive with
`v' = bounce × normal + friction × tangent`, then `gravity` and `pow(drag, dt)`; without `self` the
particle ends at the hit. Defaults `bounce` 1, `friction` 1, `gravity` 0, `drag` 1. The corpus
writes `friction` / `Bounce` inside the nested `collide { spawn { } }` block (`raindrops2`), where
the spawn table has no such row; the stage carries them on the collide record flagged `nested`,
and no `self` record is authored beside them anywhere, so the reading is never observable.

`precipitation "1"` on a root is a **runtime** kill: a particle under it dies the moment its BSP
leaf lacks the sky-visible bit (`engine.dll 0x200d3f10`). The flag is staged; the gate is the
weather plan's follow-up (it needs the visibility unit's leaf sky bit).

### Roles and the tree

A definition's `role` is by content: `drawing` (sprite, no spawn), `emitter` (spawn, no sprite),
`both`, `neither`. A closure is a **tree**: root → its spawn blocks → children, each child reached
through one block; a `both` node draws and spawns, its children spawning at its own particles'
positions with the spawn ramps evaluated at those particles' age; a `collide { spawn }` child
spawns at the impact. A definition reached through two blocks is two nodes, because the block's
keys differ. The placed corpus' deepest tree is depth 4 with 20 drawing leaves
(`blood_guardian_summon_emitter`); a reference the install lacks is a node with `resolved: false`
and the engine's silent failure.

The emitter lifecycle: create, tick returning world bounds, **stop = clear `loop`, feeding stops,
live particles finish**, free. `TurnOn` on the placing entity rebuilds the emitter (the restart).

## Extension reference

| Key | Kind | Contents |
|---|---|---|
| `schemaVersion` | string | `1.0.0` |
| `identity` | object | `asset`, `particlePath`, `sourcePolicy` |
| `sourceResolution` | object | the member table |
| `root` | string | the root token as spelled |
| `keys` | array | every key/value pair |
| `blocks` | array | the sub-block table |
| `projection` | object | `definition`, `particle`, `spawns[]`, `collide`, with the vocabulary applied |
| `role` | string | `drawing`, `emitter`, `both`, `neither` |
| `comments` | array | every `//` comment with offset |
| `dependencies` | array | the reference table |
| `anomalies`, `omissions` | arrays | see below |
| `coverage` | object | the coverage object |

## Dependencies

| Role | Produced by |
|---|---|
| `particle` | `spawn.particle`, `decal.particle` |
| `image` | `sprite` → `vtmb:image:particles/<sprite>.tga` |
| `material` | a `materials/` path, should one be authored |

A child or sprite the install lacks keeps `resolved: false` and warns: the engine fails the
reference silently, and the definition's own decode does not depend on it.

## Anomalies and omissions

| Row | Meaning |
|---|---|
| `anomalies[] unbalanced-braces` | brace depth does not return to zero; `fire2_emitter` and the Tourette suicide definition ship this way |
| `anomalies[] repeated-scalar-key` | a scalar key repeated within one block; last wins |
| `anomalies[] root-not-particle` | a root token other than `Particle` |
| `anomalies[] empty-definition` | a root block with no keys |
| `anomalies[] frames-zero-on-wrapper` | `frames "0"` on an emitter-only wrapper |
| `omissions[] unparsed-region` | text after a malformed brace that no node claims, published as `raw` with its span |
| `omissions[] empty-member` | a zero-byte file |

A malformed file publishes a best-effort tree over the region the lexer can structure and claims
the rest as `unparsed-region`, so it is still gapless and still warns.

## Byte ledger owners

| Owner | Range |
|---|---|
| `root` | the root token and its braces |
| `keys[i].key`, `keys[i].value` | one key token, one value token |
| `blocks[i].braces`, `blocks[i].name` | one sub-block's name token and braces |
| `comments[i]` | one comment through end of line |
| `whitespace` | insignificant whitespace |
| `unparsed[i]` | one unparsed region, `omitted-proven` |

## Coverage and validation

A complete particle unit has zero `unresolved` and zero `unsupported` rows, and every key is
either in the vocabulary or in `typedUnidentified`. Validation re-lexes the file independently of
the writer, checks that the owners concatenate to the source bytes, rebuilds `keys` and `blocks`,
re-parses every value and compares, and re-derives `role` from the key set.
