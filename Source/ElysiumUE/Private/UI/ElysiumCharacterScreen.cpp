#include "UI/ElysiumCharacterScreen.h"

#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumQuestTables.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "UI/ElysiumActionButton.h"
#include "UI/ElysiumUIStyle.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// Layout, in VtMB's 1024x768 virtual canvas. Horizontal values are measured from an edge, never
	// as a fraction of 1024: the canvas widens with the aspect ratio, so a fraction would drift the
	// content inward on ultrawide (`docs/architecture/ui-architecture.md` section 2).
	namespace Layout
	{
		inline constexpr float PadX        = 34.0f;   // from the left and right edges
		inline constexpr float HeaderTop   = 18.0f;
		inline constexpr float SigilSize   = 62.0f;
		inline constexpr float BubbleSize  = 15.0f;
		inline constexpr float BubbleGap   = 4.0f;
		// The mask cells tile edge to edge — their pitch in the art is their own width, so any gap
		// here would open a seam the page does not have.
		inline constexpr float MaskW       = 40.0f;
		inline constexpr float MaskH       = 38.0f;
		inline constexpr float TabGap      = 62.0f;
		inline constexpr float HubGap      = 54.0f;
		inline constexpr float ColumnGap   = 26.0f;
		inline constexpr float EntryGap    = 14.0f;
		inline constexpr float RuleCapW    = 15.0f;   // the divider's curled terminal
		inline constexpr float RuleCapH    = 13.0f;
		inline constexpr float FooterH     = 30.0f;
		inline constexpr float ChoiceGap   = 10.0f;
		inline constexpr float NameEntryW  = 200.0f;

		// The sheet: the trait columns take roughly two thirds and the detail panel the rest, with
		// the feats panel a narrower column than either trait column.
		inline constexpr float SheetRowsFill   = 2.1f;
		inline constexpr float SheetDetailFill = 1.0f;
		inline constexpr float FeatsFill       = 0.85f;

		// The reading column against the resolved ledger. Retail gives the active list roughly
		// three fifths and stacks completed over failed in the remainder.
		inline constexpr float ActiveFill  = 1.55f;
		inline constexpr float LedgerFill  = 1.0f;
	}

	// The decoded sheet chrome. Paths are relative to `out/ui/art/`.
	namespace ArtPath
	{
		inline const TCHAR* Backdrop  = TEXT("interface/charactermaintenance/background.png");
		inline const TCHAR* TopBar    = TEXT("interface/charactermaintenance/cm_topbar.png");
		inline const TCHAR* Divider   = TEXT("interface/charactermaintenance/cm_divider.png");
		inline const TCHAR* BubbleOn  = TEXT("interface/charactermaintenance/cm_bubble_filled.png");
		inline const TCHAR* BubbleOff = TEXT("interface/charactermaintenance/cm_bubble_empty.png");
		inline const TCHAR* Strike    = TEXT("interface/charactermaintenance/cm_masquerade_strike.png");
		// A dot bought this session but not yet committed, and a dot the trait-effect layer added on
		// top of the base. VtMB ships both as their own art, so the sheet distinguishes four states.
		inline const TCHAR* BubblePending = TEXT("interface/charactermaintenance/cm_bubble_pending.png");
		inline const TCHAR* BubbleBonus   = TEXT("interface/charactermaintenance/cm_bubble_bonus.png");
		inline const TCHAR* Leader        = TEXT("interface/charactermaintenance/cm_stat_underscore.png");
	}

	// A category's display name. The headings are data — `strings.txt` numbers its three attribute
	// groups and its three ability groups 0..2 apiece, and the disciplines take the last of
	// `StatCategoryTitles` — so the English below is only what a missing export falls back to.
	FString ElysiumChargenPoolLabel(const FElysiumChargenRules& Rules, EElysiumChargenPool Pool)
	{
		static const TCHAR* const Fallback[(uint8)EElysiumChargenPool::Count] =
		{
			TEXT("Physical"), TEXT("Social"), TEXT("Mental"),
			TEXT("Talents"), TEXT("Skills"), TEXT("Knowledges"), TEXT("Disciplines"),
		};
		if ((uint8)Pool >= (uint8)EElysiumChargenPool::Count)
		{
			return FString();
		}
		const FString Default(Fallback[(uint8)Pool]);
		if (Rules.Strings == nullptr)
		{
			return Default;
		}
		if (Pool == EElysiumChargenPool::Disciplines)
		{
			return Rules.Strings->At(TEXT("StatCategoryTitles"), 6, Default);
		}
		const int32 Index = ElysiumChargenPoolGroupIndex(Pool);
		const TCHAR* Group = ElysiumChargenPoolContainer(Pool) == EElysiumTraitContainer::Attributes
			? TEXT("AttributeGroup") : TEXT("AbilityGroup");
		return Rules.Strings->At(Group, Index, Default);
	}

	// The five Masquerade masks are painted into the right end of `cm_topbar` rather than shipped as
	// their own texture -- `client.dll` names every sheet material it loads and no mask is among them.
	// So the meter reads five sub-rectangles out of the header page: 43px pitch from centre 811,
	// rows 32-73 of 128, measured off the decoded PNG. They are not identical -- the artist painted a
	// brightening gradient left to right -- so each slot takes its own rect and the gradient survives.
	FBox2f MaskUv(int32 Slot)
	{
		constexpr float PageW = 1024.0f, PageH = 128.0f;
		const float Cx = 811.0f + 43.0f * float(Slot);
		return FBox2f(FVector2f((Cx - 21.5f) / PageW, 32.0f / PageH),
		              FVector2f((Cx + 21.5f) / PageW, 73.0f / PageH));
	}

	// A panel frame plus the sub-rectangle its art actually occupies. Every one of these textures is
	// a power-of-two page with the frame drawn top-left and the rest transparent, so the UV region is
	// the frame's own extent — measured off the decoded PNGs, not guessed. The margin is the corner
	// scroll's share of that extent, so a 9-slice stretches the rules and leaves the scrolls alone.
	struct FFrameArt
	{
		const TCHAR* Rel;
		FBox2f Uv;
		FMargin Slice;
	};

	const FFrameArt FrameActive
	{
		TEXT("interface/charactermaintenance/activequestwindow.png"),
		FBox2f(FVector2f(0.0f, 0.0f), FVector2f(0.5840f, 0.5479f)),
		FMargin(0.050f, 0.053f, 0.050f, 0.053f)
	};
	const FFrameArt FrameCompleted
	{
		TEXT("interface/charactermaintenance/completedquest.png"),
		FBox2f(FVector2f(0.0f, 0.0f), FVector2f(0.7090f, 0.9961f)),
		FMargin(0.083f, 0.118f, 0.083f, 0.118f)
	};
	const FFrameArt FrameFailed
	{
		TEXT("interface/charactermaintenance/failedquest.png"),
		FBox2f(FVector2f(0.0f, 0.0f), FVector2f(0.7090f, 0.9883f)),
		FMargin(0.083f, 0.119f, 0.083f, 0.119f)
	};
	const FFrameArt FrameInfo
	{
		TEXT("interface/charactermaintenance/infowindow2.png"),
		FBox2f(FVector2f(0.0f, 0.0f), FVector2f(0.9688f, 0.8223f)),
		FMargin(0.060f, 0.071f, 0.060f, 0.071f)
	};
	const FFrameArt FrameFeats
	{
		TEXT("interface/charactermaintenance/featwindow.png"),
		FBox2f(FVector2f(0.0469f, 0.0f), FVector2f(0.9531f, 1.0f)),
		FMargin(0.129f, 0.059f, 0.129f, 0.059f)
	};

	// `cm_clan_symbol_<stem>.png`. Indexed by VtMB's clan encoding, which starts at 2 — 0 and 1 are
	// unused, and a run with no clan yet flies nothing rather than flying Brujah.
	const TCHAR* ClanSigilStem(int32 Clan)
	{
		switch (Clan)
		{
		case 2:  return TEXT("brujah");
		case 3:  return TEXT("gangrel");
		case 4:  return TEXT("malkavian");
		case 5:  return TEXT("nosferatu");
		case 6:  return TEXT("toreador");
		case 7:  return TEXT("tremere");
		case 8:  return TEXT("ventrue");
		default: return nullptr;
		}
	}

	FText TabLabel(EElysiumCharacterTab Tab)
	{
		switch (Tab)
		{
		case EElysiumCharacterTab::Sheet:    return NSLOCTEXT("Elysium", "CharTabSheet", "Sheet");
		case EElysiumCharacterTab::Info:     return NSLOCTEXT("Elysium", "CharTabInfo", "Info");
		case EElysiumCharacterTab::QuestLog: return NSLOCTEXT("Elysium", "CharTabQuest", "Quest Log");
		case EElysiumCharacterTab::Base:     return NSLOCTEXT("Elysium", "CharTabBase", "Base");
		}
		return FText::GetEmpty();
	}

	// The four tables that are places, in the order the quest log lists them.
	FText HubLabel(int32 Table)
	{
		switch (Table)
		{
		case 4: return NSLOCTEXT("Elysium", "HubSantaMonica", "Santa Monica");
		case 1: return NSLOCTEXT("Elysium", "HubDowntown", "Downtown");
		case 2: return NSLOCTEXT("Elysium", "HubHollywood", "Hollywood");
		case 0: return NSLOCTEXT("Elysium", "HubChinatown", "Chinatown");
		default: return NSLOCTEXT("Elysium", "HubMain", "Main");
		}
	}

	namespace ActionGroup
	{
		const FName Tabs(TEXT("Character.Tabs"));
		const FName Hubs(TEXT("Character.Hubs"));
		const FName Traits(TEXT("Character.Traits"));
		const FName Footer(TEXT("Character.Footer"));
		const FName BaseClan(TEXT("Character.Base.Clan"));
		const FName BaseSex(TEXT("Character.Base.Sex"));
		const FName BaseHistory(TEXT("Character.Base.History"));
	}

	FName TabActionId(EElysiumCharacterTab Tab)
	{
		return FName(*FString::Printf(TEXT("Character.Tab.%d"), int32(Tab)));
	}

	FName HubActionId(int32 Hub)
	{
		return FName(*FString::Printf(TEXT("Character.Hub.%d"), Hub));
	}

	FName TraitActionId(EElysiumTraitContainer Container, int32 Slot)
	{
		return FName(*FString::Printf(TEXT("Character.Trait.%d.%d"), int32(Container), Slot));
	}

	FName BaseActionId(FName Group, int32 Option)
	{
		return FName(*FString::Printf(TEXT("%s.%d"), *Group.ToString(), Option));
	}

	UElysiumGameStateSubsystem* StateFor(const UWidget* Widget)
	{
		UGameInstance* GI = Widget ? Widget->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr;
	}

	// The one styling every selectable control shares: `On` while the row is its screen's current
	// choice (`bOn`) or the live keyboard selection, `Off` otherwise. Both colours are fixed at
	// build time; only the selection is read per frame.
	TAttribute<FSlateColor> SelectedColor(TWeakObjectPtr<UElysiumActionButton> Button, bool bOn,
		const FLinearColor& On, const FLinearColor& Off)
	{
		return TAttribute<FSlateColor>::CreateLambda([Button, bOn, On, Off]()
		{
			const UElysiumActionButton* Action = Button.Get();
			return FSlateColor(bOn || (Action && Action->IsActionSelected()) ? On : Off);
		});
	}
}

