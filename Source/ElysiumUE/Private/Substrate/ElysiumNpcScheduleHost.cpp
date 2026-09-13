#include "Substrate/ElysiumNpcScheduleHost.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"

// Story 29c-1, family **Hints**. `0x102a13d0`, `0x102a1420`, `0x102a1470`, `0x102a14c0`,
// `0x102a1510`, read from the decompiled C. Every one of the five is:
//
//     if (this->m_pHintNode == NULL) return -1;
//     switch (this->m_pHintNode->m_nHintType) {
//         case 100:    return <a>;
//         case 0x65:   return <b>;
//         case 0x27d8: return <c> - (m_bLeaningLeft != 0);   // three of five
//     }
//     return -1;
//
// The lean subtraction is present in `0x102a13d0`, `0x102a1470` and `0x102a14c0` and ABSENT from
// `0x102a1420` and `0x102a1510`, which is why this is one table and not one formula. Note also that
// `0x102a1420` and `0x102a1510` answer the SAME pair for types 100 and 0x65 (`0x1111`, `0x110d`)
// and differ only at 0x27d8 (`0x1118` against `0x55`) — reproduced, not collapsed.
int32 FElysiumNpcScheduleHost::HintNodeActivity(EElysiumHintActivityQuery Query, int32 HintType,
	bool bLeaningLeft)
{
	struct FRow
	{
		int32 Type100 = INDEX_NONE;
		int32 Type101 = INDEX_NONE;
		int32 Type10200 = INDEX_NONE;
		bool bLeanAdjusts10200 = false;
	};
	static const FRow Rows[] = {
		{ 0x1110, 0x110c, 0x111e, true },   // 0x102a13d0
		{ 0x1111, 0x110d, 0x1118, false },  // 0x102a1420
		{ 0x1112, 0x110e, 0x111a, true },   // 0x102a1470
		{ 0x1113, 0x003f, 0x111c, true },   // 0x102a14c0
		{ 0x1111, 0x110d, 0x0055, false },  // 0x102a1510
	};
	const FRow& Row = Rows[static_cast<int32>(Query)];
	switch (HintType)
	{
	case 100:
		return Row.Type100;
	case 0x65:
		return Row.Type101;
	case 0x27d8:
		return Row.bLeanAdjusts10200 && bLeaningLeft ? Row.Type10200 - 1 : Row.Type10200;
	default:
		// Retail's `m_pHintNode == NULL` arm and its "no case matched" arm are the same `-1`.
		return INDEX_NONE;
	}
}

// Story 29c-1, family **Schedule**. Three rows whose target names this struct.

bool FElysiumNpcScheduleHost::IsTaskIndexCurrent(const FElysiumScheduleState& State)
{
	// `0x10280db0`, whole:
	//
	//   return this->m_iScheduleIndex (+0x5c40) == this->m_pSchedule (+0x5c38)->field_0x24;
	//
	// and `CAI_Schedule +0x24` is the task COUNT (`docs/vtmb/npc-ai/conditions-and-states.md`
	// § "`DELAY_INTERRUPTS`, decoded", whose schedule-record table reads
	// "`+0x20` / `+0x24` task array (8 bytes/entry) and count").
	if (!State.IsRunning())
	{
		// Retail dereferences `m_pSchedule` unconditionally here; with no program installed there is
		// no task count to compare against, and answering false is the only honest reading.
		return false;
	}
	const FElysiumSchedule* Program = ElysiumScheduleFor(State.Current);
	return Program != nullptr && State.TaskIndex == Program->Tasks.Num();
}

// `_DAT_10447ee0` — the wait duration `0x102a18a0` substitutes for an operand at or below zero.
// UNRECOVERED: the corpus carries the address and 33 readers, not the float. 0.0 stands in, which
// makes the substitution a same-frame deadline; one edit closes it.
static constexpr float GScheduleDefaultWaitSeconds = 0.0f;

void FElysiumNpcScheduleHost::SetWaitFinished(float TaskSeconds, double Now)
{
	// `if (_DAT_104454c4 < task->flTaskData)` — the shared float zero, so "a positive operand".
	WaitFinished = Now + (TaskSeconds > 0.0f ? TaskSeconds : GScheduleDefaultWaitSeconds);
}

const TCHAR* FElysiumNpcScheduleHost::SetScheduleTraceName(const TCHAR* RetailClass)
{
	if (RetailClass == nullptr)
	{
		return TEXT("");
	}
	// The class, the body that fills slot 619 for it, and the literal it pushes. Checkable against
	// `docs/vtmb/npc-kernel/slots.md`; the literals are the `NameFromStrings` names the corpus
	// records at the push site.
	struct FRow
	{
		const TCHAR* Class;
		const TCHAR* Body;
		const TCHAR* Trace;
	};
	static const FRow Rows[] = {
		{ TEXT("CNPC_VAndreiBlood"), TEXT("0x1035dba0"), TEXT("CNPC_VAndreiBlood::SetSchedule") },
		{ TEXT("CNPC_VAsianVampire"), TEXT("0x10361530"), TEXT("CNPC_VAsianVampire::SetSchedule") },
		{ TEXT("CNPC_VChangBros"), TEXT("0x1036c760"), TEXT("CNPC_VChangBros::SetSchedule") },
		{ TEXT("CNPC_VChangBrosBlade"), TEXT("0x1036c760"), TEXT("CNPC_VChangBros::SetSchedule") },
		{ TEXT("CNPC_VChangBrosClaw"), TEXT("0x1036c760"), TEXT("CNPC_VChangBros::SetSchedule") },
		{ TEXT("CNPC_VSabbatLeader"), TEXT("0x103a9fd0"), TEXT("CNPC_VSabbatLeader::SetSchedule") },
		{ TEXT("CNPC_VSheriffMan"), TEXT("0x103af8d0"), TEXT("CNPC_VSheriffMan::SetSchedule") },
	};
	for (const FRow& Row : Rows)
	{
		if (FCString::Strcmp(Row.Class, RetailClass) == 0)
		{
			return Row.Trace;
		}
	}
	return TEXT("");
}

void FElysiumNpcScheduleHost::Serialize(FElysiumSaveArchive& Ar, const FElysiumEntityWorld* World)
{
	Ar << NextUpdate << NextNormal << NextMove << NextAI;
	Ar << LastUpdate << LastNormal << LastMove << LastAI;
	Ar << FailureReason << MemoryBits << GoalToleranceCm << DesiredMoveYaw;
	Ar << SquadDisconnected;
	Ar << InsideInterruptDistanceSqr << OutsideInterruptDistanceSqr << InterruptTime;
	Ar << MoveTarget << KickProp << HintNode << HintReusableAt << SavedSleepExtents << bPatrolPathUseHint;
	Ar << bOwnsHint << FailedCoverLosChecks << AttackExtentsCm;
	Ar << bSavePositionWalk << bMotorAnimationMovement << bWaitFinishedSet << Unknown6300 << Unknown659c << MoveWaitFinished;
	if (Ar.IsLoading()) PendingFailureReason = 0;
	if (Ar.IsLoading() && World)
	{
		MoveTarget = World->RebaseSavedHandle(MoveTarget);
		KickProp = World->RebaseSavedHandle(KickProp);
	}
}
