# Brief C1 — V4c: attack producers, swing movement and contact (coder; no build)

Final, 2026-10-04, against V11 `88649932` and V4o `64895278`. Read `AGENTS.md` first,
`HANDOVER.md`, README § "Rules for every agent of V4" / "Shared names", S2 items 1/7/9,
S5 item 3 (D1–D11), S11 item 2, S12, and **S13 §§2/4 / Changes to the plan**. S13 and the
owner's rulings supersede older packet recommendations. Locate port sites by function name.
V11 already ported D1–D8/D10/D11 and slot 331; V4o already ported layers and the event shot.
Your contact changes are the sweep/D9 and the settled team predicate. `chase_melee` is your red.

## Files (only these)

Prefix `Source/ElysiumUE/Private/Substrate/` unless stated; braces expand to separate files.

- `ElysiumNpcBaseMotor.{cpp,inl}` — PostRun, AnimIntervalMovement, AutoMovement only.
- `ElysiumWeaponClasses.{h,cpp}` — estimate, triggers, swing start/clock/endpoints, team filter,
  plus C2's K4 request patch, applied by you as owner.
- `ElysiumEntityWorldInteraction.cpp` — AdvanceMeleeSwings definition removal only.
- `ElysiumNpcStartTask.cpp` — melee start arms only.
- `ElysiumNpcThink.{cpp,inl}` — UpdateCharacterRetail / slot-312 stand-in only.
- `ElysiumCombatCharacterSlotBodies.cpp` — slot 315 hand body.
- `ElysiumAnimatingSlotBodies.cpp` — slot 247 hand body.
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelMotorTests.cpp`.
- `Source/ElysiumUE/Private/Tests/ElysiumWeaponTests.cpp`.
- New `Source/ElysiumUE/Private/Tests/ElysiumNpcAttackExtentsTests.cpp`.
- `docs/vtmb/combat-and-damage.md` — matching attack/contact sections and S13's bounded
  fifth-hit attribution only; look up the address then read the section, never the whole file.

C1 ∩ C2 = C1 ∩ C3 = ∅. C2 owns SequenceBounds, damage and player hooks; C3 owns team state.
No generated slots, pipeline/body data, Arena, player main file or combat-character main file.
C2 reports its exact BuildActivityClipRequest patch; you apply it, or explicitly owe it to the
integrator if the report arrives after yours. No concurrent second writer.

## The job

1. **Weapon frame `0x1032aa40` → `0x1024efa0`.** In
   `ElysiumNpcBaseMotor.cpp::FElysiumNpcBase::PostRun`, after slot 258, replace the weapon counter
   with the active weapon's frame call. In `ElysiumWeaponClasses.cpp`, supply the weapon-model
   clock: slot 250(0); finished && loops → weighted pick of its activity, commit/reset on a
   nonnegative answer; slot 258(interval,wielder), delivering weapon events to the wielder.
   No active weapon → nothing; no shot or sweep here. Draw through C2's shared animation picker.
   Keep PostRunWeaponUpdates beside the real call (other files' tests read it). If the weapon
   model has no sequence words, name that input seam at the line and report the exact clock
   words/accessor owed to 0015; do not fabricate a clock.
2. **Delete the NPC shot estimate `0x10238160` → `0x10238320` → `0x102383b0` → Shot
   `0x102387b0`.** In `ElysiumWeaponClasses.cpp::BeginRangedShot`,
   `CommitArrivesFromAnimEvent` and ContactEventCycle's commit-time path, an NPC stages no
   timed transaction or UpperBody stand-in. Keep the player's estimate. Preserve landed
   ShotFromAnimEvent's gates, +0x730 stamp and no NPC clip spend (J12). All eight slot-370 bodies
   are settled: ranged `0x10238160`, base `0x1024f030`, item `0x103f4470`, melee `0x103ea5b0`,
   `0x103ec460`, `0x103eca20`, `0x103e8be0`, thrown `0x103ed200` (player only). No NPC exception
   keeps a timer. Species event bodies are walked/ported; task-code fire stays in their files.
   Type-6 `0x10239f30` launches nothing (S5 item 5); no throw implementation. Warn once per
   (model,sequence), at Warning, when an NPC ranged clip lacks 3030..3044. Delete obsolete NPC
   timer/UpperBody assertions in ElysiumWeaponTests; list them. Keep ranged_open_fire,
   ranged_sustained_fire, cover and cover_move_shoot green.
3. **Melee start/operator `0x102a45c6`, `0x103eaca0`, `0x103e9e00`, `0x103ea5b0`.** Check
   `ElysiumNpcStartTask.cpp::StartTask` and `ElysiumWeaponClasses.cpp::BeginMeleeSwing`,
   `IsMeleeSwingTrigger`, `OperatorHandleAnimEvent`. Capability 0x18000 stamps last attack;
   PrimaryAttack then AutoMovement. Grappled owner refuses. NPC RequestActivity uses GetEnemy,
   weapon slot 361 then owner slot 376, slot 331; refusal/negative sequence without force
   returns without playing. Commit the chosen sequence through existing slot 311
   ForcePreTranslatedSequenceAndActivity in C2's ElysiumNpcBaseAnim.cpp (call, no edit): kernel
   swing row, activity/ideal 0x4b, cycle 0, **then** playback rate = speed scale × rank factor;
   next attack raised to curtime + duration/rate. No NPC PlayAnimSegment; bridge presents the
   kernel row. Preserve player arm. Trigger set **3001, 3030..3037, 3039..3044, 3047** calls
   PrimaryAttack only for non-player; 3003 swallowed; 4001/4002 bodygroup; 3038/3045/3046 base.
   Tzimisce `0x103e8be0`'s 3045/3046 (`0x103e8c50`/`0x103e8c90`): temporary +0x910=1/2,
   RequestActivity(0x4b,1,0), reset 0. If the class is absent, retain a named seam answering
   nothing and report it; do not invent a species.
4. **Character sweep `0x103246d0` → slot 315 `0x10346cd0`.** In
   `ElysiumNpcThink.cpp::Think19CombatCharacterUpdateCharacter`, call MeleeSwingUpdate() with
   **no argument**, at slot 315's think-tail place (`0x1029365b`). Name remaining slot-312
   seams (discipline visuals, slots 313/314/333, heal, expressions, eyes, render-fx expiry;
   0006/0015). Hand body in `ElysiumCombatCharacterSlotBodies.cpp::MeleeSwingUpdate`:
   no seqdesc or swings count<1 → +0xaa1=0, return unstamped; not live → Step(pos,angles,0,0),
   ForceMeleeReset, stamp. Live: dt=curtime−own +0xaa4; dt<=0 returns unstamped;
   rate=cycleRate×playbackRate; c1=(curtime−animTime+0.1)×rate+cycle; c0=c1−rate×dt.
   Keep NPC completion-percent +0x6064, once-only SendIncomingSwingNotice `0x10346ac0` and
   active-swing weapon slot 333 arms. **N=ceil(dt×100)** (float `0x10450564`); i=1..N while
   previous cycle<1, interpolate pose/cycle, clamp c to 1, Step(prev,c); stamp time/pose at tail.
   AdvanceSwingContact reads these kernel words instead of pose-layer phase/caller delta.
   Delete `ElysiumEntityWorldInteraction.cpp::AdvanceMeleeSwings`. C2 calls the same slot for
   the player after slot 258/TickStealthKill. Three species task calls already exist; no edit.
   Owe integrator world-call/declaration removal in Map/ElysiumMapActor.cpp /
   Public/ElysiumEntityWorld.h and Visual/ElysiumMeleeTrail.h comment correction.
5. **D9 `0x10343020`, CalcPose / matrix `0x100c3600`.** Replace only
   `ElysiumWeaponClasses.cpp::ElysiumSwingEndpointsAt`: ask the bone at each query's own Cycle
   under interpolated origin/angles. Inspect embodiment for a sequence-cycle bone accessor
   first. If absent, keep lerp behind the named CalcPose-at-cycle seam and report an exact
   accessor signature to integrator/judge. Preserve D1–D8/D10/D11 window/sample/impact order.
6. **Swing movement `0x10280a50` → `0x10094b70` → `0x100c5d10` → `0x102e0bd0`.** Port
   `ElysiumNpcBaseMotor.cpp::AnimIntervalMovement` / `AutoMovement`, update their .inl comments.
   Slot 250 first; interval=animTime−prevAnimTime; sample **from the just-advanced cycle** to
   cycle+cycleRate×playbackRate×interval, clamping nonlooping to>1 to 1/finished. No model →
   nothing. Rotate delta by local yaw, add local origin; yaw += dAng.y. Owe this embodiment API:
   `virtual bool GetBodySequenceIntervalMovement(USkeletalMeshComponent* Body, const FString&
   Stem, int32 RawIndex, float CycleFrom, float CycleTo, FVector& OutDeltaCm, float&
   OutYawDeltaDegrees)`; default zero/false stands for nummovements=0. Integrator adds it beside
   GetBodySequenceMovement in Public/ElysiumWorldServices.h, Public/ElysiumMapActor.h,
   Map/ElysiumMapActorEmbodiment.cpp and Tests/ElysiumTestServices.h as needed, over
   `ElysiumClipMovement::SampleDelta(*Path, Path->LastFrame()+1, CycleFrom, CycleTo, Out)`.
   Pose blending `0x100c5400` remains a named seam (path is first animation; swing is no grid).
   MoveType!=4 or FL_FROZEN 0x400 → no move. Ground test mask 0x202400b, 100 units, flags 5;
   use landed KernelHullTrace/MotorMoveTraceSweep ground seam (BaseMotor10 read-only).
   Blocked and obstruction not move target → false/no move. `0x102729d0`: goal 2 enemy,
   1 target, 7 Troika slot +0x928, otherwise none. Else SetOrigin(trace end), yaw unless −1;
   return retail result==1 (4/2/3 are false). Check SetRuntimeOrigin carries capsule; if absent,
   owe integrator Motor->Teleport(end,yaw) at apply. No MoveTo/speed request. `chase_melee`
   requires <200 cm, player damage and a nonzero-rate kernel swing row; other melee controls stay green.
7. **Contact team filter `0x1034394d..0x103439a7`, SameTeam `0x10323930`.** In
   `ElysiumWeaponClasses.cpp::ElysiumSwingSameTeam` / `AdvanceSwingContact`, replace false
   with C3's `FElysiumCombatCharacter::IsSameTeam`. NPC attacker, distinct character, FF off:
   hate/fear **then** team rejection; valid equal uint16 symbols reject before hit-list insertion,
   impact, damage or knockback. Self/non-character, player-attacker/debug bypass retain arms.
   No squad/class/faction substitute. C3 specifies melee_same_team with off-centre hated
   teammate and different-team control. If Swing.RecordHits lacks observation, report the
   read-only probe owed; outer damage refusal alone cannot prove this contact gate.
8. **Fifth-hit attribution `0x102a3339/0x102a3344/0x102a3352`, `0x102beda0` (kill
   `0x102beea4`), `0x102bef13`, `0x102a585d..0x102a5874`.** Correct
   `ElysiumWeaponTests.cpp::MakeReachTestDefs` / ShotFromAnimEvent fixture comment. S13 §4:
   admitted positive packet, nonzero base result and ONE_HIT_KILL 0x40000000 kill independently
   of health. Knockout sets 0x440a0000; UNKNOCKOUT start does not clear. COWER_SIMPLE/HINT/NOSEE
   0x109/0x10a/0x10b (blobs `0x105e5788/0x105e5590/0x105e5398`) and DO_SLEEP_ACTIVITY also
   write ONE_HIT_KILL. Pedestrian damage→FLEE `0x103a2e30/0x103a34a3`→Troika `0x102b0250`
   →COWER or failed FLEE_AND_COWER reaches the flag task. Keep isolated shot victim AI disabled.
   Historical cause is unverified: **before work relying on it**, read these damage/task arms
   and OnTakeDamage `0x1032ef60` from the listing; capture schedule/flags, effective cap/wounds,
   packet magnitude and actual death caller; read that caller before writing. Record evidence
   in docs/vtmb/combat-and-damage.md. Five 18 hits below effective cap 100000 prove no historical
   cause. Do not patch damage math or assert the fifth hit was retail knockout.
9. **Slot 247 `0x10090c80` → entity slot 15 `0x1009af40`.** In
   `ElysiumAnimatingSlotBodies.cpp::SetAttackExtentsForSequence`: Flags2&4 and descriptor
   required; e=max(abs(bbmin),bbmax), x=y=sqrt(x²+y²), excess over collision maxs floored at 0;
   write entity extents/partition, not motor slot. Call C2's SequenceBounds: J2b withdrew bbox
   import; live seam answers false, so slot writes nothing. Fixture descriptor proves arm only,
   no arena proof. Preserve sleep/cover save/restore. Report missing RemoveFlag2(4) species
   writes by function. Owe integrator verdict rows 10346cd0 / 10090c80 → matching hand bodies.
10. **Arm tests (write, never run).** Listed tests pin every address above: event-only shots,
    silent-clip Warning, trigger set, kernel swing commit/refusal, own dt (0.25→25, 0.101→11,
    nonpositive/no window unstamped), D9 cycle accessor or named seam, contact-team rejection
    before hit-list/side effects and bypasses; Motor.AutoMovement rotated interval movement,
    end clamp, gates, blocked-other refusal, stopped-at-target result. AttackExtents tests flag,
    descriptor and radial/excess arms. Preserve V11's MeleeSwingStep/MeleeContact tests;
    report exact owed lines if their files need changes.

## Not yours

Real reload: V5. Corpse physics/bake: V4d. Muzzle attachment/player clock/estimate: 0015.
BBox data/re-bake waits on acquire cone `0x1040f550` / `0x1040f080`; no V4c import. Disciplines: 0006.

## Rules

- Touch only the listed files; lines owed by other files go in your report with file/function
  and exact patch. Generated `*Slots.cpp` and bindings are never hand-edited: hand bodies go
  in matching `*SlotBodies.cpp`; the integrator owns verdict rows and regeneration.
- Never build, run tests, the arena, editor or game, or commit/push. No `Arena/` edits.
- Retail first: look up each address before searching docs, read the listing when required,
  and cite the retail address at every ported line. A missing input gets a named seam answering
  nothing. A new divergence is recorded in the report, not adopted.
- Shadowed locals are compile errors here (C4458/C4459); check includes and double definitions.
- Every query has a 60 s timeout; >10 s warns and is logged. At 60 s stop and optimize before
  retrying; never widen/retry as-is. Never read a file over ~200 KB whole. Wait by completion
  notification, never a polling loop.
- Report ≤300 words: addresses/behaviour, tests added/deleted, remaining reads/seams, and exact
  lines owed by other files. Deliver it in the worker response, never a file named `report*.md`.
