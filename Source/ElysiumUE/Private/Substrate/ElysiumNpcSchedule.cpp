#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcScheduleShared.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29c-1, family **Schedule** — the schedule host and the task surface of `order.md`
// layers 0–9.
//
// 64 rows: the two species tables (slot 580 `GetClassScheduleIdSpace`, slot 452 `LoadedSchedules`),
// the schedule-change door `0x10280de0`, the task surface (`NextScheduledTask`, `TaskComplete`, the
// two `CAI_Motor` forwards, `IsTaskIndexCurrent`, `WaitFinished`), the two stubbed slots 418
// `ResolveTaskDistance` and 547 `GetSlotSchedule`, and the selectors — slot 604
// `SelectScheduleMeleeCombat` with its five species overrides, the slot-437/438 species overrides,
// the melee failure gate `0x102b6fe0` and the entrenched cover/kick selector `0x102b7690`. The
// walked prose is `docs/vtmb/npc-ai/schedule-kernel.md`.
//
// TWO STANDING FACTS OF THIS FAMILY.
//
//   1. **There is no per-class schedule id space.** Retail's `InitCustomSchedules` parses each
//      class's schedule text into its `CAI_ClassScheduleIdSpace` and records the parse in the flag
//      slot 452 answers; this runtime registers programs by identity and parses no text. Every
//      species row below carries the retail globals so the tables are checkable, and the
//      translation says what it cannot do rather than inventing a range.
//
//   2. **Most of what these selectors answer is not a registered program.** The species selectors
//      answer raw retail schedule numbers (0x156, 0x15a–0x165, 0x9b–0xa9, 0xe4, 0xe7, 0xe9 …), and
//      this runtime registers about thirty programs. Every body here therefore answers the RETAIL
//      NUMBER — the recovered deliverable — and the one caller that installs anything
//      (`FElysiumNpc::SelectSchedule`) maps it through the registry and tallies the miss instead of
//      installing some other program under a recovered number.

namespace
{
	// ---------------------------------------------------------------------------------------------
	// Retail data constants. Everything whose value IS in the corpus is named; everything whose
	// value is a bare `.rdata` float the corpus does not carry says UNRECOVERED and is named so one
	// edit closes it.
	// ---------------------------------------------------------------------------------------------

	// `0x3b9aca00` — the "already a global id" boundary `0x10280de0` compares against. It is the same
	// 1,000,000,000 family Squad found the two squad-slot symbols registered under.
	constexpr int32 GScheduleGlobalIdBase = 1000000000;

	// `CAI_LocalIdSpace`'s "this space holds no ids" sentinel, tested by name in `0x102ea2d0`.
	constexpr int32 GScheduleEmptyIdSpace = 9999;

	// `_DAT_104454c4` — the shared float zero, 1,328 readers.
	constexpr float GScheduleZero = 0.0f;

	// `_DAT_1044e664` — 10.0f, recovered by story 16a as the follower-distance overlap
	// (`Substrate/ElysiumNpcSquad.cpp`). `ResolveTaskDistance`'s fourth table entry and the
	// tentacle's phase-2 duration both read it.
	constexpr float GScheduleFollowerOverlap = 10.0f;

	// `_DAT_104492b8` — 200.0f, the same constant `ElysiumFootsteps.h` names as the fall-punch
	// threshold. `CNPC_VChangBros` / `CNPC_VTzimisceRunner` add it to the melee-range convar.
	constexpr float GScheduleChangMeleeMargin = 200.0f;

	// `_DAT_1044ddb0` — `CNPC_VMingXiaoTentacle`'s enemy-distance split. UNRECOVERED.
	constexpr float GScheduleTentacleEnemyDistUnits = 0.0f;

	// `_DAT_10450aa0`, `_DAT_10449270`, `_DAT_104bea38` — the tentacle's phase-0/3, phase-1 and
	// phase-3 expire durations. Phase 2's is `_DAT_1044e664` (10.0) and phase 1's the pooled double
	// 0.5; the other two are UNRECOVERED.
	constexpr float GScheduleTentaclePhase0Seconds = 0.0f;
	constexpr float GScheduleTentaclePhase1Seconds = static_cast<float>(ElysiumNpcTunables::HalfDouble);
	constexpr float GScheduleTentaclePhase3Seconds = 0.0f;

	// The three hint types the cover selector's table splits on, the same three family Hints found
	// on the five activity lookups.
	constexpr int32 GScheduleHintTypeCoverLow = 100;
	constexpr int32 GScheduleHintTypeCoverMed = 0x65;
	constexpr int32 GScheduleHintTypeLean = 0x27d8;
	constexpr int32 GScheduleHintTypeShootAtA = 0x283c;
	constexpr int32 GScheduleHintTypeShootAtB = 0x283d;

	// `flags2 &= 0x7ffffeff` — the mask `0x102b7690` clears on four of its arms. Its complement is
	// `0x80000100`: the unnamed bit 31 and `COVER_VS_MELEE_MODE`.
	constexpr uint32 GScheduleCoverVsMeleeClearMask =
		0x80000000u | static_cast<uint32>(EElysiumNpcFlag2::COVER_VS_MELEE_MODE);

	// `(wpn->slot360() & 0x6000)` — the weapon-capability bits the cover selector reads off the
	// enemy's active weapon to decide whether it faces a ranged threat.
	constexpr uint32 GScheduleRangedThreatWeaponBits = 0x6000;

}

