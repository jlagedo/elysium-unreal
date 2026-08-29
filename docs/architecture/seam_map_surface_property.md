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
