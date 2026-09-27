#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcDamage2Shared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29c-1, family **Damage**, second half — the per-species death, throw and emitter bodies.
// The Troika-line slot bodies, the family seams and its four standing facts are
// `Substrate/ElysiumNpcDamage.cpp` and `Substrate/ElysiumNpcDamage.inl`; the walked
// prose for everything below is `docs/vtmb/npc-ai/lifecycle.md`.
//
// Split at ~1,500 lines, as family Bosses once split off its second half. The constants below
// are the ones this half uses, re-stated rather than shared because neither file owns the other's
// anonymous namespace; every one carries the `.rdata` address it was read from.

namespace
{
	constexpr float EnergyBallForward = 50.0f;      // _DAT_104ada24
	constexpr float EnergyBallRight = 40.0f;        // _DAT_104ada28
	constexpr float EnergyBallUp = -10.0f;          // _DAT_104ada2c

}

// =================================================================================================
// `0x1036dd20` — `CNPC_VChangBros::SpawnEnergyBall`.
// =================================================================================================

FVector FElysiumNpc::EnergyBallSpawnPoint(const FVector& OriginUnits, const FVector& FwdAxis,
	const FVector& RightAxis, const FVector& UpAxis)
{
	// The listing's three accumulations, each a dot of one basis row with the same constant triple:
	//     p.x = origin.x + fwd.x*50 + up.x*40 + right.x*(-10)
	//     p.y = origin.y + fwd.y*50 + up.y*40 + right.y*(-10)
	//     p.z = origin.z + fwd.z*50 + up.z*40 + right.z*(-10)
	// `_DAT_104ada24` = 50.0, `_DAT_104ada28` = 40.0, `_DAT_104ada2c` = -10.0, all read out of
	// `.rdata`. `AngleVectors` (`0x10139610`) fills forward/right/up from the muzzle attachment's
	// angles (slot 219, `+0x36c`).
	return OriginUnits + FwdAxis * EnergyBallForward + UpAxis * EnergyBallRight
		+ RightAxis * EnergyBallUp;
}
