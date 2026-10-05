# Packet S14 — V4d verification and final placement (2026-10-04)

Read-only verifier/planner except the three authorized planning files. AGENTS.md was read
first. No source, Arena/, tracker, research/ or existing recovery doc was edited; no build,
bake, test, arena, game or commit. V4c is mid-integration: port findings below are snapshots,
not a claim that its final binary has been exercised. Acceptance statements are predictions.

The scout at `E:/elysium-work/codex/scout-V4d/packet.md` is a lead. **Verified** means checked
against the named file/data or retail decompilation/listing here; **refuted** identifies a
contradiction or an overclaim. Inferred/measured-before-this-session results are separated.
Lookups began with vtmb_where for the death/corpse/selector/removal addresses, which returned
ledger rows and indexed headings. Functions below were then read with vtmb_code; the bone,
state 7 and burn-sound sites were also read in vtmb_asm. Modules are vampire.dll unless stated.
PowerShell/Python query subprocesses had 60 s timeouts; MCP batches used watchdogs. Individual
assembly/grep calls returned immediately; their explicit watchdog wrapper was omitted.
No query reached 10 s. Large sidecars/GLB were memory-mapped and
searched for bounded keys/records, never printed or parsed whole. Two bounded GLB attempts
failed to decode the selected slice/type; narrower properties/constraint/bone lookups succeeded.

## 1. Death admission — verified, and the spike reconciled

| Scout claim used | Verdict and independently checked evidence |
|---|---|
| synchronous ordinary kill, OnDeath before corpse, same entity | **Verified.** Code for 0x102bf340 calls 0x10265ad0 then Troika cleanup/MarkAsDead. 0x10265ad0 has the freeze return and started-script deferral, then DeathSound/0x10265a90 before 0x1032b9b0, which writes lifeState 1 and dispatches slot 301. 0x1032c0e0's ordinary arm calls BecomeClientRagdoll, then slot 137 returns its corpse identity. |
| hitbox bone or Spine2, never deliberately the state 7 seed | **Verified as the algorithm, not guaranteed success of every model lookup.** CreateCorpse assembly 0x1032c1e4..0x1032c22b: hitbox index≥0 resolves the bone word, otherwise LookupBone("Bip01 Spine2") at 0x1032c226; call 0x1032c29c passes that result. A missing named bone can still produce lookup failure; do not invent a valid ordinal. regular_cop's MDL bone list contains Spine2, source index 5. |
| no-rig Become only zeroes bounds; rig seed only for bone−1 | **Verified.** 0x10090180 code: model-interface slot 18 refusal zeroes bounds/returns false. Success always draws ACT_DIERAGDOLL, commits it only for bone−1 and valid pick; AddSolidFlags4, TriggerClientRagdoll, FX 0x17, move 0/bounds 0/ThinkSet(NULL) in order for flag 0. 0x1008b800 independently verifies force latch only when length²>0 and bone>0. |
| ordinary kill does not enter DIE/SCHED_DIE_RAGDOLL later | **Verified for completed ordinary NPC corpse creation.** 0x1032c0e0's tail replaces NPC think whether rigged or not; pedestrian override/fade replace it again. SelectSchedule assembly 0x1028a8ec..0x1028a92b is the separate zero-force/bone−1/flag 0 transaction, returning 0x2c or 0x2b. No patch to manufacture a death program. |
| both capability stubs false, CorpseForceBone INDEX_NONE, ordinary death may not reach handoff | **Verified, stronger for this snapshot: ordinary rigged admission is unwired.** ElysiumNpc.cpp::HasClientRagdollRig (:292–296) returns false; CorpseForceBone (:298–302) always INDEX_NONE. ElysiumNpcBaseSelect.cpp::SelectBecomeClientRagdoll (:75–79) also false. CombatCharacter.cpp::CreateCorpse (:1478–1491) calls the transaction only for ForceBone!=INDEX_NONE; otherwise it only performs the no-rig bounds writes. Merely baking or fixing capability leaves a rigged corpse with no call. |
| capability comes from PhysicsData, not generated PhysicsAsset | **Verified data availability and retail predicate.** CharacterProvenance.h::PhysicsSourceData is a cooked hard reference; CharacterProvenance.cpp loads physicsSourceAsset. import_characters.py::physics_source publishes/binds it. physics_data.py::physics_projection sets bHasPhysics from .phy presence. Runtime stubs must read Data.bHasPhysics; missing PhysicsAsset is a bake/attachment fault. |
| StartBodyRagdoll order works; animation/garment do not prevent the fall | **Verified as an existing spike observation, not a new run.** packets-spike.md documents scratch transient FPhysicsAssetUtils asset, actual pelvis time series, ABP installed/unpaused and bum_male garment. Current EntityBodies.cpp::StartBodyRagdoll (:1508–1558) has asset/body checks and profile→simulate→verify→wake. The spike state was reverted and predates C2's guarded real-bone path. It proves the reached handoff, not today's admission or a .phy solve. |
| change the dead motor actor collision switch | **Refuted/withdrawn.** Spike Findings1 and current BuildNpcVisual (:1742) show NewObject component owner=map actor; BuildNpcMotor attaches it to the motor. The capsule/actor collision switch does not own this mesh. Do not edit ElysiumNpcBody.cpp. |
| no-rig body advances its last retail client sequence | **Not verified as a client-clock claim; not adopted.** The listing proves absence of playback-rate writes in this function, not the client's complete animation clock. Current V4c no-rig arm and tests explicitly make no HoldBodyFinalPose call. The final brief retains refusal writes without a new holding/clock policy. |

