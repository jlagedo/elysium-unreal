// Story 29c-1, family **Debug** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelDebug.cpp` and the tests in
// `Tests/ElysiumNpcKernelDebugTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.

// --- What a debug body IS, for this family --------------------------------------------------------
//
// Twenty-one rows, and every one of them is a `DevMsg`, a debug-message ring write, an entity text
// overlay or an `NDebugOverlay` draw. What a program can observe about such a body is NOT the
// picture: it is the ORDER in which the body reads state, the GATES under which it says anything at
// all, and the side effects it performs on the way (`DrawDebugGeometryOverlays`'s `0x10000` arm
// vacates a squad slot, drops the weapon and schedules the NPC for removal — a debug bit with real
// consequences). All three are ported verbatim below; retail's own format strings are reproduced as
// strings because the text is the evidence for the arm.
//
// This runtime has no debug renderer and no `Msg` ring, so every one of the four retail output
// channels lands on ONE seam — `FDebugLine` — which writes `LogElysiumNpcEnt` and, while a capture
// is open, records the line. The capture is what a test reads: a body's recovered arm order is a
// list of `FDebugLine`s, and `Retail` carries retail's own format string so an assertion names the
// evidence rather than the port's paraphrase.

/** One line a debug body emitted. */
struct FDebugLine
{
	// The retail output channel this line went to. One of the four spellings below.
	//   `DevMsg`     — `DevMsg(fmt, …)`, the dev console (`CAI_BaseNPC::ReportAIState`).
	//   `Msg`        — the ConVar-gated debug-message ring `0x10119750` writes 0x60 bytes into
	//                  (`DrawDebugStatOverlays`'s whole output).
	//   `EntityText` — `DAT_1070b22c`+0x8c, `IVEngineServer::AddEntityTextOverlay(edict, line, …)`.
	//   `Overlay`    — `NDebugOverlay::Box` / `BoxDirection` / `Line` / `Text` / `EntityBounds`.
	const TCHAR* Channel = nullptr;
	// Retail's own format string, or the retail overlay call's name, verbatim. This is the evidence
	// that the arm was taken; a test asserts on it rather than on the formatted text.
	const TCHAR* Retail = nullptr;
	// The formatted line, or the overlay call's arguments spelled out in SOURCE units.
	FString Text;
	// `EntityText`'s line index, `INDEX_NONE` on every other channel. Retail's text overlays are
	// numbered from whatever `CBaseEntity::DrawDebugTextOverlays` returned, and the numbering is the
	// contract between a base body and its override.
	int32 Line = INDEX_NONE;
};

/** Open a capture. Every `FDebugLine` a debug body emits until `EndDebugCapture` is recorded in
 *  emission order. Game-thread only, like the rest of the substrate; nesting is not supported and a
 *  second `Begin` simply resets the list. */
static void BeginDebugCapture();

/** Close the capture and hand back what was recorded, in emission order. */
static TArray<FDebugLine> EndDebugCapture();

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

/** `NDebugOverlay::EntityBounds` (`0x10142e20`) — the whole-entity box `CBaseEntity::DrawBBoxOverlay`
 *  draws, which `CNPC_VWerewolf#620` recolours. */
void EmitOverlayEntityBounds(const TCHAR* RetailCall, int32 R, int32 G, int32 B, int32 A) const;

// --- The two id spaces `ConditionName` and `TaskName` translate through --------------------------
//
// `CAI_ClassScheduleIdSpace` is four `CAI_LocalIdSpace`s in a row — schedules at `+0x00`, tasks at
// `+0x18`, conditions at `+0x30`, squad slots at `+0x48` (family Schedule's `FScheduleIdSpace` states
// the same layout, and family Squad's table reaches the fourth). Each is
// `+0x00 m_globalBase`, `+0x04 m_localBase`, `+0x08 m_localTop`, `+0x10` the PARENT space, and 9999
// in `LocalBase` is retail's "this space holds no ids" sentinel, which `0x102ea2d0` tests by name.

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

// --- The three global name tables -----------------------------------------------------------------

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

/** `0x1027e7f0` — the SHORT condition-name table `GetShortConditionName` forwards to: three letters
 *  per condition for ids 0x00..0x76, and `"***"` for everything else. Read straight out of
 *  `.rdata` (`0x105cd454`..`0x105cd62c`, plus `0x1058b0a0` for id 0x73). */
static const TCHAR* ShortConditionNameTable(int32 ConditionId);

