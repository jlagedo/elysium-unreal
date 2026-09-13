// Story 29c-1, family **TroikaHelpers** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelTroikaHelpers.cpp` and the tests in
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
// lines; 601 and 602 are NOT, and both differences are recovered and ported (see `MeleeSlotLine`).
// This runtime stands ONE leaf, so the line a body takes is a census lookup
// (`ElysiumNpcKernelClass::BodyOf(Cls, slot)`) and never a second method.
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

/** `+0x630c`, which `ElysiumNpcKernelShapeMap.cpp` calls **ABSENT**. `SetAtCrosswalk`
 *  (`0x102a0b90`) is its one writer and it is written beside `m_bfAINPCFlags |= AT_CROSSWALK`, which
 *  is what names it: the crosswalk node this NPC is standing on. It sits between
 *  `m_iInterestingPlaceGroups` (`+0x62dc`) and `m_iRestorePedLinkNode` (`+0x6310`), so it is a NODE
 *  id like both of those and is carried as one. Nothing reads it yet. */
int32 AtCrosswalkNode = 0;

/** `CAI_Navigator+0x54`, `+0x58` and `+0x60` — the three words `0x102eeb70` resets, written as
 *  `-1`, `-1.0` and `-1.0`. Their retail names are **unrecovered**: the corpus holds the reset and
 *  no reader that pins any of the three. Declared by offset so `FUN_102eea70` / `FUN_102eeac0`
 *  write the words retail writes rather than recording a bare "reset happened".
 *
 *  `CAI_Navigator+0x18` (`NavType`) and `+0x1c` (`bNavFailed`) belong to family **Motor**'s
 *  `FNavigator Navigator` and are read and written through it rather than duplicated here. */
int32 NavigatorWord0x54 = -1;
float NavigatorWord0x58 = -1.f;
float NavigatorWord0x60 = -1.f;

/** The `thunk_FUN_102ddc40(nav+0x28)` route clear at the head of the same reset. **SEAM**: family
 *  Motor's standing fact is that this substrate keeps no route, so the clear is counted and clears
 *  nothing. */
int32 NavigatorRouteClears = 0;

/** `CAI_StandoffBehavior+0x19` — the byte `vfunc3` (`0x102c7410`) reads first and CLEARS on its
 *  two refusal arms. Carried on the leaf because this runtime stands no behaviour object; family
 *  **Lifecycle** made the same call for `FStandoffWords` and this is the one word that view does
 *  not carry. Its retail name is **unrecovered**; 29c read it as "the owner's weapon-drawn cache". */
bool bStandoffRangedCache = false;

/** `CAI_StandoffBehavior+0x50` — the float `vfunc5` (`0x102c7530`) copies into the owner's
 *  `m_flDistTooFar` (`+0x5de4`, `FElysiumNpc::DistTooFar`). Name **unrecovered**; it is the
 *  behaviour's own authored stand-off distance. */
float StandoffDistTooFar = 0.f;

/** `CBaseEntity+0x1fc`, which `vfunc5` writes `2` into. Below the shape map's band and
 *  **unrecovered** — no corpus body in layers 0–9 reads it. Declared by offset so the write lands
 *  somewhere rather than being dropped. */
int32 Field_0x01fc = 0;

/** `+0x0ec0 m_iCurFrenzyCount` (`CBaseCombatCharacter`) — the discipline gate slot 334 reads
 *  first: a body already mid-frenzy refuses every further cast outright. Below the shape map's
 *  band, so 29b did not bind it, and no landed family had a reader for it. Nothing in this runtime
 *  writes it yet. */
int32 CurFrenzyCount = 0;

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

/** `DAT_10924a1c`'s melee range, read by retail as `IsCommand() ? _DAT_104454c4 (0.0) : +0x28`.
 *  **UNRECOVERED**: neither the object's name nor its default is in the corpus. Family **Schedule**
 *  made the same reading for the same global and answers `0.0`; this answers the same number through
 *  the same reasoning, so the two selectors cannot drift. SOURCE units. */
static float MeleeRangeUnits();

/** `_DAT_10451acc` — the height difference slot 599 compares `m_flEnemyHeightDiff` against.
 *  **UNRECOVERED** float; family Schedule records the same cell and the same `0.0`. */
static float MeleeHeightDiffLimitUnits();

/** `(*DAT_10924edc)->vfunc1()` — the global melee-ENTERED event slots 599 and 600 fire. It is the
 *  same global family **Bosses** counts through `MeleeEventFires`, and this family increments that
 *  counter rather than standing a second one. Declared here only to name the two call sites. */

