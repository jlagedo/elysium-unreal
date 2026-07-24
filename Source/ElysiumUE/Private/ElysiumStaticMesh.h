#pragma once

#include "CoreMinimal.h"

struct FElysiumObjModel;
struct FElysiumTextureCache;
class UStaticMesh;

// Builds a UStaticMesh at runtime from a parsed OBJ model, for InstancedStaticMeshComponent
// rendering (ISM requires a UStaticMesh; PMC is not an option). One material slot per OBJ
// group, materials bound via FElysiumMaterialFactory. When bConvexCollision is set (solid
// props), convex collision is cooked onto the mesh's BodySetup: one FKConvexElem per hull in
// ConvexHulls when supplied (8.4 physics props read a decomposed `.hulls` sidecar), else a
// single hull of the model's vertices (matching the Godot prop path). Outer owns the mesh +
// its MIDs, so they unload with the map. Uses BuildFromMeshDescriptions with bFastBuild — the
// only runtime/packaged-safe path; normals/tangents are computed explicitly since the fast
// path does not.
struct FElysiumStaticMeshBuilder
{
	static UStaticMesh* Build(const FElysiumObjModel& Model, const FString& Dir,
		bool bConvexCollision, UObject* Outer, FElysiumTextureCache& Cache,
		const TArray<TArray<FVector>>* ConvexHulls = nullptr);

	// Read a `.hulls` sidecar (the world-collider format: one convex hull per line, flat
	// space-separated Unreal-cm verts, >= 4 per line). Returns false (Out untouched) if the
	// file is missing or holds no valid hull. Shared by the physics-prop collision path.
	static bool LoadConvexHulls(const FString& Path, TArray<TArray<FVector>>& Out);
};
