#include "ElysiumPlayerController.h"

#include "ElysiumCheatManager.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"

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

	// The three world verbs that used to sit on the pawn (11.4). `Use` and `ToggleSky` are named
	// actions in the legacy mapping set; the primary click is bound to its key directly, because
	// `DefaultInput.ini` has no primary-fire mapping and every VtMB popup instructs "left-click to
	// continue" — the binding is the panel's, not a weapon's.
	InputComponent->BindAction(TEXT("Use"), IE_Pressed, this, &AElysiumPlayerController::OnUsePressed);
	InputComponent->BindAction(TEXT("ToggleSky"), IE_Pressed, this, &AElysiumPlayerController::OnToggleSky);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AElysiumPlayerController::OnPrimaryClick);
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

void AElysiumPlayerController::OnUsePressed()
{
	// E doubles as noclip-ascend, but the pawn's vertical strafe only acts while noclipping, so in
	// normal play E is the use key. PlayerUse no-ops when the look-cursor is on nothing.
	if (FElysiumEntityWorld* World = CurrentEntityWorld())
	{
		World->PlayerUse();
	}
}

void AElysiumPlayerController::OnPrimaryClick()
{
	// Only meaningful while a sign is up; the world no-ops otherwise. MinShowTime holds the panel
	// briefly so a click already in flight when it opened cannot skip it (CSignUI's Rules block).
	if (FElysiumEntityWorld* World = CurrentEntityWorld())
	{
		World->PlayerDismissSign();
	}
}

void AElysiumPlayerController::OnToggleSky()
{
	if (AElysiumMapActor* Map = CurrentMap())
	{
		Map->ToggleSkybox();
	}
}

AElysiumMapActor* AElysiumPlayerController::CurrentMap() const
{
	if (AElysiumMapActor* Cached = CachedMap.Get())
	{
		return Cached;
	}
	const UGameInstance* GI = GetGameInstance();
	const UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	AElysiumMapActor* Map = Maps ? Maps->GetCurrentMap() : nullptr;
	CachedMap = Map;
	return Map;
}

FElysiumEntityWorld* AElysiumPlayerController::CurrentEntityWorld() const
{
	AElysiumMapActor* Map = CurrentMap();
	return Map ? Map->GetEntityWorld() : nullptr;
}