// --- Slot 408 `GetShortConditionName`: one port method and a species table -----------------------
//
// Four retail bodies fill slot 408. `CAI_BaseNPC`'s (`0x1027ede0`) forwards to the table above, and
// three species prepend a contiguous block of their OWN ids above the base's 0x76 and fall through
// to that same forward for everything else. So this is one method plus a data table, not four
// methods; `ElysiumNpcKernelClass::OverrideOf(RetailClass(), 408)` picks the row.

/** One row of retail's slot-408 species table. */
struct FShortConditionSpecies
{
	// The census class this row came from (`docs/vtmb/npc-kernel/slots.md`).
	const TCHAR* RetailClass = nullptr;
	// The retail body that fills slot 408 for it, `0x10……`; checkable against `slots.md`.
	const TCHAR* Body = nullptr;
	// Whether the body pushes a scope trace around itself before answering. Only
	// `CNPC_VWerewolf`'s does (`g_ScopeTraceStack` + `m_iName`), and it pops it on every arm.
	bool bScopeTraced = false;
	// The first condition id the block covers — 0x77 on all three, the id straight above the base
	// table's last.
	int32 FirstId = 0x77;
	// The block, in id order. `Names[i]` is the short name for `FirstId + i`.
	const TCHAR* const* Names = nullptr;
	int32 NameCount = 0;
};

/** The table: the three census classes that override slot 408. */
static const FShortConditionSpecies* ShortConditionSpeciesRows(int32& OutCount);

/** The row for a retail class name, or null when no row carries it. */
static const FShortConditionSpecies* ShortConditionSpeciesOf(const TCHAR* InRetailClass);

// --- Slot 76 `DrawDebugStatOverlays`: three bodies, one slot ---------------------------------------
//
// `CAI_BaseNPC#76` (`0x102775e0`) is the sequence/activity/state/schedule dump. `CAI_BaseNPCTroika#76`
// (`0x1029c010`) replaces it with the expression/gesture dump — but only when `m_iDialog` is set,
// and otherwise TAIL-CALLS the base. `CNPC_VBaseBoss#76` (`0x10366290`) prints one distance line and
// then calls the BASE body directly, skipping the Troika dump entirely. The slot dispatches; the
// three arms are named below.

/** `CAI_BaseNPC::DrawDebugStatOverlays` (`0x102775e0`) — slot 76's sdk-tier base body. */
void BaseDrawDebugStatOverlays();

/** `CAI_BaseNPCTroika::DrawDebugStatOverlays` (`0x1029c010`) past its `m_iDialog == 0` arm — the
 *  expression/gesture dump proper. Slot 76 takes this arm only for an NPC that has a dialogue. */
void TroikaDrawDebugStatOverlays();

/** `CNPC_VBaseBoss::DrawDebugStatOverlays` (`0x10366290`) — `"Dist to player: %.3f"` then
 *  `0x102775e0`. Thirty-six bytes, and the tail call is to the BASE and not to the Troika line. */
void BossDrawDebugStatOverlays();

// --- The remaining bodies -------------------------------------------------------------------------

/** `CAI_BaseNPC::GetSchedulingErrorName` (`0x101a6660`) — slot 451's BASE body,
 *  `return s_CAI_BaseNPC_10594820;`. The Troika line's own (`0x101aa7b0`) fills the slot. */
static const TCHAR* BaseSchedulingErrorName();

/** `CNPC_VTzimisce::GetEventName` (`0x103bdd10`) — slot 241's only species override. Answers the
 *  fixed name for anim-event ids 2..8 and null for everything else, which is the caller's signal to
 *  fall through to `CBaseAnimating::GetEventName`. */
static const TCHAR* TzimisceEventName(int32 EventId);

/** `CAI_BaseNPC::DrawDebugGeometryOverlays` (`0x10275760`) — slot 123's sdk-tier base body: eight
 *  `m_debugOverlays` arms in retail's order, then the pathfinder's overlays and the entity's. */
void BaseDrawDebugGeometryOverlays();

/** `0x10275760`'s `0x20000` arm, the enemy-memory label walk, split out because it is a third of
 *  the body. Three exclusive label tests, two independent suffixes, a SECOND colour ladder that is
 *  not the label's, and one of two shapes per record. */
void DrawEnemyMemoryOverlays();

