// Story 29c-1, family **Schedule** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelSchedule.cpp` and the tests in
// `Tests/ElysiumNpcKernelScheduleTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// THE STANDING FACT OF THIS FAMILY: **this runtime carries no per-class schedule id space.** Retail
// gives every NPC class a `CAI_ClassScheduleIdSpace` (slot 580) that its own `InitCustomSchedules`
// fills by parsing that class's schedule text — `CNPC_VBrujah`'s is `0x10367a40`, which registers
// `SCHED_VBRUJAH_WALK` 0x158 and `SCHED_VBRUJAH_WATCH` 0x159 into `DAT_1093a740` and records the
// parse result in `DAT_1062f278`, the flag slot 452 answers. The port registers its programs by
// identity (`EElysiumScheduleId`) and parses no schedule text, so the id-space ROWS below carry the
// retail globals and the translation answers -1 through the seam named at
// `ScheduleLocalToGlobal`. Nothing here invents a range.

// --- Slot 580 `GetClassScheduleIdSpace`: one method and a species table ---------------------------
//
// **NAMED `ClassScheduleIdSpace`, not `GetClassScheduleIdSpace`.** Slot 580's Troika-line body
// (`0x101aa790`, which answers `&DAT_10924248`) is defined by family EntityChain in
// `ElysiumNpcKernelEntityChain.cpp` as `FElysiumNpc::GetClassScheduleIdSpace()`, and it routes into
// the `ClassScheduleIdSpace()` chain walk below. The twelve SPECIES overrides of that slot land
// here under their own name, exactly as family Squad's `SpeciesIRelationType` does for slot 404.

/** One row of retail's slot-580 species table: the class, the body that fills the slot for it, and
 *  the `CAI_ClassScheduleIdSpace` that body returns. The three range fields are `CAI_LocalIdSpace`
 *  `+0x00 m_globalBase`, `+0x04 m_localBase`, `+0x08 m_localTop`; 9999 in `LocalBase` is retail's
 *  "this space holds no ids" sentinel, which `0x102ea2d0` tests by name. They keep the state
 *  `0x102ea090(isRoot = false)` left at static init, because the only thing that would widen them
 *  is the class's own schedule-text parse and this runtime does not run one. */
struct FScheduleIdSpace
{
	// The census class this row came from (`docs/vtmb/npc-kernel/slots.md`).
	const TCHAR* RetailClass = nullptr;
	// The retail body that fills slot 580 for it, `0x10……`; checkable against `slots.md`.
	const TCHAR* Body = nullptr;
	// The `CAI_ClassScheduleIdSpace` global that body returns, `0x10……`. The class's SCHEDULE
	// sub-space: its task, condition and squadslot spaces follow at `+0x18`, `+0x30` and `+0x48`,
	// which is how family Squad's table reaches the same class's squadslot space.
	const TCHAR* IdSpace = nullptr;
	int32 GlobalBase = INDEX_NONE;
	int32 LocalBase = 9999;
	int32 LocalTop = INDEX_NONE;
};

/** The table: the twelve species bodies of slot 580 across fourteen census classes, plus the
 *  Troika line itself (`0x101aa790` → `DAT_10924248`). */
static const FScheduleIdSpace* ScheduleIdSpaceRows(int32& OutCount);

/** The row for a retail class name, or null when no row carries it. */
static const FScheduleIdSpace* ScheduleIdSpaceOf(const TCHAR* InRetailClass);

/** Slot 580 for THIS NPC: the nearest species override of slot 580 walking its class chain, else
 *  the Troika line's own row. Never null — every chain ends at `CAI_BaseNPCTroika`. */
const FScheduleIdSpace* ClassScheduleIdSpace() const;

/** `CAI_ClassScheduleIdSpace::ScheduleLocalToGlobal` (`0x102ea2d0`, the same walk family Squad
 *  ports for squad slots over the space at `+0x48`): -1 stays -1, otherwise walk the chain and
 *  answer `(globalBase - localBase) + id` for the first space whose range holds it, else -1.
 *
 *  SEAM: every row's range is the empty one the static constructor left, because the port parses no
 *  schedule text, so this answers -1 for every id. Retail's ranges are filled by the class's own
 *  `InitCustomSchedules`. */
static int32 ScheduleLocalToGlobal(const FScheduleIdSpace* Space, int32 LocalId);

