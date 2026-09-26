#include "Substrate/ElysiumNpcDog.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcDog::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VDog"));
	return Row;
}

// Slot 375: `0x10374ad0`, which calls the Troika body `0x10295590` directly.
int32 FElysiumNpcDog::NPC_EarlyTranslateActivity(int32 Activity)
{
	return DogNpcEarlyTranslateActivity(Activity);
}

// Slot 461: `0x103743c0`, chaining the animal line's `0x1035fe80` directly.
int32 FElysiumNpcDog::SelectIdealStateRetail()
{
	return DogSelectIdealState();
}

// Slot 460: `0x10374d80`, whose non-latched arms call the Troika body `0x102ad340` directly.
int32 FElysiumNpcDog::PreSelectIdealStateRetail()
{
	return DogPreSelectIdealState();
}

// Slot 440: `0x10374370`.
int32 FElysiumNpcDog::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return DogTranslateSchedule(ScheduleNumber);
}

// Slot 516: `0x10374130`, which replaces the Troika ladder.
float FElysiumNpcDog::MaxYawSpeed()
{
	return MaxYawSpeedDog();
}

// Slot 566: `0x10374aa0`, a replacement that does not chain.
bool FElysiumNpcDog::FValidateHintType(void* Hint)
{
	return SpeciesFValidateHintType(TEXT("CNPC_VDog"), Hint);
}

// Slot 546: `0x103736d0`, the class's own schedule id space.
const TCHAR* FElysiumNpcDog::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VDog"), SlotEn);
}
