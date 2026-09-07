#pragma once

#include "Components/EditableText.h"
#include "ElysiumViewState.h"
#include "UI/ElysiumNavigableScreen.h"

#include "ElysiumTerminalScreen.generated.h"

class UElysiumActionButton;

DECLARE_DELEGATE_RetVal_ThreeParams(bool, FElysiumTerminalCommandDelegate,
	const FElysiumEntityHandle&, uint32, const FString&);

// Invisible CommonUI input owner for one physical computer.
//
// It owns the local draft, focus and navigation, and nothing else. The glass itself is
// `UElysiumTerminalProjection`, owned by `UElysiumPresentationSubsystem` for the life of the body:
// the monitor is drawn before any session opens and after every one closes, which no session-scoped
// widget could do. No terminal pixels are drawn in viewport space either way.
UCLASS()
class UElysiumTerminalScreen final : public UElysiumNavigableScreen
{
	GENERATED_BODY()

public:
	UElysiumTerminalScreen();

	void ApplyTerminal(const FElysiumTerminalView& InTerminal);
	void SetDraftText(const FString& Text);
	FString GetDraftText() const;
	bool SubmitDraft();

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
	// Rebuild the semantic action set the authority published, and re-seat focus. The terminal's
	// pixels are the projection's; what is rebuilt here is navigation, not a surface.
	void RebuildActions();
	void BuildTerminalActions();
	TSharedRef<SWidget> BuildActionVisual(UElysiumActionButton& Action, const FText& Label);
	bool SubmitCommand(const FString& Command);

	FElysiumTerminalView Terminal;
	FName DefaultActionId;

	UPROPERTY(Transient)
	TObjectPtr<UEditableText> CommandEntry;

	bool bUpdatingDraft = false;
};
