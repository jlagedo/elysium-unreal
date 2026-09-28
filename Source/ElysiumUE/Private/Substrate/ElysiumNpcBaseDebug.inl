// `CAI_BaseNPC`'s declarations of the `Debug` family (story 5 step 5),
// moved from `ElysiumNpcDebug*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseDebug.cpp`.

/** The four channels. `RetailFormat` is retail's literal; `Text` is the formatted result. */
static void EmitDevMsg(const TCHAR* RetailFormat, const FString& Text);

static void EmitDebugMsg(const TCHAR* RetailFormat, const FString& Text);

static void EmitEntityText(int32 Line, const TCHAR* RetailFormat, const FString& Text);

/** `NDebugOverlay::Box` (`0x10142aa0` / `0x10143b70`) — SOURCE units, retail's own colour bytes.
 *  There is no renderer here: the call is recorded and draws nothing. */
static void EmitOverlayBox(const TCHAR* RetailCall, const FVector& OriginUnits,
	const FVector& MinsUnits, const FVector& MaxsUnits, int32 R, int32 G, int32 B, int32 A);

/** `NDebugOverlay::BoxDirection` (`0x10142af0`) — the same, plus the facing the box is rotated to. */
static void EmitOverlayBoxDirection(const TCHAR* RetailCall, const FVector& OriginUnits,
	const FVector& MinsUnits, const FVector& MaxsUnits, const FVector& Direction, int32 R, int32 G,
	int32 B, int32 A);

/** `NDebugOverlay::Line` (`0x10142e90`). */
static void EmitOverlayLine(const TCHAR* RetailCall, const FVector& StartUnits,
	const FVector& EndUnits, int32 R, int32 G, int32 B, bool bNoDepthTest);

/** `NDebugOverlay::Text` (`0x10143710`). */
static void EmitOverlayText(const TCHAR* RetailCall, const FVector& OriginUnits,
	const FString& Text);

/** `0x1027e7f0` — the SHORT condition-name table `GetShortConditionName` forwards to: three letters
 *  per condition for ids 0x00..0x76, and `"***"` for everything else. Read straight out of
 *  `.rdata` (`0x105cd454`..`0x105cd62c`, plus `0x1058b0a0` for id 0x73). */
static const TCHAR* ShortConditionNameTable(int32 ConditionId);

/** `CAI_BaseNPC::GetSchedulingErrorName` (`0x101a6660`) — slot 451's BASE body,
 *  `return s_CAI_BaseNPC_10594820;`. The Troika line's own (`0x101aa7b0`) fills the slot. */
static const TCHAR* BaseSchedulingErrorName();

/** `0x10275760`'s `0x20000` arm, the enemy-memory label walk, split out because it is a third of
 *  the body. Three exclusive label tests, two independent suffixes, a SECOND colour ladder that is
 *  not the label's, and one of two shapes per record. */
void DrawEnemyMemoryOverlays();

/** `CBaseEntity+0x0098 m_pBaseNPCTroika`, the cached downcast the view-cone arm requires to be NULL.
 *  This runtime stands ONE leaf and it is the Troika line, so the answer is always true and the arm
 *  is dead — which is the recovered answer for every NPC on that chain in retail too. */
bool IsBaseNpcTroika() const;

/** `CAI_Hint::DrawDebugTextOverlays` (`0x102d1600`) — slot 124 on `CAI_Hint`, which is not this leaf
 *  either: the hint's own two words are passed in. Returns the next free line. */
static int32 HintDrawDebugTextOverlays(int32 EntityTextLine, int32 DebugOverlayBits, int32 HintType,
	double NextUseTime, double Now);

/** `CBaseAnimating::GetSeqDesc(m_nSequence)` (`0x1000b4f6`) and the two `studiohdr_t` string offsets
 *  the stat overlay reads off it — `seqdesc + *(int*)seqdesc` (the sequence label) and
 *  `seqdesc + *(int*)(seqdesc+4)` (its activity name). **SEAM**: family Anim already records that
 *  this runtime stands no studio header; answers false, which is retail's `"(INVALID)"` arm. */
bool SequenceDescriptor(int32 Sequence, FString& OutLabel, FString& OutActivityName) const;

/** The four-step name resolve both stat bodies and `ReportAIState` apply to an activity NUMBER:
 *  slot 375 `NPC_EarlyTranslateActivity`, slot 381 `Weapon_TranslateActivity`, slot 376
 *  `NPC_TranslateActivity`, then `SelectWeightedSequence(act, -1)` and
 *  `GetSequenceActivityName(seq)`. **SEAM**: the whole chain ends in the studio header the line
 *  above refuses, so it answers the empty string. The three translation slots are NOT called
 *  through here: two of them are ported and one is a stub that would answer 0 and turn a valid
 *  activity into `ACT_RESET`, which is a port artefact and not retail's answer. */
FString ActivityNameForNumber(int32 Activity) const;

