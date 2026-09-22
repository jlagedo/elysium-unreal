// Story 29d, families **Anim10** and **SpeciesAnim10** — the declarations of this family's layer
// 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl` and story 29c-1's family `.inl`s. A body that fills a Troika-line
// vtable slot is NOT declared here: the generator already declares that virtual and this family only
// defines it. What lands here is the non-slot half — the base-tier bodies beside the Troika
// overrides, the per-species arms the one slot method dispatches to, and the seams they read
// through.
//
// The definitions are in `Substrate/ElysiumNpcKernelAnim10.cpp` (activity, sequence, pose and model)
// and `Substrate/ElysiumNpcKernelAnim10_2.cpp` (`MaintainEyeDirection`, the three melee selectors and
// the zombie idle gate). The tests are `Tests/ElysiumNpcKernelAnim10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/shape.md` § "Story 29d, families Anim10 and SpeciesAnim10".
//
// --- What this family is -------------------------------------------------------------------------
//
// Twenty-seven rows across seven retail surfaces:
//
//   * **slot 105 `SetModel`** — the Troika body `0x10298ce0`, the `CAI_BaseHumanoid` pose-parameter
//     cache `0x1025e510`, and the two vocalization-group arms `0x1037b1f0` / `0x103e0540`.
//   * **the activity triple** — `SetActivityAndSequence` `0x10272490`, the base `SetActivity`
//     `0x102725d0`, `MaintainActivity` `0x102727d0` and `AdvanceToIdealActivity` `0x102726a0`.
//   * **slot 310 `SetActivity`** — the Troika body `0x10295750` and the two Tzimisce arms.
//   * **slot 375 `NPC_EarlyTranslateActivity`** — the Troika body `0x10295590` and its five species
//     arms.
//   * **slot 314 `UpdatePoseParameters`** — `0x102bf070`.
//   * **slots 326 / 330 / 359 / 584** — the Troika knockback gate, the near-miss flinch, the Auspex
//     aura index, and `CAI_ExpressiveNPC`'s expresser forward.
//   * **slot 333's `CAI_BaseHumanoid` body** `0x1025fa50`, and family **SpeciesAnim10**'s four
//     species arms of slots 604 and 509.
//
// **ANIMATION IS STATE, NOT A PICTURE.** An activity id, the sequence chosen for it, a translation
// table's answer and the ORDER of a pose write against a dispatch are all things a retail program
// observes: `SetActivityAndSequence` fires slot 465 `OnChangeActivity` and the activity-change
// listener on exactly one edge apiece, `SetActivity`'s two refusals are what makes a repeated request
// a no-op, and `MaintainActivity`'s activity-2 arm is what makes a transition sequence finish. Only
// the RENDERING of a chosen clip is visual, and this family takes no visual-only liberty at all.

// --- Slot 105 `SetModel` -------------------------------------------------------------------------
//
// `+0x64e8` is the disposition stance-table row index `0x100ec640` answers. Story 29d's family
// **Precache10** already writes the same word from `CAI_BaseNPCTroika::Precache` through
// `EnsureStanceResolved()`; this reads the same resolver, so the two cannot disagree.

/** `CAI_BaseNPCTroika::SetModel` (`0x10298ce0`) — slot 105's own body, without the species
 *  prologue. `SetModel()` is the slot; this is what it runs when no species arm claims it, and what
 *  a species arm's own chain call reaches through `FSpeciesDispatchScope`.
 *
 *  Fifty bytes and FOUR calls whose ORDER is the whole body, because each reads what the one before
 *  it wrote: `CBaseCombatCharacter::SetModel`, then `SetHullSizeNormal(force=1)` (`0x10273070`),
 *  then `SetDefaultEyeOffset` (`0x10274ca0`), then `+0x64e8` from `0x100ec640`. The model must be
 *  set before the hull and the hull before the eye. */
void TroikaSetModel(TCHAR* ModelName);

/** The species prologue at the top of `SetModel()`. True means a species body ran and the Troika
 *  body must NOT — which is what a vtable dispatch to an override does. Keyed on the slot-105
 *  override row's retail ADDRESS, the convention family **Precache10** set for slot 104. */
bool SetModelSpecies(TCHAR* ModelName);

/** `CAI_BaseHumanoid::SetModel` (`0x1025e510`), `CAI_BaseHumanoid#105`. 485 bytes:
 *  `CBaseCombatCharacter::SetModel` and then TWENTY-SIX pose-parameter indices cached into the
 *  consecutive words `+0x5fb8`..`+0x601c`, in retail's own order.
 *
 *  **The first thirteen go through `CBaseAnimating::LookupPoseParameter` and the last thirteen
 *  through `thunk_FUN_100b5d10`, which is `LookupFlexController`** — story 29c-1's family Anim
 *  ported that body and recorded that it answers **0**, not -1, on a miss. So the two halves of this
 *  cache take different miss values, and that asymmetry is retail's.
 *
 *  UNREACHABLE TODAY, and named: `CAI_BaseHumanoid` sits under `CAI_BaseActor` on a sibling SDK
 *  branch with no entity classname anywhere in the 77-class census, so no spawned `npc_*` resolves
 *  to it and this arm never runs in this runtime. It is ported because it is the only recovered
 *  reading of what the 26 words at `+0x5fb8` ARE. */