UElysiumCharacterScreen::UElysiumCharacterScreen()
{
	bIsBackHandler = true;
	Mode.Tabs = { EElysiumCharacterTab::Sheet, EElysiumCharacterTab::Info,
	              EElysiumCharacterTab::QuestLog };
}

bool UElysiumCharacterScreen::NativeOnHandleBackAction()
{
	if (!ExecuteAction(TEXT("Character.Footer.Cancel")))
	{
		OnCancel.ExecuteIfBound();
	}
	return true;
}

float UElysiumCharacterScreen::VirtualScale() const
{
	FVector2D Size(1920.0f, 1080.0f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	return ElysiumUI::ScaleFor(static_cast<float>(Size.Y));
}

void UElysiumCharacterScreen::SetActiveTab(EElysiumCharacterTab InTab)
{
	if (Tab == InTab)
	{
		return;
	}
	Tab = InTab;
	Refresh();
}

void UElysiumCharacterScreen::SetHub(int32 InHub)
{
	if (Hub == InHub)
	{
		return;
	}
	// Leaving a hub is what marks its rows read: the player has now seen them. Ours, not VtMB's —
	// the engine writes the unread byte and never reads it (`docs/architecture/ui-architecture.md`).
	if (UElysiumGameStateSubsystem* State = StateFor(this))
	{
		State->MarkQuestsRead(Hub);
		State->SetQuestLogArea(InHub);
	}
	Hub = InHub;
	Refresh();
}

void UElysiumCharacterScreen::NotifyClosing()
{
	if (Tab != EElysiumCharacterTab::QuestLog || Hub == INDEX_NONE)
	{
		return;
	}
	if (UElysiumGameStateSubsystem* State = StateFor(this))
	{
		State->MarkQuestsRead(Hub);
		State->SetQuestLogArea(Hub);
	}
}

void UElysiumCharacterScreen::Refresh()
{
	// The tab strip and the body are swapped in place rather than rebuilt through the subsystem: a
	// teardown from inside the screen's own key handler would destroy the widget mid-callback, and
	// it would drop keyboard focus and churn the input scope for what is a content change.
	if (TabStripHost.IsValid() || FooterHost.IsValid() || BodyHost.IsValid())
	{
		BeginNavigationBuild();
		TraitActions.Reset();
		RegisterNavigationGroups();
		if (TabStripHost.IsValid())
		{
			TabStripHost->SetContent(BuildTabStrip());
		}
		if (FooterHost.IsValid())
		{
			FooterHost->SetContent(BuildFooter());
		}
		if (BodyHost.IsValid())
		{
			BodyHost->SetContent(BuildBody());
		}
		FinalizeNavigationBuild(TabActionId(Tab));
	}
}

void UElysiumCharacterScreen::RegisterNavigationGroups()
{
	SetNavigationGroup(ActionGroup::Tabs, true, false);
	SetNavigationGroup(ActionGroup::Hubs, true, false);
	SetNavigationGroup(ActionGroup::Traits, false, true, true, false);
	SetNavigationGroup(ActionGroup::Footer, true, false);
	SetNavigationGroup(ActionGroup::BaseClan, true, false);
	SetNavigationGroup(ActionGroup::BaseSex, true, false);
	SetNavigationGroup(ActionGroup::BaseHistory, true, false);
}

// The shell.

TSharedRef<SWidget> UElysiumCharacterScreen::BuildRule(const FText& Label)
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();

	// `cm_divider` carries a curled terminal at BOTH ends, so the two caps are two sub-rectangles of
	// the one page rather than one image drawn twice with a mirror. Regions measured off the decoded
	// PNG (512x32, art at x 11..501, y 10..29). Absent art drops the caps and the rule alone carries
	// the section, which still reads.
	const FSlateBrush* CapL = Art(ArtPath::Divider, FLinearColor::White,
		FBox2f(FVector2f(0.0215f, 0.3125f), FVector2f(0.0723f, 0.9062f)), TEXT("capL"));
	const FSlateBrush* CapR = Art(ArtPath::Divider, FLinearColor::White,
		FBox2f(FVector2f(0.9277f, 0.3125f), FVector2f(0.9785f, 0.9062f)), TEXT("capR"));

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	auto AddCap = [&](const FSlateBrush* Cap)
	{
		if (!Cap)
		{
			return;
		}
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(Layout::RuleCapW).HeightOverride(Layout::RuleCapH)
			[
				SNew(SImage).Image(Cap)
			]
		];
	};

	auto AddRule = [&]()
	{
		Row->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(SBox).HeightOverride(1.0f)
			[
				SNew(SImage)
				.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Amber.CopyWithNewOpacity(0.45f)))
			]
		];
	};

	AddCap(CapL);
	AddRule();
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(ElysiumUI::Space::M, 0.0f)
	[
		SNew(STextBlock)
		.Text(Label)
		.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
		                 ElysiumUI::Type::Heading, 1.0f))
		.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::GoldLit))
	];
	AddRule();
	AddCap(CapR);

	return Row;
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildHeader()
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();
	UElysiumGameStateSubsystem* State = StateFor(this);

	// The header reads whatever the screen is editing: the scratch while one is open — so a clan
	// change on the Base tab reflags the sigil immediately — and the live character otherwise.
	const FElysiumSheet& Sheet = ViewSheet();
	const int32 Clan      = Sheet.Clan();
	const FString Name    = Spend.IsValid() ? Spend->Name : (State ? State->PlayerName() : FString());
	const int32 Humanity  = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Humanity);
	const int32 Masquerade = Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Masquerade);

	// Identity.
	TSharedRef<SHorizontalBox> Who = SNew(SHorizontalBox);
	if (const TCHAR* Stem = ClanSigilStem(Clan))
	{
		const FString Rel = FString::Printf(
			TEXT("interface/charactermaintenance/cm_clan_symbol_%s.png"), Stem);
		if (const FSlateBrush* Sigil = Art(*Rel, FLinearColor::White))
		{
			Who->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(Layout::SigilSize).HeightOverride(Layout::SigilSize)
				[
					SNew(SImage).Image(Sigil)
				]
			];
		}
	}
	Who->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(ElysiumUI::Space::M, 0.0f, 0.0f, 0.0f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			// Chargen types the name here; every other host shows it. An empty name is rendered as
			// its own state rather than substituted with a placeholder, which would read as a
			// character that has one.
			Mode.bNameEditable
				? StaticCastSharedRef<SWidget>(
					SNew(SBox).WidthOverride(Layout::NameEntryW)
					[
						SNew(SEditableTextBox)
						.Text(FText::FromString(Name))
						.HintText(NSLOCTEXT("Elysium", "CharNameHint", "Name"))
						.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
						                 ElysiumUI::Type::Title, 1.0f))
						.OnTextChanged_Lambda([this](const FText& In)
						{
							// Stored on every keystroke rather than on commit: the field can lose
							// focus to a trait click, and a name typed then abandoned is still the
							// name the player meant.
							if (Spend.IsValid()) { Spend->Name = In.ToString(); }
						})
					])
				: StaticCastSharedRef<SWidget>(
					SNew(STextBlock)
					.Text(Name.IsEmpty()
						? NSLOCTEXT("Elysium", "CharNoName", "Unnamed")
						: FText::FromString(Name))
					.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
					                 ElysiumUI::Type::Title, 1.0f))
					.ColorAndOpacity(FSlateColor(Name.IsEmpty()
						? ElysiumUI::Palette::BoneDim : ElysiumUI::Palette::GoldLit)))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString(FElysiumSheet::ClanName(Clan)).ToUpper()))
			.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
			                 ElysiumUI::Type::Caption, 1.0f))
			.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Amber))
		]
	];

	// Humanity: ten bubbles, VtMB's own rating notation.
	TSharedRef<SHorizontalBox> Dots = SNew(SHorizontalBox);
	const FSlateBrush* BubOn  = Art(ArtPath::BubbleOn, FLinearColor::White);
	const FSlateBrush* BubOff = Art(ArtPath::BubbleOff, FLinearColor::White);
	for (int32 i = 0; i < 10; ++i)
	{
		const bool bLit = i < Humanity;
		Dots->AddSlot().AutoWidth().Padding(0.0f, 0.0f, Layout::BubbleGap, 0.0f)
		[
			SNew(SBox).WidthOverride(Layout::BubbleSize).HeightOverride(Layout::BubbleSize)
			[
				(bLit ? BubOn : BubOff)
					? SNew(SImage).Image(bLit ? BubOn : BubOff)
					// Token fallback: a filled or hollow pip in the same two colours.
					: SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.ColorAndOpacity(FSlateColor(bLit
							? ElysiumUI::Palette::Blood
							: ElysiumUI::Palette::Gold.CopyWithNewOpacity(0.30f)))
			]
		];
	}

	// Masquerade: five slots, a strike over each violation.
	TSharedRef<SHorizontalBox> Marks = SNew(SHorizontalBox);
	const FSlateBrush* Strike = Art(ArtPath::Strike, FLinearColor::White);
	for (int32 i = 0; i < 5; ++i)
	{
		const bool bBroken = i < Masquerade;
		const FString Variant = FString::Printf(TEXT("mask%d"), i);
		const FSlateBrush* Face = Art(ArtPath::TopBar, FLinearColor::White, MaskUv(i), *Variant);

		TSharedRef<SOverlay> MaskSlot = SNew(SOverlay)
			+ SOverlay::Slot()
			[
				Face
					? SNew(SImage).Image(Face)
					// Token fallback: a hollow amber cell, matching the bubbles' own degrade.
					: SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Amber.CopyWithNewOpacity(0.22f)))
			];
		if (bBroken)
		{
			MaskSlot->AddSlot()
			[
				Strike
					? SNew(SImage).Image(Strike)
					: SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Blood))
			];
		}
		Marks->AddSlot().AutoWidth()
		[
			SNew(SBox).WidthOverride(Layout::MaskW).HeightOverride(Layout::MaskH)[ MaskSlot ]
		];
	}

	auto Meter = [&Fonts](const FText& Label, TSharedRef<SWidget> Body) -> TSharedRef<SWidget>
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(ElysiumUIFonts().Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
				                     ElysiumUI::Type::Label, 1.0f))
				.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::GoldLit))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			  .Padding(0.0f, ElysiumUI::Space::XS, 0.0f, 0.0f)
			[
				Body
			];
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[ Who ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Meter(NSLOCTEXT("Elysium", "Humanity", "Humanity"), Dots)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)[ SNew(SSpacer) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Meter(NSLOCTEXT("Elysium", "Masquerade", "Masquerade"), Marks)
		];
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildTabStrip()
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	for (EElysiumCharacterTab T : Mode.Tabs)
	{
		const bool bActive = (T == Tab);
		const FName ActionId = TabActionId(T);
		UElysiumActionButton* Button = CreateActionButton(
			ActionId, ActionGroup::Tabs, TabLabel(T), true,
			[this, T]() { SetActiveTab(T); });
		check(Button);
		const TWeakObjectPtr<UElysiumActionButton> WeakButton = Button;
		Button->SetSlateContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(TabLabel(T))
				.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
				                 ElysiumUI::Type::Heading, 1.0f))
				.ColorAndOpacity(SelectedColor(WeakButton, bActive,
					ElysiumUI::Palette::Cyan, ElysiumUI::Palette::BoneDim))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, ElysiumUI::Space::XS, 0.0f, 0.0f)
			[
				SNew(SBox).HeightOverride(2.0f)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity(SelectedColor(WeakButton, bActive,
						ElysiumUI::Palette::Cyan, FLinearColor::Transparent))
				]
			]);
		Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, Layout::TabGap, 0.0f)
		[
			Button->TakeWidget()
		];
	}
	return Row;
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildFooter()
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();

	// Retail's footer varies per tab and per host: chargen's Base tab carries a single NEXT, its
	// Sheet tab the Skip Intro checkbox plus AUTO-SPEND / ACCEPT / CANCEL, and the in-game screen
	// the level-up pair. A control with nothing behind it is drawn disabled rather than omitted.
	TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
	auto AddButton = [&](FName ActionId, const FText& Label, bool bEnabled,
		TFunction<void()> OnClick)
	{
		UElysiumActionButton* Button = CreateActionButton(
			ActionId, ActionGroup::Footer, Label, bEnabled, MoveTemp(OnClick));
		check(Button);
		const TWeakObjectPtr<UElysiumActionButton> WeakButton = Button;
		Button->SetSlateContent(
			SNew(STextBlock)
			.Text(Label)
			.Font(ElysiumUIFonts().Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
			                     ElysiumUI::Type::Label, 1.0f))
			// A disabled button never lights up, selected or not.
			.ColorAndOpacity(bEnabled
				? SelectedColor(WeakButton, false,
					ElysiumUI::Palette::Cyan, ElysiumUI::Palette::GoldLit)
				: TAttribute<FSlateColor>(FSlateColor(ElysiumUI::Palette::Disabled))));
		Buttons->AddSlot().AutoWidth().Padding(ElysiumUI::Space::L, 0.0f, 0.0f, 0.0f)
		[
			Button->TakeWidget()
		];
	};

	const bool bChargen = Mode.Spend == EElysiumSpendMode::Chargen;

	if (bChargen && Tab == EElysiumCharacterTab::Base)
	{
		// Pick a clan, move on to spending, or leave through the same semantic cancel action Back uses.
		AddButton(TEXT("Character.Footer.Next"), NSLOCTEXT("Elysium", "Next", "NEXT"),
			Spend.IsValid() && FElysiumSheet::IsValidClan(Spend->Clan),
			[this]() { SetActiveTab(EElysiumCharacterTab::Sheet); });
		AddButton(TEXT("Character.Footer.Cancel"), NSLOCTEXT("Elysium", "Cancel", "CANCEL"), true,
			[this]() { OnCancel.ExecuteIfBound(); });
	}
	else if (Tab == EElysiumCharacterTab::Sheet && Mode.Spend != EElysiumSpendMode::None)
	{
		if (bChargen)
		{
			// Ours in placement only — retail draws the same checkbox here. It stores a preference
			// the opening cinematic reads; nothing on this screen acts on it.
			const FText SkipLabel = Spend.IsValid() && Spend->bSkipIntro
				? NSLOCTEXT("Elysium", "SkipIntroOn", "[x] Skip Intro")
				: NSLOCTEXT("Elysium", "SkipIntroOff", "[ ] Skip Intro");
			UElysiumActionButton* SkipButton = CreateActionButton(
				TEXT("Character.Footer.SkipIntro"), ActionGroup::Footer, SkipLabel, true,
				[this]()
				{
					if (Spend.IsValid())
					{
						Spend->bSkipIntro = !Spend->bSkipIntro;
						Refresh();
					}
				});
			check(SkipButton);
			SkipButton->SetSlateContent(
				SNew(STextBlock)
				.Text(SkipLabel)
				.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
				                 ElysiumUI::Type::Caption, 1.0f))
				.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::BoneDim)));
			Buttons->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SkipButton->TakeWidget()
			];

			// **Disabled, deliberately.** Retail's auto-spend has no recovered algorithm: the clan's
			// `_CharGen` leveling template is the BASELINE and has already run by the time the pools
			// exist, so there is no authored spend order left to follow. Inventing one would be a
			// silent divergence (`docs/vtmb/game_runtime.md`), so the button states its shape and does
			// nothing until the handler is RE'd.
			AddButton(TEXT("Character.Footer.AutoSpend"),
				NSLOCTEXT("Elysium", "AutoSpend", "AUTO-SPEND POINTS"), false, nullptr);
			// ACCEPT arms only once every pool is spent out, which is retail's own gate.
			AddButton(TEXT("Character.Footer.Accept"), NSLOCTEXT("Elysium", "Accept", "ACCEPT"),
				Spend.IsValid() && Spend->IsSpentOut(), [this]() { OnAccept.ExecuteIfBound(); });
		}
		else
		{
			// The level-up screen: banked experience is allowed to stay banked, so ACCEPT is always
			// armed. `Auto-Level is Off` is retail's own label for a toggle it never enables.
			AddButton(TEXT("Character.Footer.AutoLevel"),
				NSLOCTEXT("Elysium", "AutoLevel", "Auto-Level is Off"), false, nullptr);
			AddButton(TEXT("Character.Footer.Accept"), NSLOCTEXT("Elysium", "Accept", "ACCEPT"), Spend.IsValid(),
				[this]() { OnAccept.ExecuteIfBound(); });
		}
		AddButton(TEXT("Character.Footer.Cancel"), NSLOCTEXT("Elysium", "Cancel", "CANCEL"), true,
			[this]() { OnCancel.ExecuteIfBound(); });
	}
	else
	{
		AddButton(TEXT("Character.Footer.Accept"), NSLOCTEXT("Elysium", "Accept", "Accept"), false, nullptr);
		AddButton(TEXT("Character.Footer.Cancel"), NSLOCTEXT("Elysium", "Close", "CLOSE"), true,
			[this]() { OnCancel.ExecuteIfBound(); });
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			// Ours: the screen has to say what closes it, and retail's Accept does not do that here.
			SNew(STextBlock)
			.Text(NSLOCTEXT("Elysium", "EscClose", "Esc  Close"))
			.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
			                 ElysiumUI::Type::Caption, 1.0f))
			.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::BoneDim))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)[ SNew(SSpacer) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Buttons ];
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildPlaceholder(const FText& Heading, const FText& Line)
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ BuildRule(Heading) ]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, ElysiumUI::Space::M, 0.0f, 0.0f)
		[
			Framed(FrameInfo.Rel, FrameInfo.Uv, FrameInfo.Slice,
				SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Line)
					.Font(Fonts.Font(EElysiumFontRole::Body, EElysiumFontWeight::Italic,
					                 ElysiumUI::Type::Body, 1.0f))
					.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::BoneDim))
				])
		];
}

