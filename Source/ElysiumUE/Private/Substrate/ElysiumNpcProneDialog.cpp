#include "Substrate/ElysiumNpcProneDialog.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSensesBodiesShared.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSightTrace.h"
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
	// `CNPC_ProneDialog#45`, 306 bytes, almost all of it filling an engine `Ray_t`:
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
	// The retail trace is `ElysiumNpcSight::Visible`'s shape and this is its caller: the caller's OWN
	// mask goes through verbatim, the target is THIS body (a hit on it is the `tr.m_pEnt == this` arm,
	// a hit on nothing is the `m_pEnt == NULL && fraction == 1.0` arm, anything else is blocked), and
	// no one is ignored. **Unrecovered:** the filter the listing hands `TraceRay` (`this` stands in the
	// decompile where a `CTraceFilterSimple` would be constructed), so nothing here makes an NPC
	// transparent: `bNpcsBlock` is set, the plain simple filter's answer, which has no transparency
	// gate (R2 section 6). The headless answer is the brush-only clear one.
	const FVector Delta = ToCm - FromCm;
	bOutRayIsValid = Delta.SizeSquared() != NpcKernelSensesShared::GSharedZero;
	const IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		// A headless world traces nothing, which is the CLEAR arm — `fraction == 1.0`, no entity.
		return GSensesTraceClearFraction == 1.0f;
	}
	ElysiumNpcSight::FVisibleQuery Query;
	Query.EyeCm = FromCm;
	Query.TargetCm = ToCm;
	Query.Mask = Mask;
	Query.Target = Handle;
	Query.World = World;
	Query.bNpcsBlock = true;
	return ElysiumNpcSight::Visible(*Embodiment, Query, nullptr);
}
