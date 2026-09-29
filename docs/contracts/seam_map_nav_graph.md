# Nav-graph GLB seam

This document defines one binary glTF 2.0 unit for one VtMB AI node graph: the compiled
`.ain` the engine builds from a map's `info_node` entities, and the `.loc` stamp shipped beside
it. Shared rules are owned by `seam_map_unit_contract.md`; the entities the graph was compiled
from are owned by `seam_map_map_entities.md`, and NPC navigation behaviour by
`docs/vtmb/npc-ai/README.md` and `docs/architecture/map-architecture.md`.

## Unit identity

```text
<VTMB>/Unofficial_Patch/maps/graphs/<map>.ain   (loose, the patch's own)
<VTMB>/Vampire/maps/graphs/<map>.ain            (loose, the base game's)
<VTMB>/Vampire/pack*.vpk -> maps/graphs/<map>.ain
  (and the same three for maps/graphs/<map>.loc)
  -> vtmb:nav-graph:<map>
  -> $ELYSIUM_EXPORT_V2_ROOT/nav-graphs/<map>.glb
```

The key is the map stem. The `.ain` is required and selects the unit; the `.loc` is an optional
companion. Both resolve the way the engine's filesystem does: the mod directory's loose file,
then the base game's loose file, then the packs (`docs/vtmb/navigation-jump-links.md` § "Which
graph the patched install runs on", the search order read from `launcher.dll` and
`FileSystem_Stdio.dll`).

**The loose `maps/graphs/` AINs ARE the source** (0018 story 3). `Unofficial_Patch/` ships its own
complete set of 108, built by retail from its own BSPs, and on 107 of the 108 maps the AIN is not
older than the BSP, so retail loads it rather than rebuilding. The install index used to skip the
tree as a runtime cache, which paired the base game's packed 116-node `sp_tutorial_1` graph with
the patch's map (203 nodes); `formats/install.RUNTIME_CACHE_DIRS` no longer lists it, and the
corpus index walks the same keys.

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

**A node is `NumHulls + 6 + ceil(NumNodes / 32)` tokens** — recovered 2026-09-21 (0018 story
21-2) over the patch's 108 loose `.ain` files, exact on 97 of the 97 non-empty graphs. The first
survey read "32 tokens" off the base game's 116-node `sp_tutorial_1`, which is simply what the law
gives for any graph of 97..128 nodes; the patch's own tutorial graph has 203 nodes and is 35 wide,
`sm_hub_1` (578) is 47, `sm_pawnshop_1` (5) is 29. All but the last two tokens are on one line; the
last two open the next line ahead of the following node's tokens, and a 2-token line is the
trailing pair of the node preceding a `Nodes:` label. The label follows the same block size: one
per 32 nodes, `ceil(NumNodes / 32)` of them, 97 of 97. The decoder derives the width from the file
and checks the region against the law; with `W = ceil(NumNodes / 32)`:

| Tokens | Field | Published as |
|---|---|---|
| 0 | `x,y,z` (comma-joined floats) | `origin` `{source, gltf}` |
| 1 | yaw, degrees | `yaw` |
| 2–23 | 22 per-hull floats | `hullOffsets[22]`; `-3.87`, `-4.87`, `-8.87` and `0.10` are the observed values |
| 24 | node type (`node+0x70`) | `type` |
| 25 | node flags (`node+0x74`) | `flags` |
| 26…25+W | `W` 32-bit words, **one bit per node**: the neighbour bitvector (`node+0x90`) | `neighbourBits[W]` |
| second last | zone id (`node+0x94`) | `zone` |
| last | link count, read and discarded | `linkCount` |

**Every field is named from the loader's walk** (0018 story 4; `0x102f5bd0`,
`docs/vtmb/navigation-jump-links.md` § "The load"): after the 22 hull offsets the loader reads the
type into `+0x70`, the flags into `+0x74`, `(NumNodes + 31) >> 5` words into `+0x90`, the zone into
`+0x94` (0 aborts the load with `Invalid node zone`) and one more integer it discards. Until
schema 2.0.0 the unit published the middle as a typed-unidentified `tail` and the last two as
`lead`; neither name is published any more.

