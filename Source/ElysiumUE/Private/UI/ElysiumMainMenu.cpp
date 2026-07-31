#include "UI/ElysiumMainMenu.h"

#include "ElysiumContentPaths.h"
#include "Player/ElysiumCommandBus.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "UI/ElysiumUIStrings.h"
#include "UI/ElysiumUIStyle.h"
#include "UI/ElysiumUISubsystem.h"
#include "UI/ElysiumUITexture.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMenu, Log, All);

// How far the backdrop is knocked back behind the menu, 0..1. The **classic** layout's global
// dimmer: it darkens the whole frame, because a centred column can land on anything the camera
// framed. The rail layout does not use it — its veil is local, so the lit half of the scene is
// never paid for. Live — the menu re-reads it on rebuild.
static TAutoConsoleVariable<float> CVarMenuScrim(
	TEXT("elysium.MenuScrim"),
	0.22f,
	TEXT("Classic-layout backdrop dimming, 0 (none) .. 1 (black)."),
	ECVF_Default);

// Which layout the screen builds. 1 = the rail (default); 0 = `CVMainMenu::PerformLayout`'s centred
// column, kept as the A/B against the recovered original.
static TAutoConsoleVariable<int32> CVarMenuLayout(
	TEXT("elysium.MenuLayout"),
	1,
	TEXT("Menu layout: 1 = rail (default), 0 = the classic centred column."),
	ECVF_Default);

namespace
{
	// Title art is 1024x512. VtMB stretches it with the window (VGUI scales width/640 and
	// height/480 independently, so the lockup distorts); we hold its aspect instead — a technical
	// deficit fixed, not an artist decision overridden.
	constexpr float TitleAspect = 1024.0f / 512.0f;
	constexpr float ClassicTitleWidth = 620.0f;

	// --- The rail, in virtual px on the 1024x768 canvas ----------------------------------------
	// Every horizontal number is measured from the **right edge**, never as a fraction of 1024:
	// the virtual canvas is `ScreenW*768/ScreenH` wide, so it grows past 1024 with the aspect and a
	// fraction would drift the rail inward on ultrawide.
	namespace Rail
	{
		constexpr float HairInset   = 56.0f;    // the hairline, from the right edge
		constexpr float TextInset   = 22.0f;    // text right edge, from the hairline
		constexpr float Width       = 420.0f;   // the rail column's own width
		constexpr float VeilWidth   = 560.0f;   // the veil reaches further than the type does
		constexpr float HairTop     = 150.0f;
		constexpr float HairBottom  = 96.0f;
		constexpr float HeadTop     = 152.0f;   // top of the rail column
		constexpr float HeadHeight  = 208.0f;   // head block; its content sits on the bottom edge
		constexpr float HeadGap     = 32.0f;    // head block -> first row
		constexpr float TitleWidth  = 360.0f;
		constexpr float SealSize    = 330.0f;
		constexpr float SealOverhang= 46.0f;    // how far the seal bleeds past the right edge
		constexpr float RowHeight   = 38.0f;
		constexpr float RowPrimary  = 44.0f;
		constexpr float GroupGap    = 14.0f;
		constexpr float CaptionGap  = 26.0f;
		constexpr float CaptionH    = 40.0f;    // reserved, so an appearing caption shifts nothing
		constexpr float StampBottom = 40.0f;
		constexpr float TickWidth   = 3.0f;
		// The tick marks the row's middle two thirds — a marker against the hairline, not a rule
		// under the label.
		constexpr float TickInset   = 0.16f;
		constexpr float TickSpan    = 0.68f;

		// The first row's top. Everything below it is cumulative, computed as the rows are built.
		constexpr float ListTop = HeadTop + HeadHeight + HeadGap;
	}

	// The veil's ramp, right (opaque) to left (gone). Tuned against the sm_hub_1 h1 vantage: it has
	// to bury a lit window at the rail's edge without touching the lamp pool at frame centre.
	uint8 VeilAlphaAt(float DistanceFromRight)
	{
		static const float Stops[]  = { 0.00f, 0.22f, 0.52f, 1.00f };
		static const float Alphas[] = { 0.86f, 0.78f, 0.42f, 0.00f };
		const float T = FMath::Clamp(DistanceFromRight, 0.0f, 1.0f);
		for (int32 i = 1; i < UE_ARRAY_COUNT(Stops); ++i)
		{
			if (T <= Stops[i])
			{
				const float Local = (T - Stops[i - 1]) / (Stops[i] - Stops[i - 1]);
				return static_cast<uint8>(FMath::RoundToInt(255.0f * FMath::Lerp(Alphas[i - 1], Alphas[i], Local)));
			}
		}
		return 0;
	}