// --- Slot 452 `LoadedSchedules`: one method and a species table -----------------------------------
//
// The slot itself IS this story's (`0x102b97f0`, the `CAI_BaseNPCTroika` override), so
// `FElysiumNpc::LoadedSchedules()` is defined in the family `.cpp` and reads the table below.

/** One row of retail's slot-452 species table: the class, the body that fills the slot for it, and
 *  the global `bool` that body returns. Every one of those globals is written in exactly one place
 *  — the class's own `InitCustomSchedules` loop (`CNPC_VBrujah`'s is `0x10367a40`), which seeds the
 *  loop FROM the flag, breaks on the first parse failure and stores the parser's answer back. The
 *  flag therefore ships `true` and only a failed schedule-text parse clears it. */
struct FScheduleLoadFlag
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
	// The `bool` global, `0x10……`. `DAT_105d1058` for the Troika line.
	const TCHAR* Flag = nullptr;
};

/** The table: the twelve species bodies of slot 452 across their census classes, plus the Troika
 *  line's own `0x102b97f0`. */
static const FScheduleLoadFlag* LoadedSchedulesRows(int32& OutCount);

/** The row for a retail class name, or null when no row carries it. */
static const FScheduleLoadFlag* LoadedSchedulesRowOf(const TCHAR* InRetailClass);

// --- The schedule-change door ---------------------------------------------------------------------

/** `CAI_BaseNPC::SetSchedule(int)` (`0x10280de0`), first half: the value `m_IdealSchedule`
 *  (`+0x5c3c`) is stamped with.
 *
 *  Retail: an id at or above 1,000,000,000 is already global and is stamped unchanged; anything
 *  below it — and the -1 sentinel — goes through this class's schedule id space. Split out from
 *  `ChangeSchedule` because it is the whole of what that body adds over the port's existing
 *  install chain, and a test has to be able to state it without installing a program. */
int32 ResolveIdealScheduleStamp(int32 RawRetailId) const;

/** `CAI_BaseNPC::SetSchedule(int)` (`0x10280de0`) whole, and the body all five slot-619 species
 *  overrides forward to (`0x1035dba0`, `0x10361530`, `0x1036c760`, `0x103a9fd0`, `0x103af8d0` —
 *  each a scope-trace wrapper with no logic of its own; their trace names are
 *  `FElysiumNpcScheduleHost::SetScheduleTraceName`).
 *
 *  Stamp `ScheduleHost.IdealSchedule`, then run `SetSchedule(int)` (`0x102cc1f0`) →
 *  `CAI_BaseNPC::SetSchedule(CAI_Schedule*)` (`0x10280e50`), which is `ElysiumSchedule::Start`. */
void ChangeSchedule(EElysiumScheduleId Id);

/** `CCineAI::FixScriptNPCSchedule`, slot 586 (`0x101a95d0`), applied to THIS NPC.
 *
 *  `FinishSchedule` is the director's own `m_iFinishSchedule` (`CCineAI +0x5f64`), not a word of
 *  this NPC: 0 clears the schedule, 1 installs `0x2a` (`SCHED_AISCRIPT`) with no clear, anything
 *  else is a `DevMsg("FixScriptNPCSchedule - no case!")` and then the clear. */
void FixScriptNPCSchedule(int32 FinishSchedule);

/** `CNPC_VSabbatLeader::FlipFailureType` (`0x103a9d00`): `m_FailureType = 1 - m_FailureType`. */
void FlipFailureType();

// --- Species words this family's bodies read ------------------------------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout; the SPECIES words above
// `+0x665c` have no port member because one leaf carries every classname and the same offset means
// different things per species. These three are the ones this family's bodies write, declared by
// retail name with the species class that owns the offset, exactly as family Squad declares its
// four.

/** `CNPC_VSabbatLeader::m_FailureType`, `+0x66c0`. */
int32 FailureType = 0;

/** `CNPC_VManBat::m_iMoveGoalNodeID`, `+0x6674` — written 1 by both early-out arms of that class's
 *  `SelectSchedule` (`0x1038e340`). Read by nothing in this runtime yet. */
int32 MoveGoalNodeId = 0;

/** `CNPC_VAsianVampire::m_bSuppressRanged`, `+0x66e8` — the two extra terms that class's
 *  `SelectScheduleMeleeCombat` (`0x10361be0`) puts on its ranged arms. No producer here. */
bool bSuppressRanged = false;

/** `0x102ae840` — the scripted-schedule order the director pushes.
 *
 *  NAMED `AcceptScriptedScheduleOrder`, not 29c's `ScriptedScheduleOrder`: that name is already the
 *  `+0x65cc` data member 29b declared. Retail's own name is unrecovered. */
