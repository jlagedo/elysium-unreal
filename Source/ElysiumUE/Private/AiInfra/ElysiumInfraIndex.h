#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "ElysiumInfraIndex.generated.h"

// The declared set of a level's baked infrastructure actors (0018 story 2): every BSP entity index
// the bake placed an `AElysiumInfraActor` for, with its family tag. The bake always places exactly
// one. At map load its presence switches the adoption pass on — every declared index must be
// adopted exactly once — and its absence (a level baked before this lane) leaves the entity table
// as the transport loaded it.
UCLASS(NotBlueprintable)
class AElysiumInfraIndex final : public AActor
{
	GENERATED_BODY()

public:
	AElysiumInfraIndex(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Infrastructure")
	TArray<int32> DeclaredIndices;

	// The family tag of each declared index, parallel to `DeclaredIndices`.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Infrastructure")
	TArray<FName> DeclaredFamilies;

	// The stage's content hash, for tracing a level back to the manifest it was baked from.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Elysium|Infrastructure")
	FString StageSha256;

	// False when the two arrays differ in length (nothing is stored).
	UFUNCTION(BlueprintCallable, Category = "Elysium|Bake")
	bool ConfigureDeclaredSet(const TArray<int32>& Indices, const TArray<FName>& Families);

	UFUNCTION(BlueprintCallable, Category = "Elysium|Bake")
	void SetStageSha256(const FString& InSha256);

	// How many declared indices carry `Family`.
	int32 CountOf(FName Family) const;
};
