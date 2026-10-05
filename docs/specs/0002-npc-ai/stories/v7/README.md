# V7 — retail inputs, then testable consumers

Planner rulings applied, 2026-10-05. Docs only. The integration baseline is the latest
commit of the work branch when integration starts; today's reference is **a5b58f37 (V4d)**:
arena **114 pass / 1 fail rollcall_vzombie / 15 expected-fail / 2 unexpected-pass**,
default **169 / 0**, arm **1,625 / 0**. V5b lands before V7; **V6 must commit before V7 starts**.
Read AGENTS.md, spec.md V7, TRACKER.md, triage N5/N6/N11 and Arena/README.md.
Evidence is [packets-V7.md](packets-V7.md); final authority is
[the third sitting](../v1/judge-third-sitting.md). Faint is already registered;
all19 remain the acceptance set. V7 remains unchecked until measured acceptance.

These briefs were written only under E:/elysium-work/worktrees/coord/docs/specs/0002-npc-ai/stories/v7/.
No planner source edits, build, tests, arena, bake or commit. No subagents were launched.
Coordinator names the implementation worktrees; do not create/remove/switch them under this brief.

## 1. Dependency order and scope

Land V5b and V6 before any V7 coder writes. Relocate functions **by name on V6's
committed code**, preserving its codec/applier and lifecycle changes rather than copying
old file versions. Fresh maps and freshly rebuilt arenas start at **1.0 before entity
initialization/Load**; revisit uses frozen map time and explicit load saved time.
Retain seed/reset order, retail predicates, shared draw order and original behavioral bounds.
Re-measure every formerly zero-tuned record alone and after another record. Only proved
staging/epoch assumptions may change, with per-record evidence; no old RNG draws or widened
windows. Unexplained reds block closure.

1. Integrator first completes §0 job1's read-only targetability/default/species kickoff audit.
   V7-1, V7-2 and V7-3 can then start together **after V6 commits**, on disjoint files.
   V7-3 prepares observations/records first; integrate harness support before evaluating records.
2. V7-1 adds delivery/adapters and targetability initialization/SAVE wiring; V7-2 supplies
   consumers, shared scalars and named unavailable source accessors.
3. After all three diffs are integrated, the integrator serially applies exact owed lines,
   completes the bounded Bloodshield service, then admits required content before acceptance.
4. Compile first and run the brief's acceptance sequence. Coders never build/run/commit.

The pulled-forward service fits a **serialized integrator step**, exactly as V7.1 orders;
there is no fourth coder and no second round. The integrator owns
`Source/ElysiumUE/Private/Substrate/ElysiumDisciplines.h`,
`Source/ElysiumUE/Private/Substrate/ElysiumDisciplines.cpp` and affected
`Source/ElysiumUE/Public/ElysiumPlayer.h` combat-character declarations/state only for
that service. Lane1 binds it; lane3 proves it.

Scope includes every arm of the predicate this wave repairs. 12a therefore includes its
friendly-sound/type and third-party-combat proximity arms, not just two early refusals.
10d includes the no-condition sticky tail. N6 includes dash-prefix preserving a pending word,
invalid names and deferred consumption. V7 adds no game divergence.
The shipped STRING/Bool mismatch in DontFace is preserved.

## 2. Lane manifests and disjointness

The following lists are exhaustive. A new file is explicitly named; a directory or wildcard
does not grant ownership. Existing filenames are located from today's main tree.
Integrator alone edits recovery documents, ledger inputs, spec/tracker/triage and owed lines.
A coder's report is returned to the coordinator, not saved as report*.md.

### Lane V7-1 — delivery

- `Source/ElysiumUE/Public/ElysiumClassRegistry.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumClassRegistry.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumEntityWorld.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumEntityWorldDialogue.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumEntity.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumChoreoScene.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcClasses.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcLifecycle2.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseLifecycle2.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcCamera.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumScriptedSequence.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcNewscaster.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPlayerController.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPlaceholder.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSpawnSpecies.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpc.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpc.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcGhoulCroucher.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcGhoulCroucher.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcRat.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcRat.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPayphone.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPayphone.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSquad.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumRulebook.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumRulebook.h`
- `Source/ElysiumUE/Private/Audio/ElysiumLineService.cpp`
- `Source/ElysiumUE/Public/ElysiumLineService.h`
- `Source/ElysiumUE/Private/Tests/ElysiumInputTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumStealthKillTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcInputsV7Tests.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcGhoulCroucherSlotBodies.cpp` (new)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcRatSlotBodies.cpp` (new)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPayphoneSlotBodies.cpp` (new)

