# Brief V7-3 — arena observations, explicit fixtures and records

**Start after V5b and V6 commit.** V6-3 shares ElysiumArenaScenario.cpp/.h and
ElysiumArenaScenarioRunner.cpp/.h with this lane. Relocate functions by name on V6's
committed code and retain its arena reset to1.0 before Load. Keep seed/reset order,
shared draws, predicates and original behavioral bounds; job12 owns proved epoch restaging.

Read AGENTS.md, [README.md](README.md)§4, [packets-V7.md](packets-V7.md), Arena/README.md.
**Testable first:** prepare records/support before the implementation is integrated.
The planner supplies names/stages/staging/expect/never; your job includes missing harness support.
Never hand-stage success in place of a real runtime operation.

## Files — exhaustive ownership

- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.cpp`
- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.h`
- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.cpp`
- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.h`
- `Source/ElysiumUE/Private/Debug/ElysiumArenaV7Support.cpp`
- `Source/ElysiumUE/Private/Debug/ElysiumArenaV7Support.h`
- `Source/ElysiumUE/Private/Tests/ElysiumArenaV7Tests.cpp`
- `Arena/README.md`
- `Arena/scenarios/world/input_disablethink.json`
- `Arena/scenarios/world/input_changeschedule_reselect.json`
- `Arena/scenarios/world/script_walk_to_mark.json`
- `Arena/scenarios/combat/verbs_stealth_kill_scripted.json`
- `Arena/scenarios/combat/verbs_feed_trance.json`
- `Arena/scenarios/v7/npc_inputs_v7_dispatch.json`
- `Arena/scenarios/v7/input_allowalertlookaround.json`
- `Arena/scenarios/v7/input_allowkickhintuse.json`
- `Arena/scenarios/v7/input_allowopendoors.json`
- `Arena/scenarios/v7/input_faint_v7.json`
- `Arena/scenarios/v7/input_fleeanddie.json`
- `Arena/scenarios/v7/input_makeinvincible.json`
- `Arena/scenarios/v7/input_bloodshield_v7.json`
- `Arena/scenarios/v7/input_setbossmonster.json`
- `Arena/scenarios/v7/input_default_dialog_camera.json`
- `Arena/scenarios/v7/input_dontface_datamap_bug.json`
- `Arena/scenarios/v7/input_falltoground.json`
- `Arena/scenarios/v7/input_follower_type.json`
- `Arena/scenarios/v7/input_investigate_modes.json`
- `Arena/scenarios/v7/input_movement_multiplier.json`
- `Arena/scenarios/v7/input_speech_volume.json`
- `Arena/scenarios/v7/input_stay_entrenched.json`
- `Arena/scenarios/v7/input_walktonode.json`
- `Arena/scenarios/v7/input_disablethink_bool.json`
- `Arena/scenarios/v7/input_changeschedule_deferred.json`
- `Arena/scenarios/v7/stealth_v7_state_matrix.json`
- `Arena/scenarios/v7/stealth_v7_croucher.json`
- `Arena/scenarios/v7/stealth_v7_species.json`
- `Arena/scenarios/v7/investigate_v7_priority.json`
- `Arena/scenarios/v7/sound_commit_v7_mirror.json`
- `Arena/scenarios/v7/relationship_composition_v7.json`
- `Arena/scenarios/v7/comfort_idle_weight.json`
- `Arena/scenarios/v7/clear_schedule_v7.json`
- `Arena/scenarios/v7/feed_auto_accept_ideal.json`
- `Arena/scenarios/maps/map_hub_follower_boss_v7.json`

README proves pairwise disjointness. V4c's Scenario reader/runner probes and clock reset are
prerequisites to preserve. ArenaStage.cpp and runtime files are outside this lane.
No coder pipeline, cooked content, source corpus or generated Slots edits.
V7.2's diagnosed required-payload repair is a serialized integrator prerequisite.

## Numbered jobs