// The quest log.

TSharedRef<SWidget> UElysiumCharacterScreen::BuildEntry(const ElysiumQuestView::FEntry& Entry,
                                                         bool bLedger)
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();

	// The heading, plus the tags that carry state the original never showed: a cross-hub quest is
	// marked as one, and a row that changed since the player last looked is marked unread.
	TSharedRef<SHorizontalBox> Head = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(STextBlock)
			.Text(FText::FromString(Entry.DisplayName))
			.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
			                 bLedger ? ElysiumUI::Type::Label : ElysiumUI::Type::Heading, 1.0f))
			.ColorAndOpacity(FSlateColor(bLedger
				? ElysiumUI::Palette::Bone.CopyWithNewOpacity(0.72f)
				: ElysiumUI::Palette::GoldLit))
		];

	auto AddTag = [&](const FText& Label, const FLinearColor& Colour)
	{
		Head->AddSlot().AutoWidth().VAlign(VAlign_Center)
		     .Padding(ElysiumUI::Space::S, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
			                 ElysiumUI::Type::Caption, 1.0f))
			.ColorAndOpacity(FSlateColor(Colour))
		];
	};

	if (Entry.bUnread)
	{
		AddTag(NSLOCTEXT("Elysium", "QuestUpdated", "UPDATED"), ElysiumUI::Palette::Cyan);
	}
	if (Entry.Table == FElysiumQuestTables::MainTable)
	{
		AddTag(NSLOCTEXT("Elysium", "QuestMain", "MAIN"), ElysiumUI::Palette::Amber);
	}
	if (!Entry.bResolved)
	{
		// The journal has a row the catalogue cannot place. Shown, not hidden — and labelled, so it
		// reads as a data problem rather than as a quest with no text.
		AddTag(NSLOCTEXT("Elysium", "QuestUnresolved", "UNKNOWN"), ElysiumUI::Palette::Disabled);
	}

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ Head ];

	if (!Entry.Description.IsEmpty())
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, ElysiumUI::Space::XS, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Entry.Description))
			.Font(Fonts.Font(EElysiumFontRole::Body, EElysiumFontWeight::Regular,
			                 bLedger ? ElysiumUI::Type::Label : ElysiumUI::Type::Body, 1.0f))
			.ColorAndOpacity(FSlateColor(bLedger
				? ElysiumUI::Palette::Bone.CopyWithNewOpacity(0.45f) : ElysiumUI::Palette::Bone))
			.AutoWrapText(true)
		];
	}

	return SNew(SBox).Padding(FMargin(0.0f, 0.0f, 0.0f, Layout::EntryGap))[ Body ];
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildHubRow(const int32* HubActive)
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	for (int32 Table : FElysiumQuestTables::HubTabOrder)
	{
		const bool bActive = (Table == Hub);
		const int32 Count = HubActive[Table];
		const FName ActionId = HubActionId(Table);
		UElysiumActionButton* Button = CreateActionButton(
			ActionId, ActionGroup::Hubs, HubLabel(Table), true,
			[this, Table]() { SetHub(Table); });
		check(Button);
		const TWeakObjectPtr<UElysiumActionButton> WeakButton = Button;
		Button->SetSlateContent(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(HubLabel(Table))
				.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
				                 ElysiumUI::Type::Label, 1.0f))
				.ColorAndOpacity(SelectedColor(WeakButton, bActive, ElysiumUI::Palette::Cyan,
					Count > 0 ? ElysiumUI::Palette::BoneDim : ElysiumUI::Palette::Disabled))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			  .Padding(ElysiumUI::Space::S, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::AsNumber(Count))
				.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::Regular,
				                 ElysiumUI::Type::Caption, 1.0f))
				.ColorAndOpacity(FSlateColor(bActive
					? ElysiumUI::Palette::Cyan.CopyWithNewOpacity(0.7f)
					: ElysiumUI::Palette::Disabled))
			]);
		Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, Layout::HubGap, 0.0f)
		[
			Button->TakeWidget()
		];
	}
	return Row;
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildQuestLog()
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();

	UElysiumGameStateSubsystem* State = StateFor(this);
	UElysiumRulebookSubsystem* Rules = State ? State->Rulebook() : nullptr;

	ElysiumQuestView::FView View;
	if (Rules && State)
	{
		View = ElysiumQuestView::Build(Rules->Quests(), State->Journal(), Hub);
	}

	auto Column = [this](const FFrameArt& Frame, const FText& Heading, int32 Count,
	                     const TArray<ElysiumQuestView::FEntry>& Entries,
	                     const FText& EmptyLine, bool bLedger) -> TSharedRef<SWidget>
	{
		TSharedRef<SScrollBox> List = SNew(SScrollBox);
		for (const ElysiumQuestView::FEntry& E : Entries)
		{
			List->AddSlot()[ BuildEntry(E, bLedger) ];
		}

		TSharedRef<SWidget> Inner = Entries.Num() > 0
			? StaticCastSharedRef<SWidget>(List)
			: StaticCastSharedRef<SWidget>(
				SNew(SBox).HAlign(HAlign_Left).VAlign(VAlign_Top)
				[
					SNew(STextBlock)
					.Text(EmptyLine)
					.Font(ElysiumUIFonts().Font(EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
					                     ElysiumUI::Type::Caption, 1.0f))
					.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Disabled))
				]);

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ BuildRule(Heading) ]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			  .Padding(0.0f, ElysiumUI::Space::S, 0.0f, 0.0f)
			[
				Framed(Frame.Rel, Frame.Uv, Frame.Slice, Inner)
			];
	};

	// Failed is a labelled rule and a count while it is empty, rather than an empty box holding a
	// third of the screen the way retail's does. A divergence, and the one the reference invites.
	const bool bAnyFailed = View.Failed.Num() > 0;

	TSharedRef<SVerticalBox> Ledger = SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			Column(FrameCompleted, NSLOCTEXT("Elysium", "QuestCompleted", "Completed"),
			       View.Completed.Num(), View.Completed,
			       NSLOCTEXT("Elysium", "QuestNoneYet", "NOTHING YET"), /*bLedger*/ true)
		];

	if (bAnyFailed)
	{
		Ledger->AddSlot().FillHeight(0.66f).Padding(0.0f, ElysiumUI::Space::M, 0.0f, 0.0f)
		[
			Column(FrameFailed, NSLOCTEXT("Elysium", "QuestFailed", "Failed"),
			       View.Failed.Num(), View.Failed,
			       NSLOCTEXT("Elysium", "QuestNoneYet", "NOTHING YET"), /*bLedger*/ true)
		];
	}
	else
	{
		Ledger->AddSlot().AutoHeight().Padding(0.0f, ElysiumUI::Space::M, 0.0f, 0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildRule(NSLOCTEXT("Elysium", "QuestFailed", "Failed"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, ElysiumUI::Space::S, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("Elysium", "QuestNoFailures", "NOTHING YET"))
				.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
				                 ElysiumUI::Type::Caption, 1.0f))
				.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Disabled))
			]
		];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ElysiumUI::Space::M)
		[
			BuildHubRow(View.HubActive)
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(Layout::ActiveFill)
			[
				Column(FrameActive, NSLOCTEXT("Elysium", "QuestActive", "Active Quests"),
				       View.Active.Num(), View.Active,
				       NSLOCTEXT("Elysium", "QuestNoActive", "NO OPEN QUESTS HERE"), /*bLedger*/ false)
			]
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(SSpacer).Size(FVector2D(Layout::ColumnGap, 0.0f)) ]
			+ SHorizontalBox::Slot().FillWidth(Layout::LedgerFill)[ Ledger ]
		];
}