/** `thunk_FUN_102bf5d0(this, attacker)` — the ally notice slot 322 runs before it asks the melee
 *  coordinator. Family **Squad** ported it as `NoticeAttackerNearby`; slot 322 CALLS it. */

/** SEAM for `thunk_FUN_102707d0(entity)` (`0x102707d0`) — the type-3 redirect `RecordDetectedAttack`
 *  resolves its argument through: an entity whose `+0x98` combat-character pointer answers `3` at
 *  vtable `+0x228` is replaced by its vtable `+0x184`. Neither word exists on `FElysiumEntity`, so
 *  this answers the argument unchanged, which is retail's own fall-through. */
const FElysiumEntity* RedirectDetectedAttacker(const FElysiumEntity* Candidate) const;

/** SEAM for `thunk_FUN_101e1250(&DAT_10739a4c, disciplineId, arg)` and `thunk_FUN_101e11c0` — the
 *  global discipline table slot 334 looks a discipline up in, and the cooldown float at record
 *  `+0x2c`. There is no such table on this substrate; `Find` answers `INDEX_NONE` and the row
 *  lookup answers `0.0`. */
int32 DisciplineTableFind(int32 DisciplineId, int32 Level) const;
float DisciplineTableCooldown(int32 RowIndex) const;

/** SEAM for `m_fDisciplineTimers[row]` (`+0x146c`), the per-discipline last-cast stamps slot 334
 *  measures against. Below the shape map's band and with no producer here; answers `0.0`, which
 *  makes every elapsed time `curtime` and so every cooldown expired. */
double DisciplineTimer(int32 RowIndex) const;

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

/** SEAM for `thunk_FUN_102d1180(hint, this, out)` — the hint's own "position me at this node"
 *  query `ApplyHintLeanOffset` (`0x102b6120`) runs before it applies the lean. Answers false and
 *  leaves the point untouched; family **Hints** owns the same absent hint store. */
bool HintStandPosition(int32 HintNode, FVector& InOutPointUnits) const;

/** SEAM for `thunk_FUN_102d12e0(hint)` — the hint's own yaw, which both `ApplyHintLeanOffset` and
 *  `FindTacticalHintNode` turn into a forward vector. Answers false. */
bool HintYaw(int32 HintNode, float& OutYaw) const;

/** SEAM for `thunk_FUN_102d1350(hint, this)` — the hint CLAIM `FindTacticalHintNode` takes on its
 *  winner; a false answer drops the node again. Answers false, which is the drop arm. */
bool ClaimHintNode(int32 HintNode);

/** SEAM for slot 214 (vtable `+0x358`), the record `ApplyHintLeanOffset` reads its crouch/stand
 *  scale out of at `+0x04`. The slot is unidentified in the census; answers `0.0`. */
float LeanScaleRecordField() const;

/** SEAM for slot 550 (vtable `+0x898`) — the IDEAL RANGE `FindTacticalHintNode` searches at.
 *  Answers `0.0`, so the search radius is zero and the absent hint store answers nothing either
 *  way. */
float IdealHintSearchRangeUnits() const;

/** SEAM for slot 16 (vtable `+0x40`), the attack-extent margin `FindTacticalHintNode` caches into
 *  `m_vecSavedSleepExtents` (`+0x65d0`) before `SetAbsoluteAttackExtents`. Answers the zero vector,
 *  which is the margin `FElysiumNpc::SetAttackExtents` already stands for. SOURCE units. */
FVector HintAttackExtentsUnits() const;

/** SEAM for `thunk_FUN_102e0290(pathfinder, target, out, outRatio)` — the lead-ratio query
 *  `ComputeTargetLeadPoint` (`0x102c36d0`) asks the pathfinder (`vtable +0x874`, slot 541) for.
 *  Family **Motor**'s standing fact is that there is no pathfinder here; answers false, which is
 *  retail's own "the query failed" arm and lands on the target's plain origin. */
bool TargetLeadQuery(const FElysiumEntity& Target, FVector& OutPointUnits,
	FVector& OutVelocityUnits) const;

/** SEAM for `thunk_FUN_102ee3f0(m_pNavigator)` — the navigator's current LINK activity
 *  `StopScheduledMove` (`0x102bf770`) compares `m_IdealActivity` (`+0x0ff0`) against. Family
 *  **Motor** records the same absent link object (`NavLinkActivity`); this answers `-1`. */
int32 NavCurrentLinkActivity() const;

