#pragma once

#include "CoreMinimal.h"

struct FElysiumObjModel;
class UStaticMesh;

// Builds a UStaticMesh at runtime from a parsed OBJ model, for InstancedStaticMeshComponent
// rendering (ISM requires a UStaticMesh; PMC is not an option). One material slot per OBJ
// group, materials bound via FElysiumMaterialFactory. When bConvexCollision is set (solid
// props), a single convex primitive (hull of the model's vertices, matching the Godot prop
// path) is cooked onto the mesh's BodySetup. Outer owns the mesh + its MIDs, so they unload
// with the map. Uses BuildFromMeshDescriptions with bFastBuild — the only runtime/packaged-
// safe path; normals/tangents are computed explicitly since the fast path does not.
struct FElysiumStaticMeshBuilder
{
	static UStaticMesh* Build(const FElysiumObjModel& Model, const FString& Dir,
		bool bConvexCollision, UObject* Outer);
};
