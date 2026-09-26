#include "Substrate/ElysiumNpcTaxiDriver.h"

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

const FElysiumNpcClass* FElysiumNpcTaxiDriver::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 420: `0x103b35c0`.
// `0x103b35c0`
void FElysiumNpcTaxiDriver::NPCInit()
{
	TroikaNPCInit();
	bTaxiFirstThink = false;
	WriteIdealStateRetail(1);
	SetState(1);
	Senses.bCanPerformSenses = true;
}

// Slot 546: `0x103b3170`, the class's own schedule id space.
const TCHAR* FElysiumNpcTaxiDriver::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093c7f0`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VTaxiDriver"), TEXT("0x103b3170"), TEXT("0x1093c7f0") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcKernelLifecycle19_2.cpp` (story 5 step 4) ---

