# Map-visibility GLB seam

This document defines the visibility sub-unit of one VtMB BSP map: the potentially-visible and
potentially-audible cluster sets (lump 4) decompressed into bitsets, and the portal graph lumps
(22–25) that five maps carry. The map root (`seam_map_map.md`) owns the header, the lump
directory, the leaf and area tables and the partition proof; shared rules are owned by
`seam_map_unit_contract.md`; the compression scheme is owned by `docs/vtmb/bsp_format.md`
§ VISIBILITY.

## Unit identity

```text
<VTMB>/Unofficial_Patch -> maps/<map>.bsp  (lump 4, and lumps 22-25 where present)
  -> vtmb:map-visibility:<map>
  -> $ELYSIUM_EXPORT_V2_ROOT/maps/<map>.visibility.glb
```

The key is the map stem. The one `sourceResolution` member is the BSP with one `span` per owned
lump; the ledger is gapless over those spans.

```text
uv run elysium export_v2 map-visibility-glb <map>
uv run elysium export_v2 map-visibility-glb --all
```

## Lump partition

| Lump | Name | Maps | Bytes (all maps) | Destination |
|---:|---|---:|---:|---|
| 4 | VISIBILITY | 108 | 7.9 MB | `clusters[]` rows and bitset accessors |
| 22 | PORTALS | 5 | 60.9 KB | `portals[]` |
| 23 | CLUSTERS | 5 | 42.2 KB | `portalClusters[]` |
| 24 | PORTALVERTS | 5 | 33.8 KB | `portalVerts[]` |
| 25 | CLUSTERPORTALS | 5 | 15.2 KB | `clusterPortals[]` |

The five maps with a portal graph are `sm_tattoo`, `sp_genesisdevice_1`, `sp_giovanni_4`,
`sp_giovanni_5` and `sp_soc_4`. On the other 103 the four rows are `reserved-zero` directory
entries in the root and this unit lists them as absent in `map.lumps`.

## Source closure

| Source member | Role | GLB destination |
|---|---|---|
| lump 4 | the visibility data | `numClusters`, `clusters[]`, PVS/PAS accessors |
| lumps 22–25 | the portal graph | typed-unidentified records with offsets |
| lump 10 (root) | leaf → cluster | `clusters[].leaves[]`, `derived` |

## Visibility decode

Lump 4 opens with `int numclusters` and a table of `int byteofs[numclusters][2]` — `[0]` the PVS
row offset, `[1]` the PAS row offset, both relative to the lump start — followed by the row data.
A row is run-length coded: a non-zero byte is copied, a `0x00` byte is followed by a count of
zero bytes to emit, and the row ends when `ceil(numclusters / 8)` bytes have been produced.

The unit stores each decompressed row as one `SCALAR` `UNSIGNED_BYTE` accessor of
`ceil(numclusters / 8)` bytes, bit `c` of the row set when cluster `c` is visible (audible) from
the row's cluster. The compressed span is `derived` in the ledger — its decoded content is the
accessor — and the header and offset table are `mapped`.

Two clusters whose `byteofs` entries coincide share one row. The unit records that row once, on
the lowest-indexed cluster, and the other cluster's entry carries `sharedWith: <cluster>` and no
accessor of its own, so a shared row is one accessor rather than two. On sp_tutorial_1 (2,651
clusters) and sm_hub_1 (1,164) no two clusters share a row, and every row decodes to exactly the
expected length with the last row ending at the lump's last byte; a map where a row runs past the
lump or ends short is an anomaly, not a failure, and the row is published truncated with the
anomaly naming it.

`clusters[i]` publishes:

| Field | Meaning |
|---|---|
| `index` | the cluster number |
| `pvs` | `{accessor, offset, compressedLength}` or `{sharedWith}` |
| `pas` | the same for the audible set |
| `leaves` | the root leaf indexes whose `cluster` is this one, `derived` |
| `visibleCount`, `audibleCount` | the row's set-bit counts |

Cluster `-1` — a leaf outside every cluster — has no row; `clusters[].leaves[]` covers only
clustered leaves, and `unclusteredLeaves[]` lists the rest.