	// A `mm_<clan>` sprite stem for the 2..8 level-script clan encoding (`FElysiumSheet::Clan`).
	// The sheet ships nine slots with 0/1 unused, and the sprite sheet ships all seven playable
	// clans, so the table is total over the valid range.
	const TCHAR* ClanSealStem(int32 Clan)
	{
		static const TCHAR* Stems[] = {
			TEXT("mm_cam"), TEXT("mm_cam"),    // 0/1 unused -> the sect seal
			TEXT("mm_bru"), TEXT("mm_gan"), TEXT("mm_mal"), TEXT("mm_nos"),
			TEXT("mm_tor"), TEXT("mm_tre"), TEXT("mm_ven") };
		return FElysiumSheet::IsValidClan(Clan) ? Stems[Clan] : TEXT("mm_cam");
	}

	// Small caps want air. FSlateFontInfo takes tracking in 1/1000 em, so it rides the drawn size
	// and needs no scale term of its own.
	FSlateFontInfo Tracked(FSlateFontInfo Font, float Em)
	{
		Font.LetterSpacing = FMath::RoundToInt(Em * 1000.0f);
		return Font;
	}
}

UElysiumMainMenu::UElysiumMainMenu()
{
	// CommonUI: this screen owns the input while it is up, and takes focus so keyboard/gamepad
	// navigation works with no extra wiring.
	bIsBackHandler = false;
	bAutoActivate = true;
	// Required for NativeOnKeyDown to ever run: SObjectWidget::SupportsKeyboardFocus() reports this
	// flag, and the menu's input scope names this widget as its focus target (11.5).
	SetIsFocusable(true);
}