1. **Scenario.h/.cpp::ParseText/action/probe schema and reader,
   Runner.cpp::RunAction / FireDueActions / ReadDueProbes / ReadProbe**,
   observed **0x100abc90 / 0x102b3270 / 0x102b4090 / 0x1027a420 / 0x10168910**:
   add a finite DEBUG-ONLY V7 fixture/call surface in new ArenaV7Support.h/.cpp.
   Use #if !UE_BUILD_SHIPPING; no game console inputs, production class registrations or
   unrestricted offset/field writes. Update Arena/README with exact accepted keys and failures.
   Suggested concrete schema (the names are new, not currently supported):
   `{"t":1,"do":"v7_fixture","target":"mark","fixture":"investigate","values":{...}}`,
   `{"t":1.05,"do":"v7_call","target":"mark","call":"should_investigate","to":"other","combat":true}`,
   `{"at":1.1,"who":"mark","probe":"v7_result","call":"should_investigate","equals":false}`.
   Finite fixture kinds: input_contract, stealth_state, cached_sounds, investigate,
   relationships, ideal_activity, comfort, direct_clear.
   Each gets a typed whitelist, no unknown keys; validate class, handles, ranges, units and
   legal target before mutation. A wrong record is a parse/run error, not a default.
   Record each fixture mutation as script with supplied values and retail field/address.
   Preserve normal fresh stage Load/seed/curtime; no field overrides in ordinary cast admission.
   Only explicit fixtures receive synthetic state; about/notes says so.
   Keep expectations against production output where the ordinary authored route exists.

2. **ArenaV7Support::ObserveInputDelivery / ObserveInputRejection / ReadNpcWords** (new),
   real delivery **0x100abc90 / 0x100d05d0** and packet§3's19 handlers:
   after REAL conversion/handler, capture exact input result, raw/converted type and real
   word/effect snapshot; rejection records no-handler. Include both resolved descriptor owner
   and input name; unknown remains separate.
   Use a debug observer installed for the record and removed at cleanup; never manufacture
   Delivered or bypass DeliverInputTo in a wire test. Runtime callbacks are integrator-owed.
   Add finite read-only probes for input conversion/rejection status, allow-alert/kick,
   capability mask, invincible, boss flag/registry admission, camera text, spawnflags,
   flags2, follower name/handle/type/distances, both investigate modes, forced schedule,
   disabled AI/timer-reset count, motion trail, ground scalar, playback rate, sequence/cycle,
   ideal activity, talking deadline and active voice handle/gain/cursor.
   Existing numeric probes should be reused when available.
   Full cached sound probes observe Source/TypeMask/Position/times/radii/serial; compare all
   fields with donor values, not just a stringified label.
   Interpret port globals/sentinels in packet, don't read a numeric retail id out of the wrong space.

3. **ArenaV7Support::CallRetailBody** (new), observed **0x102b3270 / 0x102b4090 /
   0x102c2300 / 0x1037bbc0 / 0x1027a420 / 0x10280d30 / 0x10168910**:
   finite calls: should_investigate(nullable candidate,combat), commit_best_sound,
   valid_stealth_target(actual player), ghoul_gather_conditions, idle_sound,
   direct_clear_start/direct_clear_run, attempt_feed.
   Call existing REAL APIs once, record result after execution; probes read that captured
   result and never call again (especially RNG/AttemptFeed).
   A call allowed only in an explicit fixture cannot leak into shipping input flow.
   Direct clear timing fixture uses the actual interpreter StartTask/RunTask sites, not a
   fake trace schedule or new shipping task name. Source missing for an arm remains explicit,
   never provided by a private copy of the retail algorithm.
   ConVar staging is a finite `v7_convar` action for DebugAllowNonIdleAutoSk only, through
   existing ElysiumNpcTunables::SetConVar; capture and restore full live value/command/default
   state on success/error/cancel BEFORE subsequent record Load. Never reset all tunables
   as an unexplained cleanup policy. Observe actual idle-roll bounds/call and scalar input
   order through debug callbacks owed to integrator; a probabilistic repeat is not the oracle.

