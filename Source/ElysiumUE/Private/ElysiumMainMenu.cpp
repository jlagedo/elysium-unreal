#include "ElysiumMainMenu.h"

#include "ElysiumContentPaths.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumUIStrings.h"
#include "ElysiumUIStyle.h"
#include "ElysiumUISubsystem.h"
#include "ElysiumUITexture.h"

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

// How far the backdrop is knocked back behind the menu, 0..1. Tunable because the right amount is
// a property of the vantage, not of the UI: a night street needs almost none, a lit wall needs a
// lot. Live — the menu re-reads it on rebuild.
static TAutoConsoleVariable<float> CVarMenuScrim(
	TEXT("elysium.MenuScrim"),
	0.22f,
	TEXT("Menu backdrop dimming, 0 (none) .. 1 (black)."),
	ECVF_Default);

namespace
{
	// One font library per module load. The composed UFonts are transient UObjects held by
	// TStrongObjectPtr inside, so they survive GC for the session; the menu is not the only screen
	// that will want them.
	FElysiumUIFontLibrary& UIFonts()
	{
		static FElysiumUIFontLibrary Library;
		return Library;
	}

	// Title art is 1024x512 in a 1024-wide canvas. VtMB stretches it with the window (VGUI scales
	// width/640 and height/480 independently, so the lockup distorts); we hold its aspect instead —
	// a technical deficit fixed, not an artist decision overridden.
	constexpr float TitleVirtualWidth = 620.0f;
	constexpr float TitleAspect = 1024.0f / 512.0f;
}

UElysiumMainMenu::UElysiumMainMenu()
{
	// CommonUI: this screen owns the input while it is up, and takes focus so keyboard/gamepad
	// navigation works with no extra wiring.
	bIsBackHandler = false;
	bAutoActivate = true;
	// Required for NativeOnKeyDown to ever run: SObjectWidget::SupportsKeyboardFocus() reports this
	// flag, and the UI subsystem's FInputModeUIOnly hands focus to this widget.
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
	switch (Mode)
	{
	case EElysiumMenuMode::Pause:
		return {
			{ TEXT("VMainMenu_BTN_CONTINUE"), TEXT("Continue"),   EElysiumMenuCommand::Continue, true },
			{ TEXT("VMainMenu_BTN_RELOAD"),   TEXT("Reload"),     EElysiumMenuCommand::Reload,   true },
			{ TEXT("VMainMenu_BTN_LOADGAME"), TEXT("Load Game"),  EElysiumMenuCommand::LoadGame, false },
			{ TEXT("VMainMenu_BTN_SAVEGAME"), TEXT("Save Game"),  EElysiumMenuCommand::SaveGame, false },
			{ TEXT("VMainMenu_BTN_OPTIONS"),  TEXT("Options"),    EElysiumMenuCommand::Options,  false },
			{ TEXT("VMainMenu_BTN_MAINMENU"), TEXT("Main Menu"),  EElysiumMenuCommand::MainMenu, true },
		};

	case EElysiumMenuMode::GameOver:
		// The run is over: there is nothing to continue, reload without a save is the same dead
		// run, and saving a corpse is not offered. Load lands with 11.9.
		return {
			{ TEXT("VMainMenu_BTN_LOADGAME"), TEXT("Load Game"), EElysiumMenuCommand::LoadGame, false },
			{ TEXT("VMainMenu_BTN_MAINMENU"), TEXT("Main Menu"), EElysiumMenuCommand::MainMenu, true },
			{ TEXT("VMainMenu_BTN_QUIT"),     TEXT("Quit"),      EElysiumMenuCommand::Quit,     true },
		};

	default:
		// Save Game is disabled out of game — retail's one main-menu/pause difference, reproduced.
		return {
			{ TEXT("VMainMenu_BTN_NEWGAME"),  TEXT("New Game"),  EElysiumMenuCommand::NewGame,  true },
			{ TEXT("VMainMenu_BTN_LOADGAME"), TEXT("Load Game"), EElysiumMenuCommand::LoadGame, false },
			{ TEXT("VMainMenu_BTN_SAVEGAME"), TEXT("Save Game"), EElysiumMenuCommand::SaveGame, false },
			{ TEXT("VMainMenu_BTN_OPTIONS"),  TEXT("Options"),   EElysiumMenuCommand::Options,  false },
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
		// Pause is the only mode Escape leaves: the front end has nothing behind it to go back to,
		// and a lost run is not dismissible. In those two it is still consumed, so the key cannot
		// reach the game underneath.
		if (Mode == EElysiumMenuMode::Pause)
		{
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UElysiumGameFlowSubsystem* Flow = GI->GetSubsystem<UElysiumGameFlowSubsystem>())
				{
					Flow->SetPaused(false);
				}
			}
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, KeyEvent);
}

