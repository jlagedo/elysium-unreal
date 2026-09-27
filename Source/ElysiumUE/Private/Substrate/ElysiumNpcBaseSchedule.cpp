// `CAI_BaseNPC`'s bodies of the `Schedule` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSchedule.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleShared.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `0x3b9aca00` — the "already a global id" boundary `0x10280de0` compares against. It is the same
	// 1,000,000,000 family Squad found the two squad-slot symbols registered under.
	constexpr int32 GScheduleGlobalIdBase = 1000000000;
}

// --- Moved from `ElysiumNpcSchedule.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::ScheduleLocalToGlobal(const FElysiumLocalIdSpace* Space, int32 LocalId)
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

int32 FElysiumNpcBase::ResolveIdealScheduleStamp(int32 RawRetailId) const
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
void FElysiumNpcBase::ChangeSchedule(int32 Id)
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

// 0x10280f40 `NextScheduledTask`
void FElysiumNpcBase::NextScheduledTask()
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
void FElysiumNpcBase::TaskComplete(bool bIgnoreTaskFailed)
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
void FElysiumNpcBase::MotorTaskComplete(bool bIgnoreTaskFailed)
{
	// `thunk_FUN_10273e80(this->m_pOuter (+0x4), param)`. The motor is a sub-object of the NPC here,
	// so the back-pointer hop is the identity.
	TaskComplete(bIgnoreTaskFailed);
}

// 0x1027db30 — the navigator node-index guard (NOT `StartTaskByIndex`; see the declaration)
bool FElysiumNpcBase::IsUnusableNodeIndex(int32 NodeIndex) const
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
// slot 547 `GetSlotSchedule` — the stubbed slot.
// -------------------------------------------------------------------------------------------------

// slot 547 0x1028b0f0 `int GetSlotSchedule(int)`
int32 FElysiumNpcBase::GetSlotSchedule(int32 SquadSlot)
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
int32 FElysiumNpcBase::PreSelectSchedule()
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
