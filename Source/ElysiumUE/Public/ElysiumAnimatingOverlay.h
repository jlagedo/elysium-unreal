#pragma once

#include "CoreMinimal.h"
#include "ElysiumAnimating.h"
// By value: the gesture-layer table is sized by the overlay stack's slot count (`ElysiumOverlay::NumSlots`).
#include "ElysiumAnimationIntent.h"

// FElysiumAnimatingOverlay — CBaseAnimatingOverlay, the retail node between the animating class and
// `CBaseFlex` (0019 story 5 step 6). It owns virtuals and bodies of its own: the gesture layers and
// the flinch records. Its datamap names no external, so it carries no keyfield.

class FElysiumAnimatingOverlay : public FElysiumAnimating
{
public:
	// The generated slot surface and the hand-written slot bodies of this class's retail node
	// (0019 story 5 step 6).
	#include "ElysiumAnimatingOverlaySlots.inl"
	#include "ElysiumAnimatingOverlaySlotBodies.inl"
};
