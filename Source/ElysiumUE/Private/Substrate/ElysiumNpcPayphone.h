#pragma once

#include "Substrate/ElysiumNpc.h"

// `CPayphone` (primary vtable `0x10479814`), built by `npc_payphone` factory `0x101aa550`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcPayphone : public FElysiumNpc
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual FVector HeadDirection2D() override;
	virtual FVector HeadDirection3D() override;
	virtual FVector EyePosition() const override;
	virtual bool CanTalk(FElysiumEntity* Activator) override;
	virtual void Think() override;
};
