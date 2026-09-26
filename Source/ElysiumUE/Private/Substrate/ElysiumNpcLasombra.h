#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VLasombra` (primary vtable `0x104ba34c`), built by `npc_VLasombra` factory `0x10388d30`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcLasombra : public FElysiumNpcVampire
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VLasombra");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual bool CanSeekCover() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcKernelMisc.inl`.
	float LasombraCoverDisableOverride = 0.f;  // +0x6664 CNPC_VLasombra::m_flCoverDisableOverride
};