/** `CAI_BaseNPC::GetNavType()` (`0x1027d990`), whose answer the stat overlay names through slot 407.
 *  **SEAM**: the motor carries no `Navigation_t`; answers -1, which slot 407 names `"None"`. */
int32 RetailNavType() const;

/** `CAI_Navigator::DrawDebugRouteOverlay(m_pNavigator)` (`0x102f28e0`), the `0x4000` arm.
 *  **SEAM**: `m_pNavigator` (+0x5d34) is a CHAIN row onto the motor and no route store exists. */
void DrawNavigatorRouteOverlay() const;

/** The `0x2000` arm's node resolve: stamp the navigator's `+0x8`/`+0xc` scratch, then
 *  `CAI_Pathfinder::NearestNodeToNPC` (`0x102f3c10`) and `CAI_Node::GetPosition(hull)`
 *  (`0x102fb0d0`). **SEAM**: no node graph here; answers false and `OutUnits` is untouched. */
bool NavigatorNearestNodePositionUnits(FVector& OutUnits) const;

/** `CAI_Pathfinder::DrawDebugGeometryOverlays(m_pPathfinder, m_debugOverlays)` (`0x103061e0`), the
 *  tail of slot 123. **SEAM**: `m_pPathfinder` (+0x5d3c) is a CHAIN row; nothing to draw. */
void DrawPathfinderDebugOverlays(int32 DebugOverlayBits) const;

/** `CBaseEntity::DrawDebugGeometryOverlays` / `DrawDebugTextOverlays` / `DrawBBoxOverlay`, the base
 *  calls every one of this family's slot-123/124/620 bodies ends on. **SEAM**: `FElysiumEntity`
 *  stands none of the three; the text one answers 0, which is retail's "no lines used yet". */
void EntityDrawDebugGeometryOverlays() const;

int32 EntityDrawDebugTextOverlays() const;

/** `m_Collision`'s vtable `+0x4` / `+0x8` — `OBBMins()` / `OBBMaxs()`, which slot 123's `0x1000` arm
 *  compares component-wise to decide between the real box and a default ±5 one. **SEAM**: the port
 *  has no collision extents on the kernel surface (family Motor's `RetailCollisionExtents` says the
 *  same); answers false, which takes the DEGENERATE arm — the one retail takes for an entity whose
 *  OBB is a point. */
bool CollisionObbExtentsUnits(FVector& OutMinsUnits, FVector& OutMaxsUnits) const;

/** One `CAI_LocalIdSpace` in a chain, with the class whose space it is and the global's address so
 *  a reader can check the row. */
struct FKernelIdSpace
{
	const TCHAR* RetailClass = nullptr;
	// The `CAI_LocalIdSpace` global, `0x10……`.
	const TCHAR* IdSpace = nullptr;
	int32 GlobalBase = INDEX_NONE;
	int32 LocalBase = 9999;
	int32 LocalTop = INDEX_NONE;
};

/** `CAI_ClassScheduleIdSpace::ConditionLocalToGlobal` / `TaskLocalToGlobal` (`0x102ea2d0`): -1 stays
 *  -1; otherwise walk the chain from `Rows[0]` and answer `(GlobalBase - LocalBase) + LocalId` for
 *  the first space whose `[LocalBase, LocalTop]` holds it, else -1. */
static int32 IdSpaceLocalToGlobal(const FKernelIdSpace* Rows, int32 Count, int32 LocalId);

/** The CONDITION chain, `CAI_BaseNPCTroika` first then `CAI_BaseNPC`. Two rows. */
static const FKernelIdSpace* ConditionIdSpaceRows(int32& OutCount);

/** The TASK chain, the same two classes. Both rows are empty; see the definition. */
static const FKernelIdSpace* TaskIdSpaceRows(int32& OutCount);

/** `CAI_GlobalNamespace::IdToSymbol(&DAT_109203dc, id)` (`0x102ea020`) — the one CONDITION
 *  namespace, seeded by `CAI_BaseNPC`'s registrar `0x102c8ce0` with exactly 119 `COND_*` symbols at
 *  global ids 0x00..0x76. `"<<null>>"` for -1, and null for an id the namespace does not carry
 *  (which retail then hands straight to `printf`). */
static const TCHAR* GlobalConditionName(int32 GlobalConditionId);

/** `CAI_GlobalNamespace::IdToSymbol(&DAT_109203d4, id)` (`0x102ea020`) — the TASK namespace.
 *  **SEAM**: its 441 `TASK_*` symbols are registered by `0x10316ff0`, and this runtime's task
 *  vocabulary (`EElysiumTask`) carries no registered numbers at all — `CurrentRetailTaskNumber`
 *  already answers false for the same reason. So this answers `"<<null>>"` for -1 and null for every
 *  other id. */
static const TCHAR* GlobalTaskName(int32 GlobalTaskId);

/** `CAI_Motor::m_YawSpeed` (`m_pMotor` `+0x38`), what `ReportAIState` prints as `Yaw speed`: the
 *  word `MotorYawSpeedWord` (RunTask19's `0x102e1c10` writes it). */
float MotorYawSpeed() const;