float UElysiumMainMenu::VirtualScale() const
{
	FVector2D Size(1920.0f, 1080.0f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	return ElysiumUI::ScaleFor(static_cast<float>(Size.Y));
}

TArray<UElysiumMainMenu::FMenuEntry> UElysiumMainMenu::BuildItemSet() const
{
	// Retail's own two sets. Multiplayer ships in gamemenu.res and is suppressed by the game, so it
	// is suppressed here too; View Intro / Tutorial / Manual are shipped tokens with no destination
	// in this rebuild yet and are left out rather than shown dead.
	//
	// Grouping is ours and is the item set's own shape: the act, the ledger, the exits. Captions
	// speak in the interface's voice and name the state, not the missing system.
	switch (Mode)
	{
	case EElysiumMenuMode::Pause:
		return {
			{ TEXT("VMainMenu_BTN_CONTINUE"), TEXT("Continue"),   EElysiumMenuCommand::Continue, true,
				nullptr, false, true },
			{ TEXT("VMainMenu_BTN_RELOAD"),   TEXT("Reload"),     EElysiumMenuCommand::Reload,   true,
				TEXT("Restarts this map from its last load."), true },
			{ TEXT("VMainMenu_BTN_LOADGAME"), TEXT("Load Game"),  EElysiumMenuCommand::LoadGame, false,
				TEXT("No saved games yet.") },
			{ TEXT("VMainMenu_BTN_SAVEGAME"), TEXT("Save Game"),  EElysiumMenuCommand::SaveGame, false,
				TEXT("Not in this build.") },
			{ TEXT("VMainMenu_BTN_OPTIONS"),  TEXT("Options"),    EElysiumMenuCommand::Options,  false,
				TEXT("Not in this build."), true },
			{ TEXT("VMainMenu_BTN_MAINMENU"), TEXT("Main Menu"),  EElysiumMenuCommand::MainMenu, true,
				TEXT("Ends the run and returns to the front end.") },
		};

	case EElysiumMenuMode::GameOver:
		// The run is over: there is nothing to continue, reload without a save is the same dead
		// run, and saving a corpse is not offered. Load lands with 11.9.
		return {
			{ TEXT("VMainMenu_BTN_LOADGAME"), TEXT("Load Game"), EElysiumMenuCommand::LoadGame, false,
				TEXT("No saved games yet."), false, true },
			{ TEXT("VMainMenu_BTN_MAINMENU"), TEXT("Main Menu"), EElysiumMenuCommand::MainMenu, true,
				nullptr, true },
			{ TEXT("VMainMenu_BTN_QUIT"),     TEXT("Quit"),      EElysiumMenuCommand::Quit,     true },
		};

	default:
		// Save Game is disabled out of game — retail's one main-menu/pause difference, reproduced.
		return {
			{ TEXT("VMainMenu_BTN_NEWGAME"),  TEXT("New Game"),  EElysiumMenuCommand::NewGame,  true,
				nullptr, false, true },
			{ TEXT("VMainMenu_BTN_LOADGAME"), TEXT("Load Game"), EElysiumMenuCommand::LoadGame, false,
				TEXT("No saved games yet."), true },
			{ TEXT("VMainMenu_BTN_SAVEGAME"), TEXT("Save Game"), EElysiumMenuCommand::SaveGame, false,
				TEXT("Available once a game is running.") },
			{ TEXT("VMainMenu_BTN_OPTIONS"),  TEXT("Options"),   EElysiumMenuCommand::Options,  false,
				TEXT("Not in this build."), true },
			{ TEXT("VMainMenu_BTN_QUIT"),     TEXT("Quit"),      EElysiumMenuCommand::Quit,     true },
		};
	}
}

void UElysiumMainMenu::Run(EElysiumMenuCommand Command)
{
	UGameInstance* GI = GetGameInstance();
	UElysiumGameFlowSubsystem* Flow = GI ? GI->GetSubsystem<UElysiumGameFlowSubsystem>() : nullptr;
	if (!Flow)
	{
		return;
	}

	// Every item is a call on the flow subsystem, which owns both the app state and the screen: it
	// tears the menu down, releases the hold and travels in one place. A screen never hides itself
	// and then hopes the state agrees.
	switch (Command)
	{
	case EElysiumMenuCommand::NewGame:
		// The backdrop world is standing in the menu map with no substrate, so this opens the story
		// entry for real — one map load, which is what buys a menu that can never run half a world.
		Flow->NewGame(FElysiumNewGameRequest{});
		break;

	case EElysiumMenuCommand::Continue:
		Flow->SetPaused(false);
		break;

	case EElysiumMenuCommand::Reload:
		Flow->ReloadMap();
		break;

	case EElysiumMenuCommand::MainMenu:
		Flow->QuitToMenu();
		break;

	case EElysiumMenuCommand::LoadGame:
		Flow->LoadGame(FString());
		break;

	case EElysiumMenuCommand::SaveGame:
		Flow->SaveGame(FString(), EElysiumSaveKind::Manual);
		break;

	case EElysiumMenuCommand::Quit:
		if (UWorld* World = GetWorld())
		{
			UKismetSystemLibrary::QuitGame(World, nullptr, EQuitPreference::Quit, false);
		}
		break;

	default:
		// Options has no backing system yet (8.10) and is drawn disabled, so this is only reachable
		// if an item's enabled flag is wrong.
		UE_LOG(LogElysiumMenu, Log, TEXT("menu command %d has no destination yet"), int32(Command));
		break;
	}
}

FReply UElysiumMainMenu::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		// The screen is the *other* key source for one named verb (11.6). While a menu is up the
		// input mode is UI-only and the player controller sees nothing, so the router's binding
		// cannot fire — but `cancelselect` is the same verb either way, and the flow subsystem's
		// implementation is what decides that Pause is the only mode Escape leaves. Consumed in
		// every mode regardless, so the key cannot reach the game underneath.
		ElysiumCommandBus::Exec(TEXT("cancelselect"));
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, KeyEvent);
}

UTexture2D* UElysiumMainMenu::ResolveSeal()
{
	// The front end has no character yet, so it flies the sect's own mark; a session flies the PC's
	// clan. `PlayerSheet()` already resolves live-entity-first, record-otherwise, so this reads the
	// same clan the sheet screens will.
	int32 Clan = 0;
	if (Mode != EElysiumMenuMode::Main)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (const UElysiumGameStateSubsystem* State = GI->GetSubsystem<UElysiumGameStateSubsystem>())
			{
				Clan = State->PlayerSheet().Clan();
			}
		}
	}
	const FString Path = FElysiumContentPaths::UiMenuSprite(ClanSealStem(Clan));
	UTexture2D* Tex = ElysiumUI::LoadPngTexture(Path);
	if (!Tex)
	{
		// The rail reads without it — it is a watermark, not a load-bearing element.
		UE_LOG(LogElysiumMenu, Verbose,
			TEXT("no menu seal at %s — run: uv run elysium export bundle ui"), *Path);
	}
	return Tex;
}

