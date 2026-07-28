#include "UI/ElysiumCharacterScreen.h"

#include "ElysiumContentPaths.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "Player/ElysiumCommandBus.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "UI/ElysiumUIStyle.h"
#include "UI/ElysiumUITexture.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
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
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCharScreen, Log, All);

namespace
{
	// One per translation unit, as the menu does — there is no shared font-library instance.
	FElysiumUIFontLibrary& UIFonts()
	{
		static FElysiumUIFontLibrary Fonts;
		return Fonts;
	}

	// Layout, in VtMB's 1024x768 virtual canvas. Horizontal values are measured from an edge, never
	// as a fraction of 1024: the canvas widens with the aspect ratio, so a fraction would drift the
	// content inward on ultrawide (`docs/ui-architecture.md` section 2).
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

	UElysiumGameStateSubsystem* StateFor(const UWidget* Widget)
	{
		UGameInstance* GI = Widget ? Widget->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr;
	}
}

UElysiumCharacterScreen::UElysiumCharacterScreen()
{
	bAutoActivate = true;
	// Without this `SObjectWidget::SupportsKeyboardFocus()` reports false and the screen never sees
	// Escape, even though the input scope names it as the focus target.
	SetIsFocusable(true);

	Mode.Tabs = { EElysiumCharacterTab::Sheet, EElysiumCharacterTab::Info,
	              EElysiumCharacterTab::QuestLog };
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
	// the engine writes the unread byte and never reads it (`docs/ui-architecture.md`).
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
}

// ================================================================================================
// Art
// ================================================================================================

const FSlateBrush* UElysiumCharacterScreen::Art(const TCHAR* RelPath, const FLinearColor& Tint,
                                                const FBox2f& Uv, const TCHAR* Variant)
{
	const FString Key = RelPath;
	if (ArtMissing.Contains(Key))
	{
		return nullptr;
	}
	// One texture can back several brushes (a sub-rectangle, a different tint), so the brush cache
	// is keyed by variant while the texture cache is keyed by file.
	const FString BrushKey = Variant ? Key + TEXT("#") + Variant : Key;
	if (const TSharedPtr<FSlateBrush>* Found = ArtBrushes.Find(BrushKey))
	{
		return Found->Get();
	}

	TObjectPtr<UTexture2D>* Cached = ArtTextures.Find(Key);
	if (!Cached)
	{
		const FString Path = FElysiumContentPaths::UiArt(Key);
		UTexture2D* Loaded = ElysiumUI::LoadPngTexture(Path);
		if (!Loaded)
		{
			// Not fatal anywhere: every caller draws the token version instead. Verbose because a
			// clone with no export would otherwise log a dozen warnings per open.
			UE_LOG(LogElysiumCharScreen, Verbose,
				TEXT("no sheet art at %s — run: python tools/UE_extract_ui.py"), *Path);
			ArtMissing.Add(Key);
			return nullptr;
		}
		Cached = &ArtTextures.Add(Key, Loaded);
	}

	TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
	Brush->SetResourceObject(Cached->Get());
	Brush->DrawAs = ESlateBrushDrawType::Image;
	Brush->ImageSize = FVector2D(1.0f, 1.0f);   // stretched by the box that holds it
	Brush->TintColor = FSlateColor(Tint);
	Brush->SetUVRegion(FBox2f(Uv.Min, Uv.Max));
	ArtBrushes.Add(BrushKey, Brush);
	return Brush.Get();
}

TSharedRef<SWidget> UElysiumCharacterScreen::Framed(const TCHAR* RelPath, const FBox2f& Uv,
                                                    const FMargin& Slice, TSharedRef<SWidget> Content)
{
	const FString Key = FString(RelPath) + TEXT("#box");
	TSharedPtr<FSlateBrush>* Found = ArtBrushes.Find(Key);
	if (!Found && !ArtMissing.Contains(FString(RelPath)))
	{
		// Reuse the plain loader for the texture, then build the box brush beside it.
		if (Art(RelPath, FLinearColor::White))
		{
			TObjectPtr<UTexture2D>* Tex = ArtTextures.Find(RelPath);
			if (Tex && *Tex)
			{
				TSharedPtr<FSlateBrush> Box = MakeShared<FSlateBrush>();
				Box->SetResourceObject(Tex->Get());
				Box->DrawAs = ESlateBrushDrawType::Box;
				Box->Margin = Slice;
				Box->SetUVRegion(FBox2f(Uv.Min, Uv.Max));
				Box->ImageSize = FVector2D(1.0f, 1.0f);
				Box->TintColor = FSlateColor(FLinearColor::White);
				Found = &ArtBrushes.Add(Key, Box);
			}
		}
	}

	if (Found && Found->IsValid())
	{
		return SNew(SBorder)
			.BorderImage(Found->Get())
			.Padding(FMargin(ElysiumUI::Space::M, ElysiumUI::Space::M))
			[
				Content
			];
	}

	// No art: a hairline in the frame's own amber, which is the same line the corner scrolls sit on.
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FSlateColor(ElysiumUI::Palette::Amber.CopyWithNewOpacity(0.28f)))
		.Padding(FMargin(1.0f))
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f)))
			.Padding(FMargin(ElysiumUI::Space::M))
			[
				Content
			]
		];
}

