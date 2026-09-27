// `CAI_BaseNPC`'s bodies of the `Senses10_2` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSenses10_2.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"

// --- Moved from `ElysiumNpcSenses10_2.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::NearestNavigatorNode(const FVector& /*ThreatCm*/, float /*FleeDistanceUnits*/,
	float /*SearchLimitUnits*/, FVector& OutNodeCm) const
{
	// SEAM for `0x102edae0` over `0x103008f0` / `0x10300b50` / `0x102ee9c0` -- the navigator's node
	// search around the threat with the caller's flee distance and 30000.0. No AI network stands on
	// this substrate, so the search fails, which is the arm that still produces a destination (the
	// march).
	OutNodeCm = FVector::ZeroVector;
	return false;
}