void BaseHumanoidSetModel(TCHAR* ModelName);

/** `CNPC_VGhoulCroucher::SetModel` (`0x1037b1f0`) and `CNPC_VZombie::SetModel` (`0x103e0540`) — the
 *  SAME 119 bytes twice, one per class, and so one port body with the retail address of the arm it
 *  is running as. The Troika base runs FIRST (so the model write happens before `IsMale` is ever
 *  asked), then `CBaseCombatCharacter::IsMale` selects `m_iszVSoundGroup` (`+0x00c0`)
 *  `"Zombie_Male"` or `"Zombie_Female"`, then `+0x00bc` takes the literal 2, then the group is
 *  resolved through the vocalization registry into `+0x00b4`. */
void ZombieLineSetModel(TCHAR* ModelName, const TCHAR* RetailBody);

/** The 26 pose-parameter cache `0x1025e510` fills, `+0x5fb8`..`+0x601c`. Declared by offset and
 *  retail name as family Lifecycle declares its unbound words: no port member claimed them, and
 *  `0x1025efc0` (the head-pose clear `MaintainEyeDirection` runs) writes three of them by index. */
static constexpr int32 NumHumanoidPoseParams = 26;
int32 HumanoidPoseParams[NumHumanoidPoseParams] = {};

/** The 26 retail strings, in the order `0x1025e510` pushes them. Index 0..12 are the
 *  `LookupPoseParameter` half and 13..25 the `LookupFlexController` half. */
static const TCHAR* HumanoidPoseParamName(int32 Index);

// Steps 2 and 3 of the Troika body are already ported and are CALLED, not re-read:
// `SetHullSizeNormal(bool)` is family **Motor10**'s `0x10273070` and `SetDefaultEyeOffset()` is
// family **BaseHelpers**' `0x10274ca0`. Calling them is what keeps slot 105's four-step ORDER the
// one thing this family owns, rather than standing a second reading of either.

/** `thunk_FUN_100ec640(this)` — the disposition stance-table ROW INDEX `SetModel` stores in
 *  `+0x64e8`. Family **Precache10** already reaches the same resolver through
 *  `EnsureStanceResolved()`; this calls that, so slot 104 and slot 105 write the word the same way
 *  and cannot drift. */
void ResolveStanceTableRow();

// --- The activity triple: `m_Activity`, `m_IdealActivity`, `m_TranslatedActivity` ------------------
//
// `ActivityNumber` (+0x0fec, family Positions), `IdealActivityNumber` (+0x0ff0, family Facing) and
// `TranslatedActivity` (+0x0ff4, family Anim) already exist. What this family adds is the three
// bodies that WRITE them in retail's order, and `m_bKeepSound`.

/** `+0x5cd8 m_bKeepSound` — the byte the Troika `SetActivity` raises around its random pick so the
 *  activity-change listener `0x101f6010` is NOT told about the intermediate activity. It is the one
 *  member this family adds to the shape; `ElysiumNpcKernelShapeMap.cpp` binds `+0x5cd8` already. */
bool bKeepSound = false;

/** `CAI_BaseNPC::SetActivityAndSequence` (`0x10272490`) — the commit. 243 bytes, and every one of
 *  its six effects is ordered against the others:
 *
 *    1. `m_TranslatedActivity` (+0x0ff4) is written FIRST, before anything can read it.
 *    2. A NEGATIVE sequence takes `ResetSequence(0)` and SKIPS the cycle, the duration and the
 *       weapon activity entirely.
 *    3. Otherwise the cycle word `m_flCycle` (+0x06f8) is zeroed UNLESS the sequence equals
 *       `m_nSequence` (+0x06f0) with `m_bSequenceFinished` (+0x065d) set, OR the old activity
 *       (+0x0fec) and the new one are BOTH in `{9 ACT_WALK, 0x13 ACT_RUN}` — retail's walk/run cycle
 *       carry, which is what keeps a footfall in phase across a gait change.
 *    4. `ResetSequence(seq)`, `CBaseAnimating::SequenceDuration(seq)`, then
 *       `CBaseCombatCharacter::Weapon_SetActivity(weaponAct, duration)`.
 *    5. slot 533 `EyeOffset(activity, m_TranslatedActivity)` feeds `SetViewOffset` (`0x1009f380`) —
 *       for EVERY request, including the negative-sequence one.
 *    6. slot 465 `OnChangeActivity(activity)` fires only when `m_Activity` DIFFERS from the new
 *       activity; the activity-change listener `0x101f6010` fires only when `m_Activity` differs
 *       from `m_TranslatedActivity` AND `m_bKeepSound` is clear. **Two different comparisons**, and
 *       both read `m_Activity` BEFORE step 7 overwrites it.
 *    7. `m_Activity = activity`, then the navigator is notified (`0x102e1cf0`).
 *
 *  `+0x065d` is the byte AFTER family Anim's `bSequenceFinished` (+0x065c). The listing reads
 *  `+0x65d`, so it is carried as its own member rather than folded into the neighbouring one. */
