// Story 29c-1, family **Schedule** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcSchedule.cpp` and the tests in
// `Tests/ElysiumNpcKernelScheduleTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// THE STANDING FACT OF THIS FAMILY: **this runtime carries no per-class schedule id space.** Retail
// gives every NPC class a `CAI_ClassScheduleIdSpace` (slot 580) that its own `InitCustomSchedules`
// fills by parsing that class's schedule text — `CNPC_VBrujah`'s is `0x10367a40`, which registers
// `SCHED_VBRUJAH_WALK` 0x158 and `SCHED_VBRUJAH_WATCH` 0x159 into `DAT_1093a740` and records the
// parse result in `DAT_1062f278`, the flag slot 452 answers. The port registers its programs by
// identity (`int32`) and parses no schedule text, so the id-space ROWS below carry the
// retail globals and the translation answers -1 through the seam named at
// `ScheduleLocalToGlobal`. Nothing here invents a range.

// --- Slot 580 `GetClassScheduleIdSpace`: one method and a species table ---------------------------
//
// **NAMED `ClassScheduleIdSpace`, not `GetClassScheduleIdSpace`.** Slot 580's Troika-line body
// (`0x101aa790`, which answers `&DAT_10924248`) is defined by family EntityChain in
// `ElysiumNpcEntityChain.cpp` as `FElysiumNpc::GetClassScheduleIdSpace()`, and it routes into
// the `ClassScheduleIdSpace()` chain walk below. The SPECIES overrides of that slot are documented
// here as a table (the live space is the corpus's, keyed on the C++ class).

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
 *  Troika line itself (`0x101aa790` → `DAT_10924248`). A record of the recovered bodies and globals,
 *  read only by tests: the live space is the schedule corpus's, per class (story 5 commit B deleted
 *  the name-keyed lookup). */
static const FScheduleIdSpace* ScheduleIdSpaceRows(int32& OutCount);

/** Slot 580 for THIS NPC: the nearest species override of slot 580 walking its class chain, else
 *  the Troika line's own row. Never null — every chain ends at `CAI_BaseNPCTroika`. */
const FElysiumLocalIdSpace* ClassScheduleIdSpace() const override;

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
 *  line's own `0x102b97f0`. A record read only by tests: the live flag is the schedule corpus's
 *  parse result, per class. */
static const FScheduleLoadFlag* LoadedSchedulesRows(int32& OutCount);

// --- The schedule-change door ---------------------------------------------------------------------

// --- Species words this family's bodies read ------------------------------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout; the SPECIES words above
// `+0x665c` have no port member because one leaf carries every classname and the same offset means
// different things per species. These three are the ones this family's bodies write, declared by
// retail name with the species class that owns the offset, exactly as family Squad declares its
// four.

/** `0x102ae840` — the scripted-schedule order the director pushes.
 *
 *  NAMED `AcceptScriptedScheduleOrder`, not 29c's `ScriptedScheduleOrder`: that name is already the
 *  `+0x65cc` data member 29b declared. Retail's own name is unrecovered. */
void AcceptScriptedScheduleOrder(int32 OrderId, bool bForce);

// --- The task surface -----------------------------------------------------------------------------

/** `CAI_Motor` slot 1 (`0x102623a0`): `JMP [[m_pOuter] + 0x700]` — the owner's slot 448
 *  `TaskFail`, unchanged. `FElysiumNpc::TaskFail` is that body; this is the motor's door into it. */
void MotorTaskFail(int32 Reason);

// --- The selectors --------------------------------------------------------------------------------

/** Slot 438's species hook: a species class that REPLACES the whole selector overrides it (story 5
 *  step 3) — `CNPC_VAndreiBlood` (`0x1035d010`), `CNPC_VCamera`/`CNPC_VCameraSecurity`
 *  (`0x10368f40`), `CNPC_VManBat` (`0x1038e340`), `CNPC_VMingXiaoTentacle` (`0x1039de20`) and
 *  `CNPC_VPlaceholder` (`0x103a4410`).
 *
 *  Answers a RAW retail schedule number, or 0 on the Troika line. `FElysiumNpc::SelectSchedule`
 *  consults it first and falls through when the number names no registered program, which is how
 *  `TranslateSchedule` already handles an unported id. */
virtual int32 SpeciesSelectSchedule();

/** `0x102b6fe0` — the melee selector's failure gate, called before every other arm of
 *  `SelectScheduleMeleeCombat` and again after the in-melee branch.
 *
 *  `COND_ENEMY_OCCLUDED` (0x48) first, then `COND_ENEMY_BLOCKED` (0x3a); each splits on
 *  `HasUsableRangedWeapon()` (slot 308) into 0xe9 and 0xcd / 0xce. 0 is "no opinion". Retail's own
 *  name is unrecovered; 29c named the port method. */
int32 MeleeScheduleFailureGate(FElysiumEntity* Enemy);

int32 SelectScheduleMeleeCombatChangLine(bool bChang);

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

// --- The seams this family's bodies ask -----------------------------------------------------------

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

int32 NavigatorGoalType() const;
