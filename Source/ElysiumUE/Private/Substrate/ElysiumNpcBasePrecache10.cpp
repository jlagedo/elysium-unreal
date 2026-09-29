// `CAI_BaseNPC`'s bodies of the `Precache10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBasePrecache10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 5) ---

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 5) ---

void FElysiumNpcBase::IssuePrecache(const FPrecacheOp& Op)
{
	// The record IS the recovered half; the acquisition is the seam. See the `.inl`.
	PrecacheLog.Add(Op);
}
