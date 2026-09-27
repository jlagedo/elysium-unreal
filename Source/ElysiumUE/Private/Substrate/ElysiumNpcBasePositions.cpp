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
	// `GetEnemies()` (slot 541) then `thunk_FUN_102dfed0(memory, &out, pEnemy)`. This runtime's
	// `FElysiumNpcEnemyMemory` is the same store, so the fact is carried; the record's position is
	// what retail's helper copies out.
	const FElysiumEntity* Enemy =
		World != nullptr ? World->Resolve(BaseMemory.Enemy) : nullptr;
	if (Enemy == nullptr)
	{
		return false;
	}
	OutPositionCm = Enemy->Origin;
	return true;
}
