# Export-v2 unit contract

This document owns the rules every `export_v2` GLB unit shares: identity, source resolution,
container layout, cross-unit references, coverage vocabulary, byte ledger and validation split.
A seam specification (`seam_map_<kind>.md`) states only what is specific to its unit kind and
links here for the rest. Where a shipped seam restates one of these rules, this document is the
owner and the restatement is a copy.

## Unit

A unit is one binary glTF 2.0 file that is the complete inspectable projection of one VtMB
source identity. One identity produces one file; one file carries one identity. A unit never
carries data another unit owns, and — where its seam has adopted the source capsule — it carries
the exact bytes of its own source members beside the decode of them.

```text
vtmb:<kind>:<key>
  -> $ELYSIUM_EXPORT_V2_ROOT/<family>/<key>.glb
```

`<key>` is lower-case, forward-slashed and install-relative below the kind's root directory. The
extension is dropped where one kind has one extension and kept where it disambiguates (`vtmb:sound:`
keeps `.wav`/`.mp3`, because both spellings of one stem ship). Each seam names its root and its
extension rule.

The public command surface is uniform:

```text
uv run elysium export_v2 <kind>-glb <argument>
uv run elysium export_v2 <kind>s-glb
uv run elysium export_v2 export-all
```

The singular command exports one unit and tolerates the root prefix and the source extension on
its argument; the plural command exports every unit the UP-first install index resolves for the
kind. Code is isolated per kind: `elysium_pipeline.formats.<kind>_glb` decodes,
`elysium_pipeline.exporters.<kind>_glb` writes, `elysium_pipeline.validation.<kind>_glb` reads
back independently of the writer.

## Source resolution

Every member resolves **UP-first**, independently of every other member:

```text
Unofficial_Patch loose -> retail loose -> retail VPK
```

An arrow names a member inside a container; loose members use the same install-relative name:

```text
<VTMB>/Vampire/pack001.vpk -> materials/models/character/teeth/upperteeth.vmt
<VTMB>/Unofficial_Patch    -> materials/models/character/eyes/prince.vmt
```

`sourceResolution` lists every member the unit was decoded from:

```json
{
  "sourceResolution": {
    "policy": "up-first",
    "members": [
      {"role": "mdl", "path": "models/character/npc/unique/downtown/lacroix/lacroix.mdl",
       "origin": {"kind": "loose", "root": "Unofficial_Patch"},
       "byteLength": 1234567, "sha256": "…"}
    ]
  }
}
```

A member `origin` is one of:

| `kind` | Fields | Meaning |
|---|---|---|
| `loose` | `root` | a file below the named install subdirectory |
| `vpk` | `container`, `offset`, `size` | a member of the named retail pack |
| `bsp-pakfile` | `map`, `member`, `origin` | a member of a BSP's PAKFILE lump; `origin` is the BSP's own origin |

A member may carry `span` — `{"offset", "length"}` — when the unit is cut from part of a larger
file (a surface-property entry, a map lump family). The ledger of such a unit is gapless over the
span alone, and the seam that cuts the file proves the whole file partitions into unit spans plus
named non-unit bytes before it publishes any unit from it.

An empty member (zero bytes) is recorded with `byteLength: 0`, the SHA-256 of the empty string and
an `omissions` row `empty-member`; a unit whose selecting member is empty publishes with a warning.

## Source capsule

A unit is a **self-contained capsule**: alongside the decode it carries, byte for byte, the source
members the UP-first policy selected. A reader holding one GLB holds everything the exporter read
and can reproduce the install file without the install, which is what lets the corpus deploy
(`uv run elysium import <family>`) read `exports_v2` alone.

The encoding is the plainest one glTF admits. Each member's bytes go into buffer 0 — the BIN chunk
— at a 4-byte aligned offset, one `bufferView` addresses them, and the member's row in
`sourceResolution.members[]` names that view:

```json
{
  "sourceResolution": {
    "policy": "up-first",
    "capsule": {"encoding": "raw"},
    "members": [
      {"role": "unit-selecting", "path": "vdata/items/item_w_katana.txt",
       "origin": {"kind": "vpk", "container": "pack002.vpk", "offset": 1, "size": 2},
       "byteLength": 4096, "sha256": "…",
       "capsule": {"bufferView": 0, "byteLength": 4096}}
    ]
  }
}
```