// -------------------------------------------------------------------------------------------------
// Slot 580 `GetClassScheduleIdSpace` — one method and a species table.
// -------------------------------------------------------------------------------------------------
//
// All fourteen bodies are one line: `return &DAT_<class schedule id space>;`. The table below is the
// ten distinct ones on classes with an instance (`CNPC_VCamera` is shared with
// `CNPC_VCameraSecurity` and `CNPC_VVampire` with `CNPC_VPlayerController`) plus the Troika line's
// own `0x101aa790` → `DAT_10924248`. `CNPC_VCombatman` (`0x1036fb10`) and `CNPC_VGangrel`
// (`0x10377070`) have no instance (`population.md` § "NPC classes with no instance") and no row.
//
// The ranges are `0x102ea090(isRoot = false)`'s leftovers, exactly as family Squad found for the
// squadslot spaces — but for a DIFFERENT reason, and the difference is the recovery. Retail's
// species schedule spaces really are filled: `CNPC_VBrujah::InitCustomSchedules` (`0x10367a40`)
// calls `CAI_LocalIdSpace::Init` (`0x102ea0e0`) on `DAT_1093a740` with the schedule namespace
// `0x109203cc` and the parent `DAT_1093d258` (`CNPC_VVampire`'s), then registers
// `SCHED_VBRUJAH_WALK` 0x158 and `SCHED_VBRUJAH_WATCH` 0x159 through `0x102ea130`. This runtime
// runs no such registration, so the rows keep the empty range and `ScheduleLocalToGlobal` says so.

namespace
{
	constexpr FElysiumNpc::FScheduleIdSpace GNpcKernelScheduleIdSpaces[] = {
		// The Troika line itself.
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x101aa790"), TEXT("0x10924248") },
		{ TEXT("CNPC_VBrujah"), TEXT("0x10367870"), TEXT("0x1093a740") },
		{ TEXT("CNPC_VCamera"), TEXT("0x103683b0"), TEXT("0x1093a7d8") },
		{ TEXT("CNPC_VCameraSecurity"), TEXT("0x103683b0"), TEXT("0x1093a7d8") },
		{ TEXT("CNPC_VChangBros"), TEXT("0x1036a1b0"), TEXT("0x1093a888") },
		{ TEXT("CNPC_VChangBrosBlade"), TEXT("0x1036eab0"), TEXT("0x1093a8f0") },
		{ TEXT("CNPC_VChangBrosClaw"), TEXT("0x1036f2b0"), TEXT("0x1093a938") },
		{ TEXT("CNPC_VCop"), TEXT("0x10370930"), TEXT("0x1093ac60") },
		{ TEXT("CNPC_VDog"), TEXT("0x10373530"), TEXT("0x1093acd8") },
		{ TEXT("CNPC_VFrenzyShadow"), TEXT("0x10375240"), TEXT("0x1093ae48") },
		{ TEXT("CNPC_VGargoyle"), TEXT("0x10377b20"), TEXT("0x1093aff0") },
		{ TEXT("CNPC_VPlayerController"), TEXT("0x103750e0"), TEXT("0x1093d258") },
		{ TEXT("CNPC_VVampire"), TEXT("0x103750e0"), TEXT("0x1093d258") },
	};
}

const FElysiumNpc::FScheduleIdSpace* FElysiumNpc::ScheduleIdSpaceRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GNpcKernelScheduleIdSpaces);
	return GNpcKernelScheduleIdSpaces;
}

const FElysiumNpc::FScheduleIdSpace* FElysiumNpc::ScheduleIdSpaceOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	for (const FScheduleIdSpace& Row : GNpcKernelScheduleIdSpaces)
	{
		if (FCString::Strcmp(Row.RetailClass, InRetailClass) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

// slot 580, the species half of 0x101aa790
const FElysiumLocalIdSpace* FElysiumNpc::ClassScheduleIdSpace() const
{
	// The table above is documentation now -- the retail class, the slot-580 body and the id space
	// that body returns, all checkable against `docs/vtmb/npc-kernel/slots.md`. The LIVE space is
	// the corpus's, keyed on this NPC's own retail class rather than on the slot-580 override row,
	// because the sidecar already folds the classes that share one space.
	return IdSpace(EElysiumIdCategory::Schedule);
}

int32 FElysiumNpc::ScheduleLocalToGlobal(const FElysiumLocalIdSpace* Space, int32 LocalId)
{
	// 0x102ea2d0, arm for arm:
	//
	//   if (id == -1) return -1;
	//   do {
	//     if (localBase != 9999 && localBase <= id && id <= localTop)
	//       return (globalBase - localBase) + id;
	//     space = space->parent;   // +0x10
	//   } while (space);
	//   return -1;
	//
	// The seam is CLOSED: the parent chain and every range are the corpus's, filled by the
	// registration pass that runs each class's `InitCustomSchedules` recipe. The arms themselves
	// live on `FElysiumLocalIdSpace`, so this body and the global->local direction cannot drift.
	return Space != nullptr ? Space->LocalToGlobal(LocalId) : INDEX_NONE;
}

// -------------------------------------------------------------------------------------------------
// Slot 452 `LoadedSchedules` — the stubbed slot, plus its species table.
// -------------------------------------------------------------------------------------------------

namespace
{
	constexpr FElysiumNpc::FScheduleLoadFlag GNpcKernelScheduleLoadFlags[] = {
		// The Troika line's own override. `CAI_BaseNPC::LoadedSchedules` (`0x1027c2e0`) is the
		// literal `true` this one replaces with a global.
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x102b97f0"), TEXT("0x105d1058") },
		{ TEXT("CNPC_VBrujah"), TEXT("0x103679b0"), TEXT("0x1062f278") },
		{ TEXT("CNPC_VCamera"), TEXT("0x103684f0"), TEXT("0x1062f668") },
		{ TEXT("CNPC_VCameraSecurity"), TEXT("0x103684f0"), TEXT("0x1062f668") },
		{ TEXT("CNPC_VChangBros"), TEXT("0x1036a390"), TEXT("0x1062fe14") },
		{ TEXT("CNPC_VChangBrosBlade"), TEXT("0x1036ec90"), TEXT("0x1062fe38") },
		{ TEXT("CNPC_VChangBrosClaw"), TEXT("0x1036f490"), TEXT("0x1062fe5c") },
		{ TEXT("CNPC_VCop"), TEXT("0x10370a70"), TEXT("0x10631ba8") },
		{ TEXT("CNPC_VDog"), TEXT("0x10373670"), TEXT("0x106368a8") },
		{ TEXT("CNPC_VFrenzyShadow"), TEXT("0x103753e0"), TEXT("0x10637b44") },
		{ TEXT("CNPC_VGargoyle"), TEXT("0x10377c70"), TEXT("0x106395c0") },
	};
}

const FElysiumNpc::FScheduleLoadFlag* FElysiumNpc::LoadedSchedulesRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GNpcKernelScheduleLoadFlags);
	return GNpcKernelScheduleLoadFlags;
}

