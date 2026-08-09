#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "Styling/SlateBrush.h"

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
	void EnsureUseIconAtlas();
	void EnsureContrastVeils();
	const FSlateBrush* UseIconBrush() const;
	const FSlateBrush* UseBindingBrush() const;
	FText UseBindingText() const;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumHUDModel> Model;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> UseAtlas;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LeftContrastVeil;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RightContrastVeil;

	FSlateBrush UseRingBrush;
	mutable FSlateBrush CurrentUseBindingBrush;
	TMap<int32, FSlateBrush> UseIconBrushes;
	FSlateBrush LeftContrastBrush;
	FSlateBrush RightContrastBrush;
	bool bUseAtlasLoadAttempted = false;
	bool bContrastVeilsBuilt = false;
};
