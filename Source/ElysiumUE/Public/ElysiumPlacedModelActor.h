#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ElysiumPlacedModelActor.generated.h"

class UAnimSequence;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

// Baked GAME_LUMP representation for a model whose authored resting pose is not storage-equivalent.
// The static component remains collision authority; the skeletal component is visual-only and held
// at frame zero before it is shown.
UCLASS()
class AElysiumPlacedModelActor : public AActor
{
	GENERATED_BODY()

public:
	AElysiumPlacedModelActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium")
	TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium")
	TObjectPtr<USkeletalMeshComponent> SkeletalVisual;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium")
	TObjectPtr<UStaticMeshComponent> CollisionProxy;

	UFUNCTION(BlueprintCallable, CallInEditor, Category="Elysium")
	bool ConfigureRest(USkeletalMesh* SkeletalMesh, UAnimSequence* RestSequence,
		UStaticMesh* StaticMesh, bool bSolid);
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Elysium")
	FString RefreshGarmentMaterials();
};