// ================================================================================================
// The shell
// ================================================================================================

TSharedRef<SWidget> UElysiumCharacterScreen::BuildRule(const FText& Label)
{
	FElysiumUIFontLibrary& Fonts = UIFonts();

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
	FElysiumUIFontLibrary& Fonts = UIFonts();
	UElysiumGameStateSubsystem* State = StateFor(this);

	const int32 Clan      = State ? State->PlayerSheet().Clan() : 0;
	const FString Name    = State ? State->PlayerName() : FString();
	const int32 Humanity  = State
		? State->PlayerSheet().GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Humanity)
		: 0;
	const int32 Masquerade = State
		? State->PlayerSheet().GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Masquerade)
		: 0;

	// --- identity ---
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
			// Empty until chargen writes it (9.4f). Rendered as its own state rather than
			// substituted with a placeholder name, which would read as a character that has one.
			SNew(STextBlock)
			.Text(Name.IsEmpty()
				? NSLOCTEXT("Elysium", "CharNoName", "Unnamed")
				: FText::FromString(Name))
			.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
			                 ElysiumUI::Type::Title, 1.0f))
			.ColorAndOpacity(FSlateColor(Name.IsEmpty()
				? ElysiumUI::Palette::BoneDim : ElysiumUI::Palette::GoldLit))
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

	// --- Humanity: ten bubbles, VtMB's own rating notation ---
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

	// --- Masquerade: five slots, a strike over each violation ---
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
				.Font(UIFonts().Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
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
	FElysiumUIFontLibrary& Fonts = UIFonts();
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	for (EElysiumCharacterTab T : Mode.Tabs)
	{
		const bool bActive = (T == Tab);
		Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, Layout::TabGap, 0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(TabLabel(T))
				.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
				                 ElysiumUI::Type::Heading, 1.0f))
				.ColorAndOpacity(FSlateColor(bActive
					? ElysiumUI::Palette::Cyan : ElysiumUI::Palette::BoneDim))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, ElysiumUI::Space::XS, 0.0f, 0.0f)
			[
				SNew(SBox).HeightOverride(2.0f)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity(FSlateColor(bActive
						? ElysiumUI::Palette::Cyan : FLinearColor::Transparent))
				]
			]
		];
	}
	return Row;
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildFooter()
{
	FElysiumUIFontLibrary& Fonts = UIFonts();

	// Retail's footer varies per tab: the sheet carries the level-up controls, the quest log a
	// disabled Accept. Both are drawn disabled here — the sheet body that would enable them is the
	// follow-up task, and drawing them absent would misreport the screen's shape.
	TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
	auto AddButton = [&](const FText& Label)
	{
		Buttons->AddSlot().AutoWidth().Padding(ElysiumUI::Space::L, 0.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
			                 ElysiumUI::Type::Label, 1.0f))
			.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Disabled))
		];
	};

	if (Tab == EElysiumCharacterTab::Sheet)
	{
		AddButton(NSLOCTEXT("Elysium", "AutoLevel", "Auto-Level is Off"));
		AddButton(NSLOCTEXT("Elysium", "Accept", "Accept"));
		AddButton(NSLOCTEXT("Elysium", "Cancel", "Cancel"));
	}
	else
	{
		AddButton(NSLOCTEXT("Elysium", "Accept", "Accept"));
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
	FElysiumUIFontLibrary& Fonts = UIFonts();
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

// ================================================================================================
// The quest log
// ================================================================================================

TSharedRef<SWidget> UElysiumCharacterScreen::BuildEntry(const ElysiumQuestView::FEntry& Entry,
                                                         bool bLedger)
{
	FElysiumUIFontLibrary& Fonts = UIFonts();

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
	FElysiumUIFontLibrary& Fonts = UIFonts();
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	for (int32 Table : FElysiumQuestTables::HubTabOrder)
	{
		const bool bActive = (Table == Hub);
		const int32 Count = HubActive[Table];
		Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, Layout::HubGap, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(HubLabel(Table))
				.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
				                 ElysiumUI::Type::Label, 1.0f))
				.ColorAndOpacity(FSlateColor(bActive ? ElysiumUI::Palette::Cyan
					: (Count > 0 ? ElysiumUI::Palette::BoneDim : ElysiumUI::Palette::Disabled)))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			  .Padding(ElysiumUI::Space::S, 0.0f, 0.0f, 0.0f)
			[
				// The count is what stops the tab row hiding work: open quests in a hub you are not
				// looking at are still visible as a number.
				SNew(STextBlock)
				.Text(FText::AsNumber(Count))
				.Font(Fonts.Font(EElysiumFontRole::Data, EElysiumFontWeight::Regular,
				                 ElysiumUI::Type::Caption, 1.0f))
				.ColorAndOpacity(FSlateColor(bActive
					? ElysiumUI::Palette::Cyan.CopyWithNewOpacity(0.7f)
					: ElysiumUI::Palette::Disabled))
			]
		];
	}
	return Row;
}

