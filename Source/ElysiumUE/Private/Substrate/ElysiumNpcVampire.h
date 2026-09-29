#pragma once

#include "Substrate/ElysiumNpcHuman.h"

// `CNPC_VVampire` (primary vtable `0x104cdd24`), built by `npc_VVampire` factory `0x103c4870`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcVampire : public FElysiumNpcHuman
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VVampire", FElysiumNpcHuman)


	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcDamage.inl`.
	/** The slot-292 gate `CNPC_VGargoyle` (`0x10378cb0`) and `CNPC_VHengeyokai` (`0x103802a0`) share
	 *  byte for byte, so it can be measured without a world.
	 *  `Magnitude` is `CVDmg_t::GetDmg()` when the packet carries a descriptor and `m_flDamage` when it
	 *  does not — retail's own two-armed read. */
	static bool DamageFlinchSuppressed(uint32 CombinedDamageBits, float Magnitude, uint32 SuppressMask);

	// From `FElysiumNpcHengeyokai` (the move manifest's corrected owner).
	/** SEAM for `thunk_FUN_101578b0(ent)` / `thunk_FUN_101578d0(ent, b)` / `thunk_FUN_10157890(ent, b)`
	 *  — the carried entity's "is it breakable" read and the two breakable/ragdoll latches the attach
	 *  and release bodies set around a carry. The read answers false. */
	bool IsCarriedBreakable(const FElysiumEntity* Carried) const;
	void SetCarriedBreakable(const FElysiumEntityHandle& Carried, bool bBreakable);
	/** The shared half of `0x10382670` (`CNPC_VHengeyokai`) and `0x1038f430` (`CNPC_VManBat`), one
	 *  body retail writes twice: `if (param_1 == NULL) return false;` then
	 *  `CreateNoSpawn("phys_animlink")` and the carrier-bone scan. Answers the created link (invalid on
	 *  any refusal) and the bone. Each class's own `AttachPickupAnimlink` runs it with its own row,
	 *  then its own arm, then `FinishPickupLink`. */
	FElysiumEntityHandle BeginPickupLink(const FPickupSpecies& Row, FElysiumEntity* Carried, int32& OutBone);
	/** The shared tail of both attach bodies: the carried thing's element at `Key` (`+0x424`), then
	 *  `LinkAnimlink(link, this, bone, element, …)` (`0x1014f210`). False when there is no element. */
	bool FinishPickupLink(const FElysiumEntityHandle& Link, int32 Bone, FElysiumEntity* Carried, int32 Key);
	/** The shared half of the release bodies `0x10382400` (`CNPC_VHengeyokai`) and `0x1038f790`
	 *  (`CNPC_VManBat`): remove the link and clear its word, then, with an aim and a live carried
	 *  thing, solve the throw onto a point 48 units above the aim's origin and apply it. True when the
	 *  throw was applied. The collision re-arm, the carried-word clear and the flag write are each
	 *  class's own, in its own order. */
	bool ReleasePickupLink(FElysiumEntityHandle& AnimlinkWord, const FElysiumEntityHandle& PickupTarget,
		const FElysiumEntity* Aim);

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
};