bool bSequenceLoopedOnce = false;   // +0x065d
void SetActivityAndSequence(int32 Activity, int32 Sequence, int32 TranslatedActivity,
	int32 WeaponActivity);

/** `CAI_BaseNPC::SetActivity` (`0x102725d0`) — slot 310's BASE body, a DISTINCT retail function
 *  beside the Troika override `0x10295750` that owns the slot, so it takes its own name and is never
 *  `hand:`. Ported under the convention wave 1's `Precache10` set with `BasePrecache`.
 *
 *  Two refusals then three writes: return at once when `m_Activity` already equals the request, and
 *  return when `m_Activity` is 2 (`ACT_TRANSITION`) unless the request is 0 (`ACT_RESET`);
 *  otherwise `m_IdealActivity = request`, `ResolveActivityToSequence` (`0x10272130`, family Anim)
 *  fills `m_nIdealSequence` / `m_IdealTranslatedActivity` / `m_IdealWeaponActivity`, and
 *  `SetActivityAndSequence` commits all four in that order. */
void BaseSetActivity(int32 Activity);

/** `AdvanceToIdealActivity` (`0x102726a0`), the non-virtual helper `MaintainActivity` ends in.
 *
 *  `FindTransitionSequence(m_nSequence, m_nIdealSequence)`; a `-NAN` answer (retail's own sentinel,
 *  which the listing produces by comparing the returned float against itself) dispatches slot 310
 *  with `m_IdealActivity` and stops. A transition sequence that is NOT the ideal one commits
 *  activity **2** with that sequence, re-resolving the translated/weapon pair from the transition's
 *  own activity when it has one. Only when the transition IS the ideal sequence does the ideal
 *  activity commit. */
void AdvanceToIdealActivity();

/** `CAI_BaseNPC::MaintainActivity` (`0x102727d0`). Gated on slot 466 `ShouldMaintainActivity`;
 *  inside, work happens only when `m_Activity` differs from `m_IdealActivity` OR `m_nSequence` from
 *  `m_nIdealSequence`. Activity 2 is the special arm — it waits for `m_bSequenceFinished` and then
 *  runs `AdvanceToIdealActivity` and NOTHING else; every other activity first re-resolves the ideal
 *  through `0x10272130` and then advances. The body writes no field of its own.
 *
 *  **Not a vtable slot** — `vtmb_func 0x102727d0` reports `__thiscall` with no dispatch site — so it
 *  takes its own name. Family **SaveRestore10** had already declared `MaintainActivity()` as a SEAM
 *  that counts the call and records the latch `m_bForceMaintainActivity` was holding; that seam is
 *  the entry point and keeps its bookkeeping, and it now runs THIS body after it instead of
 *  answering nothing. The name here is `BaseMaintainActivity` for the same reason
 *  `BaseSetActivity` is: it is `CAI_BaseNPC`'s own function, with no Troika override above it. */
void BaseMaintainActivity();

/** `CAI_BaseNPCTroika::SetActivity` (`0x10295750`) — slot 310's own body, without the species
 *  prologue. 612 bytes, three top arms, and every request outside the `0x1093`-`0x1096` and
 *  `0x1115`-`0x1117` families forwards to `BaseSetActivity` unchanged. */
void TroikaSetActivity(int32 Activity);

/** The species prologue at the top of `SetActivity()`. Keyed on the slot-310 override row's retail
 *  ADDRESS. */
bool SetActivitySpecies(int32 Activity);

/** `CNPC_VTzimisceHeadClaw::SetActivity` (`0x103c1cd0`). ONE arm in front of the Troika body:
 *  request 9 `ACT_WALK` with a non-null slot 167 `GetEnemy` becomes `0x1136 ACT_TZ_WALK2`. */
void TzimisceHeadClawSetActivity(int32 Activity);

/** `CNPC_VTzimisceRunner::SetActivity` (`0x103c3d80`). A five-entry request remap in front of the
 *  Troika body, gated on the form byte `+0x6672` being non-zero and tested in retail's order
 *  1, 9, 0x13, 3, 0xf1. With the byte clear every request forwards unchanged. */
void TzimisceRunnerSetActivity(int32 Activity);

/** `+0x6672`, `CNPC_VTzimisceRunner`'s form byte. Both its slot-310 remap and its slot-375 post-pass
 *  read it and nothing in this runtime writes it; declared by offset and retail class, as family
 *  Precache10 declares `CNPC_VMingXiaoTentacle`'s three model indices. */
bool bTzimisceRunnerForm = false;

/** `+0x6688 m_bHeavyBodyTarget`, `CNPC_VTzimisce`'s carried-body side. Retail's slot-375 arm takes
 *  the `_L` variants when it is **ZERO**. */
bool bHeavyBodyTarget = false;