TSharedRef<SWidget> UElysiumCharacterScreen::BuildQuestLog()
{
	FElysiumUIFontLibrary& Fonts = UIFonts();

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
					.Font(UIFonts().Font(EElysiumFontRole::Data, EElysiumFontWeight::SemiBold,
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

// ================================================================================================
// Assembly
// ================================================================================================

TSharedRef<SWidget> UElysiumCharacterScreen::BuildBody()
{
	switch (Tab)
	{
	case EElysiumCharacterTab::QuestLog:
		return BuildQuestLog();

	case EElysiumCharacterTab::Info:
		return BuildPlaceholder(
			NSLOCTEXT("Elysium", "CharTabInfo", "Info"),
			NSLOCTEXT("Elysium", "InfoPending",
				"The clan and history write-ups arrive with chargen."));

	case EElysiumCharacterTab::Sheet:
	default:
		// Named as what it is: the level-up interface, with its body not yet built. The footer above
		// draws its real controls disabled, so the tab does not read as a read-only display.
		return BuildPlaceholder(
			NSLOCTEXT("Elysium", "CharTabSheet", "Sheet"),
			NSLOCTEXT("Elysium", "SheetPending",
				"Traits, feats and experience spending arrive with the sheet body."));
	}
}

TSharedRef<SWidget> UElysiumCharacterScreen::RebuildWidget()
{
	// Brushes are rebuilt per tree; the textures behind them stay cached on the widget.
	ArtBrushes.Reset();

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
	return SNew(SDPIScaler)
		.DPIScale_Lambda([this]() { return VirtualScale(); })
		[
			Root
		];
}

FReply UElysiumCharacterScreen::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();

	// The scope is UI-only, so the router never sees these — the screen has to route them itself,
	// exactly as the menu routes Escape.
	if (Key == EKeys::Escape)
	{
		ElysiumCommandBus::Exec(TEXT("cancelselect"));
		return FReply::Handled();
	}

	if (Tab == EElysiumCharacterTab::QuestLog && (Key == EKeys::Left || Key == EKeys::Right))
	{
		int32 At = 0;
		for (int32 i = 0; i < 4; ++i)
		{
			if (FElysiumQuestTables::HubTabOrder[i] == Hub) { At = i; break; }
		}
		At = (At + (Key == EKeys::Right ? 1 : 3)) % 4;
		SetHub(FElysiumQuestTables::HubTabOrder[At]);
		return FReply::Handled();
	}

	if (Key == EKeys::Tab && Mode.Tabs.Num() > 1)
	{
		const int32 At = FMath::Max(0, Mode.Tabs.IndexOfByKey(Tab));
		SetActiveTab(Mode.Tabs[(At + 1) % Mode.Tabs.Num()]);
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(Geometry, KeyEvent);
}

void UElysiumCharacterScreen::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	ArtBrushes.Reset();
	TabStripHost.Reset();
	BodyHost.Reset();
	FooterHost.Reset();
}
