#include "Substrate/ElysiumNpcHengeyokai.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumAnimEvent.h"

const FElysiumNpcClass* FElysiumNpcHengeyokai::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VHengeyokai"));
	return Row;
}

// Slot 420: `0x1037fa70`.
void FElysiumNpcHengeyokai::NPCInit()
{
	HengeyokaiNPCInit();
}

// Slot 104: `0x1037f960`.
void FElysiumNpcHengeyokai::Precache()
{
	HengeyokaiPrecache();
}

// Slot 375: `0x10381b50`, which calls the human line's `0x103854f0` directly.
int32 FElysiumNpcHengeyokai::NPC_EarlyTranslateActivity(int32 Activity)
{
	return HengeyokaiNpcEarlyTranslateActivity(Activity);
}

// Slot 461: `0x10380100`, the selector tag 0x13 and then a direct call into the human line's `0x103851e0`.
int32 FElysiumNpcHengeyokai::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x13;
	return HumanSelectIdealState();
}

// Slot 448: `0x10380510`, its own arm and then a direct call into the Troika body `0x1029adb0`.
void FElysiumNpcHengeyokai::TaskFail(int32 Reason)
{
	HengeyokaiTaskFail(Reason);
	FElysiumNpc::TaskFail(Reason);
}

// Slot 440: `0x1037ffa0`.
int32 FElysiumNpcHengeyokai::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return HengeyokaiTranslateSchedule(ScheduleNumber);
}

// Slot 69: `0x10380f90` (byte-identical across Hengeyokai, MingXiao and Tzimisce): the `0x16` derived-type
// gate, then a direct call into the Troika body `0x1029b180`.
bool FElysiumNpcHengeyokai::NavIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr && (RetailDerivedType(*Other) & 0x16) != 0)
	{
		return true;
	}
	return FElysiumNpc::NavIgnoreCollision(Other);
}

// Slot 124: `0x10383560`
int32 FElysiumNpcHengeyokai::DrawDebugTextOverlays()
{
	return HengeyokaiDrawDebugTextOverlays();
}

// Slot 337: `0x1037fb20`.
int32 FElysiumNpcHengeyokai::GetUsedHullBits()
{
	return SpeciesUsedHullBits(TEXT("0x1037fb20"));
}

// Slot 546: `0x1037ea60`, the class's own schedule id space.
const TCHAR* FElysiumNpcHengeyokai::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VHengeyokai"), SlotEn);
}

// Slot 259: `0x1037fb60`, the footstep body of `docs/vtmb/footsteps.md` §1.7; an id it does not
// claim is a direct call into the base body.
bool FElysiumNpcHengeyokai::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return SpeciesFootstepAnimEvent(TEXT("npc_VHengeyokai"), Event);
}

// Slot 292: `0x103802a0` — no flinch from gunfire or a zero-magnitude hit; otherwise the base
// `CBaseCombatCharacter::DamageFlinch`. The port's flinch runs through `StartDamageFlinch`, whose
// species hook this is.
bool FElysiumNpcHengeyokai::SuppressesDamageFlinch(const FElysiumDmg& Dmg) const
{
	return SpeciesSuppressesDamageFlinch(TEXT("CNPC_VHengeyokai"), Dmg);
}

// Slot 435: `0x10383090`, the Troika body `0x102a0940` directly, then the class's own tail.
void FElysiumNpcHengeyokai::OnScheduleChange(int32 NewSchedule)
{
	HengeyokaiOnScheduleChange(NewSchedule);
}