A zero-byte member declares `{"byteLength": 0}` and no view, because glTF has no zero-length
`bufferView`; a unit all of whose members are empty therefore still carries no BIN chunk.
`sourceResolution.capsule` is what says the seam has adopted the rule — a seam that has not
publishes no such key, and a member row may not carry a capsule the unit does not declare.

**The capsule never excuses the decode.** The byte ledger, the independent re-decode and every
completeness rule below apply unchanged; a unit that carries its source and does not account for
it is refused exactly as before. What the capsule adds is that the bytes the ledger partitions are
*in the file*, so validation compares the capsule against the member's own `byteLength` and
`sha256` on every read, and at export time against the bytes the exporter actually read.

Rollout is per slice, tracked in `docs/project/seam_migration.md`:

| Seam | Capsule |
|---|---|
| `vtmb:vdata:` | required, schema 1.1.0 |
| every other kind | adopts when its slice migrates; until then it publishes no `capsule` key |

## Container

```text
unit.glb
|- header      magic 'glTF', version 2, total length
|- chunk 0     JSON  (0x4E4F534A), UTF-8, padded to 4 bytes with 0x20
`- chunk 1     BIN   (0x004E4942), padded to 4 bytes with 0x00; absent when the unit has neither an
               accessor nor a non-empty source capsule
