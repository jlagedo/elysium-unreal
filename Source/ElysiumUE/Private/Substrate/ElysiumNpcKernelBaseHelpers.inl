// Story 29c-1, family **BaseHelpers** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelBaseHelpers.cpp` and the tests in
// `Tests/ElysiumNpcKernelBaseHelpersTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// THIS FAMILY IS `CAI_BaseNPC`'S OWN UNNAMED LAYER — the `0x1025…`–`0x1029…` bodies 29a's naming
// pass could not name. Two things follow from that and are worth stating once:
//
//   * **Five rows are not on the Troika line at all.** `0x1025e780`, `0x1025f1a0`, `0x1025ea00`,
//     `0x10260540`, `0x10260670`/`0x10260750` fill slots on `CAI_BaseHumanoid`'s table and read
//     `CAI_BaseActor`'s own words. `classes.md` gives `CAI_BaseHumanoid` NO entity classname, so no
//     map stands one and `RetailClass()` never answers it; the offsets they touch mean an OUTPUT on
//     the Troika line (`ElysiumNpcKernelShapeMap.cpp` calls `+0x5f44…+0x5fbc` `_IMPLICIT`). They are
//     ported all the same, with `CAI_BaseActor`'s words declared by retail name below, exactly as
//     families Squad and Hints declared the species words their bodies read.
//   * **`signatures.md` names four of them** — `HasActiveLookTargets` (586 on `CAI_BaseActor`),
//     `ValidHeadTarget` (588), `SelectRandomExpressionForState` (589) — and the SDK twin settles
//     four more on the base line. Those carry the name; everything whose concern did not settle
//     keeps its `FUN_<address>` spelling, because inventing a name for an unrecovered concern is the
//     guess `CLAUDE.md` forbids.

// --- `CAI_BaseActor`'s own words, read by the `CAI_BaseHumanoid` rows ------------------------------
//
// Declared by retail name with the class that owns the offset. 29b did not declare them: the shape
// 29b landed is the FLATTENED `CAI_BaseNPCTroika` layout, and on that layout these offsets are the
// NPC's output descriptors. Both readings are true of different classes; the shape map's rows are
// the Troika-line ones and stay as they are.

int32 LatchedPositions = 0;               // +0x5f4c CAI_BaseActor::m_fLatchedPositions
FString ExpressionScene;                  // +0x5f9c CAI_BaseActor::m_iszExpressionScene
FElysiumEntityHandle ExpressionSceneEnt;  // +0x5fa0 CAI_BaseActor::m_hExpressionSceneEnt
FString ExpressionOverride;               // +0x5fa4 CAI_BaseActor::m_iszExpressionOverride
FString IdleExpression;                   // +0x5fa8 CAI_BaseActor::m_iszIdleExpression
FString CombatExpression;                 // +0x5fac CAI_BaseActor::m_iszCombatExpression
FString AlertExpression;                  // +0x5fb0 CAI_BaseActor::m_iszAlertExpression
FString DeathExpression;                  // +0x5fb4 CAI_BaseActor::m_iszDeathExpression

// --- `CAI_BaseNPC::m_UnreachableEnts` (`+0x5d48`, count `+0x5d54`) --------------------------------
//
// NOT declared here. Family **Positions** already stands the list as `FUnreachableEntity` /
// `UnreachableEnts` (`ElysiumNpcKernelPositions.inl`) for `IsUnreachable`'s sweep, which is the
// READ side of this family's `RememberUnreachable` (`0x10274080`) — the write side. One list, two
// bodies, and re-declaring it would have been the drift this story exists to end.

// --- Seams ----------------------------------------------------------------------------------------
//
// Each answers NOTHING and names the retail call it stands for. Nothing below invents a value.

