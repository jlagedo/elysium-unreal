#include "Substrate/ElysiumNpcGargoyle.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumEntityDefs.h"

const FElysiumNpcClass* FElysiumNpcGargoyle::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VGargoyle"));
	return Row;
}

// Slots 599 / 600: `0x10379ef0` / `0x10379f20`, byte-identical to FrenzyShadow's unconditional
// melee entry.
bool FElysiumNpcGargoyle::Slot599(int32 Arg)
{
	(void)Arg;
	return FUN_10379ef0(static_cast<const FElysiumNpc*>(this)->GetEnemy());
}

bool FElysiumNpcGargoyle::Slot600(FElysiumEntity* Enemy)
{
	return FUN_10379f20(Enemy);
}

// Slot 420: `0x103785f0`.
void FElysiumNpcGargoyle::NPCInit()
{
	GargoyleNPCInit();
}

// Slot 104: `0x10378470`.
void FElysiumNpcGargoyle::Precache()
{
	GargoylePrecache();
}

// Slot 461: `0x10378b60`, the selector tag 0x11 and then a direct call into the human line's `0x103851e0`.
int32 FElysiumNpcGargoyle::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x11;
	return HumanSelectIdealState();
}

// Slot 448: `0x10379060`, its own arm and then a direct call into the Troika body `0x1029adb0`.
void FElysiumNpcGargoyle::TaskFail(int32 Reason)
{
	GargoyleTaskFail(Reason);
	FElysiumNpc::TaskFail(Reason);
}

// Slot 440: `0x10378a30`.
int32 FElysiumNpcGargoyle::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return GargoyleTranslateSchedule(ScheduleNumber);
}

// Slot 69: `0x10379490`. The `0x16` derived-type gate, then the three `FClassnameIs` compares
// (`prop_dynamic`, `func_brush`, `func_door_rotating`); otherwise a direct call into the Troika body
// `0x1029b180`.
bool FElysiumNpcGargoyle::NavIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr)
	{
		if ((RetailDerivedType(*Other) & 0x16) != 0)
		{
			return true;
		}
		if (GargoyleIgnoresClassname(Other->Def != nullptr ? Other->Def->Classname : FString()))
		{
			return true;
		}
	}
	return FElysiumNpc::NavIgnoreCollision(Other);
}

// Slot 337: `0x10378680`.
int32 FElysiumNpcGargoyle::GetUsedHullBits()
{
	return SpeciesUsedHullBits(TEXT("0x10378680"));
}

// Slot 24: `0x1037a450`, a replacement that never calls the Troika body.
void FElysiumNpcGargoyle::OnVictimHitByMe(FElysiumEntity* Victim)
{
	GargoyleOnVictimHitByMe(Victim);
}

// Slot 546: `0x10377cd0`, the class's own schedule id space.
const TCHAR* FElysiumNpcGargoyle::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VGargoyle"), SlotEn);
}

// Slot 292: `0x10378cb0` — no flinch from gunfire or a zero-magnitude hit; otherwise the base
// `CBaseCombatCharacter::DamageFlinch`. The port's flinch runs through `StartDamageFlinch`, whose
// species hook this is.
bool FElysiumNpcGargoyle::SuppressesDamageFlinch(const FElysiumDmg& Dmg) const
{
	return SpeciesSuppressesDamageFlinch(TEXT("CNPC_VGargoyle"), Dmg);
}

// Slot 435: `0x10378fc0`, the Troika body `0x102a0940` directly, then the class's own tail.
void FElysiumNpcGargoyle::OnScheduleChange(int32 NewSchedule)
{
	GargoyleOnScheduleChange(NewSchedule);
}

// Slot 175: `0x1037a270`.
void FElysiumNpcGargoyle::TouchSpecies(FElysiumEntity* Other)
{
	GargoyleTouch(Other);
}
