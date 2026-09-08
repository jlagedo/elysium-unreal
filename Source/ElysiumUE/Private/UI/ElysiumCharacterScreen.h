#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumChargen.h"
#include "UI/ElysiumNavigableScreen.h"
#include "UI/ElysiumUiArtCache.h"
#include "UI/ElysiumUISubsystem.h"

#include "ElysiumCharacterScreen.generated.h"

struct FSlateBrush;

namespace ElysiumQuestView { struct FEntry; }

// What a dot costs when the player clicks it. **Not** "editable vs read-only": the Sheet body is
// editable in both modes — chargen spends the category pools, the in-game sheet spends experience —
// and only the currency and the ceiling differ (`docs/vtmb/vtmb-ui.md`). `None` is Info and Quest Log.
enum class EElysiumSpendMode : uint8
{
	None,
	Chargen,    // the tier pools, priced by the chargen table
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

// One screen entered on a tab. `L` opens it on the quest log, `C` on the sheet, and pressing the
// other key while it is up switches tab rather than closing — two doors into one screen.
//
// Tree is built in C++ Slate under one `SDPIScaler` at `ScreenH/768`. The chrome is VtMB's own
// decoded sheet art, each piece
// guarded: `out/ui/art/` is gitignored, so every image degrades to a token-drawn equivalent rather
// than leaving a hole.
//
// The Sheet body serves both hosts. Chargen and the in-game level-up screen draw the same rows, the
// same feats panel and the same detail panel over the same `FElysiumChargenState`; only
// `Mode.Spend` differs, and it selects which currency the state spends (`EElysiumChargenCurrency`).
// The spend state is a scratch — nothing reaches the character until ACCEPT — so CANCEL is a
// discard rather than an undo log.
UCLASS()
class UElysiumCharacterScreen : public UElysiumNavigableScreen
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
	virtual bool NativeOnHandleBackAction() override;
	virtual bool HandleNavigation(EElysiumNavigationDirection Direction) override;
	virtual void HandleSelectedActionChanged(FName PreviousActionId,
		FName NewActionId) override;

private:
	float VirtualScale() const;

	// Thin forwarders into the screen's art cache, so every builder draws through one name.
	const FSlateBrush* Art(const TCHAR* RelPath, const FLinearColor& Tint,
	                       const FBox2f& Uv = FBox2f(FVector2f::ZeroVector, FVector2f::UnitVector),
	                       const TCHAR* Variant = nullptr)
	{
		return ArtCache.Art(RelPath, Tint, Uv, Variant);
	}
	TSharedRef<SWidget> Framed(const TCHAR* RelPath, const FBox2f& Uv, const FMargin& Slice,
	                           TSharedRef<SWidget> Content)
	{
		return ArtCache.Framed(RelPath, Uv, Slice, Content);
	}

	TSharedRef<SWidget> BuildHeader();
	TSharedRef<SWidget> BuildTabStrip();
	TSharedRef<SWidget> BuildFooter();
	TSharedRef<SWidget> BuildBody();
	TSharedRef<SWidget> BuildRule(const FText& Label);
	TSharedRef<SWidget> BuildPlaceholder(const FText& Heading, const FText& Line);

	TSharedRef<SWidget> BuildQuestLog();
	TSharedRef<SWidget> BuildHubRow(const int32* HubActive);
	TSharedRef<SWidget> BuildEntry(const ElysiumQuestView::FEntry& Entry, bool bLedger);

	// The rulebook gathered for the model layer, and the sheet the body reads — the scratch when
	// there is one, the live character otherwise.
	FElysiumChargenRules SpendRules() const;
	const FElysiumSheet& ViewSheet() const;
	const FElysiumSheetEffects* ViewEffects() const;

	TSharedRef<SWidget> BuildSheet();
	// One heading plus its rows. `Pool` is `None` for a block that spends nothing (the parameter
	// keeps the heading's counter optional rather than implied).
	TSharedRef<SWidget> BuildTraitBlock(EElysiumChargenPool Pool, EElysiumTraitContainer Container,
	                                    int32 First, int32 Last);
	TSharedRef<SWidget> BuildTraitRow(EElysiumTraitContainer Container, int32 TraitSlot);
	TSharedRef<SWidget> BuildBubbles(EElysiumTraitContainer Container, int32 TraitSlot);
	TSharedRef<SWidget> BuildFeats();
	TSharedRef<SWidget> BuildDetail();

	TSharedRef<SWidget> BuildBase();
	TSharedRef<SWidget> BuildChoiceRow(FName GroupId, const FText& Heading,
	                                   const TArray<FText>& Options,
	                                   int32 Selected, TFunction<void(int32)> OnPick);

	// The screen's navigation groups, declared identically on every navigation build — the full
	// rebuild and the in-place refresh share this one registration.
	void RegisterNavigationGroups();

	void Select(EElysiumTraitContainer Container, int32 TraitSlot);
	void TryBuy(EElysiumTraitContainer Container, int32 TraitSlot);
	void TrySell(EElysiumTraitContainer Container, int32 TraitSlot);
	// Re-derive the whole state after a clan / sex / history change, then redraw.
	void RebuildBaseline();
	void CycleTab(int32 Delta);
	FName FirstBodyAction() const;
	FName LastBodyAction() const;

	FElysiumCharacterScreenMode Mode;
	EElysiumCharacterTab Tab = EElysiumCharacterTab::QuestLog;
	int32 Hub = INDEX_NONE;

	TSharedPtr<FElysiumChargenState> Spend;
	// The row the detail panel describes. Attributes/Strength is the retail default — the first row
	// of the first block, so the panel is never empty.
	EElysiumTraitContainer SelContainer = EElysiumTraitContainer::Attributes;
	int32 SelSlot = 1;

	// The decoded sheet chrome, loaded once and guarded everywhere.
	FElysiumUiArtCache ArtCache;

	// The three regions `Refresh` swaps. Held weakly-by-shared-ptr on the widget and reset in
	// `ReleaseSlateResources` with the brushes.
	TSharedPtr<class SBox> TabStripHost;
	TSharedPtr<class SBox> BodyHost;
	TSharedPtr<class SBox> FooterHost;
	TSharedPtr<class SBox> DetailHost;

	struct FTraitAction
	{
		EElysiumTraitContainer Container = EElysiumTraitContainer::Attributes;
		int32 Slot = INDEX_NONE;
	};
	TMap<FName, FTraitAction> TraitActions;
};