/** SEAM for `thunk_FUN_102ee2a0(m_pNavigator)` — the navigator reset the same body ends on.
 *  Counted, and resets nothing. */
int32 NavResets = 0;

/** SEAM for `thunk_FUN_102cc1f0(this, localId)` — `CAI_Behavior::GetSchedule(localId)`, the
 *  behaviour-local schedule id `StandoffVfunc20` / `StandoffVfunc21` compare `m_pSchedule` against.
 *  There is no behaviour-local id space here; answers `None`, so the compare fails and neither body
 *  clears its condition. */
EElysiumScheduleId StandoffScheduleForLocalId(int32 LocalId) const;

/** SEAM for slot 513 (vtable `+0x804`), the owner capability word `StandoffVfunc3` tests
 *  `0x8000000` in. Answers `0`, which CLOSES the gate — and closing it is what clears
 *  `bStandoffRangedCache`, so the refusal is still observable. */
uint32 StandoffOwnerCapabilityWord() const;

/** SEAM for `CBaseAnimating::SelectHeaviestSequence(owner, 8, -1)` — the sequence lookup
 *  `StandoffVfunc3` requires to answer a non-negative index. Answers `INDEX_NONE`. */
int32 SelectHeaviestSequence(int32 Activity, int32 CurrentSequence) const;

/** SEAM for the owner's discipline cast counter, which `StandoffVfunc3` requires to be exactly
 *  `2`. No such counter on this leaf; answers `0`. */
int32 DisciplineCastCounter() const;

/** SEAM for `thunk_FUN_10307b80(param_1->+0x4)` and `thunk_FUN_1029f5d0(param_1)` — the two halves
 *  of `FUN_102aa9e0`'s success arm over an argument this substrate has no type for. The predicate
 *  answers false; the clear is counted. */
bool TaskArgumentNeedsClear(const void* TaskArgument) const;
int32 TaskArgumentClears = 0;

/** SEAM for `thunk_FUN_1029f650(this, param_1)` — the forward `FUN_102aa9e0` makes before it
 *  completes the task. Counted. */
int32 TaskArgumentForwards = 0;

/** SEAM for `thunk_FUN_10312b20(expresser)` — the setup call `CreateExpresser` (`0x10312cd0`) ends
 *  on, and for the factory at vtable `+0x91c` (slot 583) above it. Family **Lifecycle** already
 *  states that there is no expression substrate here (`ExpressiveNpcExpresser`); the factory answers
 *  false, which is retail's null-result arm. */
bool CreateExpresserObject();

/** SEAM for `thunk_FUN_1027cae0(this)` — the base gate `CreateExpresser` opens with. Retail's base
 *  body answers true for a live NPC; this answers `IsAlive()`, which is the same fact through the
 *  slot family **Lifecycle** landed. */
bool ExpresserBaseGate() const;

// --- The melee quartet: which retail line this NPC's class takes ----------------------------------

/** Slots 599/600/601/602 each have two bodies. `Troika` is `CAI_BaseNPCTroika`'s, `AndreiBlood` the
 *  `CNPC_VAndreiBlood`-line copy; `Species` is a class that replaces the slot outright and whose
 *  body is another family's row, which this leaf answers with the Troika line and says so. */
enum class EMeleeSlotLine : uint8 { Troika, AndreiBlood, Species };

/** The line `Slot` takes for this NPC, read off the CENSUS (`ElysiumNpcKernelClass::BodyOf`) rather
 *  than off a hand-typed class list, so the answer is checkable against
 *  `docs/vtmb/npc-kernel/slots.md` by construction. A classname no census class claims — `npc_VCop`
 *  is the recovered example — answers `Troika`, which is the correct fall-through and not a bug. */
EMeleeSlotLine MeleeSlotLine(int32 Slot) const;

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

/** `0x102b8980` — advance `m_eAlertLevel` (`+0x63f4`) one rung and answer the retail GRADE letter
 *  that rung shows. `m_bFullInvestigate` (`+0x6340`) forces the top rung before the switch. */
TCHAR AdvanceAlertLevelGrade();

/** `0x102bf560` — record who attacked me, unless `m_bIgnoreDetectedAttack` (`+0x65f5`) is set:
 *  `m_hDetectedAttacker` (`+0x65c0`) and `m_flDetectedAttackTime` (`+0x65c4`). NAMED for the two
 *  datamap words it writes, which family **Squad**'s `HasDetectedAttack` is the reader of. */
void RecordDetectedAttack(const FElysiumEntity* Attacker);

