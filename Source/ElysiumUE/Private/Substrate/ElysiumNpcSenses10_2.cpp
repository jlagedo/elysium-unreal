#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29d, family **Senses10** — the SPECIES line: the `SpeciesSenses10` rows plus the species
// arms of `FVisible`, `FInViewCone` and `GetShootEnemyDir` that the dispatchers in
// `ElysiumNpcSenses10.cpp` route to (`BestEnemy`'s one species arm is `FElysiumNpcFrenzyShadow`'s,
// fold A2).
//
// Every body is retail's, arm by arm, with the `0x10……` address of the arm beside it.

// =================================================================================================
// `CNPC_VCameraSecurity` — slot 201 `0x10369ff0` and slot 363 `0x10369fb0`.
// =================================================================================================

bool FElysiumNpc::SecCameraCanSee(const FElysiumEntity* Camera, const FElysiumEntity* SeenTarget) const
{
	// SEAM for `CSecCamera::CanSee` (`0x1020cd00`): the enabled byte `+0x7d8`, then `0x1020cd30`'s
	// 2-D distance against the far radius `+0x794` and the near radius `+0x790`, then the cone test
	// `0x1020cc60`, then a `0x4091` trace whose fraction must equal `_DAT_10449280` (**1.0**).
	// The camera entity on this substrate carries none of those five words, so the ENABLED byte
	// reads clear — retail's switched-off camera, which is the refusing arm and the one that leaves
	// a security NPC blind rather than omniscient.
	(void)Camera;
	(void)SeenTarget;
	return false;
}

bool FElysiumNpc::SecCameraInViewCone(const FElysiumEntity* Camera,
	const FElysiumEntity* SeenTarget) const
{
	// SEAM for `CSecCamera::InViewCone` (`0x1020cc60`) alone: re-check the argument's `+0xa8`, take
	// the camera's forward from its angles (camera slot `+0x374` through `0x10139610`), the
	// normalised direction from the camera's `GetAbsOrigin` to the argument's `WorldSpaceCenter`
	// (`0x101d1120`), and require the dot STRICTLY greater than the camera's cosine `+0x798`. The
	// cosine word does not exist here; answers false, as above.
	(void)Camera;
	(void)SeenTarget;
	return false;
}

// =================================================================================================
// `CNPC_VScurrying` — `0x103acac0` and `0x103acba0`.
// =================================================================================================

bool FElysiumNpc::IsAreaClear(const FVector& /*PositionCm*/, int32 /*Mask*/) const
{
	// SEAM for `CAI_BaseNPCTroika::IsAreaClear(pos, 0x202400b, 0, 0)`. This runtime has no hull
	// sweep; answers true, which ADMITS the jittered point — retail's own answer for open ground.
	return true;
}
