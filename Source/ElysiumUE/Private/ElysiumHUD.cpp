#include "ElysiumHUD.h"

#include "ElysiumContentPaths.h"
#include "ElysiumDialogueWidget.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumLightProbe.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumSignData.h"
#include "ElysiumSignFonts.h"

#include "Engine/GameViewportClient.h"
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

namespace
{
	// Decode a PNG off disk into a transient BGRA texture. Shared by the PL3 use-icon atlas and the
	// PL5c sign backgrounds — both are offline-decoded RGBA sheets read 1:1, not game-content
	// textures (those go through FElysiumTextureCache). Returns null on any failure.
	UTexture2D* LoadPngTexture(const FString& PngPath)
	{
		TArray<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *PngPath))
		{
			return nullptr;
		}
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		TArray64<uint8> Raw;
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()) ||
			!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
		{
			return nullptr;
		}
		const int32 W = Wrapper->GetWidth();
		const int32 H = Wrapper->GetHeight();
		UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!Tex)
		{
			return nullptr;
		}
		Tex->SRGB = true;
		Tex->NeverStream = true;
		FTexturePlatformData* PlatformData = Tex->GetPlatformData();
		void* Dest = PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Dest, Raw.GetData(), int64(W) * H * 4);
		PlatformData->Mips[0].BulkData.Unlock();
		Tex->UpdateResource();
		return Tex;
	}
}

AElysiumHUD::AElysiumHUD()
{
	// The HUD ticks so it can manage the Slate dialogue box (add/refresh/remove) off the entity world's
	// open-conversation state, independent of the Canvas DrawHUD pass.
	PrimaryActorTick.bCanEverTick = true;
}

void AElysiumHUD::BeginPlay()
{
	Super::BeginPlay();

	// elysium.lights — show/hide the real-time light rig (A/B the world with and without it).
	LightsCmd = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.lights"),
		TEXT("elysium.lights — toggle the real-time light rig"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			if (AElysiumMapActor* Map = ResolveMapActor())
			{
				Map->ToggleLights();
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
				Map->ToggleProps();
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
	Super::EndPlay(EndPlayReason);
}

void AElysiumHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateDialogue();
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

	// Centre reticle — always on, independent of the debug overlay and any Cog window, so it never
	// flickers in/out with what happens to be open. The HUD renders every frame in -game and PIE, so
	// this is the one reliable always-visible surface. While the +use look-cursor is on a usable
	// entity (P4.4), the reticle swaps to VtMB's context cursor (ring frame + the entity's use_icon /
	// locked_icon); otherwise it is the plain aim cross.
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;

	int32 AimedIcon = 0;
	// A sign panel with HideHUD set covers the game: no reticle under it (P4.10).
	bool bSignHidesHUD = false;
	if (const AElysiumMapActor* MapForUse = ResolveMapActor())
	{
		if (const FElysiumEntityWorld* World = MapForUse->GetEntityWorld())
		{
			AimedIcon = World->GetAimedUseIcon();
			const FElysiumSignData* OpenSign = World->GetOpenSignData();
			bSignHidesHUD = OpenSign && OpenSign->bHideHUD;
		}
	}

	EnsureUseIconAtlas();
	if (bSignHidesHUD)
	{
		// nothing — the panel owns the screen
	}
	else if (AimedIcon > 0 && UseAtlas.IsValid() && UseIconUV.Contains(AimedIcon))
	{
		DrawUseReticle(CX, CY, AimedIcon);
	}
	else
	{
		DrawLine(CX - 7.f, CY, CX + 7.f, CY, FLinearColor(1, 1, 1, 0.7f), 1.2f);
		DrawLine(CX, CY - 7.f, CX, CY + 7.f, FLinearColor(1, 1, 1, 0.7f), 1.2f);
	}

	// P4.10 game_sign — the open sign/popup window, over the world and the reticle but under the
	// env_fade quad (a fade-to-black covers everything, panel included).
	DrawSignPanel();

	// P4.5 env_fade — a full-screen colour quad over everything (reticle included), driven by the
	// entity world's single screen-fade state (Fade input); the fade covers the whole viewport.
	if (const AElysiumMapActor* MapForFade = ResolveMapActor())
	{
		FLinearColor FadeColor;
		if (const FElysiumEntityWorld* World = MapForFade->GetEntityWorld())
		{
			if (World->GetScreenFade(FadeColor))
			{
				DrawRect(FadeColor, 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
			}
		}
	}

}

// --- +use context-icon reticle (P4.4) -------------------------------------------------------

void AElysiumHUD::EnsureUseIconAtlas()
{
	if (bUseAtlasLoadAttempted)
	{
		return;   // one shot — success or failure (a missing atlas just means the plain cross)
	}
	bUseAtlasLoadAttempted = true;

	// Layout + per-icon UVs from the PL3 sidecar (out/hud/use_icons.json): { ring:{u0,v0,u1,v1},
	// icons:[{n, u0,v0,u1,v1}, ...] }. The atlas is the sibling PNG.
	const FString JsonPath = FElysiumContentPaths::Root() / TEXT("hud/use_icons.json");
	const FString PngPath = FElysiumContentPaths::Root() / TEXT("hud/use_icons.png");

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *JsonPath))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}

	auto ReadUV = [](const TSharedPtr<FJsonObject>& Obj) -> FBox2D
	{
		return FBox2D(
			FVector2D(Obj->GetNumberField(TEXT("u0")), Obj->GetNumberField(TEXT("v0"))),
			FVector2D(Obj->GetNumberField(TEXT("u1")), Obj->GetNumberField(TEXT("v1"))));
	};

	if (const TSharedPtr<FJsonObject>* RingObj; Root->TryGetObjectField(TEXT("ring"), RingObj))
	{
		UseRingUV = ReadUV(*RingObj);
	}
	const TArray<TSharedPtr<FJsonValue>>* Icons = nullptr;
	if (Root->TryGetArrayField(TEXT("icons"), Icons))
	{
		for (const TSharedPtr<FJsonValue>& V : *Icons)
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (Obj.IsValid())
			{
				const int32 N = (int32)Obj->GetNumberField(TEXT("n"));
				UseIconUV.Add(N, ReadUV(Obj));
			}
		}
	}

	UseAtlas.Reset(LoadPngTexture(PngPath));
}

