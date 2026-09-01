#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "ElysiumMapCollisionPayload.generated.h"

class UBodySetup;

// One map's collision as cooked content (R4.2, `docs/architecture/seam_map_map.md` -> "Import").
// It carries the three colliders the running game stands on -- the world's convex brush set, the
// displacement terrain trimesh, and one convex body per brush entity -- as authored `UBodySetup`s
// with a stable `BodySetupGuid`, so the Chaos cook happens once offline (DDC in the editor, the
// package in a cooked build) instead of on every map load from `<map>.hulls` / `<map>.dispcol` and
// a per-entity `CreatePhysicsMeshes`.
//
// Authored by `pipeline/unreal/import_map_collision.py` from the stage
// (`elysium_pipeline.importers.map_collision`), which reads the sidecars this asset replaces and
// asserts count-and-vertex parity against them before writing the manifest.

// One convex volume as authoring input: a flat vertex set in Unreal centimetres, exactly the
// numbers `<map>.hulls` prints and the `.ents` join's `hulls` carry. `FKConvexElem` builds the
// hull, so vertex order is irrelevant. This struct crosses the Python boundary and is never stored
// on the asset -- the authored geometry lives in the body setups' `AggGeom` and nowhere else.
USTRUCT(BlueprintType)
struct FElysiumCollisionHull
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull") TArray<FVector> Vertices;
};

// One brush entity's cooked body, keyed by the entity's lump ordinal -- the running game's entity
// handle index, which is what `FElysiumEntityWorld::BuildBrushBody` has in hand.
USTRUCT()
struct FElysiumBrushCollisionBody
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Brush") int32 EntityIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category = "Brush") TObjectPtr<UBodySetup> Body;
};

// The per-map collision payload: `/ElysiumBaked/<map>/DA_<map>_Collision`, one asset per map,
// beside the map's own `.umap`. `FElysiumContentPaths::BakedMapCollision` is the one path
// accessor; its Python twin is `elysium_pipeline.importers.map_collision.asset_path`.
//
// The asset is the trimesh's collision data provider: `UBodySetup` reads triangle source from
// `Cast<IInterface_CollisionDataProvider>(GetOuter())`, and the displacement setup's outer is this
// asset (the `UProceduralMeshComponent` pattern with the asset as the vessel).
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumMapCollisionPayload : public UDataAsset,
	public IInterface_CollisionDataProvider
{
	GENERATED_BODY()

public:
	// The map stem this payload belongs to. The one settable property: everything below is authored
	// by this class's own functions, because a hand-edited convex set would be collision nobody
	// can reproduce from the corpus.
	UPROPERTY(EditAnywhere, Category = "Map") FString MapName;

	// The world's solid brushes: one `FKConvexElem` per `<map>.hulls` row, in file order.
	UPROPERTY(VisibleAnywhere, Category = "World") TObjectPtr<UBodySetup> WorldHulls;

	// The displacement terrain, complex-as-simple. Cooked from the triangle soup below, which is
	// this asset's own `GetPhysicsTriMeshData` answer.
	UPROPERTY(VisibleAnywhere, Category = "World") TObjectPtr<UBodySetup> Displacement;

	// `<map>.dispcol` as a vertex buffer plus flat index triples, Unreal centimetres. Stored at
	// float width because `FTriMeshCollisionData::Vertices` is `FVector3f` -- the cook narrows the
	// numbers either way, so storing them wider would only hide where the narrowing happens.
	UPROPERTY(VisibleAnywhere, Category = "World") TArray<FVector3f> DisplacementVertices;
	UPROPERTY(VisibleAnywhere, Category = "World") TArray<int32> DisplacementIndices;

	// One body per brush entity, ascending by lump ordinal. Entities with no hulls have no row.
	UPROPERTY(VisibleAnywhere, Category = "Brush") TArray<FElysiumBrushCollisionBody> BrushBodies;

	// The cooked world set, or null when this map authored none (which is a payload the runtime
	// must refuse -- the world collider is required, not optional).
	UBodySetup* GetWorldHulls() const { return WorldHulls; }
	UBodySetup* GetDisplacement() const { return Displacement; }
	// This entity's cooked body, or null when the ordinal has none (a point entity, or an entity
	// created at runtime past the map's def array). A null answer means "cook it", not "fail".
	UBodySetup* FindBrushBody(int32 EntityIndex) const;

	// The number of convex elements the world set carries, without touching the setup's geometry --
	// the count the overlay and the parity test compare against `<map>.hulls`.
	int32 WorldHullCount() const;
	int32 DisplacementTriangleCount() const { return DisplacementIndices.Num() / 3; }

	// Local-space bounds of the two world colliders. A collision-only procedural mesh has no render
	// section to bound it, so the components take these explicitly or register with navigation as
	// empty (`docs/architecture/seam_map_map.md` -> "Import" -> "Consumption and cutover").
	FBox WorldHullBounds() const;
	FBox DisplacementBounds() const;

	// Create the Chaos structures for every setup this payload carries, and report whether all of
	// them succeeded. Cheap when the cook is in the DDC or in the package; this is where a cooked
	// build's buffers become live geometry.
	bool CreatePhysicsMeshes();

	// IInterface_CollisionDataProvider -- the displacement soup, and nothing else.
	virtual bool GetPhysicsTriMeshData(FTriMeshCollisionData* CollisionData,
		bool InUseAllTriData) override;
	virtual bool ContainsPhysicsTriMeshData(bool InUseAllTriData) const override;
	virtual bool WantsNegXTriMesh() override { return false; }

#if WITH_EDITOR
	// Authoring, called by the import lane and by tests -- never by the running game. Each call
	// replaces what was there and mints a fresh `BodySetupGuid`, because that GUID is the physics
	// DDC key: reusing it after changing geometry would serve the previous cook.
	UFUNCTION(BlueprintCallable, Category = "Elysium|Import")
	void AuthorWorldHulls(const TArray<FElysiumCollisionHull>& Hulls);

	UFUNCTION(BlueprintCallable, Category = "Elysium|Import")
	void AuthorDisplacement(const TArray<FVector>& Vertices, const TArray<int32>& Indices);

	// One brush entity's body. Rows are appended in call order; the lane calls this in ascending
	// ordinal, which `FindBrushBody`'s search assumes only for readability, not for correctness.
	UFUNCTION(BlueprintCallable, Category = "Elysium|Import")
	void AuthorBrushBody(int32 EntityIndex, const TArray<FElysiumCollisionHull>& Hulls);

	// Drop every authored body, so a re-author of an existing asset does not accumulate.
	UFUNCTION(BlueprintCallable, Category = "Elysium|Import")
	void ResetAuthoring();

	// Cook everything just authored; the answer is the empty string on success and the failing
	// setups by name otherwise, so an import that cannot cook fails loudly at import time rather
	// than silently at map load. (One return value and no out parameter: a `bool` return alongside
	// an out `FString` reaches Python as the out parameter alone.)
	UFUNCTION(BlueprintCallable, Category = "Elysium|Import")
	FString CookAuthored();
#endif
};
