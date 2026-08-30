# Expression-table GLB seam

This document defines one binary glTF 2.0 unit for one Faceposer expression or phoneme table
under `expressions/`: the compiled `.vfe` the runtime loads and, where it ships, the readable
`.txt` it was compiled from. Shared rules are owned by `seam_map_unit_contract.md`; format and
selection facts by `docs/vtmb/facial_animation.md` (§`expressions/` and §The compiled `.vfe`
header).

A model unit references its tables by ID and inlines nothing: `phonemes.vfe` and
`phonemes_male.vfe` are the client's literal fallbacks for every model without a same-stem
table, so a table is a shared resource with one owner.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> expressions/<stem>.vfe
<VTMB>/Vampire/pack*.vpk -> expressions/<stem>.txt
  -> vtmb:expression-table:<stem>
  -> $ELYSIUM_EXPORT_V2_ROOT/expression-tables/<stem>.glb
```

The key is the file stem below `expressions/`. The VFE selects the unit when it exists; a stem
that ships only a TXT is still a unit, with `identity.sourceKind: "txt-only"` and
`identity.runtimeLoadable: false`, because no recovered runtime path loads the TXT. Both members
resolve UP-first independently.

```text
uv run elysium export_v2 expression-table-glb <stem>
uv run elysium export_v2 expression-tables-glb
```

The merged install ships 249 VFE and 249 TXT members that are not a twin set:
`scrubs_female_phonemes` is TXT-only and `demal_expressions` is VFE-only, so the corpus holds
250 units.

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `expressions/<stem>.vfe` | runtime authority, unit-selecting | `vfe`, `table` |
| `expressions/<stem>.txt` | authoring twin | `txt`, `authoring`, `comparison` |

## GLB structure

The unit is scene-less with no BIN chunk. The VFE stores its weights as binary floats, but a
table is at most 48 rows over at most 48 keys, and an accessor would restate `table.rows[]` in a
form nothing draws; the numbers are carried in the extension.

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_expression_table"],
  "extensionsRequired": ["ELYSIUM_vtmb_expression_table"],
  "extensions": {
    "ELYSIUM_vtmb_expression_table": {
      "schemaVersion": "1.0.0",
      "identity": {"asset": "vtmb:expression-table:lacroix_phonemes", "stem": "lacroix_phonemes",
                   "class": "phonemes", "sourceKind": "vfe+txt", "runtimeLoadable": true},
      "sourceResolution": {},
      "vfe": {},
      "table": {},
      "txt": {},
      "authoring": {},
      "comparison": {},
      "selectedBy": [],
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

`identity.class` is `expressions`, `phonemes` or `none` from the stem's suffix; seven stems carry
no suffix.

## Table shape

`table` (from the VFE) and `authoring` (from the TXT) share one shape and are never merged:

| Field | Meaning |
|---|---|
| `keys[]` | the flex-controller names the table writes, in declared order |
| `hasWeighting` | whether each row carries a value and a weight per key |
| `rows[]` | one row per setting: `index`, `name`, `class` (a single character or an `0x…` code point on a phoneme table; `_` on an expression table), `phonemeCode` (the integer a `.lip` row holds, when `class` decodes to one), `values[]` and `weights[]` (one per key, `weights` empty without weighting), `description` |

A zero-key table (`crooked_cop_expressions`: `$keys` with no names, weighting declared, 32
labelled rows with no numbers) decodes to empty `keys[]` and empty value arrays and is complete.

## VFE

`vfe` restates the compiled header and its tables:

| Field | Offset | Meaning |
|---|---:|---|
| `id` | 0 | `EFV\0` |
| `version` | 4 | `0` on every shipped member |
| `name` | 8 | the `char[128]` internal name |
| `namesFile` | — | whether `name` equals the member's own `expressions/…` path; 13 of 249 carry another file's name |
| `length` | 136 | the declared file length; exact on 247 of 249 |
| `numFlexSettings` | 140 | the row count |
| `directory[]` | 144 … | the remaining header words — setting, name, index and key table counts and offsets — as stored, each named per Source's `flexsettinghdr_t` and listed in `coverage.typedUnidentified` until the widened-name layout's field order is verified against the loader |
| `settings[]` | per row | the decoded setting records with their string and value offsets |

`phonemes_strong.vfe` and `phonemes_weak.vfe` are 1,260 bytes each and store float `1.0` where
`length` and `numFlexSettings` belong; they are a different layout, not damaged data. Their units
decode the identifier, version and name, carry the remainder as `typedUnidentified` ranges with
offsets and digest, publish `table: null`, and warn.

## Comparison

`comparison` grades the two members against each other and never corrects either:

| State | Meaning | Members in the install |
|---|---|---:|
| `equivalent` | same keys, rows, names and values | the majority |
| `rounding-only` | values differ only by the TXT's three-decimal rounding; `maxDelta` is stated | 21 |
| `extra-keys` | the VFE carries keys the TXT lacks (Pisha: `dilator`, both puckerers, `bite`, `wide_open`) | 1 |
| `row-order` | the same rows in a different order (Luca, Sweeper: `b`/`p` swapped) | 2 |
| `row-renamed` | a row named differently (the two stripper tables: row 9 `eyes closed` versus `bliss`) | 2 |
| `no-twin` | one member only | 2 |

A state other than `equivalent` is also an `anomalies[]` row with the rows or keys concerned,
so a reader who queries anomalies finds every provenance difference without reading
`comparison`.

## Selection

The client formats `expressions/<model stem>_<class>.vfe` from the actor's model basename and
falls back to `expressions/phonemes.vfe` and `expressions/phonemes_male.vfe`; the server selects
the model-specific expression table and then the generic fallback. That rule is the model
seam's join and is stated there. `selectedBy[]` — the models and scenes that reach this table —
is written by the corpus index from the inverse reference graph and is empty at export.

## Dependencies

None. A key names a flex controller of whichever model selects the table; that join is the
corpus index's `scene-expression-rows` and model-controller checks, not a reference this unit
can resolve on its own.

## Anomalies and omissions

| Row | Meaning |
|---|---|
| `anomalies[] internal-name-mismatch` | `vfe.name` names another file |
| `anomalies[] declared-length-mismatch` | `vfe.length` differs from the member length |
| `anomalies[] rounding-only`, `extra-keys`, `row-order`, `row-renamed` | the comparison states above |
| `anomalies[] alternate-layout` | the two 1,260-byte members |
| `omissions[] no-twin` | the missing member |
| `omissions[] trailing-fill` | non-zero bytes past the last decoded record |

## Byte ledger owners

| Owner | Member | Range |
|---|---|---|
| `vfe.header` | VFE | the header through `numFlexSettings` and the directory words |
| `vfe.settings[i]` | VFE | one setting record |
| `vfe.settings[i].name` | VFE | its name string |
| `vfe.values` | VFE | the value and weight arrays |
| `vfe.keys` | VFE | the key-name strings and key index table |
| `vfe.trailing` | VFE | bytes past the last record: `padding-zero` or an omission |
| `txt.keys` | TXT | the `$keys` line |
| `txt.directives[i]` | TXT | `$hasweighting` and any other directive line |
| `txt.rows[i]` | TXT | one row |
| `txt.comments` | TXT | `//` comment lines |
| `txt.whitespace` | TXT | blank lines and insignificant whitespace |

## Coverage and validation

A complete unit has zero `unresolved` and zero `unsupported` rows; the two alternate-layout
members carry their unread ranges as `typedUnidentified` and publish with a warning. Export-time
validation re-decodes both members independently of the writer, compares every key, row, value
and weight of `table` and `authoring`, and recomputes `comparison`. Standalone validation checks
the scene-less rule, the absence of a BIN chunk, the ledger and that `table` and `authoring`
each agree with their own `keys[]` and row counts.
