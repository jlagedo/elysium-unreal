#pragma once

#include "Components/EditableText.h"
#include "ElysiumViewState.h"
#include "Slate/WidgetRenderer.h"
#include "UI/ElysiumNavigableScreen.h"

#include "ElysiumTerminalScreen.generated.h"

class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UTextureRenderTarget2D;
class UElysiumActionButton;

DECLARE_DELEGATE_RetVal_ThreeParams(bool, FElysiumTerminalCommandDelegate,
	const FElysiumEntityHandle&, uint32, const FString&);

// Invisible CommonUI input owner for one physical computer. Its retained Slate surface is drawn
// into a render target bound to the model's exact `screen` material slot; no terminal pixels are
// drawn in viewport space. Gameplay owns the rows/actions, while this object owns only the local
// draft, focus/navigation, and the engine presentation resources.
UCLASS()
class UElysiumTerminalScreen final : public UElysiumNavigableScreen
{
	GENERATED_BODY()

public:
	UElysiumTerminalScreen();

	void ApplyTerminal(const FElysiumTerminalView& InTerminal);
	bool SetProjectionTarget(UPrimitiveComponent* InTarget);
	void EnterScreensaver();
	bool HasProjection() const;
	void SetDraftText(const FString& Text);
	FString GetDraftText() const;
	bool SubmitDraft();

	// Pure exact-match helper shared with focused tests. Similar names such as `screensaver` are
	// deliberately not accepted: only the authored `screen` material is a terminal surface.
	static int32 FindScreenMaterialSlot(const TArray<FName>& SlotNames);

	FElysiumTerminalCommandDelegate OnCommand;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,
		const FKeyEvent& KeyEvent) override;
	virtual bool NativeOnHandleBackAction() override;
	virtual bool HandleNavigation(EElysiumNavigationDirection Direction) override;
	virtual void HandleSelectedActionChanged(FName PreviousActionId,
		FName NewActionId) override;

private:
	UFUNCTION()
	void HandleDraftChanged(const FText& Text);

	UFUNCTION()
	void HandleDraftCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	void ConfigureEditor();
	void RebuildTerminalSurface();
	void RenderProjection();
	void ReleaseProjectionOwnership();
	TSharedRef<SWidget> BuildTerminalSurface();
	TSharedRef<SWidget> BuildScreensaverSurface() const;
	TSharedRef<SWidget> BuildActionVisual(UElysiumActionButton& Action, const FText& Label);
	bool SubmitCommand(const FString& Command);
	FText ScreenText() const;
	FText DraftDisplayText() const;

	FElysiumTerminalView Terminal;
	FName DefaultActionId;

	UPROPERTY(Transient)
	TObjectPtr<UEditableText> CommandEntry;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ProjectionMaterial;

	TWeakObjectPtr<UPrimitiveComponent> ProjectionTarget;
	TUniquePtr<FWidgetRenderer> WidgetRenderer;
	TSharedPtr<SWidget> TerminalSurface;
	int32 ProjectionMaterialIndex = INDEX_NONE;
	bool bUpdatingDraft = false;
};
