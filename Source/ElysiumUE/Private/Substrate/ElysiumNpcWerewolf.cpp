#include "Substrate/ElysiumNpcWerewolf.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcConditions.h"

const FElysiumNpcClass* FElysiumNpcWerewolf::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VWerewolf"));
	return Row;
}

// Slot 420: `0x103caef0`.
void FElysiumNpcWerewolf::NPCInit()
{
	WerewolfNPCInit();
}

// Slot 130: `0x103cabf0`, which calls the Troika body first.
void FElysiumNpcWerewolf::OnRestore(bool bFromLoad)
{
	WerewolfOnRestore(bFromLoad);
}

// Slot 104: `0x103cb2a0`.
void FElysiumNpcWerewolf::Precache()
{
	WerewolfPrecache();
}

// Slot 461: `0x103d0820`, chaining the Troika body directly.
int32 FElysiumNpcWerewolf::SelectIdealStateRetail()
{
	return WerewolfSelectIdealState();
}

// Slot 201: `0x103cb810`
bool FElysiumNpcWerewolf::FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4)
{
	// `CNPC_VWerewolf#201`, story 29c-1's `WerewolfFVisible`. Dispatched, not re-ported.
	FElysiumEntityHandle Unused;
	return WerewolfFVisible(SeenTarget, &Unused);
}

// Slot 448: `0x103ce750`, its own arm and then a direct call into the Troika body `0x1029adb0`.
void FElysiumNpcWerewolf::TaskFail(int32 Reason)
{
	WerewolfTaskFail(Reason);
	FElysiumNpc::TaskFail(Reason);
}

// Slot 304: `0x103cc9b0`
void FElysiumNpcWerewolf::GiveBaseFightingItems()
{
	WerewolfGiveBaseFightingItems();
}

// Slot 305: `0x103cca80`
void FElysiumNpcWerewolf::RemoveBaseFightingItems()
{
	WerewolfRemoveBaseFightingItems();
}

// Slot 440: `0x103d5e00`.
int32 FElysiumNpcWerewolf::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return WerewolfTranslateSchedule(ScheduleNumber);
}

// Slot 516: `0x103d0a30`, a scope-trace push/pop (the trace words are ABSENT in the shape map)
// around a direct call into the Troika body `0x10297ce0`.
float FElysiumNpcWerewolf::MaxYawSpeed()
{
	return FElysiumNpc::MaxYawSpeed();
}

// Slot 68: `0x103d9ab0`. `m_edtDerivedType & 0x16`, then `GetFlags2() & 8`; otherwise a direct call
// into the Troika body `0x1029afc0`.
bool FElysiumNpcWerewolf::ShouldIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr)
	{
		if ((RetailDerivedType(*Other) & 0x16) != 0)
		{
			return true;
		}
		if ((RetailFlags2(*Other) & 8) != 0)
		{
			return true;
		}
	}
	return FElysiumNpc::ShouldIgnoreCollision(Other);
}

// Slot 69: `0x103d9ba0`, the same two gates falling to the NAV base `0x1029b180`.
bool FElysiumNpcWerewolf::NavIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr)
	{
		if ((RetailDerivedType(*Other) & 0x16) != 0)
		{
			return true;
		}
		if ((RetailFlags2(*Other) & 8) != 0)
		{
			return true;
		}
	}
	return FElysiumNpc::NavIgnoreCollision(Other);
}

// Slot 620: `0x103d5050`, a virtual `CNPC_VWerewolf` introduces (no Troika-line body holds the slot).
void FElysiumNpcWerewolf::DrawBBoxOverlay()
{
	WerewolfDrawBBoxOverlay();
}

// Slot 408: `0x103d0640`, whose miss calls `CAI_BaseNPC::GetShortConditionName` (`0x1027ede0`) directly.
const TCHAR* FElysiumNpcWerewolf::GetShortConditionName(int32 ConditionId)
{
	return SpeciesShortConditionName(TEXT("CNPC_VWerewolf"), ConditionId);
}

// Slot 76: `0x103d5130`, which chains `CNPC_VBaseBoss::DrawDebugStatOverlays` (`0x10366290`) directly.
void FElysiumNpcWerewolf::DrawDebugStatOverlays()
{
	WerewolfDrawDebugStatOverlaysSlot();
}

// Slot 465: `0x103d5f60`, ending in a direct call into `CAI_BaseNPCTroika::OnChangeActivity` (`0x10295a60`).
void FElysiumNpcWerewolf::OnChangeActivity(int32 Activity)
{
	WerewolfOnChangeActivity(Activity);
}

// Slot 337: `0x103cab50`.
int32 FElysiumNpcWerewolf::GetUsedHullBits()
{
	return SpeciesUsedHullBits(TEXT("0x103cab50"));
}

// Slot 566: `0x103d7ce0`, a replacement that does not chain.
bool FElysiumNpcWerewolf::FValidateHintType(void* Hint)
{
	return SpeciesFValidateHintType(TEXT("CNPC_VWerewolf"), Hint);
}

// Slot 546: `0x103c8ed0`, the class's own schedule id space.
const TCHAR* FElysiumNpcWerewolf::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VWerewolf"), SlotEn);
}

// Slot 141: `0x103ccbf0`, a prologue ahead of a direct call into `CAI_BaseNPC::TraceAttack` (`0x10266780`).
void FElysiumNpcWerewolf::TraceAttack(void* InInfo, const FVector& DirUnits, void* InTrace)
{
	WerewolfTraceAttack(InInfo, DirUnits, InTrace);
}

// Slot 563: `0x103d9e00`, the `GoalToleranceWerewolfLead` shape; a replacement that does not chain.
void FElysiumNpcWerewolf::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	TranslateEnemyChasePositionShaped(EChaseTranslateShape::GoalToleranceWerewolfLead, Enemy, ChasePositionCm, Tolerance,
		SecondTolerance);
}

// Slot 435: `0x103ced10`, the Troika body `0x102a0940` directly, then the class's own tail.
void FElysiumNpcWerewolf::OnScheduleChange(int32 NewSchedule)
{
	WerewolfOnScheduleChange(NewSchedule);
}

// Slot 491: `0x103d87a0`, a replacement that does not chain.
void FElysiumNpcWerewolf::PainSound()
{
	WerewolfPainSound();
}

// Slot 500: `0x103d8660`, a replacement that does not chain.
void FElysiumNpcWerewolf::ExertHvySound()
{
	WerewolfExertHvySound();
}

// Slot 561: `0x103d02b0` — the zone melee suppression, else `CAI_BaseNPC::GatherAttackConditions`
// (`0x1026dd10`) directly.
void FElysiumNpcWerewolf::GatherAttackConditions(FElysiumEntity* Enemy, float DistanceUnits)
{
	if (ElysiumNpcCond::WerewolfZoneSuppressesMelee(*this, Cognition.Conditions))
	{
		return;
	}
	FElysiumNpc::GatherAttackConditions(Enemy, DistanceUnits);
}
