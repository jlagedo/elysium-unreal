#pragma once

#include "Substrate/ElysiumNpcHuman.h"

// `CNPC_VTaxiDriver` (primary vtable `0x104c8a14`), built by `npc_VTaxiDriver` factory
// `0x103b2ed0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcTaxiDriver : public FElysiumNpcHuman
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VTaxiDriver");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcLifecycle19.inl`.
	bool bTaxiFirstThink = false;
};
