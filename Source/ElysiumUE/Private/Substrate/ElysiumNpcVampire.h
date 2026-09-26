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
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VVampire");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcKernelDamage.inl`.
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
	static const FPickupSpecies* PickupSpeciesOf(const TCHAR* InRetailClass);
	/** This NPC's row, walking the census chain, or null when neither boss claims it. */
	const FPickupSpecies* PickupSpecies() const;
	/** `0x10382670` (`CNPC_VHengeyokai`, `"Bip01 R Hand"`) and `0x1038f430` (`CNPC_VManBat`,
	 *  `"Bip01_R_Foot"`) — ONE body written twice. Create a `phys_animlink`, find the carrier bone by
	 *  name, ask the carried thing for the element at the species' element key, wire the two together
	 *  and store the link at `m_hPhysicsAnimlink`. `Carried` is retail's `param_1`; `ElementKey` is its
	 *  `param_2`, which the ManBat arm passes straight through and the Hengeyokai arm ignores in favour
	 *  of the decoded `m_SecurePickupParam`. */
	bool AttachPickupAnimlink(FElysiumEntity* Carried, int32 ElementKey);
	/** The same body with the species row handed in rather than resolved, so a row can be exercised by
	 *  name. A null row is "no body fills this for my class" and answers false. */
	bool AttachPickupAnimlinkFor(const FPickupSpecies* Row, FElysiumEntity* Carried, int32 ElementKey);
	/** `0x10382400` (`CNPC_VHengeyokai`) and `0x1038f790` (`CNPC_VManBat`) — the release half, also one
	 *  body written twice: drop the link, solve an impulse from the carried thing onto a target point
	 *  48 units above the aim entity's origin, apply it, clear the carried handle and re-arm the
	 *  collision-ignore expiry. `AimTarget` is the Hengeyokai arm's `param_1`; the ManBat arm ignores it
	 *  and aims at `m_hClosestPlayer` (+0x628c) instead. */
	void ReleasePickupAnimlink(const FElysiumEntity* AimTarget);
	/** The row-explicit form, for the same reason as `AttachPickupAnimlinkFor`. */
	void ReleasePickupAnimlinkFor(const FPickupSpecies* Row, const FElysiumEntity* AimTarget);

	// From `FElysiumNpcHengeyokai` (the move manifest's corrected owner).
	static const FPickupSpecies* PickupSpeciesRows(int32& OutCount);
};