```

The JSON chunk is serialized with compact separators, sorted-key-free (writer order) and rejects
`NaN` and infinity, so one source closure yields one byte-identical product. When a BIN chunk
exists there is exactly one `buffers` entry whose `byteLength` is the BIN payload, every
`bufferView` uses buffer 0, and view offsets are 4-byte aligned.

`asset.generator` is `Elysium <Kind> GLB Exporter`. Every unit declares its own extension
`ELYSIUM_vtmb_<kind>` in both `extensionsUsed` and `extensionsRequired`, because the VTMB meaning
is reachable only through it. Cross-reference extensions (`ELYSIUM_material_reference`,
`ELYSIUM_model_reference`, `ELYSIUM_texture_reference`, `ELYSIUM_asset_reference`) are declared
where used.

A unit whose source holds nothing a general consumer can draw or play is **scene-less**: it
declares no `scenes`, `nodes`, `meshes`, `images`, `textures` or `samplers`, and declaring any of
them fails validation. Where core glTF can carry the datum (geometry, skins, animations, morphs,
KTX2 images through a buffer view) it does, and the extension carries the VTMB-only information
beside it; the same datum is never stated twice.

Every unit's extension root carries these keys, in this order, before its kind-specific keys:

| Key | Contents |
|---|---|
| `schemaVersion` | the seam's schema version, semantic |
| `identity` | `asset` (the stable ID), the source path(s) and `sourcePolicy: "up-first"` |
| `sourceResolution` | the member table above |
| `dependencies` | the reference table below |
| `coverage` | the coverage object below |

Numeric records that came from a file offset keep that offset (`sourceOffset`) so a ledger range
and the record it pays for name the same place.

## Coordinate transform

Spatial units state the transformation applied per coordinate-bearing domain in
`coordinateTransform`:

| Field | Value |
|---|---|
| `source` | `Source inches, Z-up, right-handed` |
| `destination` | `glTF metres, Y-up, right-handed` |
| `scale` | `0.0254` |
| `position` | `(x, y, z)_gltf = (x, z, -y)_source * 0.0254` |
| `direction` | `(x, y, z)_gltf = (x, z, -y)_source` |
| `quaternion` | `(x, y, z, w)_gltf = (x, z, -y, w)_source` |
| `domains` | the per-domain rule |

The mapping is a rotation, so triangle winding carries through unchanged. Quaternions are
normalized on conversion; a non-finite or degenerate quaternion fails the export. A domain the
seam keeps in source units (VPhysics metres, cloth inches, lightmap luxels) says so in `domains`.

## References between units

A unit names another unit by stable ID and never inlines its data. Every reference the unit makes
is declared once in `dependencies`, whatever produced it:

```json
{
  "dependencies": [
    {"role": "material", "asset": "vtmb:material:models/character/teeth/upperteeth",
     "sourcePath": "materials/models/character/teeth/upperteeth.vmt", "resolved": true},
    {"role": "sound", "asset": "vtmb:sound:character/dlg/main characters/jack_tutorial/line191_col_e.mp3",
     "sourcePath": "sound/character/dlg/main characters/jack_tutorial/line191_col_e.wav",
     "resolved": true, "resolution": "mp3-first"}
  ]
}
```

`role` names what the referrer needs; `asset` is the referenced identity; `sourcePath` is the
install-relative path the referrer authored (spelling preserved); `resolved` states whether the
UP-first index holds a member for it. Optional `byteLength` and `sha256` pin the referenced bytes
where the referrer's decode depends on them (an included animation bank, a paired VTX).

Object-local references use the reference extensions on the glTF object that binds them:

```json
{"extensions": {"ELYSIUM_model_reference": {"asset": "vtmb:model:scenery/misc/trashcan01"}}}
```

A reference that the referenced kind's own rules make unreachable to the engine keeps a sentinel
identity in the `vtmb:missing-<kind>:` namespace, carries `resolved: false`, produces no
`dependencies` row and enters coverage as `omitted-proven` with the reason the seam names. A
reference whose target is another seam's data and merely fails to resolve warns rather than
failing the unit; a reference that is the unit's own structure (an inheritance base, an include
model, a paired VTX) enters `coverage.unresolved` and fails it.

## Coverage

Coverage is stated in two vocabularies. The semantic states grade a source *field or record*; the
ledger states grade a *byte range*. A field graded `equivalent` and the bytes it was read from
graded `mapped` describe one decode from two directions.

### Semantic states

| State | Meaning |
|---|---|
| `mapped` | represented directly by core glTF or the unit's extension |
| `equivalent` | transformed into a representation with the same understood meaning |
| `derived` | recoverable completely from other represented data |
| `omitted-proven` | confirmed padding, dead storage, unreachable content or a redundant mechanism, with recorded evidence |
| `unresolved` | meaning unknown, or candidate interpretations disagree |
| `unsupported` | meaning understood but the schema does not yet represent it |

`coverage` carries `mapped[]`, `typedUnidentified[]`, `omittedProven[]`, `byteLedger[]`,
`unresolved[]` and `unsupported[]`. A complete unit has zero `unresolved` and zero `unsupported`
rows. A typed value whose meaning is unknown keeps its source-offset identity in
`typedUnidentified` — it is carried, not dropped, and it still counts against completeness.

### Byte ledger

`coverage.byteLedger` holds one row per member in `sourceResolution.members`:

| Field | Meaning |
|---|---|
| `sourcePath` | the member's install-relative path, matching its `sourceResolution` row |
| `sourceSha256` | SHA-256 of the member's bytes (of the span, for a span member) |
| `byteLength` | the member or span length |
| `accountedBytes` | bytes claimed by the range table; equal to `byteLength` |
| `coveragePercent` | `100.0` |
| `stateBytes` | claimed bytes per state, key-sorted |
| `rangesSha256` | digest of the range table |
| `ranges` | the gapless ordered range table |

A range row is `{"offset", "length", "state", "owner"}`. Rows are ordered by `offset`, each begins
where the previous ends, the first begins at 0 and the last ends at `byteLength`. `owner` is the
decoder's path to the record that paid for the range. `rangesSha256` is the SHA-256 of the UTF-8
encoding of

```text
json.dumps({"path": sourcePath, "byteLength": byteLength, "ranges": ranges},
           sort_keys=True, separators=(",", ":"))