**Exact ordinary fix:** wire the source predicate; implement CorpseForceBone's absent-hit
branch with real native Spine2 lookup; retain CreateCorpse's valid-bone guarded call and
no-rig bounds arm. A source-rigged model whose fallback cannot resolve gets a named data
failure, never a fabricated bone−1 seed. With a valid fallback it reaches
BecomeClientRagdoll → map StartBodyRagdoll → Bodies StartBodyRagdoll on the kill tick.
The .phy-derived asset/mesh binding supplies physics; it does not supply capability.
FElysiumTakeDamageInfo in ElysiumNpcBaseDamage.inl has no hitbox/bone field: retain a
named absent input for 0x101c2a30; scalar records use the recovered fallback. Impulse/hitbox
producer completion is 0014. The selector must execute the same transaction, not a bool-only
probe; preserve shared weighted draw/seed arms and avoid double handoff on restore.

## 2. Frames — conversions verified; placement needs one measurement

| Scout claim used | Verdict and checked evidence |
|---|---|
| decoder/projection already retain hulls, solid joins/mass/origin/angles, constraints, inverse bind | **Verified.** model_glb/physics.py::_ledge/decode/_typed_values; physics_data.py::physics_projection joins exact bone names, stores authored solid IDs/ordinals and Gaps, keeps optional-vs-zero limits, PoseToBone and raw origin/angles. No PhysicsAsset builder or placement conversion there. |
| PhysicsData hull vertices are raw IVP points to convert with (x,−z,−y)*100 | **Refuted as literal raw-point wording.** _ledge first writes `(x,−y,−z)`; exporters/model_glb.py::_physics_accessors publishes those vertices unchanged; physics_projection copies accessor values unchanged. For these published vertices `g`, native conversion is **100*(g.x,g.z,g.y)**. Applying the raw-IVP rule again would negate the wrong axes. The raw binary prop decoder separately uses 100*(x,−z,−y). |
| prop hull and skeletal mirror precedents suffice to settle solid→bone placement | **Refuted as a sufficiency claim; scout's stated remaining uncertainty is verified.** bake_lib.py::set_phy_collision receives already-converted static-prop geometry, makes one unsimplified convex per ledge and mass; it has no solid→articulated-bone mapping. bsp.py::source_to_unreal uses2.54*(x,−y,z), source_quat_to_unreal uses(−x,y,−z,w); skeletal_stage/payload.py::_conv_pos/_conv_quat actually use them. physics_projection retains origin/angles/PoseToBone but applies none. phy_vphysics.md §The ragdoll rig still explicitly leaves solid transforms unsettled. |
| regular_cop has15 solids/14 constraints; knees/elbows z-only with ranges−95..4/−120..4 | **Verified by bounded staged GLB lookups.** 15 solid properties with authored IDs0..14. Constraints1/3: R/L Thigh→Calf, x/y0, z−95..4;7/9: UpperArm→Forearm, x/y0, z−120..4. Source GLB: exports_v2/models/character/npc/common/cop_variant/regular_cop/regular_cop.glb. Default spike16/15 is a different fitted rig. |
| always map z to UE Swing2, reverse range and use midpoint45.5/58 | **Refuted as an unconditional instruction.** bsp's quaternion conjugation proves angular-axis signs only after the frame is identified. Conditional arithmetic is correct: z-reversed knee [−4,95] gives half-span49.5/midpoint45.5; elbow [−4,120] gives 62/58. It does not prove .phy's joint basis is Unreal's twist/swing basis. |
| phy.py's winding comment has a determinant error | **Verified.** The raw-IVP→UE matrix for(x,−z,−y) has determinant−1 (axis swap plus two sign negations), contrary to its +1 comment. The published-g→UE swap likewise has determinant−1. Convex point cloud cooking avoids reliance on triangle winding; otherwise validate/reverse for the actual transform. |
| BuildPhysicsAsset/cloth recipe are useful but not a ragdoll builder | **Verified.** ClothBuildLibrary.cpp::BuildPhysicsAsset creates kinematic sphere/capsule setups, converts bind points with accumulated BoneBindTransform inverse, then registry/maps; no .phy ragdoll builder. import_characters.py::cloth_assets owns cloth _PHYS, not skeletal-mesh ragdoll attachment. |
| --bodies exists; keep characters-v2; add independent recipe | **Verified.** importers/characters.py::stage_characters resolves exact unit ID/model key/unambiguous stem and include closure; cli.py::import_characters exposes repeatable selectors. import_characters.py::PRODUCER_VERSION/fingerprint affects existing products, so changing it would invalidate mesh/animations. Separate ragdoll recipe required, including mesh/source/frame versions and attachment repair on reuse. |

