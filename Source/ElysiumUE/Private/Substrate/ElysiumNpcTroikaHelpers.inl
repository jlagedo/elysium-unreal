// Story 29c-1, family **TroikaHelpers** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcTroikaHelpers.cpp` and the tests in
// `Tests/ElysiumNpcKernelTroikaHelpersTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// --- What this family is --------------------------------------------------------------------------
//
// The bodies of `CAI_BaseNPCTroika` and the helper classes beside it that 29a's naming pass could
// not name: the `0x102a…`–`0x1033…` band plus the `CAI_Motor`, `CAI_Navigator` and
// `CAI_StandoffBehavior` helpers at `0x102c7…`–`0x102ee…`. 45 rows, 17 of which fill Troika-line
// vtable slots and are DEFINED (not declared) here: 54, 56, 322, 334, 597, 599, 600, 601, 602, 606,
// 607, 608, 609, 610, 611, 612 and 616.
//
// THE STANDING FACT OF THIS FAMILY: **the melee quartet 599/600/601/602 is one behaviour written
// twice.** Retail fills each of the four slots with a `CAI_BaseNPCTroika` body for 18 classes and a
// `CNPC_VAndreiBlood`-line copy for 38-40 more. 599 and 600 are byte-identical between the two
// lines; 601 and 602 are NOT, and both differences are recovered and ported (see `MeleeSlotBody`).
// Since story 5 step 3 the human line's four are `FElysiumNpcHuman`'s overrides, which call these
// bodies (and Bosses' `FUN_10385cf0` for 601) directly.
//
// THE SECOND STANDING FACT: **there is no attack coordinator here.** `m_pAttackCoordinator`
// (`+0x65e8`) is an `int32` index of three globals with no object behind it (29b's shape map says
// so), and every one of the coordinator's five entry points is a seam below. That is what makes the
// 602 divergence observable rather than academic: the Troika line refuses on a null coordinator and
// the `CNPC_VAndreiBlood` line does not.

// --- Words this family's bodies read that 29b did not declare -------------------------------------
//
// The hand-written shape map binds `0x1a40`..`0x665a`. What is missing is (a) `CBaseEntity` /
// `CAI_Navigator` / `CAI_StandoffBehavior` words outside that band and (b) one NPC word the map
// calls ABSENT. Each carries its offset and the class that owns it.

/** SEAM for `entity->+0x200`, the word slot 56 (`0x102b5120`) requires to be non-zero before it
 *  looks at the argument at all. Below every band this runtime models and **unrecovered** — the
 *  corpus holds no other reader that pins it. Answers `false`, which is retail's own early return
 *  and leaves `m_hLastEnemy` alone. */
static bool EntityWord0x200(const FElysiumEntity& Entity);

/** SEAM for `CBaseCombatCharacter::SetDialogPartner(this, NULL)`, the clear `OnDialogRelease`
 *  (`0x102c0360`) makes between the flag and the output. This runtime carries the partner as the
 *  world's OPEN SESSION rather than as a handle on the NPC (family **Anim**'s
 *  `HasLiveDialogPartner` made the same reading), and tearing a live session down from here would
 *  be a second producer of dialogue teardown. Counted, and clears nothing. */
int32 DialogPartnerClears = 0;

// --- The seams ------------------------------------------------------------------------------------

/** The five entry points of `m_pAttackCoordinator` (`+0x65e8`), which is an INDEX here and not an
 *  object. Every one answers the value that makes its caller take retail's refusal arm, and each
 *  names the retail body it stands for:
 *
 *   - `0x1025db50` — `coord[4] < coord[0]`: "the coordinator still has a free melee slot". Answers
 *     false, so slot 602's far arm reads "no room" and returns true.
 *   - `0x1025db70` — slot 599's admission test. Answers false.
 *   - `0x1025dca0` — slot 600's admission test, called with a literal `false` third argument.
 *     Answers false.
 *   - `0x1025ddd0` — the release slot 601 forwards to. Counted through family **Bosses**'
 *     `MeleeCoordinatorReleases`, which already stands for this exact call.
 *   - `0x1025de90` — a linear scan of the coordinator's handle array answering "this NPC is NOT
 *     registered". With an empty coordinator the honest answer is TRUE, which is retail's own
 *     answer for a coordinator that does not hold this NPC.
 */
