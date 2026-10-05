# Brief V7 integrator — compile first, prove inputs and close explicitly

**Start after V5b and V6 commit.** Relocate functions by name on V6's committed code:
V7-1 shares ElysiumNpc.cpp/.h, ElysiumEntity.cpp, ElysiumEntityWorld.cpp and
ElysiumClassRegistry.h/.cpp; V7-2 shares ElysiumSchedule.cpp/.h,
ElysiumNpcSenses.cpp and ElysiumNpcBaseAnim.cpp; V7-3 shares arena scenario
reader/runner files with V6-3. Apply serial owed-line/service edits after owning diffs land.

Read AGENTS.md, [README.md](README.md), [packets-V7.md](packets-V7.md), all three coder
briefs/reports, Arena/README.md, spec0002 standing rules and triage N5/N6/N11.
The coordinator names the integration branch/worktrees. This brief grants implementation
integration after predecessors land. V7.2 authorizes only an evidenced required-payload repair,
with exact paths/provenance as a serialized acceptance prerequisite; no speculative pipeline lane.

The baseline is **the latest commit of the work branch when integration starts**.
Today it is **a5b58f37 (V4d)**: arena **114 pass / 1 fail rollcall_vzombie /
15 expected-fail / 2 unexpected-pass**; default **169 / 0**; arm **1,625 / 0**.
V5b lands before V7, and V7 runs **after V6 commits**. Record the actual starting hash,
V5b/V6 hashes and their reported results; today's totals are historical, not a new measurement.

## 0. Judge-directed kickoff and serialized prerequisites

The [third sitting](../v1/judge-third-sitting.md) is final. These are numbered jobs,
not requests for another ruling. Three coder manifests remain disjoint (34/22/43 paths);
no fourth coder/second round is needed. Jobs2/3 are serialized integrator work.

1. **V7.4 kickoff audit**, before lane1 job25 changes initialization:
   record packet §8's targetability writers and raw BOOL SAVE row. Finish the default/
   construction and species audit against the listing and V6's committed source, distinguishing
   a default from "empty template leaves the existing byte". Check real initialization dispatch
   at Npc.cpp::Activate / NpcLifecycle2.cpp::TroikaNPCInit **0x1029a0b0**, cine Spawn
   **0x101a6f10**, camera NPCInit **0x103692c0** and the named species sites in lane1 job25.
   Send exact outcomes to lanes1/2/3; preserve existing true/false writers rather than rewrite
   entire initialization. Verify the effective SAVE binding through NpcClasses.cpp's class
   registration and the common V6 codec/applier; if generator-owned mapping is absent, record
   its exact authored input/output paths and repair/regenerate serially, never hand-edit output.
   **Proof:** comfort_idle_weight ordinary initialized donor and refusal controls,
   npc_inputs_v7_dispatch provenance, true/false actual codec/applier roundtrips from
   NpcInputsV7 arms. +0x1480 is neither TIME nor an unavailable source.
   Read non-NPC feed +0xff0 and player relation +0x647c source chains before extending:
   retain named nothing-returning accessors and absent records under 0005/3 and 0002/R4
   where the substrate source is genuinely absent; fixtures prove consumer arms only.

