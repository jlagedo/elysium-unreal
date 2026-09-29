#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

class IElysiumEmbodiment;

// `CBaseEntity::FVisible 0x100a6fa0` (Troika slot 201 `0x102b4630` passes through unchanged), the
// trace half: the ray from the looker's eye to the probed point and its verdict, over
// `IElysiumEmbodiment::TraceRetail`. The target-flag and water gates before the ray and the probe
// point (`FVisibleTargetOrigin 0x100a72e0`) are the caller's.
//
// The retail rule, as 0018 story 6 recovered it:
//   - LINE, mask as passed (`0x2804091` at every sight caller), filter `CTraceFilterFVisible`
//     `0x101075e0` = `CTraceFilterSimple(looker, group 0)` plus one gate;
//   - the LOOKER is ignored (the pass entity, and its owner / owned);
//   - EVERY NPC is transparent: `ShouldHitEntity 0x10107630` skips `m_bNPCTransparent (+0xfc)`, and
//     `CAI_BaseNPC::NPCInit` sets it on every NPC as its first act (`10273394`) -- other NPCs never
//     block;
//   - the PLAYER blocks (not NPC-transparent, BCC-targetable), and so do solid props (MONSTER's
//     scope, `StandardFilterRules 0x101d3080`), unless debris, `npc_transparent` or
//     render-transparent;
//   - verdict, in order (`100a71ab`): `fraction == 1.0` -> visible; `tr.m_pEnt == target` ->
//     visible (a hit on the target is clear); else `*blocker = tr.m_pEnt`, not visible.
namespace ElysiumNpcSight
{
	struct FVisibleQuery
	{
		// The looker's eye (slot 193) and the probed point on the target, world centimetres.
		FVector EyeCm = FVector::ZeroVector;
		FVector TargetCm = FVector::ZeroVector;
		// The retail mask, verbatim.
		int32 Mask = 0x2804091;
		FElysiumEntityHandle Looker;
		FElysiumEntityHandle Target;
	};

	// True = visible. On false, `*OutBlocker` (when non-null) is what stopped the ray: the entity, or
	// Invalid for the static world.
	bool Visible(const IElysiumEmbodiment& Embodiment, const FVisibleQuery& Query,
		FElysiumEntityHandle* OutBlocker);
}
