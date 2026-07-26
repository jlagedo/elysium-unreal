#pragma once

#include "CommonActivatableWidget.h"
#include "CoreMinimal.h"

#include "ElysiumUISubsystem.h"

#include "ElysiumMainMenu.generated.h"

class UTexture2D;
struct FSlateBrush;

// The commands `gamemenu.res` binds, reduced to the ones this rebuild can service. VtMB routes
// these through `RunMenuCommand`; the tokens and the item sets are RE'd in `docs/vtmb-ui.md` §2.
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

// The main / pause / game-over menu (roadmap 8.6, driven by the app state machine at 11.3). A
// `UCommonActivatableWidget` whose visual tree is built in C++ Slate — CommonUI supplies the
// activation stack, input routing and focus; no Widget Blueprint asset is involved
// (`docs/decisions.md` 2026-07-26).
//
// Layout reproduces `CVMainMenu::PerformLayout` in VtMB's own 1024x768 virtual canvas: every item
// is sized to the **widest** label plus 20x4 virtual px of padding, stacked at
// `pitch = height + 2`, and the column is centred. The whole tree sits under one `SDPIScaler` at
// `ScreenH/768`, so those recovered constants are the literal layout code at any resolution.
// What is *not* reproduced is the craft: vector small caps instead of a 640x480 bitmap atlas.
UCLASS()
class UElysiumMainMenu : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UElysiumMainMenu();

	// Which item set to build. Pause swaps to Continue/Reload/.../Main Menu and enables Save Game,
	// which is the *entire* main-menu-vs-pause difference in retail — `CBasePanel::OnThink` does
	// nothing but gate `SaveGame` on `IsInGame` (`docs/vtmb-ui.md` §2). GameOver is ours: the run is
	// over, so only Load / Main Menu / Quit are offered and the title lockup is replaced by the
	// reason the run ended. Call before the widget is constructed.
	void SetMenuMode(EElysiumMenuMode InMode) { Mode = InMode; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	// Escape closes the pause menu (the same key that opened it) and is swallowed everywhere else a
	// menu is up, so it can never fall through to the game while a screen owns the screen. The
	// keyboard route exists because CommonUI's Back action needs the `CommonUIInputData` asset that
	// 8.6 still owes; when that lands this becomes the back handler instead.
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
	struct FMenuEntry
	{
		const TCHAR*         Token;      // VMainMenu_BTN_* — resolved against the authored table
		const TCHAR*         Fallback;   // retail English, used when the mirror is absent
		EElysiumMenuCommand  Command;
		bool                 bEnabled;
	};

	TArray<FMenuEntry> BuildItemSet() const;
	void Run(EElysiumMenuCommand Command);

	// Viewport height in pixels -> the virtual-canvas scale. Reads the live viewport so a resize
	// re-scales without a rebuild.
	float VirtualScale() const;

	// The title lockup, decoded from the user's install (out/ui/menu/title.png). Held as a UPROPERTY
	// so the transient texture survives GC for the widget's lifetime.
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TitleTexture;

	TSharedPtr<FSlateBrush> TitleBrush;
	TSharedPtr<FSlateBrush> ScrimBrush;

	EElysiumMenuMode Mode = EElysiumMenuMode::Main;
};
