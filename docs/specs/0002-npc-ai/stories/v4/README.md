# V4 — the animation chain under the kernel: the design

Planner's design for `spec.md` § Step 2, V4 (was C2; size M in the spec). It has grown since the
draft: N13 (the slow walk), the corpse, the stealth-kill corpse, the facing, slot 363, the weighted
pick and the anim events. It is cut into a reading story and three sub-stories (V4r, V4a–V4c),
each testable at its close, plus one item for the adversarial judge. Written 2026-10-04 on
`spec-0002/step-2` at `9c4f4a20` with V3c's edits uncommitted in the tree. **V4 is not started.**
Paths are relative to `Source/ElysiumUE/Private/Substrate/` unless they say otherwise.

| story | briefs |
|---|---|
| V4r | `brief-R1-walk-reader.md`, `brief-R2-chain-reader.md`; `brief-J-judge.md` (the adversarial judge, after R2) |
| V4a | `brief-A0-seam.md`, `brief-A1-dispatcher.md`, `brief-A2-clock-words.md`, `brief-A3-view-cone.md`, `brief-A-integrator.md` |
| V4b | `brief-B1-body-speed.md`, `brief-B2-move-yaw-facing.md`, `brief-B-integrator.md` (B1/B2 are final only after R1's packet) |
| V4c | `brief-C1-attack-producers.md`, `brief-C2-pick-disposition-corpse.md`, `brief-C-integrator.md` (C1 final only after R2's packet; the corpse half after the judge) |

**Line numbers are today's and will move.** V3c is being integrated now, V3d deletes the arbiter,
the owner enum, `ThinkInDialog` and `RouteScheduleMaintenance` (hundreds of lines out of
`ElysiumNpc.cpp`), and T6b moves includes and splits headers. Every coder and integrator
**re-locates each site by Grep on the function name** before editing; a cited line is a hint, the
function name is the address. Nothing below depends on code V3d deletes.

**Provenance marks.** *(read)* the planner read the listing or the port body this session.
*(doc)* walked in `docs/vtmb/` and cited, not re-read in the listing. *(draft)* from
`consolidation/draft-2026-09-30.md` § C2 or the triage, not re-verified. *(record)* from an arena
record's `about`, not re-verified.

## 1. What retail does: the contract

**The frame.** `CAI_BaseNPC::PostRun 0x1026c7c0` *(read)*: `RunAnimation 0x1026c540` → slot 258
`DispatchAnimEvents(interval, this)` (`+0x408`) → `CBaseCombatCharacter::Weapon_FrameUpdate(interval)`,
in that order, inside the NPC's own think, after `RunAI`'s tasks. `RunAnimation` *(read)*: slot 250
`StudioFrameAdvance(0)`; a debug-flag zeroing of the interval when the navigator has no goal;
`AimGun` under `CAP_AIM_GUN`; then the idle re-pick — outside states 4 (SCRIPT) and 7 (DEAD), with
`m_IdealActivity == 1` and slot 251 true: **`m_bSequenceLoops +0x65d` false →
`SelectHeaviestSequence(m_Activity)`, true → `SelectWeightedSequence(m_Activity)`**, committed by
`0x10260a50` when not −1.

**The clock.** `StudioFrameAdvance 0x1008f120` *(read)*: re-seeds a zero `m_flPrevAnimTime`; a zero
interval becomes 0.1; advances `m_flCycle` by `GetSequenceCycleRate × m_flPlaybackRate × dt`; wraps
(looping) or clamps (non-looping) at the ends and sets `m_bSequenceFinished`; writes
`m_fSequencePastHalf +0x568`; then **every advance** writes `m_flYawSpeed +0x560 =
GetSequenceYawSpeed(m_nSequence)` and `m_flGroundSpeed +0x654 = GetSequenceGroundSpeed(m_nSequence)`;
`OnSequenceFinished` on the rising edge. `GetSequenceYawSpeed 0x10091310` *(read)* =
`GetSequenceTurnYaw / SequenceDuration` (0 for a zero duration); `GetSequenceTurnYaw` asks
`FUN_10428690` with the live pose parameters *(read; which movement field it returns: packet R1)*.
`GetSequenceGroundSpeed 0x10091490` = `GetSequenceMoveDist / SequenceDuration`, pose-weighted over a
blend fan, so **a fan's speed tracks the live `move_yaw` every tick** (`animation_and_movers.md`
§ "Scripted travel speed…" :1682-1688, § "One speed pipeline…" :1706) *(doc)*.
`GetIdealYawSpeed 0x100916a0` = `m_flYawSpeed` *(read)*; `GetIdealSpeed 0x10091740` = `+0x654`, no
playback-rate term *(doc)*. `ResetSequenceInfo 0x10090950` writes the same two words, playback rate
1.0, zeroes `m_flLastEventCheck`, and calls slot 247 *(doc; :1708-1716)*.

**The events.** Slot 258 on every NPC is `CBaseAnimatingOverlay::DispatchAnimEvents 0x10098c80`: the
base `0x10091880`, then the four overlay layers through `0x10098cd0` with their own cursors
(`animation_events.md` :41-102) *(doc)*. The base: window `[m_flLastEventCheck +0x658, m_flCycle +
0.1 × cycleRate)`, **0.1 s look-ahead**; `m_bSequenceFinished` cleared, then set when the look-ahead
end reaches 1.0 (non-looping: clamped to 1.0); `m_fSequencePastHalf`; the wrap swept once with
`STUDIO_LOOPING`; ids ≥ 5000 skipped; `+0x658` stores the look-ahead end; `OnSequenceFinished` on the
rising edge. So the finish an activity-waiting task reads is set by the dispatcher one look-ahead
early *(doc; the interplay with StudioFrameAdvance's own write and the task order: packet R2)*.
Each event goes to slot 259 `HandleAnimEvent`: base `0x10274e30`, Troika `0x1029b290`, species
bodies, `CBaseCombatCharacter::HandleAnimEvent 0x1032e330` (feed 4006/4007, 4020, ornaments),
and `3000..0xfa2` → `Weapon_HandleAnimEvent` → the weapon's `Operator_HandleAnimEvent +0x5c8`
(`conditions-and-states.md` :3504-3583, `animation_events.md` :207-240, :358-440) *(doc)*. The NPC's
ranged shot is the 3031 event, authored on 105 `move_and_ranged` sequences, through
`CWeaponRanged 0x10238160`; **no shipped sequence authors a melee commit** except the dog's bite on
3001, and 3047 (the NPC swing trigger) is authored nowhere (`combat-and-damage.md` :1027-1046)
*(doc)* — so where an NPC's melee contact comes from is packet R2's question.

**The attack extents.** Slot 247 `SetAttackExtentsForSequence 0x10090c80` *(read)*, called by
`ResetSequenceInfo`: only when `Flags2 & 4`; reads the sequence descriptor's bbox (`+0x1c..+0x30`),
takes the radial excess over the collision's maxs, and hands it to the entity's slot 15 (`+0x3c`).
*Unrecovered:* slot 15's name and which classes set `Flags2 & 4` (packet R2).

**Turning and the walk.** The facing-target queue (`shape.md` :419-457) and the yaw ladders (`shape.md`
:1473-1500) *(doc)*: `CAI_BaseNPCTroika::MaxYawSpeed 0x10297ce0` answers `|GetIdealYawSpeed()| ×
cvar` (floor 1.0) **only under `m_afMemory & 0x2000`** (the turn ladder's tag), otherwise
30/160/20/25/45 by activity. `FacingIdeal 0x10278c80` is `|DeltaIdealYaw| <= 0.006`. `CAI_Motor`
slot 18 `0x102e19e0` *(read)*: past the owner's `+0x838` test, if `m_nSequence` carries the
`move_yaw` pose parameter, the heading comes from slot 15 (the facing-queue average, `0x102e2180`),
the move is reissued at that quantised yaw, and `-(AngleDiff(heading, GetAbsAngles().y))` is written
to the owner's `m_flDesiredMoveYaw +0x63ec` (Troika) or as the `move_yaw` pose parameter. What slot
15 answers with an empty queue, and how the motor step consumes `GetIdealSpeed`
(`MoveGroundExecute 0x10264680` re-writes `+0x654` at `0x10264841`), is packet R1's question; retail
`walk_0` is 136.7 cm/s (triage N13) *(record)*.

**Slot 363 on the enemy.** `GatherEnemyConditions 0x10270b20` asks the enemy's combat character
`FInViewCone(this)` (`0x1027106c`): true → `ENEMY_FACING_ME 0x56`, else `BEHIND_ENEMY 0x57`
(ported, `ElysiumNpcBaseConditions2.cpp:503-513`) *(read)*. On the player that is the base
`CBaseCombatCharacter::FInViewCone 0x10326750` *(read)*: candidate slot 192 (`+0x300`, the point),
**this character's `m_flFieldOfView`**, the candidate's slot 29 (`+0x74`, its cone scalar), then
`FinViewCone3dNew 0x103264d0` (the 2-D body when the cvar at `0x10936f74` reads 2; it ships 3).
The Troika line writes `m_flFieldOfView +0x1574 = 0.2` at spawn (`0x10298de8`); the player's is 0.5
(`senses.md` :53-58) *(doc; the player's writer: packet R2)*.

**The weighted pick.** `SelectWeightedSequence 0x1008dc40`: candidates across the three model slots
(the include-shadowing rule), then `RandomInt(0, total−1)` walked by `weights[i] <= r`; all-zero →
uniform; none → −1; one → itself. `SelectHeaviestSequence 0x1008dd30`: strict max, first wins
(`activity_enum.md` :294-337) *(doc)*.

**The disposition change.** `SetDisposition 0x102c0f70` (387 bytes), gated on `m_bDisableAI
+0x6080`: `m_IdealActivity = 0xf1`, `m_nIdealSequence +0x5ccc` from `CDispositionTable::
GetTransitionAnim 0x100ed150` (`stance_trans_<old>_<n>_<new>_<n>`, then `_1_…_1`, then the new
disposition's `idle[stance]`), `ResetSequenceInfo` (`animation_and_movers.md` :1906-1909)
*(draft for the order; packet R2 walks the body)*.

**Death.** `lifecycle.md` § "The death chain, kill to corpse" :2642-2800 *(doc)*: slot 144
`Event_Killed` → `0x1032b9b0` → slot 301 `CreateCorpse 0x1032c0e0` → `BecomeClientRagdoll
0x10090180`, which **seeds the pose with `SelectWeightedSequence(ACT_DIERAGDOLL 0x21)`, `m_flCycle =
0`, `ResetSequenceInfo`**, makes the NPC non-solid, clears its think and hands the pose to physics;
the drawn body falls, the entity stays at the death spot. Base `DIE` plays no death animation (a
recovered negative). The stealth kill reaches the same chain through `0x10165d90`.

## 2. What the port does instead

| # | mechanism | port (today's line) | retail |
|---|---|---|---|
| M1 | slot 258 on the NPC chain is a counting stub; `PostRun` calls it | `ElysiumNpcBaseMotor.cpp:587-597` (`PostRun`, the call :595); stub `ElysiumAnimatingOverlaySlots.cpp:112` (`0x10098c80`), base `ElysiumAnimatingSlots.cpp:321` | `0x10098c80` / `0x10091880` inside `PostRun` |
| M2 | **events fire from the world tick's poll**, before every NPC thinks, reading the **visual pose layer's** clip phase, keyed by play id; no look-ahead, no `+0x658`, no finish / past-half writes, no `OnSequenceFinished` | `FElysiumEntityWorld::AdvanceAnimEvents` `ElysiumEntityWorld.cpp:2077` (called :1938, before `RunThinks` :1938-1943; loop :2091-2099); `FElysiumAnimating::AdvanceAnimEvents` `ElysiumAnimatingImpl.cpp:310-396` (phase :41-42, timeline :363, ≥5000 skip :370, `animevent` tap :373-377); rule `ElysiumAnimEvents.cpp:32-108` | §1 "The events" |
| M3 | `Weapon_FrameUpdate` is a counter | `ElysiumNpcBaseMotor.cpp:597` (`++PostRunWeaponUpdates`) | `0x1026c7c0` tail |
| M4 | `m_flGroundSpeed` / `m_flYawSpeed` never computed on the kernel; `GetIdealYawSpeed` answers 0, so the turning arm floors at 1.0 (10°/s: the 13.4 s `task_face_enemy`) | `GroundSpeedCm()` seam `ElysiumNpcPositions2.cpp:81`; `GetIdealYawSpeed` stub `ElysiumAnimatingSlots.cpp:185-190`; comment `ElysiumNpcBaseAnim.cpp:229-230`; ladder `ElysiumNpcMotor.cpp:208-226`; a test pinning the stub `Tests/ElysiumNpcKernelMotorTests.cpp:246` | `0x1008f120`, `0x10090950`, `0x100916a0` |
| M5 | the walk speed is the **body's own**: the visual fan read at the body's measured `move_yaw` (velocity heading − actor yaw, slewed 720°/s); the kernel's `move_yaw` goes to its own record only; `0x102e19e0` is verdicted "mechanism" | `CommandedTravelSpeed` `Visual/ElysiumNpcBody.cpp:335-342` → `MaxWalkSpeed` :475-482; `Visual/ElysiumAnimationDriver.cpp:597-612, 663-666`; `ElysiumLocomotionSample.cpp:21-74`; kernel write `ElysiumNpcThink.cpp:343-344` → `ElysiumCombatCharacterSlotBodies.cpp:134-146`; verdict `kernel_verdicts.tsv:1466` | `0x102e19e0`, `+0x654` at the live `move_yaw` |
| M6 | N13: the walk covers ~0.55–0.7 m/s against 136.7 cm/s. **Cause unread.** The lead (a body yawed ~90° off its path by a facing target) is doubtful: a plain patrol adds no facing target (callers only `ElysiumNpcThink.cpp:221-225` `MOVE_FACE_ENEMY`, `ElysiumNpcTroikaHelpers.cpp:701-714`, `ElysiumNpcThinkSpecies.cpp:222`) and an empty queue orients to movement (`Visual/ElysiumNpcBody.cpp:107`); the kernel's ideal yaw is copied from the body during a move (`ElysiumNpcBaseMotor.cpp:1560-1563`, a named divergence) | §1 "Turning and the walk"; packet R1 |
| M7 | slot 363 on the combat character is a counting stub answering false; the cone body exists but compares against a constant 0.2, not the observer's `m_flFieldOfView` | stub `ElysiumCombatCharacterSlots.cpp:841-846`; body `ElysiumNpcSenses.cpp:378-391` (`DefaultViewConeDot`); slot 362 `ElysiumCombatCharacterSlotBodies.cpp:167-174`; the NPC's FOV word `ElysiumNpcLifecycle2.inl:118`, `ElysiumNpcSpawn.inl:92` | `0x10326750` |
| M8 | the sequence bridge: one clip per activity (`Variant = 0`, cached), so `SelectWeightedSequence` never draws; the loop bit is a guess (everything loops but the cine's `Play`), though the bake carries `STUDIO_LOOPING` | `SequenceForActivity` `ElysiumNpcAnim.cpp:375-391`; rows `SequenceRowFor` :321-340; `PlaySequenceClip` :394; loop guess `ElysiumNpcBaseAnim.cpp:65-75`; call `ElysiumNpcBaseStartTask.cpp:~400` | `0x1008dc40`, `0x1008dd30`; divergence row 4 (`stories/v1/divergences.md`) |
| M9 | `SetDisposition` plays the cross-disposition transition **directly on the body** (`PlayNpcClip`) or `ResetAnimToIdle`; no `m_IdealActivity` / `m_nIdealSequence` write | `FElysiumNpc::SetDisposition` `ElysiumNpc.cpp:1107-1177` | `0x102c0f70` |
| M10 | slot 247 a counting stub; the sequence bbox is not carried to the runtime | `ElysiumAnimatingSlots.cpp:228-232`, caller `ElysiumNpcBaseAnim.cpp:135-138` | `0x10090c80` |
| M11 | the NPC shot commits either from the 3030–3044 event **or from a `ContactEventCycle` estimate timer (0.5 of the clip)** when the clip has none; the melee contact is swept by the world interaction tick | `ElysiumWeaponClasses.h:303` (`ContactEventCycle`), `ElysiumWeaponClasses.cpp:1263-1317, 2166, 2257, 2361`; swing start `ElysiumNpcStartTask.cpp:557, 852-862`; sweep `AdvanceMeleeSwings` `ElysiumEntityWorldInteraction.cpp:299-326`, `AdvanceSwingContact` / `MeleeContact` (`ElysiumWeaponClasses.cpp:2503, 2842`) | 3031 → `0x10238160`; the melee contact: packet R2 |
| M12 | the corpse: the chain reaches `CreateCorpse` → `BecomeClientRagdoll` (`ElysiumNpc.cpp:285-311`), but **no `UPhysicsAsset` is baked for any character**, so `StartBodyRagdoll` refuses and `HoldBodyFinalPose` freezes the current standing frame; the `ACT_DIERAGDOLL` seed is unported (`ElysiumNpc.cpp:307`) | `ElysiumNpcBase.cpp:107`; refusal `ElysiumEntityBodies.cpp:1513-1529`; hold :1569; bake `pipeline/.../physics_data.py:5`, `ElysiumWorldServices.h:930` | `0x10090180`; the fall is 0014's (stories 1–3, 5) |
| M13 | the `on_ground` probe reads the motor capsule's `FindFloor`, and a dead body's actor collision is off (`SetActorEnableCollision(false)`, re-applied every dead think), so it reads **false for every corpse by construction** — a harness fault, not the game's | `Debug/ElysiumArenaScenarioRunner.cpp:667-680` → `ElysiumNpcBodyGeometry.cpp:120-125`; `Visual/ElysiumNpcBody.cpp:1250-1261, 1313`; `ElysiumNpc.cpp:646-649` | — |
| M14 | other counting stubs on the chain: `GetVelocity`, `BurnModel`, `AddExtraAnimationModels`, `SetPoseParameter02`, `GetGroundSpeedVelocity` | `ElysiumAnimatingSlots.cpp:168, 194, 211, 329, 357` | — |

**Already retail — reuse, do not rewrite.** `StudioFrameAdvance` (`ElysiumNpcBaseAnim.cpp:186-238`,
minus the two speed words and the overlay layers), `ResetSequenceInfo` (:85-140),
`IsActivityFinished` (:344-349), `RunAnimation`'s call order (:151-182, the pick still to port);
`HandleAnimEvent` base `ElysiumNpcBaseMisc2.cpp:204-431`, Troika `ElysiumNpcMisc2.cpp:385-594`,
species `ElysiumNpcMisc2Species.cpp:192-572`, combat character `ElysiumCombatCharacter.cpp:2104`,
weapon `ElysiumWeaponClasses.cpp:1349`; the facing queue, its blend and `FacingIdeal`
(`ElysiumNpcBaseFacing.cpp:36-188, 385-393`); `MotorUpdateYaw` (`ElysiumNpcBaseRunTask.cpp:183-209`);
the death chain through `CreateCorpse` (`ElysiumNpcSpawn.cpp:26`, `ElysiumNpcBaseSpawn.cpp:44-112`,
`ElysiumCombatCharacter.cpp:1429-1507`); `GatherEnemyConditions`' slot-363 call site.
**The bake already carries what V4 needs** except the bbox: events (`UElysiumClipData::Events`,
`ElysiumClipData.h:35`), activity and `actweight` (`FElysiumBodySequence`, `ElysiumBodyData.h:33-35`),
`STUDIO_LOOPING` (`ElysiumNpcClips.h:147`), ground speed and the fan
(`ElysiumClipData.h:38-40`, `Public/Visual/ElysiumBlendGrids.h:98-131`), the turn yaw
(`ElysiumClipMovement.h:56`, `YawDegrees`); a per-model sequence table (`UElysiumBodyData::Sequences`
→ `FElysiumNpcClipSet`, `ElysiumBodyData.cpp:21-29`, `ElysiumAnimSubsystem.cpp:234`); a retail-shaped
draw (`PickWeighted`, `ElysiumAnimationResolve.cpp:703-748`, hash-seeded). V4 is runtime work, except
§8 Q3 and Q4.

## 3. The seam (method step 1) — one commit, opening V4a

Written and built by one agent (`brief-A0-seam.md`); changes no behaviour; every V4 record stays red.

| stub | retail | admitting default | stands for |
|---|---|---|---|
| `float FElysiumNpcBase::LastEventCheck` | `m_flLastEventCheck +0x658` | 0; zeroed where `ResetSequenceInfo` zeroes it (that write lands in A2) | the play-id cursor of the world poll |
| `float YawSpeed`, `float GroundSpeed` on `FElysiumNpcBase`; `GroundSpeedCm()` reads `GroundSpeed` | `m_flYawSpeed +0x560`, `m_flGroundSpeed +0x654` | 0 (today's answer) | `GroundSpeedCm()`'s seam `ElysiumNpcPositions2.cpp:81` |
| `bool SequencePastHalf` (if not already a word) | `m_fSequencePastHalf +0x568` | false | the named gap in `StudioFrameAdvance` |
| the bridge row's accessors `SequenceEvents(int32)`, `SequenceLoops(int32)` (exists as `bLoops`), `SequenceTurnYaw(int32)`, `SequenceGroundSpeedAt(int32, poseParams)` | the studio descriptor: events, `flags & 1`, `GetSequenceTurnYaw`, `GetSequenceGroundSpeed` | empty / today's loop guess / 0 / 0 | the clip resolver's metadata the bake already carries |
| `FElysiumNpcBase::DispatchAnimEvents(float, FElysiumEntity*) override` in a new `ElysiumNpcBaseAnimEvents.cpp` | slot 258 `0x10098c80` on the NPC chain | forwards to today's stub | M1 |
| `FElysiumCombatCharacter` gains `m_flFieldOfView +0x1574` (moved from the NPC side if it lives there; NPC spawn keeps writing 0.2) | `CBaseCombatCharacter +0x1574` | NPC 0.2 as today; the player's value as packet R2 reads it, **written in A3** | the constant `DefaultViewConeDot` |
| shape-map rows `0x658`, `0x560`, `0x654`, `0x568`, `0x1574` | — | — | `ElysiumNpcKernelShapeMap.cpp` |
| harness **H18** probes `speed2d` (cm/s, the body's horizontal speed), `move_yaw` (the body's, degrees), `ground_speed` (the kernel's `+0x654`, cm/s) | — | — | N13's direct acceptance |
| harness **H19** `elysium_entity_get` adds, per NPC: facing-queue count and target, orient-to-movement on/off, `MaxWalkSpeed`, the kernel's `m_flDesiredMoveYaw` / `move_yaw` pose value and `+0x654` | — | — | the N13 diagnosis behind R1's run |
| harness **H20** probe `corpse_on_floor`: the drawn mesh's pelvis (`Bip01 Pelvis`) within 24 cm of the floor under it, read from the skeletal mesh, not the capsule; `on_ground` documented as the motor capsule (meaningless after death) | — | — | M13 |
| records (below) | — | — | — |

Records the seam writes or corrects (the integrator owns `Arena/` edits after the seam):
- **new** `world/anim_footsteps_walk.json`: sentry2 walking a two-point patrol (the
  `patrol_sentry2_pingpong` staging); `animevent` `2050` then `2051` on sentry2 during the leg (the
  walk clips carry them, `animation_events.md` :185-188), `never animevent` with an id ≥ 5000 on
  the NPC. A guard: may pass today through the poll; it must still pass once the poll is retired
  for NPCs.
- **new** `combat/face_enemy_turn.json`, red: a hostile Troika human, the player placed ~135° behind
  its facing, in combat; `task_face_enemy` then its `taskdone` within the bound packet R1 computes
  from `MaxYawSpeed 0x10297ce0` and the turn clips' yaw speed (the 13.4 s case). `known_red` "V4b".
  The seam agent leaves the bound as R1 wrote it in `packets.md`.
- **corrected (record error, bug protocol step 1)** `damage_lethal_death`, `verbs_stealth_kill`:
  the end probe `on_ground` → H20 `corpse_on_floor`; add `corpse` `match: "ragdoll"`. `known_red`
  "0014 (no character physics asset; see `stories/v4/README.md` §8 Q3)" until the judge rules.
- `known_red` retargeted: `sense_enemy_facing_me` → "V4a"; `ranged_open_fire` → "V4a (slot 363),
  then V5 (N2)"; the three patrols and `places_pedestrian_visit` → "V4b (N13)"; `melee_swing` →
  "V11 (N3); its hit half V4c". No expectation changed.

## 4. The cut

V4 cannot fit one story under rule 8: it needs eight coder lanes and two reading packets. Cut by
dependency — the clock and the frame advance with the events first; then the walk, which needs the
clock's ground speed; then attack and death, which need the events:

| story | size | agents | builds planned (allowed) | records turned green | stay red, on |
|---|---|---|---|---|---|
| **V4r** reading | S–M | R1, R2 readers (+ the judge, one agent, after R2) = 3 | 0 (R1 runs one lab session on the existing build) | — (packets into `docs/vtmb/`) | — |
| **V4a** seam; the clock's speed words; the dispatcher in `PostRun`; slot 363 | M | seam agent, A1, A2, A3, integrator = 5 | 2 (2) | `sense_enemy_facing_me`; `anim_footsteps_walk` (guard); `cover`, `control_sequence` stay green | `ranged_open_fire` → V5 (N2) once its `shot_event` is met |
| **V4b** the walk and the turn (N13) | S–M | B1, B2, integrator = 3 | 1 (2) | `patrol_sentry2_pingpong`, `patrol_monk_loop`, `input_clearpatrolpath`, `places_pedestrian_visit`, `face_enemy_turn` | — if R1 finds the cause in baked fan data: judge (§8 Q2) |
| **V4c** attack producers, weapon frame; weighted pick, disposition, the corpse seed | M | C1, C2, integrator = 3 | 1 (2) | `damage_lethal_death`, `verbs_stealth_kill` **only if the judge re-cuts the fall to 0014** (§8 Q3) | `melee_swing` → V11 (N3); `chase_melee` → V5/V11; the fall → 0014 |

Order V4r → V4a → V4b → V4c. 14 agents; 4 builds planned, 6 allowed. V4b and V4c could swap (V4c
needs only V4a); the walk goes first because four records and V8's hub wait on it.

**V4a — the frame.** A1: the base dispatcher `0x10091880` and the overlay wrapper `0x10098c80` as
the NPC's slot 258, on the kernel's words (`m_nSequence`, `m_flCycle`, cycle rate × playback rate,
`+0x658`, `m_bSequenceLoops`), the event table from the bridge row, the look-ahead, the finish and
past-half writes, `OnSequenceFinished`, each event to slot 259 in order, the `animevent` tap moved
here; the world poll skips NPC entities (the player, props and the camera keep it, §7 K3); the four
overlay layers a seam answering "no layer" named for `CAnimationLayer` (R2 says whether any step-2
path pushes one). A2: `StudioFrameAdvance` and `ResetSequenceInfo` write `+0x560` / `+0x654` from the
row (turn yaw / duration; ground speed at the kernel's live pose parameters, the fan where the row
has one); `ResetSequenceInfo` zeroes `+0x658`; `GetIdealYawSpeed` / `GetIdealSpeed` as plain reads;
the row's loop bit from the baked `STUDIO_LOOPING` (K2's residue in V3 closed). A3: slot 363 as
`0x10326750` over the observer's `m_flFieldOfView`, the candidate's slot 192 point and slot 29
scalar; the player's FOV written where retail writes it.
- A1: `ElysiumNpcBaseAnimEvents.cpp` (new, from the seam), `ElysiumAnimEvents.{h,cpp}`,
  `ElysiumAnimatingImpl.cpp`, `ElysiumEntityWorld.cpp` (the poll's entity filter only), new
  `Tests/ElysiumNpcKernelAnimEventsTests.cpp`.
- A2: `ElysiumNpcBaseAnim.{cpp,inl}`, `ElysiumAnimatingSlots.cpp` (slots 242, 248 only),
  `ElysiumNpcAnim.cpp` (the row accessors), `ElysiumNpcPositions2.cpp`,
  `Tests/ElysiumNpcKernelAnimTests.cpp`, `Tests/ElysiumNpcKernelMotorTests.cpp` (the `:246` stub
  assertion).
- A3: `ElysiumCombatCharacterSlots.cpp` (slot 363 only), `ElysiumCombatCharacterSlotBodies.cpp`,
  `ElysiumNpcSenses.{h,cpp}` (the cone body's threshold), the player's spawn file R2 names
  (expected `ElysiumPlayerEntity.cpp`), new `Tests/ElysiumCombatCharacterConeTests.cpp`.

**V4b — the walk and the turn.** Written as far as is honest before R1 (`brief-B1`, `brief-B2`):
B2 ports `0x102e19e0`'s `move_yaw` half on the kernel (heading from slot 15 vs the body's yaw →
`m_flDesiredMoveYaw` / the pose parameter) and the facing pieces R1 names; B1 makes the body's
commanded speed the kernel's `GetIdealSpeed` (the motor-on-the-body divergence keeps retail's
input) and removes whatever R1 measures as the 0.44×. With `+0x560` real (V4a), `task_face_enemy`
turns at the turn clips' yaw speed under the `0x2000` tag.
- B1: `Visual/ElysiumNpcBody.cpp`, `Visual/ElysiumAnimationDriver.cpp`,
  `Visual/ElysiumLocomotionSample.cpp`, the embodiment accessor for the kernel's ideal speed.
- B2: `ElysiumNpcMotor10.{cpp,inl}`, `ElysiumNpcBaseFacing.cpp`, `ElysiumNpcBaseMotor.cpp` (the
  move's ideal-yaw copy, `:1560-1563`), `ElysiumNpcThink.cpp` (the `move_yaw` write, `:343-344`),
  `Tests/ElysiumNpcKernelFacingTests.cpp`, `Tests/ElysiumNpcKernelMotorTests.cpp`.

**V4c — attack, the pick, death.** C1 (final after R2): `Weapon_FrameUpdate` in `PostRun` as
retail runs it; the NPC shot only from the 3031 event (the `ContactEventCycle` estimate removed on
the NPC path unless R2 finds a retail fallback); the melee contact where R2 places it; slot 247 if
R2 shows it live on any NPC (else it stays a named seam, §8 Q4). C2: `SelectWeightedSequence` /
`SelectHeaviestSequence` on the kernel over the body's sequence table, drawn on the `NpcSchedule`
stream, with the include-shadowing rule; `RunAnimation`'s loop-bit fork; `StartTaskSlot442`'s draw;
`SetDisposition 0x102c0f70`'s body through `m_IdealActivity` / `m_nIdealSequence` /
`ResetSequenceInfo` (the transition by name through V3c's `LookupSequence`); `BecomeClientRagdoll`'s
`ACT_DIERAGDOLL` seed. The fall waits on the judge (§8 Q3).
- C1: `ElysiumNpcBaseMotor.cpp` (`PostRun`'s weapon line), `ElysiumWeaponClasses.{h,cpp}`,
  `ElysiumEntityWorldInteraction.cpp`, `ElysiumNpcStartTask.cpp` (the attack arms R2 names),
  `ElysiumAnimatingSlots.cpp` (slot 247 only), `Tests/ElysiumWeaponTests.cpp`.
- C2: `ElysiumNpcAnim.cpp`, `ElysiumNpcBaseAnim.cpp` (`RunAnimation`), `ElysiumNpcBaseStartTask.cpp`
  (the slot-442 draw), `ElysiumNpc.cpp` (`SetDisposition`, `BecomeClientRagdoll`),
  `Tests/ElysiumNpcKernelAnimTests.cpp`, `Tests/ElysiumNpcCombatTests.cpp` (`NpcCombat.Death`'s
  port assertions).

## 5. Reading packets (method step 4) — V4r, before any coder

| packet | what must be recovered | size | feeds |
|---|---|---|---|
| **R1** the walk | (1) slot 18 `0x102e19e0` (read above) with slot 15 `0x102e2180` on an **empty** queue: what heading it answers; who calls slot 18 (`CAI_HumanoidMotor` vfunc19 `0x10264680`, `MoveGroundExecute`) and the second `+0x654` write `0x10264841/46`; how the step turns `GetIdealSpeed` into distance (`MoveGroundStep 0x102e1760`); the readers of `m_flDesiredMoveYaw +0x63ec`. (2) `FUN_10428690`: which movement field `GetSequenceTurnYaw` returns (is it the baked `YawDegrees`?). (3) `face_enemy_turn`'s bound: the activity during `TASK_FACE_ENEMY` in combat, the turn ladder `0x10297640`'s pick and tag, the turn clip's yaw speed on `regular_cop`, the resulting seconds. (4) **One diagnostic lab session** (`uv run elysium gr --arena --headless`, `elysium.gr_scenario patrol_sentry2_pingpong`): `elysium_entity_get sentry2` ≥ 5 samples mid-leg — `speed2d`, `facing_yaw`, `locomotion.move_yaw_vel`, `move_yaw_wish`, `animation`, `axis_fraction` — and the walk fan's cells for sentry2's model. Verdict on the lead: `move_yaw_vel ≈ ±90` with `speed2d ≈ 60` holds it; `≈ 0` with `≈ 60` refutes it and points at the fan's cells or scale | ~1.5 KB of listing + one session | B1, B2, the seam's `face_enemy_turn` |
| **R2** the chain | (1) `0x10098c80` / `0x10098cd0`: order base → layers, the finish / past-half writes against `StudioFrameAdvance`'s, `OnSequenceFinished`'s slot; with `NPCThink`'s order (tasks, then `PostRun`), which finish value slot 251 reads. (2) `Weapon_FrameUpdate`: what it does for an NPC. (3) The NPC shot: `TASK_RANGE_ATTACK1`'s arms → the activity → 3031 → `CWeaponRanged::Operator_HandleAnimEvent 0x10238160` → the fire body; any retail path that fires without the event. The NPC melee: how `TASK_MELEE_ATTACK1` starts a swing (3047 is authored nowhere) and where the per-frame contact sweep runs; whether `melee_swing`'s `hit_event` (`animevent` within 3 s of the swing) is retail — a record error if no event is authored on the swing. (4) Who pushes an NPC overlay layer on the step-2 records' paths. (5) `0x10090c80`'s slot 15 (`+0x3c`) and the writers of `Flags2 & 4`. (6) Slot 363's inputs: slot 192 and slot 29 on an NPC candidate; the player's `m_flFieldOfView` writer and value. (7) `SetDisposition 0x102c0f70` arm by arm; `RunAnimation 0x1026c540`'s pick (read here) into the doc. (8) `AutoMovement`'s second `StudioFrameAdvance(0)` (`ElysiumNpcBaseMotor.cpp:564`): retail or not | ~3 KB | A1, A2, A3, C1, C2 |

Everything else V4 ports is walked in `docs/vtmb/` (§1). Then **the judge** (`brief-J-judge.md`)
rules on §8 Q3 (the corpse fall) and, if R2 shows slot 247 live, Q4 (the bbox) — before V4a's seam,
because the seam writes the death records' `known_red`.

## 6. Tests

Deleted, not converted (port-only, stub or seam tests), each by the lane that owns its file:

| test | file | why | lane |
|---|---|---|---|
| the assertion "`GetIdealYawSpeed()` answers 0, so it lands on 1.0" | `ElysiumNpcKernelMotorTests.cpp:246` | pins a stub | A2 |
| `NpcCombat.Death`'s `StartBodyRagdoll → 0` / `HoldBodyFinalPose` / "no `PlayNpcClip`" assertions (the rest of it, if anything retail remains, stays) | `ElysiumNpcCombatTests.cpp:1629-1661` | pins the no-rig mechanism | C2 |
| any test of `ElysiumAnimEvents::Advance` that names an NPC (the rule stays for the player and props) | A1 audits `Tests/` by Grep | pins the poll | A1 |
| any test pinning one clip per activity on the bridge | C2 audits by Grep on `SequenceForActivity` | pins the divergence | C2 |
| slot 258 / 247 / 363 stub-count assertions (`FireAnimatingOverlaySlot` / `FireCombatCharacterSlot` counts) | the closure / dispatch tests, by Grep | pins a stub; the dispatch-table rows (slot → body name) stay | A1, A3, C1 |

Replaced by the records of §4 and these arm tests, each pinning a retail address:
`Elysium.Arm.NpcKernelAnimEvents.Window` (`0x10091880`: closed-bottom open-top, look-ahead 0.1 ×
rate, the wrap once with `STUDIO_LOOPING` and lost without it, ≥ 5000 skipped, 1.0 never fires on a
non-looping clip, `ResetSequenceInfo` restarts at 0, finish and past-half written, `OnSequenceFinished`
once); `.PostRunOrder` (`0x1026c7c0`: `RunAnimation`, slot 258, `Weapon_FrameUpdate`);
`Elysium.Arm.NpcKernelAnim.SpeedWords` (`0x1008f120` / `0x10090950` write `+0x560` and `+0x654`;
`0x100916a0` / `0x10091740` plain reads); `.WeightedPick` and `.HeaviestPick` (`0x1008dc40`,
`0x1008dd30`); `.RunAnimationPick` (`0x1026c540`'s loop-bit fork); `.DieRagdollSeed` (`0x10090180`);
`.SetDisposition` (`0x102c0f70`); `Elysium.Arm.CombatCharacter.FInViewCone` (`0x10326750` with the
player's FOV and the NPC's scalar); `Elysium.Arm.NpcKernelMotor.MoveYaw` (`0x102e19e0`).

## 7. Kept divergences (rule 2), for the owner

- **K1 (existing, kept): the motor runs on the body's tick** under Unreal's movement component
  (0019/6). After V4b its contract is retail's: the speed input is the kernel's `GetIdealSpeed`
  (`+0x654` at the kernel's `move_yaw`), the heading the kernel's; the event order is untouched
  because the motor fires no kernel event.
- **K2 (existing, kept, named modernization): the clip resolver stands for the studio sequence
  table**, and events come from the baked table, never from Unreal notifies. V4 narrows it: the
  kernel's sequence rows carry the baked flags, weights, events and speeds, so only the asset
  lookup remains a divergence.
- **K3 (new, recorded only, not adopted): the world poll stays for non-NPC entities** (the player's
  4050/4051 camera band, props, the camera) reading the visual phase. Retail runs their dispatchers
  in their own think; V4 removes it only for NPCs. Recorded for the player's / props' owners.
- **K4 (new, recorded only): the visual side's `PickWeighted` hash seed**
  (`ElysiumAnimationResolve.cpp:723`) for non-kernel picks; the kernel draws on `NpcSchedule` (C2).
- **K5 (new, recorded only, for the judge): the rigless corpse.** Until 0014's physics asset exists,
  `HoldBodyFinalPose` holds the `ACT_DIERAGDOLL` seed pose (C2) instead of falling. Retail cannot be
  followed without a rig: no `UPhysicsAsset` is baked (`pipeline/.../physics_data.py:5`).
- Not V4's, named: anim-event sounds (`EmitSoundScriptMisc19` appends to a list,
  `ElysiumNpcBaseMisc2.cpp:198-202`), the 2042–2044 weapon-model counters (`:390-411`), the 2020
  bone controller — audio and weapon-model stories; the four overlay layers (a seam, A1).

## 8. Risks and open questions, each with a recommendation

- **Q1. The cut** into V4r + V4a–c. *Recommend accept*: each closes on its own records; rule 8.
- **Q2. N13's cause is unknown** (§2 M6). R1's session settles it before B1/B2 are final. If the
  cause is the baked fan (cells, units, symmetrization in `ElysiumBlendGrids`), the fix is pipeline
  + a character re-import: **for the adversarial judge** — cost, from the recorded logs: a full
  character import 1,434 s (1,207 entries, ~17.7k assets,
  `$ELYSIUM_WORK_ROOT/logs/20260928T205853.840641Z-import-characters.json`), a partial ~1,172 s.
  *Recommend*: if the fan is wrong, implement now (four records and V8's hub depend on the walk).
- **Q3. The corpse fall needs 0014 (stories 1–3 calibrations and the physics-asset bake, 5 the
  handoff: M + M + L + XS).** This is a planning bug by the bug protocol: `damage_lethal_death` and
  `verbs_stealth_kill` (and V8's stealth kill) assert a corpse on the floor that step 2 cannot
  produce. **For the adversarial judge.** Options: (a) pull 0014/1–3 + 5 forward as V4d — pipeline
  work (the `.phy` is decoded, `model_glb/physics.py`; no `UPhysicsAsset` builder exists), two
  calibrations, a full character re-import (~24 min), one build, and the ragdoll's own collision at
  the death handoff; (b) re-cut the two records to what step 2 owns — the transaction (`death`,
  `OnDeath`, `corpse ragdoll`, the `ACT_DIERAGDOLL` seed `sequence`, nothing after death) — and move
  `corpse_on_floor` to 0014's witness. *Recommend (b)*: the fall is the visual half ("VtMB owns the
  rules, Chaos owns the solve", 0014 § Witness); nothing the bytecode observes depends on where the
  drawn body rests (retail's entity stays at the death spot). Stated to the owner either way.
- **Q4. Slot 247 needs the sequence bbox**, which the bake drops (`clip_data.py:13-25`). If R2 shows
  `Flags2 & 4` set on any NPC class, **for the judge**: the bbox on the `UElysiumBodyData` row
  (re-authors the `DA_` assets; unverified whether that avoids re-cooking the animation packages) or
  in `clip_data.py` (changes the animation fingerprint: full 1,434 s re-import). *Recommend*: leave
  the named seam unless a step-2 record observes a hit the extents decide.
- **Q5. The finish moves one look-ahead earlier.** With the dispatcher writing `m_bSequenceFinished`,
  every activity-waiting task completes ~0.1 s × rate earlier — retail's timing. Records with tight
  windows may move. *Recommend*: the V4a integrator runs the whole arena at close; a moved verdict is
  triaged under the bug protocol, never loosened.
- **Q6. Removing the `ContactEventCycle` estimate** silences an NPC shot on a clip with no 3031.
  That is retail (only `move_and_ranged` carries it) if R2 finds no fallback. *Recommend* C1 logs
  once per (model, sequence) an NPC attack clip with no fire event, so a silent gunman is visible.
- **Q7. `melee_swing`'s `hit_event`** may be a record error (no authored melee commit, §1). R2
  settles; the V4c integrator corrects the record with the retail source if so. The record stays red
  on N3 (V11) regardless; the spec's "melee-and-die" scenario cannot close in V4.
- **Q8. `ElysiumNpc.cpp`** is touched by C2 only within V4; V3d and T6b move it first — re-locate.
- **Q9. Save words.** `+0x658`, `+0x560`, `+0x654`, `+0x568` are datamap words retail saves. V4 does
  not touch the save walk; V6 (resume) adds them with the cursor.

## 9. Harness gaps V4 needs

- **H18** (seam): `speed2d`, `move_yaw`, `ground_speed` probes — N13's acceptance becomes direct
  (`speed2d` within 10 % of retail's 136.7 cm/s mid-leg on a straight patrol leg).
- **H19** (seam): `elysium_entity_get`'s facing / speed fields (§3) — what R1's session lacked.
- **H20** (seam): `corpse_on_floor` read from the drawn mesh; `on_ground` documented as the capsule.
- An anim-event tap check needs no new kind: `animevent` keeps its text (`<id> <options>`) and moves
  to the kernel dispatcher (A1); `anim_footsteps_walk` guards it.
- Numbers H18–H20 follow H17 (the arena seed door); take the next free numbers if others land first.
- Not needed: H6, H8 (V6), H13 (V8).

## 10. Boundaries — named, not designed here

- **V5**: `GatherAttackConditions`' clears, red 4, N1, N2 (`ranged_open_fire`'s wait). V4a only
  gets the record past `BEHIND_ENEMY` and its `shot_event`.
- **V6**: the save walk of V4's words and the resume; `OnTakeDamage_Alive`'s last-damage record.
- **V7**: the inputs, including the playback/speed-scalar input `0x102c3580`
  (`SetPlaybackAndSpeedScalar 0x1008d230`) — V4 reads `m_flGroundSpeedScalar` as 1.0.
- **V11**: the attack coordinator (N3); `chase_melee`, `melee_swing`, "melee-and-die".
- **0014**: the rig, the fall, the death impulse, the corpse volume (Q3). **0005**: the death family.
- **R3**: cover and the goal selectors. **Audio**: anim-event sounds.

## Rules for every agent of V4

- Read `CLAUDE.md`, `spec.md` § Standing rules, § The method per story, § The bug protocol and the
  owner's rulings, this README, and your brief. `Arena/README.md` if you touch records or the runner.
- **Re-locate every cited site by Grep on its function name**: V3c, V3d and T6b move lines.
- **Query budget**: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops (never
  retried as-is or widened). Never read a file over ~200 KB whole. Look an address up with
  `uv run elysium research where <addr>` / `research section` before searching `docs/`.
- Text through the built-in **Grep / Read / Glob** tools, never shell `grep`/`cat`/`sed`.
- **Wait on a background command by its completion notification**, never a sleep or polling loop.
- **Coders never build**, never launch the editor, never run the arena or a suite; a coder may run
  only the single Python test file of a module it changed. Touch only your brief's files; a line
  another file needs goes in your report, exact, with its place.
- No record under `Arena/scenarios/` is edited by a coder; the seam agent and the integrator do.
- Follow the listing; cite the address at every line you port. A retail input with no source yet is
  a seam answering "nothing", named for the retail field. Divergences only as named in §7; a new one
  is recorded in your report, not adopted.
- No new test of a port mechanism. Do not commit (the seam agent and the integrators commit once
  each, on `spec-0002/step-2`, never push). Report ≤300 words.

## Shared names (fixed here so lanes agree)

- `FElysiumNpcBase::LastEventCheck` (`+0x658`), `YawSpeed` (`+0x560`), `GroundSpeed` (`+0x654`),
  `SequencePastHalf` (`+0x568`); `FElysiumCombatCharacter::FieldOfView` (`+0x1574`).
- The bridge row accessors on `FElysiumNpc`: `SequenceEvents(int32 Seq)` →
  `TConstArrayView<FElysiumAnimEvent>`, `SequenceLoops(int32)`, `SequenceTurnYaw(int32)` (degrees),
  `SequenceGroundSpeedAt(int32 Seq, poseParams)` (cm/s).
- `FElysiumNpcBase::DispatchAnimEvents(float Interval, FElysiumEntity* Handler)` — slot 258.
- `FElysiumNpc::SelectWeightedSequence(int32 Activity)` / `SelectHeaviestSequence(int32 Activity)` —
  the kernel's draws (C2); the `NpcSchedule` stream.
- Trace kinds unchanged; `animevent` text `<id> <options>`.
