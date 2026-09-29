#include "Substrate/ElysiumNpcTaxiDriver.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLifecycle2_2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumWeaponClasses.h"

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

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