/** `CBaseAnimating::FindTransitionSequence(from, to, &dir)` — the studio transition graph.
 *  **SEAM**: family Anim recorded that this substrate stands no studio header and no sequence index.
 *  It answers **`To`**, which is retail's OWN no-transition answer — the SDK body returns the
 *  destination sequence unchanged when no transition clip stands between the two — and is the arm
 *  that commits the ideal through `SetActivityAndSequence` directly. See the definition for why the
 *  `-1` sentinel would have stranded a body in `ACT_TRANSITION`. */
int32 FindTransitionSequence(int32 From, int32 To) const;

/** `CBaseCombatCharacter::Weapon_SetActivity(activity, duration)`. **SEAM**: the port's weapon
 *  activity lives on the item's own clip set and there is no per-weapon activity word on the NPC;
 *  the request is recorded with the duration `SequenceDuration` answered, which is what makes the
 *  step-4 ORDER assertable. */
struct FWeaponActivityRequest
{
	int32 Activity = 0;
	float Duration = 0.f;
};
TArray<FWeaponActivityRequest> WeaponActivityRequests;
void WeaponSetActivity(int32 Activity, float Duration);

/** `thunk_FUN_1009f380(this, &offset)` — `CBaseEntity::SetViewOffset`, which slot 533 `EyeOffset`
 *  feeds on EVERY commit, including the negative-sequence one.
 *  **SEAM**: this runtime has no writable `m_vecViewOffset` word — `FElysiumEntity::EyePosition()`
 *  derives the eye from the character's standing view height rather than from a stored offset — so
 *  the write is recorded. The recorded half is what matters here: that slot 533 is dispatched once
 *  per commit, with the NEW translated activity beside the requested one. */
TArray<FVector> ViewOffsetWrites;
void SetViewOffset(const FVector& OffsetUnits);

/** `m_pHintNode` (`+0x5ddc`) as the two cover/reload delegates take it. The generated slot-569/570
 *  signatures take an opaque `void*` and story 29c-1's bodies use it only as a NON-NULL marker,
 *  reading the node itself off `ScheduleHost.HintNode`; this spells that once so slot 375 passes
 *  what retail passes. */
void* CurrentHintPointer();

/** `thunk_FUN_101f6010(&DAT_1073dd58, this, newTranslated, oldActivity)` — the global
 *  activity-change listener list. **SEAM**: no such registry exists here. The notice is RECORDED,
 *  because WHICH edge fires it (and that `m_bKeepSound` suppresses it) is the recovered rule. */
struct FActivityChangeNotice
{
	int32 NewTranslatedActivity = 0;
	int32 OldActivity = 0;
};
TArray<FActivityChangeNotice> ActivityChangeNotices;

/** `thunk_FUN_102e1cf0(m_pNavigator)` — `CAI_Navigator::OnNewActivity`. **SEAM**: story 29c-1's
 *  family Motor found no `CAI_Navigator` object in this substrate at all; the notice is counted so
 *  the tail of every commit is assertable. */
int32 NavigatorActivityNotices = 0;

// --- Slot 375 `NPC_EarlyTranslateActivity` ---------------------------------------------------------
//
// **THE TWO SURFACES, AND HOW THEY ARE JOINED.** Before this story slot 375 was split: the VISUAL
// path walked `Visual/ElysiumNpcActivityTables.cpp`'s generated `PreTranslate_*` rows through
// `ElysiumActionTables::NpcTranslate`, and the KERNEL slot was a generated stub returning 0. The
// tables' live-predicate evaluator (`Visual/ElysiumAnimationResolve.cpp`) answered only `ArmedAlert`
// and `NotArmedAlert` and returned FALSE for every other predicate, so the frenzy rows, the gait
// rows, the cover and reload delegates and every form/variant row were unreachable.
//
// This family lands the kernel body VERBATIM — it is the interpreter, and it reads the live state the
// table walker cannot see — and then hands that same live state BACK to the table walker through one
// evaluator, `PreTranslatePredicate`. There is now one answer to "does this NPC's frenzy word carry
// `0x40`", and both surfaces read it.

/** `CAI_BaseNPCTroika::NPC_EarlyTranslateActivity` (`0x10295590`) — slot 375's own body, without the
 *  species prologue. Arms in retail's order:
 *
 *    1. the gait ConVar `DAT_10924d6c`: value 1 rewrites 9 `ACT_WALK` and `0x1115 ACT_HUNT_WALK` to
 *       `0x13 ACT_RUN`; value 2 rewrites `0x13` to 9.
 *    2. `m_bfNPCFrenziedFlags` (+0x5b84) bit `0x40` rewrites `{0x17, 9, 0x13, 0x16, 0x1115, 0x1121}`
 *       to `0xf17 ACT_RUN_FRENZY`; ELSE bit `0x20` rewrites `{0x1115, 9, 0x16, 0x1121}` to `0x13`.
 *    3. 3 `ACT_FIDGET` becomes 1 `ACT_IDLE` — **and only when neither rewrite in step 2 fired**, as
 *       both of those arms `goto` past this test. Unobservable, because no member of either rewrite
 *       set is 3; recorded because the listing says so.
 *    4. ONLY under capability bit `0x8000000` (slot 513): `0x55 ACT_RELOAD_FAST` delegates to slot
 *       570 with `m_pHintNode`, and `6 ACT_COVER` — or 1 `ACT_IDLE` when `m_afMemory` bit 2 is set —
 *       delegates to slot 569 with `m_pHintNode`. Each RETURNS the delegate's answer.
 *    5. otherwise `CBaseCombatCharacter::NPC_EarlyTranslateActivity`. */