`type` is the value `CNodeEnt::Spawn 0x102d78d0` writes on the rebuild branch -- 3 for
`info_node_air` / `info_node_air_hint`, 4 for `info_node_climb`, 2 (`NODE_GROUND`, shared as
`formats.nav_graph_glb.model.NODE_GROUND`) for every other class -- and it is what
`CAI_Node::GetPosition 0x102fb0d0` branches on: a ground node adds `hullOffsets[hull]` to its Z.
Over the 108 patch graphs (11,558 nodes) `type` is 2 on 11,517, 1 on 37 and 4 on 4, and never 3;
`flags` is 0 on 11,554 and 1 on 4. What type 1 and flag bit 1 mean belongs to their readers and is
not recovered here -- `CNodeEnt::Spawn` itself writes neither. The large bit-mask-looking values
the old `tail` carried (`134217728`, `33554432`, `1073741824`, negative two's-complement) are
bitvector words.

The bitvector is stored as read. Over all 11,558 nodes of the 97 graphs it never names a node that
is not one of the node's own link neighbours (0 cases); it equals the neighbour set on 6,395 nodes
and is a strict subset on 5,163, and it cannot be derived from the link rows (on `sm_pawnshop_1`
node 0 names node 4 and node 4 does not name node 0, across a link row identical to the five that
are named from both ends). A row whose width departs from the law still publishes: the zone and
link count come off the end, and a row too short to carry a type or flags publishes `null` for it
beside its `node-count-mismatch` anomaly. Each node publishes `sourceLine` and `sourceOffset` of
its first token.

### Link stream

`TotalNumLinks` lines of 25 integer tokens follow: on sp_tutorial_1 exactly 234, every line 25
tokens. Tokens 0 and 1 are node indexes (`src`, `dst`). The unit publishes `src`, `dst` and
`fields[23]`, and the line-list accessor draws `src → dst`.

**What the 23 are** (recovered 2026-09-21, 0018 story 21-8, from the loader's own listing --
`FUN_102f5bd0` at `0x102f61e8`-`0x102f6243`). The loader allocates `0x6c` bytes per link and reads
**three scalars and then twenty-two** (`MOV EBP,0x16`):

| token | lands at | what it is |
|---|---|---|
| 0 | `link+0x04` | `m_iSrcID` -- read back at `0x102f6245` and bounds-checked against `NumNodes` |
| 1 | `link+0x08` | `m_iDestID` -- read back at `0x102f6275`, same check |
| 2 | `link+0x64` | `m_LinkInfo`, the unit's `fields[0]` |
| 3 .. 24 | `link+0x0c` .. `+0x60` | `m_iAcceptedMoveTypes[22]`, the unit's `fields[1+h]` for hull `h` |

So the "one extra" the earlier text could not place is token 2, and it is read THIRD but stored
LAST: the struct is `{ +0x04 src, +0x08 dst, +0x0c move types[22], +0x64 link info }`. The per-hull
column therefore lines up with the hull index directly -- `fields[1]` is hull 0 -- which is why a
map whose `UsedHullBits` is `1` has no non-zero column but `fields[1]`, and why `sm_pier_1`'s 179
`fields[1] == 1` and 8 `fields[1] == 2` are exactly the ground-link and jump-link counts the
navigation gate reports for it. The values are Source's capability bits: `1` ground, `2` jump.

**`m_LinkInfo` bit `0x2000` is a cost penalty, not a prohibition.** It is the only value the field
ever takes in the corpus -- 1,331 links across the 108 graphs carry it and nothing else -- and the
A* at `FUN_102fe9f0` reads it once:

```c
if (param_3 != '\0') { local_20 = rand(5, 10); }
...
if ((param_3 != '\0') && ((local_1c[0x19] & 0x2000) != 0))  // local_1c[0x19] is link+0x64
    fVar16 = extraout_ST0 * (float10)local_20;               // edge cost x 5..10
```

The flag multiplies that edge's cost by a random integer in `[5, 10]`, and only when `param_3` is
set -- which `FUN_102f2060` passes as the navigation goal's own flag byte
(`*(char *)(this->+0x30 + 1)`), so it is a per-route mode rather than a global one; `FUN_103055b0`
passes `'\0'` and gets the unpenalised cost. A marked link is always traversable; retail merely
prices it up. `sm_pier_1`'s link 97 is the corpus's cleanest single example -- the only marked link
in that map -- and is pinned in `validation/nav_known_findings.py` with the measurement that
settled it.

### Flying hulls

A hull's NPC does not necessarily WALK. `MANBAT_HULL` (bit 20) flies, and its graph nodes stand
in the air, so its links are not claims about a walkable surface and no navigation mesh cut for
a walkable agent can answer them. Recovered 2026-09-21 (0018 story 21-8), the first time a map
carrying one reached the navigation gate.

The evidence is threefold and each part is independent:

* **The task list.** `vampire.dll` `thunk_FUN_10389f80` names `TASK_MANBAT_TAKEOFF`,
  `TASK_MANBAT_FLY_TO_HINT`, `TASK_MANBAT_FLY_RANDOM`, `TASK_MANBAT_FALL_TO_GROUND`,
  `TASK_MANBAT_FIND_FLYNODE` and `TASK_MANBAT_FIND_LANDNODE`. The bat takes off, flies between
  fly nodes, finds a land node and falls to the ground.
* **The graph.** On `la_ventruetower_3` -- the corpus's only hull-20 map -- every link the gate
  reported is claimed by hull 20 ALONE (`fields[1+h]` is `0` for hulls 0, 7 and 21 on all seven).
  Counting each node's claims settles it: nodes **46 and 48 carry sixteen hull-20 links and zero
  links for every other hull**, and node 47 carries twelve hull-20 links while hulls 0 and 7 carry
  exactly one each -- whose move type is `2`, a JUMP, not a walk. Hull 21, the Sheriff, the other
  large body on this map, claims none of the three. Contrast nodes 44 and 85, shared by hulls 0,
  20 and 21 at move type `1`: both pass the gate.
* **The mesh.** Asked at its own hull offset, the Manbat's mesh answers at no height within 600 cm
  of any of the three.

`MANBAT_HULL`'s 160-unit height is therefore a flight envelope rather than a body walking under a
ceiling, which is why reading the 70 findings as the clearance-and-erosion problem the `sm_hub_1`
rat holes really are was wrong.

**What is measured and what is inferred.** Measured: the exclusive hull-20 claims above, the
absent mesh, and the ManBat's flight tasks. INFERRED: that these particular nodes are the fly and
land nodes `TASK_MANBAT_FIND_FLYNODE` / `FIND_LANDNODE` search. The node record's own type word is
named now (§ Node stream) and reads 2, ground, on all three -- no shipped graph carries an air node
(type 3) at all -- so nothing here reads a node as `NODE_AIR`; the claim is only that no walking
hull reaches them and the hull that does, flies. One measurement that
looked like counter-evidence is not: the Sheriff's mesh answers 100-125 cm below all three nodes,
but the Sheriff has no link at any of them, so that surface is a lower storey in the same column
and says nothing about the nodes themselves.

Note also that this does not contradict 0018 story 12's "no link in any shipped graph flies": a
link's move type is never the fly bit anywhere in the corpus (`fields[1+h]` is only ever 0, 1 or
2 across all 29,382 links). Retail's flight is a straight move at a goal, not a link traversal.
How a hull-20 GROUND link squares with a flyer that never ground-paths is the open question this
leaves, and it belongs to story 12.

