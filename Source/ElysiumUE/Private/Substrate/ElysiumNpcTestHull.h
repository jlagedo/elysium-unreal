#pragma once

#include "Substrate/ElysiumNpcBase.h"

// `CAI_TestHull` (primary vtable `0x1049be5c`), a direct `CAI_BaseNPC` subclass (story 5 fold A1).
//
// No factory builds it by classname (`docs/vtmb/npc-ai/population.md`): retail builds one singleton
// by code, and only graph-build code drives it — `CAI_Node::InitLinks 0x102fb4e0` and the hull
// bumper `0x102f7a90` probe links with it (`docs/vtmb/navigation-jump-links.md`). No NPC path touches
// it. This port builds no node graph at runtime (the pipeline bakes navigation, spec 0018/3), so **no
// port path constructs a test hull**; tests construct the C++ type directly. Of its six own bodies
// only `Spawn` (slot 103, `0x102d72f0`) stays, because test fixtures stand the class as a base-only
// NPC through it. `ObjectCaps` (117), `IsJumpLegal` (521), `StepHeight` (522) and
// `GetStepDownHeight` (523) are the bake's hull probe, which the
// pipeline models (step 40); slot 5 (`0x102d77d0`) is the C++ destructor.
class FElysiumNpcTestHull : public FElysiumNpcBase
{
public:
	ELYSIUM_NPC_CLASS("CAI_TestHull", FElysiumNpcBase)

	// Slot 103 `0x102d72f0`.
	virtual void Spawn() override;

	/** `CAI_TestHull::Spawn`'s hull pick, `0x102d72f5`–`0x102d732e`, as a PURE function so every arm is
	 *  reachable from a test without a hull table. `HullBits` is `NAI_Hull::Bits`.
	 *
	 *  Retail, from the listing: read the used mask; **`TEST mask,mask; JLE`** — a SIGNED test, so a
	 *  zero OR NEGATIVE mask short-circuits straight to hull 0 WITHOUT the fallback call; otherwise
	 *  walk `i = 0 .. 21` and take the first `i` whose `NAI_Hull::Bits(i)` intersects the mask; after
	 *  22 misses call `AddUsedHullBits(0)` — which ORs zero and is a no-op — and answer 0. */
	static int32 PickHull(int32 UsedHullBits, TFunctionRef<int32(int32)> HullBits, bool& bOutTookFallback);

	/** SEAM for `this->+0x5f44 = 0` (a BYTE store, `102d7449`). `sizeof(CAI_BaseNPC)` is `0x5f44`:
	 *  four sibling subclasses start their own fields there (`CAI_BaseNPCTroika::m_OnDialogBegin`,
	 *  `CCineNPC::m_iszPreIdle`, `CNPC_Bullseye::m_hPainPartner`, `CScriptedTarget::m_vLastPosition`),
	 *  and the base's last word is `+0x5f40` (`MaintainSchedule 0x102817c0` reads and writes it). So
	 *  `+0x5f44` is `CAI_TestHull`'s own first byte. No datamap names it and the only typed
	 *  `CAI_TestHull` access at `+0x5f44` is this store in `0x102d72f0`, so **what the byte is stays
	 *  unrecovered**. It is carried under its offset and read by the test alone. */
	bool bUnknown5f44 = false;

	virtual float PlayActivity(const FString& Activity);
};
