# V7 — retail inputs, then testable consumers

Planner final, 2026-10-05. Docs only. Baseline **V4c d0f79574**, main planning read
HEAD eb6d9f94 plus live V4d edits. Read AGENTS.md, spec.md § "V7. The inputs", TRACKER.md's
V7 box, triage N5/N6/N11 and Arena/README.md. Settling evidence is [packets-V7.md](packets-V7.md).
The old word “19 unregistered” is stale: **Faint is already registered**, but all19 stay
in the acceptance set. V7 remains unchecked until measured acceptance and judge disposition.

These briefs were written only under E:/elysium-work/worktrees/coord/docs/specs/0002-npc-ai/stories/v7/.
No planner source edits, build, tests, arena, bake or commit. No subagents were launched.
Coordinator names the implementation worktrees; do not create/remove/switch them under this brief.

## 1. Dependency order and scope

Land V4d and V5b first; retain any V6 lifecycle work that lands before V7. Re-read actual
prerequisite diffs, not whole older files. Existing compiled assets need no V7 bake.
V4c recorded default169/0, arm1624/0, arena113 pass/1 fail/16 expected-fail/2 unexpected-pass
of132; these are historical commit results, not an installed-binary claim.

1. Lane3 writes strict observations/staging and records first against the packet; all three
   coders can prepare disjoint diffs, but integration takes harness support before evaluating records.
2. Lane1 adds typed delivery and missing adapters, N5/N6/N11, runtime follower row read and voice gain.
3. Lane2 ports the consumers and shared scalar; retains all predecessor behavior.
4. Integrator applies owed declarations/docs/verdict rows, compiles FIRST, then runs acceptance.
   No coder build/run/commit. Coder rules are at the end of each brief.

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
Lane counts: L1=26, L2=21, L3=43.
In particular Npc.h belongs only to1; NpcEntityChain.inl and Animating.h only to2;
Arena reader/runner and all JSON only to3. No generated *Slots.cpp belongs to a coder.

### Files shared with incoming waves

V4d D3 overlaps **Npc.cpp, Npc.h, Entity.cpp** (lane1); its integrator also owns
**docs/vtmb/combat-and-damage.md, docs/vtmb/npc-ai/lifecycle.md** and the generated ledger,
which V7's integrator may update. V4d's live BaseAnim.inl change is also a predecessor
collision with lane2; preserve ragdoll/sequence-zero behavior there.
V7 deliberately owns neither Visual/EntityBodies nor Physics Asset/pipeline files.
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
| input_bloodshield_v7 | Bool on, repeat on, off and clean-off; existing target record/rules | status/effect buffer + trait/modifier/remove chain, no blood/cost activation gate; never generic Use side effects;0x102c32d0→101e3380. **Judge-held effect proof**, not a green thunk-count substitute. |
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
| comfort_idle_weight | real installed0x12f comfort on eligible NPC; explicit targetability seam fixture; float hook eligible; normal-program control | actual local0x12f, inclusive0..20 draw and no float call; normal0..999 or float; never Local=-1 or certainty assertion;0x1027a420. Capture actual draw bounds/calls rather than probabilistic pass threshold. |
| clear_schedule_v7 | real scripted NPC direct ClearSchedule during start/run fixture, preserve path set | six zeros in order, preserve clear and slot435zero; same-loop start vs next-think run; never request latch/delayed extra-clear;0x10280d30. |
| script_walk_to_mark | retain existing Green Room cine record | cleanup's direct clear/reselect, preserve original event windows; never leftover request;0x10280d30 and existing cine cleanup citations. |
| feed_auto_accept_ideal | real AttemptFeed fixture mismatch current vs ideal; four numeric ids and adjacent COWER2/3/INTO controls | exactly ideal104e/1068/1069/1098 autoaccept BEFORE resistance/RNG; others existing roll, never disposition/schedule substitute;0x10168910. |
| verbs_feed_trance | existing transaction, release then second real feed while ideal mesm remains | first sets actual ideal, second automatic acceptance, events once; never admission based only on schedule or double OnFedUponEnd;0x10168910/1033a9e0. |

Arena fixture additions are owned by lane3, not deferred acceptance:
finite named state/sound/relationship staging with retail offsets in schema comments, trace before
each mutation, isolated fresh rows per case, strict parser; direct predicate calls and observations
cannot assert a synthetic result. A read-only probe of the runtime result does not call RNG again.
Input delivery snapshots observe real post-handler words (and rejection); no fake dispatch events.

All new records must run alone AND after another record. Hub runs alone and after another named
map record in a fresh non-sharing boot; do not claim this checks persistent shares_map state.
New Green Room records run after control_sequence in the same stage-group boot with real reset.
Existing N5/N6/N11 red removal belongs to V7; a record this wave writes or breaks is this wave's.

## 5. For the judge

1. **Bloodshield direct status service**: retail0x102c32d0→101e3380→101e3560/101e3730 and
   high-bit RemoveEffect are recovered. Existing Use imposes extra gates, EndBloodshield only
   tears down a modifier group. Spec0006's completed joins do not expose the required direct
   status API. Alternatives: spec0006 owner provides this narrow service and its record, or judge
   explicitly amends V7 ownership for that service. Until then expose only a named unavailable
   hook/binding, keep input_bloodshield_v7's effect target outstanding with evidence/owner;
   do not tick whole V7 from thunk availability. No discipline implementation lane here.
2. **Content failure encountered during verification**: exact class/model/sequence/voice and
   baked provenance must be filed. Alternatives: use an already cooked body/voice proving the
   same runtime arm, or content owner repairs/rebakes. Do not schedule pipeline/bake work in V7,
   invent a silent asset or claim a timer proves real audio. None was measured by the planner.
3. **Later substrates**: V10 CSound lifetime and R1 producers/VSound; R3 real kick/door/cover
   mechanisms; R4 squad/follower locomotion; R5 additional incapacitation programs remain there.
   V7 proves setter and landed consumer contracts with real bodies/explicit fixtures; no claim
   those later systems are complete. Follower-type runtime row read needs no pipeline and is V7.
4. **Unrecovered input sources**: BCCTargetable producer/default, non-NPC victim ideal activity
   +0xff0, and player's unrelated+647c
   slot404 read. Keep named accessors answering nothing, test known arms through explicit debug
   fixtures. Alternatives: retail live measurement/recovery by integrator if readily available,
   or named later owner; never fill in invented values. HUD composed relation change is an
   explicitly INFERRED consistency repair of existing visual modernization, no new gameplay rule.

The judge list is bounded: no blanket “all19 need pipeline”, no moving wave-owned record failures
to a later spec. Retail mismatch DontFace, N5, N6, N11, mirror and movement are settled now.