const FElysiumNpc::FScheduleLoadFlag* FElysiumNpc::LoadedSchedulesRowOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	for (const FScheduleLoadFlag& Row : GNpcKernelScheduleLoadFlags)
	{
		if (FCString::Strcmp(Row.RetailClass, InRetailClass) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

// slot 452 0x102b97f0 `bool LoadedSchedules()`, with the twelve species bodies
bool FElysiumNpc::LoadedSchedules()
{
	// Every one of the thirteen bodies is `return <global bool>;`. Each of those globals is written
	// in exactly ONE place — the owning class's `InitCustomSchedules` parse loop, which seeds itself
	// from the flag (`uVar2 = DAT_1062f278`), breaks on the first parse failure and stores the
	// parser's answer back — so the shipped value is `true` and only a malformed schedule text
	// clears it. `CAI_BaseNPC::Precache` (`0x1027bb50`) is the reader: a false return is
	// `"ERROR: Rejecting spawn of %s as error in NPC's schedules"`.
	//
	// The seam is CLOSED: this runtime parses the class's texts and records the parse, so the answer
	// is the real one. It is `true` for all 56 loaded spaces today -- every shipped text parses --
	// and it goes false the moment one does not, which is exactly what retail's `Precache` refusal
	// (`"ERROR: Rejecting spawn of %s as error in NPC's schedules"`) reads.
	const FElysiumScheduleSpaceUnit* Unit =
		FElysiumScheduleCorpus::Get().UnitForClass(RetailClass() != nullptr ? FString(RetailClass()->Name) : FString());
	return Unit == nullptr || Unit->bAllTextsLoaded;
}

// -------------------------------------------------------------------------------------------------
// The schedule-change door.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::ResolveIdealScheduleStamp(int32 RawRetailId) const
{
	// 0x10280de0, first half, from the listing:
	//
	//   CMP EDI, 0x3b9aca00 / JL translate / CMP EDI, -1 / JNZ keep
	//
	// so an id at or above 1,000,000,000 that is not -1 is stamped unchanged, and everything else —
	// including -1 — goes through slot 580's id space.
	if (RawRetailId >= GScheduleGlobalIdBase && RawRetailId != INDEX_NONE)
	{
		return RawRetailId;
	}
	return ScheduleLocalToGlobal(ClassScheduleIdSpace(), RawRetailId);
}

// 0x10280de0 `CAI_BaseNPC::SetSchedule(int)`, and the body slot 619's five species overrides forward
// to unchanged
void FElysiumNpc::ChangeSchedule(int32 Id)
{
	// The raw number retail passes. Every program this runtime registers carries retail's own
	// registered number and every one of them is below 1,000,000,000, so the translate arm is the
	// one taken — which is exactly what retail does for an id named in a schedule table.
	const int32 RawRetailId = Id;
	const int32 Stamp = ResolveIdealScheduleStamp(RawRetailId);

	// `m_IdealSchedule = <stamp>` (`+0x5c3c`) keeps the raw int32 exactly, including -1 and the
	// global >=1e9 namespace that the typed installed-program enum cannot spell.
	BaseScheduleHost.IdealScheduleRetail = Stamp;
	if (Stamp == INDEX_NONE)
	{
		// SEAM, stated once per call rather than swallowed: with no class schedule id space the
		// translation answers -1 and the stamp is empty. Retail would have stamped the global id
		// the class registered.
		RecordScheduleEvent(FString::Printf(
			TEXT("ChangeSchedule(%s): m_IdealSchedule left empty — no class schedule id space "
				"(0x10280de0 / 0x102ea2d0)"),
			ElysiumScheduleName(Id)));
	}

	// Then `SetSchedule(int)` (`0x102cc1f0`: slot 440 `TranslateSchedule`, slot 446
	// `GetScheduleOfType`, the `"GetScheduleOfType(): No CASE for %d"` miss arm installing base 1)
	// and `CAI_BaseNPC::SetSchedule(CAI_Schedule*)` (`0x10280e50`). Both halves are
	// `ElysiumSchedule::Start`, which already carries the translate, the miss trace and the
	// condition clear.
	ElysiumSchedule::Start(Schedule, Id, *this);
}

// slot 586 0x101a95d0 `CCineAI::FixScriptNPCSchedule`
void FElysiumNpc::FixScriptNPCSchedule(int32 FinishSchedule)
{
	// `m_iFinishSchedule` is the DIRECTOR's word (`CCineAI +0x5f64`), passed in rather than read off
	// this NPC. Retail's arms, in order:
	if (FinishSchedule != 0)
	{
		if (FinishSchedule == 1)
		{
			// `thunk_FUN_10280de0(npc, 0x2a)` — `SCHED_AISCRIPT`, installed with NO clear.
			//
			// SEAM: 0x2a is not a registered program here. The scripted family this runtime carries
			// is `ScriptedMoveToGoal` / `ScriptedFollowPath`, which the director pushes directly, so
			// naming one of them here would be a different program under a recovered number.
			ElysiumStub::Fired(TEXT("schedule"),
				TEXT("CCineAI::FixScriptNPCSchedule SCHED_AISCRIPT 0x2a"), DebugString(),
				TEXT("m_iFinishSchedule=1"), TEXT("0002/0003: SCHED_AISCRIPT is not registered"));
			return;
		}
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s FixScriptNPCSchedule - no case!"), *DebugString());
	}
	// Both the 0 arm and the no-case arm fall through to the clear.
	ClearSchedule();
}

// 0x102ae840 — the scripted-schedule order push
void FElysiumNpc::AcceptScriptedScheduleOrder(int32 OrderId, bool bForce)
{
	// `if (m_NPCState != 7 && m_IdealNPCState != 7)` — retail state 7 is dead.
	if (Mind.State() == EElysiumNpcState::Dead || Mind.IdealState() == EElysiumNpcState::Dead)
	{
		return;
	}
	// `if (IsAlive() || param_2)` — slot 158. Its generated stub answers false; the port carries the
	// fact on the entity, so `!IsDead()` is the answer and the slot is named beside it.
	if (IsDead() && !bForce)
	{
		return;
	}
	// `+0x65cc = param_1` — the port's `+0x65cc` is the whole pushed order (`the forced state
	// travels with the pushed order`), so the id is recorded on it rather than beside it.
	ScriptedScheduleOrder.RetailOrderId = OrderId;
	// `m_bForceStateChange (+0x1b28) = 1`.
	Mind.ForceStateChange();
	// `m_bfAINPCFlags2 |= 0x82000000` — `CHOOSE_NEW_SCHEDULE` and the unnamed bit 31.
	NpcFlags.Set(EElysiumNpcFlag2::CHOOSE_NEW_SCHEDULE);
	NpcFlags.SetRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
}

// -------------------------------------------------------------------------------------------------
// The task surface.
// -------------------------------------------------------------------------------------------------

// 0x10280f40 `NextScheduledTask`
void FElysiumNpc::NextScheduledTask()
{
	// `m_ScheduleState.fTaskStatus (+0x5c44) = 0` (TASKSTATUS_NEW) and `m_iScheduleIndex (+0x5c40)
	// += 1`, in that order.
	Schedule.TaskStatus = EElysiumTaskStatus::New;
	++Schedule.TaskIndex;
	// `if (IsTaskIndexCurrent())` — `0x10280db0`, which is "the index reached the program's task
	// count", i.e. the program is exhausted.
	if (FElysiumNpcScheduleHost::IsTaskIndexCurrent(Schedule))
	{
		// `m_failedSchedule (+0x5f38) = m_interuptSchedule (+0x5f3c) = 0`. The listing's `EDX` is
		// zeroed at `0x10280f43` and never rewritten, so both stores are literal zero.
		BaseScheduleHost.FailedSchedule = ElysiumScheduleId::None;
		BaseScheduleHost.InterruptSchedule = ElysiumScheduleId::None;
		// `(*DAT_10924a6c + 4)()` — a global object's slot 1, taking no argument and discarding its
		// answer. UNRECOVERED: the object is not identified and the call has no observable here.
		// `SetCondition(COND_SCHEDULE_DONE 0x5d)`.
		Cognition.Conditions.Set(EElysiumNpcCond::ScheduleDone);
	}
}

// 0x10273e80 `CAI_BaseNPC::TaskComplete(bool)`
void FElysiumNpc::TaskComplete(bool bIgnoreTaskFailed)
{
	// `if (!ignore && HasCondition(COND_TASK_FAILED 0x5c)) return;` — a completion cannot overwrite
	// an already-raised failure. Then `fTaskStatus (+0x5c44) = 4` (TASKSTATUS_COMPLETE).
	if (!bIgnoreTaskFailed && Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed))
	{
		return;
	}
	Schedule.TaskStatus = EElysiumTaskStatus::Complete;
}

// 0x102623c0 `CAI_Motor` slot 2
void FElysiumNpc::MotorTaskComplete(bool bIgnoreTaskFailed)
{
	// `thunk_FUN_10273e80(this->m_pOuter (+0x4), param)`. The motor is a sub-object of the NPC here,
	// so the back-pointer hop is the identity.
	TaskComplete(bIgnoreTaskFailed);
}

// 0x102623a0 `CAI_Motor` slot 1
void FElysiumNpc::MotorTaskFail(int32 Reason)
{
	// `MOV ECX,[ECX+4]; JMP [[ECX]+0x700]` — the owner's slot 448, unchanged. `FElysiumNpc::TaskFail`
	// IS slot 448 (`0x1029adb0`), ported in story 13; family Motor's `ValidateNavGoal` already calls
	// it with `0x1b`. This adds the motor's door and nothing else.
	TaskFail(Reason);
}

// 0x1027db30 — the navigator node-index guard (NOT `StartTaskByIndex`; see the declaration)
bool FElysiumNpc::IsUnusableNodeIndex(int32 NodeIndex) const
{
	// EAX = m_pNavigator (+0x5d34); EAX = [EAX + 0x2c] — the node list, count at +0x00, array at
	// +0x04. A negative index, or one at or past the count, increments `DAT_0x106c994c` and answers
	// false; a null node answers false without touching the counter; otherwise slot 527.
	//
	// SEAM: the port's motor carries no node list, so the count is zero and every index is out of
	// range. The error counter is a global diagnostic with no port home, so the out-of-range arm is
	// tallied instead.
	ElysiumStub::Fired(TEXT("navigator"), TEXT("CAI_BaseNPC::IsUnusableNodeIndex 0x1027db30"),
		DebugString(), FString::FromInt(NodeIndex),
		TEXT("0002: no node list on the motor; retail bumps 0x106c994c"));
	return false;
}

// -------------------------------------------------------------------------------------------------
// slot 418 `ResolveTaskDistance` — the stubbed slot, plus two species overrides.
// -------------------------------------------------------------------------------------------------

// slot 418 0x102bf6e0 `float ResolveTaskDistance(float)`
float FElysiumNpc::ResolveTaskDistance(float Distance)
{
	// `CNPC_VMingXiao` (`0x10392a10`) and `CNPC_VTzimisce` (`0x103b9120`) override slot 418 on their
	// C++ classes (story 5 step 3): each tests its own sentinel and only then calls this body.
	const int32 Truncated = static_cast<int32>(Distance);   // retail's `__ftol`

	// 0x102bf6e0, the Troika line: a four-entry jump table on `(int)param + 1000008`.
	switch (Truncated + 1000008)
	{
	case 0:   // -1000008
		return FollowerDistanceBackAway;
	case 1:   // -1000007
		return FollowerDistanceWalkTo;
	case 2:   // -1000006
		return FollowerDistanceRunTo;
	case 3:   // -1000005
		return GScheduleFollowerOverlap;
	default:
		break;
	}
	// `CAI_BaseNPC::ResolveTaskDistance` (`0x102702d0`), layer 0 and NOT one of this story's rows:
	// it splits -1000003 (through a global's slot 1), -1000002 and -1000000 and otherwise answers
	// the truncated value. Only the pass-through arm is reproduced; the three sentinels are named.
	if (Truncated == -1000003 || Truncated == -1000002 || Truncated == -1000000)
	{
		ElysiumStub::Fired(TEXT("schedule"), TEXT("CAI_BaseNPC::ResolveTaskDistance 0x102702d0"),
			DebugString(), FString::FromInt(Truncated), TEXT("0002/29c: the base sentinel arms"));
	}
	return static_cast<float>(Truncated);
}

// -------------------------------------------------------------------------------------------------
// slot 547 `GetSlotSchedule` — the stubbed slot.
// -------------------------------------------------------------------------------------------------

// slot 547 0x1028b0f0 `int GetSlotSchedule(int)`
int32 FElysiumNpc::GetSlotSchedule(int32 SquadSlot)
{
	// The whole retail body: `DevMsg("ERROR: Subclass missing GetSlotSchedule()!\n"); return 0;`.
	// No class in the family overrides it, so this warning IS the behaviour, and family Squad's
	// recovery says why nothing ever reaches it usefully — every class registers zero squad slots.
	(void)SquadSlot;
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s ERROR: Subclass missing GetSlotSchedule()!"),
		*DebugString());
	return 0;
}

