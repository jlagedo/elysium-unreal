#include "Substrate/ElysiumNpcCameraSecurity.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"

const FElysiumNpcClass* FElysiumNpcCameraSecurity::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 201: `0x10369ff0`. It REPLACES the Troika body `0x102b4630` outright: a security-camera NPC
// never consults its own eyes, and only the candidate is read of the four arguments.
bool FElysiumNpcCameraSecurity::FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4)
{
	// `10369ff0`: resolve the linked `CSecCamera`, then answer 1 only when the candidate carries a
	// player record (`+0xa8`) AND the link is non-null AND `0x1020cd00` is true. Every other case is 0.
	FElysiumEntity* Camera = ResolveSecCameraLink();
	const bool bPlayer = SeenTarget != nullptr && World != nullptr
		&& SeenTarget->Handle == World->PlayerHandle();
	return bPlayer && Camera != nullptr && SecCameraCanSee(Camera, SeenTarget);
}

// Slot 468: `0x1036a030`, 18 bytes, and the WHOLE body is `return *(int*)(param_1 + 0xa8) != 0;`.
//
// `+0x00a8` is `CBaseEntity::m_pPlayer` (`npc-kernel/layout.md`), the self-downcast cache the
// `CBasePlayer` constructor fills — so the test is "the candidate is the player", and the security
// camera's sense admission is exactly that and nothing else. It does NOT chain the base Troika
// `QuerySeeEntity` (`0x102b38b0`); 29c's walk says it adds a gate "on top of" the base, and there
// is no call in the 18 bytes. Its own base `CNPC_VCamera` fills slot 468 with nothing of its own,
// so the security camera is the only class in the family whose sight is player-only.
bool FElysiumNpcCameraSecurity::QuerySeeEntity(FElysiumEntity* Candidate)
{
	// `FElysiumEntity` carries no self-downcast cache, so "is the player" is spelled the way every
	// other ported body in this runtime spells it: the world's one player handle.
	return Candidate != nullptr && World != nullptr && Candidate->Handle == World->PlayerHandle();
}

// --- Moved from `ElysiumNpcDialogueBodies.cpp` (story 5 step 4) ---

FElysiumEntity* FElysiumNpcCameraSecurity::ResolveSecCameraLink()
{
	// 0x10369e70 — 252 bytes, unnamed in retail; the name is inferred from the RTTI cast it performs
	// and from the census field names (`m_iszLinkedCamera` +0x6660, `m_hLinkedCamera` +0x6664). Its
	// three callers are all `CNPC_VCameraSecurity`'s: slot 201 `FVisible` (`0x10369ff0`), slot 363
	// `FInViewCone` (`0x10369fb0`) and `vfunc201`'s thunk — a security-camera NPC sees through the
	// `CSecCamera` it is linked to, not through its own eyes.
	//
	// Three blocks, each re-reading `+0x6664` from scratch:
	//
	//  1. If the cached handle does NOT resolve to a live entity:
	//         name = m_iszLinkedCamera (+0x6660), or "" when null
	//         ent  = FindEntityByName(gEntList, NULL, name, this, this)   // 0x100f7770
	//         cam  = dynamic_cast<CSecCamera*>(ent)                        // ___RTDynamicCast
	//         m_hLinkedCamera = cam ? cam->GetRefEHandle() : -1;
	//     The cast is what makes the lookup type-safe: an entity of the right NAME but the wrong
	//     class clears the handle rather than caching it.
	//  2. Re-test the handle:
	//         live  -> m_bLinkedCameraBound (+0x6668) = 1
	//         dead  -> if (m_bLinkedCameraBound) UTIL_Remove(this)          // 0x101cd940
	//     So the latch is a one-way door: a camera NPC that HAS been linked and then loses its
	//     camera removes ITSELF. A camera NPC that never linked simply keeps answering null.
	//  3. Return the resolved handle, or 0.
	//
	// **SEAM**: `CSecCamera` has no port class, so the RTTI cast has nothing to test (the
	// `npc_VCameraSecurity` classname stands its class since story 5 step 2). The name lookup goes through the world's own name index — the same set
	// retail's `FindEntityByName` walks — and the class filter is stated as unrecovered rather than
	// silently dropped.
	if (World == nullptr)
	{
		return nullptr;
	}

	// Block 1.
	if (World->Resolve(LinkedCamera) == nullptr)
	{
		FElysiumEntity* Found = !LinkedCameraName.IsEmpty()
			? World->FindByName(*LinkedCameraName) : nullptr;
		// **Unrecovered**: `dynamic_cast<CSecCamera*>`. This runtime stands no `CSecCamera` leaf, so
		// the cast cannot be applied and a name hit of ANY class is accepted — the one place this
		// body is more permissive than retail. Named here so a later story that lands `CSecCamera`
		// knows exactly where the filter goes.
		LinkedCamera = Found != nullptr ? Found->Handle : FElysiumEntityHandle::Invalid();
	}

	// Block 2.
	FElysiumEntity* Camera = World->Resolve(LinkedCamera);
	if (Camera != nullptr)
	{
		bLinkedCameraBound = true;
	}
	else if (bLinkedCameraBound)
	{
		Kill();   // `UTIL_Remove(this)` `0x101cd940`
	}

	// Block 3 — retail resolves `+0x6664` a THIRD time here; after block 2 nothing can have changed
	// it, so the cached pointer is returned.
	return Camera;
}

// --- Moved from `ElysiumNpcSenses10_2.cpp` (story 5 step 4) ---

bool FElysiumNpcCameraSecurity::CameraSecurityFInViewCone(FElysiumEntity* Candidate)
{
	// `10369fb0`: slot 201's shape with the camera's CONE test in place of its full sight test.
	FElysiumEntity* Camera = ResolveSecCameraLink();
	const bool bPlayer = Candidate != nullptr && World != nullptr
		&& Candidate->Handle == World->PlayerHandle();
	return bPlayer && Camera != nullptr && SecCameraInViewCone(Camera, Candidate);
}
