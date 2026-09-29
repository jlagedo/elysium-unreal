// `CAI_BaseNPC`'s bodies of the `Conditions10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseConditions10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::NavIsGoalSet() const
{
	// `0x102ee2e0` reads `path+0x10` `m_bPaused`, NOT the goal type (`0x102ee680` is `IsGoalSet`, see
	// `NavigatorIsGoalSet`); the name is the port's old misreading, kept because callers outside the
	// navigator's files spell it. Every caller cites `0x102ee2e0` / `0x102bf7e0` and so wants the
	// paused byte.
	return NavigatorIsPaused();
}

void FElysiumNpcBase::Slot532(int32 FailureBits)
{
	// `CAI_BaseNPC::vfunc532` (`0x1027e0f0`): the whole body is the two door words and `return 1`.
	(void)FailureBits;
	OpeningDoor = FElysiumEntityHandle::Invalid();                       // +0x5d24
	bOpeningDoorWait = false;                                            // +0x5d30
}
