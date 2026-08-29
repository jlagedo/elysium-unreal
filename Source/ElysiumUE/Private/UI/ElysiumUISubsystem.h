#pragma once

#include "CoreMinimal.h"
#include "ElysiumCommands.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ElysiumUISubsystem.generated.h"

class UElysiumMainMenu;
class UElysiumCharacterScreen;

// Which body the character screen shows. Retail splits these across three client.dll classes
// (`VCharWizardUI`, `CharEditPanel`, `QuestLogPanel`) that share art and layout but not code; this
// rebuild carries one screen and switches the body (`docs/vtmb/vtmb-ui.md`). `Base` belongs to chargen.
enum class EElysiumCharacterTab : uint8
{
	Sheet,
	Info,
	QuestLog,
	Base,
};

// Which item set the menu screen is showing. Retail's own main/pause split is a single gate on
// `IsInGame` (`docs/vtmb/vtmb-ui.md` §2); GameOver is the third, reached from the app state machine's
// loss condition and offering only Load / Main Menu / Quit.
enum class EElysiumMenuMode : uint8
{
	Main,
	Pause,
	GameOver,
};

// GameInstance-scoped UI facade. Game flow decides when policy-owned screens exist; this object
// prepares per-open screen state and forwards presentation to the local player's one UI root.
//
// The screens are `UCommonActivatableWidget`s built in C++ Slate. The local-player subsystem owns
// their CommonUI containers and the screen base owns its Elysium input scope. *When* a menu is up
// is not this facade's call: `UElysiumGameFlowSubsystem` drives it from app state. Verbs:
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
	// `elysium.MenuScrim`) are read when the tree is built, so a console change has to take the
	// screen down and put it back up; no-op when no screen is up.
	void RebuildMenu();
	bool IsMenuOpen() const { return Menu != nullptr; }
	bool IsModalScreenOpen() const
	{
		return Menu != nullptr || CharacterScreen != nullptr || ChargenPopup != nullptr;
	}
	EElysiumMenuMode MenuMode() const { return CurrentMode; }

	// The character screen — one screen the whole game reuses, entered on a tab. `L` and `C` are two
	// doors into it, and showing it while it is already up switches tab rather than rebuilding.
	void ShowCharacterScreen(EElysiumCharacterTab Tab);
	void HideCharacterScreen();

	// Character creation — the same screen with the Base tab in front of the Sheet, the pools as its
	// currency and the name editable. Raised by the `createplayer` verb, which the genesis map's
	// `newplayer` trigger reaches through `ccmd.createplayer`, and by `elysium.chargen`.
	void ShowChargen();
	// The quiz's answer handler: apply the choice, then either redraw the popup or — when the chain
	// ends — take it down and open the sheet with what it decided already on it.
	void AnswerChargenPopup(int32 Index);
	// Raise the sheet on the pending character - the second half of both routes.
	void OpenChargenSheet();
	// Chargen's ACCEPT — land the character through the game state's own funnel, then close.
	void CommitChargen();
	// The screen's ACCEPT on the in-game level-up sheet: write the scratch back onto the character,
	// then close. Chargen's own commit is the chargen door's, because it also lands a clan, a sex, a
	// History and a name (`Substrate/ElysiumChargen.h`).
	void CommitCharacterSpend();
	// Re-point the stage's body at whatever clan/sex the screen is now showing.
	void UpdateCharacterStageBody();
	bool IsCharacterScreenOpen() const { return CharacterScreen != nullptr; }

	// Close the topmost screen the PLAYER opened, and report whether there was one. This is what
	// Escape asks first (`docs/vtmb/controls.md`: "close panel / open menu").
	//
	// The menu is deliberately NOT closable this way: its lifetime belongs to
	// `UElysiumGameFlowSubsystem`'s app state, so tearing it down here would leave a paused run with
	// no way back. Escape falls through to the pause toggle instead, which takes the menu with it.
	bool CloseTopScreen();

private:
	// `questlog` / `chareditor` — declared and key-bound already (`ElysiumCommands.cpp`,
	// `ElysiumBinds.cpp`); this subsystem supplies what they do, because it owns the screen.
	void RegisterCommands();
	void UnregisterCommands();
	// Close the character screen and its stage before the world the stage's actors live in goes away.
	void OnMapEpochRetired(uint64 Epoch);
	// Drop the wizard's pause latch without running its `teleport_player firetrans` close tail. What
	// a teardown wants; a panel close goes through HideCharacterScreen's own tail instead.
	void ReleaseChargenHold();

	UPROPERTY(Transient)
	TObjectPtr<UElysiumMainMenu> Menu;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumCharacterScreen> CharacterScreen;
	// The body + backdrop behind the screen's panels. Plain C++ rather than a UObject: it owns
	// transient actors and its own GC roots (`UI/ElysiumCharacterStage.h`).
	TSharedPtr<class FElysiumCharacterStage> CharacterStage;

	// The wizard's entry popup and question chain. Alive only between `createplayer` and the moment
	// the chain ends; the sheet is what follows it.
	UPROPERTY(Transient)
	TObjectPtr<class UElysiumChargenPopup> ChargenPopup;
	TSharedPtr<struct FElysiumWizRun> ChargenRun;
	TSharedPtr<struct FElysiumChargenState> PendingChargen;
	// The chargen host alone holds the world. The in-game Sheet/Quest/Info host shares the same
	// close function, so this latch is the boundary that keeps those screens from unpausing or
	// teleporting the player when they close.
	bool bChargenHold = false;

	EElysiumMenuMode CurrentMode = EElysiumMenuMode::Main;

	TArray<IConsoleObject*> ConsoleObjects;
	TArray<FElysiumCommandBinding> Bindings;
	FDelegateHandle MapEpochRetiredHandle;
};
