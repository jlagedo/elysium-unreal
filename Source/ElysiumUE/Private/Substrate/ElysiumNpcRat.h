#pragma once

#include "Substrate/ElysiumNpcScurrying.h"

// `CNPC_VRat` (primary vtable `0x104c558c`), built by `npc_VRat` factory `0x103ad710`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcRat : public FElysiumNpcScurrying
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual bool ShouldIgnoreCollision(FElysiumEntity* Other) override;
	virtual FVector HeadDirection2D() override;
	virtual FVector HeadDirection3D() override;
	virtual void* CreateLocalNavigator() override;
};