// The sheet — one body, two currencies.

void UElysiumCharacterScreen::SetSpendState(TSharedPtr<FElysiumChargenState> InState)
{
	Spend = InState;
	SelContainer = EElysiumTraitContainer::Attributes;
	SelSlot = ElysiumSlot::Strength;
	OnCharacterChanged.ExecuteIfBound();
	Refresh();
}

FElysiumChargenRules UElysiumCharacterScreen::SpendRules() const
{
	FElysiumChargenRules Out;
	UElysiumGameStateSubsystem* State = StateFor(this);
	UElysiumRulebookSubsystem* Book = State ? State->Rulebook() : nullptr;
	if (Book == nullptr)
	{
		return Out;
	}
	Out.Stats        = &Book->Stats();
	Out.Rules        = &Book->Rules();
	Out.Clans        = &Book->Clans();
	Out.Histories    = &Book->Histories();
	Out.Leveling     = &Book->Leveling();
	Out.TraitEffects = &Book->TraitEffects();
	Out.Feats        = &Book->Feats();
	Out.Strings      = &Book->Strings();
	return Out;
}

const FElysiumSheet& UElysiumCharacterScreen::ViewSheet() const
{
	if (Spend.IsValid())
	{
		return Spend->Sheet;
	}
	// No scratch — the tab still draws, over the live character, with nothing buyable.
	static const FElysiumSheet Empty;
	UElysiumGameStateSubsystem* State = StateFor(this);
	return State ? State->PlayerSheet() : Empty;
}

