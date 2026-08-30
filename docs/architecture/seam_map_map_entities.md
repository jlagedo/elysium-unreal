# Map-entities GLB seam

This document defines the entities sub-unit of one VtMB BSP map: the ENTITIES lump (lump 0)
decoded as an unfiltered, addressable table of every entity the map authors, with each keyvalue,
output and cross-seam reference stated once. The map root (`seam_map_map.md`) owns the BSP
header, the lump directory and the partition proof; shared rules are owned by
`seam_map_unit_contract.md`; the meaning of classes, keys, inputs and outputs is owned by
`docs/vtmb/entity_io.md`, `docs/vtmb/entity_visuals.md` and the per-system VtMB documents.

## Unit identity

```text
<VTMB>/Unofficial_Patch -> maps/<map>.bsp  (lump 0 span)
  -> vtmb:map-entities:<map>
  -> $ELYSIUM_EXPORT_V2_ROOT/maps/<map>.entities.glb
```

The key is the map stem. The BSP member resolves UP-first; the unit's one `sourceResolution`
member is the BSP with a `span` of lump 0's offset and length, and its ledger is gapless over
that span alone. The unit shares the root's `sourceResolution.origin`, `sha256` of the whole
file and `mapRevision` so the two can be matched without opening either's sibling.

```text
uv run elysium export_v2 map-entities-glb <map>
uv run elysium export_v2 map-entities-glb --all
```

`map-glb` publishes this unit beside the root as part of the partition.

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `maps/<map>.bsp` lump 0 | unit-selecting, authoritative | `entities[]`, `worldspawn`, `classCensus[]` |
| `maps/<map>.bsp` lump 14 | brush-model index space | `*N` model references validated against the root's `models[]` count |

The lump is Latin-1 text: a sequence of `{ … }` blocks of `"key" "value"` pairs, one pair per
line, LF line ends, closed by one NUL byte. On every one of the 108 maps the lump holds exactly
one NUL, at its last byte, no CR and no byte above 127; those are census facts the unit verifies
per map and reports in `anomalies[]` when they fail.

## GLB structure

The unit is scene-less. Every datum is a string the source wrote as text or a number read from
one, so the unit has no BIN chunk.

```json
{
  "asset": {"version": "2.0", "generator": "Elysium Map-entities GLB Exporter"},
  "extensionsUsed": ["ELYSIUM_vtmb_map_entities"],
  "extensionsRequired": ["ELYSIUM_vtmb_map_entities"],
  "extensions": {
    "ELYSIUM_vtmb_map_entities": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "coordinateTransform": {},
      "map": {},
      "worldspawn": {},
      "entities": [],
      "classCensus": [],
      "scriptExpressions": [],
      "dependencies": [],
      "comments": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

## Mapping

| Entity datum | Extension field |
|---|---|
| Block order | `entities[i].index`, the zero-based block order in the lump |
| `classname` | `entities[i].classname`, plus the raw pair in `keyValues[]` |
| Every keyvalue, in order | `entities[i].keyValues[]` of `index`, `key`, `sourceKey`, `value`, `quotedKey`, `quotedValue`, `offset`, `length` |
| Repeated keys | kept in authored order, every occurrence; a scalar key's engine value is its last occurrence |
| `origin`, `angles` | `entities[i].origin`, `entities[i].angles` — `{raw, source, gltf}`; `source` read with C `atof` semantics, `gltf` through the unit contract transform |
| Outputs | `entities[i].outputs[]` — one row per keyvalue whose key the entity's datamap types as an output, with the parsed fields below and `keyValue` back-link |
| `model` | `entities[i].model` — `{"kind": "brush", "index": N}` for `*N`, `{"kind": "model", "asset": "vtmb:model:…"}` for a studio path, `{"kind": "sprite" | "other", "path": …}` otherwise |
| Typed joins | `entities[i].references[]` — every keyvalue that names another unit, with `keyValue`, `role`, `asset`, `resolved` |
| `worldspawn` | `worldspawn` — the block's index and the same row shape, restated at root level because every map has exactly one and its keys (`skyname`, `detailmaterial`, `maxpropscreenwidth`, fog keys) are map-wide |
| Field-5 Python | `scriptExpressions[]` — every output's Python call string verbatim, with `entity`, `output` back-links |
| Class vocabulary | `classCensus[]` of `classname`, `count` |

The unit carries no per-class schema. Every key of every entity is present in `keyValues[]`
whether or not any consumer reads it, so a class handler's field usage is a consumer fact checked
against this table, not a filter applied to it.

### Numbers

A keyvalue number is read the way the engine reads it: C `atof` — the longest numeric prefix,
stopping at the first character that cannot continue the number. `hw_jewelry_1` ships origins
with comma decimals (`"-3496,92"`), which the engine and therefore this unit read as `-3496`.
Every numeric field publishes the raw string beside the value it resolved to, and a string whose
`atof` prefix is shorter than its non-blank content produces an `atof-truncated-number` anomaly
naming the entity, key and the dropped suffix.

### Outputs

An output keyvalue's value is a comma-joined list. Retail writes seven fields and consumes six:

```text
target , input , parameter , delay , times , python , extra
   0       1        2          3       4       5       6
