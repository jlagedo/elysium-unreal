#include "Substrate/ElysiumNpcRat.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"

// Slot 68: `0x103ad6d0`. A fixed global entity is ignored; otherwise a direct call into the Troika
// body `0x1029afc0`.
bool FElysiumNpcRat::ShouldIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr && Other == RatIgnoredGlobalEntity())
	{
		return true;
	}
	return FElysiumNpc::ShouldIgnoreCollision(Other);
}

// Slot 370: `0x103ad7f0`, a tail call through slot 368 — the head aim IS the body direction. Slot 368
// (`FElysiumCombatCharacter::BodyDirection2D`) has no override on any port class, so the direct call
// resolves exactly as retail's virtual one does.
FVector FElysiumNpcRat::HeadDirection2D()
{
	return BodyDirection2D();
}

// Slot 371: `0x103ad820`, a tail call through slot 369.
FVector FElysiumNpcRat::HeadDirection3D()
{
	return BodyDirection3D();
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 4) ---

FElysiumEntity* FElysiumNpcRat::RatIgnoredGlobalEntity() const
{
	// `thunk_FUN_101cda50()` — a no-argument global read. **SEAM**, and the entity's retail
	// identity is **unrecovered**.
	return nullptr;
}