const FElysiumSheetEffects* UElysiumCharacterScreen::ViewEffects() const
{
	return Spend.IsValid() ? &Spend->Effects : nullptr;
}

void UElysiumCharacterScreen::Select(EElysiumTraitContainer Container, int32 TraitSlot)
{
	if (SelContainer == Container && SelSlot == TraitSlot)
	{
		return;
	}
	SelContainer = Container;
	SelSlot = TraitSlot;
	Refresh();
}

void UElysiumCharacterScreen::TryBuy(EElysiumTraitContainer Container, int32 TraitSlot)
{
	Select(Container, TraitSlot);
	if (!Spend.IsValid() || Mode.Spend == EElysiumSpendMode::None)
	{
		return;
	}
	if (ElysiumChargen::Buy(*Spend, SpendRules(), Container, TraitSlot))
	{
		Refresh();
	}
}

void UElysiumCharacterScreen::TrySell(EElysiumTraitContainer Container, int32 TraitSlot)
{
	Select(Container, TraitSlot);
	if (!Spend.IsValid() || Mode.Spend == EElysiumSpendMode::None)
	{
		return;
	}
	if (ElysiumChargen::Sell(*Spend, SpendRules(), Container, TraitSlot))
	{
		Refresh();
	}
}

void UElysiumCharacterScreen::RebuildBaseline()
{
	if (!Spend.IsValid() || Spend->Currency != EElysiumChargenCurrency::Pools)
	{
		return;
	}
	// A clan, sex or history change moves the baseline, so everything bought on top of it is
	// invalidated rather than carried — the pools are re-derived and the spend starts over.
	ElysiumChargen::ApplyBaseline(*Spend, SpendRules());
	OnCharacterChanged.ExecuteIfBound();
	Refresh();
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildBubbles(EElysiumTraitContainer Container, int32 TraitSlot)
{
	const FElysiumSheet& Sheet = ViewSheet();
	const int32 Base    = Sheet.GetBase(Container, TraitSlot);
	const int32 Current = Sheet.GetCurrent(Container, TraitSlot);
	const int32 Floor   = Spend.IsValid() ? Spend->Baseline.GetBase(Container, TraitSlot) : Base;

	const FSlateBrush* Filled  = Art(ArtPath::BubbleOn, FLinearColor::White);
	const FSlateBrush* Empty   = Art(ArtPath::BubbleOff, FLinearColor::White);
	const FSlateBrush* Pending = Art(ArtPath::BubblePending, FLinearColor::White);
	const FSlateBrush* Bonus   = Art(ArtPath::BubbleBonus, FLinearColor::White);

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 i = 0; i < 5; ++i)
	{
		// Four states, in the order the art names them: a dot the character came in with, a dot
		// bought this session and not yet committed, a dot the trait-effect layer added on top of
		// the base, and nothing.
		const FSlateBrush* Brush = Empty;
		FLinearColor Token = ElysiumUI::Palette::Gold.CopyWithNewOpacity(0.30f);
		if (i < FMath::Min(Floor, Base))
		{
			Brush = Filled;  Token = ElysiumUI::Palette::Blood;
		}
		else if (i < Base)
		{
			Brush = Pending; Token = ElysiumUI::Palette::GoldLit;
		}
		else if (i < Current)
		{
			Brush = Bonus;   Token = ElysiumUI::Palette::Cyan;
		}

		Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, Layout::BubbleGap, 0.0f)
		[
			SNew(SBox).WidthOverride(Layout::BubbleSize).HeightOverride(Layout::BubbleSize)
			[
				Brush
					? SNew(SImage).Image(Brush)
					: SNew(SImage)
						.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.ColorAndOpacity(FSlateColor(Token))
			]
		];
	}
	return Row;
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildTraitRow(EElysiumTraitContainer Container, int32 TraitSlot)
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();
	const FElysiumChargenRules Rules = SpendRules();
	const FElysiumStat* Stat = Rules.Stats ? Rules.Stats->Container(Container).At(TraitSlot) : nullptr;

	const bool bSelected = (Container == SelContainer && TraitSlot == SelSlot);
	int32 Cost = 0;
	const bool bBuyable = Spend.IsValid() && Mode.Spend != EElysiumSpendMode::None
		&& ElysiumChargen::CanBuy(*Spend, Rules, Container, TraitSlot, Cost);
	const FText Label = FText::FromString(Stat ? Stat->Name : FString::FromInt(TraitSlot));
	const FName ActionId = TraitActionId(Container, TraitSlot);
	UElysiumActionButton* Button = CreateActionButton(
		ActionId, ActionGroup::Traits, Label, true,
		[this, Container, TraitSlot]() { TryBuy(Container, TraitSlot); },
		[this, Container, TraitSlot]() { TrySell(Container, TraitSlot); });
	check(Button);
	TraitActions.Add(ActionId, { Container, TraitSlot });
	const TWeakObjectPtr<UElysiumActionButton> WeakButton = Button;

	// Name, a leader dotted out to the bubbles, then the rating. The leader is what makes a long
	// column readable at 1024 wide, and it is the one piece of the row VtMB draws as art.
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::Regular,
			                 ElysiumUI::Type::Label, 1.0f))
			.ColorAndOpacity(SelectedColor(WeakButton, bSelected, ElysiumUI::Palette::Cyan,
				bBuyable ? ElysiumUI::Palette::Bone : ElysiumUI::Palette::BoneDim))
		];

	if (const FSlateBrush* Leader = Art(ArtPath::Leader, FLinearColor::White))
	{
		Row->AddSlot().FillWidth(1.0f).VAlign(VAlign_Bottom)
		    .Padding(ElysiumUI::Space::XS, 0.0f, ElysiumUI::Space::XS, 3.0f)
		[
			SNew(SBox).HeightOverride(2.0f)[ SNew(SImage).Image(Leader) ]
		];
	}
	else
	{
		Row->AddSlot().FillWidth(1.0f)[ SNew(SSpacer) ];
	}

	Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[ BuildBubbles(Container, TraitSlot) ];

	Button->SetSlateContent(
		SNew(SBox)
		.Padding(FMargin(0.0f, 1.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(SelectedColor(WeakButton, bSelected,
				ElysiumUI::Palette::Cyan.CopyWithNewOpacity(0.10f), FLinearColor::Transparent))
			.Padding(FMargin(ElysiumUI::Space::XS, 0.0f))
			[
				Row
			]
		]);
	return Button->TakeWidget();
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildTraitBlock(EElysiumChargenPool Pool,
	EElysiumTraitContainer Container, int32 First, int32 Last)
{
	const FElysiumChargenRules Rules = SpendRules();

	// The heading carries the category's remaining points in parentheses — and **a zero pool draws
	// no parenthetical at all**, which is what the retail capture shows for a tertiary category.
	FText Heading = FText::FromString(ElysiumChargenPoolLabel(Rules, Pool));
	if (Spend.IsValid() && Spend->Currency == EElysiumChargenCurrency::Pools)
	{
		const int32 Left = Spend->Remaining(Pool);
		if (Left > 0)
		{
			Heading = FText::Format(NSLOCTEXT("Elysium", "PoolHeading", "{0} ({1})"),
				Heading, FText::AsNumber(Left));
		}
	}

	TSharedRef<SVerticalBox> Block = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ BuildRule(Heading) ];

	for (int32 TraitSlot = First; TraitSlot <= Last; ++TraitSlot)
	{
		// The `[0, 6)` filter, so a discipline the clan cannot learn has no row rather than a
		// greyed one.
		if (!ElysiumChargen::IsRowVisible(Rules, ViewSheet(), Container, TraitSlot))
		{
			continue;
		}
		Block->AddSlot().AutoHeight().Padding(0.0f, ElysiumUI::Space::XS, 0.0f, 0.0f)
		[
			BuildTraitRow(Container, TraitSlot)
		];
	}
	return Block;
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildFeats()
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();
	const FElysiumChargenRules Rules = SpendRules();

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	if (Rules.Feats)
	{
		for (const FElysiumFeat& Feat : Rules.Feats->Feats)
		{
			// The value through the effect layer, and the same value without it. They differ exactly
			// where a clan gift or a History moved the feat off its baseline — which is what the
			// cyan is for.
			const int32 Value = ElysiumFeats::FeatValue(Feat, ViewSheet(), ViewEffects());
			const int32 Plain = ElysiumFeats::FeatValue(Feat, ViewSheet(), nullptr);

			List->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Feat.Name))
					.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::Regular,
					                 ElysiumUI::Type::Caption, 1.0f))
					.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::BoneDim))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::AsNumber(Value))
					.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
					                 ElysiumUI::Type::Caption, 1.0f))
					.ColorAndOpacity(FSlateColor(Value != Plain
						? ElysiumUI::Palette::Cyan : ElysiumUI::Palette::Bone))
				]
			];
		}
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildRule(NSLOCTEXT("Elysium", "SheetFeats", "Feats"))
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, ElysiumUI::Space::S, 0.0f, 0.0f)
		[
			Framed(FrameFeats.Rel, FrameFeats.Uv, FrameFeats.Slice,
				SNew(SScrollBox) + SScrollBox::Slot()[ List ])
		];
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildDetail()
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();
	const FElysiumChargenRules Rules = SpendRules();
	const FElysiumStat* Stat = Rules.Stats
		? Rules.Stats->Container(SelContainer).At(SelSlot) : nullptr;

	const int32 Rating = ViewSheet().GetCurrent(SelContainer, SelSlot);

	// The header row: the trait's name on the left, and on the right whichever counter the mode
	// spends — chargen's remaining points, or the level-up screen's banked experience.
	TSharedRef<SHorizontalBox> Head = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Stat ? Stat->Name : FString()))
			.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
			                 ElysiumUI::Type::Heading, 1.0f))
			.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::GoldLit))
		];

	if (Spend.IsValid())
	{
		const bool bPools = Spend->Currency == EElysiumChargenCurrency::Pools;
		Head->AddSlot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(bPools
				? FText::Format(NSLOCTEXT("Elysium", "RemainingPoints", "REMAINING POINTS: {0}"),
					FText::AsNumber(Spend->TotalRemaining()))
				: FText::Format(NSLOCTEXT("Elysium", "ExperienceLeft", "EXPERIENCE: {0}"),
					FText::AsNumber(Spend->Experience())))
			.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
			                 ElysiumUI::Type::Label, 1.0f))
			.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Amber))
		];
	}

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	auto AddLine = [&Body, &Fonts](const FString& Text, const FLinearColor& Colour,
	                               EElysiumFontWeight Weight)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		Body->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ElysiumUI::Space::XS)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Text))
			.Font(ElysiumUIFonts().Font(EElysiumFontRole::Body, Weight, ElysiumUI::Type::Label, 1.0f))
			.ColorAndOpacity(FSlateColor(Colour))
			.AutoWrapText(true)
		];
	};

	if (Stat)
	{
		AddLine(Stat->HelpText, ElysiumUI::Palette::Bone, EElysiumFontWeight::Regular);
		AddLine(Stat->HelpText2, ElysiumUI::Palette::BoneDim, EElysiumFontWeight::Italic);

		// `LEVEL n:` — the rating's own line, which only the disciplines author. The index is the
		// rating, so an unauthored one is simply absent rather than reading its neighbour's.
		const int32 At = Rating - 1;
		if (Stat->LevelHeadings.IsValidIndex(At) && !Stat->LevelHeadings[At].IsEmpty())
		{
			AddLine(Stat->LevelHeadings[At], ElysiumUI::Palette::GoldLit,
				EElysiumFontWeight::SemiBold);
		}
		if (Stat->LevelDetails.IsValidIndex(At))
		{
			AddLine(Stat->LevelDetails[At], ElysiumUI::Palette::BoneDim, EElysiumFontWeight::Regular);
		}
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, ElysiumUI::Space::XS)[ Head ]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			Framed(FrameInfo.Rel, FrameInfo.Uv, FrameInfo.Slice,
				SNew(SScrollBox) + SScrollBox::Slot()[ Body ])
		];
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildSheet()
{
	// Attributes down the left, Abilities beside them, Disciplines under Attributes — VtMB's own
	// three-block arrangement, with the feats panel holding the right column and the detail panel
	// spanning the bottom.
	TSharedRef<SVerticalBox> Left = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildTraitBlock(EElysiumChargenPool::Physical, EElysiumTraitContainer::Attributes, 1, 3)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, ElysiumUI::Space::S, 0.0f, 0.0f)
		[
			BuildTraitBlock(EElysiumChargenPool::Social, EElysiumTraitContainer::Attributes, 4, 6)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, ElysiumUI::Space::S, 0.0f, 0.0f)
		[
			BuildTraitBlock(EElysiumChargenPool::Mental, EElysiumTraitContainer::Attributes, 7, 9)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, ElysiumUI::Space::S, 0.0f, 0.0f)
		[
			BuildTraitBlock(EElysiumChargenPool::Disciplines,
				EElysiumTraitContainer::Disciplines, 0, 12)
		];

	TSharedRef<SVerticalBox> Middle = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildTraitBlock(EElysiumChargenPool::Talents, EElysiumTraitContainer::Abilities, 1, 4)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, ElysiumUI::Space::S, 0.0f, 0.0f)
		[
			BuildTraitBlock(EElysiumChargenPool::Skills, EElysiumTraitContainer::Abilities, 5, 8)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, ElysiumUI::Space::S, 0.0f, 0.0f)
		[
			BuildTraitBlock(EElysiumChargenPool::Knowledges, EElysiumTraitContainer::Abilities, 9, 12)
		];

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(Layout::SheetRowsFill)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SScrollBox) + SScrollBox::Slot()[ Left ]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SSpacer).Size(FVector2D(Layout::ColumnGap, 0.0f))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SScrollBox) + SScrollBox::Slot()[ Middle ]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SSpacer).Size(FVector2D(Layout::ColumnGap, 0.0f))
			]
			+ SHorizontalBox::Slot().FillWidth(Layout::FeatsFill)[ BuildFeats() ]
		]
		+ SVerticalBox::Slot().FillHeight(Layout::SheetDetailFill)
		  .Padding(0.0f, ElysiumUI::Space::M, 0.0f, 0.0f)
		[
			SAssignNew(DetailHost, SBox)[ BuildDetail() ]
		];
}