**Settled algebra:** write published-g→UE exactly once. Let `B_u` be the accumulated
reference transform of the actual baked bone. If measurement establishes solid-local points,
convert Source origin/QAngles to `S_u` (`R_u=M R_s M`, mirrored2.54cm translation), then
`p_b=B_u.inverse(S_u(100*(g.x,g.z,g.y)))`. Already-model-local and already-bone-local
alternatives omit S_u or both transforms respectively. The files cannot choose among these.
PoseToBone is Source-inch row-major 3x4 inverse bind (mdl_skel.py::read_bones and typed header),
not a native parent-relative frame. Do not apply it blindly to IVP metre points.
After finding a common component-space joint frame J_u, express it in both endpoint bones:
`B_parent.inverse*J_u`, `B_child.inverse*J_u`; derive the min/max signs in that basis.

**One first measurement owed, precisely:** scoped regular_cop .phy bake, then one controlled
collision/constraint-gizmo check in Physics Asset Editor/lab: right thigh/calf placement and
shared knee pivot in bind pose, plus angular sweep to the −95/+4 authored z stops. Confirm
calf hull follows anatomy/pivots coincide, then mirror-check left knee and elbow with the same
matrices. Capture accepted transforms, axes/signs, endpoint angles, recipe and screenshot in
phy_vphysics.md. This identifies placement and signed limits, not a calibration of retail's
fall. D1 prepares the candidate diagnostic; integrator executes after build 1 under coder
no-run rules, before the 34-model bake. Reserve build 2 for a corrected builder if needed.

## 3. Removal, current cleanup, sound and tests

