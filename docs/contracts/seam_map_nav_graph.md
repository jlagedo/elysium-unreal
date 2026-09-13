# Nav-graph GLB seam

This document defines one binary glTF 2.0 unit for one VtMB AI node graph: the compiled
`.ain` the engine builds from a map's `info_node` entities, and the `.loc` stamp shipped beside
it. Shared rules are owned by `seam_map_unit_contract.md`; the entities the graph was compiled
from are owned by `seam_map_map_entities.md`, and NPC navigation behaviour by
`docs/vtmb/npc-ai/README.md` and `docs/architecture/map-architecture.md`.

## Unit identity

```text
<VTMB>/Vampire/pack*.vpk -> maps/graphs/<map>.ain
<VTMB>/Vampire/pack*.vpk -> maps/graphs/<map>.loc
  -> vtmb:nav-graph:<map>
  -> $ELYSIUM_EXPORT_V2_ROOT/nav-graphs/<map>.glb
```

The key is the map stem. The `.ain` is required and selects the unit; the `.loc` is an optional
companion. The retail VPKs ship 100 of each; both resolve UP-first.

The engine also writes both files under the install's loose `maps/graphs/` while it runs, and
that tree is a runtime cache the install index skips. A loose `maps/graphs/` member is therefore
**not** a source for this unit: only the VPK members, and a patch member under
`Unofficial_Patch/maps/graphs/` should one ship, are admitted.

```text
uv run elysium export_v2 nav-graph-glb <map>
uv run elysium export_v2 nav-graphs-glb
```

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| `maps/graphs/<map>.ain` | unit-selecting, authoritative | `header`, `zones`, `nodes[]`, `links[]`, `wcLookup[]`; node and link accessors |
| `maps/graphs/<map>.loc` | companion stamp | `stamp` |

Both are text. The `.ain` is CRLF, ASCII, whitespace-tokenized; sp_tutorial_1's is 32,688 bytes
over 363 lines, sm_hub_1's 2,462 lines. The `.loc` is one decimal integer and a CRLF —
`68288495\r\n` on sp_tutorial_1, 10 bytes.

## The `.ain` grammar

The file is a sequence of labelled header lines, then three token streams. Line breaks are not
record boundaries inside the streams, and the label `Nodes:` recurs inside the node stream — 4
times on sp_tutorial_1, 19 on sm_hub_1 — without any correspondence to the zone count.

```text
Version\t30
NumHulls:         22
UsedHullBits:     524289
ZoneCount:        11
<ZoneCount ints>
NumNodes:         116
<blank>
Nodes:            <node stream>
TotalNumLinks      234
<link stream>
WCLookup:          <NumNodes ints>
```

`Version` is 30 on every shipped file, `NumHulls` 22, and the zone line carries `ZoneCount`
integers.

### Node stream

Each node is 32 tokens. The first 30 are on one line, the last two open the next line ahead of the
following node's tokens, so a 32-token line is `<t30> <t31>` of node `n` followed by all 30 lead
tokens of node `n+1`, and a 2-token line is the trailing pair of the node preceding a `Nodes:`
label. On sp_tutorial_1: 112 lines of 32 tokens, 4 of 31 (`Nodes:` plus 30), 4 of 2, and
`116 × 32 + 4 = 3,716` tokens in the region — which is the check the decoder performs.

| Tokens | Field | Published as |
|---|---|---|
| 0 | `x,y,z` (comma-joined floats) | `origin` `{source, gltf}` |
| 1 | yaw, degrees | `yaw` |
| 2–23 | 22 per-hull floats | `hullOffsets[22]`; `-3.87`, `-4.87`, `-8.87` and `0.10` are the observed values |
| 24–29 | six integers | `tail[6]`, typed-unidentified |
| 30–31 | two integers | `lead[2]`, typed-unidentified |

