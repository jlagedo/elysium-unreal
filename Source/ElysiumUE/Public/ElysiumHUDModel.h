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

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	EElysiumZoneState ZoneState = EElysiumZoneState::None;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Feed")
	bool bFeedVictimVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Feed")
	int32 FeedVictimBlood = 0;

	UPROPERTY(BlueprintReadOnly, Category = "HUD|Feed")
	int32 FeedVictimBloodCapacity = 0;

	// The drawn fraction. The bar does NOT divide the two counters above: the published view already
	// carries `CFeedBar`'s percent, including the pre-pulse anticipation the widget has no clock for.
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Feed")
	float FeedVictimPercent = 0.0f;

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

	// The terminal's `InfoCtrl` line (`docs/vtmb/computer-terminals.md` §8.4), drawn bottom-centre
	// while a hacking session raises one. Retail's client resolves the type byte into a
	// `Hacking_Strings` entry and draws it in the HUD font; the authority resolves it here, so this
	// is already the finished line and empty means the hint is down.
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FText TerminalHint;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FElysiumHUDEquipmentView Equipment;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FElysiumHUDDisciplineView Discipline;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FElysiumHUDSelectorView Selector;

	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FElysiumHUDStealthView Stealth;

	// What the player is wearing. Persistent and independent of the browsed category, the way
	// retail draws the worn clothing beside the meters rather than inside the selector.
	UPROPERTY(BlueprintReadOnly, Category = "HUD")
	FElysiumHUDEquipmentView Worn;

	UPROPERTY(BlueprintAssignable, Category = "HUD")
	FElysiumHUDModelChanged OnChanged;

private:
	// Fill the equipment readout and the weapon selector from the published snapshot. Both are one
	// projection so they cannot disagree about what is in hand.
	void ProjectEquipment(const FElysiumEquipmentView& View);
};
