#pragma once

#include "Substrate/ElysiumNpcHumanCombatant.h"

// `CNPC_VSabbatGunman` (primary vtable `0x104c311c`), built by `npc_VSabbatGunman` factory
// `0x103a4ff0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcSabbatGunman : public FElysiumNpcHumanCombatant
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VSabbatGunman", FElysiumNpcHumanCombatant)

	virtual void OnChangeActivity(int32 Activity) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	static FMotionTrailPick SabbatGunmanMotionTrail(float GroundSpeed, float SpeedThreshold,
		int32 TrailId, float TrailScalar);

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual int32 StartTaskSlot442(void* Arg0) override;
};
