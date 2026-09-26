#include "Substrate/ElysiumNpcSabbatLeader.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcSabbatLeader::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VSabbatLeader"));
	return Row;
}

// Slot 420: `0x103a6d40`.
void FElysiumNpcSabbatLeader::NPCInit()
{
	SabbatLeaderNPCInit();
}

// Slot 104: `0x103a6ab0`.
void FElysiumNpcSabbatLeader::Precache()
{
	SabbatLeaderPrecache();
}

// Slot 461: `0x103a7450`.
int32 FElysiumNpcSabbatLeader::SelectIdealStateRetail()
{
	return SabbatLeaderSelectIdealState();
}

// Slot 604: `0x103aa060`, which replaces the Troika body wholesale; its argument is read by no arm.
int32 FElysiumNpcSabbatLeader::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	return SelectScheduleMeleeCombatSabbatLeader();
}

// Slot 448: `0x103a9400`. Its route-flip arm returns at once (`0x103a94bc` / `0x103a94db`);
// otherwise a direct call into the Troika body `0x1029adb0`.
void FElysiumNpcSabbatLeader::TaskFail(int32 Reason)
{
	if (SabbatLeaderTaskFail(Reason))
	{
		return;
	}
	FElysiumNpc::TaskFail(Reason);
}

// Slot 440: `0x103a7390`.
int32 FElysiumNpcSabbatLeader::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return SabbatLeaderTranslateSchedule(ScheduleNumber);
}

// Slot 24: `0x103ab4a0`, a replacement that never calls the Troika body.
void FElysiumNpcSabbatLeader::OnVictimHitByMe(FElysiumEntity* Victim)
{
	SabbatLeaderOnVictimHitByMe(Victim);
}

// Slot 590: `0x103ab400`, whose miss calls the Troika body `0x1029f940` directly.
bool FElysiumNpcSabbatLeader::OkToInterruptForMelee()
{
	return SabbatLeaderOkToInterruptForMelee();
}

// Slot 566: `0x103a9340`, a replacement that does not chain.
bool FElysiumNpcSabbatLeader::FValidateHintType(void* Hint)
{
	return SpeciesFValidateHintType(TEXT("CNPC_VSabbatLeader"), Hint);
}

// Slot 546: `0x103a5e50`, the class's own schedule id space.
const TCHAR* FElysiumNpcSabbatLeader::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VSabbatLeader"), SlotEn);
}

// Slot 620: `0x103aa5e0`, a virtual `CNPC_VSabbatLeader` introduces. Driven by the schedule tasks
// `TASK_VSABBATLEADER_PLAY_FOOTSTEP_SOUND` / `..._STOP_FOOTSTEP_SOUND`, which are unbuilt.
void FElysiumNpcSabbatLeader::FootstepSound()
{
	SpeciesVocalize(TEXT("CNPC_VSabbatLeader"), 620);
}

// Slot 621: `0x103aa7a0`, a virtual `CNPC_VSabbatLeader` introduces.
void FElysiumNpcSabbatLeader::AttackSound()
{
	SpeciesVocalize(TEXT("CNPC_VSabbatLeader"), 621);
}

// Slot 127: `0x103a6e80`, whose body is the `CNPC_VVampireBoss` restore (`0x103c5910`, family
// SaveRestore10's `VampireBossRestore`) — the census's mechanism row for this class.
int32 FElysiumNpcSabbatLeader::Restore(void* Archive)
{
	return VampireBossRestore(Archive);
}

// Slot 366: `0x103a76d0`, a scope-trace wrapper over a direct call into `CNPC_VAndreiBlood`'s
// `0x10385a70` (`return 0;`).
bool FElysiumNpcSabbatLeader::HandleInteraction(int32 Interaction, void* Data, FElysiumEntity* Other)
{
	return SabbatLeaderHandleInteraction(Interaction, Data, Other);
}
