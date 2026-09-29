#include "Substrate/ElysiumNpcChangBrosBlade.h"

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

// Slot 420: `0x1036f100`.
// `0x1036f100`
void FElysiumNpcChangBrosBlade::NPCInit()
{
	FUN_1036c7f0(0);                                                     // BEFORE the chain
	ChangBrosNPCInit();
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

