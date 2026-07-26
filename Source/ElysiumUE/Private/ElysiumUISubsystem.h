#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ElysiumUISubsystem.generated.h"

class UElysiumMainMenu;

// Owns the player-facing UI screens (roadmap 8.6). GI-scoped because the menu outlives any one
// world — it is up before the first map and survives the travel New Game triggers.
//
// The screens are `UCommonActivatableWidget`s built in C++ Slate; this subsystem is what creates,
// shows and tears them down, and what owns the input-mode switch while one is up. Verbs:
// `elysium.menu` / `elysium.menu.close`.
UCLASS()
class UElysiumUISubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Shows the main (bPauseMode = false) or pause (true) menu. Idempotent: showing the menu that is
	// already up does nothing.
	void ShowMenu(bool bPauseMode);
	void HideMenu();
	bool IsMenuOpen() const { return Menu != nullptr; }

private:
	// Puts the local player into UI-only input (mouse visible) while a screen is up, and restores
	// game input when it goes away. The menu is modal by construction — the world behind it is a
	// backdrop, not something the player can reach past it.
	void ApplyInputMode(bool bUIOnly);

	UPROPERTY(Transient)
	TObjectPtr<UElysiumMainMenu> Menu;

	TArray<IConsoleObject*> ConsoleObjects;
};