int32 TroikaNpcEarlyTranslateActivity(int32 Activity);

/** The species prologue at the top of `NPC_EarlyTranslateActivity()`. True means a species body ran
 *  and its answer is in `OutActivity`. */
bool NpcEarlyTranslateActivitySpecies(int32 Activity, int32& OutActivity);

/** `CNPC_VDog::NPC_EarlyTranslateActivity` (`0x10374ad0`), 21 bytes. Retail preserves
 *  `3 ACT_FIDGET` by returning with `EAX` still holding the request — one early return the Troika
 *  base never sees — and forwards every other activity to `0x10295590`. */
int32 DogNpcEarlyTranslateActivity(int32 Activity);

/** `CNPC_VHengeyokai::NPC_EarlyTranslateActivity` (`0x10381b50`). Under the carry-form bit, request
 *  1 returns `0x128` and 9 or `0x13` return `0x129`, each immediately; everything else tail-calls
 *  the HUMAN body `0x103854f0`, which itself chains the Troika one. */
int32 HengeyokaiNpcEarlyTranslateActivity(int32 Activity);

/** `CNPC_VHuman::NPC_EarlyTranslateActivity` (`0x103854f0`), 565 bytes and 39 census classes — the
 *  armed/alert decision tree, then the rewrites. See the definition for the arm-by-arm walk; the one
 *  thing to carry here is that `m_bAggressiveAnims` (`+0x6410`) is written on FOUR different paths
 *  and every later rewrite reads it. */
int32 HumanNpcEarlyTranslateActivity(int32 Activity);

/** `CNPC_VTzimisce::NPC_EarlyTranslateActivity` (`0x103bde40`). Under the carry-body flag bit, a
 *  ZERO `m_bHeavyBodyTarget` gives `0xfd` / `0xff` and a non-zero one `0xfc` / `0xfe`. */
int32 TzimisceNpcEarlyTranslateActivity(int32 Activity);

/** `CNPC_VTzimisceRunner::NPC_EarlyTranslateActivity` (`0x103c3e10`). Chains the Troika body FIRST
 *  and only then remaps the TRANSLATED activity under a non-zero `+0x6672`, so it is a POST-PASS on
 *  the base's answer and not a replacement: 1 and `0xf1` become `0x1134`, 3 `0x1135`, 9 `0x1136`,
 *  `0x13` `0x1137`. */
int32 TzimisceRunnerNpcEarlyTranslateActivity(int32 Activity);

/** `thunk_FUN_10381c80(this)` — `CNPC_VHengeyokai`'s carry-form probe, `m_bfAINPCFlags` (`+0x14b8`)
 *  bit `0x20 CARRYING_BODY`. Not a seam: the port carries the word. */
bool HengeyokaiCarryFormBit() const;

/** The active weapon's `+0x19c` word, whose bit `0x40 NODRAW` makes `0x103854f0` treat an armed NPC
 *  as unarmed. **SEAM**: no port member carries the weapon's draw flags — the port's weapon state is
 *  the item's own catalogue row — so this answers 0, the arm in which the weapon DOES draw and the
 *  decision tree runs. Answering `0x40` instead would clear `m_bAggressiveAnims` for every armed
 *  body and make the whole tree unreachable. */
uint32 ActiveWeaponDrawFlags() const;

/** `thunk_FUN_103be130(this)` — `CNPC_VTzimisce`'s carry-body probe, the same `+0x14b8` bit 5. */
bool TzimisceCarryFormBit() const;

/** `CBaseCombatCharacter::NPC_EarlyTranslateActivity(activity)` — the chain tail every arm above
 *  ends in. **SEAM**: `CBaseCombatCharacter`'s own body is not an NPC-kernel row and no class in the
 *  census overrides it below `CAI_BaseNPCTroika`; it answers the activity UNCHANGED, which is the
 *  identity every recovered caller relies on. */
int32 BaseCombatCharacterNpcEarlyTranslateActivity(int32 Activity) const;

/** **THE ONE LIVE-STATE EVALUATOR** for `Visual/ElysiumActionTables.h`'s `ENpcPredicate` vocabulary,
 *  answered from THIS NPC's kernel state. `Predicate` is an `ElysiumActionTables::ENpcPredicate`
 *  carried as `int32` so the substrate does not take a dependency on a `Visual/` private header; the
 *  definition static-asserts the two ends against each other.
 *
 *  This is how the anim surface and the kernel slot are made to agree: `ElysiumAnimResolve::
 *  TranslateActivity` consults an intent's `NpcLiveState` provider before its own two-answer
 *  fallback, and an intent built for a live NPC binds that provider to this. Every predicate is
 *  answered from the SAME read the slot-375 body above makes. */
