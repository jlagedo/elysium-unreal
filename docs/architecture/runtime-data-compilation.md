# Runtime data compilation — gap report

This document owns the representation boundary between offline exported intermediates and the
immutable data the runtime consumes. It is an architecture assessment, not a status tracker or an
implementation archive. Source remains the as-built record; sequencing and task status belong only
in `docs/project/roadmap.md`.

VtMB format and behaviour facts remain in `docs/vtmb/`. The baked-world contract remains in
`docs/architecture/uasset-bake-spike.md`; the entity object model remains in
`docs/architecture/engine-core.md`; map activation remains in
`docs/architecture/map-architecture.md`.

---

## Executive conclusion

**Runtime-built does not require runtime JSON parsing.** Elysium keeps JSON as a readable,
engine-neutral export and inspection product, then compiles immutable runtime data offline into the
representation that owns it:

| Data kind | Runtime representation |
|---|---|
| Engine-neutral map and gameplay definitions | a versioned, bounds-checked binary runtime pack |
| Metadata whose purpose is to locate or annotate baked Unreal assets | partitioned generated `UDataAsset` objects or metadata on the owned asset |
| VtMB scripts and files the embedded script environment may address | loose files in the virtual script filesystem |
| Development surveys, captures and diagnostic output | JSON or another human-readable format |

The compiled form changes storage and loading only. Entity identity, authored rules and mutable
state remain in the plain-C++ substrate. The map pack produces `FElysiumEntityDefs`; it does not
produce live `UObject` entities or bake away their addressability.

This direction is a contract proposal. No runtime speedup is treated as confirmed until focused
instrumentation demonstrates one.

---

## Confidence language

| Label | Meaning |
|---|---|
| **Confirmed** | Directly established by the named source path or current export contract |
| **Observed** | Measured or inspected in one configured local corpus; useful evidence, not a stable project invariant |
| **Proposed** | The recommended target contract; not an implementation or roadmap status claim |
| **Rejected** | Considered and unsuitable as the default architecture |

---

## 1. Current evidence surface

The table names only the load boundary needed to understand the gap. The source owns the complete
field-level behaviour.

| Input family | Runtime evidence | Classification |
|---|---|---|
| `<map>.ents` | `FElysiumEntityDefs::Parse` loads and deserializes the whole JSON document; `AElysiumMapActor` invokes it synchronously before level-script import and entity-world construction (`Source/ElysiumUE/Private/Substrate/ElysiumEntityDefs.cpp`, `Source/ElysiumUE/Private/Map/ElysiumMapActorLifecycle.cpp`) | **Confirmed** |
| `npc/npc_index.json`, `npc/clips/*.json`, blend, facial, eye and procedural sidecars | `UElysiumAnimSubsystem` loads the index once and lazily loads and caches per-owner metadata while resolving baked skeletal assets (`Source/ElysiumUE/Private/Visual/ElysiumAnimSubsystem.cpp`) | **Confirmed** |
| `audio/catalog.json` | `UElysiumAudioSubsystem::BeginCatalogLoad` parses the complete document on a worker, checks only the root version, discards the parsed tree and reports readiness (`Source/ElysiumUE/Private/Audio/ElysiumAudioSubsystem.cpp`) | **Confirmed** |
| `items/ground_models.json`, `ui/strings.json`, `sound/usable/soundgroups.json` (`hud/use_icons.json` retired at R6.6) | Small immutable lookup manifests are parsed independently by their owning runtime systems | **Confirmed** |
| `_lights/<map>.json` | The light-survey tool writes and reapplies development calibration state; this is editable diagnostic input rather than a shipping gameplay contract (`Source/ElysiumUE/Private/Visual/ElysiumLightRig.cpp`) | **Confirmed** |
| Configured local export corpus | Character metadata is the largest JSON family; `.ents` documents are comparatively small, and their isolated contribution to map activation has not been benchmarked | **Observed** |

The repository already proves both target mechanisms:

- `/ElysiumBaked/Items/DA_WieldModels` is a generated typed `UDataAsset`, loaded once and rooted by
  `UElysiumWieldTable` (`Source/ElysiumUE/Public/ElysiumWieldTable.h`,
  `Source/ElysiumUE/Private/Visual/ElysiumWieldTable.cpp`).
- `.eskm` is an engine-neutral binary container with explicit magic and version checks plus a
  bounds-checked C++ reader (`pipeline/src/elysium_pipeline/exporters/UE_mdl_skeletal.py`,
  `Source/ElysiumUE/Private/Visual/ElysiumSkeletalSource.cpp`).

Neither precedent alone is the answer for every file. They establish that the repository can use a
typed Unreal asset where Unreal owns the references and a compact binary container where plain C++
owns the data.

---

## 2. Confirmed gaps

### G1 — Export JSON is also the production load contract

JSON is useful at the pipeline seam because it is inspectable, diffable and easy to validate. At
runtime it requires text decoding, a generic object tree, repeated field-name lookups and a second
conversion into the typed structures the game actually uses. The runtime also inherits each
exporter's ad hoc schema checks.