/** SEAM for `CAI_Hint` `+0x8c0`… no: `GetActiveWeapon()->+0x8c0`, the ACTIVE WEAPON's own maximum
 *  range in SOURCE units, which `0x10296c40`'s distance band uses as its upper bound alongside the
 *  hint's `m_flTargetDistMax`. No port weapon record carries a range, so this answers false and the
 *  weapon half of that `||` cannot be evaluated; the hint half still is, and the body says so. */
bool ActiveWeaponMaxRangeUnits(float& OutRangeUnits) const;

/** SEAM for `0x1029f6c0` — the patrol node's interesting-place record, whose `+0x46c` is the
 *  `ip_percent` chance `0x1029f650` rolls against and whose pointer `0x1029f730` caches at `+0x659c`.
 *  There is no patrol-node graph here (family Hints stands the same absence from the name side,
 *  `PatrolNodeInterestRecordName`), so this answers `INDEX_NONE`. */
int32 PatrolNodeInterestRecord(int32 PatrolNode) const;

/** SEAM for that record's `+0x46c m_iIPPercent`. Answers 0 — the chance that never fires — which is
 *  distinct from "no record" above. */
int32 PatrolNodeInterestPercent(int32 Record) const;

/** SEAM for `0x102968f0`, the hint LOS check `0x10295ed0` tails into and `0x10296c40` runs under
 *  `m_bForceCoverLOSCheck`. Takes the hint being validated and the entity being covered from.
 *  Answers true, which is retail's PASS arm — the trace seam behind it reports a clear line. */
bool HintLosCheck(int32 HintNode, const FElysiumEntity* Target) const;

/** SEAM for `0x102d1180(hint, npc, &out)` — the point on a hint `0x102961a0` runs its LOS ray to.
 *  Answers false; the caller then has no endpoint and takes its own refusal. */
bool HintLosEndpoint(int32 HintNode, FVector& OutPointCm) const;

/** SEAM for `DAT_10925444`, the `ai_debug_npc` handle every reason-string arm of `0x102961a0` and
 *  `0x10296c40` compares against `this` before formatting anything. Answers false, so the reason
 *  strings are never built — which is retail's own behaviour for every NPC but the one being
 *  debugged. The REASONS are still decided and returned (`FHintRejectReason` below), because the
 *  reason is what the walk recovered and a test has to be able to read it. */
bool IsHintDebugNpc() const;

/** SEAM for `GetCurTask()->iTask` (`0x1028a150`), the retail task NUMBER of the running program's
 *  current step. `EElysiumTask` carries no registered numbers, so this answers false and
 *  `IsCurTaskContinuousMove` reads that as retail's "a task that is not one of the three". */
bool CurrentRetailTaskNumber(int32& OutTaskNumber) const;

/** SEAM for `0x102d1540` — "is this hint free, or already mine?". Retail: the hint's `m_hHintOwner`
 *  (`+0x5e0`) is me, or `curtime >= m_flNextUseTime` (`+0x5ec`) and the owner handle is dead. Family
 *  Hints ports the same three words as `IsHintUnusable` from the other side; this is the OWNER-only
 *  half `IsUnusableNode` (slot 527) negates, and it answers true (free) with no hint store. */
bool IsHintAvailableToMe(int32 HintNode) const;

// --- The rejection reasons the two verbose validators decide --------------------------------------
//
// `0x102961a0` and `0x10296c40` each end every failing arm by formatting a named string onto the
// hint (`0x102d0ab0`) and a passing one by clearing it (`0x102d0b20`). The STRING is a debug
// artefact gated on `ai_debug_npc`; the REASON is the recovered decision, so the bodies answer it
// and the strings are reproduced verbatim beside each arm.
enum class EHintRejectReason : uint8
{
	None = 0,            // the hint passed
	NoHint,              // a null node, or the seam could not resolve it
	Disabled,            // "Disabled"                          — m_iDisabled != 0
	TargetNameMismatch,  // "Target name mismatch (%s)"         — 0x102961a0 only
	NoCoverObject,       // "No cover object"                   — 0x102961a0 only
	NoActiveWeapon,      // "No active weapon"                  — 0x10296c40 only
	HeightDiff,          // "Height diff (%d) > %d"             — 0x10296c40 only
	DistanceBelowMin,    // "Distance (%d) < %d"                — 0x10296c40
	DistanceOutOfBand,   // "Distance (%d) < %d or > %d"        — 0x102961a0
	DistanceAboveMax,    // "Distance (%d) > %d or %d"          — 0x10296c40
	Projection,          // "Projection (%.2f) < 0.2"           — 0x10296c40 only
	OutsideGoodRange,    // "Enemy outside of good range (%.2f) <= %.2f"
	InsideBadRange,      // "Enemy inside of bad range (%.2f) >= %.2f"  — 0x10296c40 only
	FailedLos,           // "Failed LOS check (%s)" / "Failed hint LOS"
};