bool MeleeCoordinatorHasRoom() const;
bool MeleeCoordinatorAdmits599() const;
bool MeleeCoordinatorAdmits600() const;
bool MeleeCoordinatorHoldsMe() const;

/** `DAT_10924a1c`'s melee range, read by retail as `IsCommand() ? _DAT_104454c4 (0.0) : +0x28`:
 *  the ConVar `debug_melee_advance_combatmove_dist`, shipped "100". SOURCE units. */
static float MeleeRangeUnits();

/** `_DAT_10451acc` — the height difference slot 599 compares `m_flEnemyHeightDiff` against, the
 *  pooled 64.0f (family Schedule's melee height-difference threshold too). */
static float MeleeHeightDiffLimitUnits();

/** `(*DAT_10924edc)->vfunc1()` — `ent_trace_melee`'s `IsCommand()`, answer dropped (no observable; RE-BACKLOG 41) slots 599 and 600 fire. It is the
 *  same global family **Bosses** counts through `MeleeEventFires`, and this family increments that
 *  counter rather than standing a second one. Declared here only to name the two call sites. */

/** `thunk_FUN_102bf5d0(this, attacker)` — the ally notice slot 322 runs before it asks the melee
 *  coordinator. Family **Squad** ported it as `NoticeAttackerNearby`; slot 322 CALLS it. */

/** `DAT_10937cf2` — the global byte slot 334 clears on entry and sets on its success arm. A retail
 *  GLOBAL, not a per-NPC word, and ported as one (a file static): a per-NPC copy would be a
 *  divergence. This is its read side, for the test and the debug layer. */
static bool DisciplineReadyFlag();

/** SEAM for `CBaseCombatCharacter::LookupExpressionIndex(name)` — the resolve slot 610 runs for
 *  each of its four literal expression names. This runtime NAMES expressions (family
 *  **Conditions**' `DefExpression` and 29b's `NoDeformExpression` both say so), so there is no index
 *  to answer: it answers `INDEX_NONE`, and slot 610 stores the NAMES. */
int32 LookupExpressionIndex(const TCHAR* ExpressionName) const;

/** SEAM for the three disposition-table reads slot 610's DEFAULT arm makes — `0x100ec360` (the
 *  expression index), `0x100ec2e0` (the no-deform index) and `0x100ec3d0` (the blend weight), all
 *  against `&DAT_10924980` keyed on `m_nCurrDisposition` (`+0x64d4`). Family **Anim** records the
 *  same table as unreachable from the kernel; this answers false and leaves all three untouched. */
bool DispositionExpressionRow(FString& OutExpression, FString& OutNoDeformExpression,
	float& OutBlendWeight) const;

/** SEAM for `thunk_FUN_1025e120(ptr)` — the coordinator's NAME accessor slot 608 compares its
 *  argument against, twice per candidate. Answers the empty string, so no candidate ever matches
 *  and slot 608 answers false with `m_pAttackCoordinator` left alone. NOT `AttackCoordinatorName`:
 *  that name is the `+0x65ec` data member 29b declared, which slot 608 WRITES. */
static FString AttackCoordinatorNameOf(int32 CoordinatorIndex);

/** `+0x10b8` — the expression BLEND WEIGHT slot 610 writes beside the two expression words
 *  (`+0x10b4`, family **Conditions**' `DefExpression`, and `+0x64d0`, 29b's `NoDeformExpression`).
 *  Below the shape map's band, so 29b did not bind it, and no landed family declared it. The three
 *  values slot 610 can write are `0.5` and `1.0` and the disposition table's own. */
float ExpressionBlendWeight = 0.f;

/** `DAT_1090fbec` / `DAT_1090fbf0` / `DAT_1090fbf4` — the three global coordinator pointers slot
 *  608 walks, in retail's order. They are POINTERS in retail and INDICES here; the table below is
 *  the index set, and `0` is retail's own "this global is null, skip it" value. */
