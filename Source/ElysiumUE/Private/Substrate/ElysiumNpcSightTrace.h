#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

class FElysiumEntityWorld;
class IElysiumEmbodiment;

// `CBaseEntity::FVisible 0x100a6fa0` (Troika slot 201 `0x102b4630` passes through unchanged), the
// trace half: the ray from the looker's eye to the probed point and its verdict, over
// `IElysiumEmbodiment::TraceRetail`. The target-flag and water gates before the ray are the caller's
// (`FElysiumNpc::BaseEntityFVisible`); the probe point (`FVisibleTargetOrigin 0x100a72e0`) is
// `VisibleTargetOrigin` below.
//
// Two filters, one query. `bNpcsBlock` picks which (default false = `FVisible`'s):
//   - false: `CTraceFilterFVisible` (below). NPC-transparent entities are skipped.
//   - true: the lateral pre-check's `CTraceFilterSimpleTwoEnt(this, ignore, group 0)` (`0x101ccd70`,
//     `ShouldHitEntity 0x101ccda0`; R2 section 5): no `StandardFilterRules`, no BCC / hidden gate and NO
//     NPC-transparent gate, so third-party NPCs block. The two entities it ignores are `Looker` and
//     `SecondIgnore`.
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
		// `CTraceFilterSimpleTwoEnt`'s second pass entity (the lateral pre-check's `ignore`); unset for
		// `FVisible`, whose filter has one.
		FElysiumEntityHandle SecondIgnore;
		// Resolves a listed character's handle to an entity so the filter can read its
		// `m_bNPCTransparent` (`+0xfc`, `FElysiumEntity::bNpcTransparent`: `NPCInit` sets it on every
		// NPC, `0x10273394`). Null answers "nothing to decide from": no listed character then blocks.
		const FElysiumEntityWorld* World = nullptr;
		// false: `CTraceFilterFVisible` (NPCs never block). true: `CTraceFilterSimpleTwoEnt` (they do).
		bool bNpcsBlock = false;
	};

	// `FVisibleTargetOrigin 0x100a72e0(target, probe)`, jump table `0x100a78c4` -- the ONE place the
	// probe table lives; `FElysiumNpc::BaseEntityFVisible` (the `CBaseEntity::FVisible` body) is its one
	// caller. `Probe` is `m_eEnemyOccludedCheck +0x5b98` at the enemy call site.
	//   0, 10 and above 10: the target's own eye (`TargetEyeCm`, slot 193), `100a7890`;
	//   1: the OBB centre `(mins + maxs) / 2 + origin`, `100a7360`;
	//   2..9: the eight OBB corners, x/y from mins/maxs and z pulled toward the centre to
	//         `cz + 0.9 * (corner.z - cz)` (`0x10450a9c`); 2..5 the top four, 6..9 the bottom four,
	//         each in the order (min x, min y), (min x, max y), (max x, min y), (max x, max y).
	// The box is in the same frame as the origin: centimetres, mins / maxs relative to it.
	FVector VisibleTargetOrigin(int32 Probe, const FVector& TargetEyeCm, const FVector& OriginCm,
		const FVector& MinsCm, const FVector& MaxsCm);

	// True = visible. On false, `*OutBlocker` (when non-null) is what stopped the ray: the entity, or
	// Invalid for the static world (and Invalid on true).
	//
	// Order as built: (1) `TraceRetail` for the line, `Ignore = {Looker, SecondIgnore}`; a false answer
	// (headless) is `QueryLineOfSight`'s brush-only verdict. (2) The world hit (`Fraction < 1`) is one
	// candidate; the other is the nearest KEPT character -- `Characters` is walked nearest first, an
	// unresolved handle is dropped, and unless `bNpcsBlock` so is every entity with `bNpcTransparent`,
	// which leaves the player. (3) Neither -> visible; the nearer one is `Target` -> visible (a hit on
	// the target is clear, `100a71ab`); else the blocker is that one, not visible. A tie goes to the
	// world.
	bool Visible(const IElysiumEmbodiment& Embodiment, const FVisibleQuery& Query,
		FElysiumEntityHandle* OutBlocker);
}