void AElysiumHUD::DrawUseReticle(float CenterX, float CenterY, int32 IconIndex)
{
	// A single context cursor: the icon cell centred on the crosshair, framed by the ring. Sized to
	// the viewport height so it reads at any resolution (clamped to a sane pixel range).
	const float Size = FMath::Clamp(Canvas->ClipY * 0.055f, 40.f, 96.f);
	const FVector2D Pos(CenterX - Size * 0.5f, CenterY - Size * 0.5f);
	const FVector2D Extent(Size, Size);
	FTextureResource* Res = UseAtlas->GetResource();

	auto DrawCell = [&](const FBox2D& UV)
	{
		if (!UV.bIsValid)
		{
			return;
		}
		FCanvasTileItem Tile(Pos, Res, Extent, UV.Min, UV.Max, FLinearColor::White);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	};

	// Icon first, then the ring frame on top (its centre is transparent, so the icon shows through).
	DrawCell(UseIconUV[IconIndex]);
	DrawCell(UseRingUV);
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

void AElysiumHUD::DrawSignPanel()
{
	if (CVarDrawSigns.GetValueOnGameThread() == 0)
	{
		return;
	}
	const AElysiumMapActor* Map = ResolveMapActor();
	const FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	if (!World)
	{
		return;
	}
	const FElysiumSignData* Sign = World->GetOpenSignData();
	if (!Sign || !Sign->bParsed)
	{
		return;
	}

	const float ScreenW = Canvas->ClipX;
	const float ScreenH = Canvas->ClipY;

	// fade_in ramps the whole panel up; the game clock drives it, matching every other timed
	// entity state. (fade_out is applied by the dismissal path, which tears the panel down
	// immediately in this slice — a fading-out panel needs a lingering copy, deferred to 8.8.)
	double OpenTime = 0.0;
	World->GetOpenSign(&OpenTime);
	const float FadeIn = World->GetOpenSignFadeIn();
	float Alpha = 1.0f;
	if (FadeIn > KINDA_SMALL_NUMBER)
	{
		Alpha = FMath::Clamp(float(World->NowSeconds() - OpenTime) / FadeIn, 0.0f, 1.0f);
	}

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

void AElysiumHUD::UpdateDialogue()
{
	AElysiumMapActor* Map = ResolveMapActor();
	FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	FElysiumDlgConversation* Conv = World ? World->GetOpenDialog() : nullptr;

	if (!Conv)
	{
		if (DialogueWidget.IsValid())
		{
			TeardownDialogue();
		}
		return;
	}

	// Nothing changed since the last rebuild — leave the retained widget alone.
	if (Conv == DialogueConv && Conv->Revision() == DialogueRev && DialogueWidget.IsValid())
	{
		return;
	}

	// Snapshot this turn: the speaker, the current NPC line, and its visible choice labels.
	FString Speaker;
	if (const FElysiumEntity* OwnerEnt = World->Resolve(World->GetOpenDialogOwner()))
	{
		Speaker = OwnerEnt->Def ? OwnerEnt->Def->TargetName : FString();
	}
	const bool bMale = Conv->PlayerMale();
	const bool bMalk = Conv->PlayerMalkavian();
	const FElysiumDlgLine* NpcLine = Conv->CurrentNpcLine();
	const FString LineText = NpcLine ? NpcLine->DisplayText(bMale, bMalk) : FString();

	TArray<FString> Choices;
	for (int32 v = 0; v < Conv->VisibleChoices().Num(); ++v)
	{
		if (const FElysiumDlgLine* Choice = Conv->VisibleChoice(v))
		{
			Choices.Add(Choice->DisplayText(bMale, bMalk));
		}
	}
	const bool bTerminal = Conv->IsTerminalLine();

	// Rebuild the box for the new turn (turns are user-paced, so a full rebuild is cheap).
	if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		if (DialogueWidget.IsValid())
		{
			Viewport->RemoveViewportWidgetContent(DialogueWidget.ToSharedRef());
		}
		DialogueWidget = SNew(SElysiumDialogueBox)
			.Speaker(Speaker)
			.Line(LineText)
			.Choices(Choices)
			.bTerminal(bTerminal)
			.OnChoose(FElysiumOnDlgChoice::CreateUObject(this, &AElysiumHUD::OnDialogueChoice));
		Viewport->AddViewportWidgetContent(DialogueWidget.ToSharedRef(), /*ZOrder*/ 100);
	}

	// Freeze the world into UI-only input and give the box keyboard focus (number-key selection), once,
	// for the life of the conversation.
	if (!bDialogueInput && PlayerOwner)
	{
		FInputModeUIOnly Mode;
		if (DialogueWidget.IsValid())
		{
			Mode.SetWidgetToFocus(DialogueWidget);
		}
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerOwner->SetInputMode(Mode);
		PlayerOwner->bShowMouseCursor = true;
		bDialogueInput = true;
	}
	else if (DialogueWidget.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(DialogueWidget);
	}

	DialogueConv = Conv;
	DialogueRev = Conv->Revision();
}

void AElysiumHUD::TeardownDialogue()
{
	if (DialogueWidget.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(DialogueWidget.ToSharedRef());
		}
		DialogueWidget.Reset();
	}
	if (bDialogueInput && PlayerOwner)
	{
		PlayerOwner->SetInputMode(FInputModeGameOnly());
		PlayerOwner->bShowMouseCursor = false;
		bDialogueInput = false;
	}
	DialogueConv = nullptr;
	DialogueRev = 0;
}

void AElysiumHUD::OnDialogueChoice(int32 VisibleIndex)
{
	AElysiumMapActor* Map = ResolveMapActor();
	FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	if (!World)
	{
		return;
	}
	// -1 is the terminal "continue"; otherwise the Nth visible PC choice. Both route through the world
	// chokepoint; the next Tick reflects the new turn (or tears the box down when the conversation ends).
	if (VisibleIndex < 0)
	{
		World->PlayerDialogAdvance();
	}
	else
	{
		World->PlayerDialogChoose(VisibleIndex);
	}
}
