#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ElysiumUISubsystem.generated.h"

class UElysiumMainMenu;

// Which item set the menu screen is showing. Retail's own main/pause split is a single gate on
// `IsInGame` (`docs/vtmb-ui.md` §2); GameOver is the third, reached from the app state machine's
// loss condition (11.3) and offering only Load / Main Menu / Quit.
enum class EElysiumMenuMode : uint8
{
	Main,
	Pause,
	GameOver,
};

// Owns the player-facing UI screens (roadmap 8.6). GI-scoped because the menu outlives any one
// world — it is up before the first map and survives the travel New Game triggers.
//
// The screens are `UCommonActivatableWidget`s built in C++ Slate; this subsystem is what creates,
// shows and tears them down, and what owns the input-mode switch while one is up. *When* a screen
// is up is not its call: `UElysiumGameFlowSubsystem` drives it from the app state (11.3). Verbs:
// `elysium.menu` / `elysium.menu.close`.
UCLASS()
class UElysiumUISubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Shows the menu in the given mode. Idempotent for the same mode; a different mode rebuilds the
	// screen, so a run that ends while the pause menu is up swaps to the game-over item set.
	void ShowMenu(EElysiumMenuMode Mode);
	void HideMenu();
	bool IsMenuOpen() const { return Menu != nullptr; }
	EElysiumMenuMode MenuMode() const { return CurrentMode; }

private:
	// Puts the local player into UI-only input (mouse visible) while a screen is up, and restores
	// game input when it goes away. The menu is modal by construction — the world behind it is a
	// backdrop, not something the player can reach past it.
	//
	// Focus is handed to the menu widget itself, not left on the game viewport: that is what lets
	// the screen see Escape (`UElysiumMainMenu::NativeOnKeyDown`) and close itself. 11.5 replaces
	// this with the input scope stack; until then this is the one place the mode is set for a menu.
	void ApplyInputMode(bool bUIOnly);

	UPROPERTY(Transient)
	TObjectPtr<UElysiumMainMenu> Menu;

	EElysiumMenuMode CurrentMode = EElysiumMenuMode::Main;

	TArray<IConsoleObject*> ConsoleObjects;
};
