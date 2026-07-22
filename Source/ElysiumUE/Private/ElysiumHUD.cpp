#include "ElysiumHUD.h"

#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPawn.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// Unreal (cm, left-handed) -> Source (inches, right-handed): the inverse of the
	// (sx,-sy,sz)*2.54 load transform.
	FVector UnrealToSource(const FVector& U)
	{
		return FVector(U.X / 2.54, -U.Y / 2.54, U.Z / 2.54);
	}

	const FLinearColor ColHeader(1.0f, 0.82f, 0.50f);
	const FLinearColor ColLabel(0.60f, 0.66f, 0.74f);
	const FLinearColor ColValue(0.86f, 0.94f, 1.00f);
	const FLinearColor ColOn(0.45f, 1.00f, 0.62f);
	const FLinearColor ColOff(0.55f, 0.60f, 0.68f);
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
}

void AElysiumHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (DebugCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(DebugCmd);
		DebugCmd = nullptr;
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

	if (!bShowDebug || !Canvas)
	{
		return;
	}

	const float Dt = GetWorld()->GetDeltaSeconds();
	if (Dt > 0.f)
	{
		SmoothedFPS = FMath::FInterpTo(SmoothedFPS, 1.f / Dt, Dt, 4.f);
	}

	UFont* Font = GEngine->GetMediumFont();
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;

	// Centre crosshair.
	DrawLine(CX - 7.f, CY, CX + 7.f, CY, FLinearColor(1, 1, 1, 0.7f), 1.2f);
	DrawLine(CX, CY - 7.f, CX, CY + 7.f, FLinearColor(1, 1, 1, 0.7f), 1.2f);

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

	// Panel background.
	const float X = 16.f;
	float Y = 16.f;
	const float LineH = 18.f;
	const int32 NumLines = 6;
	DrawRect(FLinearColor(0, 0, 0, 0.62f), X - 8.f, Y - 8.f, 540.f, NumLines * LineH + 16.f);

	auto Row = [&](const FString& Text, const FLinearColor& Color)
	{
		DrawText(Text, Color, X, Y, Font);
		Y += LineH;
	};

	// Header: map + surface counts.
	AElysiumMapActor* MapActor = ResolveMapActor();
	if (MapActor)
	{
		Row(FString::Printf(TEXT("▸ %s    %d surf · %d sky"),
			*MapActor->LoadedMap, MapActor->WorldSurfaceCount, MapActor->SkySurfaceCount), ColHeader);
	}
	else
	{
		Row(TEXT("▸ (no map)"), ColHeader);
	}

	// Player pose: metres + look, then the Source-unit position.
	Row(FString::Printf(TEXT("you  (%.1f, %.1f, %.1f) m    yaw %.0f°"),
		Met.X, Met.Y, Met.Z, ViewRot.Yaw), ColValue);
	Row(FString::Printf(TEXT("src  (%.0f, %.0f, %.0f)"), Src.X, Src.Y, Src.Z), ColLabel);

	// Movement mode + skybox state.
	const bool bNoclip = Cast<AElysiumPawn>(Pawn) && Cast<AElysiumPawn>(Pawn)->IsNoclip();
	const bool bSky = MapActor && MapActor->IsSkyboxVisible();
	Row(FString::Printf(TEXT("mode %s    sky %s"),
		bNoclip ? TEXT("NOCLIP") : TEXT("WALK"),
		bSky ? TEXT("ON") : TEXT("OFF")), bNoclip ? ColOn : ColValue);

	// Crosshair pick: forward trace, report which mesh + where.
	Row(TEXT("─ aim ─"), ColLabel);
	FString AimText = TEXT("no hit");
	FLinearColor AimColor = ColOff;
	if (PC)
	{
		const FVector Start = ViewLoc;
		const FVector End = ViewLoc + ViewRot.Vector() * 1000000.0;
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ElysiumAim), true, Pawn);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Q))
		{
			const FString CompName = Hit.GetComponent() ? Hit.GetComponent()->GetName() : TEXT("?");
			const FVector HitSrc = UnrealToSource(Hit.ImpactPoint);
			AimText = FString::Printf(TEXT("%s  @ %.1f m   src %.0f %.0f %.0f"),
				*CompName, Hit.Distance / 100.f, HitSrc.X, HitSrc.Y, HitSrc.Z);
			AimColor = CompName.Contains(TEXT("Sky")) ? ColHeader : ColOn;
		}
	}
	Row(AimText, AimColor);

	// FPS: a big green number top-right, so a frame-rate hit is obvious at a glance.
	UFont* Big = GEngine->GetLargeFont();
	DrawText(FString::Printf(TEXT("%.0f"), SmoothedFPS), ColOn, Canvas->ClipX - 96.f, 12.f, Big);
}