static const int32* AttackCoordinatorIndices(int32& OutCount);

/** `thunk_FUN_102d1180(hint, this, out)` — `CAI_Hint::GetPosition` as `ApplyHintLeanOffset`
 *  (`0x102b6120`), `EyeOffset` and the cower arm read it: `HintPositionCm` (the one body) in SOURCE
 *  units (`cm / U`). False, the point untouched, only for an index that names no live hint. */
bool HintStandPosition(int32 HintNode, FVector& InOutPointUnits) const;

/** `thunk_FUN_102d1350(hint, this)` — the hint CLAIM `FindTacticalHintNode` takes on its winner; a
 *  false answer drops the node again. Forwards to `FElysiumNpcBase::ClaimHint` (the live hint's
 *  `m_hHintOwner`). */
bool ClaimHintNode(int32 HintNode);

/** SEAM for slot 214 (vtable `+0x358`), the record `ApplyHintLeanOffset` reads its crouch/stand
 *  scale out of at `+0x04`. The slot is unidentified in the census; answers `0.0`. */
float LeanScaleRecordField() const;

/** SEAM for `thunk_FUN_102e0290(pathfinder, target, out, outRatio)` — the lead-ratio query
 *  `ComputeTargetLeadPoint` (`0x102c36d0`) asks the pathfinder (`vtable +0x874`, slot 541) for.
 *  Family **Motor**'s standing fact is that there is no pathfinder here; answers false, which is
 *  retail's own "the query failed" arm and lands on the target's plain origin. */
bool TargetLeadQuery(const FElysiumEntity& Target, FVector& OutPointUnits,
	FVector& OutVelocityUnits) const;

// --- The melee quartet: which retail line this NPC's class takes ----------------------------------

/** Slots 599/600/601/602 each have two shared bodies. `Troika` is `CAI_BaseNPCTroika`'s, `AndreiBlood`
 *  the `CNPC_VAndreiBlood`-line copy (the Human line's overrides since story 5 step 3); `Species` is
 *  a class that replaces the slot outright with its own body. */
enum class EMeleeSlotLine : uint8 { Troika, AndreiBlood, Species };


/** The two body addresses, so a test can name them. */
static const TCHAR* MeleeSlotBody(int32 Slot, EMeleeSlotLine Line);

// --- The bodies -----------------------------------------------------------------------------------
//
// NAMED where the reading settles what the body is FOR (the body writes a named datamap word, or
// fills a slot `signatures.md` names); left spelled `FUN_<address>` where it does not. Every one
// cites its address in the definition.

/** `0x102b6120` — the cover-LEAN position offset. Reads the claimed hint node (`m_pHintNode`
 *  `+0x5ddc`), and for a node of type `0x27d8` offsets the in/out point along the hint's own facing
 *  by `+-_DAT_1049949c` per `m_bLeaningLeft` (`+0x63fd`), scaled by the crouch or stand constant.
 *  `bStanding` is retail's second argument, which selects `_DAT_1049ae90` over `_DAT_1049ae8c`.
 *  SOURCE units. Answers false for an NPC with no hint node, which is the arm that ZEROES the
 *  point. */
bool ApplyHintLeanOffset(FVector& InOutPointUnits, bool bStanding) const;

/** `0x102b7110` — the tactical hint search: find a type-8 hint at the ideal range, retry once when
 *  `m_bStayEntrenched` (`+0x6435`) is set and the first search missed, claim it, decide which side
 *  of it this NPC leans from by a 2-D cross product against the hint's facing, and cache the attack
 *  extents. `SearchType` is retail's one argument. */
bool FindTacticalHintNode(uint32 SearchType);

/** `0x102b8980` — advance `m_eAlertLevel` (`+0x63f4`) one rung and answer the Troika schedule that
 *  rung selects: 0x4c, 0x4d, then 0x51 (`SCHED_TROIKA_INVESTIGATE_SOUND`) or 0x52 (`..._OTHER_SOUND`)
 *  (`MOV EAX,imm32`; `0x102b9060` tail-jumps here at `0x102b9204`, so the value is its answer).
 *  `m_bFullInvestigate` (`+0x6340`) forces the top rung before the switch. */
