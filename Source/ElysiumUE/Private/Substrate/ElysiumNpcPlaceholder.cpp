#include "Substrate/ElysiumNpcPlaceholder.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Slot 420: `0x103a4350`.
// `0x103a4350`
void FElysiumNpcPlaceholder::NPCInit()
{
	// `103a435x`: `m_pInterestingPlace = NULL`, before the base. This runtime spells "no place" as
	// `INDEX_NONE` (`FElysiumNpc::CurrentSpotIndex`), because 0 is a valid entity index here.
	CurrentSpotIndex = INDEX_NONE;                                       // +0x62ec, BEFORE the base
	TroikaNPCInit();
	bIsBccTargetable = true;
	ThinkSet(nullptr, 0.0);
}

// Slot 437: `0x103a43f0`.
// Slot 437: `0x103a43f0`, the body of its class's `PreSelectSchedule` override (story 5 step 3).
int32 FElysiumNpcPlaceholder::PreSelectSchedule()
{
	// CNPC_VPlaceholder: `field_0x1b2c = 0x1e; return 0x157;`
	RecordScheduleEvent(
		TEXT("PreSelectSchedule trace 0x1e (CNPC_VPlaceholder 0x103a43f0) -> 0x157"));
	return 0x157;
}

// Slot 438: `0x103a4410`, which replaces the whole selector (the Troika selector's species hook).
// Slot 438: `0x103a4410`, the body of its class's `SpeciesSelectSchedule` override (story 5 step 3).
int32 FElysiumNpcPlaceholder::SpeciesSelectSchedule()
{
	// CNPC_VPlaceholder: `field_0x1b2c = 0x1e; return 0x157;`
	RecordScheduleEvent(TEXT("SelectSchedule trace 0x1e (CNPC_VPlaceholder 0x103a4410) -> 0x157"));
	return 0x157;
}

// Slot 546: `0x103a3c50`, the class's own schedule id space.
const TCHAR* FElysiumNpcPlaceholder::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093c08c`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VPlaceholder"), TEXT("0x103a3c50"), TEXT("0x1093c08c") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}