The 3D-skybox split and the area-reachability computation (`pvs_reachable` in
`formats/bsp.py`) are consumer computations over this unit, the root's leaf and area tables and
the entities unit's `sky_camera`; the unit does not publish a per-cluster area list, because the
three units already carry everything that computation needs and a fourth statement of it would
be a derived duplicate.

## Portal graph

Lumps 22–25 are the compiler's portal graph, present on five maps. Their record layouts are not
established against these five maps' bytes: the stock Source `dportal_t`, `dcluster_t`,
`dportalvert` and `dclusterportal` shapes are the candidates, and each lump's length must divide
by the candidate record size before that reading is admitted. Until then the unit publishes each
lump as `typedUnidentified` — one entry per lump with `offset`, `length`, `sha256`, the candidate
struct name and the divisibility test result — and the ledger claims the span `mapped` under the
lump's owner name, so the bytes are accounted for and their meaning is stated as open. A map
without those lumps is complete with none of this.

## GLB structure

```text
map.visibility.glb
|- JSON chunk
|  `- extensions.ELYSIUM_vtmb_map_visibility
`- BIN chunk
   |- one PVS bitset accessor per unshared cluster row
   `- one PAS bitset accessor per unshared cluster row
```

The unit is scene-less.

```json
{
  "asset": {"version": "2.0", "generator": "Elysium Map-visibility GLB Exporter"},
  "extensionsUsed": ["ELYSIUM_vtmb_map_visibility"],
  "extensionsRequired": ["ELYSIUM_vtmb_map_visibility"],
  "extensions": {
    "ELYSIUM_vtmb_map_visibility": {
      "schemaVersion": "1.0.0",
      "identity": {},
      "sourceResolution": {},
      "map": {},
      "numClusters": 0,
      "rowByteLength": 0,
      "clusters": [],
      "unclusteredLeaves": [],
      "portals": {},
      "dependencies": [],
      "anomalies": [],
      "omissions": [],
      "coverage": {}
    }
  }
}
```

## Extension reference

| Key | Contents |
|---|---|
| `map` | `stem`, `mapRevision`, `lumps` — each owned lump's presence, offset and length |
| `numClusters`, `rowByteLength` | the header count and `ceil(numClusters / 8)` |
| `clusters` | the per-cluster rows |
| `unclusteredLeaves` | leaf indexes with cluster `-1`, `derived` |
| `portals` | `portals`, `clusters`, `verts`, `clusterPortals` — each a typed-unidentified entry, or absent |
| `anomalies` | `pvs-row-overrun`, `pvs-row-short`, `pas-row-overrun`, `pas-row-short`, `byteofs-out-of-range`, `portal-lump-not-divisible` |
| `omissions` | `unreferenced-vis-bytes` — lump bytes no row's compressed span covers, `omitted-proven` with digest |

## Dependencies

| Role | Produced by |
|---|---|
| `map` | the root unit, whose leaf table the `derived` rows restate |

## Byte ledger owners

| Owner | Range | State |
|---|---|---|
| `vis.header` | `numclusters` | `mapped` |
| `vis.byteofs[i]` | one cluster's two offsets | `mapped` |
| `vis.pvs[i]`, `vis.pas[i]` | one compressed row | `derived` |
| `vis.unreferenced[n]` | one uncovered run | `omitted-proven` |
| `portals.<lump>` | the whole lump | `mapped`, typed-unidentified |

## Coverage and validation

A complete unit has zero `unresolved` and zero `unsupported` rows; the portal lumps count as
`typedUnidentified`, which is carried and reported but is not `unresolved`, because their bytes
and identity are stated and only their meaning is open. Export-time validation decompresses
every row independently of the writer and compares it with its accessor, checks that shared rows
are recorded once, that every `derived` leaf list matches the root's leaf table, and that the
compressed spans plus unreferenced runs cover lump 4 exactly. The standalone validator checks
every accessor's length against `rowByteLength`, the set-bit counts, the ledger and the
scene-less rule.