int32 AdvanceAlertLevelGrade();

/** `0x102bf560` — record who attacked me, unless `m_bIgnoreDetectedAttack` (`+0x65f5`) is set:
 *  `m_hDetectedAttacker` (`+0x65c0`) and `m_flDetectedAttackTime` (`+0x65c4`). NAMED for the two
 *  datamap words it writes, which family **Squad**'s `HasDetectedAttack` is the reader of. */
void RecordDetectedAttack(const FElysiumEntity* Attacker);

/** `0x102bf770` — `TASK_PAUSE_MOVING`'s body: re-apply the link's idle activity when the navigator is
 *  still on the activity this NPC made ideal, then clear `m_bShouldMove` (`+0x1a40`), PAUSE the path
 *  (`0x102ee2a0` -> `0x1030be80`, `path+0x10 = 1`) and zero `m_flDesiredMoveYaw` (`+0x63ec`). NAMED
 *  as the inverse of family **Motor**'s
 *  `ResumeScheduledMove` (`0x102bf7e0`), which is the sibling body. */
void StopScheduledMove();

/** `0x102c0360` — the owning NPC's dialogue-END path, called from `CDialog::Release`
 *  (`docs/vtmb/game_runtime.md` names it): clear `CBaseCombatCharacter`'s in-dialog flag
 *  (`+0x1590`), fire `m_OnDialogEnd` (`+0x5f5c`) and `SetDialogPartner(NULL)`. The unrelated
 *  `entity_debug_stats` tail is a debug hook and is named, not ported. */
void OnDialogRelease();

/** `0x102c36d0` — the predictive aim point, blended by the five `m_flTargetLead*` words
 *  (`+0x655c`..`+0x656c`) this method is NAMED for. `AimFromUnits` is retail's `param_1..3`,
 *  `Lead` its `param_4` (null copies `Fallback` through), `Interval` its `param_5`, `Fallback` its
 *  `param_6` (null skips the trace) and `InOutPointUnits` its `param_7`. SOURCE units. */
void ComputeTargetLeadPoint(const FVector& AimFromUnits, const FElysiumEntity* Lead, float Interval,
	const FVector* FallbackUnits, FVector& InOutPointUnits) const;

/** `0x102c36d0`'s ratio clamp, spelled retail's way rather than as a `Clamp`: `if (r <= Max) { if
 *  (r < Min) r = Min; } else r = Max;`. Pure, so the two bounds are assertable without a
 *  pathfinder. */
static float ClampTargetLeadRatio(float Ratio, float MinRatio, float MaxRatio);

/** `0x102c36d0`'s blend, the same three words on both of its arms:
 *  `(Predicted * PredictedWeight + Other * OtherWeight) * WeightScale`. `Other` is the QUERIED point
 *  on the no-fallback arm and the FALLBACK point on the other — that asymmetry is retail's and is
 *  what the two call sites pass. Pure. */
static FVector BlendTargetLeadPoint(const FVector& PredictedUnits, const FVector& OtherUnits,
	float PredictedWeight, float OtherWeight, float WeightScale);

/** SEAM for `thunk_FUN_10272650(this, activityId)` — `SetActivity(Activity)`, which
 *  `StopScheduledMove` calls with the resolved link activity. This runtime's activity vocabulary is
 *  NAMES (family **Hints** records the same gap for `RestartIdealActivity`), so the id is recorded
 *  and nothing is played. */
int32 SetActivityIdCalls = 0;
int32 LastSetActivityId = INDEX_NONE;

/** `0x102c4380` — `StartIgnoringCollision(other)` then pin `m_flIgnoreCollisionTimer` (`+0x6458`)
 *  to `FLT_MAX`, the "never expire" sentinel.
 *
 *  Family **Bosses** had declared `StartIgnoringCollision(handle)` as a SEAM for this same address
 *  that recorded the call and wrote nothing. This is the body: `IgnoreCollisionUntil` IS a port
 *  member and the shape map binds it, so the write is real here. Bosses' name survives as the
 *  boss-side spelling and now forwards to this body; family Damage's four call sites go through it
 *  too, so all six sites reach one writer. */