bool PreTranslatePredicate(int32 Predicate, int32 Operand) const;

/** Bind the evaluator above into an animation intent, so a cast body's translation walk reads THIS
 *  NPC's live state instead of the two-answer fallback `ElysiumAnimResolve::TranslateActivity` used
 *  before this story. One line, so a producer cannot bind half of it. */
void BindPreTranslateState(struct FElysiumAnimationIntent& Intent) const;

// --- Slot 314 `UpdatePoseParameters` ---------------------------------------------------------------

/** `m_bAimWeaponAtTarget` (+0x0e6c), `m_hWeaponAimTarget` (+0x0e70), `m_vWeaponAimOffset`
 *  (+0x0e74..+0x0e7c) and the aim pose pair `m_flSet_PoseParameters` (+0x1064) / `+0x1068`. Four
 *  `CBaseAnimating`-tier words below story 29b's band, declared here as family Anim declares its own.
 *  The PAIR is the pose the aim resolves to — `+0x1064` the yaw and `+0x1068` the pitch, in the
 *  order the no-aim arm writes them. */
bool bAimWeaponAtTarget = false;
FElysiumEntityHandle WeaponAimTarget;
FVector WeaponAimOffset = FVector::ZeroVector;
float SetPoseParameterYaw = 0.f;    // +0x1064
float SetPoseParameterPitch = 0.f;  // +0x1068

/** `thunk_FUN_10278650(this, &out, &eye, 0.0f, <one>)` — the aim POINT the pose is taken to.
 *  **SEAM**: this is `CAI_BaseNPC::GetShootTarget`-adjacent body-target arithmetic the substrate does
 *  not stand; it answers the target's `WorldSpaceCenter` (slot 192), which is retail's own answer for
 *  a target with no authored body-target list, and reports whether it could. */
bool AimPointFor(const FElysiumEntity* Target, FVector& OutPointUnits) const;

/** `CBaseCombatCharacter::UpdatePoseParameters(interval)` — the tail slot 314 ALWAYS ends in,
 *  whichever arm it took. **SEAM**: the base body is a `CBaseCombatCharacter` row and not an NPC
 *  kernel one; the call is counted so the "always" is assertable. */
int32 BaseUpdatePoseParameterCalls = 0;

// --- Slot 326, the Troika knockback-eligibility gate ----------------------------------------------

/** `CAI_BaseNPC::FUN_103482e0` — slot 326's BASE body, the knockback eligibility predicate.
 *  `ElysiumReactions::IsKnockbackAllowed` already carries all three of its arms (checklist verdict
 *  `present`), so this is the vtable-shaped wrapper over it and NOT a second reading: the template
 *  byte `+0x9e Disallow_Knockbacks`, the `CVStatList_t::IsEqual(0x0f, 0x11)` dead test, and slot 158
 *  `m_lifeState == 0`. */
bool BaseKnockbackAllowed(void* SwingRecord, FElysiumEntity* Attacker);

/** `DAT_109245e4`'s int (`+0x2c`) — the hit-buildup ConVar slot 326's Troika arm compares
 *  `m_iHitBuildupCount` (`+0x6064`) against. `FElysiumCombatCharacter::HitBuildupAdmitAtOrBelow` is
 *  the port's recovered value for the same ConVar and is read rather than restated. */
static int32 HitBuildupConVarValue();

// --- Slot 330, the near-miss flinch ----------------------------------------------------------------

/** SEAM for `thunk_FUN_10344f80(this, &direction, kind, 0)` — the reaction dispatcher slots 330 and
 *  several damage bodies share. Its THREE refusal gates are exactly
 *  `ElysiumReactions::IsKnockbackAllowed`'s — slot 400 `AllowsKnockbackBypass` first, then the
 *  template byte, then the dead test — so they are asked through that one rule rather than restated;
 *  what has no callee here is the rest (slot 323's direction, `LookupActivity`,
 *  `TranslateFlyingKnockbackActivity`, slot 320). The request is recorded with the KIND, which is
 *  what separates slot 330's two distance bands. */
struct FNearMissReaction
{
	FVector DirectionUnits = FVector::ZeroVector;
	int32 Kind = 0;   // 2 inside the near band, 0 inside the far one
};
TArray<FNearMissReaction> NearMissReactions;
void PlayReaction(const FVector& DirectionUnits, int32 Kind);

/** Retail's `FireBulletsInfo_t`, as much of it as slot 330 reads: `m_vecSrc` at `+0x08`, the
 *  attacker at `+0x94` and the firing weapon at `+0x98`. This substrate stands no `FireBulletsInfo_t`
 *  — its bullet path carries an `FElysiumDmg` and a trace — so the three words slot 330 reads are
 *  declared here and the slot's generated `void*` is interpreted as this. It is a PORT type standing
 *  for a retail one, named so nobody mistakes it for the whole struct. */
struct FFireBulletsInfo
{
	FVector SourceUnits = FVector::ZeroVector;   // +0x08 .. +0x10 m_vecSrc
	FElysiumEntity* Attacker = nullptr;          // +0x94
	FElysiumEntity* Weapon = nullptr;            // +0x98
};