// The Base tab — chargen's own.

TSharedRef<SWidget> UElysiumCharacterScreen::BuildChoiceRow(FName GroupId,
	const FText& Heading, const TArray<FText>& Options, int32 Selected,
	TFunction<void(int32)> OnPick)
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();

	// A row of selectable words rather than a dropdown: at chargen every list is short enough to
	// show whole, and a list that is always open is one fewer state than a combo box. The UI has
	// no classic mode (`docs/project/remaster-direction.md`).
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 i = 0; i < Options.Num(); ++i)
	{
		const bool bOn = (i == Selected);
		const FName ActionId = BaseActionId(GroupId, i);
		UElysiumActionButton* Button = CreateActionButton(
			ActionId, GroupId, Options[i], true,
			[OnPick, i]() mutable { OnPick(i); });
		check(Button);
		const TWeakObjectPtr<UElysiumActionButton> WeakButton = Button;
		Button->SetSlateContent(
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(SelectedColor(WeakButton, bOn,
				ElysiumUI::Palette::Cyan.CopyWithNewOpacity(0.12f), FLinearColor::Transparent))
			.Padding(FMargin(ElysiumUI::Space::S, ElysiumUI::Space::XS))
			[
				SNew(STextBlock)
				.Text(Options[i])
				.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
				                 ElysiumUI::Type::Label, 1.0f))
				.ColorAndOpacity(SelectedColor(WeakButton, bOn,
					ElysiumUI::Palette::Cyan, ElysiumUI::Palette::BoneDim))
			]);
		Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, Layout::ChoiceGap, 0.0f)
		[
			Button->TakeWidget()
		];
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ BuildRule(Heading) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, ElysiumUI::Space::XS, 0.0f, 0.0f)
		[
			SNew(SScrollBox).Orientation(Orient_Horizontal) + SScrollBox::Slot()[ Row ]
		];
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildBase()
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();
	const FElysiumChargenRules Rules = SpendRules();

	// Clan: the seven playable templates, in clandoc order.
	TArray<FText> ClanNames;
	TArray<int32> ClanIndices;
	for (int32 Clan = 2; Clan <= 8; ++Clan)
	{
		ClanNames.Add(FText::FromString(FString(FElysiumSheet::ClanName(Clan)).ToUpper()));
		ClanIndices.Add(Clan);
	}
	const int32 ClanAt = Spend.IsValid() ? ClanIndices.IndexOfByKey(Spend->Clan) : INDEX_NONE;

	// History: whatever `histories000.txt` holds, in file order.
	TArray<FText> HistoryNames;
	if (Rules.Histories)
	{
		for (const FElysiumHistory& H : Rules.Histories->Rows)
		{
			HistoryNames.Add(FText::FromString(H.Name.IsEmpty() ? H.InternalName : H.Name));
		}
	}

	const TArray<FText> Sexes =
	{
		NSLOCTEXT("Elysium", "SexFemale", "FEMALE"),
		NSLOCTEXT("Elysium", "SexMale", "MALE"),
	};

	// The write-up for whatever is selected.
	FString Blurb;
	if (Spend.IsValid())
	{
		FElysiumClanTemplate Template;
		if (ElysiumChargen::ResolveClanTemplate(Rules, Spend->Clan, Template))
		{
			Blurb = Template.Description;
		}
		if (const FElysiumHistory* H = Rules.Histories
			? Rules.Histories->At(Spend->HistoryId) : nullptr)
		{
			if (!H->Description.IsEmpty())
			{
				Blurb = Blurb.IsEmpty() ? H->Description : Blurb + LINE_TERMINATOR LINE_TERMINATOR
					+ H->Description;
			}
		}
	}

	TSharedRef<SVerticalBox> Choices = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			BuildChoiceRow(ActionGroup::BaseClan,
				NSLOCTEXT("Elysium", "BaseClan", "Clan"), ClanNames, ClanAt,
				[this, ClanIndices](int32 Pick)
				{
					if (Spend.IsValid() && ClanIndices.IsValidIndex(Pick))
					{
						Spend->Clan = ClanIndices[Pick];
						RebuildBaseline();
					}
				})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, ElysiumUI::Space::M, 0.0f, 0.0f)
		[
			BuildChoiceRow(ActionGroup::BaseSex,
				NSLOCTEXT("Elysium", "BaseSex", "Gender"), Sexes,
				Spend.IsValid() ? (Spend->bMale ? 1 : 0) : INDEX_NONE,
				[this](int32 Pick)
				{
					if (Spend.IsValid())
					{
						Spend->bMale = (Pick != 0);
						RebuildBaseline();
					}
				})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, ElysiumUI::Space::M, 0.0f, 0.0f)
		[
			BuildChoiceRow(ActionGroup::BaseHistory,
				NSLOCTEXT("Elysium", "BaseHistory", "History"), HistoryNames,
				Spend.IsValid() ? Spend->HistoryId : INDEX_NONE,
				[this](int32 Pick)
				{
					if (Spend.IsValid())
					{
						Spend->HistoryId = Pick;
						RebuildBaseline();
					}
				})
		];

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[ Choices ]
		]
		+ SHorizontalBox::Slot().AutoWidth()[ SNew(SSpacer).Size(FVector2D(Layout::ColumnGap, 0.0f)) ]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
		[
			Framed(FrameInfo.Rel, FrameInfo.Uv, FrameInfo.Slice,
				SNew(SScrollBox) + SScrollBox::Slot()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Blurb))
					.Font(Fonts.Font(EElysiumFontRole::Body, EElysiumFontWeight::Regular,
					                 ElysiumUI::Type::Label, 1.0f))
					.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Bone))
					.AutoWrapText(true)
				])
		];
}