// -------------------------------------------------------------------------------------------------
// The pre-selector.
// -------------------------------------------------------------------------------------------------

// 0x1028a2a0 `CAI_BaseNPC::PreSelectSchedule` (slot 437 for the base classes)
int32 FElysiumNpc::BasePreSelectSchedule()
{
	// `field_0x1b2c = 1` — retail's selector trace, ELYSIUM_NPC_WORD_ABSENT: "this runtime records
	// selections in the mind's transition trace". The tag is recorded there rather than dropped.
	RecordScheduleEvent(TEXT("PreSelectSchedule trace 1 (CAI_BaseNPC 0x1028a2a0)"));

	// Arm 1, and it is NOT a return: floating off the ground restores gravity and clears the ground
	// entity, then falls through to the three tests.
	if (Cognition.Conditions.Has(EElysiumNpcCond::FloatingOffGround))
	{
		Gravity = 1.0f;
		SetGroundEntity(nullptr);   // slot 208
	}
	// Retail's order, corrected by `docs/vtmb/npc-ai/conditions-and-states.md` § "The idle branch,
	// decided": NPC_FREEZE, then ON_FIRE, then FLOATING_OFF_GROUND again.
	if (Cognition.Conditions.Has(EElysiumNpcCond::NpcFreeze))
	{
		RecordScheduleEvent(TEXT("PreSelectSchedule AI_BaseNPC.cpp:3627 -> 0x3a NPC_FREEZE"));
		return 0x3a;
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::OnFire))
	{
		RecordScheduleEvent(TEXT("PreSelectSchedule AI_BaseNPC.cpp:3634 -> 0x151"));
		return 0x151;
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::FloatingOffGround))
	{
		RecordScheduleEvent(TEXT("PreSelectSchedule AI_BaseNPC.cpp:3639 -> 0x3e FALL_TO_GROUND"));
		return 0x3e;
	}
	return 0;
}

