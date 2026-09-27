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
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// --- Moved from `ElysiumNpcDialogueBodies.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::NavigatorPathType() const
{
	// **SEAM** for `0x102ee620`, which is `return path->+0x30` on `m_pNavigator` (`+0x5d34`) — the
	// navigator's current path TYPE. The shape map routes `+0x5d34` to
	// `FElysiumScriptedCharacter::Motor`, "the one motor seam this chain stands beside the body",
	// and that motor carries no path object at all.
	//
	// Answers `-1`, which is neither the crosswalk type `8` nor any other retail type, so both
	// bodies below take their own "not a pedestrian path" arm rather than being refused.
	return NavigatorPathTypeWord;
}

