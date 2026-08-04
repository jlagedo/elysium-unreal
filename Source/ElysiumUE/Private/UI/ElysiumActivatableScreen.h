#pragma once

#include "CommonActivatableWidget.h"
#include "ElysiumInputScope.h"

#include "ElysiumActivatableScreen.generated.h"

// Common lifecycle for every interactive screen hosted by the player UI root. CommonUI owns
// activation, focus restoration, navigation and Back routing; Elysium's input scope remains the
// sole authority that may call SetInputMode or change gameplay input contexts.
UCLASS(Abstract)
class UElysiumActivatableScreen : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	// Call from the container's pre-activation initializer on every push. Pooled widgets do not
	// promise to retain prior state, so this deliberately overwrites the whole screen input policy.
	void ConfigureInputScope(
		FName Name,
		int32 ScopePriority,
		EElysiumInputMode Mode = EElysiumInputMode::UIOnly,
		bool bShowCursor = true);
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

private:
	void PopInputScope();

	FElysiumInputScope InputScope;
	FElysiumInputScopeHandle InputScopeHandle;
	bool bHasInputScope = false;
};