/** `0x102bf770` — the move is over: re-apply the link's idle activity when the navigator is still
 *  on the activity this NPC made ideal, then clear `m_bShouldMove` (`+0x1a40`), reset the navigator
 *  and zero `m_flDesiredMoveYaw` (`+0x63ec`). NAMED as the inverse of family **Motor**'s
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

/** How many times the expresser factory (vtable `+0x91c`, slot 583) was asked. The seam answers
 *  false, so the count is the only evidence the gate opened. */
int32 ExpresserFactoryCalls = 0;

/** One live `CAI_Motor` facing-queue entry as `FUN_102e2180` reads it: the target position
 *  (`thunk_FUN_102d8a90`) and the blend weight (`thunk_FUN_102d8bc0`). SOURCE units. */
struct FFacingQueueEntry
{
	FVector TargetUnits = FVector::ZeroVector;
	float Weight = 0.f;
};

/** `FUN_102e2180`'s blend, as a pure function of the surviving entries: for each, accumulate
 *  `(target - self) * weight + accumulator * (1 - weight)` and then normalise the accumulator.
 *  Retail normalises the per-entry delta too and then **throws that away**, blending the RAW delta
 *  — the registers it multiplies are loaded before the call. Reproduced verbatim. */
static FVector BlendFacingQueue(TArrayView<const FFacingQueueEntry> Entries,
	const FVector& SelfUnits);

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

/** `0x10312cd0`, slot 424 on `CAI_BaseHumanoid` / `CAI_ExpressiveNPC` — the EXPRESSER factory
 *  (`docs/vtmb/npc-ai/lifecycle.md` names it): the base gate, the vtable `+0x91c` factory, the
 *  back-link to this NPC and to `+0x5f44`, then the setup call. False is retail's own null-factory
 *  answer. */
bool CreateExpresser();

/** `0x102aa9e0` — no slot and no recovered name. When the argument and its `+0x4` member are both
 *  live it runs the clear/forward/`TaskComplete(false)` chain; otherwise it stamps the ASSERT
 *  file/line at `+0x1b44`/`+0x1b48` and raises through the entity's own vtable `+0x700`
 *  (`TaskFail`) with code `0x1d`. `docs/vtmb/npc-ai/programs.md` documents it and the port does not
 *  cite it, so the `FUN_` spelling stands. */
void FUN_102aa9e0(const void* TaskArgument);

/** `0x102a0b90` — set `m_bfAINPCFlags |= AT_CROSSWALK` (`0x4`) and store the crosswalk node at
 *  `+0x630c`. **NAMED**: 29c's target was `Slot0x630c`, but the flag bit this body sets beside the
 *  write is the named `AT_CROSSWALK`, which settles what `+0x630c` is. */
void SetAtCrosswalk(int32 CrosswalkNode);

// --- `CAI_Motor`'s own vtable ---------------------------------------------------------------------
//
// Seven bodies on `CAI_Motor`'s OWN vtable (slots 3, 4, 6, 8, 15, 17, 18 there, unrelated to the
// shared entity numbering). Family Motor's standing fact holds: `IElysiumNpcMotor` takes a
// destination, a yaw and a speed and keeps no state of its own, so each body is ported verbatim and
// then asks a seam. `this` is the motor in retail and this leaf in the port, which is why
// `field_0x4` (the owning NPC) reads as `*this`.

/** `CAI_Motor#3` `0x102e0ea0` — the motor's init: force activity `0x33` through the owner
 *  (vtable `+0x4d8`), reset the motor state, `SetSolid(2)`, zero `m_flGravity` (`+0x3ec`) and
 *  dispatch the owner's `+0x340` with `(0, 5, 0)`. */
void FUN_102e0ea0();

/** `CAI_Motor#4` `0x102e0f90` — ground deceleration toward `GoalUnits`, scaled by `_DAT_10450564`:
 *  under `m_flMoveInterval * scale` it draws the interval down by `distance * _DAT_10450aa4` and
 *  issues the move; at or above it, it zeroes the interval and restarts at speed `-1.0`. Answers
 *  retail's `1` / `0`. */
bool FUN_102e0f90(const FVector& GoalUnits, float Yaw);

/** `CAI_Motor#6` `0x102e1180` — stop and face: `SetAbsVelocity(goal)`, force activity `0x2c`,
 *  then reissue the move at the goal's own yaw and speed `-1.0`. */
void FUN_102e1180(const FVector& GoalUnits);

