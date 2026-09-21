#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Visual/ElysiumRopes.h"
#include "ElysiumRopeActor.generated.h"

class UBillboardComponent;
class USceneComponent;

/**
 * One overhead cable segment in the baked level (0018 story 21-3). The bake
 * (`pipeline/unreal/bake_ropes.py`) stands one at each segment's A endpoint, writes its eight facts
 * through `ConfigureRope` and stamps the `elysium.rope` / `elysium.src=<index>` tags; the runtime
 * buckets it once (`UElysiumMapVisuals::AdoptBakedLevel`) and `UElysiumMapVisuals::BuildRopes`
 * builds one `UCableComponent` per adopted actor.
 *
 * The actor draws nothing. It is the serialized rope def and the thing a designer selects; the
 * cable itself is built on the map actor, where the material instance cache and the map's lifetime
 * already are.
 */
UCLASS(NotBlueprintable)
class AElysiumRopeActor final : public AActor
{
	GENERATED_BODY()

public:
	AElysiumRopeActor(const FObjectInitializer& ObjectInitializer);

	/** Bake-only: the staged row, hooked up in one call so the writer cannot set half of it. */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Bake")
	void ConfigureRope(int32 InSourceIndex, const FString& MaterialId, FVector A, FVector B,
		float WidthCm, float RestCm, int32 Nodes, float TexScale, int32 Flags);

	/** The segment's row ordinal in the staged `ropes` block, the number the `elysium.src` tag carries. */
	UPROPERTY(VisibleAnywhere, Category = "Elysium|Rope")
	int32 SourceIndex = INDEX_NONE;

	/** Every fact the cable is built from. `A` is also this actor's own location. */
	UPROPERTY(VisibleAnywhere, Category = "Elysium|Rope")
	FElysiumRopeDef Rope;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Billboard;
#endif
};