| Scout claim used | Verdict and checked evidence |
|---|---|
| four removal clocks and later fade precedence | **Verified retail algorithms.** 0x1032c0e0 burn/PVS tail; 0x102696f0 code checks player cone→PVS→visibility mask 0x2804091 and rearm/remove; 0x103a38c0 code snapshots bounds/base/ThinkSet(NULL)/SOLID_NONE; 0x10265ad0 code calls fade after corpse. 0x10269960 code subtracts7 then zero/remove. +13.8 is the S13 timing consequence of255,36 decrements,0.1/0.2 cadence, not a measured result here. |
| DestroyNpcMotor alone does not release map-owned mesh; no per-entity sim cleanup | **Verified, with the missing world path now located.** Entity.cpp::Kill (:89–131) marks bDead and calls OnDormancyChanged unless already hidden. AnimatingImpl.cpp::GateVisual hides/stops pose tick and gates garment simulation. Npc.cpp::OnDormancyChanged disables motor; it does not destroy mesh/physics. ScriptedCharacter destructor calls DestroyMotor; MapActorEmbodiment.cpp::DestroyNpcMotor removes prerequisites and destroys only the motor. EntityWorld.cpp::RegisterNpcBody (:1119) keeps weak components; **Teardown (:2998)** destroys them through ElysiumWorldDestroyWeakComponents. Per-entity corpse removal leaves allocated bodies until teardown; hidden Kill skips dormancy hook entirely. |
| no visual destruction exists anywhere | **Refuted if read broadly.** Whole-world Teardown destroys registered NPC meshes; OnRuntimeModelChanged destroys/rebuilds one, and controller-release has explicit destruction. Those are not the corpse-removal path. At teardown DestroyComponent unregisters/releases component state; no special pre-stop is presently installed for corpse clocks. |
| burn sound missing at snapshot; exactly one retail call with stated fields | **Verified.** CombatCharacter.cpp::CreateCorpse burn tail still comments out presentation/audio conceptually, with no emission. Assembly 0x1032c3c1..0x1032c3d7 pushes pitch 100, attenuation 0.8, volume 1, wav, **channel 0**. Native pitch is1, not100; existing PlayBodySound exposes channel and recording BodySounds. Recheck after C2 integration; add only if still absent, with arm proof. |
| stale burn-as-static prose in both combat and physics docs | **Partly refuted as current status.** physics-interaction.md §2 still names burning as a static arm. The current combat-and-damage.md §Corpse construction already lists player/0x80000/ordinary ragdoll correctly, with burn in the tail. Preserve C2's correction rather than rewrite it from the scout's older snapshot. |
| ragdoll-true branch never has a test | **Refuted if broad; ordinary provenance-backed death coverage is still missing.** Current V4c NpcKernelAnim.RagdollSeed test checks rig-true real-bone/−1 via bTestRig override. NpcCombat.Death still exercises no-rig/no-hold/bounds; recording bBodiesRagdoll defaults false. Add a typed-provenance ordinary-death test and release-while-simulating test; retain the existing seed arms. |

The release must run on terminal removal, **not lethal death**, including hidden corpses.
Use an explicit visual pointer, not motor attachment discovery (physics can detach it).
Centralize stop-simulation/collision, physics-state release, presentation-child destruction,
claim cleanup, component destruction and pointer clear in an idempotent embodiment adapter;
call from NPC terminal Kill and destructor before motor destruction. Respect EndPlay retirement;
do not break DestroyMotor's model-replacement caller, which carries entity children forward.
Pedestrian corpses retain their body; removal must work whether awake, resting or sleeping.

## 4. Verified body scope and record staging

Narrow, case-normalized `model` key lookup in:
`E:/elysium-work/exports_v2/_sidecars/sm_hub_1/sm_hub_1.ents` and
`.../sp_tutorial_1/sp_tutorial_1.ents`. Only `models/character/*.mdl` keys were returned;
includes maker model keys. **Scout count/list verified: 22+18−6=34 distinct models.** These are
staged model dependencies, not34 asserted ragdoll-capable models; builder reports no-.phy skips.
Canonical selectors below include character/ and normalize case; no ambiguous leaf guessing.

| Canonical model key (repeatable --bodies selector) | Map |
|---|---|
| character/monster/rat/rat_swimming | hub |
| character/monster/rat/rat | tutorial |
| character/monster/wolf_form/wolf_form | tutorial |
| character/npc/common/blueblood/male/blueblood_male | both |
| character/npc/common/bum/female/bum_female | hub |
| character/npc/common/bum/male/bum_male | both |
| character/npc/common/cabbie/cabbie | hub |
| character/npc/common/citizen/female/female_citizen | hub |
| character/npc/common/citizen/female2/female_citizen_2 | hub |
| character/npc/common/citizen/female3/female_citizen_3 | hub |
| character/npc/common/citizen/male/male_citizen_1 | hub |
| character/npc/common/citizen/male2/male_citizen_2 | hub |
| character/npc/common/cop_variant/regular_cop/regular_cop | hub |
| character/npc/common/goth_kids/male/goth_male | hub |
| character/npc/common/prostitute/prostitute_2/prostitute_2_ref | hub |
| character/npc/common/sabbat_henchman/sabbat_henchman | both |
| character/npc/common/security_guard/security_guard | hub |
| character/npc/common/security_guard/security_guard_skinny/security_guard_skinny | tutorial |
| character/npc/common/shovelhead/shovelhead | tutorial |
| character/npc/common/gangmember_male_2/gangmember_male_2 | tutorial |
| character/npc/common/gangmember_male_2/gangmember_male_2_alt | tutorial |
| character/npc/unique/santa_monica/bertram/bertram | hub |
| character/npc/unique/santa_monica/knox/knox | hub |
| character/npc/unique/santa_monica/mercurio/mercuriodamagedstreet | hub |
| character/npc/unique/santa_monica/prophet/prophet | hub |
| character/npc/unique/downtown/igor/vdor | tutorial |
| character/npc/unique/downtown/sheriff/sheriff | tutorial |
| character/npc/unique/downtown/smiling_jack/smiling_jack | tutorial |
| character/npc/unique/malkavian_mansion/bach/buch | tutorial |
| character/npc/unique/society_of_leopold/average_vampire_hunter/average_vampire_hunter | both |
| character/npc/unique/society_of_leopold/average_vampire_hunter/dverage_vampire_hunter | tutorial |
| character/npc/unique/society_of_leopold/elite_hunter/elite_hunter | both |
| character/npc/unique/society_of_leopold/female_hunter/vampire_hunter_chick | both |
| character/npc/unique/society_of_leopold/female_hunter/vampire_hunter_chock | tutorial |

