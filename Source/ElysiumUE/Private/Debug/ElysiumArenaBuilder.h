#pragma once

#include "CoreMinimal.h"
#include "Debug/ElysiumArenaSpec.h"
#include "ElysiumEntityHandle.h"   // the anchors are runtime entities, held by handle

#if !UE_BUILD_SHIPPING

class AActor;
class FElysiumEntityWorld;
class UWorld;
struct FElysiumEntityDef;
struct FElysiumPlaceRow;

// The arena's engine half: it turns `ElysiumArena::FSpec`'s values into collision, a navigable
// surface, `intersting_place` entities and the cover-node AI network, and makes no decisions of its
// own. Every choice about
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
	 *
	 * **Navigation is bake-only in this project (0018 story 21) and the arena is the exception.** The
	 * ini takes the engine's initial build lock in every world (`bInitialBuildingLocked`), creates no
	 * nav data (`bAutoCreateNavigationData=False`) and makes every Recast mesh
	 * `DynamicModifiersOnly`, which cannot rasterise geometry in a game world. A stage world has no
	 * baked mesh to adopt, so the builder creates the `Human` agent's mesh itself, makes that one
	 * instance `Dynamic`, and takes the initial lock off the way `AElysiumMapActor::
	 * EnsureRuntimeNavigation` does (`NoRebuild`) before it asks for the build.
	 */
	bool Stand(UWorld* World, FElysiumEntityWorld* EntityWorld, const FSpec& Spec,
		const FVector& Origin, bool bWithMeshes, FStanding& Out, FString& OutError);

	/**
	 * One entity row shaped the way a baked map's is (`UElysiumMapEntities::Deserialize`): `Origin`
	 * in Unreal centimetres, and the raw keyvalues carrying the authored `origin` (Source inches, Y
	 * negated back) and `angles` (Source degrees, from the Unreal-native `YawDeg`). The caller adds
	 * the class's own keys. Every arena row that stands in for a map-authored entity — the cover
	 * nodes, the scenario's gunman — is built here, so none carries a key a map row would not.
	 */
	FElysiumEntityDef AuthoredRow(const TCHAR* Classname, const FString& TargetName,
		const FVector& OriginCm, float YawDeg);

	// The rows the arena stands, as data, so a caller can hand them to the map path
	// (`AElysiumMapActor::RebuildStageWorld` → `FElysiumEntityWorld::Load`) instead of the runtime
	// door. `Stand` and `StandCoverNetwork` build exactly these and spawn them one at a time.
	//
	// One `intersting_place` anchor row (`arena_<name>`).
	FElysiumEntityDef AnchorRow(const FAnchor& Anchor, const FVector& Origin);
	// One hint-making node row, in the classname that forces the node's cover type. `NodeIndex` is
	// written as the row's `nodeid` (provenance only: the spawn counter, not the key, binds the hint).
	FElysiumEntityDef NodeRow(const FNode& Node, int32 NodeIndex, const FVector& Origin);
	// The spec's AI network: one ground row per node, network index = position.
	TArray<FElysiumPlaceRow> NodePlaceRows(const FSpec& Spec, const FVector& Origin);

	/**
	 * Stand the spec's cover nodes as the entity world's AI network and spawn one hint-making node
	 * row per node on it. Returns the number of `ai_hint`s that stood; `OutNodes` receives them in
	 * spec order.
	 *
	 * The map actor's order, mirrored: `AdoptMapPlaces` adopts the rows BEFORE `Load`, and `Load`
	 * opens with `BeginMapSpawn` (`0x102f6690`: the node-row counter back to 0, every node's run-time
	 * words cleared) before any node row spawns. A stage world has already run `Load` over zero defs,
	 * so this adopts, then calls `BeginMapSpawn` itself, then spawns -- and the counter hands node
	 * `i` to the `i`th row, which is what binds each hint's `m_nNodeID` to its own row.
	 *
	 * Idempotent: every live entity already named after a spec node is killed first (a killed hint
	 * stays on the hint list, but no search reads a dead entity), so a re-stand replaces rather than
	 * stacks.
	 *
	 * **The runtime door, not `Load`.** This is the arena-entry path and `gr_scenario --runtime`'s:
	 * each row goes through `SpawnRuntimeEntity`, which runs the same `CNodeEnt::Spawn` arm at
	 * creation and then Spawn / PostSpawn / Activate per entity (the world is active) instead of
	 * `Load`'s all-Spawn, all-PostSpawn, then `Activate` barrier. A second `Load` over the live world
	 * would alias handles (`Load` never clears `EntityList` and hands each def its def-array index),
	 * so the map path (`gr_scenario cover`) does not re-`Load` it: it hands `AnchorRow` / `NodeRow` /
	 * `NodePlaceRows` to `AElysiumMapActor::RebuildStageWorld`, which builds a fresh world.
	 */
	int32 StandCoverNetwork(FElysiumEntityWorld& EntityWorld, const FSpec& Spec,
		const FVector& Origin, TArray<FElysiumEntityHandle>& OutNodes);

	// Kill every live entity named after a spec node and put the AI network back to zero nodes.
	// Returns the number killed.
	int32 ClearCoverNetwork(FElysiumEntityWorld& EntityWorld, const FSpec& Spec);

	// Destroy the solids and the bounds volume and kill the anchor and node entities. Idempotent.
	void Teardown(FElysiumEntityWorld* EntityWorld, FStanding& Standing);

	// Whether a Recast graph over the arena has tiles and has finished building. The same test
	// `AElysiumMapActor::IsRuntimeNavigationReady` applies to a map's own graph: any registered Recast
	// mesh with tiles, not only the main one.
	bool IsNavigationReady(const UWorld* World);
}

#endif // !UE_BUILD_SHIPPING