`tail` and `lead` carry values that read as bit masks (`134217728`, `33554432`, `1073741824`,
negative two's-complement) beside small counts; the stock Source node record's `nodeType`,
`nodeInfo` and `zone` are the candidate readings and none is admitted without a
decompile-verified mapping. Each node publishes `sourceLine` and `sourceOffset` of its first token.

### Link stream

`TotalNumLinks` lines of 25 integer tokens follow: on sp_tutorial_1 exactly 234, every line 25
tokens. Tokens 0 and 1 are node indexes (`src`, `dst`); of the remaining 23, only column 1 (values
`0`/`1`) and column 20 (values `1`/`2`) are ever non-zero on that map. With 22 hulls declared, 22
of the 23 read as per-hull move flags and one is extra; which one, and whether the flags are
booleans or move types, is to be verified against the binary. The unit publishes `src`, `dst`
and `fields[23]` typed-unidentified, and the line-list accessor draws `src → dst`.

### WCLookup

`NumNodes` integers, one per node in node order: the Hammer `nodeid` the node was compiled from
(`0` where no entity carries the id). It joins `nodes[i]` to the entities unit's `info_node`
rows by `nodeid`.

## GLB structure

```text
nav-graph.glb
|- JSON chunk
|  |- scenes[0], one node per graph node, one mesh node holding the link primitive
|  `- extensions.ELYSIUM_vtmb_nav_graph
`- BIN chunk
   |- node POSITION accessor  (VEC3 FLOAT, glTF metres)
   `- link index accessor     (SCALAR UNSIGNED_INT, pairs, primitive mode 1)
```

```json
{
  "asset": {"version": "2.0", "generator": "Elysium Nav-graph GLB Exporter"},
  "extensionsUsed": ["ELYSIUM_vtmb_nav_graph"],
  "extensionsRequired": ["ELYSIUM_vtmb_nav_graph"],
  "extensions": {
    "ELYSIUM_vtmb_nav_graph": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "coordinateTransform": {},
      "header": {},
      "zones": [],
      "nodes": [],
      "links": [],
      "wcLookup": [],
      "stamp": {},
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

## Mapping

| Graph datum | glTF core | Extension |
|---|---|---|
| Node position | `nodes[i].translation` and the `POSITION` accessor | `nodes[i].origin` source and glTF, `yaw`, `hullOffsets`, `tail`, `lead`, `sourceLine`, `sourceOffset` |
| Node yaw | `nodes[i].rotation` about glTF +Y | `yaw` in degrees |
| Links | one mesh, one primitive, `mode: 1` (LINES), indices `src, dst` per link | `links[i]` with `fields[23]` and source location |
| Header | — | `header.version`, `numHulls`, `usedHullBits` (int and 22-bit decode), `zoneCount`, `numNodes`, `totalNumLinks` |
| Zone table | — | `zones[]` |
| Hammer ids | — | `wcLookup[]`, and `nodes[i].wcId` |
| `.loc` | — | `stamp` |

`coordinateTransform` follows the unit contract; the graph's positions are Source inches.

## The `.loc` stamp

`stamp` publishes `{raw, value, byteLength}`. Its meaning is typed-unidentified: the engine
compares the graph against the map before trusting it, and the stamp is the candidate for that
comparison, but on sp_tutorial_1 the value `68288495` equals neither the BSP's `mapRevision` (28)
nor the CRC-32 of the patched BSP (`3182719289`), and the retail BSP has not been tested. The
corpus index carries the cross-check once the rule is found; until then the unit states the value
and its `sha256`.

## Dependencies

| Role | Produced by |
|---|---|
| `map` | `vtmb:map:<map>` — the map the graph belongs to |
| `map-entities` | `vtmb:map-entities:<map>` — the `info_node` rows `wcLookup` joins to |

Both are joins to another seam's data and warn when absent.

## Byte ledger owners

| Owner | Range | State |
|---|---|---|
| `header.version`, `.numHulls`, `.usedHullBits`, `.zoneCount`, `.numNodes`, `.totalNumLinks` | each labelled line's label and value | `mapped-text` |
| `zones` | the zone line | `mapped-text` |
| `nodes[i]` | that node's 32 tokens | `mapped-text` |
| `nodes.label[n]` | one `Nodes:` label token | `mapped-text` |
| `links[i]` | one link line's 25 tokens | `mapped-text` |
| `wcLookup` | the label and its integers | `mapped-text` |
| `whitespace` | spaces, tabs, CRLF and blank lines between tokens | `mapped-text` |
| `stamp` | the `.loc` integer | `mapped-text` |
| `stamp.lineEnd` | its CRLF | `mapped-text` |

## Anomalies and omissions

| Row | Meaning |
|---|---|
| `anomalies[] node-count-mismatch` | the node-region token count is not `NumNodes × 32` plus the label count |
| `anomalies[] link-count-mismatch` | the link line count differs from `TotalNumLinks`, or a line is not 25 tokens |
| `anomalies[] zone-count-mismatch` | the zone line's integer count differs from `ZoneCount` |
| `anomalies[] wclookup-count-mismatch` | `WCLookup` carries other than `NumNodes` integers |
| `anomalies[] link-index-out-of-range` | `src` or `dst` at or above `NumNodes` |
| `anomalies[] unknown-line` | a line whose leading token is not a known label and that falls in no stream |
| `anomalies[] version-not-30` | any other `Version` |
| `omissions[] missing-loc` | no `.loc` companion resolves |

## Coverage and validation

A complete unit has zero `unresolved` and zero `unsupported` rows; `tail`, `lead`, `fields[23]`
and `stamp` are `typedUnidentified` and are counted against completeness by name. Export-time
validation re-tokenizes both files independently of the writer, re-derives every count, compares
every node's tokens with its row and accessor entry, every link with its index pair, and every
`wcLookup` value with the node it labels. The standalone validator checks the accessors'
extents, the line-list primitive's index range, the ledger and the two members' identities.
