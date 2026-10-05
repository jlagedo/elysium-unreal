#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class AActor;
class FElysiumEntityWorld;
class UWorld;

// The two engine-shaped questions `CNPCMaker` asks the world, answered against Unreal geometry for
// `AElysiumMapActor` (the substrate reaches them through `IElysiumEmbodiment`). Free functions so the
// automation suite can drive them against a bare test world.
namespace ElysiumNpcMakerGeometry
{
	/** The ground cache of `CNPCMaker::MakeNPC` `0x1034b7b0` (`1034b7ba`..`1034b8a3`): a ray from
	 *  `GetAbsOrigin()` straight down `_DAT_1046bacc` = 2048 units, `CTraceFilterSimple(this, 0)`,
	 *  mask `0x2400b` (`MASK_NPCSOLID_BRUSHONLY`: SOLID | WINDOW | GRATE | MOVEABLE | MONSTERCLIP),
	 *  and `m_flGround = tr.endpos.z` (`1034b881 MOV EAX,[ESP+0x3c]`, the trace's `endpos.z`). A miss
	 *  answers the ray's end. `TraceDepthCm` is the 2048 units in centimetres.
	 *
	 *  **Named modernization — the start-on-surface semantics.** A Source ray whose start lies on (or
	 *  inside) a floor surface hits at fraction 0, so a maker placed exactly on the floor caches its
	 *  own Z. An Unreal line trace ignores the surface it starts on, so it would fall through to the
	 *  ray's end. The ray therefore starts `ElysiumMove::DistEpsilon` above the origin and the answer
	 *  is clamped to the origin's Z — the same `endpos` retail reports; a start inside the solid
	 *  (`bStartPenetrating`, retail's `startsolid`) answers the origin's Z too. `Ignore` is the player
	 *  pawn. The mask is the NPC-solid question, so the ray runs on `GroundChannel` (`ECC_Pawn`, the
	 *  channel the contents-signature world profiles block for NPC-solid brushes; the `ElysiumUse`
	 *  channel the first cut used is ignored by every `ElysiumSig_*` world profile, which is why the
	 *  shipped floors were missed) and skips character bodies (`CONTENTS_MONSTER` is not in
	 *  `0x2400b`). */
	float ResolveGroundZ(const UWorld* World, const FVector& MakerOriginCm, float TraceDepthCm,
		const AActor* Ignore);

	/** `UTIL_EntitiesInBox(list, 2, mins, maxs, 0x2080)` (`0x101cca80`): the entities whose
	 *  collision bounds meet the box and whose `m_fFlags` carry `FL_CLIENT` (`0x80`) or `FL_NPC`
	 *  (`0x2000`); the box is occupied when the count is non-zero (`1034b733 TEST EAX,EAX / SETZ`).
	 *  `CNPCMaker::CanMakeNPC` `0x1034b580` builds it at `1034b686`..`1034b72b`: x/y `origin ±
	 *  _DAT_1049ffac` (34 units) and BOTH z at the maker's own `origin.z` — a flat square. The
	 *  fleshpile's `0x1034c2d0` lowers `mins.z` to `m_flGround` unless `m_bNoDrop`. So the box is
	 *  `[Centre.xy ± HalfExtentCm] x [FloorZCm, Centre.z]`.
	 *
	 *  The flag filter is what the port reproduces: the player (its pawn's bounds) and every entity
	 *  whose `Flags` carry `FL_NPC` — set only by `CAI_BaseNPC::NPCInit` `0x10273390` (`102733b6 PUSH
	 *  0x12000`), so a maker, a director or a prop never counts — measured by the standing hull at
	 *  its feet (the collision AABB, not the render bounds). A hidden entity is out of the partition
	 *  walk. */
	bool IsSpawnAreaOccupied(const FElysiumEntityWorld* EntityWorld, const AActor* PlayerPawn,
		const FVector& CentreCm, float HalfExtentCm, float FloorZCm);

    FString DescribeSpawnArea(const FElysiumEntityWorld* EntityWorld, const AActor* PlayerPawn,
        const FVector& CentreCm, float HalfExtentCm, float FloorZCm);

	/** The channel `0x2400b` (`MASK_NPCSOLID_BRUSHONLY`) maps to: whether a brush blocks an NPC. */
	inline constexpr ECollisionChannel GroundChannel = ECC_Pawn;

	/** `FL_CLIENT | FL_NPC`, the `0x2080` flag mask `CanMakeNPC` passes (`1034b715 PUSH 0x2080`). */
	inline constexpr int32 SpawnAreaFlagMask = 0x2080;
	/** `FL_NPC`, the half of that mask an entity (not the player) can carry. */
	inline constexpr int32 FlagNpc = 0x2000;
}