// -------------------------------------------------------------------------------------------------
// The species half of slot 438 `SelectSchedule`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::SpeciesSelectSchedule()
{
	// No species body on the Troika line: 0 lets the Troika selector run. Five classes override this
	// hook with their replacement selectors (story 5 step 3).
	return 0;
}

// -------------------------------------------------------------------------------------------------
// The melee failure gate and slot 604 `SelectScheduleMeleeCombat`.
// -------------------------------------------------------------------------------------------------

// 0x102b6fe0
int32 FElysiumNpc::MeleeScheduleFailureGate(FElysiumEntity* Enemy)
{
	// Two identical shapes, one per condition, in retail's order.
	const bool bOccluded = Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded);
	const bool bBlocked = !bOccluded && Cognition.Conditions.Has(EElysiumNpcCond::EnemyBlocked);
	if (!bOccluded && !bBlocked)
	{
		return 0;
	}
	if (NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this))   // slot 308
	{
		if (bInMelee)
		{
			Slot601(Enemy);   // vtable +0x964, `void vfunc601(CBaseEntity*)`; retail name unrecovered
		}
		RecordScheduleEvent(bOccluded
			? TEXT("MeleeScheduleFailureGate AI_BaseNPCTroika.cpp:23135 -> 0xe9")
			: TEXT("MeleeScheduleFailureGate AI_BaseNPCTroika.cpp:23159 -> 0xe9"));
		return 0xe9;
	}
	if (bOccluded)
	{
		RecordScheduleEvent(TEXT("MeleeScheduleFailureGate AI_BaseNPCTroika.cpp:23145 -> 0xcd"));
		return 0xcd;
	}
	RecordScheduleEvent(TEXT("MeleeScheduleFailureGate AI_BaseNPCTroika.cpp:23169 -> 0xce"));
	return 0xce;
}
// slot 604 0x102b6c30 `int SelectScheduleMeleeCombat(int)`, the Troika line
int32 FElysiumNpc::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;   // retail's argument is read by no arm of any of the six bodies
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FElysiumEntity* Enemy = const_cast<FElysiumEntity*>(World != nullptr
		? ElysiumNpcCond::ResolveEnemyHandle(*World, BaseMemory.Enemy) : nullptr);
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// Eight species classes override slot 604 on their C++ classes (story 5 step 3): the human line
	// `0x10385e40`, MingXiao `0x10396050`, Bach `0x10364080`, the Chang brothers `0x1036d800`, the
	// Tzimisce runner `0x103c4430`, AsianVampire `0x10361be0`, SheriffMan `0x103af960` and
	// SabbatLeader `0x103aa060`. Each REPLACES this body wholesale.

	// --- The Troika line 0x102b6c30 ----------------------------------------------------------------
	if (!bInMelee)
	{
		if (!Slot599(0))
		{
			if (const int32 Gate = MeleeScheduleFailureGate(Enemy); Gate != 0)
			{
				return Gate;
			}
			RecordScheduleEvent(TEXT("SelectScheduleMeleeCombat AI_BaseNPCTroika.cpp:22943 -> 0xe4"));
			return 0xe4;
		}
	}
	else if (Slot602())
	{
		Slot601(Enemy);
		if (NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this))
		{
			return 0xe9;
		}
		return ScheduleHost.EnemyDistUnits > MeleeRangeUnits() * 2.0f ? 0xe7 : 0xe4;
	}
	if (const int32 Gate = MeleeScheduleFailureGate(Enemy); Gate != 0)
	{
		return Gate;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		return NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this) ? 0xdc : 0xdd;
	}
	const bool bHeightArmed = NpcKernelScheduleShared::TickMeleeHeightDiffTimer(*this, Now);
	if (NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this)
		&& (Conds.Has(EElysiumNpcCond::TooFarForMelee) || Conds.Has(EElysiumNpcCond::InterruptTime)
			|| Conds.Has(EElysiumNpcCond::EnemyUnreachable) || bHeightArmed))
	{
		Slot601(Enemy);
		return 0xe9;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		Slot601(Enemy);
		return 0x17;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee) && !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		return 199;
	}
	return NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this) ? 0xca : 0xcb;
}
// `CNPC_VChangBros::SelectScheduleMeleeCombat` `0x1036d800` and `CNPC_VTzimisceRunner`'s
// `0x103c4430`: one shape, two schedule id sets (`bChang`). The `0x59` and `0x15a`/`0xe1` arms are
// where they differ. Each class's slot-604 override calls it (story 5 step 3).
int32 FElysiumNpc::SelectScheduleMeleeCombatChangLine(bool bChang)
{
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FElysiumEntity* Enemy = const_cast<FElysiumEntity*>(World != nullptr
		? ElysiumNpcCond::ResolveEnemyHandle(*World, BaseMemory.Enemy) : nullptr);
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	if (!bInMelee && !Slot599(0))
	{
		// SEAM: slot 599 takes the enemy in retail (`GetEnemy()` then `vfunc599(enemy)`); the
		// ledger types its parameter `int`, so the generated signature cannot carry a pointer
		// and the port passes 0. Named rather than hidden.
		const float Threshold = MeleeRangeUnits() + GScheduleChangMeleeMargin;
		if (ScheduleHost.EnemyDistUnits <= Threshold)
		{
			return 0xe4;
		}
		return 0xe7;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyOccluded))
	{
		return 0xcd;
	}
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
		EElysiumNpcCond::ShouldDodge))
	{
		return 0xd5;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		return 0xdd;
	}
	const bool bHeightArmed = NpcKernelScheduleShared::TickMeleeHeightDiffTimer(*this, Now);
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		return bChang ? 0x15d : 0x17;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee)
		&& !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		return 199;
	}
	// `GetEnemy()` twice, then the enemy's `WorldSpaceCenter` (slot 192) into a discarded
	// stack vector, then `0x102a11d0`.
	if (Enemy != nullptr && ScheduleMeleeReachGate())
	{
		return bChang ? 0x15a : 0xe1;
	}
	if (ScheduleHost.EnemyDistUnits <= MeleeRangeUnits() && !bHeightArmed)
	{
		return 0xd2;
	}
	return 0xcb;
}

