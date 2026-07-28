#pragma once

#include "CommonActivatableWidget.h"
#include "CoreMinimal.h"

#include "UI/ElysiumUISubsystem.h"

#include "ElysiumCharacterScreen.generated.h"

class UTexture2D;
struct FSlateBrush;

namespace ElysiumQuestView { struct FEntry; }

// What a dot costs when the player clicks it. **Not** "editable vs read-only": the Sheet body is
// editable in both modes — chargen spends the category pools, the in-game sheet spends experience —
// and only the currency and the ceiling differ (`docs/vtmb-ui.md`). `None` is Info and Quest Log.
enum class EElysiumSpendMode : uint8
{
	None,
	Chargen,    // the tier pools, priced by RE25's table (9.4f)
	LevelUp,    // the XP total, priced the same way against `Experience`
};

// How the shell is dressed for its host. The four axes are exactly what the retail captures differ
// on between the chargen wizard and the in-game screen; everything else about the screen is shared.
struct FElysiumCharacterScreenMode
{
	TArray<EElysiumCharacterTab> Tabs;
	EElysiumSpendMode Spend = EElysiumSpendMode::LevelUp;
	bool bNameEditable = false;   // the `NAME:` text entry — chargen only
};

// The character screen: one screen, entered on a tab (roadmap 9.4e). `L` opens it on the quest log,
// `C` on the sheet, and pressing the other key while it is up switches tab rather than closing —
// which is what makes them two doors into one screen rather than two screens.
//
// A `UCommonActivatableWidget` whose tree is built in C++ Slate under one `SDPIScaler` at
// `ScreenH/768`, like the menu (`docs/ui-architecture.md`). The chrome is VtMB's own decoded sheet
// art, each piece guarded: `out/ui/art/` is gitignored, so every image degrades to a token-drawn
// equivalent rather than leaving a hole.
//
// **Sheet and Info are framed placeholders.** They draw their real panels and headings with a line
// naming the task that fills them; the shell exists so those bodies — and chargen's — drop in.
UCLASS()
class UElysiumCharacterScreen : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UElysiumCharacterScreen();

	// Call before the widget is constructed, or call and then rebuild.
	void SetMode(const FElysiumCharacterScreenMode& InMode) { Mode = InMode; }
	void SetActiveTab(EElysiumCharacterTab Tab);
	EElysiumCharacterTab ActiveTab() const { return Tab; }

	// Swap the tab strip, footer and body for the current tab/hub, in place. Everything that changes
	// what the screen shows goes through here rather than through a teardown.
	void Refresh();

	// The screen is about to be torn down. Marks the hub the player was looking at as read — the
	// last chance to, since a hub change is what does it otherwise.
	void NotifyClosing();

	// The hub the quest log is showing. Persisted on the player record as VtMB's
	// `m_iCurrQuestLogArea`, so it is read at open and written on every change.
	void SetHub(int32 InHub);

	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

	// Empty, deliberately: CommonUI's action router must not become a second owner of the input
	// mode — the scope stack is the only one (11.5), same as the menu.
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override
	{
		return TOptional<FUIInputConfig>();
	}

private:
	float VirtualScale() const;

	// --- art ------------------------------------------------------------------------------------
	// One cache rather than a member per image: the screen draws a dozen pieces and they all want
	// the same load-once-guard-everywhere treatment. Returns null when the file is absent, which
	// every caller handles by drawing the token version instead.
	// `Uv` selects a sub-rectangle of the page — every one of these textures is a power-of-two page
	// with the art in one corner, and several carry two usable pieces (the divider's two curled
	// ends). The whole page is the default.
	const FSlateBrush* Art(const TCHAR* RelPath, const FLinearColor& Tint,
	                       const FBox2f& Uv = FBox2f(FVector2f::ZeroVector, FVector2f::UnitVector),
	                       const TCHAR* Variant = nullptr);
	// A framed panel out of one of the sheet's window textures, 9-sliced so the corner scrolls do
	// not stretch. Falls back to a hairline border.
	TSharedRef<SWidget> Framed(const TCHAR* RelPath, const FBox2f& Uv, const FMargin& Slice,
	                           TSharedRef<SWidget> Content);

	// --- the shell ------------------------------------------------------------------------------
	TSharedRef<SWidget> BuildHeader();
	TSharedRef<SWidget> BuildTabStrip();
	TSharedRef<SWidget> BuildFooter();
	TSharedRef<SWidget> BuildBody();
	TSharedRef<SWidget> BuildRule(const FText& Label);
	TSharedRef<SWidget> BuildPlaceholder(const FText& Heading, const FText& Line);

	// --- the quest log --------------------------------------------------------------------------
	TSharedRef<SWidget> BuildQuestLog();
	TSharedRef<SWidget> BuildHubRow(const int32* HubActive);
	TSharedRef<SWidget> BuildEntry(const ElysiumQuestView::FEntry& Entry, bool bLedger);

	FElysiumCharacterScreenMode Mode;
	EElysiumCharacterTab Tab = EElysiumCharacterTab::QuestLog;
	int32 Hub = INDEX_NONE;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> ArtTextures;

	TMap<FString, TSharedPtr<FSlateBrush>> ArtBrushes;
	// A miss is cached so a rebuild does not re-hit the disk for a file that is not there.
	TSet<FString> ArtMissing;

	// The three regions `Refresh` swaps. Held weakly-by-shared-ptr on the widget and reset in
	// `ReleaseSlateResources` with the brushes.
	TSharedPtr<class SBox> TabStripHost;
	TSharedPtr<class SBox> BodyHost;
	TSharedPtr<class SBox> FooterHost;
};
