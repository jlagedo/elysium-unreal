#pragma once

#include "CoreMinimal.h"

struct FElysiumObjModel;
struct FElysiumTextureCache;
class UStaticMesh;

// Builds a UStaticMesh at runtime from a parsed OBJ model. The map's own geometry is baked
// content now, so this serves the one case the bake cannot: a `prop_physics` model, whose
// simulating body needs the exporter's CoACD-decomposed `.hulls` cooked into its own BodySetup —
// one FKConvexElem per hull, or a single hull of the model's vertices when the sidecar is absent.
// One material slot per OBJ group, materials bound via FElysiumMaterialFactory. Outer owns the
// mesh + its MIDs, so they unload with the map. Uses BuildFromMeshDescriptions with bFastBuild —
// the only runtime/packaged-safe path; normals/tangents are computed explicitly since the fast
// path does not. Such a mesh gets bounds-derived Lumen cards, not the DDC-fitted surfel cards a
// baked asset carries.
struct FElysiumStaticMeshBuilder
{
	static UStaticMesh* Build(const FElysiumObjModel& Model, const FString& Dir,
		bool bConvexCollision, UObject* Outer, FElysiumTextureCache& Cache,
		const TArray<TArray<FVector>>* ConvexHulls = nullptr);

	// Read a `.hulls` sidecar (the world-collider format: one convex hull per line, flat
	// space-separated Unreal-cm verts, >= 4 per line). Returns false (Out untouched) if the
	// file is missing or holds no valid hull. Shared by the physics-prop collision path.
	static bool LoadConvexHulls(const FString& Path, TArray<TArray<FVector>>& Out);

	// Hand a runtime-built mesh a bounds-derived card representation, so it can enter Lumen's
	// surface cache at all. Build() applies this itself. Must run before any component renders
	// the mesh — the scene proxy copies the pointer in its ctor.
	static void AttachLumenCards(UStaticMesh* Mesh);
};
