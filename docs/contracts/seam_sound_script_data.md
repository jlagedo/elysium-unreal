# Selected live sound-script source seam

`uv run elysium export_v2 sound-script-data-glb` publishes
`$ELYSIUM_EXPORT_V2_ROOT/sound-script-data/manifest.glb`; identity
`vtmb:sound-script-data:manifest`, required extension `ELYSIUM_vtmb_sound_script_data`,
schema1.0.0. `uv run elysium import sound-script-data` owns only
`Content/ElysiumCorpus/audio/scripts/`, mapping `scripts/<relative>` there. This is distinct
from the Python script corpus. Existing per-entry sound-script exports remain unchanged.

The unit retains the whole selected raw manifest and each unique selected existing table as
separate source capsules with original bytes/origin/SHA256. UP-first member resolution selects
the source. The manifest is required; failure to locate it refuses export. Ordered declarations
include original spelling, precache/declare flags, unknown keys and selected missing references.
Only children of the first manifest root select files; dormant tables are not included. A missing
referenced table stays a declaration, with no fabricated source member. Repeated references remain
ordered while the capsule member set is unique. Raw table roots/duplicates/comments/whitespace are
retained, never concatenated per-entry decoded payloads. Lexical/byte-ledger fields are inspection
metadata; runtime parses the raw selected files using its settled LoadFile producer.

`sourceBindings` records each unique source's path, SHA256, byteLength and bytesBase64.
`waveAvailability` records each accepted entry/wave ordinal, authored name/value, gender category,
source path/hash, filesystemPath and availability. Candidate derivation reproduces first-definition
case-insensitive dedup across all selected table root siblings, ordered repeated wave/rndwave
values, male1/female2 gender expansion, and the resolver's maximum two leading sound prefixes.
The separate projection uses the settled loader's root/typed-value grammar, including unquoted
number GetString conversion. Empty/leading-! candidates have null filesystemPath and nonfile status.
No candidate or duplicate wave is filtered from the authored descriptor table.

Availability is `present`, `absent`, `unknown` or `nonfile`. Only present carries a supported
loose/VPK origin. `availabilityCoverage` includes complete, issues, pack directory observations
and loose-root observations. The strict OPTIONAL name directory reader shares the existing row
decoder and leaves ordinary index behavior unchanged. Current coverage includes canonical9-byte
footer directories, zero-offset named rows and empty directories. Unsupported/unreadable/partial
pack names or unreadable/link-skipped loose trees produce coverage issues. Known present names
remain present; a missing name becomes absent only when every relevant source was covered without
errors, otherwise unknown. No payload extraction or general retail mountability claim follows.

Presence/absence is an exporter-observed installation fact like source origin. Import cannot
authenticate a self-consistent factual flip without the original installation; this is not a
signature or authentication boundary. It independently rederives declarations/selection/missing
references from raw manifest bytes, then candidates and source bindings from verified capsules.
Invalid ordinal/order/path/gender/name/source/hash/byte closure, contradictory status/origin,
incomplete-coverage absence or malformed members fail that unit before writes. Other units retain
the common deployer's fail-isolation, safe paths, read-back, stamp/idempotency and owned pruning.

Recipe `elysium-sound-script-data-v2` deploys `_source_provenance.json` with sourceResolution,
declarations, sourceBindings, waveAvailability and availabilityCoverage. Required input currently
has manifest371 bytes SHAea09cd8ce3bfb9a5d5ab31be40ee78e31854de15e91d7bf1ed9f01ce5b236f02
(pack001 offset896688); UP selected table13135 bytes
SHAe5de11ef6a648b45cb2196af15aaa47bd3d3a989b14f1bb7986430f692019eb6.

Runtime opens only project corpus/baked content. Cache1 source text survives reload/new GIs;
the optional default-empty synchronous FKvLoadArgs selected-text observer receives the actual
chosen FString and raw Size before parsing, and the host value-copies both. Active source/member
and ordered candidate binding must match deployed metadata exactly; a changed on-disk table's
new metadata cannot bless cached old text. There is no private metadata-cache clone or cache bypass.

Only validated authored absence plus a missing baked asset is a terminal warning for native
precache. Unknown, present-but-unbaked, source mismatch, contradiction and native failure block
readiness. Every authored attempt stays ordered; existing sampled resolver failure outputs,
descriptor missing flags and process missing cache remain independent. Nonfile native mapping
is unselected and OPEN. Public ResolveSoundScript/SubmitResolvedScript grants native permission
only after selected preparation, copies wave/volume/pitch/level into the existing typed API and
preserves caller owner/placement. Native chunk ownership is per GI, survives map epoch retirement,
and releases on reload/shutdown. No table serialization, global UObject host or install path opens.

Proof: 191 affected pipeline tests; raw export/import readback; six source arms; initial native
and lifecycle fixtures. Detailed frozen receipts and limitations are in
`docs/specs/layers/L0-entity/walks/L0-sound-producer.md`. This prerequisite leaves actual impact
consumer `0x1002cd50`, generic EmitSound workers and full L0 OPEN.
