#pragma once

#include "ElysiumViewState.h"
#include "UI/ElysiumNavigableScreen.h"

#include "ElysiumTerminalScreen.generated.h"

class UElysiumActionButton;
class UElysiumTerminalInputWidget;

// The five things retail's `hackcmd` channel can carry, as delegates. `docs/vtmb/computer-
// terminals.md` §8.1: the client's key handler picks one string and the ConCommand takes it from
// there, so the screen never invents a command and the player-UI layer binds each of these to the
// matching intent on `UElysiumPresentationSubsystem`.
DECLARE_DELEGATE_RetVal_ThreeParams(bool, FElysiumTerminalCommandDelegate,
	const FElysiumEntityHandle&, uint32, const FString&);
DECLARE_DELEGATE_RetVal_ThreeParams(bool, FElysiumTerminalCharacterDelegate,
	const FElysiumEntityHandle&, uint32, TCHAR);
DECLARE_DELEGATE_RetVal_TwoParams(bool, FElysiumTerminalIntentDelegate,
	const FElysiumEntityHandle&, uint32);
// The local line, for the glass. Not an intent: nothing is submitted, the monitor is just told what
// the player has typed so far.
DECLARE_DELEGATE_TwoParams(FElysiumTerminalDraftDelegate,
	const FElysiumEntityHandle&, const FString&);

// Invisible CommonUI input owner for one physical computer.
//
// It owns focus, navigation and the keyboard leaf, and nothing else. The glass itself is
// `UElysiumTerminalProjection`, owned by `UElysiumPresentationSubsystem` for the life of the body:
// the monitor is drawn before any session opens and after every one closes, which no session-scoped
// widget could do. No terminal pixels are drawn in viewport space either way — including the typed
// line, which retail composes into the client's own cell buffer (§8.1, TERM13) and this port mirrors
// onto the projection through `OnDraft`.
UCLASS()
class UElysiumTerminalScreen final : public UElysiumNavigableScreen
{
	GENERATED_BODY()

public:
	UElysiumTerminalScreen();

	void ApplyTerminal(const FElysiumTerminalView& InTerminal);
	// The local line the keyboard leaf is holding. There is no setter: retail's line is built one
	// keystroke at a time and cleared by the server's redraw, and so is this one.
	FString GetDraftText() const;
	// The gamepad face button's fallback, and the Enter path's shape: the draft in line/password
	// mode, the acknowledge intent in acknowledge mode.
	bool SubmitDraft();

	FElysiumTerminalCommandDelegate OnCommand;
	FElysiumTerminalCharacterDelegate OnCharacter;
	FElysiumTerminalIntentDelegate OnAcknowledge;
	FElysiumTerminalIntentDelegate OnQuit;
	FElysiumTerminalIntentDelegate OnBreak;
	FElysiumTerminalDraftDelegate OnDraft;

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
	// Rebuild the semantic action set the authority published, and re-seat focus. The terminal's
	// pixels are the projection's; what is rebuilt here is navigation, not a surface.
	void RebuildActions();
	void BuildTerminalActions();
	TSharedRef<SWidget> BuildActionVisual(UElysiumActionButton& Action, const FText& Label);
	bool SubmitCommand(const FString& Command);
	void BindInputDelegates();
	void PublishDraft();

	FElysiumTerminalView Terminal;
	FName DefaultActionId;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumTerminalInputWidget> TerminalInput;
};
