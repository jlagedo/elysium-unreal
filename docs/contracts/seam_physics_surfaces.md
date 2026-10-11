# Physics surface parser input seam

## Late-bound world material resolver prerequisite (2026-10-11)

Map root schema1.2.0 supplies ordered requested name/atoi-slot declarations and
authored triangle selectors, without adding an exporter SurfaceProps dependency.
FElysiumWorldMaterialMapping retains128 uint16 slots as explicit value state. A fresh
manager starts identity; InstallWorldMaterialTable accepts an existing mapping plus
present/absent input and ordered declarations against this existing typed service.
Absent preserves state; present starts zeroed, ordered duplicate slots last-win,
>=128 is ignored, and unknown names narrow to65535. Empty present tables reset0.
ResolveWorldMaterial selector0 returns the explicit current body row; nonzero maps
and delegates existing GetSurfaceData, retaining first-row special/ordinary-null arms.
No name is described as already resolved by the map producer. Returned rows borrow
table storage and require copied typed values before any future native binding.

Signed ASCII decimal-prefix atoi behavior is preserved, including partial and
nonnumeric tokens. Negative/overflow inputs explicitly refuse atomically; no shipped
108-map witness authors these. The helper does not modify ProcessService map state,
bind a native material, add a clock/save word, or replace the source capsule below.
The required baked association and supported native contact-binding consumer remain
open. Typed raw elasticity2 is retained; older flattened physical asset clamps and
general native response parity are unchanged. See seam_map_map.md and the recovered
world material section in physics-interaction.md for source/consumer contracts.

This unit supplies the actual SurfaceProps001 parser's whole source table. Retail semantics and
layout are owned by `docs/vtmb/physics-interaction.md`, "Independent physics surface material
producer". This boundary owns all input bytes, including inter-entry comments and whitespace,
duplicate entry definitions, repeated keys, original token spelling and the position of `base`.

`uv run elysium export_v2 physics-surfaces-glb` resolves
`scripts/surfaceproperties.txt` UP-first through the install index and publishes
`$ELYSIUM_EXPORT_V2_ROOT/physics-surfaces/table.glb`. Its identity is
`vtmb:physics-surfaces:table`, extension `ELYSIUM_vtmb_physics_surfaces`, schema1.0.0.
The source capsule contains the complete unchanged member with origin/hash. A gapless
`coverage.byteLedger` accounts for every byte as an ordered lexical span. `tokens` are inspection
metadata from the offset lexer; they do not normalize or replace the runtime parser input.
Publication reads the capsule back and compares it to the selected install bytes.
`semantics` independently decodes all authored typed values and last sound names in source
order, with copied defaults/base position and first-entry rules. It is verification metadata,
not an IVP packed memory layout. Initial single-file parsing is its explicit boundary.

`uv run elysium import physics-surfaces` deploys that one capsule unchanged to
`Content/ElysiumCorpus/physics/surfaceproperties.txt`, with the common stamp/hash/read-back
machinery. The lane owns/prunes the dedicated `physics/` corpus directory and rejects any
source member other than the table path. The running game only opens this project-resident
file. The same stamped importer derives `physics/surfaceproperties.semantic.json` from the
unit's semantic metadata for tests. This user-owned data is gitignored; no shipped table/golden
is tracked. `FElysiumEntityWorld` initializes `FElysiumPhysicsSurfaceProps::ProcessService` before
entities exist; the parser copies values/symbols into process-owned memory, then releases the
file input. It reports a missing deployed file with the actual export/import commands.
`Config/DefaultGame.ini` stages this dedicated directory as UFS in packaged builds, so the same
project-content lookup works through Unreal's platform file. No packaged cook was run in this packet.

No named-unit flattening, string re-encoding, recursive inheritance, numeric reformatting or
case fold occurs in the seam. The separate existing `surface-properties` assets still serve
Unreal visual/audio consumers with their older flattened contract. They cannot substitute for
this input; their pool/default/order divergence remains recorded for future consumer work.

The 2026-10-10 compact collision reconstruction removal does not change this authored input
seam or its typed parser/material service. Project-authored parser cases and ignored deployed
semantic expectations remain. Chaos geometry/body adapters must consume the required authored
values; no packed Source/IVP box layout or internal feature numbering is part of this seam.

2026-10-10 final typed-semantic public deployment: source SHA256
`3219d190cfb3eadb65159a4f48373c5ac3bd5f999cb960262abc87a6e09c4570`, GLB SHA256
`4d0accfc9e7edf612ccd5f2ad7e01078567632e0b37ab4825b65792ff85cd016`.
Export report `E:/elysium-work/logs/20261010T235438.709391Z-export-v2-physics-surfaces-glb.json`;
import report `E:/elysium-work/logs/20261010T235628.534573Z-import-physics-surfaces.json`
(one unit, one written/one unchanged file, both read-back verified, zero failures). Semantic JSON
SHA256 `7a208569f8177f18cf530e297014bb49d91aca04c72abdde45a43a58bc25378c`.
Focused publication tests cover
whole-source byte equality through real export/deploy, duplicate/order preservation, missing
source refusal and unowned-member rejection.
