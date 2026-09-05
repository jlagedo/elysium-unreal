# Corpus-index GLB seam

This document defines the one product that turns "every VtMB asset is decoded" from a policy
into a check: a scene-less binary glTF 2.0 unit that lists every member of the merged install,
states which unit owns it or why nothing does, and carries the whole cross-unit reference
graph. Shared rules are owned by `seam_map_unit_contract.md`; the install search order by
`docs/vtmb/vpk_format.md`.

## Unit identity

```text
<VTMB>/Vampire/, <VTMB>/Vampire/pack*.vpk, <VTMB>/Unofficial_Patch/
  -> vtmb:corpus-index
  -> $ELYSIUM_EXPORT_V2_ROOT/index.glb
```

There is exactly one corpus index per export root. It is written last by `export-all`, after
every other kind, and rewritten by any single-unit command so that its `units[]` row for that
unit is current.

```text
uv run elysium export_v2 corpus-index-glb
uv run elysium export_v2 export-all
```

## Install walk

The index is built from a complete UP-first walk: every file below `<VTMB>/Vampire/` and
`<VTMB>/Unofficial_Patch/`, and every member of every `pack*.vpk`, keyed by lower-case,
forward-slashed, install-relative path with loose members shadowing VPK members and the patch
tree shadowing the retail tree. It walks every directory, not the asset subset the map and
character bakes index, because a member no seam looks at is exactly what it exists to find.

Only two things are left out, and both are named in `excludedTrees[]` with their reason: the
VPK container files themselves, which are the source of their members and not members; and the
loose `maps/graphs/` and `maps/soundcache/` trees, which the retail engine writes while it runs.
The VPK-shipped `maps/graphs/*.ain` and `*.loc` members are not excluded; they are the nav-graph
seam's.

`sourceResolution.members[]` lists the install roots and every VPK container with its byte
length and SHA-256, so the index states which install it describes.

## GLB structure

```json
{
  "extensionsUsed": ["ELYSIUM_vtmb_corpus_index"],
  "extensionsRequired": ["ELYSIUM_vtmb_corpus_index"],
  "extensions": {
    "ELYSIUM_vtmb_corpus_index": {
      "schemaVersion": "1.0.0",
      "identity": {"asset": "vtmb:corpus-index", "exportRoot": "…", "sourcePolicy": "up-first"},
      "sourceResolution": {},
      "excludedTrees": [],
      "members": [],
      "units": [],
      "references": [],
      "inverse": {},
      "danglingReferences": [],
      "orphans": [],
      "crossUnitChecks": [],
      "census": {},
      "summary": {},
      "dependencies": [],
      "coverage": {}
    }
  }
}
```

The unit is scene-less with no BIN chunk, and it carries no byte ledger: it is a product over
other products, and every byte it describes is ledgered by the unit that owns it.

## Members

One `members[]` row per winning member:

| Field | Meaning |
|---|---|
| `path` | the install-relative key |
| `origin` | the winning member's origin |
| `byteLength`, `sha256` | the winning bytes |
| `shadowed[]` | every losing source for the same path — origin, byte length, SHA-256 — so retail-versus-patch divergence is a query rather than a re-walk |
| `disposition` | one of the states below |
| `asset` | the owning unit for `unit` and `companion` |
| `embedded[]` | for a BSP: the PAKFILE members it carries, each with the unit it became |
| `evidence` | for `residue`: the category and the fact that proves the engine never reads it |

| Disposition | Meaning |
|---|---|
| `unit` | the member selects a unit; `asset` names it |
| `companion` | the member is owned by another member's unit — a VTX or PHY by its model, a TTZ by its TTH, a LIP by its sound, a font page by its font, an expression TXT by its VFE |
| `residue` | evidence-backed: no shipped code path reads the member |
| `unclaimed` | no seam claims the member |