```

A row publishes `target`, `input`, `parameter`, `delay` (`{raw, value}`), `times`
(`{raw, value}`; the parser initializes `-1` and rewrites an authored `0` to `-1`, so both mean
unlimited — the row carries `unlimited: true` for either), `python` (verbatim), `extra` (verbatim,
present only when authored, never interpreted), `fieldCount` and `raw`. Across the 108 maps,
24,065 of 24,081 outputs write seven fields, 14 write six, one writes five and one writes eight;
a count other than seven is a `output-field-count` anomaly, not a failure.

A negative `delay` is carried as authored, because the engine admits it unclamped. Repeated rows
for one output name are kept in lump order; their reverse firing order is engine behaviour owned
by `docs/vtmb/entity_io.md`, not a reordering performed here.

Whether an `On*`/`Out*` key is an output is decided by the class's datamap, which is engine
knowledge. The unit types a key as an output when the class's declared output surface
(`docs/vtmb/entity_io.md` § "Input surface per class") names it, and otherwise keeps it as a plain
keyvalue with `outputLike: true` — `sm_diner_1`'s two `trigger_player_activity_level.OnTrigger`
rows are the shipped example of a key that looks like an output and is not one.

### References

Every keyvalue that names another seam's unit produces one `references[]` row on the entity and
one `dependencies` row on the unit:

| Key or pattern | Role | Asset |
|---|---|---|
| `model` = `models/….mdl` | `model` | `vtmb:model:<path below models/ without .mdl>` |
| `model` = `*N` | `brush-model` | the root unit, `models[N]` |
| `scheme_file` | `sound-scheme` | `vtmb:sound-scheme:<stem below sound/schemes/>` |
| `message`, `noise*`, `*sound*`, `soundgroup` keys naming a `.wav`/`.mp3` | `sound` | `vtmb:sound:<path below sound/>` |
| `definition_file` | `vdata` | `vtmb:vdata:signs/<name>` |
| `hackterminal`, `terminal_file` | `vdata` | `vtmb:vdata:hackterminals/<name>` |
| `particle*`, `emitter` keys naming a particle root | `particle` | `vtmb:particle:<name>` |
| `scenefile`, `SceneFile` | `scene` | `vtmb:scene:<path below sound/>` |
| `material`, `texture`, `decal`, sprite `model` | `material` | `vtmb:material:<path below materials/>` |
| `worldspawn.skyname` | `material` ×6 | `vtmb:material:skybox/<name>{ft,bk,lf,rt,up,dn}` |
| `dialog_file`, `dlg` keys | `dialogue` | `vtmb:dialogue:<path below dlg/>` |
| `python` output field, `logic_pythoncheck` expressions | none | `scriptExpressions[]`; the script unit join is by module name and is a consumer computation |

Spelling is preserved in `sourcePath`; the asset key is folded. A path that resolves to no member
is `resolved: false` and warns; the join is another seam's data, not this unit's structure. The
key patterns above are the vocabulary observed over the 108 maps and are closed at export: a
value that matches a known file extension under a key outside the table produces a
`untyped-file-reference` anomaly so the vocabulary grows by evidence.

## Extension reference

| Key | Contents |
|---|---|
| `map` | `stem`, `mapRevision`, the lump's `offset` and `length` |
| `coordinateTransform` | the unit contract rule for `origin` (position) and `angles` (pitch, yaw, roll in degrees, restated as a glTF quaternion) |
| `worldspawn` | the worldspawn row |
| `entities` | one row per block, in lump order |
| `classCensus` | class counts |
| `scriptExpressions` | verbatim Python strings with back-links |
| `comments` | none are possible in this format; present and empty for uniformity |
| `anomalies` | `duplicate-scalar-key`, `unterminated-block`, `non-ascii-byte`, `atof-truncated-number`, `output-field-count`, `untyped-file-reference`, `terminator-count`, `line-end-cr` |
| `omissions` | `bytes-after-terminator` — any bytes past the NUL, claimed `omitted-proven` with digest |

`duplicate-scalar-key` fires when a key that the engine reads as a scalar is authored more than
once in one block; the row names the entity, key and every occurrence.

## Byte ledger owners

| Owner | Range |
|---|---|
| `entities[i].braces.open`, `.close` | the `{` and `}` bytes |
| `entities[i].keyValues[j]` | the pair from its opening quote to its closing quote |
| `whitespace` | LF and blank runs between tokens |
| `trailing-null` | the final NUL byte, verified to be one and last |
| `omissions[] bytes-after-terminator` | anything after the NUL |

Every range is `mapped-text` except the terminator (`reserved-zero`) and a post-terminator range
(`omitted-proven`).

## Coverage and validation

A complete unit has zero `unresolved` and zero `unsupported` rows; the format has no reserved
vocabulary, so `unsupported` is empty by construction and `unresolved` names only a block the
tokenizer could not close. Export-time validation re-parses the lump independently of the writer
and checks block count, every pair's text and offset, every output's fields against a fresh split,
every `*N` model index against the root's model count, and every `references[]` row against a
`dependencies` row. The standalone validator checks the ledger, the scene-less rule, and that
every `entities[i].outputs[]` row back-links to an existing `keyValues[]` index.
