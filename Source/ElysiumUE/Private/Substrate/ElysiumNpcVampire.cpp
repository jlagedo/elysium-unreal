#include "Substrate/ElysiumNpcVampire.h"

#include "Substrate/ElysiumNpcManBat.h"

#include "Substrate/ElysiumNpcHengeyokai.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
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
	constexpr float ThrowAimHeight = 48.0f;        // _DAT_10447ee8
	// `0x102c43b0`'s collision-ignore re-arm, per species.
	constexpr float HengeyokaiIgnoreSeconds = 0.75f;
	constexpr float ManBatIgnoreSeconds = 2.0f;
}

const FElysiumNpcClass* FElysiumNpcVampire::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 546: `0x103c4a80`, the class's own schedule id space.
const TCHAR* FElysiumNpcVampire::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093d2a4`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VVampire"), TEXT("0x103c4a80"), TEXT("0x1093d2a4") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
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

const FElysiumNpc::FPickupSpecies* FElysiumNpcVampire::PickupSpeciesOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	int32 Count = 0;
	const FPickupSpecies* Rows = PickupSpeciesRows(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		if (FCString::Strcmp(Rows[i].RetailClass, InRetailClass) == 0)
		{
			return &Rows[i];
		}
	}
	// The two bodies are non-virtual helpers each species calls directly, so the row is the class's
	// own: neither boss has a subclass in the census (story 5 step 3 dropped the base-chain walk).
	return nullptr;
}

const FElysiumNpc::FPickupSpecies* FElysiumNpcVampire::PickupSpecies() const
{
	const FElysiumNpcClass* Cls = RetailClass();
	return PickupSpeciesOf(Cls != nullptr ? Cls->Name : nullptr);
}

bool FElysiumNpcVampire::AttachPickupAnimlink(FElysiumEntity* Carried, int32 ElementKey)
{
	return AttachPickupAnimlinkFor(PickupSpecies(), Carried, ElementKey);
}

void FElysiumNpcVampire::ReleasePickupAnimlink(const FElysiumEntity* AimTarget)
{
	ReleasePickupAnimlinkFor(PickupSpecies(), AimTarget);
}

bool FElysiumNpcVampire::AttachPickupAnimlinkFor(const FPickupSpecies* Row, FElysiumEntity* Carried,
	int32 ElementKey)
{
	// `0x10382670` (Hengeyokai) and `0x1038f430` (ManBat), one body written twice:
	//
	//     if (param_1 == NULL) return false;
	//     link = CreateNoSpawn("phys_animlink", vec3_origin);   if (!link) return false;
	//     bone = <scan the model's bone table for the species' carrier bone>;
	//     if (bone < 0) return false;
	//     if (param_1->m_pPhysicsObject (+0x36c) == 0) return false;
	//     --- Hengeyokai only ---
	//       rag = dynamic_cast<CRagdollProp*>(param_1);
	//       if (rag) { rag->SetHeld(true);                     // 0x10157890
	//                  key = Decode(m_SecurePickupParam);       // 0x10430130 over +0x66a0
	//                  rag->SetDamage((float)key);              // +0x2fc
	//                  element = rag->GetElement(key);          // +0x424
	//                  if (!element) return false; }
	//     --- ManBat only ---
	//       element = param_1->GetElement(param_2);             // +0x424, no cast, no decode
	//       if (!element) return false;
	//     LinkAnimlink(link, this, bone, element, vec3_origin, vec3_origin);   // 0x1014f210
	//     m_hPhysicsAnimlink = link->GetRefEHandle();
	//     --- Hengeyokai only ---  FINDING_BODY off, then FormBit(true): CARRYING_BODY on,
	//                              m_flFishTimer = curtime + RandomFloat(5, 8), m_bDidFakeThrow = 0
	//     --- ManBat only ---      CARRYING_BODY on (0x1038f600),
	//                              m_bPickupTargetBreakable = IsBreakable(param_1),
	//                              SetBreakable(param_1, false)
	//     return true;
	//
	// The `bone < 0` guard is retail's and is reproduced exactly: its scan leaves the loop counter
	// at the bone COUNT when nothing matched, so a miss is a POSITIVE index and the guard does not
	// fire. Only an empty bone table (count 0, counter 0) reaches it, and even that is not negative
	// — which is why this port answers on the SEAM's `INDEX_NONE` instead and says so.
	if (Row == nullptr)
	{
		// Neither boss: no body fills this for the class, so there is nothing to run.
		return false;
	}
	// Each class runs its own copy over its own words: the row names the class, and an NPC that is
	// not of it has none of those words, so the row has nothing to run on it.
	const bool bHengeyokai = FCString::Strcmp(Row->RetailClass, TEXT("CNPC_VHengeyokai")) == 0;
	FElysiumNpcHengeyokai* const Hengeyokai = AsSpecies<FElysiumNpcHengeyokai>();
	FElysiumNpcManBat* const ManBat = AsSpecies<FElysiumNpcManBat>();
	if (bHengeyokai ? Hengeyokai == nullptr : ManBat == nullptr)
	{
		return false;
	}
	if (Carried == nullptr)
	{
		return false;
	}
	const FElysiumEntityHandle Link = CreatePhysAnimlink();
	if (World == nullptr || World->Resolve(Link) == nullptr)
	{
		return false;
	}
	const int32 Bone = LookupBoneByName(Row->CarrierBone);
	if (Bone < 0)
	{
		return false;
	}
	// `param_1->m_pPhysicsObject != 0` — no physics object on a port entity, so this is part of the
	// element seam and is folded into it.
	const int32 Key = bHengeyokai ? Hengeyokai->HengeyokaiPickupParam : ElementKey;
	if (bHengeyokai)
	{
		Hengeyokai->SetCarriedRagdollHeld(Carried->Handle, true);
	}
	if (!RagdollElementForBone(Carried, Key))
	{
		return false;
	}
	WirePhysAnimlink(Link, Bone, Key);
	if (bHengeyokai)
	{
		Hengeyokai->HengeyokaiPhysicsAnimlink = Link;
		NpcFlags.Clear(EElysiumNpcFlag::FINDING_BODY);   // 0x10381ba0(this, false)
		CallFormBit(true);                               // 0x10381c00(this, true)
	}
	else
	{
		ManBat->ManBatPhysicsAnimlink = Link;
		NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);    // 0x1038f600(this, true)
		ManBat->bManBatPickupTargetBreakable = IsCarriedBreakable(Carried);
		SetCarriedBreakable(Carried->Handle, false);
	}
	return true;
}

void FElysiumNpcVampire::ReleasePickupAnimlinkFor(const FPickupSpecies* Row, const FElysiumEntity* AimTarget)
{
	// `0x10382400` (Hengeyokai) and `0x1038f790` (ManBat), one body written twice:
	//
	//     if (m_hPhysicsAnimlink resolves) UTIL_Remove(it);     // 0x101cd970
	//     m_hPhysicsAnimlink = -1;
	//     aim = <Hengeyokai: param_1 | ManBat: m_hClosestPlayer (+0x628c)>;
	//     if (aim) {
	//         if (m_hPickupTarget resolves) {
	//             impulse = vec3_origin;
	//             to = aim->GetAbsOrigin(); to.z += 48.0;        // slot 217, _DAT_10447ee8
	//             SolveThrow(&impulse, carried->GetAbsOrigin(), &to);         // 0x102c4cc0
	//             rag = dynamic_cast<CRagdollProp*>(carried);
	//             if (rag)  { rag->ApplyImpulse(impulse, vec3_origin);        // +0x428
	//                         --- ManBat only --- SetBreakable(rag, m_bPickupTargetBreakable); }
	//             else      { carried->m_pPhysicsObject->GetPosition(...);    // +0xa0
	//                         carried->m_pPhysicsObject->ApplyForceCenter(...); }   // +0x9c
	//         }
	//     }
	//     --- ManBat only --- StartIgnoringCollision(carried);   // 0x102c4380, expire = FLT_MAX
	//     ArmIgnoreCollisionExpiry(<0.75 | 2.0>);                 // 0x102c43b0
	//     m_hPickupTarget = -1;
	//     <Hengeyokai: FormBit(false) | ManBat: CARRYING_BODY off>;
	//
	// TWO ORDERING FACTS, both reproduced: the Hengeyokai arm clears `m_hPickupTarget` AFTER the
	// throw and BEFORE the collision re-arm, and the ManBat arm clears it AFTER the re-arm — which
	// is why its `StartIgnoringCollision` still resolves the carried entity and the Hengeyokai arm
	// never calls it at all.
	if (Row == nullptr)
	{
		return;
	}
	// Each class runs its own copy over its own words (see `AttachPickupAnimlinkFor`).
	const bool bHengeyokai = FCString::Strcmp(Row->RetailClass, TEXT("CNPC_VHengeyokai")) == 0;
	FElysiumNpcHengeyokai* const Hengeyokai = AsSpecies<FElysiumNpcHengeyokai>();
	FElysiumNpcManBat* const ManBat = AsSpecies<FElysiumNpcManBat>();
	if (bHengeyokai ? Hengeyokai == nullptr : ManBat == nullptr)
	{
		return;
	}
	FElysiumEntityHandle& AnimLink = bHengeyokai ? Hengeyokai->HengeyokaiPhysicsAnimlink : ManBat->ManBatPhysicsAnimlink;
	FElysiumEntityHandle& PickupTargetWord =
		bHengeyokai ? Hengeyokai->HengeyokaiPickupTarget : ManBat->ManBatPickupTarget;

	if (World != nullptr && World->Resolve(AnimLink) != nullptr)
	{
		RemovePhysAnimlink(AnimLink);
	}
	AnimLink = FElysiumEntityHandle::Invalid();

	const FElysiumEntity* Aim = AimTarget;
	if (!bHengeyokai)
	{
		// `0x1038f790` ignores any argument and aims at the cached closest player.
		Aim = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	}
	if (Aim != nullptr && World != nullptr)
	{
		const FElysiumEntity* Carried = World->Resolve(PickupTargetWord);
		if (Carried != nullptr)
		{
			FVector Impulse = FVector::ZeroVector;
			FVector ToUnits = Aim->Origin / ElysiumMove::U;
			ToUnits.Z += ThrowAimHeight;
			SolveThrowImpulse(Carried->Origin / ElysiumMove::U, ToUnits, Impulse);
			ApplyThrowImpulse(PickupTargetWord, Impulse);
			if (Row->bRestoresBreakable)
			{
				SetCarriedBreakable(PickupTargetWord, ManBat->bManBatPickupTargetBreakable);
			}
		}
	}
	if (!bHengeyokai)
	{
		StartIgnoringCollision(PickupTargetWord);
		ArmIgnoreCollisionExpiry(Row->IgnoreCollisionSeconds);
		PickupTargetWord = FElysiumEntityHandle::Invalid();
		NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);   // 0x1038f600(this, false)
		return;
	}
	PickupTargetWord = FElysiumEntityHandle::Invalid();
	ArmIgnoreCollisionExpiry(Row->IgnoreCollisionSeconds);
	CallFormBit(false);                                   // 0x10381c00(this, false)
}

// --- From `FElysiumNpcHengeyokai` (the move manifest's corrected owner) ---

const FElysiumNpc::FPickupSpecies* FElysiumNpcVampire::PickupSpeciesRows(int32& OutCount)
{
	static constexpr FPickupSpecies Rows[] =
	{
		{ TEXT("CNPC_VHengeyokai"), TEXT("0x10382670"), TEXT("0x10382400"),
			TEXT("Bip01 R Hand"), 0x6664, HengeyokaiIgnoreSeconds, false },
		{ TEXT("CNPC_VManBat"), TEXT("0x1038f430"), TEXT("0x1038f790"),
			TEXT("Bip01_R_Foot"), 0x668c, ManBatIgnoreSeconds, true },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}