TSharedRef<SWidget> UElysiumMainMenu::BuildRailRow(const FMenuEntry& Item, const FText& Label,
                                                   int32 Index, float& RowTop)
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();
	const float Height = Item.bPrimary ? Rail::RowPrimary : Rail::RowHeight;
	const FSlateFontInfo Font = Tracked(
		Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::Regular,
		           Item.bPrimary ? ElysiumUI::Type::MenuItem : 26.0f, /*Scale*/ 1.0f),
		ElysiumUI::Type::TrackLabel);

	const EElysiumMenuCommand Command = Item.Command;
	const bool bEnabled = Item.bEnabled;

	// The row is left **enabled** even when its destination is missing. A disabled SButton takes
	// neither hover nor focus, so it could never arm — and arming is the whole point of a drawn-but-
	// dead row: the caption beneath the rail is where "no saved games yet" gets said. The click is
	// gated instead, and the label colour is what reports the state.
	const TSharedRef<SButton> Button =
		SNew(SButton)
		.ButtonStyle(FCoreStyle::Get(), "NoBorder")
		.ContentPadding(FMargin(0.0f, 0.0f, Rail::TextInset, 0.0f))
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.OnClicked_Lambda([this, Command, bEnabled]()
		{
			if (bEnabled)
			{
				Run(Command);
			}
			return FReply::Handled();
		});

	Button->SetContent(
		SNew(STextBlock)
		.Text(Label)
		.Font(Font)
		.Justification(ETextJustify::Right)
		.ColorAndOpacity_Lambda([this, Index, bEnabled]() -> FSlateColor
		{
			if (!bEnabled)
			{
				// Off reads as off, not as a second red: the dead rows drop to the body-copy dim
				// rather than to a darker blood, which retail's own column could not distinguish.
				return FSlateColor(ElysiumUI::Palette::BoneDim);
			}
			// Bone at rest, blood when armed. The recovered 0xc00000a8 is the accent the token
			// layer always said it was — it now marks *selection* instead of being the ground.
			return FSlateColor(ArmedIndex == Index
				? ElysiumUI::Palette::BloodLit
				: ElysiumUI::Palette::Bone);
		}));

	FRailRow Row;
	Row.Button = Button;
	Row.Top = RowTop;
	Row.Height = Height;
	RailRows.Add(Row);
	RowTop += Height;

	return SNew(SBox).HeightOverride(Height)[Button];
}

