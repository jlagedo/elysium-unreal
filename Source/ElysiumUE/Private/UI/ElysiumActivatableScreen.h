#pragma once

#include "CommonActivatableWidget.h"
#include "ElysiumInputScope.h"

#include "ElysiumActivatableScreen.generated.h"

UENUM()
enum class EElysiumUIScreenKind : uint8
{
	Menu,
	Character,
	Dialogue,
	Loot,
	Terminal,
	Chargen,
	Sign,
};

// Common lifecycle for every interactive screen hosted by the player UI root. CommonUI owns
// activation, focus restoration, navigation and Back routing; Elysium's input scope remains the
// sole authority that may call SetInputMode or change gameplay input contexts.
UCLASS(Abstract)
class UElysiumActivatableScreen : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	// Call from the container's pre-activation initializer on every push. This is the single policy
	// table for interactive screens; callers select a semantic kind rather than rebuilding a scope.
	void ConfigureScreenPolicy(EElysiumUIScreenKind Kind);
	void ClearInputScope();

protected:
	UElysiumActivatableScreen();

	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override final
	{
		return TOptional<FUIInputConfig>();
	}

	// A dynamic screen rebuild must update both CommonUI's desired target and the input mode's Slate
	// focus widget. Keeping the two names identical prevents focus returning to a dead turn widget.
	void RefreshInputFocusTarget(UWidget* Widget);

private:
	void PopInputScope();

	FElysiumInputScope InputScope;
	FElysiumInputScopeHandle InputScopeHandle;
	bool bHasInputScope = false;
};