### Lane V7-2 — consumers

- `Source/ElysiumUE/Public/ElysiumAnimating.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumAnimatingImpl.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcEntityChain.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnim.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnim.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions10.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSenses.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseSounds.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumFeed.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumFeedSchedules.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumStealth.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumSchedule.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumSchedule.h`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcInvestigateTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcSoundSweepTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSoundsTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumFeedingTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumFeedTranceTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumScheduleTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcConsumersV7Tests.cpp`

### Lane V7-3 — harness and records

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

**Proof:** comparing the literal paths gives L1∩L2=∅, L1∩L3=∅, L2∩L3=∅.
Lane counts: **L1=34, L2=22, L3=43**. The same literal lists appear in each coder brief.
The serialized integrator service files are in none of these coder lists; owed-line edits
to coder-owned files occur only after the owning diffs are integrated.
In particular Npc.h belongs only to1; NpcEntityChain.inl and Animating.h only to2;
Arena reader/runner and all JSON only to3. No generated *Slots.cpp belongs to a coder.

### Files shared with incoming waves

**V7 runs after V6 commits.** V7-1 overlaps V6 at ElysiumNpc.cpp/.h,
ElysiumEntity.cpp, ElysiumEntityWorld.cpp and ElysiumClassRegistry.h/.cpp;
V7-2 overlaps at ElysiumSchedule.cpp/.h, ElysiumNpcSenses.cpp and
ElysiumNpcBaseAnim.cpp. V7-3 shares ElysiumArenaScenario.cpp/.h and
ElysiumArenaScenarioRunner.cpp/.h with V6-3. Relocate every function by name
against V6's committed code before editing. These collisions forbid concurrent V6/V7 writes.


V4d D3 overlaps **Npc.cpp, Npc.h, Entity.cpp** (lane1); its integrator also owns
**docs/vtmb/combat-and-damage.md, docs/vtmb/npc-ai/lifecycle.md** and the generated ledger,
which V7's integrator may update. V4d's committed BaseAnim.inl change is also a predecessor
collision with lane2; preserve ragdoll/sequence-zero behavior there.
V7 coders own neither Visual/EntityBodies nor Physics Asset/pipeline files.
V7.2 permits only a diagnosed required missing-payload repair as a serialized acceptance
prerequisite, with exact paths/provenance recorded before repair; no speculative pipeline lane.
V4d's changes to damage_lethal_death / verbs_stealth_kill are acceptance dependencies, not lane3 files.

V5b's coders have no intersection with these V7 coder manifests. Its **integrator**
owes **Npc.cpp, Schedule.cpp, Schedule.h**, and **conditions-and-states.md,
schedule-kernel.md, combat-and-damage.md, kernel_verdicts.tsv**; these overlap V7.
Keep its common positive/inverse interrupt-mask cache, virtual453→empty411→freeze order.
Conditions10.cpp/tests, Maintain.cpp, Motor.cpp, WeaponClasses and RunTask stay outside V7.
Do not regress their reload, shared RNG, animation-event shots or live deadlines.
Main is being edited: ownership lists prove V7 coder disjointness, not concurrent safety
against unlanded predecessors. Coordinator waits for their landing before lane writes.

## 3. Shared interfaces agreed before coding

- Lane1 adds optional declared input type metadata to FElysiumClassDesc::Input and resolves
  descriptor/type/thunk together in DeliverInputTo. Existing untyped registrations stay untyped.
  V7 types are in packet§3; DisableThink Bool, ChangeSchedule String. Conversion failure is
  distinguishable from UnknownInput; success traces alone are not proof of state/effect.
- Lane2 supplies shared FElysiumAnimating::SetGroundSpeedScalar(float),
  GetGroundSpeedScalar() const, SetPlaybackAndSpeedScalar(float), and moves the existing
  SequencePlaybackRate member from NpcEntityChain.inl to Animating.h, preserving its name.
  GroundSpeedScalar defaults1; one live playback word, no shadow. Lane1's multiplier adapter
  calls that setter and updates existing MotionTrail. Lane2 uses the existing embodiment
  SetBodySequencePlaybackRate door; no extra motor/visual writer.
- Lane1 uses live ConVarInt(DebugAllowNonIdleAutoSk); makes the existing NPC stealth query
  virtual const and adds actual GhoulCroucher/Rat/Payphone overrides, preserving other slot294
  forwarding. The base is an existing hand method; new species bodies belong in the named
  *SlotBodies.cpp files and their verdict rows are integrator-owned.
- Lane2 provides a nullable ShouldInvestigate overload plus reference forwarding; its relations
  use IRelationTypeOf and real const/mutable enemy accessors. Source type is
  Senses.Memory.InvestigateSound.TypeMask.
- Lane3's support is debug-only: finite named probes and explicit fixture/call actions, described
  below. It cannot mutate shipping flow, coerce inputs on behalf of the runtime, change seed/time
  mid-record, add fake input names, mark admissions successful or supply missing content.
  Runtime annotations owed outside3 are applied serially by the integrator.

## 4. Records — witnesses before implementation

Every new non-map record uses **stage "arena"**, Green Room with ordinary rows/cast. Default
donor: neutral TutorialThug human/regular_cop, use_interesting0, explicit seed1, player outside
vision/hearing unless the case requires it. Copy complete donor survival/clock/seed setup.
Timed actions normally begin after admission, t1; reversible inputs at t1/t2; ordered input trace
and authoritative probes after each delivery. Freeze task advancement with native Bool
DisableThink where observing a pending word; do not bypass the I/O dispatcher to test wires.

The table below defines exact names, staging, expect and never. Packet citations apply to every
listed value. Lane3 expands cases into multiple named fixture calls within that record, or asks
the coordinator to name additional records; never silently drops an arm. A fixture is marked
synthetic in about/notes. It invokes the REAL body and records its result, not a copied predicate.

| record | staging | expect / never; retail citation |
|---|---|---|
| npc_inputs_v7_dispatch | enumerate19 thunks on Troika human and inherited cop/croucher; dispatch canonical arguments in isolated fixtures | all19 resolve, inherited owner/type correct, delivered effect matches rows below; never unknown or duplicate registration. Bloodshield availability separate from effect; 0x100abc90, packet§3. |
| input_allowalertlookaround | fire true,false,"1","0", invalid Bool conversion/vector fixture; ALERT control | +6434 toggles, true enables existing lookaround selection; invalid conversion leaves value; never COMBAT program;0x102c2b90. |
| input_allowkickhintuse | hint-capable human; toggles +6436, call existing kick eligibility with same candidate | branch observes toggle, preserves unrelated flags; never kick admitted while false;0x102c2c10. Actual prop execution belongs R3. |
| input_allowopendoors | preset other capability bits; Bool/string toggle | mask0xd00 add/remove only; never other bit changed;0x102c3540. No locked-door bypass. |
| input_faint_v7 | live human and dead/ideal-dead controls, pending think deadlines | reset614 before schedule0xfa, source0x26c2, knockout flag/event; dead refusal retains schedule; never bypass death guard;0x1029f250/102ae780. Faint thunk already landed. |
| input_fleeanddie | human on navigable pad with an escape node; live/dead controls | reset→source0x26b5→schedule0x6f, subsequent authored tasks; never immediate death or undead schedule installation;0x1029f210/102ae750. |
| input_makeinvincible | fixed damage packet before/after toggle, Bool and wire String | +63d8 gates damage; off admits it, never wounds while invincible;0x102c2a30, existing damage guard. |
| input_bloodshield_v7 | Bool on, repeat on, off and clean-off; real ordinary human and BloodGuardian donors plus explicit player-service fixtures; admitted deployed record/rules | status/effect buffer + trait/modifier/remove chain, no blood/cost activation gate; never generic Use side effects;0x102c32d0→101e3380. Required live effect proof, including status duplicate gate, target replacement, sound and removal order; a missing service blocks V7. |
| input_setbossmonster | active human, true/false/repeated, normal registry state | +6496 writes and existing UpdateCharacter registry adds/removes once; never duplicate boss entries;0x102c3500/10298070. |
| input_default_dialog_camera | " Dynamic3 ", whitespace, empty, 260-byte overflow and failed Bool conversion; open dialogue afterward | trimmed nonempty write, empties preserve, native buffer limit, next camera reads it; never empty overwrite;0x102c2910. |
| input_dontface_datamap_bug | initial spawnflags8 then normal string"1"/"0"; separate raw-handler fixture Bool and failed Bool wire | wire clears8, Bool wire refused/preserves; raw handler Bool sets then StartTalking skips306; never claim wire Bool sets it;PE105d0d18/102c34b0/102c0270. |
| input_falltoground | flags2 containing unrelated bits; true/false/String; invoke ordinary think | AND/OR0x80004000; existing bit0x4000 reader honored; never invented motion call/effect (retail0x102bfdf0 empty);0x102c3470/10293311. |
| map_hub_follower_boss_v7 | **stage map:sm_hub_1**, prostitute_1, real .dlg makeFollower then resetHos row; fresh map, no shares_map | input from Python, name"!player"/handle player → empty/invalid; resetAi then0x3008 on acceptance; never synthetic fire called an authored witness;0x1019643c/102c3350/102c4430; dialogue52/82. |
| input_follower_type | valid Default/CombatNonCombatant, missing/empty; fixture rule distances overlapping | authored64/100/150,384/500/550; missing FIRST row; clamps +10 in order, type name retains caller text; never stale prior distances;0x101e8c90/102c4640/102c4680. |
| input_investigate_modes | BOTH inputs0..6,-1,7,String"6",Float6.9,failed Bool; query real predicate on player/nonplayer | correct word/unchanged invalid, truncate6.9 to6; modes' truth table; never invalid clamped or wrong-mode overwrite;0x102c2ab0/102c2b20/100d05d0. |
| input_movement_multiplier | live moving stable clip; -1,0,1,2,.5,-2,.001, String"2"; raw wrong type and NaN fixture | scalar/rate1,.001,1,2,.5,.001,.001; trail0 except2/NaN3; scaled GroundSpeed/cycle/event phase, resets playback only on sequence reset; never double speed or restart;0x102c3580/1008d230/10091595. |
| input_speech_volume | dialogue and PlayDialogFile/choreo speaker; idle/talking/decode-pending; -1,0,.5,2,String".5" | +6550 clamp; current voice gain0/.5/1, same handle/media cursor/talking deadline; idle only store; never restart/advance/finish dialogue;0x102c2680/102c2793. |
| input_stay_entrenched | normal and combat interest controls, toggle true/false | early refusal even current enemy/mode6; false restores other logic; never investigate when true;0x102c2bd0/102b3270. |
| input_walktonode | named real node, valid schedule with comma/space variants, extra third token, missing token/node/schedule, DEAD | one-node Replace repeat0/type0; empty/missing node preserves path, dead no-op; invalid schedule -1 follows body; never generic whitespace-only behavior or append;0x1029e840/1029f460/102c47e0. |
| input_disablethink | retain original Hunter1 from_map Green Room STRING1 at2/0 at9 | existing task freeze then resume windows unchanged; never taskdone/schedule/death during disable;0x1029f2a0/1029f300. Remove known_red only on evidence. |
| input_disablethink_bool | same donor native true/false plus repeated false | same task behavior, reset only true→false; never conversion repair that regresses nativeBool;0x1029f300. |
| input_changeschedule_reselect | existing Hunter1 from_map stage arena; '-' at3 | choose-new reselection at next think, unchanged original .5s deadline; never immediate in-handler install;0x102c33f0. |
| input_changeschedule_deferred | admitted disabled human; name then "-suffix" before enabling; empty/bad name/0 controls | forced word retained across dash-prefix, flags0x82000000; consumption clears once before normal selection, no synchronous install; invalid stores-1; never dash clears;0x102c33f0/102ae7f0/102ae920. |
| verbs_stealth_kill_scripted | existing scripted donor, crouch/knife+use; live ConVar default1 | SCRIPT victim admitted and dies; never SEE_PLAYER, retain pose/death prerequisites;0x102c2300. |
| stealth_v7_state_matrix | direct real eligibility query fixtures across raw states, hidden/dialog/invincible/hear/see/life controls; ConVar1 then0 |1 excludes DEAD,0 only1/0xd; guards in retail priority, never bypass base life gates;0x102c2300. Restore ConVar. |
| stealth_v7_croucher | real GhoulCroucher disturbed key0/1; undisturbed with base refusal guards; NEW_ENEMY gathers once | undisturbed always true at slot294, disturbed base; latch/output once through real Gather/OnDisturbed; never base guard before undisturbed bypass;0x1037bbc0/1037b040/1037b570/1037b6e0. Real +use admission still has ray/other gates. |
| stealth_v7_species | Rat and Payphone fixtures, call via NPC virtual | Rat true,Payphone false independently of base; never flatten inherited slot;0x103ad680/101aa8d0. |
| investigate_v7_priority | named fixture matrix flags/stay/null/boss/enemy, all7 modes, friendly source/type1,0x10,other, third-party combat | exact ordered answers including <=256XY/<=80Z boundary, candidate and enemy distance OR; never boss/enemy admission before guards or missing suppression;0x102b3270/102b8cd0. |
| sound_commit_v7_mirror | cached seven sound records with distinct source/type/position/time/radius/serial; multi-condition and no-condition calls | priority winner full copy→source→InvestigateSound; no-condition retains winner yet rewrites source/mirror; never partial copy or V10 expiry dependency;0x102b4090/102b3d90. |
| relationship_composition_v7 | NPCs with boss/reverse relation/insane/source; real sight and post-feed consumer, HUD projection separate | virtual404 composition at all gameplay sites; const/mutable enemy differences, own bossLIKE/reverse candidate answer; never flat table overriding composition;0x10299da0/1026a2c0/1033a9e0. |
| comfort_idle_weight | real installed0x12f comfort on an ordinary initialized nonempty-statTemplate NPC, no positive latch fixture; empty-template/cine/camera refusals; float hook and normal-program controls | actual local0x12f, inclusive0..20 draw and no float call; normal0..999 or float; never Local=-1 or certainty assertion;0x1027a420. Capture actual draw bounds/calls rather than probabilistic pass threshold. |
| clear_schedule_v7 | real scripted NPC direct ClearSchedule during start/run fixture, preserve path set | six zeros in order, preserve clear and slot435zero; same-loop start vs next-think run; never request latch/delayed extra-clear;0x10280d30. |
| script_walk_to_mark | retain existing Green Room cine record | cleanup's direct clear/reselect, preserve original event windows; never leftover request;0x10280d30 and existing cine cleanup citations. |
| feed_auto_accept_ideal | real AttemptFeed fixture mismatch current vs ideal; four numeric ids and adjacent COWER2/3/INTO controls | exactly ideal104e/1068/1069/1098 autoaccept BEFORE resistance/RNG; others existing roll, never disposition/schedule substitute;0x10168910. |
| verbs_feed_trance | existing transaction, release then second real feed while ideal mesm remains | first sets actual ideal, second automatic acceptance, events once; never admission based only on schedule or double OnFedUponEnd;0x10168910/1033a9e0. |

Arena fixture additions are owned by lane3, not deferred acceptance:
finite named state/sound/relationship staging with retail offsets in schema comments, trace before
each mutation, isolated fresh rows per case, strict parser; direct predicate calls and observations
cannot assert a synthetic result. A read-only probe of the runtime result does not call RNG again.
Input delivery snapshots observe real post-handler words (and rejection); no fake dispatch events.

All new records must run alone AND after another record at V6's fresh epoch1.0.
Epoch restaging needs measured proof and retains original behavioral bounds. Hub runs alone and after another named
map record in a fresh non-sharing boot; do not claim this checks persistent shares_map state.
New Green Room records run after control_sequence in the same stage-group boot with real reset.
Existing N5/N6/N11 red removal belongs to V7; a record this wave writes or breaks is this wave's.

## 5. Judge's rulings applied

The [third sitting](../v1/judge-third-sitting.md) is final. No V7 row was refused;
the inherited Clock ruling also applies.

| item | ruling | where it landed |
|---|---|---|
| V7.1 Bloodshield direct status service | pull forward | Integrator §0 job2: bounded ElysiumDisciplines.h/.cpp service and affected combat-character declarations/state; V7-1 job10 binds it; V7-3 job5 and input_bloodshield_v7 prove the full effect; packet §8. No unavailable-hook or binding-only close. |
| V7.2 content failure in verification | do now | Integrator §0 job3 admission/provenance and bounded required-payload repair; V7-1 job20 real voice gain wire; V7-3 jobs6/9 retain hub Python/dialogue and real same-handle speech, including pending decode; packet §8. |
| V7.3 later sound/mechanism/social substrates | file | V7-1 job26 and V7-2 job10 leave typed setters/real-reader seams; README §6 names V10/R1/R3/R4/R5 and absent live records; integrator §4 close job4 names each handoff. Follower-row loading, full sound mirror and every ShouldInvestigate arm remain V7. |
| V7.4 input source correction | do now | Integrator §0 job1 records/default/species audit; V7-1 job25 initialization and BOOL SAVE sites; V7-2 jobs6/7/11 latch and named +0xff0/+0x647c accessors; V7-3 job10 ordinary initialized comfort donor and refusal/consumer controls; packet §8. Genuine unavailable sources filed below. |
| Clock standing question, inherited from V6 | do now | Dependency order and top of every brief; V7-3 job12 epoch observation/restaging; integrator §0 job4 re-measures zero-tuned records at1.0, alone/after, preserving draws/predicates/bounds; packet §8 cites engine epoch/load sites. |

## 6. Filed seams and later owners

These are explicit handoffs, not wave-owned failures. Setters still perform conversion,
exact state writes and every already landed consumer; a fixture never credits a missing system.

| seam left by V7 / real reader | later owner | live record remains absent |
|---|---|---|
| Committed sound record/mirror via CommitBestSound and ShouldInvestigate; named **SoundLifetime** handoff | 0002/V10 lifetime | lifetime witnesses belong V10, not V7's cached-record proof |
| Existing EmitGameSound receipt and typed producer door; named **OtherSoundProducers/VSoundSource** handoff, no fabricated result | 0002/R1 other sound producers and VSound | unproduced-sound witnesses |
| AllowKickHintUse, capability mask0xd00 and real SelectCoverOrKickSchedule / OnObstructingDoor readers; named **KickHintMechanism / DoorInputMechanism / CoverMechanism** handoffs report unavailable where absent | 0002/R3 complete kick/door/cover | kick_hint_mechanism_live, door_input_mechanism_live (full cover coverage remains R3) |
| SetFollowerBossName/SetFollowerType, real boss relation reader and ordered authored distances; named **FollowerLocomotion** handoff unavailable where absent | 0002/R4 squad/follower locomotion | follower_locomotion_live |
| Faint/FleeAndDie and existing schedule consumers; named **IncapacitationRecovery** handoff unavailable where absent | 0002/R5 additional incapacitation programs | incapacitation_recovery_live |
| FeedIdealActivityNumber non-NPC +0xff0 -> INDEX_NONE; no disposition/schedule substitute | 0005/3 feeding-source follow-up | feed_auto_accept_non_npc_live |
| CandidatePlayerRelationHandle647c player accessor -> invalid/nothing; NPC uses actual FollowerBoss, no invented player handle | 0002/R4 player relation-source follow-up | relationship_player_647c_live |

These bold handoff names label the seam at the named real setter/reader; they are not
new shipping inputs or asserted implemented methods. V7-1 job26 / V7-2 job10 name
them in the existing sites' comments/accessors, using unavailable/nothing where absent.

Integrator's close names all seven seams, their owners and absent records, distinguishes
consumer fixture coverage from live-source coverage, and keeps HUD changes presentation-only.
Targetability is recovered V7 work and is **not** a filed unavailable source.