The missing boundary is a deterministic compilation step between **intermediate** and **runtime
product**. The JSON remains available for inspection and focused semantic tests; Shipping does not
need to parse it.

### G2 — Map-definition parsing sits on the activation path

`AElysiumMapActor` parses `.ents` before importing the level script, constructing
`FElysiumEntityWorld` and running the spawn pass. The parser is therefore part of synchronous map
activation even though no gameplay rule requires JSON or requires the work to happen on the game
thread.

This is a structural gap, not yet a demonstrated performance defect. The gameplay plan correctly
keeps an `.ents` cook-cache conditional on parse time mattering. Measurement must separate file
read, JSON deserialization, typed conversion, entity-world load and spawn before attributing a
visible hitch to this parser.

### G3 — Runtime products have no uniform stale-data identity

The `.ents` root has no runtime-container magic, section directory or source digest. Individual
JSON families use their own version fields and failure shapes. A valid but stale file can therefore
be structurally accepted without proving that it belongs to the export inputs expected by the
running build.

The bake's per-asset recipe stamps solve this for baked packages. `.ents`, `.hulls`, `.dispcol`
and `.ropes` now carry a whole-file digest in the level recipe too (R2.3/MP-1.3), but only so a
touched sidecar dirties the `.umap` the bake writes -- the bake still does not parse their fields
into any baked actor, and the digest is invisible to the running game. The runtime side of this
gap is unchanged: a separate runtime-data receipt and source digest are still needed for the
build the game itself loads, distinct from the bake's own invalidation domain.

### G4 — Some metadata is separated from the Unreal asset that gives it meaning

Clip selection, blend grids, layer/combat annotations, facial data, eye data and procedural rules
are resolved beside baked skeletal meshes, animation sequences and blend spaces. The metadata is
partitioned and cached, but its load contract remains a parallel loose JSON tree.

Where a value exists only to locate or annotate a baked Unreal object, the package is the natural
ownership boundary. This does not apply to authored gameplay rules merely because they mention an
animation label: those rules remain typed plain-C++ data, while the package supplies the asset and
asset-local descriptors needed to play it.

### G5 — The audio catalog pays for data it does not consume

The audio subsystem currently deserializes `audio/catalog.json`, validates only `version == 1`, and
discards the root. Direct sound-mirror resolution supplies playback. Until catalog rows have a
runtime consumer, a small version/digest/count receipt proves the required export contract without
building the large JSON tree.

### G6 — Required-versus-optional failure policy is not uniform

Some readers log malformed input; some return failure or an empty result without owning a warning.
For example, `.ents` logs malformed JSON but a missing file or missing `entities` array returns
false silently, while the HUD icon-atlas loader quietly returns on file or parse failure
(`Source/ElysiumUE/Private/Substrate/ElysiumEntityDefs.cpp`,
`Source/ElysiumUE/Private/UI/ElysiumHUDWidget.cpp`).

Each compiled product must declare whether absence is optional. A missing, corrupt, unsupported or
stale required product emits one warning at the owning load boundary with the map/owner, path,
expected version or digest, and the focused export command that repairs it. Callers receive a
structured failure instead of duplicating the warning.

### G7 — Performance evidence is too coarse to justify a blanket conversion

The map lifecycle reports a broad entity phase, not separate read/deserialize/convert/spawn costs.
The animation metadata is lazy and cached, so corpus size alone does not describe a frame or
startup cost. Replacing all JSON with reflected assets before measuring risks paying commandlet,
package-count and reference-loading costs without improving the player-visible path.

---

## 3. Target contracts

### 3.1 Engine-neutral runtime pack

**Proposed:** the offline pipeline compiles immutable map-side inputs into one per-map runtime pack.
The first focused version may contain `.ents` alone; the container is extensible to hulls,
displacement collision and ropes only when measurement or packaging justifies co-location.

The header carries at least:

- a fixed magic and endian declaration;
- a container schema version and the reader contract version;
- the source semantic digest and producer version;
- a section directory of type, offset, byte length and record count;
- total-size and checksum fields.

Sections use bounded integer counts, offsets and indices into an interned UTF-8 string table. The
C++ cursor checks every span and multiplication before exposing records. Unsupported versions,
truncation, bad offsets, count overflow, checksum failure and source-digest mismatch are distinct
failures.

The reader fills the existing typed plain-C++ objects. It performs no Source-to-Unreal coordinate
conversion, applies no VtMB frame rule and creates no live entity. Those questions are already
settled before the runtime product is written.

The pack is a separate pipeline/cache stage rather than an Unreal visual-bake stage. This preserves
the existing guarantee that changing `.ents` does not launch an editor commandlet or invalidate the
world-look packages. The packaged-content design may later stage the pack beside the executable or
wrap the unchanged payload in a generated package; the payload contract does not depend on that
distribution choice.

### 3.2 Generated Unreal metadata

**Proposed:** data that exists to resolve or describe a baked Unreal object is authored into a
generated typed asset during the owning asset bake.

