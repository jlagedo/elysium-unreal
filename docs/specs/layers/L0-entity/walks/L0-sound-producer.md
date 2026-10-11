# Selected live SoundEmitter prerequisite — 2026-10-11

This cycle supplies the live selected authored table, explicit resolver-to-native request
boundary and map readiness/lifetime prerequisite. It closes no original package: **23/88
complete, 65 remaining**. Actual impact consumer `vampire.dll 0x1002cd50`, impact dispatcher,
generic EmitSound workers and full L0 remain OPEN. No next packet is part of this checkpoint.

Recovered behavior and addresses are in [audio_pipeline.md](../../../../vtmb/audio_pipeline.md#31-selected-live-soundemitter-source-and-native-lifetime-2026-10-11);
the whole-source capsule contract is [seam_sound_script_data.md](../../../../contracts/seam_sound_script_data.md).
The selected source chain is `0x101b2c60`/`0x101b4240`/`0x101b2e30`, ordered keys
`0x101b3bb0`, scalar producers `0x101b2420`/`0x101b2570`/`0x101b2630`/`0x1012f640`/
`0x101b4900`/`0x101b49f0`, and waves/gender `0x101b3830`/`0x101b3770`/`0x101b3990`.
Settled defaults, FParams, channel parser, resolver, random draws, missing flags and sampled
failure outputs are reused. Every same/lower selected input is real; no synthetic production table.

The first manifest root selects files in authored order; every table root sibling with children
can define a record, with case-insensitive first definition winning across files. Ordered repeated
scalars replace and waves append, including every rndwave child value. Referenced open failure
warns/removes its file record/continues; required manifest open failure is explicit. The settled
KeyValues producer retains open-only success and process cache1, including malformed/empty text.
Reload clears descriptors/files/waves while retaining names, process missing cache and cached
source text. Metadata binds actual chosen text plus raw Size, value-copied synchronously before
parse; new on-disk metadata cannot authorize cached old source text across reload/new GI.

The independent whole raw source seam uses UP-first selection, safe project-only deployment,
ordered declarations/missing references and exact bytes/origins/SHA. Import independently
rederives first-root selection and accepted candidates before writes. Availability is an
exporter-observed installation fact, not authenticated truth: a self-consistent factual flip
cannot be rejected without the installation. Absence requires complete error-free local name
coverage. The optional strict name reader shares the decoder and preserves default index behavior;
zero-offset named rows and empty directories are covered, unknown/unreadable sources fail closed.
This is exact-name coverage, not a general retail filesystem/mountability reconstruction.

`0x101afff0` attempts every eligible authored wave and ignores the return. The bounded missing
callee at `engine.dll 0x2003cf20` takes failed open `0x2003cf88` to size-1 `0x2003cfa5` and normal
return `0x2003cfbf`–`0x2003cfe1`; it does not abort/init-fail. Ten exact absent names produce13
ordered missing attempts. Only source/candidate-bound authored absence plus missing bake is a
terminal warning. Present-but-unbaked, unknown, contradictory binding and native failures block;
all authored waves and independent sampled resolver semantics remain intact. No aliases/filtering.

Unreal owns asset load, precache, mixer and chunk service. Per-GI strong wave references and public
FAudioChunkHandles survive map teardown and release on reload/shutdown; weak host/generation fences
precede callback UObject access. Selected NativeReady is separate from unrelated general Prefetch,
then both compose map readiness. Explicit -nosound permits populated/error-free simulation only,
with native resolve/submit denied. A usable physical device and actual native readiness are required
for the native fixture. Direct typed requests copy values, apply pitch/100 once, preserve caller
owner/placement and use the existing soundlevel attenuation curve. Channel/owner-only remain output
flags; this does not claim generic replacement or recipient semantics.

All retained artifacts below live under
`E:/elysium-work/codex/l0-sound-producer-implementation-20261011/`.
Functional26 freeze: `prejob-v5-lifecycle/sources.json`
SHA dc77770f3949b948061f74d242bdfb18a192b115b6765d3930c78de0b03856ed;
whole diff b3c0127be64455758a40996c0ea53982c1d5e4afd160a99a2d3fb48386635076;
lifecycle public/project API evidence3db2b0120a668c98fae7569caf14d5ea9ba177ab9a385c0ca3f6855a50bc7701.
Only the two NEW fixture files changed after the approved first native branch; runtime/pipeline
and deployed input assumptions stayed frozen, so affected pipeline/export/import receipts are reused.

| gate | measured result | retained receipt SHA256 |
|---|---|---|
| affected pipeline, job11 | 191 pass | 10d814abe9cabeaee9612ec1b10e8cd9aed9dfe7518733713addd37bb5c6e216 |
| public whole-source export, job12 | pass | e58994d1fb4eb6db084dfd583023bc96e1760461f40ecf82199beb62cb0ebaf0 |
| public import, job13 | one unit; one metadata write; two raw members unchanged; zero failed | b939d28e0c0fca56d015cd09a4355c2e0a7ad0596d5d772eee6f6821f2daebce |
| final --arm build, job16 | pass,24.6s | 8c84cc438c05047a38c559877caa353b21d1a540c0ae1ac611b8b74c0056c833 |
| native family, job17 | two Success, zero errors | 11f43035646f26ca0602aecb969a4613e4d6083e4f055c6f8a0e2f8ad96ae511 |
| affected arms/substrate, job18 | 21/21 pass | baab9b2c938a01b7fcd3929b3dcb0fea178238530b4704433de47eb16c9b784b |
| default, job19 | 194/194, zero failed/abstained;171 warnings | 403b3f13ab9739dc861b1a0b12aa6e4af04d567d39f665eb4791e3e5c9bb89ca |
| six settled audio/KV scenarios, job20 | six pass, first_unmet null, one boot; wrapper27 existing startup errors/22 warnings | 7630bff74cf82b783f063f7e7a0d9cb86a18c2339fe40011ecb4c1c5877ea37f |
| doctor, job21 | pass;21 existing warnings | c2ca3a86bd0b11443d7b83ee9e29e3b6fd4e28a957c1fccfa39facfc8fb27f62 |
| doctor --repo-only, job22 | pass;zero warnings | ce6d482c811101274b86c6d0de15c78e45b1da61ca9b04f232ad660a76c3ae13 |

Native191559.823695Z/automation191559.994586Z proves physical WASAPI mixer48000, bound371/13135-byte
sources,48 descriptors/138 waves,13 AuthoredAbsent and125 NativeQueued attempts yielding121 unique
ready assets. All121 actual SoundWaves are compression0/streaming1/chunks2, Done with zeroth and
valid retained chunk1. Actual shipped Concrete.Impact component IsPlaying=true, with gain/pitch/
attenuation/fixed placement and map epoch behavior asserted. First branch proof was reviewed before
expansion, retained `jobs/15-initial-native-availability/branch-proof.json`,
SHAe89064f6ced08922cbec31b0b60978c4434d73e99dbddaca885bf139590a66ef.

Lifecycle proof `jobs/17-native-family-lifecycle/lifecycle-proof.json`,
SHAbeff98870f2637999b4db47f71498ae0e648cb276c3c87dcae16184e0e1dc822, observes actual121
pending before reload/shutdown, generation1→2→3,121 ready retained handles, release, copied requests
after reload, and selected resolve/play during unrelated pending general Prefetch. All three voices
are playing immediately before owner cancellation/epoch retirement; exact OwnerCanceled then
MapEpochRetired events leave the unrelated voice playing until its own OwnerCanceled. Delegate
subscription is removed before teardown. Public adapter state has no host/native rows/descriptors/
voices after shutdown and three callback frames. These are ownership/state observations, not proof
of unseen callback invocation, engine-global eviction or audible/waveform parity.

Source arms cover OrderedPopulationReload, ScalarGenderBounds, SymbolsFlagsAndOrdering,
SelectedTextObserver, AvailabilityBindingAndOutcomes, and real -nosound OutputDisabledHost. They
preserve process-cache semantics, nested cold-cache fixture ownership, immutable copied outputs,
malformed-open success, first-root selection, candidate binding, missing metadata, present-unbaked/
unknown failure, safe bounds, duplicate skip and reload name identity. Pipeline controls cover
exact raw bytes/hashes, independent forged structural binding/selection rejection, path safety,
per-unit failure isolation, readback/idempotency and unchanged existing lanes/default launch args.

Every failed trial remains retained before correction: unity-visible NEW fixture constant collision;
unsupported Win64 fixture hash API; fixture GI outer/invalid SetAudioDevice; and the initial global
readiness failure on genuine authored absence. Reviewed corrections preserve existing valid tests
and source SHA expectations. The last required production correction uses explicit validated
availability and the recovered nonfatal missing arm; it is not a blanket missing-bake exemption.

Generic attenuation x87 runtime precision remains UNRECOVERED (selected table has no attenuation),
and unselected nonfile native mapping stays OPEN. This cycle does not prove generic EmitSound,
Source channel/recipient policy, indexed shared-default consumer, impact dispatcher, full-map
audio parity, packaged cook, or acoustic parity. No audit/hooks/retail ledger, gamecontent or old
golden changes belong to this checkpoint. Stop after this sound cycle; no impact implementation.
