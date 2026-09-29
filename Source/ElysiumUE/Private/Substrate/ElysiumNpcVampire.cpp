#include "Substrate/ElysiumNpcVampire.h"

#include "Substrate/ElysiumNpcManBat.h"

#include "Substrate/ElysiumNpcHengeyokai.h"

#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumReactions.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// The release bodies' aim offset above the target's origin.
	constexpr float ThrowAimHeight = ElysiumNpcTunables::FortyEight;        // _DAT_10447ee8
}

// --- Moved from `ElysiumNpcDamage.cpp` (story 5 step 4) ---

bool FElysiumNpcVampire::DamageFlinchSuppressed(uint32 CombinedBits, float Magnitude, uint32 SuppressMask)
{
	// The two bodies, verbatim:
	//     bits = dmg ? (dmg->m_bdmgTypes | info[0xe]) : info[0xe];
	//     if ((bits & 0x4000002) != 0) return;                      // no flinch at all
	//     mag = dmg ? dmg->GetDmg() : (float)info[0xc];
	//     if (mag != 0.0f) CBaseCombatCharacter::DamageFlinch(...);
	// `0x4000002` is `DMG_BULLET | DMG_BUCKSHOT`, which is exactly `ElysiumDamage::FirearmMask`:
	// a Gargoyle and a Hengeyokai do not flinch from gunfire. The magnitude test is an EXACT
	// inequality against 0.0, not a threshold.
	if ((CombinedBits & SuppressMask) != 0)
	{
		return true;
	}
	return Magnitude == NpcKernelDamageShared::DamageZero;
}

// --- From `FElysiumNpcHengeyokai` (the move manifest's corrected owner) ---

bool FElysiumNpcVampire::IsCarriedBreakable(const FElysiumEntity* Carried) const
{
	// SEAM for `thunk_FUN_101578b0(ent)`.
	(void)Carried;
	return false;
}

void FElysiumNpcVampire::SetCarriedBreakable(const FElysiumEntityHandle& Carried, bool bBreakable)
{
	// SEAM for `thunk_FUN_101578d0(ent, b)`.
	(void)Carried;
	(void)bBreakable;
}

FElysiumEntityHandle FElysiumNpcVampire::BeginPickupLink(const FPickupSpecies& Row, FElysiumEntity* Carried,
	int32& OutBone)
{
	// `0x10382670` / `0x1038f430`, the shared head:
	//
	//     if (param_1 == NULL) return false;
	//     link = CreateNoSpawn("phys_animlink", vec3_origin);   if (!link) return false;
	//     bone = <scan the model's bone table for the species' carrier bone>;
	//     if (bone < 0) return false;
	//     if (param_1->m_pPhysicsObject (+0x36c) == 0) return false;
	//
	// The `bone < 0` guard is retail's and is reproduced exactly: its scan leaves the loop counter
	// at the bone COUNT when nothing matched, so a miss is a POSITIVE index and the guard does not
	// fire. Only an empty bone table (count 0, counter 0) reaches it, and even that is not negative
	// — which is why this port answers on the SEAM's `INDEX_NONE` instead and says so. The physics
	// object test has no port object, so it is part of the element seam (`FinishPickupLink`).
	OutBone = INDEX_NONE;
	if (Carried == nullptr)
	{
		return FElysiumEntityHandle::Invalid();
	}
	const FElysiumEntityHandle Link = CreatePhysAnimlink();
	if (World == nullptr || World->Resolve(Link) == nullptr)
	{
		return FElysiumEntityHandle::Invalid();
	}
	OutBone = LookupBoneByName(Row.CarrierBone);
	if (OutBone < 0)
	{
		return FElysiumEntityHandle::Invalid();
	}
	return Link;
}

bool FElysiumNpcVampire::FinishPickupLink(const FElysiumEntityHandle& Link, int32 Bone, FElysiumEntity* Carried,
	int32 Key)
{
	//     element = carried->GetElement(key);   if (!element) return false;    // +0x424
	//     LinkAnimlink(link, this, bone, element, vec3_origin, vec3_origin);    // 0x1014f210
	if (Carried == nullptr || !RagdollElementForBone(Carried, Key))
	{
		return false;
	}
	WirePhysAnimlink(Link, Bone, Key);
	return true;
}

bool FElysiumNpcVampire::ReleasePickupLink(FElysiumEntityHandle& AnimlinkWord,
	const FElysiumEntityHandle& PickupTarget, const FElysiumEntity* Aim)
{
	// `0x10382400` / `0x1038f790`, the shared head:
	//
	//     if (m_hPhysicsAnimlink resolves) UTIL_Remove(it);     // 0x101cd970
	//     m_hPhysicsAnimlink = -1;
	//     if (aim) {
	//         if (m_hPickupTarget resolves) {
	//             impulse = vec3_origin;
	//             to = aim->GetAbsOrigin(); to.z += 48.0;        // slot 217, _DAT_10447ee8
	//             SolveThrow(&impulse, carried->GetAbsOrigin(), &to);         // 0x102c4cc0
	//             rag = dynamic_cast<CRagdollProp*>(carried);
	//             if (rag)  rag->ApplyImpulse(impulse, vec3_origin);          // +0x428
	//             else      carried->m_pPhysicsObject->ApplyForceCenter(...); // +0x9c
	//         }
	//     }
	if (World != nullptr && World->Resolve(AnimlinkWord) != nullptr)
	{
		RemovePhysAnimlink(AnimlinkWord);
	}
	AnimlinkWord = FElysiumEntityHandle::Invalid();
	if (Aim == nullptr || World == nullptr)
	{
		return false;
	}
	const FElysiumEntity* Carried = World->Resolve(PickupTarget);
	if (Carried == nullptr)
	{
		return false;
	}
	FVector Impulse = FVector::ZeroVector;
	FVector ToUnits = Aim->Origin / ElysiumMove::U;
	ToUnits.Z += ThrowAimHeight;
	SolveThrowImpulse(Carried->Origin / ElysiumMove::U, ToUnits, Impulse);
	ApplyThrowImpulse(PickupTarget, Impulse);
	return true;
}
