// The ground-surface sense both locomotion producers publish through. The header carries the
// design (and the retail addresses); this file is its mechanics.

#include "ElysiumGroundSurface.h"

#include "ElysiumDecalSubsystem.h"   // ElysiumImpactDecals::SurfaceTraceChannel — the drawn half
#include "ElysiumPhysicalMaterial.h"

#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"

namespace
{
	// How far either side of a floor contact point the render-surface probe reaches, cm. One Source
	// unit up and two down: enough to straddle the gap between a convex hull's face and the drawn
	// face it stands for (the bake places them on the same plane, and the collision solver's own
	// contact offset is what the up-span is for), and short enough that it cannot reach past the
	// floor into whatever is under it.
	constexpr float ProbeUpCm = 2.54f;
	constexpr float ProbeDownCm = 5.08f;
}

FName ElysiumGroundSurface::DefaultSurface()
{
	// `surfaceproperties.txt`'s own entry name for index 0, which is what the bake names
	// `/ElysiumBaked/SurfaceProperties/PM_default` after.
	static const FName Name(TEXT("default"));
	return Name;
}

FName ElysiumGroundSurface::SurfaceNameFor(const FHitResult& Hit)
{
	if (!Hit.bBlockingHit)
	{
		return FName();   // retail's null `surfacedata_t`: nothing standable was found
	}
	const UElysiumPhysicalMaterial* Surface =
		Cast<UElysiumPhysicalMaterial>(Hit.PhysMaterial.Get());
	if (Surface == nullptr || Surface->SourceName.IsEmpty())
	{
		// The engine's default material, a collider nothing was bound to, a face whose VMT carried
		// no `$surfaceprop` — all one answer, and it is a REAL surface with real step sounds.
		return DefaultSurface();
	}
	return FName(*Surface->SourceName);
}

FName ElysiumGroundSurface::TraceBelow(const UWorld* World, const FVector& StartCm, float DepthCm,
	const AActor* IgnoreActor)
{
	if (World == nullptr || DepthCm <= 0.0f)
	{
		return FName();
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumGroundSurface), /*bTraceComplex*/ false,
		IgnoreActor);
	// The surface character is the whole question this trace exists to answer.
	Params.bReturnPhysicalMaterial = true;

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, StartCm, StartCm - FVector(0.0f, 0.0f, DepthCm),
		ElysiumImpactDecals::SurfaceTraceChannel, Params))
	{
		return FName();
	}
	return SurfaceNameFor(Hit);
}

FName ElysiumGroundSurface::AtFloorHit(const UWorld* World, const FHitResult& FloorHit,
	const AActor* IgnoreActor)
{
	if (!FloorHit.bBlockingHit)
	{
		return FName();
	}
	if (Cast<UElysiumPhysicalMaterial>(FloorHit.PhysMaterial.Get()) != nullptr)
	{
		// A baked prop, an authored floor, anything whose collision body carries the surface: the
		// query the caller already ran answered the whole question, exactly as retail's one trace
		// does.
		return SurfaceNameFor(FloorHit);
	}
	// The drawn half of the same floor. Short and local rather than a second full-length ground
	// trace: the contact point is already known, so this only has to cross the plane it sits on.
	const FName Rendered = TraceBelow(World,
		FloorHit.ImpactPoint + FVector(0.0f, 0.0f, ProbeUpCm), ProbeUpCm + ProbeDownCm, IgnoreActor);
	// Nothing drawn under a floor that is definitely there — a brush entity's hull, a lift, a test
	// slab. Index 0, not silence: the body IS standing on something.
	return Rendered.IsNone() ? DefaultSurface() : Rendered;
}
