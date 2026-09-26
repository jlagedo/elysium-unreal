#pragma once

#include "Substrate/ElysiumNpcHumanCombatant.h"

// `CNPC_ProneDialog` (primary vtable `0x104c2554`), built by `npc_VMercurio` factory `0x103a4ab0`;
// `npc_VProneDialog` factory `0x103a4b30`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcProneDialog : public FElysiumNpcHumanCombatant
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_ProneDialog");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcSensesBodies.inl`.
	/** `0x103a4bb0`, `CNPC_ProneDialog#45` — the prone-dialog body. A ray from `FromCm` toward `ToCm`
	 *  with the caller's mask; the answer is TRUE only when the trace hit THIS NPC, or hit nothing at
	 *  all with `fraction == _DAT_10449280` (**1.0**). The `!= 0.0` squared-length byte retail packs
	 *  into the ray request is the engine's "this ray has a direction" flag and is carried as
	 *  `bOutRayIsValid` so the degenerate case is visible rather than silently equal. */
	bool ProneDialogPassesFindEntityFovTrace(const FVector& FromCm, const FVector& ToCm, int32 Mask,
		bool& bOutRayIsValid) const;
};