/** Family Hints owns `FHintWords`, the typed view of one `CAI_Hint`'s datamap. Forward-declared
 *  here because this family's `.inl` is included ahead of that one and the pure rules below take it
 *  by reference; reusing it is the point — a second copy of a hint's words is exactly what the two
 *  families must not stand. */
struct FHintWords;

// --- The bodies -----------------------------------------------------------------------------------
//
// `CAI_BaseHumanoid` / `CAI_BaseActor`'s branch first, then `CAI_BaseNPC`'s own.

/** `CAI_BaseHumanoid::vfunc277` (`0x1025e780`), the `SetViewtarget` override: clear bit 0 of
 *  `m_fLatchedPositions` (`+0x5f4c`), then chain `CBaseFlex::SetViewtarget` (`0x100b5b00`, slot 277).
 *  Retail name unrecovered for the OVERRIDE — slot 277 is `SetViewtarget` and that name belongs to
 *  the generated virtual, which carries the base body, not this one. */
void FUN_1025e780(const FVector& ViewTarget);

/** `CAI_BaseActor::HasActiveLookTargets` (`0x1025f1a0`, `CAI_BaseHumanoid#586`) — the look-queue
 *  count at `+0x5f94` is non-zero. NAMED from `signatures.md`'s `CAI_BaseActor` row for slot 586.
 *  The queue itself is family Facing's `LookTargets`; this asks it rather than standing a second. */
bool HasActiveLookTargets() const;

/** `CAI_BaseActor::ValidHeadTarget(const Vector&)` (`0x1025ea00`, `CAI_BaseHumanoid#588`). NAMED
 *  from `signatures.md`. NOT `Slot588`: the Troika line's slot 588 is `0x10293e50`, a different
 *  table and a different body, and the generated `Slot588()` takes no argument.
 *
 *  SPELLED `…BaseActor` because family **Lifecycle** already declares `ValidHeadTarget(const
 *  FVector&)` as a SEAM for `thunk_FUN_10325da0` (vtable `+0x930`), which is `CBaseCombatCharacter`'s
 *  own 149-byte body and not this one. Two classes, two bodies, one retail name — the port cannot
 *  give both the bare name and this is the one with an address behind it. */
bool ValidHeadTargetBaseActor(const FVector& LookTargetPosCm) const;

/** `CAI_BaseActor::SelectRandomExpressionForState(NPC_STATE)` (`0x10260540`,
 *  `CAI_BaseHumanoid#589`). NAMED from `signatures.md`. Answers a POINTER so retail's three answers
 *  stay distinct: null (the state authors none), an empty string (`DAT_106b8540`, the `string_t`
 *  null sentinel) and the authored expression. */
const FString* SelectRandomExpressionForState(int32 NpcState) const;

/** `CAI_BaseActor::SetExpression(const char*)` (`0x10260670`). NAMED: the body is the SDK 2013
 *  function arm for arm — the empty/null clear, the `stricmp` no-op, then
 *  `InstancedScriptedScene` into `m_hExpressionSceneEnt` and the pooled string only on a live
 *  handle. */
void SetExpression(const FString& SceneName);