```

| Ledger state | Meaning |
|---|---|
| `mapped` | a binary record or payload decoded into the extension or a core accessor, or copied verbatim into a BIN payload whose every byte the extension describes |
| `mapped-string` | a null-terminated string decoded into a named field |
| `mapped-text` | a text region decoded into a structured table |
| `derived` | a compressed or encoded range whose decoded content is represented (zlib spans, ADPCM blocks) |
| `omitted-proven` | an evidence-backed omission carrying its reason in `omissions` or `coverage.omittedProven` |
| `reserved-zero` | a declared field the source stores as zero |
| `padding-zero` | alignment or unreferenced storage the source stores as zero |

`reserved-zero` and `padding-zero` are verified: claiming either over a non-zero source byte aborts
publication. Publication also fails when ranges overlap, leave one byte unclaimed, disagree with
the source hash or length, or when a member has no ledger row. This is **100% byte
accountability**: the capsule says what the bytes were, the ledger says what the decode made of
every one of them, and carrying the first is never an answer for the second.

A byte the format stores as text is claimed by the record its token belongs to; comments and
insignificant whitespace between records are claimed by the enclosing table's `comments[]` and
`whitespace` owners so that a text unit is gapless too.

## Non-canonical storage

Retail bookkeeping is not uniformly canonical. A seam is written against what the bytes support
rather than what a header claims, and every case where the two disagree is a named row in the
unit's `anomalies[]` (a decoded value the source states inconsistently) or `omissions[]` (a range
that contributes no payload byte), each with the evidence that proves the classification. The
exporter never pads, fabricates or reorders source data to make it well-formed; a unit that is
recoverable only in part publishes what the install holds and warns.

## Validation

**Export-time validation** receives the selected source members, runs before the destination is
written, and is the only place `-zero` claims can be proven: it re-reads the members, re-hashes
them against the declared identities, verifies every zero-state range, proves each capsule holds
exactly the bytes it was cut from, re-decodes the source independently of the writer and compares
the result against the emitted core and extension. Only
then is the GLB written to a temporary sibling and atomically renamed over the destination.

**Standalone validation** reads a published unit with no install present and verifies the
container, chunk order, the scene-less rule where it applies, the extension's presence and
version, the identity prefix, every accessor's extent and digest, the ledger's continuity, state
totals, source identities and range-table digest, and — for a seam that declares one — that every
member's capsule is present, the declared length and the declared digest.

Cross-unit consistency — an inheritance chain, a model's include tree, a map's material closure —
is a corpus property checked by the corpus index (`seam_map_corpus_index.md`), not something one
unit can be validated against.

## Baked assets

Every unit kind that lands as Unreal content lands under one mount, `/ElysiumBaked`, and **the
mount mirrors `exports_v2`**: the kind root, the unit's directory, the unit's name. One rule,
stated once here and cited by every seam's `## Import`; a seam states only its prefixes and roles.

```text
exports_v2/<kind>/<dir>/<base>.glb  ->  /ElysiumBaked/<Kind>/<dir'>/<Prefix>_<base'>[_<Role>]      the unit's products
                                        /ElysiumBaked/<Kind>/<dir'>/<base'>/<Prefix>_<label'>       per-label products
                                        /ElysiumBaked/<Kind>/_Corpus/<Prefix>_<name>                corpus-wide, no single unit
```

- **`<Kind>`** is the export root in PascalCase — `Textures`, `Materials`, `Models`,
  `SurfaceProperties`, `Maps`, `ExpressionTables`, `Sounds`, `Particles`, `Scenes`, … Nothing
  else sits at the mount root.
- **Folding.** `<dir'>`, `<base'>` and `<label'>` are the source segments through
  `asset_names.safe_name` (C++ twin `FElysiumContentPaths::SafeName`), one segment at a time.
  The exact source path lives in the provenance, so folding is never a loss. A source segment
  that begins with `_` is refused (`_Corpus` is reserved; no VtMB directory begins with one). A
  resulting file path longer than 240 characters is refused at stage.
- **`<Prefix>`** names the Unreal class: `T_`/`TC_`/`TA_` textures, `MI_` material instances,
  `SM_`/`SK_`/`SKEL_`/`A_`/`BS_`/`CLOTH_`/`PHYS_`/`DYN_` model products, `PM_` physical
  materials, `DA_` data assets. A level carries no prefix: `Maps/<map>/<map>.umap`.
- **`<Role>`** distinguishes two products of one unit that share a class: `_linear`, `_Decal`,
  `_Skinned`, `_Sprite_<Blend>`, `_DetailSway`, `_<n>` for the n-th garment, `_PHYS` for a
  cloth collider. Where the prefix already differs there is no role.
- **Per-label products** — a model's clips and blend spaces, a map's chunk meshes, brushes and
  captures — nest one level, in a folder named for the unit's base. A label folds like a
  segment; a derived host form is `<layer>_<host>`.