/** `CBaseEntity+0x0098 m_pBaseNPCTroika`, the cached downcast the view-cone arm requires to be NULL.
 *  This runtime stands ONE leaf and it is the Troika line, so the answer is always true and the arm
 *  is dead — which is the recovered answer for every NPC on that chain in retail too. */
bool IsBaseNpcTroika() const;

/** `CScriptedTarget::DrawDebugGeometryOverlays` (`0x1034e070`) — slot 123 on a class no map stands
 *  here (`classes.md` gives `CScriptedTarget` no entity classname); ported against the two words
 *  family Species already declared for it. */
void ScriptedTargetDrawDebugGeometryOverlays();

/** `CScriptedTarget::DrawDebugTextOverlays` (`0x1034ddf0`) — slot 124 on the same class. Returns the
 *  next free text-overlay line, which is the base's answer plus three when the `0x1` bit is set. */
int32 ScriptedTargetDrawDebugTextOverlays();

/** `CAI_Hint::DrawDebugTextOverlays` (`0x102d1600`) — slot 124 on `CAI_Hint`, which is not this leaf
 *  either: the hint's own two words are passed in. Returns the next free line. */
static int32 HintDrawDebugTextOverlays(int32 EntityTextLine, int32 DebugOverlayBits, int32 HintType,
	double NextUseTime, double Now);

/** `CNPC_VWerewolf::DrawBBoxOverlay` (`0x103d5050`) — slot 620, filled by `CNPC_VWerewolf` alone.
 *  Not a declared virtual (no Troika-line body holds slot 620), so it is declared here. */
void DrawBBoxOverlay();

/** `CNPC_VWerewolf::DrawDebugHullAtPoint` (`0x103d4820`) — the hull box plus one line, at a point.
 *  `RET 0x10`: a `Vector` by value and one more dword, the overlay duration. */
void DrawDebugHullAtPoint(const FVector& PointUnits, float Duration) const;

// --- The seams these bodies read through ----------------------------------------------------------

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

/** `0x100eccf0(&DAT_10924980, m_nCurrDisposition)` — the disposition table's DEFAULT eye target for
 *  a disposition, the middle `%d` of the Troika stat overlay's eye-target line. **SEAM**: the port's
 *  `FElysiumDisposition` row carries no such index; answers -1. */
int32 DispositionDefaultEyeTarget() const;

/** `CAI_Motor::m_YawSpeed` (`m_pMotor` `+0x38`), what `ReportAIState` prints as `Yaw speed`.
 *  **SEAM**: `m_pMotor` is an `ELYSIUM_NPC_WORD_CHAIN` row and no port member carries the motor's
 *  stored yaw speed — slot 516 `MaxYawSpeed` COMPUTES one, which is a different word. Answers 0. */
float MotorYawSpeed() const;

/** `CBaseCombatCharacter::GetActiveWeapon()` (`0x10007e19` → the inventory's active slot), which
 *  `DrawDebugGeometryOverlays`'s `0x10000` arm hands to `Weapon_Drop`. **SEAM**: no kernel accessor
 *  stands the active weapon entity yet (family BaseHelpers seams its range the same way). Answers
 *  null, and the drop still happens with a null weapon exactly as retail's would. */
FElysiumEntity* ActiveWeaponEntity() const;

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
void EntityDrawBBoxOverlay() const;

/** `m_Collision`'s vtable `+0x4` / `+0x8` — `OBBMins()` / `OBBMaxs()`, which slot 123's `0x1000` arm
 *  compares component-wise to decide between the real box and a default ±5 one. **SEAM**: the port
 *  has no collision extents on the kernel surface (family Motor's `RetailCollisionExtents` says the
 *  same); answers false, which takes the DEGENERATE arm — the one retail takes for an entity whose
 *  OBB is a point. */
bool CollisionObbExtentsUnits(FVector& OutMinsUnits, FVector& OutMaxsUnits) const;

/** `CAI_Enemies`'s record list (slot 541 `GetEnemies()`, then `+0xc` head and `+0x38` next), which
 *  slot 123's `0x20000` arm walks. This runtime's `FElysiumNpcEnemyMemory` IS that store, so the
 *  walk is real; what is a seam is `CBaseEntity+0x9c m_pCombatCharacter` (the arm skips a record
 *  whose entity is not a combat character) and `+0xa8 m_pPlayer`. Both are answered from the port's
 *  own `AsCombatCharacter()` and a compare against `FElysiumEntityWorld::FindPlayer()`, which are
 *  the same two questions. */
