#pragma once

#include "CoreMinimal.h"
#include "ElysiumAnimatingOverlay.h"

// FElysiumFlex — CBaseFlex, the retail node between `CBaseAnimatingOverlay` and
// `CBaseCombatCharacter` (0019 story 5 step 6). It owns virtuals and bodies of its own: the flex
// weights and the scene-event list. Its datamap names no external, so it carries no keyfield.

class FElysiumFlex : public FElysiumAnimatingOverlay
{
public:
	// The generated slot surface and the hand-written slot bodies of this class's retail node
	// (0019 story 5 step 6).
	#include "ElysiumFlexSlots.inl"
	#include "ElysiumFlexSlotBodies.inl"
};