/** `CAI_Motor#8` `0x102e1270` — the full stop: `SetAbsVelocity(0,0,0)` then force activity `0x30`.
 *  Also fills `CAI_HumanoidMotor#8`. */
void FUN_102e1270();

/** `CAI_Motor#15` `0x102e2180` — the facing-queue average: drop every expired entry (stride
 *  `0x24`), then blend each survivor's target-minus-self delta into a running accumulator by that
 *  entry's own weight against `1.0 - weight`, normalising before and after each blend. Answers the
 *  accumulator in SOURCE units and the number of entries that survived. */
FVector FUN_102e2180(int32& OutSurvivors);

/** `CAI_Motor#17` `0x102e2580` — the move speed: the base speed clamped DOWN to the motor's own
 *  ceiling (vtable `+0x40`) and then UP to the hull table's floor for this hull. SOURCE units. */
float FUN_102e2580() const;

/** `CAI_Motor#18` `0x102e19e0` — the steering write. Guarded by the owner's clip test (vtable
 *  `+0x838`); on a miss it takes the yaw toward the goal, asks whether the playing sequence carries
 *  the named pose parameter `"move_yaw"`, and writes the final yaw either to the owning NPC's
 *  `m_flDesiredMoveYaw` (`+0x63ec`) or through the motor's own pose-parameter setter. */
void FUN_102e19e0(const FVector& GoalUnits);

/** SEAM for the four `CAI_Motor` calls the seven bodies above make that have no mover here:
 *  `thunk_FUN_102e2840` (the state reset), `thunk_FUN_102e2690` (`SetAbsVelocity`),
 *  `thunk_FUN_102e1c10` (reissue the move at a yaw and a speed) and `thunk_FUN_102e12c0` (the base
 *  speed). The first two are counted; the reissue records its arguments; the base speed answers
 *  `0.0`. */
struct FTroikaMotorSeams
{
	int32 StateResets = 0;
	int32 VelocitySets = 0;
	FVector LastVelocityUnits = FVector::ZeroVector;
	int32 MoveReissues = 0;
	float LastReissueYaw = 0.f;
	float LastReissueSpeed = 0.f;
	int32 ForcedActivities = 0;
	int32 LastForcedActivity = INDEX_NONE;
	/** The owner's vtable `+0xf8` (slot 62) move dispatch `FUN_102e0f90` ends its near arm on, and
	 *  the `+0x340` (slot 208) and `SetSolid` calls `FUN_102e0ea0` makes. Unidentified in the
	 *  census; counted. */
	int32 OwnerMoveDispatches = 0;
	int32 OwnerSlot208Dispatches = 0;
	int32 SolidSets = 0;
	/** `m_flMoveInterval`, `CAI_Motor+0x30` — the one motor word these bodies read AND write. */
	float MoveInterval = 0.f;
	/** `CAI_Motor+0x54 m_facingQueue`'s live count, `CAI_Motor+0x3c`. */
	int32 FacingQueueCount = 0;
	/** The owner's clip test, vtable `+0x838`. True REFUSES the steer, which is retail's guard. */
	bool bSteerClipped = false;
	/** `thunk_FUN_102e2820(sequence, "move_yaw")` — does the playing sequence carry the pose
	 *  parameter? False takes the pseudo-yaw arm. */
	bool bHasMoveYawPoseParam = false;
	/** `thunk_FUN_102e27d0(this, "move_yaw", yaw)` — the pose-parameter write, recorded. */
	int32 PoseParamWrites = 0;
	float LastPoseParamYaw = 0.f;
	/** `thunk_FUN_102d61b0(hull)` — the hull table's speed floor. Unrecovered; answers `0.0`. */
	float HullSpeedFloorUnits = 0.f;
	/** The motor's own speed ceiling, vtable `+0x40`. Unrecovered; answers `0.0`. */
	float SpeedCeilingUnits = 0.f;
};
mutable FTroikaMotorSeams TroikaMotor;

// --- `CAI_Navigator`'s own vtable -----------------------------------------------------------------

/** `CAI_Navigator#7` `0x102eea70` and `CAI_Navigator#11` `0x102eeac0` — **byte-identical bodies at
 *  two distinct slots**: the shared `0x102eeb70` reset (`+0x54 := -1`, `+0x58 := -1.0`,
 *  `+0x60 := -1.0`, clear the route at `+0x28`) and then `+0x1c := 1`, family Motor's
 *  `Navigator.bNavFailed`. One method carries both, and the two `FUN_` names forward to it so each
 *  slot still has a body of its own. */