2. **V7.1 bounded 0006/1–2 direct service**, after coder integration and before build1:
   owned `Source/ElysiumUE/Private/Substrate/ElysiumDisciplines.h` and
   `Source/ElysiumUE/Private/Substrate/ElysiumDisciplines.cpp`, plus affected
   `Source/ElysiumUE/Public/ElysiumPlayer.h` combat-character declarations/
   FElysiumDisciplineState and FElysiumActiveDisciplineEffect state only.
   Add proposed ElysiumDisciplines::SetBloodshieldDirect(FElysiumCombatCharacter&, bool),
   which lane1 job10 binds at Npc.cpp::InputSetBloodShieldDiscipline.
   Resolve Thaumaturgy_Bloodshield **0x101e1590**; true calls direct status
   **0x101e3560** then self target **0x101e3730** via **0x101e3380**.
   Source status duplicate gating is independent of old target replacement:
   target replacement uses RemoveEffect(high-bit,true), input false uses
   **0x101e1870 -> 0x101e3af0(high-bit,false)**, including clean-off no-op.
   Reuse ResolveRules, SourceActivationEnd/HasDisciplineStatus, ResolveHitTable/ApplyHit,
   RunTriggerCasting and RemoveTargetEffect only where their recovered behavior matches.
   Finish every Bloodshield-reachable missing modifier/hit/helper/teardown arm before accepting
   the input. Preserve distinct source COMBAT and target FLINCH emission/order; no learned,
   cost, world-area, recovery or ordinary-Use gates.
   Packet §8 pins direct ordering, removal and authored human/guardian/player hit arms.
   **Proof:** input_bloodshield_v7 on/repeated-on/off/clean-off, actual buffer/modifiers/
   status, sound and removal ordering; admission includes ordinary human Use_Spell and
   BloodGuardian buffer300, plus explicitly bounded player service fixtures.
   An unavailable hook, registration tally or EndBloodshield-only teardown cannot close V7.

3. **V7.2 admission and provenance**, before accepting each required witness:
   inspect exact class/model/native sequence/body/clip/voice asset/recipe and actual admission.
   Runtime sites: LineService.cpp::PlayDirect/PlayDialogueTurn and SpeechVolumeFor,
   Entity.cpp::PlayDialogFile, ChoreoScene.cpp direct speaker calls,
   EntityWorldDialogue.cpp dialogue-turn submission; **0x102c2680 / 0x102c278d..27a2**.
   Run the live admission observation only after successful compilation.
   An already cooked controlled donor may change payload only when it proves the identical
   body/sequence/voice arm. Preserve map_hub_follower_boss_v7's authored prostitute_1 dialogue/
   Python path. If required payload is actually missing/wrong, repair only that prerequisite,
   recording exact asset/recipe/source/deployed paths and repeat admission before the record
   closes; no speculative/full-corpus bake or extra lane. This overrides blanket V7 bake/content
   bans solely for evidenced required payloads.
   **Proof:** input_speech_volume has a real handle/gain with unchanged media cursor/talking
   deadline through live and decode-pending updates; silent controls remain negative controls.
   input_movement_multiplier and hub/Bloodshield witnesses require their actual admitted payload.
   A timer, silent stand-in or synthetic authored delivery is insufficient; unavailable required
   witnesses block acceptance.

4. **Inherited Clock ruling**, V6 ArenaStage.cpp::Stage/Session BeginNewGame handoff,
   engine **0x200f5bb4..0x200f5bc4 / 0x200975f0**, NPC
   **0x10273390 / 0x10273ad0**:
   confirm1.0 before fresh entity initialization/arena Load, with seed/reset order retained;
   revisit reads frozen map time, explicit load saved time. Re-measure every zero-tuned V7
   record alone and after another record on the final binary, including the inherited N5/N6/N11
   witnesses. Lane3 job12 owns record observations; serialize any exact ArenaStage.cpp owed
   observation line after V6/V7 harness integration. Change only proved staging/epoch assumptions
   with per-record evidence, keeping shared draws, predicates and original behavioral bounds.
   No replayed old RNG draws or enlarged windows; unexplained reds block closure.
   **Proof:** all35 named records plus pre-entity epoch and dispatch/probe trace stamps.

## 1. Compile pass first## 1. Compile pass first — at most six builds

1. Gather all three diffs onto the named integration tree after prerequisite landings.
   Verify exact path manifests and empty intersections. Re-read final predecessor diffs in
   Npc.cpp/.h/Entity.cpp/BaseAnim.inl, Schedule.cpp/.h and shared oracle sections.
   Do not copy full older files. Preserve V4c event-only shots, timer guard, shared NpcSchedule
   stream/equal-bound fast path, team gates/dead-enemy memory, death/fade, initial NONE and
   V6's fresh epoch1.0 before Load (revisit frozen / explicit load saved). Preserve V4d corpse ownership/release/rig behavior and V5b
   both interrupt masks/453→411→freeze/cache and reload deadlines/count semantics.

