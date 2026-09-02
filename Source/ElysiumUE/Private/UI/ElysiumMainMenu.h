#pragma once

#include "CoreMinimal.h"

#include "UI/ElysiumNavigableScreen.h"
#include "UI/ElysiumUISubsystem.h"

#include "ElysiumMainMenu.generated.h"

class UTexture2D;
class UElysiumActionButton;
struct FSlateBrush;

// The commands `gamemenu.res` binds, reduced to the ones this rebuild can service. VtMB routes
// these through `RunMenuCommand`; the tokens and the item sets are RE'd in `docs/vtmb/vtmb-ui.md` §2.
UENUM()
enum class EElysiumMenuCommand : uint8
{
	NewGame,
	LoadGame,
	SaveGame,
	Reload,
	Continue,
	MainMenu,
	Options,
	Quit,
};

// Main / pause / game-over menu, driven by the app state machine. Visual tree is built in C++
// Slate — CommonUI supplies the activation stack, input routing and focus; no Widget Blueprint
// asset is involved (`docs/architecture/ui-architecture.md`).
//
// Two layouts, selected by `elysium.MenuLayout`, both authored in VtMB's 1024x768 virtual canvas
// under one `SDPIScaler` at `ScreenH/768`:
//
//  - Rail (1, default) — a right-hand rail over a veil that falls to nothing by mid-frame, so the
//    backdrop's own darkness carries the type and the lit half of the scene is never dimmed to
//    rescue it. Items rest in bone and arm in blood, marked by one tick sliding along the rail's
//    hairline; the seal behind them is a `mm_<clan>` sigil off the menu particle sheet. Why this
//    diverges from the recovered law: `docs/architecture/ui-architecture.md`.
//  - Column (0) — `CVMainMenu::PerformLayout`: every item sized to the widest label plus 20x4
//    virtual px, stacked at `pitch = height + 2`, the column centred, the whole screen behind it
//    knocked back by `UElysiumUISettings::MenuScrim` (Project Settings -> Elysium -> UI).
//
// Type is vector small caps, not a bitmap atlas.
UCLASS()
class UElysiumMainMenu : public UElysiumNavigableScreen
{
	GENERATED_BODY()

public:
	UElysiumMainMenu();

	// Which item set to build. Pause swaps to Continue/Reload/.../Main Menu and enables Save Game,
	// which is the *entire* main-menu-vs-pause difference in retail — `CBasePanel::OnThink` does
	// nothing but gate `SaveGame` on `IsInGame` (`docs/vtmb/vtmb-ui.md` §2). GameOver is ours: the run is
	// over, so only Load / Main Menu / Quit are offered and the title lockup is replaced by the
	// reason the run ended. Call before the widget is constructed.
	void SetMenuMode(EElysiumMenuMode InMode) { Mode = InMode; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	// Resolves which row is armed and eases the rail's tick onto it. Slate ticks widgets off the
	// application, not the world, so this keeps running while the world is held for a pause — which
	// is the state the pause menu is always in.
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	// Escape closes the pause menu (the same key that opened it) and is swallowed everywhere else a
	// menu is up, so it can never fall through to the game while a screen owns the screen. The
	// keyboard route exists because CommonUI's Back action is not the owner of this policy.
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
	virtual bool NativeOnHandleBackAction() override;

private:
	struct FMenuEntry
	{
		const TCHAR*         Token;      // VMainMenu_BTN_* — resolved against the authored table
		const TCHAR*         Fallback;   // retail English, used when the mirror is absent
		EElysiumMenuCommand  Command;
		bool                 bEnabled;
		// What the rail's caption says while this row is armed, or null for the rows that need no
		// explaining. It carries two jobs: why a drawn-but-dead row is dead, and what a row that
		// ends the run is about to do. Unused by the column layout.
		const TCHAR*         Caption = nullptr;
		// Start a new group above this row. The item set is not a peer list — an act, a ledger and
		// the exits — and the 14 virtual px gap is the only thing that says so.
		bool                 bGroupBreak = false;
		// The one act of its set (New Game / Continue), set larger than the rest.
		bool                 bPrimary = false;
	};

	TArray<FMenuEntry> BuildItemSet() const;
	void Run(EElysiumMenuCommand Command);

	// The two layouts. Both take the resolved labels so the item set is built exactly once.
	TSharedRef<SWidget> BuildRail(const TArray<FMenuEntry>& Items, const TArray<FText>& Labels);
	TSharedRef<SWidget> BuildClassic(const TArray<FMenuEntry>& Items, const TArray<FText>& Labels);

	// One row of the rail: a transparent button carrying hover/focus/click, whose label colour
	// reports its state. Records the row's virtual-canvas Y so the tick can be placed analytically
	// rather than by querying geometry.
	TSharedRef<SWidget> BuildRailRow(const FMenuEntry& Item, const FText& Label, int32 Index, float& RowTop);

	// The clan sigil this mode draws: nothing in the front end (no character exists yet), the PC's
	// own clan's imported `cm_clan_symbol` once a session does. Null when the asset is not imported.
	UTexture2D* ResolveSeal();

	// Viewport height in pixels -> the virtual-canvas scale. Reads the live viewport so a resize
	// re-scales without a rebuild.
	float VirtualScale() const;

	// The front end's static plate (`UElysiumUISettings::MenuWallpaper`) and title lockup (the
	// imported `interface/mainmenu/vtm_title`), held as UPROPERTYs for the widget's lifetime.
	// Pause/game-over modes leave WallpaperTexture unused and show the held game world.
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> WallpaperTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TitleTexture;

	// The rail's own art: the clan sigil asset, plus three code-authored alpha ramps (the veil,
	// the hairline, the tick's solid bar) whose colour comes from the brush tint.
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SealTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> VeilTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> HairTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BarTexture;

	TSharedPtr<FSlateBrush> WallpaperBrush;
	TSharedPtr<FSlateBrush> TitleBrush;
	TSharedPtr<FSlateBrush> ScrimBrush;
	TSharedPtr<FSlateBrush> SealBrush;
	TSharedPtr<FSlateBrush> VeilBrush;
	TSharedPtr<FSlateBrush> HairBrush;
	TSharedPtr<FSlateBrush> BarBrush;

	// The rail's rows, in draw order: the button to poll for hover/focus, and the row's top and
	// height in virtual px. Non-owning — the tree owns the buttons.
	struct FRailRow
	{
		TWeakObjectPtr<UElysiumActionButton> Button;
		float             Top = 0.0f;
		float             Height = 0.0f;
	};
	TArray<FRailRow>   RailRows;
	TArray<FMenuEntry> RailItems;    // captions, by the same index

	// Which row is armed, and where the tick has eased to. The armed row persists when the pointer
	// leaves the rail, so the marker reads as a cursor rather than a hover highlight.
	int32 ArmedIndex = 0;
	float TickTop = 0.0f;
	float TickHeight = 0.0f;
	bool  bTickSeeded = false;   // first tick snaps; every later move eases

	EElysiumMenuMode Mode = EElysiumMenuMode::Main;
};