TSharedRef<SWidget> UElysiumMainMenu::BuildRail(const TArray<FMenuEntry>& Items,
                                                const TArray<FText>& Labels)
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();

	RailRows.Reset();
	RailItems = Items;
	ArmedIndex = FMath::Clamp(ArmedIndex, 0, FMath::Max(0, Items.Num() - 1));
	bTickSeeded = false;

	// --- the item column ------------------------------------------------------------------------
	const TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	float RowTop = Rail::ListTop;
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const float GapAbove = (Items[i].bGroupBreak && i > 0) ? Rail::GroupGap : 0.0f;
		RowTop += GapAbove;
		Column->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.Padding(0.0f, GapAbove, 0.0f, 0.0f)
			[
				BuildRailRow(Items[i], Labels[i], i, RowTop)
			];
	}
	const float ListBottom = RowTop;

	// --- the head -------------------------------------------------------------------------------
	// The front end flies the wordmark; a pause has no business repeating it, and a lost run is not
	// what the wordmark is about. Each is set on the head block's bottom edge, so the list below
	// starts at the same Y in all three modes.
	TSharedRef<SWidget> Head = SNullWidget::NullWidget;
	if (Mode == EElysiumMenuMode::GameOver)
	{
		const UElysiumGameFlowSubsystem* Flow =
			GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumGameFlowSubsystem>() : nullptr;
		const bool bMasquerade = Flow
			&& Flow->LastGameOverReason() == EElysiumGameOverReason::MasqueradeBreach;
		Head = SNew(STextBlock)
			.Text(bMasquerade
				? NSLOCTEXT("Elysium", "GameOverMasquerade", "The Masquerade is broken")
				: NSLOCTEXT("Elysium", "GameOverKilled", "Final Death"))
			.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold, 42.0f, 1.0f))
			.Justification(ETextJustify::Right)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Blood));
	}
	else if (Mode == EElysiumMenuMode::Pause)
	{
		Head = SNew(STextBlock)
			.Text(NSLOCTEXT("Elysium", "MenuPaused", "Paused"))
			.Font(Tracked(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::Regular, 12.0f, 1.0f), 0.26f))
			.Justification(ETextJustify::Right)
			.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Gold.CopyWithNewOpacity(0.82f)));
	}
	else
	{
		if (!TitleTexture)
		{
			TitleTexture = ElysiumUI::LoadPngTexture(FElysiumContentPaths::UiTitle());
		}
		if (TitleTexture)
		{
			TitleBrush = MakeShared<FSlateBrush>();
			TitleBrush->SetResourceObject(TitleTexture);
			TitleBrush->ImageSize = FVector2D(Rail::TitleWidth, Rail::TitleWidth / TitleAspect);
			TitleBrush->DrawAs = ESlateBrushDrawType::Image;
			Head = SNew(SImage).Image(TitleBrush.Get());
		}
		else
		{
			// Absent (no export yet) -> the wordmark is set in type instead, so the menu still
			// reads rather than showing a hole.
			UE_LOG(LogElysiumMenu, Warning,
				TEXT("no title lockup at %s — run: uv run elysium export bundle ui"),
				*FElysiumContentPaths::UiTitle());
			Head = SNew(STextBlock)
				.Text(NSLOCTEXT("Elysium", "TitleFallback", "Elysium"))
				.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
				                 ElysiumUI::Type::Display, 1.0f))
				.Justification(ETextJustify::Right)
				.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::GoldLit));
		}
	}

	// --- the code-authored ramps ----------------------------------------------------------------
	if (!VeilTexture)
	{
		constexpr int32 VeilSteps = 128;
		TArray<uint8> Ramp;
		Ramp.Reserve(VeilSteps);
		for (int32 x = 0; x < VeilSteps; ++x)
		{
			// Texel 0 is the veil's left edge, which is its transparent end.
			Ramp.Add(VeilAlphaAt(1.0f - (float(x) / float(VeilSteps - 1))));
		}
		VeilTexture = ElysiumUI::MakeAlphaRamp(Ramp, VeilSteps, 1);
	}
	if (!HairTexture)
	{
		constexpr int32 HairSteps = 128;
		TArray<uint8> Ramp;
		Ramp.Reserve(HairSteps);
		for (int32 y = 0; y < HairSteps; ++y)
		{
			const float T = float(y) / float(HairSteps - 1);
			// Fades in over the first 14% and out over the last, so the line has no cut ends.
			const float A = FMath::Min(FMath::GetMappedRangeValueClamped(FVector2f(0.0f, 0.14f), FVector2f(0.0f, 1.0f), T),
			                           FMath::GetMappedRangeValueClamped(FVector2f(0.86f, 1.0f), FVector2f(1.0f, 0.0f), T));
			Ramp.Add(static_cast<uint8>(FMath::RoundToInt(255.0f * 0.55f * A)));
		}
		HairTexture = ElysiumUI::MakeAlphaRamp(Ramp, 1, HairSteps);
	}
	if (!BarTexture)
	{
		BarTexture = ElysiumUI::MakeAlphaRamp({ 255 }, 1, 1);
	}
	if (!SealTexture)
	{
		SealTexture = ResolveSeal();
	}

	auto MakeBrush = [](UTexture2D* Texture, const FLinearColor& Tint) -> TSharedPtr<FSlateBrush>
	{
		if (!Texture)
		{
			return nullptr;
		}
		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
		Brush->SetResourceObject(Texture);
		Brush->DrawAs = ESlateBrushDrawType::Image;
		Brush->ImageSize = FVector2D(1.0f, 1.0f);   // every one of these is stretched by its box
		Brush->TintColor = FSlateColor(Tint);
		return Brush;
	};

	FLinearColor VeilTint = ElysiumUI::Palette::Ink;
	VeilTint.A = 1.0f;   // the ramp carries the falloff; the tint carries only the colour
	VeilBrush = MakeBrush(VeilTexture, VeilTint);
	HairBrush = MakeBrush(HairTexture, ElysiumUI::Palette::Gold);
	BarBrush  = MakeBrush(BarTexture, ElysiumUI::Palette::Blood);
	SealBrush = MakeBrush(SealTexture, ElysiumUI::Palette::Gold.CopyWithNewOpacity(0.10f));

	// --- assembly -------------------------------------------------------------------------------
	const TSharedRef<SOverlay> Root = SNew(SOverlay);

	if (VeilBrush.IsValid())
	{
		Root->AddSlot()
			.HAlign(HAlign_Right).VAlign(VAlign_Fill)
			[
				SNew(SBox).WidthOverride(Rail::VeilWidth)[SNew(SImage).Image(VeilBrush.Get())]
			];
	}
	if (SealBrush.IsValid())
	{
		// Bleeds off the right edge and sits a little above centre, behind the item column — a
		// watermark the rail is etched over, not a badge beside it.
		Root->AddSlot()
			.HAlign(HAlign_Right).VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, -Rail::SealOverhang, Rail::SealSize * 0.08f)
			[
				SNew(SBox).WidthOverride(Rail::SealSize).HeightOverride(Rail::SealSize)
				[
					SNew(SImage).Image(SealBrush.Get())
				]
			];
	}
	if (HairBrush.IsValid())
	{
		Root->AddSlot()
			.HAlign(HAlign_Right).VAlign(VAlign_Fill)
			.Padding(0.0f, Rail::HairTop, Rail::HairInset, Rail::HairBottom)
			[
				SNew(SBox).WidthOverride(1.0f)[SNew(SImage).Image(HairBrush.Get())]
			];
	}

	// The rail column: head block, item column, caption. One vertical stack anchored at HeadTop, so
	// the whole assembly moves as a unit if that constant is retuned.
	Root->AddSlot()
		.HAlign(HAlign_Right).VAlign(VAlign_Top)
		.Padding(0.0f, Rail::HeadTop, Rail::HairInset, 0.0f)
		[
			SNew(SBox).WidthOverride(Rail::Width)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox).HeightOverride(Rail::HeadHeight).HAlign(HAlign_Right).VAlign(VAlign_Bottom)
					.Padding(0.0f, 0.0f, Rail::TextInset, 0.0f)
					[
						Head
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, Rail::HeadGap, 0.0f, 0.0f)
				[
					Column
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, Rail::CaptionGap, Rail::TextInset, 0.0f)
				[
					// Height is reserved whether or not there is anything to say, so a caption
					// appearing never moves the column above it.
					SNew(SBox).HeightOverride(Rail::CaptionH).VAlign(VAlign_Top)
					[
						SNew(STextBlock)
						.Font(Fonts.Font(EElysiumFontRole::Body, EElysiumFontWeight::Italic, 14.0f, 1.0f))
						.Justification(ETextJustify::Right)
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::BoneDim.CopyWithNewOpacity(0.9f)))
						.Text_Lambda([this]() -> FText
						{
							if (RailItems.IsValidIndex(ArmedIndex) && RailItems[ArmedIndex].Caption)
							{
								return FText::FromString(RailItems[ArmedIndex].Caption);
							}
							return FText::GetEmpty();
						})
					]
				]
			]
		];

	// The tick: one marker riding the hairline, eased onto the armed row by NativeTick. Placed by
	// padding rather than by a canvas slot, because the row geometry is already known analytically.
	if (BarBrush.IsValid())
	{
		Root->AddSlot()
			.HAlign(HAlign_Right).VAlign(VAlign_Top)
			.Padding(TAttribute<FMargin>::CreateLambda(
				[this]() { return FMargin(0.0f, TickTop, Rail::HairInset, 0.0f); }))
			[
				SNew(SBox)
				.WidthOverride(Rail::TickWidth)
				.HeightOverride_Lambda([this]() { return FOptionalSize(TickHeight); })
				[
					SNew(SImage)
					.Image(BarBrush.Get())
					.Visibility_Lambda([this]()
					{
						// A dead row arms and explains itself, but it is not a destination, so the
						// marker does not claim it.
						return (RailItems.IsValidIndex(ArmedIndex) && RailItems[ArmedIndex].bEnabled)
							? EVisibility::HitTestInvisible : EVisibility::Hidden;
					})
				]
			];
	}

	// ListBottom is what the caption row's reserved height is measured against; asserting it here
	// keeps the analytic row table and the built stack from drifting apart silently.
	ensureMsgf(ListBottom > Rail::ListTop || Items.Num() == 0,
		TEXT("rail row table did not advance"));

	return Root;
}

