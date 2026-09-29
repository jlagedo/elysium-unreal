// `CAI_BaseNPC`'s bodies of the `Dialogue` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseDialogue.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// --- Moved from `ElysiumNpcDialogueBodies.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::NavigatorPathType() const
{
	// `0x102ee620` -- `GetGoalType()`, `path+0x5c` on `m_pNavigator` (`+0x5d34`): the navigator's
	// current goal TYPE (the SDK's path type). `8` is the pedestrian/crosswalk type both crosswalk
	// bodies gate on. No goal: 0 -- retail's value, where the port's old word defaulted to -1; so
	// `BaseSelect`'s `NavigatorPathType() == 0` (`0x1028a44b`) now opens for a body with no goal.
	return Navigator.GetGoalType();
}