- Character metadata stays partitioned per character, shared bank or other independently requested
  owner. One global asset with hard references to the whole cast is prohibited.
- References to meshes, sequences, blend spaces, textures and materials are soft unless the caller
  intentionally loads that complete dependency graph.
- Strictly asset-local values may be attached to the owned asset; cross-asset vocabularies and
  include relationships live in a small catalog `UDataAsset`.
- The bake reads the saved object back and verifies semantic counts, identifiers, references and
  the source digest before promoting its receipt.

`items/ground_models.json` and the use-icon atlas metadata fit the same model because their purpose
is to bind authored identifiers to baked assets. UI strings may use a generated typed asset while
remaining game-derived and gitignored. A sound-group table that names loose audio bytes instead
belongs in a small engine-neutral table unless its owner changes to baked `USoundWave` assets.

Unreal's relevant contracts are [Data Assets](https://dev.epicgames.com/documentation/en-us/unreal-engine/data-assets-in-unreal-engine),
the [Asset Manager](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine),
and [asynchronous asset loading](https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine).

### 3.3 Intentionally loose data

Loose files remain correct where loose addressability is part of the product:

- VtMB level and gameplay scripts loaded by the embedded CPython VM;
- files those scripts may open through the virtual filesystem;
- encoded audio bytes decoded or streamed by the runtime;
- development calibration, capture and inspection artifacts.

Native consumers may receive a compiled index over those bytes. The loose source still exists when
script compatibility or runtime streaming requires it.

---

## 4. Rejected defaults

| Alternative | Decision | Reason |
|---|---|---|
| Convert every JSON file to `UDataAsset` | **Rejected** | It couples engine-neutral gameplay data to Unreal reflection and makes runtime-only changes participate in commandlet/package invalidation |
| Use `UDataTable` for `.ents` | **Rejected** | The entity document contains nested, ordered outputs and heterogeneous raw keyfields rather than one flat row schema |
| Put all metadata in one primary asset | **Rejected** | A monolith undermines lazy character loading and can pull a large hard-reference graph into memory |
| Use Data Registry as the universal store | **Rejected** | It adds a registry/cache abstraction without solving the map-pack ownership problem; no current requirement needs it |
| Fall back to JSON in Shipping | **Rejected** | Fallback hides missing, stale and corrupt compiled products and permits two runtime contracts to diverge |
| Fold runtime sidecars into the visual bake fingerprint | **Rejected** | It turns entity-only iteration into an Unreal commandlet operation and invalidates a package domain whose visual bytes did not change |

A development-only semantic verifier may read both forms and compare them. That is a test path, not
a second gameplay load path.

---

## 5. Focused implementation and acceptance shape

If this gap is promoted to roadmap work, the smallest proving sequence is:

1. Instrument `.ents` file read, JSON deserialization, typed conversion, entity-world load and
   spawn separately. Instrument the first NPC-index and per-owner metadata loads. Record the audio
   catalog worker duration and peak temporary allocation.
2. Replace the audio catalog's version-only parse with a deterministic small receipt. This proves
   the producer/consumer digest contract without changing playback.
3. Compile one owner-named map's `.ents` into the runtime pack and load it into the existing
   `FElysiumEntityDefs`. The report does not select the map because that would duplicate sequencing
   owned by the roadmap.
4. Move one independently baked character owner's clip/asset metadata into a partitioned generated
   asset. Do not widen to the character corpus until this slice proves loading and invalidation.

The map-pack slice passes only when all of these gates hold:

- **Semantic parity:** map name, entity count and order, every raw key/value, output order and
  delay, field-6 Python, level-script module, brush/hull data and sky-adjusted values match the JSON
  reference.
- **Determinism:** identical semantic input produces byte-identical pack and receipt output.
- **Failure visibility:** missing, stale, unsupported, truncated and corrupt fixtures each produce
  one contextual warning and a structured load failure; no Shipping fallback runs.
- **Fast QA:** producing the focused pack launches no Unreal commandlet and does not invalidate a
  visual bake stage.
- **Measured effect:** before/after results report isolated parsing cost and total map activation;
  an unmeasured speedup is not a gain claim.
- **Packaging independence:** the reader accepts the same payload whether the packaging task stages
  it loose or stores it inside a generated package.

The character slice additionally proves soft-reference dependency closure, per-owner lazy loading,
saved-asset readback and semantic equality with the exported metadata. Headless tests establish the
contracts; live map and character acceptance remains a separate gate.

---

## 6. Owner calls still required

The architecture does not pre-empt these choices:

- whether the first map container compiles only `.ents` or co-locates other runtime sidecars;
- which map is the first focused runtime-pack slice;
- whether packaged distribution stages raw packs or wraps them in generated packages;
- which character owner is the first metadata-asset slice;
- whether developer builds retain an explicit JSON-versus-compiled comparison command after the
  migration tests are established.

None of those choices changes the governing boundary: readable export intermediates are compiled
offline, plain C++ continues to own game identity and rules, and the runtime consumes one validated
representation for each required product.
