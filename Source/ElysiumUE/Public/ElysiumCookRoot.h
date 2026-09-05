#pragma once

#include "CoreMinimal.h"
#include "Engine/PrimaryAssetLabel.h"
#include "ElysiumCookRoot.generated.h"

/** Packaging-only root. Producers never reference it; it never loads the corpus at runtime.
 * A dedicated primary type avoids changing the engine's /Game label policy or model identity.
 */
UCLASS()
class ELYSIUMUE_API UElysiumCookRoot final : public UPrimaryAssetLabel
{
	GENERATED_BODY()
public:
	UElysiumCookRoot();
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	static FString PackagePath();
	// The original producer scope/absence ledger is retained in this editor packaging artifact.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Cook") FString SourceEvidenceJson;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Cook") FString InputDigest;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Cook") FString InventoryDigest;
	UFUNCTION(BlueprintCallable, Category="Elysium|Cook")
	static UElysiumCookRoot* ApplyJson(UElysiumCookRoot* Asset, const FString& Json, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|Cook")
	static FString Verify(UElysiumCookRoot* Asset, const FString& Json);
	/** Read AssetManager registration/management rules in an editor with main's config installed.
	 * This is not proof that a platform cook/container contains the expected packages.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Cook")
	static FString VerifyCookRules(const FString& Json);
};