PAKFILE members are not install members; they appear under their map's `embedded[]` with their
`bsp-pakfile` origin and the material or texture unit each became. Claiming an embedded member
needs no selecting install member: the texture and material seams' own `source_keys()`
enumerate every BSP's PAKFILE `.tth`/`.ttz` and `.vmt` (SF-1.3/SF-1.4), and the walk's claim
table (`walk.py::_claims`) records the asset every such key resolves to whether or not that key
also selects a member below `materials/` on disk. A `.vmt`/`.tth`/`.ttz` embedded member's
`asset` is therefore null only when the corpus genuinely publishes no such unit; export-time
validation still requires that unit to exist on disk (`embedded[].asset` must name a published
unit, the same rule a `unit`/`companion` member's `asset` is held to). A member of any other
kind a BSP happens to pack is not routed by the map seam at all and stays `asset: null`.

### Residue categories

| Category | Members | Evidence |
|---|---|---|
| `authoring-leftover` | `sound/**/*.sfk` (11), `sound/**/*.pk` (2), `models/**/cmdseq.wc` (2), `models/bad_models.txt` and two nested copies, 22 `models/**/<name>.txt` texture-compiler configs, 8 `materials/**/*.vmt.txt`, `scripts/liblist.gam~`, `python/warehouse/warehouse.old`, `scripts/hl2_scripts.dsp`, `vdata/system/stealth.xls` | tool-owned formats or dead extensions the engine's loaders do not compose |
| `unreachable-member` | `unpacked 0.74/shovelhead/*` (3), `models/**/*.vmt` (3) | the model and material path rules cannot produce the path |
| `engine-binary` | `dlls/*.dll`, `cl_dlls/*.dll`, `dlls/vampire.dll.12` | code, not content |
| `user-data` | `save/*.sav` (27), `logs/console.log` | written by a play session |
| `foreign-file` | `cfg/elysium_*.cfg`, `cfg/dummy.txt` | not shipped by retail or the patch; placed by this project |
| `excluded-by-decision` | `media/*.bik` (4) | the owner call that the logo videos are not exported; size and hash only |

A residue row still carries the member's length and hash; residue is a classification, not an
omission from the index.

### The guarantee

`summary.unclaimed` is the count of `unclaimed` members. The corpus export **fails while it is
non-zero**, printing every unclaimed path. A new file type in a future install, a seam whose
selection rule misses a spelling, or a residue category applied without evidence therefore
surfaces as a failed export rather than a silently smaller corpus. That is the mechanical form
of "no data left undecoded": every member is a unit, a companion of a unit, or residue with a
stated reason.

## Units

One `units[]` row per published unit: `asset`, `kind`, `path` below the export root, `schemaVersion`,
`byteLength`, `sha256`, `warnings` (count and the first reason of each), `dependencyCount`,
`unresolvedCount`, `unsupportedCount`. A unit file present under the export root with no row, or
a row whose file is missing or whose hash differs, fails the index.

## References

`references[]` is every `dependencies` row of every unit as an edge: `from`, `role`, `to`,
`sourcePath`, `resolved`, and `parameter` where the row carries one -- the VMT key a material's
texture binding was read from, published only when set (a material unit already writes
`parameter` on its texture dependency rows; SF-1.5 carries it through to the edge). `inverse` is
the same graph keyed by target, and it is what fills the fields no unit can write about itself:

| Written into | From |
|---|---|
| `vtmb:model:` `identity.roles` | `model` edges from maps, entities, vdata items, scenes |
| `vtmb:sound:` `identity.referencedBy` | `sound` edges from scenes, schemes, surface properties, sound scripts, entities |
| `vtmb:expression-table:` `selectedBy[]` | `expression-table` edges from models and scenes |
| `vtmb:shader-program:` `selectedBy[]` | `shaderResolution.programs` of materials |

The index writes those fields into the target units' JSON chunks as its last step and re-hashes
them; a unit's own export leaves them empty.

`danglingReferences[]` groups every `resolved: false` edge by role with the referrer and the
authored path. `orphans[]` lists, by kind, every unit no edge targets — a material nothing
binds, a sound nothing plays, a model nothing places — because an orphan is either dead content
or a missing seam rule, and both are worth a query.

## Cross-unit checks

`crossUnitChecks[]` carries the properties no single unit can validate, one row per check with
`name`, `passed`, `failures[]`:

| Check | Property |
|---|---|
| `surface-property-inheritance` | every `base` chain terminates and is acyclic |
| `model-include-tree` | every include edge resolves and the tree is acyclic |
| `scene-expression-rows` | every `expression` event names a row its table carries |
| `surface-sound-scripts` | every `impact`/`scrape` script name is an entry of a sound-script unit |
| `nav-graph-stamp` | each `.loc` value agrees with its map's `mapRevision` or the documented alternative |
| `font-list` | every `fontlist.txt` row matches a font unit |
| `dialogue-line-audio` | every dialogue line whose audio the convention names resolves to a sound unit, `.mp3`-first |
| `map-partition` | each map's four ledgers together claim every BSP byte once |
| `texture-material-roles` | every texture unit is bound by at least one material, is a map's own reflection-probe placement, or is an orphan |
| `map-references-published` | every asset a map root names in a *resolved* `textures[]`/`cubemaps[]` row, or in `pakfile.entries[].unit`, is a unit the corpus actually publishes |

A failed check fails the corpus export like an unclaimed member does.

`map-references-published` (SF-1.5, `graph.unpublished_map_references`) is the corpus-level half
of a map's own resolution: a map unit's `resolved` flag on a dependency row only states whether
the *install* holds the member a texture or cubemap names, because the map's own export has no
view of the published corpus. The index has the whole `units[]` table in hand, so this check asks
the question the map unit cannot ask itself -- whether the corpus went on to publish a unit for
that asset -- and fails a map that names one it does not, most usefully a patched material or
reflection probe SF-1.3/1.4 export but the plural texture/material commands have not (yet) been
rerun to publish. Exactly like `dialogue-line-audio`, an asset the *install* itself does not
resolve is left to `danglingReferences[]` rather than reported here: a `textures[]` row names
every TEXDATA name a level compiled with, whether or not its `.vmt` shipped (a `tools/*`
compile-only material commonly does not), and a `cubemaps[]` row can carry `resolved: false` for
a sample the map compiler placed but never baked. Only rows the map's own decode already marks
resolved are checked; `pakfile.entries[].unit` carries no such flag because a PAKFILE entry is,
by construction, bytes the BSP's own zip actually holds.

`texture-material-roles` accepts a map's own `cubemaps[]` binding (`role: texture`, source kind
`map`) as well as a material's: the owner call "Baked reflection probes are not reflection
content" (`seam_migration.md`, 2026-08-31) is that a probe's pixels are never sampled by a
surface, only its origin placed as a capture, so the 1,325 SF-1.3 probe texture units are
legitimately bound only by the map that places them and never by a material.

