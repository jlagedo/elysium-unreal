#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcSensesBodies.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelSensesShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"

namespace NpcKernelSensesShared
{
	// `_DAT_104454c4` = 0.0f, the image's shared zero: `EffectiveVisionDistanceCm`'s floor and the
	// `!= 0.0` test on the prone-dialog ray's squared length.
	inline constexpr float GSharedZero = 0.0f;
	// `CSecureType`'s scramble, verbatim from `0x1042fde0` / `0x1028ea60` and its reader pair
	// `0x1042fe90` / `0x103a2e30`. Two different XOR immediates on the two sides (`0x0ae8746f`
	// writing, `0x0ce9f66a` reading) is not a transcription slip — the listing has both.
	inline constexpr uint32 GSecureHashXor = 0x7e92476fu;
	inline constexpr uint32 GSecureHashMaskA = 0xa0086435u;
	inline constexpr uint32 GSecureHashXorA = 0x4814ade7u;
	inline constexpr uint32 GSecureHashAddA = 0x8c4b7d1fu;
	inline constexpr uint32 GSecureHashXorB = 0x16066412u;
	inline constexpr uint32 GSecureHashMaskB = 0x5ff79bcau;
	inline constexpr uint32 GSecureStoreMask = 0x068d8635u;
	inline constexpr uint32 GSecureStoreXorRead = 0x0ce9f66au;
	inline constexpr uint32 GSecureStoreAdd = 0x0ffa91d8u;
	inline constexpr uint32 GSecureStoreMask2 = 0x197279cau;
	inline constexpr uint32 GSecureStoreXorTail = 0xa641cacdu;
}
