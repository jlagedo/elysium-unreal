#pragma once

#include "CoreMinimal.h"
#include "ElysiumCommands.h"
#include "ElysiumInputScope.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ElysiumUISubsystem.generated.h"

class UElysiumMainMenu;
class UElysiumCharacterScreen;

// Which body the character screen shows. Retail splits these across three client.dll classes
// (`VCharWizardUI`, `CharEditPanel`, `QuestLogPanel`) that share art and layout but not code; we
// carry one screen and switch the body, which is what the player sees either way
// (`docs/vtmb-ui.md`). `Base` belongs to chargen (9.4f) and is not built yet.
enum class EElysiumCharacterTab : uint8
{
	Sheet,
	Info,
	QuestLog,
	Base,
};

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

	// Rebuild the open screen in place, keeping its mode. The look knobs (`elysium.MenuLayout`,
	// `elysium.MenuScrim`) are read when the tree is built, so an A/B typed at the console has to
	// take the screen down and put it back up; no-op when no screen is up.
	void RebuildMenu();
	bool IsMenuOpen() const { return Menu != nullptr; }
	EElysiumMenuMode MenuMode() const { return CurrentMode; }

	// The character screen — one screen the whole game reuses, entered on a tab. `L` and `C` are two
	// doors into it, and showing it while it is already up switches tab rather than rebuilding.
	void ShowCharacterScreen(EElysiumCharacterTab Tab);
	void HideCharacterScreen();
	bool IsCharacterScreenOpen() const { return CharacterScreen != nullptr; }

	// Close the topmost screen the PLAYER opened, and report whether there was one. This is what
	// Escape asks first (`controls.md`: "close panel / open menu").
	//
	// The menu is deliberately NOT closable this way: its lifetime belongs to
	// `UElysiumGameFlowSubsystem`'s app state, so tearing it down here would leave a paused run with
	// no way back. Escape falls through to the pause toggle instead, which takes the menu with it.
	bool CloseTopScreen();

private:
	// Claim input for the screen while it is up, and release it when it goes away (11.5). The menu
	// is modal by construction — the world behind it is a backdrop, not something the player can
	// reach past it — so the scope is UI-only with the cursor shown.
	//
	// Focus is handed to the menu widget itself, not left on the game viewport: that is what lets
	// the screen see Escape (`UElysiumMainMenu::NativeOnKeyDown`) and close itself. The subsystem
	// no longer calls SetInputMode, and a UI-only push is also what takes an inherited Cog capture
	// back, so a menu can never open under a debug UI that eats its clicks.
	void PushMenuScope();
	void PopMenuScope();

	void PushCharacterScope();
	void PopCharacterScope();

	// `questlog` / `chareditor` — declared and key-bound already (`ElysiumCommands.cpp`,
	// `ElysiumBinds.cpp`); this subsystem supplies what they do, because it owns the screen.
	void RegisterCommands();
	void UnregisterCommands();

	UPROPERTY(Transient)
	TObjectPtr<UElysiumMainMenu> Menu;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumCharacterScreen> CharacterScreen;

	EElysiumMenuMode CurrentMode = EElysiumMenuMode::Main;
	FElysiumInputScopeHandle MenuScope;
	FElysiumInputScopeHandle CharacterScope;

	TArray<IConsoleObject*> ConsoleObjects;
	TArray<FElysiumCommandBinding> Bindings;
};
