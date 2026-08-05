#include "ElysiumHUD.h"

#include "ElysiumContentPaths.h"
#include "ElysiumInputSubsystem.h"
#include "ElysiumPlayerUISubsystem.h"
#include "Debug/ElysiumLightProbe.h"
#include "ElysiumMapActor.h"
#include "Visual/ElysiumMapVisuals.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPresentationSubsystem.h"
#include "Substrate/ElysiumSignData.h"
#include "Visual/ElysiumSignFonts.h"
#include "UI/ElysiumDialogueScreen.h"
#include "UI/ElysiumUITexture.h"

#include "GameFramework/PlayerController.h"

#include "CanvasItem.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumHUD, Log, All);

// Draw game_sign / popup panels (1) or suppress them (0). Signs are gameplay UI, so this defaults
// on; the headless screenshot harness turns it off so a map-load popup does not cover every capture
// (e.g. the tutorial's linux_check logic_pythoncheck warns when its Python scripts were not compiled
// under a Linux/Wine environment, and fires a game_sign every load).
static TAutoConsoleVariable<int32> CVarDrawSigns(
	TEXT("elysium.DrawSigns"), 1,
	TEXT("Draw game_sign/popup panels (1) or suppress them (0)."),
	ECVF_Default);

// The PNG->transient-texture decode moved to ElysiumUITexture.{h,cpp} when the 8.6 title lockup
// became its third consumer (after the PL3 use-icon atlas and the PL5c sign backgrounds).
using ElysiumUI::LoadPngTexture;

AElysiumHUD::AElysiumHUD()
{
	// The HUD does not tick. The Slate dialogue box and the sign's input scope are reconciled from
	// the publisher's OnViewPublished, which runs in step 9 of the frame (TG_PostUpdateWork); an
	// actor tick would run in TG_PrePhysics and therefore always act on the previous frame's state.
	PrimaryActorTick.bCanEverTick = false;
}

void AElysiumHUD::BeginPlay()
{
	Super::BeginPlay();

	// 11.8 — the one inbound seam. Everything drawn below comes from the state this hands over.
	if (UElysiumPresentationSubsystem* P = Presentation())
	{
		ViewPublishedHandle = P->OnViewPublished().AddUObject(this, &AElysiumHUD::OnViewPublished);
	}

	// elysium.lights — show/hide the real-time light rig (A/B the world with and without it).
	LightsCmd = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.lights"),
		TEXT("elysium.lights — toggle the real-time light rig"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			if (AElysiumMapActor* Map = ResolveMapActor())
			{
				if (UElysiumMapVisuals* Visuals = Map->GetVisuals()) { Visuals->ToggleLights(); }
			}
		}),
		ECVF_Cheat);

	// elysium.props — show/hide the static-prop instances.
	PropsCmd = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.props"),
		TEXT("elysium.props — toggle the static props"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			if (AElysiumMapActor* Map = ResolveMapActor())
			{
				if (UElysiumMapVisuals* Visuals = Map->GetVisuals()) { Visuals->ToggleProps(); }
			}
		}),
		ECVF_Cheat);

#if !UE_BUILD_SHIPPING
	// elysium.lightprobe [rays] — trace every light against the real scene and write the
	// attribution features (what each light is nearest, and how much of its own patch it lights)
	// alongside the Lights window's hand survey.
	LightProbeCmd = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.lightprobe"),
		TEXT("elysium.lightprobe [rays] — probe every light against the scene, write <map>.probe.json"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			AElysiumMapActor* Map = ResolveMapActor();
			const int32 Rays = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 64;
			const int32 N = ElysiumLightProbe::Run(GetWorld(), Map, Rays);
			if (N < 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("elysium.lightprobe: no light rig on this map"));
			}
		}),
		ECVF_Cheat);
#endif
}

void AElysiumHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ViewPublishedHandle.IsValid())
	{
		if (UElysiumPresentationSubsystem* P = Presentation())
		{
			P->OnViewPublished().Remove(ViewPublishedHandle);
		}
		ViewPublishedHandle.Reset();
	}
	if (LightsCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(LightsCmd);
		LightsCmd = nullptr;
	}
#if !UE_BUILD_SHIPPING
	if (LightProbeCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(LightProbeCmd);
		LightProbeCmd = nullptr;
	}
