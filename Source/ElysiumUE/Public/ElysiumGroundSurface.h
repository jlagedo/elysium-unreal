#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;
struct FHitResult;

/**
 * The surface under a body's feet, as both locomotion producers publish it
 * (`FElysiumLocomotionSample::GroundSurface`, `docs/architecture/footstep-architecture.md` §4.1).
 *
 * Retail asks this once, in the ground trace it was already running: `CGameMovement::
 * CategorizePosition` caches the hit's `surfacedata_t` on the mover, and `CAI_Navigator::MoveEnact`
 * `0x102ef870` caches it on the NPC at `+0x5b90`. In Source the collision brush and the drawn
 * brush are the same object, so one trace answers both "is there a floor" and "what is it made of".
 *
 * **This port split them, and that is why the rules below exist.** A converted map's solid body is
 * the `.hulls` convex collider — `BlockAll`, invisible and material-less — while the `$surfaceprop`
 * rides the drawn geometry, which wears `ElysiumPickOnly` and blocks nothing but
 * `ElysiumImpactDecals::SurfaceTraceChannel` (`ElysiumDecalSubsystem.h` states the same split for
 * the ranged shot's stain, and this uses that channel for the same reason). So a floor query on the
 * pawn channel finds the floor and no material, and the material has to be asked for separately.
 * A baked solid prop (`ElysiumPropSolid`) blocks both and carries its material on either.
 *
 * The two rules the sample's field documents are implemented here and nowhere else:
 * `NAME_None` = no standable floor; a floor with no `UElysiumPhysicalMaterial` = `default`,
 * Source's surfaceprop index 0.
 */
namespace ElysiumGroundSurface
{
	/** Source's surfaceprop index 0 — the name a floor with no `UElysiumPhysicalMaterial` takes. */
	FName DefaultSurface();

	/**
	 * Rule 1 over one hit, and nothing else: `NAME_None` when the hit did not block, the material's
	 * `SourceName` when it is a `UElysiumPhysicalMaterial`, `default` otherwise.
	 */
	FName SurfaceNameFor(const FHitResult& Hit);

	/**
	 * One down line-trace on the render-surface channel, `DepthCm` from `StartCm`, ignoring
	 * `IgnoreActor`. `NAME_None` when there is no world, no depth, or nothing drawn under the
	 * segment — which is *not* the same as "no floor": nothing on this channel means nothing this
	 * runtime can name a surface off, and the caller decides what a floor with no drawn half is.
	 */
	FName TraceBelow(const UWorld* World, const FVector& StartCm, float DepthCm,
		const AActor* IgnoreActor);

	/**
	 * The whole sense for a producer that already has its floor hit: the hit's own material when it
	 * carries one, else a short probe across the contact point on the render-surface channel, else
	 * `default` — because the hit proved a floor and a floor is never `NAME_None`. `NAME_None` only
	 * when the hit did not block at all.
	 */
	FName AtFloorHit(const UWorld* World, const FHitResult& FloorHit, const AActor* IgnoreActor);
}