2. Apply EXACT owed lines before build1:
   - **Npc.h** remove RequestClearSchedule, TakeClearScheduleRequest override and private latch;
     **Npc.cpp** remove that now-unused implementation. Lane2 removes generic polling/interface.
     Preserve all real NpcBase/ElysiumSchedule direct clears and start/run timing, **0x10280d30**.
   - **Public/ElysiumPlayer.h**, FElysiumCombatCharacter:
     declare `int32 FeedIdealActivityNumber() const;`, definition lane2 Feed.cpp,
     **0x10168910 +0xff0**. Non-NPC absent source remains explicitly INDEX_NONE/nothing.
   - **NpcSelect.cpp::TroikaSelectSchedule**, the HearWorld-only InvestigateSound assignment:
     review and remove duplicate only where lane2's CommitBestSound now covers that exact
     selection call. Preserve PreSelectSchedule's reset-before-forced order **0x102ae920**.
     **NpcSenses.h** comment no longer calls InvestigateSound unwritten, **0x102b4090**.
     **NpcScheduleHost.h** document forced-id mapping/sentinels if stale.
   - Nonshipping **EntityWorld.cpp::DeliverInputTo** conversion rejection and post-handler
     callbacks to ArenaV7Support; resolve real descriptor owner/type, no Delivered on failure,
     no callback-coerced params, **0x100abc90 / 0x100d05d0**.
   - **NpcBaseSounds.cpp::ShouldPlayIdleSound** debug observation captures actual weight/result
     and float-call branch at the real sites, **0x1027a4b0 / 0x1027a4c0 / 0x1027a4de**.
     Keep one RNG draw. Never get a test's result by calling the predicate a second time.
   - Audio/LineService missing read-only voice gain/media-cursor/owner accessors, exact pair
     from lane3 report; shared scalar order observer only if needed. Use actual existing voice
     ledger (SetGain) and don't introduce media control in probes, **0x102c2680 / 0x1008d230**.
   - Complete §0 job2's bounded direct Bloodshield service and lane1 binding before build1.
     No unavailable-hook metadata substitutes for the required effect.
   - Other exact coder-owed declarations/callers only, with recovered address.
     New unowned runtime scope requires retail evidence and named coordination.

3. Check method constness/inheritance/includes/linkage and all shadowed locals (C4458/C4459).
   The shared SequencePlaybackRate exists ONCE on FElysiumAnimating after lane2 migration;
   ground scalar storage remains through ResetSequenceInfo; lane1 calls same setter.
   ArenaV7Support callbacks compile only outside shipping; fixtures and CVar restoration retain
   command/override/default identity. Hand new slot bodies go in *SlotBodies.cpp;
   never hand-edit generated *Slots.cpp. Add exact kernel_verdicts.tsv rows and generator
   target/signature evidence as needed for the species hand queries before regeneration.
   Invoke `uv run elysium research kernel` only after these owned rows are correct.

4. Compile FIRST: **`uv run elysium build --arm`**.
   Allow **up to six builds, about two minutes each**, record actual times and errors.
   Compile/link fixes are integration-owned. Stop at the sixth failed build with concrete errors
   and incomplete jobs; never test an assumed old binary or omit a lane to pass.
   Rebuilds after runtime changes consume the same six-build budget and return affected tests.
   Do not run any arena/default/arm tests before a successful compile pass.

## 2. Default tests, then the wave's named records

After successful compilation, run **`uv run elysium test`**.
Resolve wave-owned failures against packet/listing. No fake boolean/disposition or new interval
constants to restore old expectations.

