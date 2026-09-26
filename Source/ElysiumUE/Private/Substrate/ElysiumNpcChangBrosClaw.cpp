#include "Substrate/ElysiumNpcChangBrosClaw.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumWeaponClasses.h"

const FElysiumNpcClass* FElysiumNpcChangBrosClaw::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 420: `0x1036f900`.
// `0x1036f900`
void FElysiumNpcChangBrosClaw::NPCInit()
{
	FUN_1036c7f0(1);
	ChangBrosNPCInit();
}

// Slot 546: `0x1036f4f0`, the class's own schedule id space.
const TCHAR* FElysiumNpcChangBrosClaw::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093aa10`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VChangBrosClaw"), TEXT("0x1036f4f0"), TEXT("0x1093aa10") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcKernelLifecycle19_2.cpp` (story 5 step 4) ---

