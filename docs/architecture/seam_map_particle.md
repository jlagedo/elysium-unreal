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
offset. The vocabulary is the union of what `formats/particles.py` compiles and what the corpus
authors:

| Scope | Keys | Meaning |
|---|---|---|
| definition | `loop`, `precipitation`, `fps`, `frames`, `min_frames`, `max_frames` | lifetime and looping |
| drawing particle | `sprite`, `movealign`, `flat`, `sortfront`, `lighting` | sprite binding and orientation |
| drawing particle | `x_speed`, `y_speed`, `z_speed`, `parent_speed`, `radius_speed`, `elevation_speed`, `theta_speed`, `phi_speed` | motion |
| drawing particle | `size`, `radius`, `height`, `width`, `rotation`, `rotate`, `depth_offset` | extent and roll |
| drawing particle | `color`, `red`, `green`, `blue`, `mask` | colour ramps |
| drawing particle | `normal`, `refract` | a second texture used as a refraction card |
| drawing particle | `collide`, `burst` | collision behaviour |
| `spawn` | `particle`, `rate`, `burst`, `loop`, `radius`, `theta`, `phi`, `friction`, `bounce`, `x`, `y`, `z`, `depth_offset`, `timescale`, `frames` | child emission |
| `collide` | `spawn`, `decal` | impact response |
| `decal` | `particle` | the decal definition |

Keys the runtime compiler rejects — `normal`, `refract`, `rotate` on a particle body, `timescale`
and `frames` inside `spawn`, an enabled `sortfront`, `lighting`, `radius` on a drawing particle —
are decoded here like every other key. Where the meaning is not established the projection carries
the key with `meaning: null` and a `typedUnidentified` row; the unit is complete when every key is
accounted for, not when every key is understood. `depth_offset`'s unit is unestablished and stated
so.

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