Then run the following **35 named records**. Lane3 owns their listed paths;
each row in README§4 defines staging, expect/never and address. JSON can loop freely without
rebuild; code corrections recompile within the budget. Parse errors are failures to fix.
Do not remove assertions or mark new wave regressions known_red.

```text
input_disablethink
input_changeschedule_reselect
script_walk_to_mark
verbs_stealth_kill_scripted
verbs_feed_trance
npc_inputs_v7_dispatch
input_allowalertlookaround
input_allowkickhintuse
input_allowopendoors
input_faint_v7
input_fleeanddie
input_makeinvincible
input_bloodshield_v7
input_setbossmonster
input_default_dialog_camera
input_dontface_datamap_bug
input_falltoground
input_follower_type
input_investigate_modes
input_movement_multiplier
input_speech_volume
input_stay_entrenched
input_walktonode
input_disablethink_bool
input_changeschedule_deferred
stealth_v7_state_matrix
stealth_v7_croucher
stealth_v7_species
investigate_v7_priority
sound_commit_v7_mirror
relationship_composition_v7
comfort_idle_weight
clear_schedule_v7
feed_auto_accept_ideal
map_hub_follower_boss_v7
```

Suggested grouped command for Green Room records (split only for useful diagnostics):
`uv run elysium arena input_disablethink input_changeschedule_reselect script_walk_to_mark verbs_stealth_kill_scripted verbs_feed_trance npc_inputs_v7_dispatch input_allowalertlookaround input_allowkickhintuse input_allowopendoors input_faint_v7 input_fleeanddie input_makeinvincible input_bloodshield_v7 input_setbossmonster input_default_dialog_camera input_dontface_datamap_bug input_falltoground input_follower_type input_investigate_modes input_movement_multiplier input_speech_volume input_stay_entrenched input_walktonode input_disablethink_bool input_changeschedule_deferred stealth_v7_state_matrix stealth_v7_croucher stealth_v7_species investigate_v7_priority sound_commit_v7_mirror relationship_composition_v7 comfort_idle_weight clear_schedule_v7 feed_auto_accept_ideal`.
Map witness separately: `uv run elysium arena map_hub_follower_boss_v7`.

**Each of the30 new records** runs both alone and after another record:
for EACH non-map name above under scenarios/v7, run
`uv run elysium arena <name>` then `uv run elysium arena control_sequence <name>`.
Check report boot/stage grouping and per-record reset: do not claim same-boot proof from
two independent invocations. Each new map record runs alone and after a named preceding
map record such as map_hub_idle in an ordered invocation; without shares_map they get fresh
map boots. Document that separation, don't pretend to have tested persistent map inheritance.
If launcher order differs, cite actual boot order rather than command order.

Inspect .scenarios[name,result,first_unmet] and both traces. Every record this wave writes
or breaks belongs to this wave, including unrelated former greens affected by typed dispatch,
idle/relationship/feeding/scalar or callback changes. A trace of accepted input is NOT a
state/effect verdict. New records are not expected-fail by default.
Bloodshield is required pulled-forward work: service/effect failure blocks closure; it cannot
receive a filed known_red or pass on binding-only availability.

Existing regressions likely affected: verbs_stealth_kill, verbs_feed_victim_dispatch,
input_setrelationship, lifecycle_relationship_flip, ranged_open_fire, anim_footsteps_walk,
patrol_sentry2_pingpong/input_clearpatrolpath, hearing/investigation and dialogue program records.
Run affected named families as needed while looping. Keep their survival fixtures and seed/clock.
Don't revive V4c's old RNG, initial-state bans or fixed cooldown assumptions.

## 3. Required measurements — the listing cannot answer Unreal outcomes

Perform after compile/default tests during the named-record loop, not as a coder blocker.
Write evidence into these planning packet/recovery documents or commit message; never report*.md.

