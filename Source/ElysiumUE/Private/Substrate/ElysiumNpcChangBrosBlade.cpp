#include "Substrate/ElysiumNpcChangBrosBlade.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Slot 420: `0x1036f100`.
// `0x1036f100`
void FElysiumNpcChangBrosBlade::NPCInit()
{
	FUN_1036c7f0(0);                                                     // BEFORE the chain
	ChangBrosNPCInit();
}

// Slot 546: `0x1036ecf0`, the class's own schedule id space.
const TCHAR* FElysiumNpcChangBrosBlade::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093a8d8`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VChangBrosBlade"), TEXT("0x1036ecf0"), TEXT("0x1093a8d8") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

