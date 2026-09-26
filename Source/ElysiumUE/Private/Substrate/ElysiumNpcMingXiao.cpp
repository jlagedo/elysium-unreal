#include "Substrate/ElysiumNpcMingXiao.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumAnimEvent.h"

const FElysiumNpcClass* FElysiumNpcMingXiao::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VMingXiao"));
	return Row;
}

// Slot 482: `0x10396e90`, the same standalone copy as the human line's.
int32 FElysiumNpcMingXiao::CanPlaySequence(bool bDisregardState, int32 InterruptLevel)
{
	return CanPlaySequenceSpecies(bDisregardState, InterruptLevel);
}

// Slot 104: `0x10392660`.
void FElysiumNpcMingXiao::Precache()
{
	MingXiaoPrecache();
}

// Slot 126: `0x10395f80`.
int32 FElysiumNpcMingXiao::Save(void* Archive)
{
	return MingXiaoSave(Archive);
}

// Slot 127: `0x10396000`.
int32 FElysiumNpcMingXiao::Restore(void* Archive)
{
	return MingXiaoRestore(Archive);
}

// Slot 180: `0x10391230`, which ends in `TroikaUpdateOnRemove`.
void FElysiumNpcMingXiao::UpdateOnRemove()
{
	MingXiaoUpdateOnRemove();
}

// Slot 461: `0x103945a0`, a complete replacement: `GetEnemy() ? COMBAT : IDLE`.
int32 FElysiumNpcMingXiao::SelectIdealStateRetail()
{
	return SpeciesIdealStateRetail(EIdealStateSpecies::MingXiao);
}

// Slot 574: `0x10395d00`
FVector FElysiumNpcMingXiao::GetShootEnemyDir(const FVector& ShootPositionCm, int32 A, int32 B)
{
	return MingXiaoGetShootEnemyDir(ShootPositionCm, A, B);
}

// Slot 604: `0x10396050`, which replaces the Troika body wholesale; its argument is read by no arm.
int32 FElysiumNpcMingXiao::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	return SelectScheduleMeleeCombatMingXiao();
}

// Slot 418: `0x10392a10`, a species sentinel ahead of a direct call into the Troika body.
float FElysiumNpcMingXiao::ResolveTaskDistance(float Distance)
{
	return MingXiaoResolveTaskDistance(Distance);
}

// Slot 448: `0x10394090`, its own arm and then a direct call into the Troika body `0x1029adb0`.
void FElysiumNpcMingXiao::TaskFail(int32 Reason)
{
	MingXiaoTaskFail(Reason);
	FElysiumNpc::TaskFail(Reason);
}

// Slot 348: `0x103970d0`
int32 FElysiumNpcMingXiao::HealthToPercent()
{
	return MingXiaoHealthToPercent();
}

// Slot 605: `0x103967d0`
int32 FElysiumNpcMingXiao::SelectScheduleRangedCombat(int32 Arg)
{
	return MingXiaoSelectScheduleRangedCombat(Arg);
}

// Slot 440: `0x10394570`.
int32 FElysiumNpcMingXiao::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return MingXiaoTranslateSchedule(ScheduleNumber);
}

// Slot 516: `0x10394930`, which replaces the Troika ladder.
float FElysiumNpcMingXiao::MaxYawSpeed()
{
	return MingXiaoMaxYawSpeed();
}

// Slot 69: `0x10396fd0` (byte-identical across Hengeyokai, MingXiao and Tzimisce): the `0x16` derived-type
// gate, then a direct call into the Troika body `0x1029b180`.
bool FElysiumNpcMingXiao::NavIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr && (RetailDerivedType(*Other) & 0x16) != 0)
	{
		return true;
	}
	return FElysiumNpc::NavIgnoreCollision(Other);
}

// Slot 123: `0x10399d40`
void FElysiumNpcMingXiao::DrawDebugGeometryOverlays()
{
	MingXiaoDrawDebugGeometryOverlays();
}

// Slot 408: `0x103951d0`, whose miss calls `CAI_BaseNPC::GetShortConditionName` (`0x1027ede0`) directly.
const TCHAR* FElysiumNpcMingXiao::GetShortConditionName(int32 ConditionId)
{
	return SpeciesShortConditionName(TEXT("CNPC_VMingXiao"), ConditionId);
}

// Slot 465: `0x103947b0`, ending in a direct call into `CAI_BaseNPCTroika::OnChangeActivity` (`0x10295a60`).
void FElysiumNpcMingXiao::OnChangeActivity(int32 Activity)
{
	MingXiaoOnChangeActivity(Activity);
}

// Slot 337: `0x10392a50`.
int32 FElysiumNpcMingXiao::GetUsedHullBits()
{
	return SpeciesUsedHullBits(TEXT("0x10392a50"));
}

// Slot 546: `0x10391390`, the class's own schedule id space.
const TCHAR* FElysiumNpcMingXiao::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VMingXiao"), SlotEn);
}

// Slot 259: `0x10392a70`, the footstep body of `docs/vtmb/footsteps.md` §1.7; an id it does not
// claim is a direct call into the base body.
bool FElysiumNpcMingXiao::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return SpeciesFootstepAnimEvent(TEXT("npc_VMingXiao"), Event);
}

// Slot 563: `0x10392c40`, the `GoalToleranceLead` shape; a replacement that does not chain.
void FElysiumNpcMingXiao::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	TranslateEnemyChasePositionShaped(EChaseTranslateShape::GoalToleranceLead, Enemy, ChasePositionCm, Tolerance,
		SecondTolerance);
}