1. **Hub path:** fresh sm_hub_1, existing prostitute_1, inspect actual dialogue row/condition and
   response index. Stage legitimate money/quest state using existing public gameplay/debug doors
   and record it explicitly. Follow authored makeFollower row52 and resetHos row82 or a real
   authored reset output. Observe Python→AcceptInput, handle/name, reset/flags ordering.
   If dialogue support is missing, finish the narrow harness/VM door in this wave; don't relabel
   direct input fire a witness. Preserve authored map/script identity; required missing body/voice
   payload repair follows §0 job3. Body movement itself is R4.
2. **Speech:** both dialogue CurrentVoice and direct/choreo speaker-owner routes, silent,
   decode-pending and live. Snapshot same handle, requested gain, media cursor, talking deadline,
   lip/scene state at0/.5/1; one lab audible check when voice content is available. No restart,
   premature OnDialogEnd/OnEndSequence, or missing zero gain. If absent asset, name exact authored
   path/recipe/bake provenance and follow §0 job3's admission/required-payload repair, never a
   field-only green or silent stand-in. Pending decode must carry a real voice handle and gain.
3. **Movement:** already playing same cooked gait, multiplier ±1,0,.5,2 and negative low.
   Measure kernel scalar/rate, ground speed, cycle slope, distance and event phase with fixed
   pose/sequence. ResetSequenceInfo control shows playback1 while ground scalar persists.
   Trace protects sequence identity/RNG/event cursor. No second MaxWalkSpeed multiplier.
4. **Source audit:** targetability is the known +0x1480 latch and initialization/SAVE chain
   in §0 job1, lane1 job25 and lane2 job6. Measure an ordinarily initialized comfort donor,
   empty-template/cine/camera refusals and true/false actual codec/applier roundtrips.
   FeedIdealActivityNumber non-NPC +0xff0 and CandidatePlayerRelationHandle647c player
   +0x647c remain named unavailable source accessors after the packet's bounded source read.
   Their live records are absent under 0005/3 and 0002/R4; fixtures prove only consumers.
   Never infer a player handle from NPC layout or substitute a disposition/schedule for ideal.

5. **Buffers/variants:** String→numeric and conversion rejection are listing-settled.
   Byte limits260/256 must be confirmed in port encoding, with an ASCII boundary and non-ASCII
   control. If retail-compatible byte conversion is absent, report exact carrier/encoding
   limitation; no undocumented Unicode modernization.
6. **Bloodshield:** perform §0 job2's effect proof on real admitted donors, not just a
   registration/binding test. Record status duplicate gate, target replacement, buffer/modifiers,
   both sound categories/order, false/clean-off removal and all selected hit/helper arms.
   Failure or any unfinished reachable arm keeps V7 unchecked; the final ruling already owns it.

## 4. Recovery## 4. Recovery, verdict rows and close gates

Before final validation, record recovery in the matching documents (integrator writes):
- **docs/vtmb/npc-ai/authored-control.md**:19 typed adapters, dash-prefix/pending forced word,
  N5 converter matrix/failed-dispatch distinction and PE DontFace mismatch.
- **docs/vtmb/entity_io.md** / **docs/vtmb/python_bridge.md** only where dispatcher or hub bridge
  facts require correction: actual declared types versus raw handler type, reachable helper chain.
- **docs/vtmb/npc-ai/conditions-and-states.md**: full ShouldInvestigate body, equality boundaries,
  composed relation readers, real disturbed producer, comfort branch/actual targetability latch,
  producer/SAVE audit and genuinely unavailable non-NPC/player source accessors.
- **docs/vtmb/npc-ai/senses.md**: complete mirror and no-condition sticky tail independent of V10.
- **docs/vtmb/npc-ai/schedule-kernel.md**: direct clear remains sole door; dead latch removed,
  preserve path/slot435 and StartTask-vs-RunTask timing.
- **docs/vtmb/animation_and_movers.md**:0x1008d230 ordered scalar writes,
  assembly0x10091595/15f6, no playback/ground double multiplication/reset distinction.