#endif
	if (PropsCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(PropsCmd);
		PropsCmd = nullptr;
	}
	TeardownDialogue();
	// The scope stack outlives this world (it is the local player's), so a map epoch ending has to
	// return what it borrowed or the next map boots with a sign's claim still on the stack.
	if (UElysiumInputSubsystem* Input = UElysiumInputSubsystem::Get(GetGameInstance()))
	{
		Input->Pop(SignScope);
	}
	SignScope.Reset();
	Super::EndPlay(EndPlayReason);
}

UElysiumPresentationSubsystem* AElysiumHUD::Presentation() const
{
	return UElysiumPresentationSubsystem::Get(GetWorld());
}

const FElysiumViewState& AElysiumHUD::View() const
{
	// A world with no publisher (there is none before OnWorldBeginPlay, and none at all in an editor
	// preview world) reads as "nothing on screen" rather than as a special case at every draw site.
	static const FElysiumViewState Empty;
	const UElysiumPresentationSubsystem* P = Presentation();
	return P ? P->View() : Empty;
}

void AElysiumHUD::OnViewPublished(const FElysiumViewState& NewView)
{
	// The sign's scope tracks the published panel, so a sign that opens behind a menu claims nothing
	// until the menu closes and the panel is published again — the scope and the panel are raised and
	// dropped by the same fact.
	UpdateSignScope(NewView.Sign != nullptr);

	switch (ElysiumView::ReconcileDialogue(ShownConv, ShownRev, NewView.Dialogue))
	{
	case ElysiumView::EDialogueAction::Rebuild:
		RebuildDialogue(NewView.Dialogue);
		break;
	case ElysiumView::EDialogueAction::Teardown:
		// Reached both when the conversation ends and when a screen comes up over it: the publisher
		// withholds the whole player-facing surface, so the box comes down instead of drawing
		// through the menu. The conversation itself is untouched in the entity world — `sm_hub_1`'s
		// havenbum panhandles behind the main menu exactly as it does in play — and the box is
		// rebuilt from the next publish once the screen closes.
		TeardownDialogue();
		break;
	case ElysiumView::EDialogueAction::None:
		break;
	}
}

AElysiumMapActor* AElysiumHUD::ResolveMapActor() const
{
	const UGameInstance* GI = GetGameInstance();
	const UElysiumMapSubsystem* Maps = GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	return Maps ? Maps->GetCurrentMap() : nullptr;
}

void AElysiumHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const FElysiumViewState& V = View();

	// The local-player HUD subsystem owns the reticle, vitals and full-screen fade. Canvas remains
	// only as the faithful game_sign bridge in this slice; dialogue is already retained Slate.
	DrawSignPanel(V);
}

// --- Sign / popup window (P4.10) ------------------------------------------------------------

UTexture2D* AElysiumHUD::GetSignBackground(const FString& ImageName)
{
	if (const TStrongObjectPtr<UTexture2D>* Cached = SignBackgrounds.Find(ImageName))
	{
		return Cached->Get();   // may be null: a failed decode is cached so it is not retried
	}

	// The PL5c manifest maps a material name to its decoded PNG, so the runtime never has to
	// reproduce the exporter's safe-name rule.
	if (!bSignManifestLoaded)
	{
		bSignManifestLoaded = true;
		FString JsonText;
		if (FFileHelper::LoadFileToString(JsonText, *FElysiumContentPaths::SignBackgrounds()))
		{
			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
			if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
			{
				const TSharedPtr<FJsonObject>* Backgrounds = nullptr;
				if (Root->TryGetObjectField(TEXT("backgrounds"), Backgrounds))
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Backgrounds)->Values)
					{
						const TSharedPtr<FJsonObject> Obj = Pair.Value->AsObject();
						if (Obj.IsValid())
						{
							SignBackgroundFiles.Add(Pair.Key.ToLower(), Obj->GetStringField(TEXT("png")));
						}
					}
				}
			}
		}
	}

	UTexture2D* Tex = nullptr;
	if (const FString* File = SignBackgroundFiles.Find(ImageName))
	{
		Tex = LoadPngTexture(FElysiumContentPaths::SignTexDir() / *File);
	}
	SignBackgrounds.Add(ImageName, TStrongObjectPtr<UTexture2D>(Tex));
	return Tex;
}