TSharedRef<SWidget> UElysiumMainMenu::BuildClassic(const TArray<FMenuEntry>& Items,
                                                   const TArray<FText>& Labels)
{
	FElysiumUIFontLibrary& Fonts = ElysiumUIFonts();
	const FSlateFontInfo ItemFont =
		Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::Regular,
		           ElysiumUI::Type::MenuItem, /*Scale*/ 1.0f);

	// CVMainMenu sizes every button to the widest label, so the column is one uniform block.
	const TSharedRef<FSlateFontMeasure> Measure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	float MaxW = 0.0f;
	float MaxH = 0.0f;
	for (const FText& Label : Labels)
	{
		const FVector2D Size = Measure->Measure(Label.ToString(), ItemFont);
		MaxW = FMath::Max(MaxW, static_cast<float>(Size.X));
		MaxH = FMath::Max(MaxH, static_cast<float>(Size.Y));
	}
	// FUN_100660e0: btnW = maxLabelW + 20, btnH = maxLabelH + 4, pitch = btnH + 2 (virtual px).
	const float ButtonW = MaxW + 20.0f;
	const float ButtonH = MaxH + 4.0f;
	const float Gutter = 2.0f;

	const TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const EElysiumMenuCommand Command = Items[i].Command;
		const bool bEnabled = Items[i].bEnabled;

		// A transparent SButton carries hover, click, focus and keyboard activation for free; the
		// only visual it contributes is the label, whose colour reports the state.
		//
		// The button is built first and its content set afterwards, because the label's colour
		// attribute has to see the button. Capturing the local TSharedPtr by reference would dangle
		// — the local dies at the end of this iteration, long before Slate paints — and capturing it
		// by value would capture null, since SAssignNew has not run yet. A weak pointer taken after
		// construction is the only form that is both valid and non-owning (the button owns the text,
		// so an owning capture would be a cycle).
		const TSharedRef<SButton> Button =
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(0.0f))
			.IsEnabled(bEnabled)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this, Command]()
			{
				Run(Command);
				return FReply::Handled();
			});

		const TWeakPtr<SButton> WeakButton = Button;
		Button->SetContent(
			SNew(STextBlock)
			.Text(Labels[i])
			.Font(ItemFont)
			.Justification(ETextJustify::Center)
			.ColorAndOpacity_Lambda([WeakButton, bEnabled]() -> FSlateColor
			{
				if (!bEnabled)
				{
					// Dim the hue, not the alpha: Slate already multiplies a disabled widget's
					// opacity, and stacking a second alpha cut on top made these items vanish
					// outright against a lit backdrop.
					const FLinearColor& B = ElysiumUI::Palette::Blood;
					return FSlateColor(FLinearColor(B.R * 0.7f, B.G, B.B, 1.0f));
				}
				const TSharedPtr<SButton> Pinned = WeakButton.Pin();
				const bool bHot = Pinned.IsValid() && (Pinned->IsHovered() || Pinned->HasKeyboardFocus());
				return FSlateColor(bHot ? ElysiumUI::Palette::BloodLit : ElysiumUI::Palette::Blood);
			}));

		Column->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, Gutter)
			[
				SNew(SBox).WidthOverride(ButtonW).HeightOverride(ButtonH)[Button]
			];
	}

	TSharedRef<SWidget> Title = SNullWidget::NullWidget;
	if (Mode == EElysiumMenuMode::GameOver)
	{
		const UElysiumGameFlowSubsystem* Flow =
			GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumGameFlowSubsystem>() : nullptr;
		const bool bMasquerade = Flow
			&& Flow->LastGameOverReason() == EElysiumGameOverReason::MasqueradeBreach;
		Title = SNew(STextBlock)
			.Text(bMasquerade
				? NSLOCTEXT("Elysium", "GameOverMasquerade", "The Masquerade is broken")
				: NSLOCTEXT("Elysium", "GameOverKilled", "Final Death"))
			.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
			                 ElysiumUI::Type::Display, 1.0f))
			.Justification(ETextJustify::Center)
			.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::Blood));
	}
	else
	{
		if (!TitleTexture)
		{
			TitleTexture = ElysiumUI::LoadPngTexture(FElysiumContentPaths::UiTitle());
		}
		if (TitleTexture)
		{
			TitleBrush = MakeShared<FSlateBrush>();
			TitleBrush->SetResourceObject(TitleTexture);
			TitleBrush->ImageSize = FVector2D(ClassicTitleWidth, ClassicTitleWidth / TitleAspect);
			TitleBrush->DrawAs = ESlateBrushDrawType::Image;
			Title = SNew(SImage).Image(TitleBrush.Get());
		}
		else
		{
			Title = SNew(STextBlock)
				.Text(NSLOCTEXT("Elysium", "TitleFallback", "Elysium"))
				.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
				                 ElysiumUI::Type::Display, 1.0f))
				.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::GoldLit));
		}
	}

	// The global scrim. VtMB needs none — every background colour in VampireScheme is fully
	// transparent because its menu floats over a dark particle field — and a centred column over a
	// real 3D backdrop is exactly the case that does, whatever the camera is pointed at.
	if (!ScrimBrush.IsValid())
	{
		ScrimBrush = MakeShared<FSlateBrush>();
		ScrimBrush->DrawAs = ESlateBrushDrawType::Image;
	}
	{
		FLinearColor Scrim = ElysiumUI::Palette::Scrim;
		Scrim.A = FMath::Clamp(CVarMenuScrim.GetValueOnGameThread(), 0.0f, 1.0f);
		ScrimBrush->TintColor = FSlateColor(Scrim);
	}

	return SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SImage).Image(ScrimBrush.Get())
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, ElysiumUI::Space::XL * 2.0f, 0.0f, ElysiumUI::Space::L)
			[
				Title
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Top)
			[
				Column
			]
		];
}