/** SEAM for `thunk_FUN_102517e0(weapon)` — the `CVDmg_t` row (three rows of stride `0xec84`, keyed
 *  on the weapon's `+0x848`) whose `+0x404` and `+0x408` are slot 330's two near-miss distance
 *  bands. This runtime carries no `CVDmg_t` table, and retail's own miss arm BUILDS a default row
 *  rather than answering none — so there is no admitting value to borrow. This answers false and the
 *  body takes retail's beyond-the-far-band arm, which does nothing. Named, not hidden. */
bool NearMissBands(const FElysiumEntity* Weapon, float& OutNearUnits, float& OutFarUnits) const;

// --- Slot 359, the Auspex aura index ---------------------------------------------------------------

/** SEAM for `CBaseEntity::EntityUnselectable(this)` — slot 359's first refusal.
 *  **UNRECOVERED** as a port concept: nothing in this substrate marks an entity unselectable. It
 *  answers FALSE, the arm that lets the rest of the ladder run, so no aura is silently refused. */
bool EntityUnselectable() const;

// --- Slot 584, `CAI_ExpressiveNPC`'s expresser forward ---------------------------------------------

/** `FUN_10260dc0` (`CAI_BaseHumanoid#584`, `CAI_ExpressiveNPC#584`), an 11-byte tail jump through
 *  the expresser pointer at `+0x5f48` into `0x10311c10` with the concept id and a modifier string.
 *
 *  Two facts are recorded rather than hidden. (1) **The arities differ.** The generated slot-584
 *  signature carries the TROIKA body's (`0x1028d910`, `ResetAllThinkStamps`, one int), so the
 *  modifier string has no way through the slot and this arm is dispatched with an empty one.
 *  (2) **Half of it is reachable, and the checklist's walk said otherwise.** The census gives
 *  `CAI_BaseHumanoid` no entity classname, so that half is unreachable; but `CAI_ExpressiveNPC`
 *  claims **`npc_TestBaseHumanoid`**, so a body spawned under that classname DOES take this arm.
 *  The census also names slot 584 `Speak(AIConcept_t, const char*)`, which is the retail name the
 *  coined `BaseSpeak` was standing in for.
 *
 *  **SEAM**: family Lifecycle's `ExpressiveNpcExpresser()` answers null (no expression substrate),
 *  which is retail's own null-expresser fault; the port records the speak request instead of
 *  faulting. NAMED CRASH GUARD. */
struct FExpresserSpeak
{
	int32 ConceptId = 0;
	FString Modifier;
};
TArray<FExpresserSpeak> ExpresserSpeaks;
void ExpressiveNpcSpeak(int32 ConceptId, const TCHAR* Modifier);

/** The species prologue of slot 584, called from the TOP of `FElysiumNpc::Slot584`
 *  (`ElysiumNpcKernelClosure.cpp`, story 29c-1's `ResetAllThinkStamps`). True means the arm above
 *  ran and the Troika body must not. */
bool Slot584Species(int32 ConceptId);

// --- Slot 333's `CAI_BaseHumanoid` body, `0x1025fa50` ----------------------------------------------
//
// The look queue (`LookTargets`, family Facing), the named expression scene (`ExpressionScene` /
// `ExpressionSceneEnt`, family BaseHelpers), `HasActiveLookTargets` and `HumanoidValidEyeTarget`
// (families BaseHelpers and Senses) and `PickLookTarget` (family Lifecycle) all already exist; what
// this body adds is the head vector it accumulates into and the two stamps around it.

/** `+0x5f74`..`+0x5f7c` — the blended head direction `MaintainEyeDirection` writes and re-reads. */
FVector HumanoidHeadVector = FVector::ZeroVector;

/** `+0x5f80` — the curtime deadline the body toggles `+0x0854` on, re-armed to
 *  `curtime + RandomFloat(1.5, 4.5)`. */
double HumanoidBlinkToggleTime = 0.0;

/** `+0x0854` — the `CBaseFlex` word the deadline above TOGGLES (`x = (x == 0)`). Retail's only use
 *  of it in this body is the toggle itself. */
int32 FlexToggleWord = 0;

/** `CAI_BaseHumanoid::MaintainEyeDirection` (`0x1025fa50`), `CAI_BaseHumanoid#333`. 2,226 bytes.
 *  A DISTINCT retail function beside the Troika slot-333 body (`0x102bff20`, story 29c) that owns
 *  the slot, so it takes its own name and is never `hand:`. UNREACHABLE TODAY for the same census
 *  reason `BaseHumanoidSetModel` is, and named there.
 *
 *  Read off the LISTING: the decompiler aliases six stack floats across the three passes and loses
 *  which vector is which. See the definition for the five passes. */
void BaseHumanoidMaintainEyeDirection(float Interval);

/** `1025fb7a`, the cycler-actor acquisition arm of the body above. Split out because BOTH of its
 *  callers are inside one `if`/`else` in retail and the arm itself is fifteen lines: the two FLOAT
 *  ConVars, `RandomFloat` between them, and `AddLookTarget(UTIL_PlayerByIndex(1), 0.5, duration)`. */
