#pragma once

#include "CoreMinimal.h"
#include "ElysiumHUDTypes.h"
#include "ElysiumViewState.h"
#include "UObject/Object.h"

#include "ElysiumHUDModel.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FElysiumHUDModelChanged);

// The stable, Blueprint-readable model consumed by the player HUD. It is a projection of
// FElysiumViewState, never another gameplay state store. Missing systems remain invalid and their
// regions collapse; only the non-Shipping preview path supplies representative placeholder rows.
UCLASS(BlueprintType)
class UElysiumHUDModel : public UObject
{
	GENERATED_BODY()

public:
	void Apply(const FElysiumViewState& View, EElysiumHUDPreview Preview = EElysiumHUDPreview::Off);

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 Revision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bVitalsValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 Health = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 MaxHealth = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 BloodPool = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 BloodCapacity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 Humanity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 Masquerade = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Feed")
	bool bFeedVictimVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Feed")
	int32 FeedVictimBlood = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Feed")
	int32 FeedVictimBloodCapacity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	EElysiumHUDReticle Reticle = EElysiumHUDReticle::None;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	int32 UseIcon = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	float UsePromptAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bUseActionable = false;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	bool bUseLocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FName UseAction = FName(TEXT("Use"));

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FLinearColor Fade = FLinearColor::Transparent;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FElysiumHUDEquipmentView Equipment;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FElysiumHUDDisciplineView Discipline;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FElysiumHUDSelectorView Selector;

	UPROPERTY(BlueprintAssignable, Category = "HUD")
	FElysiumHUDModelChanged OnChanged;
};
