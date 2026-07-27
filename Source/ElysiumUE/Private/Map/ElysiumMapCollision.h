#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "ElysiumMapCollision.generated.h"

class UProceduralMeshComponent;

// The map's WALKABLE SURFACE. Baked world geometry carries no gameplay collision, so the two
// colliders built here are the only thing the player stands on: `<map>.hulls` is one convex
// element per solid world brush (invisible PLAYERCLIP volumes included, geometry the designer
// clipped off excluded), and `<map>.dispcol` is the displacement terrain trimesh the convex set
// cannot represent. Both are collision-only — never drawn.
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

	// Build both colliders for this map. Returns true when at least one convex hull loaded — a
	// false leaves the map with no walkable surface at all, which is a warning, not an error
	// (`elysium.BrushCollision 0` is a debugging flythrough). Reads the cvar itself, so the caller
	// asks for the surface and gets whatever the map and the A/B allow.
	bool Build(const FString& MapName);

	// Convex-hull count and displacement-triangle count, for the debug overlay.
	int32 HullCount = 0;
	int32 DispTriCount = 0;
	// Whether Build produced brush collision (its return value, kept for the overlay).
	bool bBrushCollision = false;

private:
	// One FKConvexElem per solid brush, cooked in a single call. Returns true when at least one
	// hull loaded; false (sidecar missing/empty) means this map has no brush collider.
	bool LoadHulls(const FString& MapName);
	// The displacement terrain trimesh. Only meaningful alongside brush collision; no-op when the
	// sidecar is absent (map has no displacements).
	void LoadDispCol(const FString& MapName);

	UPROPERTY() TObjectPtr<UProceduralMeshComponent> HullCollision;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> DispCollision;
};