- **docs/vtmb/stealth.md**: live ConVar default1/state arms, GhoulCroucher immediate bypass and
  Rat/Payphone override scope. No claim bypass guarantees later verb admission.
- **docs/vtmb/npc-ai/social.md**: runtime ordered follower-rule row source and voice gain update,
  same live stream/time, preserved existing squad fatal→log divergence.
- **docs/vtmb/feeding.md**:ideal+ff0 four ids, post-feed virtual relation, absent non-NPC source.
  Keep V4c transaction evidence.
- **research/tooling/ghidra/driver/kernel_verdicts.tsv**: update only actual coverage rows for
  ported input/consumer functions and hand-slot destinations; list address/verdict/target/evidence.
  Existing-hand methods stay hand; new hand bodies go in matching *SlotBodies.cpp.
  Never edit generated *Slots.cpp; `uv run elysium research kernel` owns generated changes.
  Do not invent out-of-closure rows or mark Bloodshield service present when only its hook exists.
- **docs/vtmb/disciplines.md**: bounded direct self-status/self-target chain, duplicate/replacement
  distinction, high-bit removal, selected deployed Bloodshield hit/helper rows and sound ordering.
- **docs/vtmb/npc-ai/lifecycle.md**: exact targetability writers/default/species audit and BOOL SAVE,
  including preserved already landed initialization and common-applier roundtrip evidence.

After successful compile, default tests and named loops, close in EXACT order:

1. **`uv run elysium test arm`**.
2. **`uv run elysium research kernel --check`**.
   If stale, correct verdicts/regenerate with `uv run elysium research kernel`, check again.
   Compile-impacting regeneration returns to compile/affected/default/named/arm checks; final
   results must describe final files.
3. **Full arena ONCE: `uv run elysium arena`**, after last code/build/record change.
   Every moved verdict gets before/after/retail reason/owner evidence.
   If a change follows that final acceptance, it invalidates acceptance; state incomplete
   rather than presenting the earlier full run as final. Do not schedule blind repeated full runs.
4. Update N5/N6/N11 in **stories/v1/triage.md**, and **spec.md V7 / TRACKER.md** only when
   acceptance actually closes each scope. N5 Bool corrections are partial predecessor work;
   String-wire record now decides remaining conversion defect. DontFace retail bug is preserved,
   not a new divergence. Bloodshield unfinished means V7 remains open.
   Include README §6's explicit filed close lines: V10 lifetime; R1 producers/VSound;
   R3 KickHintMechanism/DoorInputMechanism/CoverMechanism;
   R4 FollowerLocomotion; R5 IncapacitationRecovery; name SoundLifetime and
   OtherSoundProducers/VSoundSource at the V10/R1 boundaries;
   0005/3 non-NPC feed +0xff0; 0002/R4 player +0x647c. Name each typed setter/real-reader or
   unavailable accessor and keep kick_hint_mechanism_live, door_input_mechanism_live,
   follower_locomotion_live, incapacitation_recovery_live, unproduced-sound witnesses,
   feed_auto_accept_non_npc_live and relationship_player_647c_live absent under those owners.
   Follower-row loading, all ShouldInvestigate arms and complete sound mirrors must close now;
   HUD remains presentation-only. Filed mechanisms do not excuse any wave-owned red.
5. **One commit**, stage by **explicit paths** from manifests plus precise owed/doc/generated
   paths. Never git add -A / ., never push. No clean/reset/stash/checkout/delete/worktree removal.
   Message includes actual prerequisite hashes, build times/count, default/arm totals,
   named alone/after results and full arena baseline/final totals; verdict table:
   `record | before | after | retail address/reason | remaining owner`.
   Include all changed assertions, recovered/INFERRED distinctions, named source seams and
   judge decisions. No file named **report*.md**.

Return under350 words with files/addresses, builds/times, tier totals, moved verdict table,
remaining measurements/filed-owner work and whether V7 actually closed. Never push.