TSharedRef<SWidget> UElysiumMainMenu::RebuildWidget()
{
	const TArray<FMenuEntry> Items = BuildItemSet();

	TArray<FText> Labels;
	Labels.Reserve(Items.Num());
	for (const FMenuEntry& Item : Items)
	{
		Labels.Add(FElysiumUIStrings::Get().Resolve(Item.Token, Item.Fallback));
	}

	const bool bRail = CVarMenuLayout.GetValueOnGameThread() != 0;
	if (!bRail)
	{
		RailRows.Reset();
		RailItems.Reset();
	}

	// Everything below is authored in VtMB's 1024x768 virtual canvas; the SDPIScaler at the root
	// converts once. So the font is measured at its *virtual* size and every recovered constant is
	// used verbatim.
	return SNew(SDPIScaler)
		.DPIScale_Lambda([this]() { return VirtualScale(); })
		[
			bRail ? BuildRail(Items, Labels) : BuildClassic(Items, Labels)
		];
}

void UElysiumMainMenu::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	if (RailRows.Num() == 0)
	{
		return;
	}

	// Resolve the armed row by polling, rather than by wiring four delegates per row. The last
	// armed row is *kept* when nothing is hovered or focused, so the marker behaves like a cursor
	// — a mouse leaving the rail must not blank it and strand the keyboard user.
	for (int32 i = 0; i < RailRows.Num(); ++i)
	{
		const TSharedPtr<SButton> Button = RailRows[i].Button.Pin();
		if (Button.IsValid() && (Button->IsHovered() || Button->HasKeyboardFocus()))
		{
			ArmedIndex = i;
			break;
		}
	}

	if (!RailRows.IsValidIndex(ArmedIndex))
	{
		return;
	}
	const FRailRow& Row = RailRows[ArmedIndex];
	const float TargetTop = Row.Top + Row.Height * Rail::TickInset;
	const float TargetHeight = Row.Height * Rail::TickSpan;

	if (!bTickSeeded)
	{
		// The first frame places the marker; it does not slide in from wherever zero happens to be.
		TickTop = TargetTop;
		TickHeight = TargetHeight;
		bTickSeeded = true;
		return;
	}

	// Exponential approach, framerate-independent: ~130 ms to close the gap, which is fast enough
	// to feel attached to the key press and slow enough to read as travel.
	const float Alpha = 1.0f - FMath::Exp(-DeltaSeconds / 0.045f);
	TickTop = FMath::Lerp(TickTop, TargetTop, Alpha);
	TickHeight = FMath::Lerp(TickHeight, TargetHeight, Alpha);
}

void UElysiumMainMenu::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	TitleBrush.Reset();
	ScrimBrush.Reset();
	SealBrush.Reset();
	VeilBrush.Reset();
	HairBrush.Reset();
	BarBrush.Reset();
	RailRows.Reset();
}
