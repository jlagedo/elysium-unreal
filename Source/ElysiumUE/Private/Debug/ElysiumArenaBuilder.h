#pragma once

#include "CoreMinimal.h"
#include "Debug/ElysiumArenaSpec.h"
#include "ElysiumEntityHandle.h"   // the anchors are runtime entities, held by handle

#if !UE_BUILD_SHIPPING

class AActor;
class FElysiumEntityWorld;
class UWorld;

// The arena's engine half: it turns `ElysiumArena::FSpec`'s values into collision, a navigable
// surface and `intersting_place` entities, and makes no decisions of its own. Every choice about
// *where* something sits belongs to the spec.
//
// **This is the piece the movement gym deliberately does not have.** `ElysiumGymBuilder` sets
// `SetCanEverAffectNavigation(false)` on every solid, because a bracket lane is walked by a command
// stream and wants no Recast tiles. An arena is the opposite: `AElysiumNpcBody` is an `ACharacter`
// driving `AAIController::MoveToLocation`, and `IElysiumNpcMotor::ProjectToNavigable` goes straight
// to `UNavigationSystemV1` — so without a built navmesh every `TASK_GET_PATH_TO_ENEMY` fails by
// name and the cast stands still. Standing the room up and building navigation over it are one
// operation here for that reason: a caller cannot get the geometry without the graph.
namespace ElysiumArena
{
	// `FStanding` — what one stood-up arena owns — is declared in `ElysiumArenaSpec.h`, because the
	// green-room harness holds one by value and is compiled in Shipping where this file is not.

	/**
	 * Stand the room up at `Origin` and request a Recast build over it.
	 *
	 * `bWithMeshes` hangs decorative, collision-free cubes under the boxes. The box stays the
	 * authority either way, so what a human sees and what a body walks on cannot drift apart.
	 *
	 * `EntityWorld` may be null — the solids and the navigation stand without it — and the anchors
	 * are simply not created in that case, which is reported rather than silent.
	 *
	 * The navigation build is ASYNCHRONOUS. `IsNavigationReady` is the poll; a caller that lets the
	 * cast in before it answers true gets characters that path nowhere.
	 */
	bool Stand(UWorld* World, FElysiumEntityWorld* EntityWorld, const FSpec& Spec,
		const FVector& Origin, bool bWithMeshes, FStanding& Out, FString& OutError);

	// Destroy the solids and the bounds volume and kill the anchor entities. Idempotent.
	void Teardown(FElysiumEntityWorld* EntityWorld, FStanding& Standing);

	// Whether the Recast graph over the arena has tiles and has finished building. The same test
	// `AElysiumMapActor::IsRuntimeNavigationReady` applies to a map's own graph.
	bool IsNavigationReady(const UWorld* World);
}

#endif // !UE_BUILD_SHIPPING