TSharedRef<SWidget> UElysiumMainMenu::RebuildWidget()
{
	const TArray<FMenuEntry> Items = BuildItemSet();

	// Everything below is authored in VtMB's 1024x768 virtual canvas; the SDPIScaler at the root
	// converts once. So the font is measured at its *virtual* size and the recovered padding
	// constants are used verbatim.
	FElysiumUIFontLibrary& Fonts = UIFonts();
	const FSlateFontInfo ItemFont =
		Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::Regular,
		           ElysiumUI::Type::MenuItem, /*Scale*/ 1.0f);

	// CVMainMenu sizes every button to the widest label, so the column is one uniform block.
	const TSharedRef<FSlateFontMeasure> Measure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	float MaxW = 0.0f;
	float MaxH = 0.0f;
	TArray<FText> Labels;
	Labels.Reserve(Items.Num());
	for (const FMenuEntry& Item : Items)
	{
		const FText Label = FElysiumUIStrings::Get().Resolve(Item.Token, Item.Fallback);
		const FVector2D Size = Measure->Measure(Label.ToString(), ItemFont);
		MaxW = FMath::Max(MaxW, static_cast<float>(Size.X));
		MaxH = FMath::Max(MaxH, static_cast<float>(Size.Y));
		Labels.Add(Label);
	}
	// FUN_100660e0: btnW = maxLabelW + 20, btnH = maxLabelH + 4, pitch = btnH + 2 (virtual px).
	const float ButtonW = MaxW + 20.0f;
	const float ButtonH = MaxH + 4.0f;
	const float Gutter = 2.0f;

	const TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FMenuEntry& Item = Items[i];
		const EElysiumMenuCommand Command = Item.Command;
		const bool bEnabled = Item.bEnabled;

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
				SNew(SBox)
				.WidthOverride(ButtonW)
				.HeightOverride(ButtonH)
				[
					Button
				]
			];
	}

	// The head of the screen. The title lockup is the front door's, read from the user's own install;
	// the game-over screen states why the run ended instead, because the wordmark is not what that
	// moment is about.
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
		// Absent (no export yet) -> the wordmark is set in type instead, so the menu still reads
		// rather than showing a hole.
		if (!TitleTexture)
		{
			TitleTexture = ElysiumUI::LoadPngTexture(FElysiumContentPaths::UiTitle());
		}
		if (TitleTexture)
		{
			TitleBrush = MakeShared<FSlateBrush>();
			TitleBrush->SetResourceObject(TitleTexture);
			TitleBrush->ImageSize = FVector2D(TitleVirtualWidth, TitleVirtualWidth / TitleAspect);
			TitleBrush->DrawAs = ESlateBrushDrawType::Image;
			Title = SNew(SImage).Image(TitleBrush.Get());
		}
		else
		{
			UE_LOG(LogElysiumMenu, Warning,
				TEXT("no title lockup at %s — run: python tools/UE_extract_ui.py"),
				*FElysiumContentPaths::UiTitle());
			Title = SNew(STextBlock)
				.Text(NSLOCTEXT("Elysium", "TitleFallback", "Elysium"))
				.Font(Fonts.Font(EElysiumFontRole::Label, EElysiumFontWeight::SemiBold,
				                 ElysiumUI::Type::Display, 1.0f))
				.ColorAndOpacity(FSlateColor(ElysiumUI::Palette::GoldLit));
		}
	}

	// A scrim under the whole screen. VtMB needs none — every background colour in VampireScheme is
	// fully transparent because its menu floats over a dark particle field. A real 3D backdrop is
	// not that reliable: how much the type needs depends entirely on what the camera is pointed at,
	// so the amount is a knob rather than a constant. A night street wants very little; a sunlit
	// wall wanted a lot. `elysium.MenuScrim`.
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
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SImage).Image(ScrimBrush.Get())
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SDPIScaler)
			.DPIScale_Lambda([this]() { return VirtualScale(); })
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
			]
		];
}

void UElysiumMainMenu::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	TitleBrush.Reset();
	ScrimBrush.Reset();
}