void AElysiumHUD::DrawSignPanel(const FElysiumViewState& V)
{
	if (CVarDrawSigns.GetValueOnGameThread() == 0)
	{
		return;
	}
	const FElysiumSignData* Sign = V.Sign;
	if (!Sign || !Sign->bParsed)
	{
		return;
	}

	const float ScreenW = Canvas->ClipX;
	const float ScreenH = Canvas->ClipY;

	// fade_in ramps the whole panel up; the publisher resolves the ramp against the game clock,
	// matching every other timed entity state. (fade_out is applied by the dismissal path, which
	// tears the panel down immediately in this slice — a fading-out panel needs a lingering copy,
	// deferred to 8.8.)
	const float Alpha = V.SignAlpha;

	// --- panel rect + background -------------------------------------------------------------
	// The BackgroundImage block sizes the sign panel itself, so its rect is also the origin every
	// text block is placed against (they are VGUI children of the panel). With no background the
	// panel keeps its constructed full-screen bounds.
	FVector2D PanelPos = FVector2D::ZeroVector;
	if (Sign->Background.bValid)
	{
		const int32 W = Sign->Background.bHasWide ? Sign->Background.Wide : int32(ScreenW);
		const int32 H = Sign->Background.bHasTall ? Sign->Background.Tall : int32(ScreenH);
		FVector2D Size;
		ElysiumSign::RectToScreen(Sign->Background.XPos, Sign->Background.YPos, W, H,
			ScreenW, ScreenH, Sign->Background.bCentre, PanelPos, Size);

		if (UTexture2D* Tex = GetSignBackground(Sign->Background.ImageName))
		{
			FCanvasTileItem Tile(PanelPos, Tex->GetResource(), Size,
				FVector2D(0, 0), FVector2D(1, 1), FLinearColor(1, 1, 1, Alpha));
			Tile.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Tile);
		}
		UE_LOG(LogElysiumHUD, Verbose,
			TEXT("sign '%s' screen %.0fx%.0f | authored %d,%d %dx%d centre=%d | panel %.0f,%.0f %.0fx%.0f"),
			*Sign->SourceFile, ScreenW, ScreenH, Sign->Background.XPos, Sign->Background.YPos,
			W, H, Sign->Background.bCentre ? 1 : 0, PanelPos.X, PanelPos.Y, Size.X, Size.Y);
	}

	// --- text blocks -----------------------------------------------------------------------
	// Vector type: each block's authored face name (ParagraphText, Newsprint, Headline, ...) resolves
	// through the font library to a committed OFL face as a FSlateFontInfo already sized in pixels for
	// this viewport (SignFonts::ResolveFace scales the face's virtual size by ScaleFor(ScreenH)), so
	// the layout stays proportional at any resolution with no per-item Canvas scale. Measurement and
	// wrapping run through the Slate font-measure service to match what is rasterised.
	if (!SignFonts.IsValid())
	{
		SignFonts = MakePimpl<FElysiumSignFontLibrary>();
	}
	if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		return;   // no Slate renderer (e.g. a headless path) — nothing to measure or draw against
	}
	const TSharedRef<FSlateFontMeasure> FontMeasure =
		FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const int32 ScreenWpx = FMath::RoundToInt(ScreenW);

	for (const FElysiumSignTextBlock& Block : Sign->Blocks)
	{
		// Child of the sign panel: scaled by the same factor, offset by the panel's own origin
		// (which already carries the horizontal letterbox offset).
		const double S = ElysiumSign::ScaleFor(ScreenH);
		const FVector2D Pos(PanelPos.X + double(int32(S * Block.XPos)),
			PanelPos.Y + double(int32(S * Block.YPos)));
		const FVector2D Size(double(int32(S * Block.Wide)), double(int32(S * Block.Tall)));

		if (Block.BackgroundColor.A > KINDA_SMALL_NUMBER)
		{
			FLinearColor Back = Block.BackgroundColor;
			Back.A *= Alpha;
			DrawRect(Back, Pos.X, Pos.Y, Size.X, Size.Y);
		}
		if (Block.Text.IsEmpty())
		{
			continue;
		}

		FLinearColor TextColor = Block.TextColor;
		TextColor.A *= Alpha;

		const FSlateFontInfo BlockFont = SignFonts->ResolveFace(Block.ResolveFont(ScreenWpx), ScreenH);
		const float LineHeight = float(FontMeasure->GetMaxCharacterHeight(BlockFont));

		// Authored newlines are hard breaks; each resulting paragraph word-wraps to the block width.
		TArray<FString> Paragraphs;
		Block.Text.ParseIntoArray(Paragraphs, TEXT("\n"), /*CullEmpty*/ false);

		const bool bCentreText = Block.Alignment == TEXT("center");
		float LineY = Pos.Y;

		for (FString Paragraph : Paragraphs)
		{
			Paragraph.ReplaceInline(TEXT("\r"), TEXT(""));
			TArray<FString> Words;
			Paragraph.ParseIntoArray(Words, TEXT(" "), /*CullEmpty*/ true);

			FString Line;
			auto FlushLine = [&]()
			{
				if (Line.IsEmpty())
				{
					LineY += LineHeight;   // a blank paragraph advances one line
					return;
				}
				const float LineW = float(FontMeasure->Measure(Line, BlockFont).X);
				const float DrawX = bCentreText ? Pos.X + (Size.X - LineW) * 0.5f : Pos.X;
				FCanvasTextItem Item(FVector2D(DrawX, LineY), FText::FromString(Line), BlockFont, TextColor);
				Canvas->DrawItem(Item);
				LineY += LineHeight;
				Line.Reset();
			};

			for (const FString& Word : Words)
			{
				const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
				const float CandidateW = float(FontMeasure->Measure(Candidate, BlockFont).X);
				if (!Line.IsEmpty() && CandidateW > Size.X)
				{
					FlushLine();
					Line = Word;
				}
				else
				{
					Line = Candidate;
				}
			}
			FlushLine();
		}
	}
}