// Assembly.

TSharedRef<SWidget> UElysiumCharacterScreen::BuildBody()
{
	switch (Tab)
	{
	case EElysiumCharacterTab::QuestLog:
		return BuildQuestLog();

	case EElysiumCharacterTab::Base:
		return BuildBase();

	case EElysiumCharacterTab::Info:
		return BuildPlaceholder(
			NSLOCTEXT("Elysium", "CharTabInfo", "Info"),
			NSLOCTEXT("Elysium", "InfoPending",
				"The in-game reference pages arrive with the Info tab."));

	case EElysiumCharacterTab::Sheet:
	default:
		return BuildSheet();
	}
}

TSharedRef<SWidget> UElysiumCharacterScreen::RebuildWidget()
{
	(void)Super::RebuildWidget();
	BeginNavigationBuild();
	TraitActions.Reset();
	RegisterNavigationGroups();

	// Brushes are rebuilt per tree; the textures behind them stay cached on the widget.
	ArtCache.ResetBrushes();

	// The hub the record remembers (`m_iCurrQuestLogArea`), or the one with the most open work the
	// first time this character ever opens the screen.
	if (Hub == INDEX_NONE)
	{
		UElysiumGameStateSubsystem* State = StateFor(this);
		UElysiumRulebookSubsystem* Rules = State ? State->Rulebook() : nullptr;
		if (State)
		{
			Hub = State->QuestLogArea();
		}
		if (Hub == INDEX_NONE)
		{
			Hub = (Rules && State)
				? ElysiumQuestView::DefaultHub(Rules->Quests(), State->Journal())
				: FElysiumQuestTables::HubTabOrder[0];
			if (State)
			{
				State->SetQuestLogArea(Hub);
			}
		}
	}

	const FSlateBrush* Backdrop = Art(ArtPath::Backdrop, FLinearColor(1.0f, 1.0f, 1.0f, 0.48f));

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		  .Padding(Layout::PadX, Layout::HeaderTop, Layout::PadX, 0.0f)
		[
			BuildHeader()
		];

	// The header page carries two full-width rules, at rows 74 and 99 of its 128, and the tab strip
	// sits between them. Each is taken as its own sub-rectangle -- drawing the whole page as one rule
	// would squash the masks baked into its right end into the hairline.
	auto PageRule = [this](float Row, const TCHAR* Variant)
	{
		const FSlateBrush* Rule = Art(ArtPath::TopBar, FLinearColor::White,
			FBox2f(FVector2f(0.0f, Row / 128.0f), FVector2f(1.0f, (Row + 3.0f) / 128.0f)), Variant);
		return Rule
			? SNew(SBox).HeightOverride(3.0f)[ SNew(SImage).Image(Rule) ]
			: SNew(SBox).HeightOverride(1.0f)
			  [
			      SNew(SImage)
			      .Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
			      .ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Amber.CopyWithNewOpacity(0.4f)))
			  ];
	};

	Content->AddSlot().AutoHeight().Padding(0.0f, ElysiumUI::Space::S, 0.0f, 0.0f)
	[
		PageRule(74.0f, TEXT("rule0"))
	];
	Content->AddSlot().AutoHeight()
	       .Padding(Layout::PadX, ElysiumUI::Space::XS, Layout::PadX, 0.0f)
	[
		SAssignNew(TabStripHost, SBox)[ BuildTabStrip() ]
	];
	Content->AddSlot().AutoHeight().Padding(0.0f, ElysiumUI::Space::XS, 0.0f, 0.0f)
	[
		PageRule(99.0f, TEXT("rule1"))
	];
	Content->AddSlot().FillHeight(1.0f)
	       .Padding(Layout::PadX, ElysiumUI::Space::M, Layout::PadX, 0.0f)
	[
		SAssignNew(BodyHost, SBox)[ BuildBody() ]
	];
	Content->AddSlot().AutoHeight()
	       .Padding(Layout::PadX, ElysiumUI::Space::S, Layout::PadX, ElysiumUI::Space::M)
	[
		SAssignNew(FooterHost, SBox).HeightOverride(Layout::FooterH)[ BuildFooter() ]
	];

	TSharedRef<SOverlay> Root = SNew(SOverlay);

	// The ground: VtMB's own painted street under an ink veil, so the type never has to fight it.
	// Without the art the veil alone is the ground, which is what the menu does.
	if (Backdrop)
	{
		Root->AddSlot()[ SNew(SImage).Image(Backdrop) ];
	}
	Root->AddSlot()
	[
		SNew(SImage)
		.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.02f, 0.018f, 0.024f, Backdrop ? 0.72f : 0.94f)))
	];
	Root->AddSlot()[ Content ];

	// Authored in 1024x768; the scaler converts once at the root.
	TSharedRef<SWidget> Result = SNew(SDPIScaler)
		.DPIScale_Lambda([this]() { return VirtualScale(); })
		[
			Root
		];
	FinalizeNavigationBuild(TabActionId(Tab));
	return Result;
}

