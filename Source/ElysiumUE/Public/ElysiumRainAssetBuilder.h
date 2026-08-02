#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumRainAssetBuilder.generated.h"

class UNiagaraEmitter;
class UNiagaraSystem;
class UMaterialInterface;

/** Editor-only implementation behind the reproducible Python weather-asset generator. */
UCLASS()
class ELYSIUMUE_API UElysiumRainAssetBuilder final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Elysium|Weather")
	static UNiagaraSystem* BuildRainSystem(
		const FString& AssetName,
		const FString& PackagePath,
		UNiagaraEmitter* TemplateEmitter);

	UFUNCTION(BlueprintCallable, Category="Elysium|Weather")
	static bool BindRainMaterial(UNiagaraSystem* System, UMaterialInterface* Material);

	/** Empty on success; otherwise a verifier-ready description of every discovered error. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Weather")
	static FString ValidateRainSystem(UNiagaraSystem* System, UMaterialInterface* Material);
};