void AcceptScriptedScheduleOrder(int32 OrderId, bool bForce);

// --- The task surface -----------------------------------------------------------------------------

/** `NextScheduledTask` (`0x10280f40`): clear the task status, advance the task index, and when the
 *  program is exhausted zero `m_failedSchedule`/`m_interuptSchedule` and raise
 *  `COND_SCHEDULE_DONE` (0x5d). */
void NextScheduledTask();

/** `CAI_BaseNPC::TaskComplete(bool)` (`0x10273e80`) — the body `CAI_Motor` slot 2 (`0x102623c0`)
 *  forwards to through its owner back-pointer. `bIgnoreTaskFailed` false refuses to overwrite an
 *  already raised `COND_TASK_FAILED`; true writes the status regardless. */
void TaskComplete(bool bIgnoreTaskFailed);

/** `CAI_Motor` slot 2 (`0x102623c0`): `m_pOuter->TaskComplete(b)`. The motor telling its owner a
 *  motor-driven task finished; no logic of its own. */
void MotorTaskComplete(bool bIgnoreTaskFailed);

/** `CAI_Motor` slot 1 (`0x102623a0`): `JMP [[m_pOuter] + 0x700]` — the owner's slot 448
 *  `TaskFail`, unchanged. `FElysiumNpc::TaskFail` is that body; this is the motor's door into it. */
void MotorTaskFail(int32 Reason);

/** `0x1027db30` — **NOT `StartTaskByIndex`.** 29c's target name was a guess over a `‼` row with no
 *  recovered callers; the disassembly reads the NAVIGATOR at `+0x5d34`, indexes its node list
 *  (`+0x2c`, count at `+0x00`, array at `+0x04`) and tail-jumps to slot 527 `IsUnusableNode`, which
 *  the ledger's signature table names. An index below zero or past the end bumps the global error
 *  counter `0x106c994c` and answers false, as does a null node.
 *
 *  SEAM: there is no node list on the port's motor, so the bounds test always fails and the counter
 *  is tallied instead of incremented. */
bool IsUnusableNodeIndex(int32 NodeIndex) const;

// --- The selectors --------------------------------------------------------------------------------

/** `CAI_BaseNPC::PreSelectSchedule` (`0x1028a2a0`).
 *
 *  **NAMED `BasePreSelectSchedule`, not `PreSelectSchedule`.** Slot 437 is filled by a DIFFERENT
 *  retail body on the Troika line (`CAI_BaseNPCTroika` `0x102ae920`, layer 25), which is story
 *  29e's and whose generated stub is still in `ElysiumNpcKernelSlots.cpp`. This is the base class's
 *  body, which fills the same slot for `CAI_BaseNPC`, `CAI_BaseHumanoid`, `CAI_ExpressiveNPC` and
 *  ten more.
 *
 *  Answers a RAW retail schedule number (0x3a `NPC_FREEZE`, 0x151, 0x3e `FALL_TO_GROUND`) or 0 for
 *  "no opinion" — none of the three is a registered program here. */
int32 BasePreSelectSchedule();

/** The three SPECIES overrides of slot 437 this story carries: `CNPC_VCamera` /
 *  `CNPC_VCameraSecurity` (`0x10368f20`), `CNPC_VMingXiaoTentacle` (`0x1039de00`) and
 *  `CNPC_VPlaceholder` (`0x103a43f0`). Each writes retail's selector-trace tag and answers a fixed
 *  raw schedule number; 0 means this NPC's class has no slot-437 species body. */
int32 SpeciesPreSelectSchedule();

/** The five SPECIES overrides of slot 438 `SelectSchedule` this story carries:
 *  `CNPC_VAndreiBlood` (`0x1035d010`), `CNPC_VCamera`/`CNPC_VCameraSecurity` (`0x10368f40`),
 *  `CNPC_VManBat` (`0x1038e340`), `CNPC_VMingXiaoTentacle` (`0x1039de20`) and `CNPC_VPlaceholder`
 *  (`0x103a4410`).
 *
 *  Answers a RAW retail schedule number, or 0 when this NPC's class has no slot-438 species body.
 *  `FElysiumNpc::SelectSchedule` consults it first and falls through when the number names no
 *  registered program, which is how `TranslateSchedule` already handles an unported id. */
int32 SpeciesSelectSchedule();