4. **Arena/scenarios/v7/*.json** EXACT files in manifest, **packet§3 input addresses**:
   implement each README table row, staged "arena", no inherited unstated keyvalues.
   npc_inputs_v7_dispatch covers19 availability/types across inherited classes, but per-input
   records must prove authoritative writes/effects and negative controls.
   Input fire params must retain real JSON variants: Bool true is a different case from String"1".
   Ordinary wrong-type conversion must be rejected before handler; raw-handler fallback arms
   are separate explicit fixtures and may not be counted as ordinary I/O.
   Camera byte-buffer and WalkToNode comma/space/token failures need positive controls.
   Faint is already wired; its record checks reset/stamp/order/real schedule, no duplicate thunk.
   DontFace must reflect PE105d0d18's STRING declaration, preserving the retail mismatch.
   FallToGround must NOT demand new falling movement:0x102bfdf0 is empty.
   OpenDoors/Kick check landed capability/eligibility readers; R3 later physical mechanisms are
   not silently asserted green. Invincible's damage test checks wounds/applied state, not damage
   trace alone. Boss registry repeated-call control cannot inherit state from prior records.

5. **input_bloodshield_v7.json**, ArenaV7Support.cpp::ObserveInputDelivery/ReadNpcWords,
   **0x102c32d0 / 0x101e3380 / 0x101e3560 / 0x101e3730 / 0x101e3af0**:
   Bool on/repeated-on/off/clean-off must execute the real direct service supplied by the
   integrator, status before target; duplicate status is suppressed while target replacement
   still runs (old target teardown interrupted=true; input false interrupted=false).
   Observe buffer/modifiers/status, source COMBAT0x1 and target FLINCH0x10 sound order,
   removal callbacks and final ownership, including no-effect clean-off.
   Use real admitted BloodGuardian for inherited buffer300 and ordinary human for
   Hit_Human -> Use_Spell invocation/filter-refusal; explicit real-player direct-service fixtures cover buffer80 and
   player mappings without claiming the NPC input is a player registration. Packet §8 pins
   these deployed row differences; never force a player HitTable on a human.
   A missing reachable helper/modifier/hit/teardown arm blocks V7 until completed in the
   bounded integrator step. Binding-only, generic Use or an unavailable hook cannot pass.
   No later-spec expansion or service implementation belongs to this lane.

6. **map_hub_follower_boss_v7.json**, **0x10195510 / 0x1019643c / 0x102c3350**:
   stage **map:sm_hub_1**, existing prostitute_1 only, no rows/spawn/from_map there.
   Use authored prostitute.dlg makeFollower52 then resetHos82 (or its real authored reset output)
   through existing dialog_choose, player/router actions and public scripting doors.
   Preserve the record as a fresh non-sharing map boot.
   Annotate retail BSP lump19797/19799 and vamputil4398/4421, the actual .dlg responses taken.
   Exact live money/quest choices, response indices and time bounds are INTEGRATOR measurements;
   name provisional bounds/setup in notes, do not call them measured.
   No synthetic SetFollowerBoss fire in this authored witness.
   If runtime script/dialogue staging needs support, add narrow runner action to invoke the
   already deployed shared helper through the REAL script VM, marked synthetic unless launched
   by the authored dialogue/output. Do not write scripts or map assets.

7. **input_disablethink.json / input_disablethink_bool.json / input_changeschedule_reselect.json /
   input_changeschedule_deferred.json**, **0x1029f2a0 / 0x1029f300 / 0x102c33f0 / 0x102ae920**:
   preserve original N5 String1/0 and deadlines; add nativeBool donor control instead of replacing
   strings. Preserve N6 "-" reselection deadline; add name then "-suffix" preserving pending id,
   invalid-1 and deferred next-think installation. Observe post-input/prethink snapshot without
   changing time or immediate-running schedule semantics.
   Remove known_red only after final measured evidence, not in coder prediction.

8. **verbs_stealth_kill_scripted / stealth_v7_state_matrix / stealth_v7_croucher /
   stealth_v7_species**, **0x102c2300 / 0x1037bbc0 / 0x1037b040 / 0x1037b570 /
   0x1037b6e0 / 0x103ad680 / 0x101aa8d0**:
   real +use/crouch/knife and SCRIPT hold remains the N11 end-to-end witness.
   Query fixture covers all state/ConVar guards; croucher undisturbed bypass is eligibility
   ONLY, not guaranteed FindVictim/grapple admission. Include base-refused hidden/dialog/
   invincible/hear/see/dead-life controls to show it precedes guards, then disturbed equivalents.
   Seed disturbed using its REAL "disturbed" key, and test NEW_ENEMY→Gather→once OnDisturbed
   path with output/closest-player/relationship reads; do not add a second producer.
   Rat/Payphone must be dynamically dispatched queries, not classname expected booleans.

9. **input_movement_multiplier / input_speech_volume**, **0x102c3580 / 0x1008d230 /
   0x10091595 / 0x102c2680**:
   record BOTH scalar words and real movement/ground-speed/cycle/event observations on an
   already playing cooked gait; same sequence/cycle continuity around setter, separate reset test.
   The input ±1 resets to1; low negative→.001 is distinct from shared setter negative→1.
   Speech record covers dialogue/direct/choreo current voice, silent and decode-pending:
   clamped store AND actual gain on same handle/cursor/deadline; no media restart or outputs.
   Lab audible/rate observations are integrator measurements, not guessed ratios from a changing clip.
   V7.2: record exact class/model/native sequence/voice asset/recipe and admission failure.
   Integrator diagnoses and repairs only that required payload or admits an equivalent cooked
   controlled donor. Observe a real voice handle/gain and unchanged cursor/deadline, including
   pending decode; a silent stand-in/timer cannot pass. Preserve the authored hub witness.

10. **investigate_v7_priority / sound_commit_v7_mirror / relationship_composition_v7 /
    comfort_idle_weight / clear_schedule_v7 / feed_auto_accept_ideal**,
    **0x102b3270 / 0x102b4090 / 0x10299da0 / 0x1027a420 / 0x10280d30 / 0x10168910**:
    implement full matrices in README/packet, especially guard priority, sound-type suppression,
    equality at256XY/80Z, both third-party distances, reverse relation, mirror NO CONDITION,
    actual comfort draw bounds/float bypass, direct clear timing, IDEAL versus current mismatch.
    Comfort also has an ordinary initialized nonempty-statTemplate donor whose targetability
    is written by real NPCInit, never a fixture. Add empty-template/cine/camera and all other
    refusal controls; packet §8 and lane1 job25 identify producer/SAVE sites. Observe actual
    latch, local0x12f, draw bounds and float skip without re-executing the predicate.
    Other synthetic fixture records call real bodies. Do not wait for V10 sound-life,
    disposition producer/R5, R4 squad mechanics or pipeline just to stage an observable.
    Existing script_walk_to_mark and verbs_feed_trance remain program/verb evidence;
    add second real feeding after first's post-feed ideal activity is installed, not immediately
    when only a schedule id changed. Retain original paired-output/cleanup clauses.

11. **Tests/ElysiumArenaV7Tests.cpp**, **the observed addresses above**:
    strict reader invalid-key/type/class/missing-target cases, fixture refusal-as-error,
    snapshot-not-reexecute rule, cleanup of ConVar/observers/fixture state on normal/error/cancel,
    multi-record no-leak coverage and pending-schedule post-delivery/prethink observation order.
    Test meaningful harness guarantees, not copies of production expected formulas.
    Family `Elysium.Arm.ArenaV7.`; add no new production test tiers or command surface.

12. **Inherited Clock ruling**, ScenarioRunner.cpp::FireDueActions/ReadDueProbes and
    affected records in the43 owned paths, engine **0x200f5bb4..0x200f5bc4 /
    0x200975f0**, NPC **0x10273390 / 0x10273ad0**:
    observe V6's pre-Load/pre-entity epoch1.0 through the existing reset fence
    (ArenaStage.cpp remains integrator-owned). Re-measure every zero-tuned record alone and
    after control_sequence/another named map record. State time conventions explicitly:
    corrected absolute-vs-relative staging needs measured before/after retail-chain evidence,
    with original behavioral bounds unchanged. Keep seed/reset order, shared draws and <=1
    initialization arms; no RNG replay, mid-record clock change or interval enlargement.
    Unexplained reds block closure.
    **Proof:** all35 named records, especially input_disablethink,
    input_changeschedule_reselect, script_walk_to_mark and comfort_idle_weight; report actual
    pre-entity epoch and dispatch/probe stamps for each restaged record.

## Dependencies / exact owed lines

Integrator serially adds:
- EntityWorld.cpp::DeliverInputTo nonshipping actual delivery/rejection observer callbacks;
  lane1 supplies conversion status, owner and real snapshot data.
- NpcBaseSounds.cpp::ShouldPlayIdleSound actual weight/result and float-call debug observation,
  retaining ONE RandomInt call; lane2 supplies the body.
- Shared scalar setter actual write-order observation only if necessary.
- Voice handle/request/cursor read accessors on existing audio/line interfaces if missing;
  return exact declaration/definition pairs, no implementation by reflection/private pointer hacks.
- Any ArenaStage.cpp cleanup door beyond runner lifetime, with exact location/reason.

No invented probes accepted by JSON until schema/reader/runner all land. Return missing API
signatures and provisional measurement points under350 words; integrator owns record loops.

## Coder rules

Write only this lane's listed files, **by absolute path**, in the implementation worktree
the coordinator names. Read files and run read-only tools from **E:\\dev\\elysium-unreal**.
Never build, run the editor/game/tests/arena, bake or commit. Do not edit another checkout.
Put the retail address at **each changed line**, including declarations, adapters, tests and
harness observations. Report a new divergence; do not adopt it.
Never hand-edit generated `*Slots.cpp`: new generated-slot hand bodies go in matching
`*SlotBodies.cpp`; the integrator adds the exact `kernel_verdicts.tsv` row and regenerates.
Shadowed locals, members or globals are compile errors (C4458 / C4459).
Return a report **under 350 words**: files/functions/addresses, changed test names, record
coverage, and exact lines owed by each file outside the lane (or explicitly none).
Do not save it as a file named `report*.md`. Never clean/reset/stash/checkout/delete/remove
worktrees; never write the retail install or the main checkout.
