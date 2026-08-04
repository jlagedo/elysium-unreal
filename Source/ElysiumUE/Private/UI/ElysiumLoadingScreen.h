#pragma once

#include "UI/ElysiumActivatableScreen.h"

#include "ElysiumLoadingScreen.generated.h"

namespace ElysiumLoadingUI
{
	// Pure-Slate loading visual shared by MoviePlayer and the post-load CommonUI screen. The
	// MoviePlayer copy must remain UObject-free because it renders while the game thread is blocked.
	TSharedRef<SWidget> Build(const FText& Message, bool bShowThrobber);
}

// Post-LoadMap loading surface. Unlike MoviePlayer's blocking copy, this lives in the unified
// local-player root while Elysium finishes collision, entity and player activation on normal ticks.
UCLASS()
class UElysiumLoadingScreen final : public UElysiumActivatableScreen
{
	GENERATED_BODY()

public:
	void SetLoadingState(const FText& InMessage, bool bInShowThrobber)
	{
		Message = InMessage;
		bShowThrobber = bInShowThrobber;
	}

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	FText Message;
	bool bShowThrobber = true;
};
