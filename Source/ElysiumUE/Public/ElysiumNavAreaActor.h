#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

#include "ElysiumNavAreaActor.generated.h"

class UNavAreaBase;

/** One convex mark: a brush's hull points, world centimetres. */
USTRUCT()
struct FElysiumNavAreaConvex
{
	GENERATED_BODY()

	UPROPERTY() TArray<FVector> Points;
};

/**
 * Convex nav-area marks, stated as the hull points the pipeline already staged.
 *
 * Not `ANavModifierVolume`, and not `UNavModifierComponent`: a volume wants a `UBrushComponent`,
 * which means building brush geometry to describe a convex set we already have exactly, and the
 * component marks a BOX. Retail's volumes are convex brushes and their shape is the point -- a
 * roadway slab grown to its AABB would price pavement the oracle says is not priced.
 *
 * So this exports `FAreaNavModifier` convexes straight from the points, through the same
 * `INavRelevantInterface` seam the engine's own modifier uses. Static, saved with the level, and
 * carrying no collision: it marks an area, it does not stop anything.
 */
UCLASS(ClassGroup = Elysium)
class ELYSIUMUE_API UElysiumNavAreaComponent : public USceneComponent, public INavRelevantInterface
{
	GENERATED_BODY()

public:
	UElysiumNavAreaComponent();

	/** The area every convex below wears. */
	UPROPERTY() TSubclassOf<UNavAreaBase> AreaClass;

	/** One convex per brush -- the same point clouds the collision payload is cooked from, so the
	 *  mark and the solid cannot disagree about where a brush is. */
	UPROPERTY() TArray<FElysiumNavAreaConvex> Convexes;

	/** The union of every convex, which is what the navigation octree keys on. */
	UPROPERTY() FBox AreaBounds = FBox(ForceInit);

	// INavRelevantInterface
	virtual void GetNavigationData(FNavigationRelevantData& Data) const override;
	virtual FBox GetNavigationBounds() const override;
	virtual bool IsNavigationRelevant() const override;
};

/**
 * One map's nav-area marks, standing in its level beside the world-collision actor.
 *
 * Placed by the collision import, for the same reason that actor is: the lane that stages the
 * hulls is the one that knows which of them are roadway and which are doorways, so a second lane
 * could not disagree with it.
 */
UCLASS()
class ELYSIUMUE_API AElysiumNavAreaActor : public AActor
{
	GENERATED_BODY()

public:
	AElysiumNavAreaActor();

	/** The map these marks belong to, checked on adoption exactly as the collision actor's is. */
	UPROPERTY(VisibleAnywhere, Category = "Elysium|Nav") FString MapName;

	UPROPERTY(VisibleAnywhere, Category = "Elysium|Nav")
	TArray<TObjectPtr<UElysiumNavAreaComponent>> Areas;

	/** Name the map these marks belong to. Set here rather than from the caller's property write,
	 *  so the field stays read-only everywhere else -- the same arrangement the world-collision
	 *  actor uses. */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Nav")
	void Author(const FString& InMapName);

	/**
	 * Add one component wearing `AreaClass`, carrying one convex per run of `ConvexSizes` taken
	 * from `Points` in order. Returns the component, or null when nothing usable was given: a
	 * convex needs at least a tetrahedron, exactly as the collision payload's do.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Nav")
	UElysiumNavAreaComponent* AddArea(const FString& Label, TSubclassOf<UNavAreaBase> AreaClass,
		const TArray<FVector>& Points, const TArray<int32>& ConvexSizes);
};
