#pragma once

#include "Components/Widget.h"
#include "ElysiumViewState.h"

#include "ElysiumTerminalInputWidget.generated.h"

class SElysiumTerminalInput;

// The UMG host for `SElysiumTerminalInput`.
//
// It exists for one reason: `UCommonActivatableWidget::GetDesiredFocusTarget` answers with a
// `UWidget*`, so the leaf that owns the keyboard has to be reachable as one. Everything below is a
// forward to the Slate leaf; this class holds no terminal state of its own.
UCLASS()
class UElysiumTerminalInputWidget final : public UWidget
{
	GENERATED_BODY()

public:
	void SetSession(const FElysiumTerminalView& View);
	FString GetDraft() const;
	void ClearDraft();

	// Bound by the owning screen before the leaf is built, so a rebuild re-attaches them.
	TDelegate<bool(const FString&)> OnSubmitLine;
	TDelegate<bool(TCHAR)> OnSubmitCharacter;
	FSimpleDelegate OnAcknowledge;
	FSimpleDelegate OnQuit;
	FSimpleDelegate OnBreak;
	FSimpleDelegate OnDraftChanged;

	TSharedPtr<SElysiumTerminalInput> GetInput() const { return Input; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TSharedPtr<SElysiumTerminalInput> Input;
	// The last session applied while no leaf existed, replayed on the next rebuild so focus and the
	// input rules never depend on the order of `ApplyTerminal` and `RebuildWidget`.
	FElysiumTerminalView PendingSession;
};
