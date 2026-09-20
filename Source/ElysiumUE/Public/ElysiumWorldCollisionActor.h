#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "ElysiumContentsSignature.h"
#include "GameFramework/Actor.h"

#include "ElysiumWorldCollisionActor.generated.h"

class UElysiumMapCollisionPayload;

/**
 * One cooked world body, standing in the level.
 *
 * Deliberately a bare `UPrimitiveComponent` and not the `UProceduralMeshComponent` the runtime's
 * transient colliders use: that class owns its `BodySetup` as an INSTANCED sub-object, which is
 * the wrong ownership for a saved component pointing at a body the payload asset owns -- saving
 * would duplicate the cooked geometry into the level.
 *
 * It is never drawn (no scene proxy) and carries its bounds explicitly, because a collision-only
 * component has no render section to bound it and would otherwise register with navigation as
 * empty even though its body holds the whole walkable surface.
 */
UCLASS(ClassGroup = Elysium, NotBlueprintable)
class ELYSIUMUE_API UElysiumWorldCollisionComponent final : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UElysiumWorldCollisionComponent();

	/** The contents signature every brush in this body answers with. */
	UPROPERTY(VisibleAnywhere, Category = "Elysium|Collision") uint8 Signature = 0;

	/** The payload's cooked body. Referenced, never owned -- the asset outlives the level load. */
	UPROPERTY(VisibleAnywhere, Category = "Elysium|Collision") TObjectPtr<UBodySetup> Body;

	/** Local-space bounds of `Body`'s convexes. */
	UPROPERTY(VisibleAnywhere, Category = "Elysium|Collision") FBox LocalCollisionBounds =
		FBox(ForceInit);

	/** Apply the signature's collision profile and navigation relevance. Called when the
	 *  component is authored and again on load, because only a profile NAME survives a `.umap`
	 *  round trip -- responses set beside it are discarded. */
	void ApplySignature();

	virtual UBodySetup* GetBodySetup() override { return Body; }
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override { return nullptr; }
	virtual void PostLoad() override;
};

/**
 * The map's world collision, as an actor saved in the level (0018 story 3, job 3).
 *
 * The runtime used to build these bodies into transient components at map load. Standing them in
 * the level instead is what lets navigation be BAKED: Recast can only cut a mesh from geometry
 * that exists before the game runs, and it must be cut from the right solids -- a body affects
 * navigation exactly when its contents signature blocks an NPC, so the NPC-only clips cut the
 * mesh and the sight-only brushes do not.
 *
 * Placed by the collision import, which is also what authors the bodies it points at, so the
 * actor and the asset are written by one lane and cannot disagree.
 */
UCLASS(NotBlueprintable)
class ELYSIUMUE_API AElysiumWorldCollisionActor final : public AActor
{
	GENERATED_BODY()

public:
	AElysiumWorldCollisionActor();

	/** The payload these components' bodies belong to. A hard reference: the level cannot load
	 *  its own collision without it. */
	UPROPERTY(VisibleAnywhere, Category = "Elysium|Collision")
	TObjectPtr<UElysiumMapCollisionPayload> Payload;

	/** The map stem, for the adoption check -- a level must never adopt another map's collision. */
	UPROPERTY(VisibleAnywhere, Category = "Elysium|Collision") FString MapName;

	UPROPERTY(VisibleAnywhere, Category = "Elysium|Collision")
	TArray<TObjectPtr<UElysiumWorldCollisionComponent>> Bodies;

#if WITH_EDITOR
	/** Author one component per world body the payload carries, replacing any already here.
	 *  Returns the number of components authored. */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Import")
	int32 AuthorFromPayload(UElysiumMapCollisionPayload* InPayload);
#endif
};
