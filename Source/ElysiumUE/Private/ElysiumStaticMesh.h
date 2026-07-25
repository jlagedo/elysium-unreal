#pragma once

#include "CoreMinimal.h"

struct FElysiumCardBakeItem;
struct FElysiumCardStore;
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
	// Everything the Lumen-cards path needs to give a prop model its card representation
	// (docs/lumen-coverage-spike.md). Optional: with no Cards block Build() falls straight to
	// bounds cards, which is what a map with no bake gets.
	struct FPropCards
	{
		FString Stem;                            // the props/<stem>.obj model key the bake is keyed on
		const FElysiumCardStore* Store = nullptr;   // the loaded <map>.cards, or null
		TArray<FElysiumCardBakeItem>* BakeItems = nullptr;   // where a -ElysiumCards run collects this mesh
	};

	static UStaticMesh* Build(const FElysiumObjModel& Model, const FString& Dir,
		bool bConvexCollision, UObject* Outer, FElysiumTextureCache& Cache,
		const TArray<TArray<FVector>>* ConvexHulls = nullptr,
		const FPropCards* Cards = nullptr);

	// The content hash of a prop model's geometry, as the bake keys it: the OBJ's shared vertex
	// list plus every group's index list, in parse order. Geometry only — a re-texture must not
	// invalidate a fit.
	static uint64 HashPropGeometry(const FElysiumObjModel& Model);

	// Read a `.hulls` sidecar (the world-collider format: one convex hull per line, flat
	// space-separated Unreal-cm verts, >= 4 per line). Returns false (Out untouched) if the
	// file is missing or holds no valid hull. Shared by the physics-prop collision path.
	static bool LoadConvexHulls(const FString& Path, TArray<TArray<FVector>>& Out);

	// `elysium.LumenCards` — whether runtime-built meshes are given a Lumen card representation
	// (docs/lumen-coverage-spike.md). Read at build time, so re-travel to A/B it.
	static bool LumenCardsEnabled();

	// `elysium.LumenCardsBaked` — whether the `<map>.cards` bake is consulted at all, or every
	// mesh takes the bounds fallback. The A/B behind the spike's measurements.
	static bool BakedCardsEnabled();

	// Hand a runtime-built mesh the bounds-derived card representation it needs to enter Lumen's
	// surface cache — the fallback when the map has no baked cards for it. Build() applies this
	// itself; the world-chunk path calls it directly. Must run before any component renders the
	// mesh — the scene proxy copies the pointer in its ctor.
	static void AttachLumenCards(UStaticMesh* Mesh);
};