/** A raw retail schedule number back to the identity this runtime registers, or `None` when no
 *  registered program carries it. The direction `ElysiumScheduleNumber` does not have, needed
 *  wherever a recovered body answers retail's number and a caller has to install something. */
static EElysiumScheduleId ScheduleFromRetailNumber(int32 RetailNumber);

/** `0x102b6fe0` — the melee selector's failure gate, called before every other arm of
 *  `SelectScheduleMeleeCombat` and again after the in-melee branch.
 *
 *  `COND_ENEMY_OCCLUDED` (0x48) first, then `COND_ENEMY_BLOCKED` (0x3a); each splits on
 *  `HasUsableRangedWeapon()` (slot 308) into 0xe9 and 0xcd / 0xce. 0 is "no opinion". Retail's own
 *  name is unrecovered; 29c named the port method. */
int32 MeleeScheduleFailureGate(FElysiumEntity* Enemy);

/** The hint-search request `SelectCoverOrKickSchedule` builds its mask from.
 *
 *  **The four parameter names are UNRECOVERED.** Only their mask bits are: `bRequest1` → 1,
 *  `bRequest2` → 2, `bRequest3` → 4 and `bRequest4` → 8, with bits 4 and 8 additionally gated on
 *  `m_bAllowKickHintUse` (`+0x6436`); `bRequest4` also arms the kick-physics-prop refresh at the
 *  top of the body. Named by their bit rather than by a guess at their intent. */
struct FScheduleHintSearchRequest
{
	bool bRequest1 = false;
	bool bRequest2 = false;
	bool bRequest3 = false;
	bool bRequest4 = false;
};

/** `0x102b7690` — the entrenched cover / kick-prop schedule selector.
 *
 *  Answers a RAW retail schedule number (0x9b, 0x9d, 0x9e, 0xa0–0xa5, 0xa7–0xa9) or 0. */
int32 SelectCoverOrKickSchedule(const FScheduleHintSearchRequest& Request);

/** The species half of slot 453 `BuildScheduleTestBits`: `CNPC_VGuard1` (`0x1037cdf0`),
 *  `CNPC_VHumanCombatant` (`0x10387520`, nine classes), `CNPC_VPedestrian` (`0x103a2980`) and
 *  `CNPC_VTzimisceHeadClaw` (`0x103c16f0`). `FElysiumNpc::BuildScheduleTestBits` — the Troika-line
 *  body, ported in story 25 — calls this at its tail; every one of the four overlays runs AFTER the
 *  base body the retail override opens with. */
void SpeciesBuildScheduleTestBits(FElysiumNpcConditions& InOutMask);

// --- The seams this family's bodies ask -----------------------------------------------------------

/** SEAM for `GetJumpSchedule` (`0x10361a80`, reached from `CNPC_VAsianVampire::
 *  SelectScheduleMeleeCombat`'s `COND_ENEMY_UNREACHABLE` arm): the schedule that jumps to an
 *  unreachable enemy. Answers 0 — no jump vocabulary exists here. */
int32 GetJumpSchedule(FElysiumEntity* Enemy) const;

/** SEAM for `0x102b6650`, the kick-physics-prop search `SelectCoverOrKickSchedule` refreshes on its
 *  two-second timer. Retail answers a `CBaseEntity*`; this has no prop sweep and answers null. */
FElysiumEntity* FindKickPhysicsProp() const;

/** SEAM for `0x102b7110`, the hint search `SelectCoverOrKickSchedule` runs with the four-bit mask
 *  above. Retail writes `m_pHintNode` (`+0x5ddc`) on a hit; this has no hint store and writes
 *  nothing. */
void SearchForCoverHint(uint32 SearchMask);

/** SEAM for `0x102a11d0`, the gate `CNPC_VChangBros` / `CNPC_VTzimisceRunner` put in front of their
 *  0x15a / 0xe1 arm, after taking the enemy's `WorldSpaceCenter` (slot 192) and discarding it.
 *  Retail name unrecovered. Answers false, which is the arm that falls through. */
bool ScheduleMeleeReachGate() const;

/** SEAM for `thunk_FUN_1035e920` (`CNPC_VAndreiBlood::SelectSchedule`'s 0x15d/0x15c split, which
 *  reads `+0x66b8`) and for `CNPC_VManBat`'s navigator-state probe `0x1027d990`. Both are species
 *  state this substrate does not carry; each answers its refusing value and says so at the call
 *  site in `SpeciesSelectSchedule`. */
bool AndreiBloodSelectGate() const;
int32 NavigatorGoalType() const;
