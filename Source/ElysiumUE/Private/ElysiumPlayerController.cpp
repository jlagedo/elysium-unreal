#include "ElysiumPlayerController.h"

#include "ElysiumCheatManager.h"
#include "ElysiumGameFlowSubsystem.h"

#include "Components/InputComponent.h"
#include "Engine/GameInstance.h"
#include "InputCoreTypes.h"

AElysiumPlayerController::AElysiumPlayerController()
{
	// The controller spawns this in non-Shipping / cheats-enabled builds; it is what makes the
	// Elysium (and inherited stock) UFUNCTION(exec) cheats reachable from the console.
	CheatClass = UElysiumCheatManager::StaticClass();
}

void AElysiumPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputComponent)
	{
		return;
	}

	// Bound to the key rather than to a named action: `DefaultInput.ini` is the legacy mapping set
	// 10.6 replaces wholesale, and Esc is a reserved key in every VtMB control scheme, so there is
	// nothing to rebind it to yet.
	FInputKeyBinding& Binding =
		InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AElysiumPlayerController::OnPauseKey);
	// Without this the key is dead exactly when it is needed most: the pause menu holds the world
	// through engine pause, and a paused world stops delivering input bindings.
	Binding.bExecuteWhenPaused = true;
}

void AElysiumPlayerController::OnPauseKey()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameFlowSubsystem* Flow = GI->GetSubsystem<UElysiumGameFlowSubsystem>())
		{
			// A no-op outside Playing/Paused: the front end deliberately does not pause, because the
			// live backdrop behind the menu is the feature.
			Flow->TogglePause();
		}
	}
}