void MaintainEyeDirectionCyclerArm();

/** `0x1025efc0` — the head-pose clear `MaintainEyeDirection` runs third. Writes pose parameters
 *  `HumanoidPoseParams[10..12]` (head_yaw / head_pitch / head_roll) to 0 through the animating
 *  tier's `SetPoseParameter`, flushes the bone cache, and clears the two cached-direction bits of
 *  `HumanoidHeadCacheBits` (+0x5f4c, family Facing) so the next read recomputes.
 *  **SEAM**: the pose write and the bone-cache flush have no callee here; the two cache bits are a
 *  port member and ARE cleared. */
void ClearHeadPoseParameters();

/** SEAM for `DAT_1090fc0c` / `DAT_1090fc9c`, the two FLOAT ConVars (`+0x28`, not the `+0x2c` int
 *  form `DebugConVar` carries) whose pair is the cycler-actor look duration
 *  `RandomFloat(DAT_1090fc0c, DAT_1090fc9c)`. Neither name nor default is in the corpus; both answer
 *  `0.0`, which is retail's own `IsCommand()` arm, and a test sets the value it wants. */
static float Anim10FloatConVar(const TCHAR* RetailGlobal);
static void SetAnim10FloatConVar(const TCHAR* RetailGlobal, float Value);

// =================================================================================================
// Family **SpeciesAnim10** — four species arms of slots 604 and 509.
// =================================================================================================
//
// All four are arms of slot bodies story 29c-1 already landed, so each is a case inside that slot's
// one port method and the dispatch is added AT that method rather than cloned here:
// `FElysiumNpc::SelectScheduleMeleeCombat` (`ElysiumNpcKernelSchedule.cpp`) and
// `FElysiumNpc::ShouldPlayIdleSound` (`ElysiumNpcKernelSounds.cpp`).

/** `CNPC_VHuman::SelectScheduleMeleeCombat` (`0x10385e40`), 1,449 bytes, slot 604 for **34** census
 *  classes — the single most-inherited melee selector in the game. It REPLACES the Troika body
 *  `0x102b6c30` wholesale and never chains it. Every return also stamps the selector trace
 *  (`+0x1b30` the source file, `+0x1b34` the line), which this runtime records through
 *  `RecordScheduleEvent`. */
int32 SelectScheduleMeleeCombatHuman();

// `thunk_FUN_102b7370(this)` — `SelectDoorObstructionSchedule`, the first of the two offers the
// human body's common tail makes and the ONLY one MingXiao's makes. `FElysiumNpc::
// SelectDoorObstructionSchedule` (`ElysiumNpc.cpp`) already IS that body and answers an
// `int32`; the two selectors call it and convert through `ElysiumScheduleNumber`,
// rather than a second reading being stood beside it.

/** `CNPC_VMingXiao::SelectScheduleMeleeCombat` (`0x10396050`), 1,522 bytes. The same skeleton as the
 *  human's with FOUR stated differences: the distance is tested BEFORE the roll; the common tail
 *  offers only `0x102b7370`; an extra `COND 0x48 ENEMY_OCCLUDED` arm opens it; and there is no
 *  `COND 0x0d SHOULD_BLOCK` arm at all. */
int32 SelectScheduleMeleeCombatMingXiao();

/** `CNPC_VBach::SelectScheduleMeleeCombat` (`0x10364080`), 395 bytes. A weapon-discipline prologue —
 *  Bach must be holding the right gun or the right sword for the condition he is in — and then the
 *  HUMAN body, with a `+0x6444` clear and a forced `0x159` when that answered zero. */
int32 SelectScheduleMeleeCombatBach();

/** `+0x6690` — the curtime stamp `CNPC_VBach`'s condition-0x7b arm writes
 *  (`curtime + _DAT_10463584`, 15.0f). No reader is recovered; declared by offset so the write is
 *  assertable. */
double BachFailStamp = 0.0;

/** `+0x6444` — the word Bach's fall-through clears unless `m_NPCState` is 4 or 0xc. No reader is
 *  recovered either; same treatment. */
int32 BachClearWord = 0;

/** `CNPC_VZombie::vfunc509` (`0x103e0fa0`), slot 509's zombie arm. It REPLACES the Troika body
 *  `0x10294040` wholesale — no dialog refusal, no state test, no `SF_NPC_GAG` — and its own arms are
 *  the targetable byte, a live dialog partner, `IsBusyWithDiscipline`, then a weight of 999 that
 *  drops to 20 (a 1-in-21 roll) when the running schedule's local id is `0x12f`, in which case the
 *  float-sound arm is SKIPPED. */
bool ShouldPlayIdleSoundZombie();

/** Whether slot 509's override for this NPC's retail class IS `0x103e0fa0`. A separate predicate
 *  because the arm's own answer is a `bool` and cannot double as "an arm ran", which is the shape
 *  the other four dispatchers in this family use. */
bool ShouldPlayIdleSoundZombieArm() const;
