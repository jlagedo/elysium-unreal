// `CAI_BaseNPC`'s bodies of the `Positions` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBasePositions.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcPositionsShared.h"

// --- Moved from `ElysiumNpcPositions.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::EnemyLastKnownPosition(FVector& OutPositionCm) const
{
	// `GetEnemy()` (slot 167) then `GetEnemies()` (slot 541) and `CAI_Enemies::GetLastKnownPosition`
	// (`0x102dfed0`): the enemy's memory RECORD position (record `+0xc`), the last position-only
	// record's, or `vec3_origin` -- never the enemy's live origin, which this body answered before
	// story 8 (L07 integration). The false return is the callers' "no enemy" arm, not retail's.
	const FElysiumEntity* const Enemy = GetEnemy();
	if (Enemy == nullptr)
	{
		return false;
	}
	OutPositionCm = Conditions19LastKnownPosition(Enemy);                // 0x102dfed0
	return true;
}