| Record body claim | Verdict / narrow evidence |
|---|---|
| damage_lethal_death / verbs_stealth_kill are regular_cop | **Verified:** both combat JSON casts. First is t3 scalar kill, second direct arena_mark/spawnflags 4/knife grapple, duration20. **Old brief's fade-child assertion refuted.** No stealth timing change justified. |
| corpse_removed_unseen / corpse_kept_seen are regular_cop | **Verified:** cast/model keys in their JSON; seen/unseen controls retain the ordinary clock. |
| corpse_kindred_burns adds Sabbat_Henchman | **Verified:** JSON from_map sabbat_redshirt_1; bounded sidecar row is npc_VVampire, TutorialShovelhead, spawnflags 4, Sabbat_Henchman model. |
| corpse_pedestrian_stays adds female_citizen_2 | **Verified:** JSON from_map pedestrian_female; bounded hub row's model is citizen/female2/female_citizen_2. |
| corpse_fades adds Shovelhead | **Verified:** JSON from_map stealth_victim_maker; bounded tutorial row's model is common/Shovelhead/shovelhead. All four record models already in 34. |
| corpse_on_floor is drawn pelvis/rest speed,24cm provisional | **Verified:** ArenaScenarioRunner.cpp::CorpseOnFloor (:746–795) reads Bip01 Pelvis, ignores skeletal component, traces WorldStatic 500 cm, compares measured speed <5 and record height. SampleCorpsePelvises computes speed from successive positions. Spike's default asset rest heights 16.2/32.9 cm are historical measurements, not .phy bounds. Both death JSONs already use this probe. |
| stealth was green despite no rig, floor probe can pass held pose | **Verified as record/spike history, not current binary outcome.** verbs JSON notes the held lying pose; probe code has no “is simulating” assertion. Integrator must check real asset attachment and simulation separately. |
| three known_red records; no arena sound proof | **Verified snapshot:** damage_lethal_death(V4d), pedestrian/fade(V4c); unseen/seen/Kindred/stealth lack known_red. No sound expectation in Kindred JSON; arm recording is the selected audio proof. V4c may update record statuses before V4d. |

No whole-corpus321/289 rig census,621 physics-asset count or rollout timing from the scout
was adopted as freshly verified evidence; these do not size the scoped task. Only the34-unit
lookup and regular_cop rig were counted here. No pipeline import inventory timing was claimed:
importers/characters.py::stage_characters inventories the corpus before selection; the integrator
must time its read-only inventory/preflight query before scheduling and optimize if it exceeds
the query budget. Bake durations in the integrator brief are planning allowances, not measurements.

## 5. Placement

D1 owns editor builder/header/editor dependencies/phy_vphysics recovery. D2 owns only
import_characters.py. D3 owns the explicit runtime/API/test/death-recovery paths listed in
brief-D-ragdoll.md. **All pairwise file intersections empty**, including header ownership.
Integrator owns only declared serial extra lines, named Arena records, the acceptance runs
and spec/tracker close; workers report those owed lines under 350 words. V4c source is an
in-progress input, never overwritten. The first frame measurement belongs to the integrator
under the no-run coder rule. No new owner decision. Full-corpus pipeline rollout, impulse,
prop_ragdoll/friction/BurnModel look remain 0014; save semantics remain V6.
