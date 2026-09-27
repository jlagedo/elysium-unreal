#include "Substrate/ElysiumNpcProneDialog.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSensesBodiesShared.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// `_DAT_10449280` = 1.0 (a double) — the engine's CLEAR trace fraction.
	constexpr float GSensesTraceClearFraction = 1.0f;
}

// --- Moved from `ElysiumNpcSensesBodies.cpp` (story 5 step 4) ---

bool FElysiumNpcProneDialog::ProneDialogPassesFindEntityFovTrace(const FVector& FromCm, const FVector& ToCm,
	int32 Mask, bool& bOutRayIsValid) const
{
	// `0x103a4bb0`, `CNPC_ProneDialog#45`, 306 bytes, almost all of it filling an engine `Ray_t`:
	//
	//     delta = to - from
	//     ray.m_IsRay = (delta.LengthSquared() != 0.0)          (_DAT_104454c4, a byte at +0x?? )
	//     ray.m_IsSwept = 1; every other field zero
	//     enginetrace->TraceRay(&ray, mask, this, &tr)          ((*DAT_1070b254 + 8))
	//     return tr.m_pEnt == this || (tr.m_pEnt == NULL && tr.fraction == 1.0);
	//
	// So the answer is "the ray from `from` toward `to` reaches ME, or reaches nothing at all".
	// `_DAT_10449280` is 1.0 as a double and is the engine's CLEAR fraction.
	//
	// **SEAM**: this runtime's embodiment answers `QueryLineOfSight(from, to)` — clear or blocked —
	// and reports NO hit entity, so the `tr.m_pEnt == this` arm has no source and only the
	// clear-segment arm can answer true. Named: `IElysiumEmbodiment::QueryLineOfSight` is the
	// retail `TraceRay` this stands for, and `tr.m_pEnt` is the word it does not carry.
	const FVector Delta = ToCm - FromCm;
	bOutRayIsValid = Delta.SizeSquared() != NpcKernelSensesShared::GSharedZero;
	(void)Mask;
	const IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		// A headless world traces nothing, which is the CLEAR arm — `fraction == 1.0`, no entity.
		return GSensesTraceClearFraction == 1.0f;
	}
	return Embodiment->QueryLineOfSight(FromCm, ToCm);
}
