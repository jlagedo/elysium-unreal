#include "Substrate/ElysiumNpcZombie.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcZombie::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VZombie"));
	return Row;
}

// Slots 25 / 26: `0x103e12c0` / `0x103e12f0`, the `m_OnAttackedVictim` fire with no base forward.
void FElysiumNpcZombie::Slot25(FElysiumEntity* Victim)
{
	FUN_103e12c0(Victim);
}

void FElysiumNpcZombie::Slot26(FElysiumEntity* Victim)
{
	FUN_103e12f0(Victim);
}

// Slot 510: `0x103e1080`, which tails directly into the CAI_BaseNPC body `0x1027a530`.
bool FElysiumNpcZombie::ShouldPlayFloatSound()
{
	return FUN_103e1080();
}

// Slot 420: `0x103defc0`.
void FElysiumNpcZombie::NPCInit()
{
	ZombieNPCInit();
}

// Slot 104: `0x103df120`.
void FElysiumNpcZombie::Precache()
{
	ZombiePrecache();
}

// Slot 105: `0x103e0540`, the same vocalization-group body as the ghoul croucher's.
void FElysiumNpcZombie::SetModel(TCHAR* ModelName)
{
	ZombieLineSetModel(ModelName, TEXT("0x103e0540"));
}

// Slot 461: `0x103df5f0`, chaining the animal line's `0x1035fe80` directly.
int32 FElysiumNpcZombie::SelectIdealStateRetail()
{
	return ZombieSelectIdealState();
}

// Slot 201: `0x103e0bc0`
bool FElysiumNpcZombie::FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4)
{
	return ZombieFVisible(SeenTarget, Mask, Blocker, Arg4);
}

// Slot 440: `0x103df580`.
int32 FElysiumNpcZombie::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return ZombieTranslateSchedule(ScheduleNumber);
}

// Slot 124: `0x103e0e80`
int32 FElysiumNpcZombie::DrawDebugTextOverlays()
{
	return ZombieDrawDebugTextOverlays();
}

// Slot 24: `0x103e1280`, the Troika body `0x1029f8d0` directly FIRST, then the output.
void FElysiumNpcZombie::OnVictimHitByMe(FElysiumEntity* Victim)
{
	ZombieOnVictimHitByMe(Victim);
}

// Slot 566: `0x103e03b0`, a replacement that does not chain.
bool FElysiumNpcZombie::FValidateHintType(void* Hint)
{
	return SpeciesFValidateHintType(TEXT("CNPC_VZombie"), Hint);
}

// Slot 546: `0x103de4d0`, the class's own schedule id space.
const TCHAR* FElysiumNpcZombie::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VZombie"), SlotEn);
}

// Slot 509: `0x103e0fa0`, a replacement that does not chain.
bool FElysiumNpcZombie::ShouldPlayIdleSound()
{
	return ShouldPlayIdleSoundZombie();
}

// Slot 141: `0x103e0430`, a prologue ahead of a direct call into `CAI_BaseNPC::TraceAttack` (`0x10266780`).
void FElysiumNpcZombie::TraceAttack(void* InInfo, const FVector& DirUnits, void* InTrace)
{
	ZombieTraceAttack(InInfo, DirUnits, InTrace);
}
