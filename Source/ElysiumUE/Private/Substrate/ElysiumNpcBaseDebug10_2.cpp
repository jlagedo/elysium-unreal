// `CAI_BaseNPC`'s bodies of the `Debug10_2` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseDebug10_2.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcDebug10_2Shared.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"

// --- Moved from `ElysiumNpcDebug10_2.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::HintOverlayWords(int32& OutHintType, float& OutHintYawDegrees,
	float& OutNodeYawDegrees, FVector& OutOriginUnits) const
{
	// SEAM for `m_pHintNode`'s four overlay words: `m_nHintType` (`+0x5dc`), the yaw at `+0x454`,
	// `0x102d12e0`'s own yaw and the node's `GetAbsOrigin()`. Family Hints records that hints are
	// node INDICES in this runtime with no type or angle store behind them.
	OutHintType = INDEX_NONE;
	OutHintYawDegrees = 0.f;
	OutNodeYawDegrees = 0.f;
	OutOriginUnits = FVector::ZeroVector;
	return false;
}

FVector FElysiumNpcBase::RetailStandoffAnchorUnits(const FVector& EyeUnits) const
{
	// SEAM for `0x10278650(out, in, 0.0, 0.0)` — family **Motor10**'s row in this same band
	// (`FElysiumNpc::ComputeStandoffAnchorOffset`). Answers the position handed in until the two are
	// wired, so the ±2 box lands on the ±3 box and the joining line has zero length, which is what
	// retail draws when the offset is zero.
	return EyeUnits;
}