- **Corpus-wide** assets that are a function of a set of units and of none in particular —
  family skeletons, the registries, a lane's placeholders — live in `<Kind>/_Corpus/`, named for
  what they are: `SKEL_Family_<crc32 of the sorted member ids>`, `DA_PropSkins`,
  `DA_WieldModels`, `DA_PlacedModels`, `DA_CinematicSets`, `DA_Cast`, `DA_ExpressionTables`,
  `SM_Missing`, `MI_Missing`, `T_MissingChecker`.
- **Composed from several units of one kind** with a common directory — a sky cube from six
  faces — the composite lands in that directory, **written by the lane that owns that kind root**
  and carrying a `<Role>` so it can never collide with a unit's own asset:
  `Textures/skybox/TC_<sky>_Sky`. The stage refuses a composite path equal to a unit path.
- **One resolver.** `elysium_pipeline.asset_paths.baked_path(kind, key, prefix, role=None,
  label=None)` and `FElysiumContentPaths::BakedUnit(...)` are twins over one golden fixture that
  covers every kind, every prefix and role, the folded segments and the per-label nest; each
  `Baked<Kind>(id)` accessor is a one-line call into it. A consumer resolves from the **unit id**,
  built from the raw source path it already holds (`vtmb:model:` + the path below `models/`
  without `.mdl`); no consumer recomputes a stem. `static_stem`, `PropModelStem`, `mesh_asset`,
  `texture_asset_name`, `baked_asset_name` and every stem-keyed accessor retire.
- **Keyed tables key by id, with no exception.** `DA_PropSkins`, `DA_WieldModels`,
  `DA_PlacedModels`, the registries and the authored tuning assets that name a body
  (`DA_ClothTuning`, `DA_HairDynamics`) key their rows by unit id — `DA_WieldModels` by the
  item's `vtmb:vdata:items/<classname>`, whose tail is the classname its caller already passes. A human-facing stem — a capture, an oracle file, a
  debug picker, a CLI argument — resolves through `Models/_Corpus/DA_Cast` (stem ↔ id, written by
  the model lane's stage), never through a fold.
- **Provenance** on every asset carries `AssetId`; the asset-registry tag makes the id searchable
  without loading the asset.
- **Generated, not baked, is a criterion and not a list.** An asset derived from a VtMB unit
  lands under its kind root with an `AssetId`, a producer stamp and a manifest-driven prune;
  `/Game/ElysiumGenerated` holds only assets with **no VtMB unit behind them** — the masters, the
  boot map, the lookdev map, the sky dome mesh, the authored `NS_` base emitters. The namespace
  gets the same discipline: `build_content.py`'s `GENERATORS` is the claim list, and
  `uv run elysium build content` reports and deletes any package no listed generator claims. A
  generator retires in the same task as the assets it authors.
- **Landing and prune are by producer, not by path.** A kind root can hold assets from more
  than one producer (four lanes write into `Models/`; the map bake writes a sprite instance into
  `Materials/` and a sky cube into `Textures/`), so every baked asset carries an
  `ElysiumProducer` registry tag beside `ElysiumRecipe`, stamped by the same `stamp_recipe` call
  every lane already makes and readable off the registry without loading. **A run deletes an
  asset under its prune scope only when that asset's producer is this run's lane and this run's
  manifest neither names nor keeps it.** A foreign or unstamped asset is reported in
  `import_report.json` and never deleted; a partial-cutover manifest sets `pruneScope: null`.
- **Legacy roots are named, each with the task that empties it.** A tracked list beside the
  resolver's kind roots — `Shared/` (the legacy map bake's corpus, R9.2), `Characters/` (R8.2),
  `Props/` (R8.4), `Items/` (R8.3) — receives nothing new, and a registry test asserts that every
  asset on the mount is under a kind root, under `/Game/ElysiumGenerated`, or under a listed
  legacy root. "Beside, not over" was the rule while two producers had to coexist for a bisect;
  under one resolver the roots differ by kind, not by lane, and coexistence is per unit through
  the producer-assignment list of `docs/project/characters_r8.md` → D5.

Adopted 2026-09-04 (`docs/project/characters_r8.md` → D1); the lanes that drift from it —
models (flat `Meshes/SM_<stem>`), maps (at the mount root), sprites, detail instances, sky,
lookdev, the placeholders — move in R8.0a, and every later lane lands on it.