/** `CAI_BaseActor::ClearExpression` (`0x10260750`). NAMED by the same twin. Retail's 11-byte body is
 *  ONLY `m_iszExpressionScene = NULL`; SDK 2013's also stops the scene, and retail's does not. */
void ClearExpression();

/** `CAI_BaseNPC::GetNavTargetEntity` (`0x102729d0`). NAMED: the SDK twin's two arms
 *  (`GOALTYPE_ENEMY` -> `GetEnemy()`, `GOALTYPE_TARGETENT` -> `GetTarget()`) are retail's modes 2
 *  and 1 exactly; retail adds a third, mode 7 `GOALTYPE_COVER`, through the navigator. */
FElysiumEntity* GetNavTargetEntity() const;

/** `CAI_BaseNPC::RememberUnreachable` (`0x10274080`). NAMED by the SDK twin, arm for arm: a
 *  BACKWARD scan for an existing record refreshes its expiry, a miss appends one, and the entity's
 *  current position is written either way. The duration is baked (`_DAT_10449258`). */
void RememberUnreachable(FElysiumEntity* Entity);

/** `CAI_BaseNPC::SetDefaultEyeOffset` (`0x10274ca0`). NAMED by the SDK twin and by its own string,
 *  `"WARNING: %s has no eye offset in .qc!"`. */
void SetDefaultEyeOffset();

/** `CAI_BaseNPC::GetScriptCustomMoveActivity` (`0x10289fe0`). NAMED by the SDK twin: the cine's
 *  `m_iszCustomMove` (`+0x5f50` on `m_hCine`) as an activity, else as a sequence, else `ACT_WALK`.
 *  Returns a retail `Activity` number — 9 `ACT_WALK` or 0x18 `ACT_SCRIPT_CUSTOM_MOVE`. */
int32 GetScriptCustomMoveActivity() const;

/** `0x1028ebc0` — can I see this point? `FInViewCone`, then a squared-distance test against
 *  `m_flVisionDistance` (`+0x63b8`), then a clear trace. Retail name UNRECOVERED and the corpus
 *  records no caller for it, so the spelling stays the address. */
bool FUN_1028ebc0(const FVector& PointCm) const;

/** `IsThinkDue` (`0x10290660`) over the three named clocks `0x102906a0` (`m_flNextUpdateTime`
 *  `+0x6244`), `0x102906c0` (`m_flNextNormalTime` `+0x6248`) and `0x10290700` (`m_flNextAITime`
 *  `+0x6250`). Each retail body is a 13-byte forward and nothing else; the NAME of each forward is
 *  unrecovered but its CONCERN is settled by the clock it names, so they are spelled by clock. */
bool IsUpdateThinkDue() const;
bool IsNormalThinkDue() const;
bool IsAiThinkDue() const;

/** `0x1029f610` — a local-move-goal probe: when the goal and its `+0x04` entity are both set, is
 *  that entity the one the navigator's path at `+0x2c` is routed through? Retail name UNRECOVERED.
 *  `Goal` is the `AILocalMoveGoal_t*` retail takes, which this substrate has no type for; the
 *  argument is the entity behind its `+0x04`, which is the only word the body reads. */
bool FUN_1029f610(const FElysiumEntity* GoalEntity) const;

/** `0x1029f650` — reset `m_bPatrolPathUseHint` (`+0x65a0`), then roll `Random(0, 99)` against the
 *  patrol node's interesting-place `ip_percent` and set the flag when the roll comes in under it.
 *  Retail name UNRECOVERED; `docs/vtmb/npc-ai/programs.md` reads the flag as the patrol route's
 *  "visit the interesting place at this node" draw. Returns the flag, as retail does. */
bool FUN_1029f650(int32 PatrolNode);

/** `0x1029f730` — the cached read side of the draw above: nothing unless `+0x65a0` stands, then the
 *  record cached at `+0x659c`, resolved on first ask. Retail name UNRECOVERED. */
int32 FUN_1029f730(int32 PatrolNode);