void NavStopAndMarkDirty();
void FUN_102eea70();
void FUN_102eeac0();

/** `CAI_Navigator#17` `0x102eee40` — the move-info block built from the current path: the path
 *  point, the per-axis delta (2-D at nav type 0, 3-D otherwise), its length, the motor's base speed
 *  and the goal tolerance (the motor's `+0x30` scaled by it, floored at the length), the navigator
 *  radius, bit 0 for a straight-line path and bit 2 for a waypoint whose kind differs from its
 *  owner's. SOURCE units. */
struct FNavMoveInfo
{
	FVector TargetUnits = FVector::ZeroVector;   // out[0..2]  the path point
	FVector DeltaUnits = FVector::ZeroVector;    // out[3..5]  target - my origin
	FVector DirUnits = FVector::ZeroVector;      // out[6..8]  the same delta, copied
	float BaseSpeedUnits = 0.f;                  // out[9]
	float DistanceUnits = 0.f;                   // out[10]
	float GoalToleranceUnits = 0.f;              // out[11]
	int32 NavType = 0;                           // out[12]
	float Radius = 0.f;                          // out[13]
	uint32 Flags = 0;                            // out[14] bit0 straight line, bit2 kind change
	bool bHasPath = false;                       // out[15] — the path pointer retail stores
};
FNavMoveInfo FUN_102eee40() const;

/** SEAM for the `CAI_Path` reads `0x102eee40` makes: the current path point (`0x10012805`), the
 *  straight-line test (`0x1030bd50`), the navigator radius (`0x102ecc40`) and the next waypoint's
 *  kind against its owner's (`path+0x24`, `+0x30`, `+0x2c`). There is no path object here; the
 *  whole block answers false and the move info comes back with `bHasPath` clear. */
struct FNavPathSample
{
	bool bValid = false;
	FVector PointUnits = FVector::ZeroVector;
	bool bStraightLine = false;
	bool bNextWaypointKindDiffers = false;
	float RadiusUnits = 0.f;
};
FNavPathSample NavPathSample() const;

// --- `CAI_StandoffBehavior`'s own vtable ----------------------------------------------------------
//
// Family **Lifecycle** landed slot 13 (`0x102c7600`) as `StandoffSelect` over the declared
// `FStandoffWords` / `FStandoffConditions` views. These four EXTEND that: the two words those views
// do not carry are on the leaf (`bStandoffRangedCache`, `StandoffDistTooFar`) and every arm below
// goes through the same absent-behaviour posture. The names carry `Standoff` because slot 3 and
// slot 5 here are the BEHAVIOUR's vtable, not the shared entity numbering, and a bare `vfunc3`
// would read as the latter.

/** `CAI_StandoffBehavior::vfunc3` `0x102c7410` — may this standoff do its ranged thing? Reads
 *  `+0x19` first (false there answers false outright), then the owner's capability bit `0x8000000`,
 *  then a non-negative `SelectHeaviestSequence(owner, 8, -1)`, then `m_iDisciplineCastCounter == 2`
 *  AND a live active weapon. Both of the two middle refusals CLEAR `+0x19`. */
bool StandoffVfunc3();

/** `CAI_StandoffBehavior::vfunc5` `0x102c7530` — release the owner's claimed hint node
 *  (`+0x5ddc`) when it is live and this behaviour owns it, zero it, copy the behaviour's `+0x50`
 *  into the owner's `m_flDistTooFar` (`+0x5de4`) and force the owner's `+0x1fc` to `2`. */
void StandoffVfunc5();

/** `CAI_StandoffBehavior::vfunc20` `0x102c7960` and `vfunc21` `0x102c79a0` — **byte-identical
 *  bodies at two distinct slots**: when the running program is the behaviour's local schedule
 *  `0x17`, clear `COND_NEW_ENEMY` (`0x54`). One method, two entry points, as the navigator pair
 *  above. */
void StandoffClearNewEnemyOnLocalSchedule();
void StandoffVfunc20();
void StandoffVfunc21();

// --- The slot bodies that need a named entry point ------------------------------------------------

/** Slot 609's body (`0x102b6b50`) as an INDEX, which is what a hint is in this runtime. The
 *  generated slot answers `void*` and there is no hint object to answer with, so `Slot609` forwards
 *  here and answers null; this is the arm a test drives. `bForce` is retail's one argument and
 *  skips the `m_flNextShootAtHintSearchTime` (`+0x6440`) wait. */
int32 FindShootAtHintNode(bool bForce);