**What the port does with it.** The mesh is still cut: `TASK_MANBAT_FIND_LANDNODE` and
`TASK_MANBAT_FALL_TO_GROUND` say the bat touches the ground, and it needs somewhere to land. But
`validation/nav_acceptance.FLYING_HULLS` routes a flying hull's ground-link, jump-projection and
bridging verdicts through `flight_row`, which reports every finding under `flightClaims` and
fails none of them. They are flight the port does not implement yet, not floor it failed to
rasterise, and the report still names exactly which links and points a flight implementation
owes. Whichever story builds flight reads that list.

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
      "schemaVersion": "2.0.0",
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
| Node position | `nodes[i].translation` and the `POSITION` accessor | `nodes[i].origin` source and glTF, `yaw`, `hullOffsets`, `type`, `flags`, `neighbourBits`, `zone`, `linkCount`, `sourceLine`, `sourceOffset` |
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
| `nodes[i]` | that node's tokens | `mapped-text` |
| `nodes.label[n]` | one `Nodes:` label token | `mapped-text` |
| `links[i]` | one link line's 25 tokens | `mapped-text` |
| `wcLookup` | the label and its integers | `mapped-text` |
| `whitespace` | spaces, tabs, CRLF and blank lines between tokens | `mapped-text` |
| `stamp` | the `.loc` integer | `mapped-text` |
| `stamp.lineEnd` | its CRLF | `mapped-text` |

## Anomalies and omissions

| Row | Meaning |
|---|---|
| `anomalies[] node-count-mismatch` | the node-region token count is not `NumNodes × (NumHulls + 6 + ceil(NumNodes / 32))` plus the label count; carries `expectedNodeWidth` and `derivedNodeWidth`. (Against the old `× 32` it flagged 79 of 101 units — every graph outside 97..128 nodes.) |
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