FReply UElysiumCharacterScreen::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();

	// The scope is UI-only, so the router never sees these — the screen has to route them itself,
	// exactly as the menu routes Escape.
	if (Key == EKeys::Escape)
	{
		NativeOnHandleBackAction();
		return FReply::Handled();
	}

	if (Key == EKeys::Tab)
	{
		if (!KeyEvent.IsRepeat())
		{
			CycleTab(1);
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Q || Key == EKeys::Gamepad_LeftShoulder)
	{
		if (!KeyEvent.IsRepeat())
		{
			CycleTab(-1);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::E || Key == EKeys::Gamepad_RightShoulder)
	{
		if (!KeyEvent.IsRepeat())
		{
			CycleTab(1);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Left)
	{
		if (!KeyEvent.IsRepeat())
		{
			ExecuteSelectedSecondaryAction();
		}
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(Geometry, KeyEvent);
}

void UElysiumCharacterScreen::CycleTab(int32 Delta)
{
	if (Mode.Tabs.Num() <= 1)
	{
		return;
	}
	const int32 At = FMath::Max(0, Mode.Tabs.IndexOfByKey(Tab));
	const int32 Next = (At + Delta % Mode.Tabs.Num() + Mode.Tabs.Num()) % Mode.Tabs.Num();
	SetActiveTab(Mode.Tabs[Next]);
}

FName UElysiumCharacterScreen::FirstBodyAction() const
{
	auto FirstIn = [this](FName Group)
	{
		const TArray<FName> Found = GetActionsInGroup(Group);
		return Found.IsEmpty() ? NAME_None : Found[0];
	};
	switch (Tab)
	{
	case EElysiumCharacterTab::QuestLog:
		return FindAction(HubActionId(Hub)) ? HubActionId(Hub) : FirstIn(ActionGroup::Hubs);
	case EElysiumCharacterTab::Sheet:
		return FirstIn(ActionGroup::Traits);
	case EElysiumCharacterTab::Base:
		return FirstIn(ActionGroup::BaseClan);
	default:
		return NAME_None;
	}
}

FName UElysiumCharacterScreen::LastBodyAction() const
{
	auto LastIn = [this](FName Group)
	{
		const TArray<FName> Found = GetActionsInGroup(Group);
		return Found.IsEmpty() ? NAME_None : Found.Last();
	};
	switch (Tab)
	{
	case EElysiumCharacterTab::QuestLog:
		return FindAction(HubActionId(Hub)) ? HubActionId(Hub) : LastIn(ActionGroup::Hubs);
	case EElysiumCharacterTab::Sheet:
		return LastIn(ActionGroup::Traits);
	case EElysiumCharacterTab::Base:
		return LastIn(ActionGroup::BaseHistory);
	default:
		return NAME_None;
	}
}

bool UElysiumCharacterScreen::HandleNavigation(EElysiumNavigationDirection Direction)
{
	const FName Selected = GetSelectedActionId();
	const FName Group = GetActionGroup(Selected);
	const bool bForward = Direction == EElysiumNavigationDirection::Down
		|| Direction == EElysiumNavigationDirection::Right;

	if (Direction == EElysiumNavigationDirection::Left
		|| Direction == EElysiumNavigationDirection::Right)
	{
		if (Group == ActionGroup::Traits)
		{
			if (Direction == EElysiumNavigationDirection::Left)
			{
				ExecuteSelectedSecondaryAction();
			}
			else
			{
				ExecuteSelectedAction();
			}
			return true;
		}
		if (Group == ActionGroup::Hubs || Group == ActionGroup::BaseClan
			|| Group == ActionGroup::BaseSex || Group == ActionGroup::BaseHistory)
		{
			SelectAdjacentInGroup(Group, bForward ? 1 : -1, true);
			ExecuteSelectedAction();
			return true;
		}
		return false;
	}

	if (Direction != EElysiumNavigationDirection::Up
		&& Direction != EElysiumNavigationDirection::Down)
	{
		return false;
	}

	const TArray<FName> Footer = GetActionsInGroup(ActionGroup::Footer);
	const FName ActiveTabAction = TabActionId(Tab);
	if (Group == ActionGroup::Tabs)
	{
		const FName Body = Direction == EElysiumNavigationDirection::Down
			? FirstBodyAction() : LastBodyAction();
		if (!Body.IsNone())
		{
			return SelectAction(Body);
		}
		return Footer.IsEmpty() ? false
			: SelectAction(Direction == EElysiumNavigationDirection::Down
				? Footer[0] : Footer.Last());
	}
	if (Group == ActionGroup::Footer)
	{
		if (Direction == EElysiumNavigationDirection::Down)
		{
			return SelectAction(ActiveTabAction);
		}
		const FName Body = LastBodyAction();
		return Body.IsNone() ? SelectAction(ActiveTabAction) : SelectAction(Body);
	}
	if (Group == ActionGroup::Hubs)
	{
		return Direction == EElysiumNavigationDirection::Up
			? SelectAction(ActiveTabAction)
			: (!Footer.IsEmpty() && SelectAction(Footer[0]));
	}
	if (Group == ActionGroup::Traits)
	{
		const TArray<FName> Traits = GetActionsInGroup(ActionGroup::Traits);
		const int32 At = Traits.IndexOfByKey(Selected);
		if (Direction == EElysiumNavigationDirection::Up && At <= 0)
		{
			return SelectAction(ActiveTabAction);
		}
		if (Direction == EElysiumNavigationDirection::Down && At >= Traits.Num() - 1)
		{
			return !Footer.IsEmpty() && SelectAction(Footer[0]);
		}
		return SelectAction(Traits[At + (Direction == EElysiumNavigationDirection::Down ? 1 : -1)]);
	}

	const FName BaseGroups[] = {
		ActionGroup::BaseClan, ActionGroup::BaseSex, ActionGroup::BaseHistory };
	int32 BaseGroupIndex = INDEX_NONE;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(BaseGroups); ++Index)
	{
		if (Group == BaseGroups[Index])
		{
			BaseGroupIndex = Index;
			break;
		}
	}
	if (BaseGroupIndex != INDEX_NONE)
	{
		const int32 NextGroup = BaseGroupIndex
			+ (Direction == EElysiumNavigationDirection::Down ? 1 : -1);
		if (NextGroup < 0)
		{
			return SelectAction(ActiveTabAction);
		}
		if (NextGroup >= UE_ARRAY_COUNT(BaseGroups))
		{
			return !Footer.IsEmpty() && SelectAction(Footer[0]);
		}
		const TArray<FName> Current = GetActionsInGroup(Group);
		const TArray<FName> Next = GetActionsInGroup(BaseGroups[NextGroup]);
		if (Next.IsEmpty())
		{
			return false;
		}
		const int32 Option = FMath::Max(0, Current.IndexOfByKey(Selected));
		return SelectAction(Next[FMath::Min(Option, Next.Num() - 1)]);
	}
	return false;
}

void UElysiumCharacterScreen::HandleSelectedActionChanged(FName PreviousActionId,
	FName NewActionId)
{
	const FTraitAction* Trait = TraitActions.Find(NewActionId);
	if (!Trait || (SelContainer == Trait->Container && SelSlot == Trait->Slot))
	{
		return;
	}
	SelContainer = Trait->Container;
	SelSlot = Trait->Slot;
	if (DetailHost.IsValid())
	{
		DetailHost->SetContent(BuildDetail());
	}
}

void UElysiumCharacterScreen::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	ArtCache.ResetBrushes();
	TabStripHost.Reset();
	BodyHost.Reset();
	FooterHost.Reset();
	DetailHost.Reset();
	TraitActions.Reset();
}
