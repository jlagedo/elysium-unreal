// `CAI_BaseNPC`'s bodies of the `SpeciesMisc10_2` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSpeciesMisc10_2.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcSpeciesMisc10_2Shared.h"

// --- Moved from `ElysiumNpcSpeciesMisc10_2.cpp` (story 5 step 5) ---

void FElysiumNpcBase::Slot332(FElysiumEntity* SlowTarget)
{
	// `CAI_BaseNPC#332` / `CAI_BaseNPCTroika#332` (`0x1014f890`) — the whole Troika-line body is
	// `return;`. The ONE species override is `CNPC_VTzimisceHeadClaw`'s `0x103c1d80`, on its C++
	// class (story 5 step 3).
	(void)SlowTarget;
}
