#pragma once

#include "CoreMinimal.h"
#include "ElysiumGymSpec.h"

#if !UE_BUILD_SHIPPING

class AActor;
class UWorld;

// The gym's engine half (CCC0): it turns `ElysiumGym::FSpec`'s values into collision and makes no
// decisions of its own. Every choice about *where* a rung sits belongs to the spec, which is
// asserted with no world; what is left here is spawning.
namespace ElysiumGym
{
	// Stand the gym up at `Origin`. Returns the actor that owns every solid, so a caller can tear
	// the whole thing down by destroying one thing.
	//
	// **Solids are `UBoxComponent`s, not meshes.** A box shape exists the moment the component
	// registers — no `UBodySetup`, no cook, and no dependency on a mesh asset being loadable, which
	// matters because the harness runs under `-nullrhi`. A ramp also needs an exactly rotated box,
	// where a scaled cube would arrive via a convex round-trip.
	//
	// `bWithMeshes` hangs a decorative, collision-free cube under each box for a human to look at.
	// The box stays the authority either way, so what the green room shows and what the headless
	// run walks on cannot drift apart.
	AActor* Spawn(UWorld* World, const FSpec& Spec, const FVector& Origin, bool bWithMeshes = false);
}

#endif // !UE_BUILD_SHIPPING
