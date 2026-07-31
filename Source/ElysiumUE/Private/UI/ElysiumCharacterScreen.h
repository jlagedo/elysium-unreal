#pragma once

#include "CommonActivatableWidget.h"
#include "CoreMinimal.h"

#include "Substrate/ElysiumChargen.h"
#include "UI/ElysiumUISubsystem.h"

#include "ElysiumCharacterScreen.generated.h"

class UTexture2D;
struct FSlateBrush;

namespace ElysiumQuestView { struct FEntry; }

// What a dot costs when the player clicks it. **Not** "editable vs read-only": the Sheet body is
// editable in both modes — chargen spends the category pools, the in-game sheet spends experience —
// and only the currency and the ceiling differ (`docs/vtmb/vtmb-ui.md`). `None` is Info and Quest Log.
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
// `ScreenH/768`, like the menu (`docs/architecture/ui-architecture.md`). The chrome is VtMB's own decoded sheet
// art, each piece guarded: `out/ui/art/` is gitignored, so every image degrades to a token-drawn
// equivalent rather than leaving a hole.
//
// **The Sheet body is one widget serving both hosts.** Chargen and the in-game level-up screen draw
// the same rows, the same feats panel and the same detail panel over the same
// `FElysiumChargenState`; only `Mode.Spend` differs, and it selects which currency the state spends
// (`EElysiumChargenCurrency`). The spend state is a scratch — nothing reaches the character until
// ACCEPT — so CANCEL is a discard rather than an undo log.
UCLASS()
class UElysiumCharacterScreen : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UElysiumCharacterScreen();

	// Call before the widget is constructed, or call and then rebuild.
	void SetMode(const FElysiumCharacterScreenMode& InMode) { Mode = InMode; }

	// The scratch the Sheet body edits. The host builds it — chargen from `ApplyBaseline`, the
	// in-game screen from `BeginLevelUp` — and owns what ACCEPT does with it. Null leaves the Sheet
	// tab reading the live character with nothing buyable.
	void SetSpendState(TSharedPtr<FElysiumChargenState> InState);
	TSharedPtr<FElysiumChargenState> SpendState() const { return Spend; }

	// Fired when the footer's ACCEPT / NEXT / CANCEL is pressed. The screen never commits and never
	// closes itself: the host decides, because what a commit means differs between the two.
	DECLARE_DELEGATE(FOnCharacterScreenAction);
	FOnCharacterScreenAction OnAccept;
	FOnCharacterScreenAction OnCancel;
	// The character's clan or sex changed, so the body behind the panels is a different one. Fired
	// by the Base tab and by `SetSpendState`; the host owns the stage, because the in-game screen
	// raises the same one.
	FOnCharacterScreenAction OnCharacterChanged;
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

	// --- the sheet ------------------------------------------------------------------------------
	// The rulebook gathered for the model layer, and the sheet the body reads — the scratch when
	// there is one, the live character otherwise.
	FElysiumChargenRules SpendRules() const;
	const FElysiumSheet& ViewSheet() const;
	const FElysiumSheetEffects* ViewEffects() const;

	TSharedRef<SWidget> BuildSheet();
	// One heading plus its rows. `Pool` is `None` for a block that spends nothing (nothing does
	// today, but the parameter is what keeps the heading's counter optional rather than implied).
	TSharedRef<SWidget> BuildTraitBlock(EElysiumChargenPool Pool, EElysiumTraitContainer Container,
	                                    int32 First, int32 Last);
	TSharedRef<SWidget> BuildTraitRow(EElysiumTraitContainer Container, int32 TraitSlot);
	TSharedRef<SWidget> BuildBubbles(EElysiumTraitContainer Container, int32 TraitSlot);
	TSharedRef<SWidget> BuildFeats();
	TSharedRef<SWidget> BuildDetail();

	// --- the Base tab ---------------------------------------------------------------------------
	TSharedRef<SWidget> BuildBase();
	TSharedRef<SWidget> BuildChoiceRow(const FText& Heading, const TArray<FText>& Options,
	                                   int32 Selected, TFunction<void(int32)> OnPick);

	void Select(EElysiumTraitContainer Container, int32 TraitSlot);
	void TryBuy(EElysiumTraitContainer Container, int32 TraitSlot);
	void TrySell(EElysiumTraitContainer Container, int32 TraitSlot);
	// Re-derive the whole state after a clan / sex / history change, then redraw.
	void RebuildBaseline();

	FElysiumCharacterScreenMode Mode;
	EElysiumCharacterTab Tab = EElysiumCharacterTab::QuestLog;
	int32 Hub = INDEX_NONE;

	TSharedPtr<FElysiumChargenState> Spend;
	// The row the detail panel describes. Attributes/Strength is the retail default — the first row
	// of the first block, so the panel is never empty.
	EElysiumTraitContainer SelContainer = EElysiumTraitContainer::Attributes;
	int32 SelSlot = 1;

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
