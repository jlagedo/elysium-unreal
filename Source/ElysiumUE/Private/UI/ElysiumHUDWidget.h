#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "Styling/SlateBrush.h"

#include "ElysiumHUDTypes.h"

#include "ElysiumHUDWidget.generated.h"

class UElysiumHUDModel;
class UTexture2D;

// The passive in-world HUD surface. It deliberately owns no input or gameplay references: every
// Slate binding reads the local player's stable UElysiumHUDModel projection.
UCLASS()
class UElysiumHUDWidget final : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	void SetModel(UElysiumHUDModel* InModel) { Model = InModel; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	float VirtualScale() const;
	void EnsureUseIcons();
	void EnsureContrastVeils();
	const FSlateBrush* UseIconBrush() const;
	const FSlateBrush* UseBindingBrush() const;
	FText UseBindingText() const;
	const FSlateBrush* HudArtBrush(FName ArtPath);

	UPROPERTY(Transient)
	TObjectPtr<UElysiumHUDModel> Model;

	// The 72 use icons and the ring, one imported `T_` each (R6.6), pinned against GC for the
	// widget's lifetime; the brushes below reference them.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> UseIconTextures;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UTexture2D>> HudArtTextures;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LeftContrastVeil;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RightContrastVeil;

	FSlateBrush UseRingBrush;
	mutable FSlateBrush CurrentUseBindingBrush;
	TMap<int32, FSlateBrush> UseIconBrushes;
	TMap<FName, FSlateBrush> HudArtBrushes;
	FSlateBrush LeftContrastBrush;
	FSlateBrush RightContrastBrush;
	bool bUseIconsLoadAttempted = false;
	bool bContrastVeilsBuilt = false;
};