// --- Dialogue box (P9 9.1 / B4) -------------------------------------------------------------

void AElysiumHUD::RebuildDialogue(const FElysiumDialogueView& Dialogue)
{
	// Rebuild the box for the new turn (turns are user-paced, so a full rebuild is cheap). Every
	// string is the publisher's — the speaker, the subtitle and the choice labels are already
	// resolved against the player's clan and gender, so the box never touches the `.dlg` data.
	TeardownDialogue();
	if (UElysiumPlayerUISubsystem* UI = UElysiumPlayerUISubsystem::Get(this))
	{
		DialogueScreen = Cast<UElysiumDialogueScreen>(UI->PushWidget(
			EElysiumUILayer::GameModal,
			UElysiumDialogueScreen::StaticClass(),
			[this, Dialogue](UCommonActivatableWidget& Widget)
		{
			UElysiumDialogueScreen* Screen = CastChecked<UElysiumDialogueScreen>(&Widget);
			Screen->SetDialogue(Dialogue);
			Screen->OnChoice.BindUObject(this, &AElysiumHUD::OnDialogueChoice);
			Screen->ConfigureInputScope(TEXT("Dialogue"), ElysiumInput::Priority::Dialogue);
		}));
	}

	ShownConv = Dialogue.Conversation;
	ShownRev = Dialogue.Revision;
}

void AElysiumHUD::TeardownDialogue()
{
	if (DialogueScreen)
	{
		if (UElysiumPlayerUISubsystem* UI = UElysiumPlayerUISubsystem::Get(this))
		{
			UI->RemoveWidget(EElysiumUILayer::GameModal, DialogueScreen);
		}
		DialogueScreen = nullptr;
	}
	ShownConv = nullptr;
	ShownRev = 0;
}

void AElysiumHUD::UpdateSignScope(bool bSignOpen)
{
	UElysiumInputSubsystem* Input = UElysiumInputSubsystem::Get(GetGameInstance());
	if (!Input)
	{
		return;
	}

	if (bSignOpen && !SignScope.IsValid())
	{
		FElysiumInputScope Scope;
		Scope.Name = TEXT("Sign");
		Scope.Priority = ElysiumInput::Priority::Sign;
		// Game input, no cursor: the panel is dismissed by a world click the player controller
		// binds, so taking the mouse away from the game would make it undismissable.
		Scope.Mode = EElysiumInputMode::GameOnly;
		Scope.Contexts.Add(ElysiumInput::PlayerGamepadContext());
		SignScope = Input->Push(MoveTemp(Scope));
	}
	else if (!bSignOpen && SignScope.IsValid())
	{
		Input->Pop(SignScope);
	}
}

void AElysiumHUD::OnDialogueChoice(int32 VisibleIndex)
{
	UElysiumPresentationSubsystem* P = Presentation();
	if (!P)
	{
		return;
	}
	// -1 is the terminal "continue"; otherwise the Nth visible PC choice. Player input goes back the
	// other way through the presenter, which resolves the world and hands it to the same
	// PlayerDialogChoose/PlayerDialogAdvance chokepoint every other caller uses; the next publish
	// reflects the new turn (or tears the box down when the conversation ends).
	if (VisibleIndex < 0)
	{
		P->DialogueAdvance();
	}
	else
	{
		P->DialogueChoose(VisibleIndex);
	}
}
