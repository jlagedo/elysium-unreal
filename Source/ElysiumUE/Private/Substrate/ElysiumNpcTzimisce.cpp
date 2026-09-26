#include "Substrate/ElysiumNpcTzimisce.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcTzimisce::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VTzimisce"));
	return Row;
}

// Slot 482: `0x103bd270`, the standalone copy with the SCRIPT-state tail.
int32 FElysiumNpcTzimisce::CanPlaySequence(bool bDisregardState, int32 InterruptLevel)
{
	return FUN_103bd270(bDisregardState, InterruptLevel);
}

// Slot 488: `0x103b92a0`, `SPI_DIES` at the script host (its slot-487 tail call is unported).
void FElysiumNpcTzimisce::DeathSound()
{
	FUN_103b92a0();
}

// Slot 593: `0x103b9180`, the Troika body `0x1029a070` first (a direct call), then five overwrites.
void FElysiumNpcTzimisce::Slot593()
{
	FUN_103b9180();
}

// Slot 420: `0x103b91d0`.
void FElysiumNpcTzimisce::NPCInit()
{
	TzimisceNPCInit();
}

// Slot 422: `0x103b9270`.
void FElysiumNpcTzimisce::StartNPC()
{
	TzimisceStartNPC();
}

// Slot 104: `0x103b8fa0`.
void FElysiumNpcTzimisce::Precache()
{
	TzimiscePrecache();
}

// Slot 375: `0x103bde40`, which calls the Troika body `0x10295590` directly.
int32 FElysiumNpcTzimisce::NPC_EarlyTranslateActivity(int32 Activity)
{
	return TzimisceNpcEarlyTranslateActivity(Activity);
}

// Slot 463: `0x103ba2c0`. On a real transition the new state picks a facial expression blended over
// 1.0 s; the direct call into the Troika body runs either way.
void FElysiumNpcTzimisce::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	if (OldState != NewState)
	{
		if (const TCHAR* Name = StateChangeExpressionName(NewState))
		{
			SetDefaultExpression(Name, 1.0f);
		}
	}
	OnStateChangeTroika(OldState, NewState);
}

// Slot 461: `0x103bd690`, chaining the Troika body directly.
int32 FElysiumNpcTzimisce::SelectIdealStateRetail()
{
	return TzimisceSelectIdealState();
}

// Slot 201: `0x103ba290`
bool FElysiumNpcTzimisce::FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4)
{
	return TzimisceFVisible(SeenTarget, Mask, Blocker, Arg4);
}

// Slot 418: `0x103b9120`, a species sentinel ahead of a direct call into the Troika body.
float FElysiumNpcTzimisce::ResolveTaskDistance(float Distance)
{
	return TzimisceResolveTaskDistance(Distance);
}

// Slot 448: `0x103ba350`, its own arm and then a direct call into the Troika body `0x1029adb0`.
void FElysiumNpcTzimisce::TaskFail(int32 Reason)
{
	TzimisceTaskFail(Reason);
	FElysiumNpc::TaskFail(Reason);
}

// Slot 440: `0x103bd390`.
int32 FElysiumNpcTzimisce::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return TzimisceTranslateSchedule(ScheduleNumber);
}

// Slot 516: `0x103ba020`, which replaces the Troika ladder.
float FElysiumNpcTzimisce::MaxYawSpeed()
{
	return MaxYawSpeedTzimisce();
}

// Slot 69: `0x103bfa00` (byte-identical across Hengeyokai, MingXiao and Tzimisce): the `0x16` derived-type
// gate, then a direct call into the Troika body `0x1029b180`.
bool FElysiumNpcTzimisce::NavIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr && (RetailDerivedType(*Other) & 0x16) != 0)
	{
		return true;
	}
	return FElysiumNpc::NavIgnoreCollision(Other);
}

// Slot 124: `0x103c08d0`
int32 FElysiumNpcTzimisce::DrawDebugTextOverlays()
{
	return TzimisceDrawDebugTextOverlays();
}

// Slot 337: `0x103b9160`.
int32 FElysiumNpcTzimisce::GetUsedHullBits()
{
	return SpeciesUsedHullBits(TEXT("0x103b9160"));
}

// Slot 566: `0x103ba780`, a replacement that does not chain.
bool FElysiumNpcTzimisce::FValidateHintType(void* Hint)
{
	return SpeciesFValidateHintType(TEXT("CNPC_VTzimisce"), Hint);
}

// Slot 546: `0x103b70f0`, the class's own schedule id space.
const TCHAR* FElysiumNpcTzimisce::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VTzimisce"), SlotEn);
}

// Slot 563: `0x103ba640`, the `GoalToleranceLead` shape; a replacement that does not chain.
void FElysiumNpcTzimisce::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	TranslateEnemyChasePositionShaped(EChaseTranslateShape::GoalToleranceLead, Enemy, ChasePositionCm, Tolerance,
		SecondTolerance);
}

// Slot 435: `0x103bf610`, the Troika body `0x102a0940` directly, then the class's own tail.
void FElysiumNpcTzimisce::OnScheduleChange(int32 NewSchedule)
{
	TzimisceOnScheduleChange(NewSchedule);
}

// Slot 487: `0x103b9f10`, a replacement that does not chain.
void FElysiumNpcTzimisce::JustMadeSound()
{
	TzimisceJustMadeSound();
}

// Slot 490: `0x103b9380`, gated on `FOkToMakeSound()`, a sentence group; it does not chain.
void FElysiumNpcTzimisce::IdleSound()
{
	SpeciesVocalize(TEXT("CNPC_VTzimisce"), 490);
}

// Slot 491: `0x103b9500`, gated on `FOkToMakeSound()`, a sentence group; it does not chain.
void FElysiumNpcTzimisce::PainSound()
{
	SpeciesVocalize(TEXT("CNPC_VTzimisce"), 491);
}