void FUN_102c4380(const FElysiumEntity* Other);

/** `0x102c43b0` — renew the ignore-collision window to `Seconds + curtime`, but ONLY while an
 *  ignored entity is still held, then run the expiry check. Family **Bosses**' name for the same
 *  address is `ArmIgnoreCollisionExpiry`, which forwards here; the same note applies. */
void FUN_102c43b0(float Seconds);

/** `0x102c43f0` — the expiry: once `curtime` reaches `m_flIgnoreCollisionTimer`, stop ignoring the
 *  partner and pin the word to `FLT_MAX` so it never refires. */
void FUN_102c43f0();

/** `0x102c4ad0` — stamp `m_vJumpOrigin` (`+0x649c`), `m_fJumpHeight` (`+0x64b4`) and
 *  `m_vJumpTarget` (`+0x64a8`) for the jump `CNPC_VAsianVampire::SetupJump` then reads. NAMED for
 *  those three datamap words. Below `Backoff` the target IS the goal's origin; at or above it the
 *  target is pulled back along the planar delta by `Backoff` — and retail leaves Z alone on that
 *  arm (`fVar6 - param_3 * 0.0`), which is reproduced verbatim. SOURCE units. */
void SetJumpOriginAndTarget(const FElysiumEntity* Goal, float Height, float Backoff);

/** `0x102aa9e0` -- `TASK_NEXT_PATROL_POINT`'s body (StartTask arms `0x102a3b91` / `0x102a3bac`, over
 *  `m_sppPatrolPath` / `m_sppPatrolPathHunt`). A null cell or an empty one fails the task `0x1d`
 *  (the ASSERT pair `+0x1b44`/`+0x1b48` = `AI_BaseNPCTroika.cpp`:0x3d9c). Otherwise `NextPoint`
 *  (`0x10307b80`); an exhausted path is released (`0x1029f5d0`); the node interest is redrawn on the
 *  same cell (`0x1029f650`, which clears `m_bPatrolPathUseHint` first -- a released cell reads no
 *  node); `TaskComplete(false)`. No slot and no recovered name, so the `FUN_` spelling stands. */
void FUN_102aa9e0(FPatrolPathCell* Cell);

// --- `CAI_Motor`'s own vtable ---------------------------------------------------------------------
//
// Seven bodies on `CAI_Motor`'s OWN vtable (slots 3, 4, 6, 8, 15, 17, 18 there, unrelated to the
// shared entity numbering). Family Motor's standing fact holds: `IElysiumNpcMotor` takes a
// destination, a yaw and a speed and keeps no state of its own, so each body is ported verbatim and
// then asks a seam. `this` is the motor in retail and this leaf in the port, which is why
// `field_0x4` (the owning NPC) reads as `*this`.

// --- `CAI_Navigator`'s own vtable -----------------------------------------------------------------

// --- `CAI_StandoffBehavior`'s own vtable ----------------------------------------------------------
//
// Family **Lifecycle** landed slot 13 (`0x102c7600`) as `StandoffSelect` over the declared
// `FStandoffWords` / `FStandoffConditions` views. These four EXTEND that: the two words those views
// do not carry are on the leaf (`bStandoffRangedCache`, `StandoffDistTooFar`) and every arm below
// goes through the same absent-behaviour posture. The names carry `Standoff` because slot 3 and
// slot 5 here are the BEHAVIOUR's vtable, not the shared entity numbering, and a bare `vfunc3`
// would read as the latter.

// --- The slot bodies that need a named entry point ------------------------------------------------

/** Slot 609's body (`0x102b6b50`) as an INDEX, which is what a hint is in this runtime. The
 *  generated slot answers `void*` and there is no hint object to answer with, so `Slot609` forwards
 *  here and answers null; this is the arm a test drives. `bForce` is retail's one argument and
 *  skips the `m_flNextShootAtHintSearchTime` (`+0x6440`) wait. */
int32 FindShootAtHintNode(bool bForce);