/** `0x10295ed0` — is this cover hint still a valid place to stand relative to `m_hHintCoverObject`?
 *  The quiet twin of `0x102961a0`. Retail name UNRECOVERED. */
bool FUN_10295ed0(int32 HintNode) const;

/** The pure rule of `0x10295ed0` over a hint's words, so every threshold is assertable without a
 *  hint store. `CoverObjectCm` is where `m_hHintCoverObject` is standing, `bIsCurrentHint` is
 *  `hint == m_pHintNode`, `MyOriginCm` is `GetAbsOrigin()`. */
bool CoverHintStillValid(const FHintWords& Hint, const FVector& CoverObjectCm,
	const FVector& MyOriginCm, bool bIsCurrentHint) const;

/** `0x102961a0` — the verbose twin of `0x10295ed0`: the same band and projection over the cover
 *  object, with a `target_name` gate in front and its own inline LOS ray instead of `0x102968f0`,
 *  and a named reason on every rejection. Retail name UNRECOVERED. */
EHintRejectReason FUN_102961a0(int32 HintNode) const;

/** The pure rule of `0x102961a0`, minus the LOS ray (which the trace seam answers). */
EHintRejectReason CoverHintRejectReason(const FHintWords& Hint, const FVector& CoverObjectCm,
	bool bIsCurrentHint) const;

/** `0x10296c40` — is this hint a valid place to attack `Enemy` from, given my active weapon?
 *  Retail name UNRECOVERED. Family Hints stands `ValidateHintCoverRange` as a SEAM naming this
 *  address and deliberately left it refusing; this is the recovered body, and the two should be
 *  joined by the coordinator rather than by an edit to another family's file. */
EHintRejectReason FUN_10296c40(int32 HintNode, const FElysiumEntity* Enemy, float GoodRangeDot,
	float BadRangeDot) const;

/** The pure rule of `0x10296c40`, minus the hint-LOS seam. `EnemyCm` is the enemy's origin,
 *  `MyOriginCm` mine, `bHasActiveWeapon` is `GetActiveWeapon() != NULL`. */
EHintRejectReason AttackHintRejectReason(const FHintWords& Hint, const FVector& EnemyCm,
	const FVector& MyOriginCm, bool bIsCurrentHint, bool bHasActiveWeapon, float GoodRangeDot,
	float BadRangeDot) const;

/** `0x10297a20` — pick a turn-in-place program from the motor's yaw delta and record the pick in
 *  `m_eFaceAnim` (`+0x63e4`) and `m_flFaceYawDiff` (`+0x63e8`). Retail name UNRECOVERED. A THIRD
 *  turn ladder beside the two family Facing already carries (`TurnActivityBaseLadder` `0x10289d10`,
 *  `TurnActivityTroikaLadder` `0x10297640`): different activities, different thresholds, and it
 *  writes two words neither of those touches. */
void FUN_10297a20();

/** The pure rule of `0x10297a20`, as a pick over the yaw delta. */
struct FFaceAnimPick
{
	int32 Activity = 1;     // the retail activity the body makes ideal; 1 is the ACT_IDLE tail
	int32 FaceAnim = 0;     // +0x63e4 m_eFaceAnim — 8 / 6 / 3 / 1 / 0
	bool bRandomDuration = false;  // the three upper rungs draw +0x63e8; the lower two copy the delta
};
static FFaceAnimPick FaceAnimLadder(float YawDelta, TFunctionRef<bool(int32)> HasSequence);

/** `CAI_BaseNPC::FUN_1027e0f0` — the BASE line's slot-532 body: forget the door being opened and
 *  report handled. Retail name UNRECOVERED (`signatures.md`: "no SDK 2013 twin or string names it").
 *  NOT the port's `Slot532`: the generated virtual carries the TROIKA override `0x10290570`, which
 *  is `order.md` layer 11 and story 29d's row, and that body chains to this one. */
bool FUN_1027e0f0();
