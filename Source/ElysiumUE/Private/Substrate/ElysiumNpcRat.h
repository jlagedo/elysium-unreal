#pragma once

#include "Substrate/ElysiumNpcScurrying.h"

// `CNPC_VRat` (primary vtable `0x104c558c`), built by `npc_VRat` factory `0x103ad710`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcRat : public FElysiumNpcScurrying
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VRat", FElysiumNpcScurrying)

	virtual bool ShouldIgnoreCollision(FElysiumEntity* Other) override;
	virtual FVector HeadDirection2D() override;
	virtual FVector HeadDirection3D() override;
	virtual void* CreateLocalNavigator() override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcMotor.inl`.
	/** `thunk_FUN_101cda50()` — the fixed global entity `CNPC_VRat::ShouldIgnoreCollision` compares
	 *  against. **SEAM**, and its retail identity is **unrecovered**: the body takes no argument and
	 *  reads a global; answers null. */
	FElysiumEntity* RatIgnoredGlobalEntity() const;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
};
