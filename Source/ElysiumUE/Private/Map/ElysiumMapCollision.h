#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ElysiumContentsSignature.h"
#include "ProceduralMeshComponent.h"
#include "ElysiumMapCollision.generated.h"

class UElysiumMapCollisionPayload;

// UProceduralMeshComponent derives its bounds exclusively from render sections. The brush world
// deliberately has none — and on the R4.2 cooked-payload path neither collider has any — so a
// collision-only component otherwise registers with navigation as empty even though its BodySetup
// contains the complete walkable surface. Carry the collider's own bounds explicitly; the
// BodySetup remains the geometry Recast reads. Overrides CreateSceneProxy to return nullptr:
// collision-only, never drawn or ray-traced.
UCLASS(Transient)
class UElysiumCollisionOnlyMeshComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	void SetLocalCollisionBounds(const FBox& InBounds);
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override { return nullptr; }

private:
	FBox LocalCollisionBounds = FBox(ForceInit);
};

// The displacement terrain trimesh (the payload's cooked trimesh).
UCLASS(Transient)
class UElysiumDispCollisionComponent final : public UElysiumCollisionOnlyMeshComponent
{
	GENERATED_BODY()
};

// Readiness of the only collision the player can stand on. Disabled is an intentional satisfied
// state (`elysium.BrushCollision 0`); Failed is never safe to admit gameplay.
enum class EElysiumCollisionBuildState : uint8
{
	Disabled,
	Cooking,
	Ready,
	Failed,
};

const TCHAR* ElysiumCollisionBuildStateName(EElysiumCollisionBuildState State);

// Where this map's world collision came from (R4.2). Reported so a caller can log or assert the
// transport it actually got rather than the one it assumed, exactly as the entity table does.
enum class EElysiumCollisionSource : uint8
{
	None,      // nothing built (disabled, or no usable payload)
	Payload,   // /ElysiumBaked/<map>/DA_<map>_Collision, cooked offline -- the only transport
};

const TCHAR* ElysiumCollisionSourceName(EElysiumCollisionSource Source);

// The map's WALKABLE SURFACE, adopted rather than built. Baked world geometry carries no gameplay
// collision, so what stands here is the only thing the player stands on, and all of it arrives
// cooked in one `UElysiumMapCollisionPayload` per map.
//
// The world itself is not a component of this object at all: it is the level's own saved
// `AElysiumWorldCollisionActor`, one static body per contents signature, which is what lets a
// navigation mesh be cut from it offline and adopted at load. This object verifies that actor
// against the payload and keeps a pointer. Only the displacement trimesh -- one body, outside the
// signature partition -- is still a component registered here.
//
// There is no loose-sidecar path and no run-time build (0018 story 21): a map that cannot answer
// from its own baked content fails the load with a named error naming the bake command.
//
// A component on AElysiumMapActor, deliberately separate from UElysiumMapVisuals: what the map
// looks like and what it is solid against are two different sidecars answering two different
// questions, and only one of them is a rendering concern.
UCLASS()
class UElysiumMapCollision : public USceneComponent
{
	GENERATED_BODY()

public:
	UElysiumMapCollision();

	// Build both colliders for this map. Returns true when a required asynchronous build started.
	// When collision is intentionally disabled, BuildState is Disabled and the activation
	// prerequisite is satisfied. Missing/invalid required hull data while enabled is Failed.
	bool Build(const FString& MapName);
	EElysiumCollisionBuildState GetBuildState() const;
	const FString& GetFailureReason() const { return FailureReason; }
	// Union of the live hull/displacement collision components. Valid once Build has produced at
	// least one collider; used to size the runtime Recast bounds around the actual playable world.
	FBox GetWorldBounds() const;
	// Re-register the completed BodySetups with the navigation octree immediately before the one
	// runtime Recast build. Async Chaos cooking completes after component registration.
	void RefreshNavigationData();

	// Convex-hull count and displacement-triangle count, for the debug overlay.
	int32 HullCount = 0;
	int32 DispTriCount = 0;
	// Whether Build produced brush collision (its return value, kept for the overlay).
	bool bBrushCollision = false;

	// Which transport answered for this map, and the payload itself when one did. The payload is
	// retained for the whole map load because the entity world reads per-brush-entity bodies off
	// it while it builds them (`FElysiumEntityWorld::BuildBrushBody`).
	EElysiumCollisionSource GetSource() const { return Source; }
	const UElysiumMapCollisionPayload* GetPayload() const { return Payload; }

private:
	// The R4.2 cooked payload, when this map has one: adopt its two world body setups verbatim.
	// Returns true when the world convex set was adopted (which is what makes the map walkable);
	// false fails the load -- there are no sidecar readers behind this any more.
	bool AdoptPayload(const FString& MapName);
	// The level's own world-collision actor, when it carries one: its components are static and
	// saved, which is what lets a navigation mesh be baked from them. Adopting it spawns nothing.
	// False means the level has none (build the components) or carries a wrong one (fail).
	bool AdoptLevelCollisionActor(const FString& InMapName,
		const class UElysiumMapCollisionPayload& Asset);
	UElysiumDispCollisionComponent* MakeDispComponent(AActor* Owner);

	UPROPERTY() TObjectPtr<UElysiumDispCollisionComponent> DispCollision;
	UPROPERTY() TObjectPtr<const UElysiumMapCollisionPayload> Payload;
	// The adopted level actor, when this map's collision stands in the level rather than being
	// built into transient components at load.
	UPROPERTY() TObjectPtr<class AElysiumWorldCollisionActor> LevelCollision;
	EElysiumCollisionSource Source = EElysiumCollisionSource::None;
	EElysiumCollisionBuildState BuildState = EElysiumCollisionBuildState::Disabled;
	FString FailureReason;
};
