#include "Substrate/ElysiumNpcMingXiaoTentacle.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcMingXiaoTentacle::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VMingXiaoTentacle"));
	return Row;
}

// Slots 21-23: `0x1039e800` / `0x1039e830` / `0x1039e860`, each forwarding to the head.
void FElysiumNpcMingXiaoTentacle::Slot21(FElysiumEntity* Attacker)
{
	FUN_1039e800(Attacker);
}

void FElysiumNpcMingXiaoTentacle::Slot22(FElysiumEntity* Attacker)
{
	FUN_1039e830(Attacker);
}

void FElysiumNpcMingXiaoTentacle::Slot23(FElysiumEntity* Attacker)
{
	FUN_1039e860(Attacker);
}

// Slot 130: `0x1039f000`, which calls the Troika body first.
void FElysiumNpcMingXiaoTentacle::OnRestore(bool bFromLoad)
{
	MingXiaoTentacleOnRestore(bFromLoad);
}

// Slot 104: `0x1039c220`.
void FElysiumNpcMingXiaoTentacle::Precache()
{
	MingXiaoTentaclePrecache();
}

// Slot 126: `0x1039ed50`.
int32 FElysiumNpcMingXiaoTentacle::Save(void* Archive)
{
	return MingXiaoTentacleSave(Archive);
}

// Slot 127: `0x1039eda0`.
int32 FElysiumNpcMingXiaoTentacle::Restore(void* Archive)
{
	return MingXiaoTentacleRestore(Archive);
}

// Slot 461: `0x1039e310`, a complete replacement: DEAD stays DEAD, else `GetEnemy() ? COMBAT : IDLE`.
int32 FElysiumNpcMingXiaoTentacle::SelectIdealStateRetail()
{
	return SpeciesIdealStateRetail(EIdealStateSpecies::MingXiaoTentacle);
}

// Slot 437: `0x1039de00`.
int32 FElysiumNpcMingXiaoTentacle::PreSelectSchedule()
{
	return MingXiaoTentaclePreSelectSchedule();
}

// Slot 438: `0x1039de20`, which replaces the whole selector (the Troika selector's species hook).
int32 FElysiumNpcMingXiaoTentacle::SpeciesSelectSchedule()
{
	return MingXiaoTentacleSelectSchedule();
}

// Slot 440: `0x1039e2d0`.
int32 FElysiumNpcMingXiaoTentacle::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return MingXiaoTentacleTranslateSchedule(ScheduleNumber);
}

// Slot 68: `0x1039eb50`. Ignore unconditionally unless the candidate is non-null AND (it is not a
// `CBaseCombatCharacter` (other+0x9c) OR `m_bIgnoreCollision` is clear); then a direct call into the
// Troika body `0x1029afc0`.
bool FElysiumNpcMingXiaoTentacle::ShouldIgnoreCollision(FElysiumEntity* Other)
{
	const bool bFallToBase = Other != nullptr
		&& (Other->AsCombatCharacter() == nullptr || !bIgnoreCollisionSpecies);
	if (!bFallToBase)
	{
		return true;
	}
	return FElysiumNpc::ShouldIgnoreCollision(Other);
}

// Slot 69: `0x1039eb90`, the same shape as its slot 68 falling to the NAV base `0x1029b180`.
bool FElysiumNpcMingXiaoTentacle::NavIgnoreCollision(FElysiumEntity* Other)
{
	const bool bFallToBase = Other != nullptr
		&& (Other->AsCombatCharacter() == nullptr || !bIgnoreCollisionSpecies);
	if (!bFallToBase)
	{
		return true;
	}
	return FElysiumNpc::NavIgnoreCollision(Other);
}

// Slot 166: `0x1039ebd0`. The tentacle's own companion is never standable; then a direct call into
// the family body `0x10026f80`.
bool FElysiumNpcMingXiaoTentacle::CanStandOn(FElysiumEntity* Other)
{
	if (Other == MingXiaoTentacleCompanion())
	{
		return false;
	}
	return FElysiumNpc::CanStandOn(Other);
}

// Slot 408: `0x1039ece0`, whose miss calls `CAI_BaseNPC::GetShortConditionName` (`0x1027ede0`) directly.
const TCHAR* FElysiumNpcMingXiaoTentacle::GetShortConditionName(int32 ConditionId)
{
	return SpeciesShortConditionName(TEXT("CNPC_VMingXiaoTentacle"), ConditionId);
}

// Slot 337: `0x1039c480`.
int32 FElysiumNpcMingXiaoTentacle::GetUsedHullBits()
{
	return SpeciesUsedHullBits(TEXT("0x1039c480"));
}

// Slot 546: `0x1039b230`, the class's own schedule id space.
const TCHAR* FElysiumNpcMingXiaoTentacle::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VMingXiaoTentacle"), SlotEn);
}
