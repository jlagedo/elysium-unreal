#include "ElysiumHUD.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPawn.h"

#include "CanvasItem.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TextureResource.h"

namespace
{
	// Unreal (cm, left-handed) -> Source (inches, right-handed): the inverse of the
	// (sx,-sy,sz)*2.54 load transform.
	FVector UnrealToSource(const FVector& U)
	{
		return FVector(U.X / 2.54, -U.Y / 2.54, U.Z / 2.54);
	}

	const FLinearColor ColLabel(0.60f, 0.66f, 0.74f);
	const FLinearColor ColValue(0.86f, 0.94f, 1.00f);
	const FLinearColor ColOn(0.45f, 1.00f, 0.62f);
}

void AElysiumHUD::BeginPlay()
{
	Super::BeginPlay();

	// `elysium.debug` mirrors F1: type it in the engine console (' or `) to toggle the
	// overlay. Namespaced alongside elysium.map / elysium.maps for shared autocomplete.
	DebugCmd = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("elysium.debug"),
		TEXT("elysium.debug — toggle the debug overlay"),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]() { ToggleDebug(); }),
		ECVF_Cheat);

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
}

void AElysiumHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (DebugCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(DebugCmd);
		DebugCmd = nullptr;
	}
	if (LightsCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(LightsCmd);
		LightsCmd = nullptr;
	}
	if (PropsCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(PropsCmd);
		PropsCmd = nullptr;
	}
	Super::EndPlay(EndPlayReason);
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
	if (const AElysiumMapActor* MapForUse = ResolveMapActor())
	{
		if (const FElysiumEntityWorld* World = MapForUse->GetEntityWorld())
		{
			AimedIcon = World->GetAimedUseIcon();
		}
	}

	EnsureUseIconAtlas();
	if (AimedIcon > 0 && UseAtlas.IsValid() && UseIconUV.Contains(AimedIcon))
	{
		DrawUseReticle(CX, CY, AimedIcon);
	}
	else
	{
		DrawLine(CX - 7.f, CY, CX + 7.f, CY, FLinearColor(1, 1, 1, 0.7f), 1.2f);
		DrawLine(CX, CY - 7.f, CX, CY + 7.f, FLinearColor(1, 1, 1, 0.7f), 1.2f);
	}

	// P4.5 env_fade — a full-screen colour quad over everything (reticle included), driven by the
	// entity world's single screen-fade state (Fade input). Drawn before the debug overlay's early
	// return so it shows regardless of the debug toggle; the fade covers the whole viewport.
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

	if (!bShowDebug)
	{
		return;
	}

	const float Dt = GetWorld()->GetDeltaSeconds();
	if (Dt > 0.f)
	{
		SmoothedFPS = FMath::FInterpTo(SmoothedFPS, 1.f / Dt, Dt, 4.f);
	}

	UFont* Font = GEngine->GetMediumFont();

	APlayerController* PC = GetOwningPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	FVector ViewLoc = FVector::ZeroVector;
	FRotator ViewRot = FRotator::ZeroRotator;
	if (PC)
	{
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	}
	const FVector Met = ViewLoc / 100.0;
	const FVector Src = UnrealToSource(ViewLoc);

	// Always-available position overlay. The map/collision/light counts moved to the Maps + Status
	// Cog windows, and "what am I aiming at" to the Entity Inspector's live crosshair readout
	// (debug-tooling.md: the HUD keeps only the FPS/position overlay, always visible in -game).
	const float X = 16.f;
	float Y = 16.f;
	const float LineH = 18.f;
	const int32 NumLines = 3;
	DrawRect(FLinearColor(0, 0, 0, 0.62f), X - 8.f, Y - 8.f, 360.f, NumLines * LineH + 16.f);

	auto Row = [&](const FString& Text, const FLinearColor& Color)
	{
		DrawText(Text, Color, X, Y, Font);
		Y += LineH;
	};

	// Player pose: metres + look, then the Source-unit position.
	Row(FString::Printf(TEXT("you  (%.1f, %.1f, %.1f) m    yaw %.0f°"),
		Met.X, Met.Y, Met.Z, ViewRot.Yaw), ColValue);
	Row(FString::Printf(TEXT("src  (%.0f, %.0f, %.0f)"), Src.X, Src.Y, Src.Z), ColLabel);

	// Movement mode + skybox state.
	AElysiumMapActor* MapActor = ResolveMapActor();
	const bool bNoclip = Cast<AElysiumPawn>(Pawn) && Cast<AElysiumPawn>(Pawn)->IsNoclip();
	const bool bSky = MapActor && MapActor->IsSkyboxVisible();
	const bool bLights = MapActor && MapActor->AreLightsVisible();
	Row(FString::Printf(TEXT("mode %s    sky %s    lights %s"),
		bNoclip ? TEXT("NOCLIP") : TEXT("WALK"),
		bSky ? TEXT("ON") : TEXT("OFF"),
		bLights ? TEXT("ON") : TEXT("OFF")), bNoclip ? ColOn : ColValue);

	// FPS: a big green number top-right, so a frame-rate hit is obvious at a glance.
	UFont* Big = GEngine->GetLargeFont();
	DrawText(FString::Printf(TEXT("%.0f"), SmoothedFPS), ColOn, Canvas->ClipX - 96.f, 12.f, Big);
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

	// Decode the PNG into a transient BGRA texture (the same path FElysiumTextureCache uses for
	// world textures; the atlas is a hand-authored HUD sheet, not game content).
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *PngPath))
	{
		return;
	}
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	TArray64<uint8> Raw;
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()) ||
		!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		return;
	}
	const int32 W = Wrapper->GetWidth();
	const int32 H = Wrapper->GetHeight();
	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Tex)
	{
		return;
	}
	Tex->SRGB = true;
	Tex->NeverStream = true;
	FTexturePlatformData* PlatformData = Tex->GetPlatformData();
	void* Dest = PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dest, Raw.GetData(), int64(W) * H * 4);
	PlatformData->Mips[0].BulkData.Unlock();
	Tex->UpdateResource();
	UseAtlas.Reset(Tex);
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
