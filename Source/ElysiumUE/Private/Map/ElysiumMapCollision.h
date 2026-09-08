#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
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

// The world's convex brush set (`<map>.hulls`, or the payload's cooked convexes).
UCLASS(Transient)
class UElysiumHullCollisionComponent final : public UElysiumCollisionOnlyMeshComponent
{
	GENERATED_BODY()
};

// The displacement terrain trimesh (`<map>.dispcol`, or the payload's cooked trimesh).
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
	None,      // nothing built (disabled, or no hull data at all)
	Payload,   // /ElysiumBaked/<map>/DA_<map>_Collision, cooked offline
	Sidecar,   // <map>.hulls + <map>.dispcol, parsed and cooked at load
};

const TCHAR* ElysiumCollisionSourceName(EElysiumCollisionSource Source);

// The map's WALKABLE SURFACE. Baked world geometry carries no gameplay collision, so the two
// colliders built here are the only thing the player stands on: `<map>.hulls` is one convex
// element per solid world brush (invisible PLAYERCLIP volumes included, geometry the designer
// clipped off excluded), and `<map>.dispcol` is the displacement terrain trimesh the convex set
// cannot represent. Both are collision-only — never drawn.
//
// Both may instead arrive cooked, as one `UElysiumMapCollisionPayload` per map: same
// geometry, same component recipe, but the Chaos cook happened offline. The payload wins when the map has one and the
// sidecar readers answer otherwise; the asset's presence is the cutover flag.
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
	// false means no payload, and the sidecar readers below answer instead.
	bool AdoptPayload(const FString& MapName);
	// One FKConvexElem per solid brush, cooked in a single call. Returns true when at least one
	// hull loaded; false (sidecar missing/empty) means this map has no brush collider.
	bool LoadHulls(const FString& MapName);
	// The displacement terrain trimesh. Only meaningful alongside brush collision; no-op when the
	// sidecar is absent (map has no displacements).
	void LoadDispCol(const FString& MapName);
	// The two components are built the same way on both paths — profile, channel ignores, bounds,
	// registration — and differ only in where their BodySetup came from.
	UElysiumHullCollisionComponent* MakeHullComponent(AActor* Owner, const FBox& LocalBounds);
	UElysiumDispCollisionComponent* MakeDispComponent(AActor* Owner);

	UPROPERTY() TObjectPtr<UElysiumHullCollisionComponent> HullCollision;
	UPROPERTY() TObjectPtr<UElysiumDispCollisionComponent> DispCollision;
	UPROPERTY() TObjectPtr<const UElysiumMapCollisionPayload> Payload;
	EElysiumCollisionSource Source = EElysiumCollisionSource::None;
	EElysiumCollisionBuildState BuildState = EElysiumCollisionBuildState::Disabled;
	FString FailureReason;
};