// -------------------------------------------------------------------------------------------------
// 0x102b7690 — the entrenched cover / kick-prop selector.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::SelectCoverOrKickSchedule(const FScheduleHintSearchRequest& Request)
{
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const bool bHasHintNode = BaseScheduleHost.HintNode != INDEX_NONE;
	const bool bDodging = NpcFlags.Has(EElysiumNpcFlag::DODGING);   // flags1 0x800
	FElysiumEntity* Prop = World != nullptr ? World->Resolve(ScheduleHost.KickProp) : nullptr;

	// A. The kick-physics-prop refresh, on its own two-second timer.
	if (Request.bRequest4 && ScheduleHost.bAllowKickHintUse && !bHasHintNode && !bDodging
		&& Prop == nullptr && ScheduleHost.KickPropSearchTimer <= Now)
	{
		// `RandomFloat(2.0, 2.0)` — a draw whose bounds are equal, so the interval is exactly two
		// seconds. Reproduced as the draw retail makes, because it advances the stream.
		ScheduleHost.KickPropSearchTimer =
			Now + ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(2.0f, 2.0f);
		Prop = FindKickPhysicsProp();
		ScheduleHost.KickProp = Prop != nullptr ? Prop->Handle : FElysiumEntityHandle::Invalid();
	}

	// B. A live kick prop short-circuits the whole hint search.
	if (Prop != nullptr)
	{
		return 0xa9;   // SCHED_TROIKA_KICK_PROP_AT_ENEMY
	}

	// C. The hint search, under the same three gates.
	if (!bHasHintNode && !bDodging && Prop == nullptr)
	{
		const FElysiumEntity* Enemy = World != nullptr
			? ElysiumNpcCond::ResolveEnemyHandle(*World, BaseMemory.Enemy) : nullptr;
		ScheduleHost.HintCoverObject =
			Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle::Invalid();
		uint32 SearchMask = Request.bRequest1 ? 1u : 0u;
		if (Request.bRequest2)
		{
			SearchMask |= 2u;
		}
		if (Request.bRequest3 && ScheduleHost.bAllowKickHintUse)
		{
			SearchMask |= 4u;
		}
		if (Request.bRequest4 && ScheduleHost.bAllowKickHintUse)
		{
			SearchMask |= 8u;
		}
		SearchForCoverHint(SearchMask);
	}

	// D. The hint-type table. Retail reads `m_pHintNode->m_nHintType` (`+0x5dc`).
	FHintWords Hint;
	if (BaseScheduleHost.HintNode == INDEX_NONE || !HintWords(BaseScheduleHost.HintNode, Hint))
	{
		return 0;
	}
	if (Hint.HintType == GScheduleHintTypeShootAtA)
	{
		RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23812 -> 0xa7"));
		return 0xa7;
	}
	if (Hint.HintType == GScheduleHintTypeShootAtB)
	{
		RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23818 -> 0xa8"));
		return 0xa8;
	}
	if (Hint.HintType != GScheduleHintTypeCoverLow && Hint.HintType != GScheduleHintTypeCoverMed
		&& Hint.HintType != GScheduleHintTypeLean)
	{
		return 0;
	}

	// The cover-hint tail.
	if (!NpcFlags.Has(EElysiumNpcFlag::AT_COVER_HINT))   // flags1 0x2000
	{
		RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23805 -> 0x9b"));
		NpcFlags.ClearRawWord2Bits(GScheduleCoverVsMeleeClearMask);
		return 0x9b;
	}

	// "Does my enemy carry a ranged threat" — the enemy's own enemy's active weapon reporting
	// `0x6000` in its capability word (slot 360). `bNoRangedThreat` is retail's `bVar1`.
	bool bNoRangedThreat = true;
	if (const FElysiumEntity* Enemy = World != nullptr
		? ElysiumNpcCond::ResolveEnemyHandle(*World, BaseMemory.Enemy) : nullptr)
	{
		// SEAM: retail reads `enemy->+0x9c` — `CBaseCombatCharacter`'s cached downcast of ITSELF
		// (`docs/vtmb/npc-ai/schedule-kernel.md` § "The three cached downcasts") — and asks that
		// character's active weapon. This substrate has no cross-entity weapon-capability reader, so
		// the threat is never seen and the `bVar1 == true` arm is the one taken.
		ElysiumStub::Fired(TEXT("schedule"),
			TEXT("SelectCoverOrKickSchedule enemy ranged-threat bits 0x6000"), DebugString(),
			*Enemy->DebugString(), TEXT("0002/29c-1: no cross-entity weapon capability reader"));
		(void)GScheduleRangedThreatWeaponBits;
	}

	if (ScheduleHost.ShootAtHintNode == 0)
	{
		// `m_pShootAtHint = vfunc609(false)` (vtable `+0x984`, `CAI_Hint*`). The port carries hints
		// as node indices, and slot 609 is a generated stub answering null, so the write is null and
		// the inner arm below is the one taken.
		void* const ShootAtHint = Slot609(false);
		if (ShootAtHint == nullptr)
		{
			if (!HintIdleActivityGate())   // 0x102b5de0
			{
				++PeekOutCount;
				if (PeekOutCount < 5 && !Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded))
				{
					RecordScheduleEvent(
						TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23797 -> 0x9d"));
					return 0x9d;
				}
				NpcFlags.ClearRawWord2Bits(GScheduleCoverVsMeleeClearMask);
				if (!bStayEntrenched)
				{
					// `CAI_BaseNPCTroika::ClearHintNode(60.0)` and no schedule.
					ClearScheduleHint(60.0f);
					return 0;
				}
				RecordScheduleEvent(
					TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23792 -> 0x9e"));
				return 0x9e;
			}
			// The gate passed: the peek-out counter decays by two with a floor at zero.
			PeekOutCount = FMath::Max(PeekOutCount - 2, 0);
			// `m_pSchedule == NULL || m_pSchedule != GetScheduleOfType(0x9e)` — "I am not already
			// running 0x9e" — and `COVER_VS_MELEE_MODE` (flags2 0x100) clear.
			const bool bNotAlready9e = !Schedule.IsRunning()
				|| GetLocalScheduleId(Schedule.Current) != 0x9e;
			const bool bCoverVsMelee = NpcFlags.Has(EElysiumNpcFlag2::COVER_VS_MELEE_MODE);
			if (!bNoRangedThreat)
			{
				const int32 Roll =
					ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99);
				if (bNotAlready9e && !bCoverVsMelee)
				{
					RecordScheduleEvent(
						TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23769 -> 0xa3"));
					return 0xa3;
				}
				NpcFlags.ClearRawWord2Bits(GScheduleCoverVsMeleeClearMask);
				return Roll > 0x1d ? 0xa0 : 0xa1;
			}
			if (bNotAlready9e && !bCoverVsMelee)
			{
				RecordScheduleEvent(
					TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23747 -> 0xa4"));
				return 0xa4;
			}
			RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23743 -> 0xa5"));
			NpcFlags.Set(EElysiumNpcFlag2::COVER_VS_MELEE_MODE);
			NpcFlags.SetRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
			return 0xa5;
		}
	}
	RecordScheduleEvent(TEXT("SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23725 -> 0xa2"));
	return 0xa2;
}

// -------------------------------------------------------------------------------------------------
// The seams.
// -------------------------------------------------------------------------------------------------

FElysiumEntity* FElysiumNpc::FindKickPhysicsProp() const
{
	// SEAM for `0x102b6650`, which reads `GetEnemy()` (`+0x29c`) and `GetAbsOrigin()` (`+0x364`) and
	// sweeps for a kickable `prop_physics`. `FElysiumNpc::TaskFail` already knows how to release one
	// (`ScheduleHost.KickProp`), so only the producer is missing.
	return nullptr;
}

void FElysiumNpc::SearchForCoverHint(uint32 SearchMask)
{
	// SEAM for `0x102b7110`, the hint search that writes `m_pHintNode` (`+0x5ddc`). It reads
	// `m_bLeaningLeft`, the cover-LOS counters (`+0x6404`/`+0x6408`), `m_iPeekOutCount` and
	// `m_bStayEntrenched` and is the Hints layer's producer, not this family's.
	ElysiumStub::Fired(TEXT("hints"), TEXT("CAI_BaseNPCTroika::SearchForCoverHint 0x102b7110"),
		DebugString(), FString::Printf(TEXT("mask=0x%x"), SearchMask),
		TEXT("0002/29c-1: no hint-node store"));
}

bool FElysiumNpc::ScheduleMeleeReachGate() const
{
	// SEAM for `0x102a11d0`. Retail name unrecovered; four direct callers, reads `+0x300` (the
	// enemy's `WorldSpaceCenter`) and `+0x650`.
	return false;
}

int32 FElysiumNpc::NavigatorGoalType() const
{
	// SEAM for `0x1027d990`, the navigator probe `CNPC_VManBat::SelectSchedule` opens with. It reads
	// the navigator at `+0x5d34`; the port's motor carries no goal-type word. Answering anything but
	// 2 is retail's "no active goal" arm, and 0 is the value `GoalType_t` spells for it.
	return 0;
}
