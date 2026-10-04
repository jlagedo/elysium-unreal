# Brief C2 — V4c: shared picks, disposition, corpse clocks and enemy memory (coder; no build)

Final, 2026-10-04, against V11/V4o. Read `AGENTS.md` first, `HANDOVER.md`, README § "Rules for
every agent of V4" / "Shared names", packets S1/S4 (death), R2 extension 2 (pick sites), S5
item 9 (stance), S10, S11, S12, and **S13 §§1/3/5 / Changes to the plan**. Owner has ruled:
**NPC and non-NPC animation picks draw on one shared stream**; team registry is C3's in V4c;
real reload is V5's. No owner question remains about the pick stream. Re-locate by function name.

RunAnimation's gate, solid flag on ragdoll, grapple word commits, both enemy selectors and
ChooseEnemy/SetEnemy are landed. Preserve their order. Your dead-enemy fix is the one proved
candidate-liveness divergence plus the separate memory fidelity work, not a new selector.

## Files (only these)

Prefix `Source/ElysiumUE/Private/Substrate/` unless stated; braces expand to separate files.

- `ElysiumNpcAnim.cpp`, `ElysiumNpc.h` — candidate rows/picks/SequenceBounds/stance declarations.
- `ElysiumNpcBaseAnim.cpp` — RunAnimation callers/resolver lookup only.
- `ElysiumNpcBaseStartTask.cpp` — StartTaskSlot442's pick only.
- `ElysiumNpc.cpp` — disposition, ragdoll seed, PlayActivity, StartWalkingAnimation, corpse Think.
- `ElysiumNpcBaseRunTask.{cpp,inl}` — StartFadeOut and fade state only.
- `ElysiumNpcBaseSpawn.{cpp,inl}` — Event_Killed fade step and C3's real spawn registration hooks.
- `ElysiumCombatCharacter.cpp` — CreateCorpse, PlayReactionActivity, team damage predicate only.
- `ElysiumNpcPedestrian.{h,cpp}` — PedestrianCreateCorpse and its wiring/state only.
- `ElysiumPlayerEntity.cpp` — PostThinkAnimation, Spawn/Hydrate team joins only.
- `ElysiumFeed.cpp` — SelectGrappleSequence only.
- `ElysiumNpcBaseSenses10.cpp` — BestEnemy liveness and selected-store walk only.
- `ElysiumNpcBaseSenses.cpp` — GetEnemies/RemoveMemory ownership plumbing only.
- `ElysiumNpcEnemyMemory.{h,cpp}` — Refresh, owner context and notify/store access only.
- `ElysiumNpcBaseConditions2.cpp` — GatherConditions refresh and its memory reads only.
- `Source/ElysiumUE/Private/Visual/ElysiumAnimationResolve.cpp` — TryActivity/PickWeighted only.
- New `Source/ElysiumUE/Private/Visual/ElysiumAnimationPick.{h,cpp}` — one common weighted/
  heaviest selection body, with the shared stream (C1's weapon-model caller also uses it).
- `Source/ElysiumUE/Private/Player/ElysiumAnimationIntent.cpp` — locomotion pick request only.
- `ElysiumProp.cpp` — random animator's pick only.
- `Source/ElysiumUE/Private/Tests/ElysiumPlayerPostThinkTests.cpp`.
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSpeciesMisc10Tests.cpp` — corpse assertions only.
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelAnimTests.cpp`.
- `Source/ElysiumUE/Private/Tests/ElysiumNpcCombatTests.cpp` — death assertions only.
- `Source/ElysiumUE/Private/Tests/ElysiumNpcEnemyTests.cpp`.
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSenses10Tests.cpp` — BestEnemy arms only.
- `docs/vtmb/npc-ai/lifecycle.md` — death sound/seed and S13 maker/fade correction.
- `docs/vtmb/npc-ai/senses.md` — S13 memory/selection correction.
- `docs/vtmb/feeding.md` — released first-task maintenance/fallback correction.

C1 ∩ C2 = C2 ∩ C3 = ∅. C1 owns ElysiumWeaponClasses.cpp; give K4's exact request patch to
C1 as its job (or integrator after reports). C3 owns combat-character declarations in
Public/ElysiumPlayer.h and the new team implementations. You own the existing spawn/player/
damage functions that call them. Generated bindings, world lifecycle/restore hooks and any
other-file declarations are explicit serial integration patches; no concurrent second writer.

## The job

1. **Direct picks `0x1008dc40`, heaviest `0x1008dd30`.** In
   `ElysiumNpcAnim.cpp::FElysiumNpc::SequenceForActivity` / `MeleeSequencesForActivity`, gather
   the body's matching sequence rows once for both the weighted pick and slot-331 band. Remove
   one-clip-per-activity caching; each candidate has a kernel row. Respect include-shadowing
   insofar as the baked groups carry it (activity_enum.md); report absent group metadata to the
   pipeline owner rather than inventing it. No candidates → −1; one → that row; positive total
   → RandomInt(0,total−1), subtract weights in table order (`weight <= r`); all zero → uniform.
   Heaviest: strict max, first tie wins, no random draw. Put this one selection body in new
   Visual/ElysiumAnimationPick.{h,cpp}, used by kernel and visual callers. A bare activity lookup
   reports a miss; fallback belongs to the retail resolver, not this gatherer.
2. **K4 shared stream — retail `*0x1070b244` slot 2.** Use the existing
   **`ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)`** as V4c's common animation-pick stream.
   NPC and non-NPC animation selections share its advancing state; no per-entity seeds, hash,
   new stream, Reaction-stream sequence draw or speculative Variant draw followed by a second
   weighted draw. No draw for a miss, singleton or heaviest pick. Record this resolved ruling
   beside the pick recovery, and inventory the following sites with retail counterparts:

   | port file/function | retail | job |
   |---|---|---|
   | ElysiumNpcAnim.cpp::SequenceForActivity / SelectWeightedSequence | `0x1008dc40`, `0x10272130` | direct shared pick |
   | ElysiumNpc.cpp::PlayActivity / StartWalkingAnimation | `0x10272130`, motor `0x10264680` | remove ScheduleActivityCycle++ / Handle.Index variants; kernel picks |
   | ElysiumNpcBaseStartTask.cpp::StartTaskSlot442 | `0x102827f0` | same direct pick |
   | ElysiumNpcBaseAnim.cpp::RunAnimation | `0x1026c540` | shared weighted or strict heaviest, item 3 |
   | ElysiumNpcAnim.cpp::ChangeStanceForReaction / stance idle path | `0x102c1230`, slot 611 `0x102c12a0` | retain order on NpcSchedule, item 4 |
   | ElysiumFeed.cpp::ElysiumFeedGrappleCommit::SelectGrappleSequence | `0x1032a2cc`, `0x1032a2de` | direct cell picks, attacker before victim |
   | ElysiumMeleeSequenceChoice.cpp::ChooseMeleeAttackSequence | `0x10347180` / picker `0x10348100` | already NpcSchedule, no edit; preserve interleaving |
   | Visual/ElysiumAnimationResolve.cpp::TryActivity / PickWeighted | `0x1008dc40` | replace hash with common body/stream |
   | ElysiumCombatCharacter.cpp::PlayReactionActivity | `0x10099690`, `0x101606e0` | remove Reaction Variant draw, shared sequence draw at actual lookup |
   | ElysiumWeaponClasses.cpp::BuildActivityClipRequest (C1) | player `0x101644f0` / layer `0x1015fbb0`; NPC `0x10272130`; weapon model `0x10253390` / `0x1024efa0` | exact patch to C1: no Handle.Index seed; correct weighted/heaviest caller on shared helper |
   | Player/ElysiumAnimationIntent.cpp::BuildLocomotionIntent | player `0x101644f0`; cast `0x10272130` | player locomotion heaviest, no draw; cast kernel pick |
   | ElysiumProp.cpp::StandRestPose / PlayRandomAnimation | `0x10190850`, Spawn `0x1018df70` | shared weighted ACT_IDLE pick at each retail call |

   StandRestPose is reached from Spawn; PlayRandomAnimation from Think. Pass-through cache keys in
   AnimationDriver/AnimSubsystem/Resolve are not extra pick sites. ElysiumGrapple's heaviest
   pick stays deterministic. Report whether the driver's Variant cache remains meaningful;
   do not let it suppress a retail draw. Non-selection flinch direction/jitter and unrelated
   subsystem draws are outside this ruling; do not casually move all Reaction draws.
   **Landed prop adapter, located by name:** StandRestPose and PlayRandomAnimation call
   `AElysiumMapActor::AnimatedPropRestClip` (Map/ElysiumMapActorEmbodiment.cpp), which forwards
   to `UElysiumEntityBodies::AnimatedPropRestClip` (Visual/ElysiumEntityBodiesProps.cpp), then
   `FElysiumCataloguePlacedModel::SelectRest` (Private/ElysiumModelCatalogues.cpp). The latter
   uses RestSeed and floors weights to 1. Owe integrator a dedicated
   `PickAnimatedPropRestClip(const FString& Stem)` interface/map/body adapter using the common
   picker over Clips/RestCandidates **with raw weights**; change your two actual retail pick
   calls to it. Keep BuildBody's capability check and BuildAnimatedPropVisualWithStaticStem's
   initial presentation lookup draw-free; they are not extra retail selection transactions.
   The deterministic SelectRest preview must no longer supply either live retail pick. This
   needs interface/body source wiring, not a catalogue asset re-bake (weights are already baked).
3. **RunAnimation `0x1026c540`, commit `0x10260a50`.** Preserve landed
   `ElysiumNpcBaseAnim.cpp::RunAnimation`: state not 4/7, ActivityNumber==1, slot 251; nonlooping
   → heaviest TranslatedActivity, looping → weighted TranslatedActivity. Commit only a valid
   row, **without zeroing cycle**. A finished one-shot idle re-picks every think at cycle 1 and
   re-fires its table after reset (S12 b), retail's bug. Read
   `ElysiumNpcBaseHelpers2.cpp::SelectHeaviestSequence` and
   `ElysiumAnimatingOverlaySlotBodies.cpp::SelectWeightedSequenceForActivity`. The first
   currently forwards to the weighted selector: **owe the integrator its replacement with
   the true heaviest body (`0x1008dd30`)**, no draw. The second already forwards NPCs to
   SequenceForActivity, which must become the direct common pick; keep non-NPC model-less −1
   behavior until a real sequence table supplies that input. Report exact function patches.
4. **Disposition `0x102c0f70`, slot 611 `0x102c12a0`, stance `0x102c1230`.** In
   `ElysiumNpc.cpp::SetDisposition` / `ElysiumNpcAnim.cpp` stance functions: lookup miss→Neutral,1
   and old=−1; always write tuning +0x64d8/+0x6584/8/+0x5b94/+0xe3c/+0x10b4/+0x64d0/+0x10b8
   via CommitDisposition. On index change, old=−1 takes slot 611, otherwise transition
   `0x100ed150`: named transition, _1_..._1, new stance idle. Slot 611 follows S5 item 9's
   current/alternate idle draw then minimum-time stance change; preserve draw order; −1 keeps
   current sequence. Valid sequence always writes ideal activity 0xf1/ideal sequence; only
   !DisableAI commits sequence, cycle 0, activity 0xf1, animTime=now and ResetSequenceInfo.
   Remove IsFeedBusy gate; no direct PlayNpcClip/ResetAnimToIdle. Report missing tuning words.
5. **Death transaction/seed `0x1032c0e0`, `0x10090180`, seed `0x1009021a`.** In
   `ElysiumNpc.cpp::BecomeClientRagdoll`, weighted ACT_DIERAGDOLL 0x21 seed only for bone −1;
   miss → no seed; real bone → retain current pose. Ordinary CreateCorpse always passes the
   hit bone or Bip01 Spine2; no seed/hold-pose stand-in for ordinary kills. Preserve landed
   RetailSolidFlags|=4 on the rig branch; no-rig retail zeroes bounds and returns false, with
   no additional writes. CreateCorpse owns the think replacement on every NPC arm: static
   corpse/hide +0.5 removal, mortal +10 PVS poll, Kindred/burning +10 SUB_Remove. Ordinary kill
   never returns to NPCThink/SCHED_DIE. In lifecycle.md correct the death-sound section:
   once on ordinary kill `0x10265ad0`; three only on rig-less state-7 fork → SOUND_DIE → DIE →
   `Die 0x103392c0` → Event_Killed. The seed is arm-tested only: retail reachers are deferred
   CineCleanup `0x1027d170`'s health<1 arm, sibling `0x1027d0a0`, Werewolf `0x103d0820`, zombie
   collapse `0x103dfbb0`; neither death arena record reaches it. Owe fork bone−1 correction in
   ElysiumNpcBaseSelect.cpp if needed. `0x102b5bb0` is dead code (S12), no reading owed.
6. **Pedestrian corpse `0x103a38c0`.** Wire
   `ElysiumNpcPedestrian.cpp::PedestrianCreateCorpse` around the **real**
   `ElysiumCombatCharacter.cpp::CreateCorpse` at slot 301: snapshot mins/maxs +0x6660/+0x666c
   before base zeroes bounds; call entire base; ThinkSet(NULL); SOLID_NONE. Replace counted
   seam and stale header warning. In `ElysiumNpc.cpp::Think`, explicit cleared think must
   stay cleared; distinguish it from a missing restored name. Committed death alone must not
   force SUB_PVSRemove. Mortal seen/unseen and Kindred controls keep their own installed thinks.
   Report virtual declarations/generated-dispatch/verdict lines outside your files.
7. **Fade child and death-time install — S13 §1, `0x1034b7b0`, `0x10265d66/0x10265d72`,
   `0x1027a400`, `0x102695d0`, `0x10269960`.** Read-only check
   `ElysiumNpcMaker.cpp::MakeNPC` / Spawn: ordinary maker **assigns** 4 or 0x204 (not OR),
   m_bFade/Flag_Fade; Flag_InfChild forces fade at `0x1034afe0`. Fleshpile `0x1034c2d0` ORs
   4/0x204; Spawn `0x1034c020` has same implication; zombie `0x1034d140` calls ordinary maker,
   Spawn `0x1034cc60` forces fade. These landed implementations already match; no maker patch
   is owed. Correct assignment-vs-OR prose in lifecycle.md.
   `ElysiumNpcBaseSpawn.cpp::Event_Killed` **already** asks ShouldFadeOnDeath (child bit 9,
   0x200) after slot 301 and calls Spawn19StartFadeOut; fill its counted body, do not duplicate
   the install. Route it and `ElysiumNpcBaseRunTask.cpp::StartFadeOut` (TASK_DIE) to one body.
   Start: mode 0→2/alpha255 (other modes keep alpha), solid flag 4, angular velocity 0, relink,
   next think now+10, install SUB_FadeOut thunk `0x100152b2`. Each fade think alpha>7→−7 and
   next +0.1; else alpha=0, next +0.2, install SUB_Remove `0x101c0b10`. Dispatch the installed
   think in ElysiumNpc.cpp::Think. **Later fade wins** over Kindred +10 removal and pedestrian
   clear, visibility irrelevant. 255→36 decrements, zero at ~death+13.6, removal ~+13.8.
   Missing render-alpha presentation is a named visual seam, never a missing kernel clock.
   `corpse_fades`: tutorial stealth_victim_maker, Spawn, named child, alpha255/mode0, watching
   player; expect 0x204/death/corpse, never removed through death+13, removed by +14.5.
8. **Dead enemy — S13 §3, `0x102743c0` gate `0x10274475`, IsAlive `0x100b4dc0`.** In
   `ElysiumNpcBaseSenses10.cpp::BestEnemy`, replace candidate IsInert with **!IsAlive**. A
   nonhidden resolvable corpse must be rejected. This is the single proved divergence in the
   already-ported selection transaction: preserve
   `ElysiumNpcEnemy.cpp::ChooseEnemy/SetEnemy` (`0x10279dd0/0x10279a50`),
   `ElysiumNpcBaseConditions2.cpp::GatherEnemyConditions` (`0x10270b20`, death arm
   `0x10270e5a..0x10270e89`), base SelectSchedule `0x1028a380`, Troika `0x102af660` death arm
   `0x102afc24..0x102afca7`, PreSelectSchedule `0x102ae920` and SelectIdealState `0x1026f660`.
   No START_COMBAT suppression, eager handle clear or ENEMY_DEAD→idle shortcut. Quiet normal
   Troika exit is ALERT/0x4b; NoAlertState is IDLE/0x6b; replacement enemy permits START_COMBAT.
9. **Memory fidelity — `0x102df320`, cursor `0x102df50e..0x102df518`, slot 541
   `0x10273e10`.** In `ElysiumNpcEnemyMemory.cpp::Refresh` / header owner context,
   `ElysiumNpcBaseConditions2.cpp::GatherConditions` refresh and
   `ElysiumNpcBaseSenses10.cpp::BestEnemy`, use the **actual selected GetEnemies store**, not
   unconditional member EnemyMemory. Preserve `ElysiumNpcBaseSenses.cpp::GetEnemies`' connected
   (<1), disconnected/shared-store seam; bind owner context at creation/redirection, never
   infer owner from the victim or arbitrary caller.
   Unresolvable handle removes unconditionally. Resolvable dead-entry candidate requires NPC
   self-cast +0x94 and state slot 464==7, then owner slot 54 OR squad AND-of-members
   `0x103167f0` permission. Base slot54 `0x10026910` refuses; landed
   `ElysiumNpcTroikaHelpers.cpp::Slot54` (`0x102b50b0`) vetoes current enemy if schedule exists
   without LOST_ENEMY. Vetoed entries may remain indefinitely; player corpse is no NPC-state-7
   candidate, but still unselectable. Kept entries refresh tracked +0x00 only while
   now<lastSeen+freeKnowledge; no age expiry or LKP+0x0c refresh. Unlink **before** owner slot56
   (or squad fanout `0x103169a0`) receives target, LKP+0x0c, vector+0x18 and function tag.
   Landed Troika Slot56 `0x102b5120` clears last enemy only if nonalive/matching. Preserve
   removal's successor-next quirk: skip immediate successor this pass, including multiple dead
   entries; no RemoveAll(!IsAlive). Update senses.md's abbreviated memory account; give C1
   the exact matching combat-and-damage.md correction (its file).
   **First step for owner/squad plumbing:** read `0x102df320`, `0x103167f0`, `0x103169a0` from
   the listing before writing; record call arguments/owner storage in senses.md. A squad object
   absent from substrate gets a named null hook for that retail field, not a fabricated veto
   or squad service; report the exact later squad-owner work and any declarations owed.
10. **Released feed fallback — S13 §5, `0x1033a9e0`, `0x1032a100`, `0x10281eee`,
    `0x102727d0`, `0x10272130`, `0x10295a80`.** In
    `ElysiumFeed.cpp::SelectGrappleSequence`, use bare Npc.SelectWeightedSequence(cell);
    remove label fallback/ladder guard (S12: one authored sequence per cell). A true miss
    warns/EndGrapple; preserve attacker then victim. Preserve release ideal/base 0xf88 versus
    activity/cell 0xf8c, LeaveGrappleState's no-animation writes
    (`0x10329a70/0x1026ce30/0x102b5d90`, slot614 tail `0x102b5d9d`), first MAKE_OBLIVIOUS task
    `0x102a72e3..0x102a7315`, then MaintainActivity before later SET_ACTIVITY `0x102a1c0f`.
    Released translation `0x10328030/0x10328380` leaves bare 0xf88; direct lookup **misses**,
    retail resolver retries disposition 0xf1/slot611 and commits an idle sequence while
    requested activity remains 0xf88; eventual mesmerized ideal/activity is 0x104e. No reset
    on leave, maintenance reorder, cycle-zero at RunAnimation or blanket released no-idle gate.
    RunAnimation's activity==1 gate excludes this intermediate row. Correct feeding.md's
    "next sequence is the trance task's" claim. Idle variant 3 is not guaranteed.
11. **Player PostThink `0x1016be10`, gates `0x1016bede..0x1016bf17`, tail `0x1016c316`.** In
    `ElysiumPlayerEntity.cpp::PostThinkAnimation`, retain order: game-over, locked, !IsAlive,
    observer. Implement actual liveness gate before body test; dead player advances/dispatches
    nothing and keeps channel/cursor words. Other three unavailable inputs are named open
    seams, no invented frozen/cinematic/controller gates. After TickStealthKill call C1's
    MeleeSwingUpdate() (slot312/315), after slot258; C1 removes all world-tick sweeping.
12. **C3-owned team calls in your files — `0x10323a90`, `0x10298d30`, `0x10348890`,
    `0x1016d260`, `0x1016ebd0`, damage `0x1032ef60`.** In
    ElysiumNpcBaseSpawn.cpp::Spawn19TeamName return combat-character TeamName; Spawn19AddToTeam
    calls C3's AddToTeam, replacing counted-only seam; update .inl comments. Both existing base
    Spawn and ElysiumNpcSpawn.cpp::TroikaSpawnBody call these hooks already; do not reorder.
    In ElysiumPlayerEntity.cpp::Spawn / Hydrate join literal player at retail-equivalent spawn/
    restored-state point. Integrator re-registers restored NPC names after ApplyEntityRecord;
    no numeric symbols persist across levels. In ElysiumCombatCharacter.cpp::OnTakeDamage,
    replace CombatTeamSymbolOf/CombatSameTeam constants with C3's common IsSameTeam; refuse a
    different teammate's packet **before** discipline notification/life-state dispatch, admit
    self-damage through this predicate. C3 supplies exact calls; no same-team exemption based
    on relationship, and no attackerless scalar packet as proof.
13. **Arm tests, write only.** Listed files pin weighted/heaviest/miss/singleton/zero-total
    and interleaved NPC+reaction+weapon+prop stream order; RunAnimation gates and kept cycle;
    disposition disableAI ideal-vs-commit arms; real-bone/no-seed versus bone−1/seed;
    pedestrian snapshot/base/clear/solid and explicit-clear-vs-missing-restored name; fade
    modes, solid/angular/relink, +10/−7/0.1/+0.2 and late install over burn/clear; BestEnemy dead
    corpse/player rejection; memory owner veto, unresolved removal, NPC-state7/player distinction,
    notify-after-unlink vectors, free-knowledge boundary/no LKP change, successor skip and selected
    disconnected store; feed first-task fallback/0xf88→0x104e; PostThink AliveGate/Slot312Order.
    Cite each retail address in the test. Remove tests pinning hash/single clip/count-only fade
    or HoldBodyFinalPose; list them. Reports owe other-file declarations/forwarding explicitly.

## Not yours

C1 attack/contact/extents and combat doc; C3 registry/keyfield implementation; generated files
and world restore hooks are integrator's. SequenceBounds stays a named false seam for seqdesc
bbox +0x1c..+0x30 / slot247 `0x10090c80` (J2b), replaceable by C1's test double as needed.
No bbox import/re-bake in V4c. Corpse fall/physics: V4d; burn look: 0014; real reload: V5;
wider Presence: discipline owner; broader squads remain their owner behind the named null seam.

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