## Census

`census.byExtension[]` is one row per extension with counts and byte totals per origin kind
(VPK, retail loose, patch loose); `census.byKind[]` one row per unit kind with unit count, byte
total and warning count; `census.byDisposition` the member totals. This is the table the seams
were designed against, restated from the walk that built the index.

## Summary

`summary` is what one line of a report needs. Besides `unclaimed` ("The guarantee", above) and
the other per-disposition and per-check totals, it carries the PAKFILE embedding gap: every
BSP's `embedded[]` rows are members of no seam's own count, so they get their own three fields.
`embeddedMembers` is the total row count over every map's `embedded[]`; `embeddedUnclaimed` is
how many of those rows carry `asset: null` — no seam claims the PAKFILE member yet;
`embeddedUnclaimedByExtension` breaks the unclaimed count down by the member's extension. `uv run
elysium doctor` reports `embeddedUnclaimed` as a warning, not a failure — unlike
`summary.unclaimed`, an unclaimed PAKFILE member does not fail the corpus export
(`seam_migration.md` → "Plan — surfaces track" → SF-1.1). SF-1.5 is the task that drives
`embeddedUnclaimed` to zero: the texture and material seams' `source_keys()` now claim every
`.vmt`/`.tth`/`.ttz` PAKFILE key regardless of whether it also selects an install member, so
`embeddedUnclaimed` is 0 whenever the plural texture and material exports have published every
key those two seams enumerate. Only a member of a kind the map seam's own PAKFILE routing does
not recognise at all (today never seen in a real install; `.vmt`/`.tth`/`.ttz` are the only
extensions the 108 BSPs pack) can still leave a row unclaimed.

## Dependencies

The index declares one `corpus-unit` dependency per unit, pinning kind, path and hash; that is
the same list as `units[]` in the contract's row shape, so a reader that only understands the
contract still sees the whole corpus.

## Coverage and validation

`coverage` grades the walk rather than bytes: `mapped[]` names the sections, and
`unresolved[]` is the unclaimed member list — a complete index has none. Export-time validation
rebuilds the member table from the install a second time and compares path, origin, length and
hash per row, re-hashes every unit file against its `units[]` row, and checks that every
`references[]` edge appears in exactly one unit's `dependencies` and that `inverse` is its
transpose. Standalone validation checks the scene-less rule, the absence of a BIN chunk, the
identity, and the internal consistency of `members[]`, `units[]`, `references[]` and `summary`.


### Named source-policy exceptions

The member table always records the actual patch-first install winner and hashes its shadowed
sources. A unit may select a shadowed source only through the exact `(asset id, source path)`
registry in `formats/unit_contract/source_policy.py`; the member then carries `unitSourcePolicy`.
The existing water ruling N.3 selects the retail VPK `particles/waterbigsplash_emitter.txt` and
leaves the patch stub as the recorded install winner. Reconciliation and the independent index
validator compare the unit's origin, byte length and digest to that measured VPK source. An
unregistered unit, a different member path, or a wrong digest still fails. The particle unit's
`sourceResolution.overridePolicy` records the same decision. This is source selection, never
an exemption from source-byte validation.
