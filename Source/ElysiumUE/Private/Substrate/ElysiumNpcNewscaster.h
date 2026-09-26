#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VNewscaster` (primary vtable `0x104bf634`), built by `npc_VNewscaster` factory
// `0x1039fef0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcNewscaster : public FElysiumNpc
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual void UpdateOnRemove() override;
	virtual int32 IRelationType(FElysiumEntity* Candidate) override;
	virtual int32 DrawDebugTextOverlays() override;
};
