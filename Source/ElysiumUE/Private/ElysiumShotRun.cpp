#include "ElysiumShotRun.h"

#include "ElysiumContentPaths.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumScreenshot.h"
#include "ElysiumVantages.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RHI.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumShots, Log, All);

namespace
{
	// Frames to settle after the spawn hold releases, before the first vantage — same rationale as
	// the profiler's SettleFrames (drain first-frame stalls out of the picture).
	constexpr int32 BootSettleFrames = 30;
}

bool FElysiumShotRun::IsRequested()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("ElysiumShots")))
	{
		return false;
	}
	if (GUsingNullRHI)
	{
		UE_LOG(LogElysiumShots, Warning,
			TEXT("-ElysiumShots ignored: a screenshot under the null RHI is a blank frame."));
		return false;
	}
	return true;
}

FElysiumShotRun::FElysiumShotRun(UElysiumMapSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
	FParse::Value(FCommandLine::Get(), TEXT("ShotSettle="), SettleFrames);
	FParse::Value(FCommandLine::Get(), TEXT("ShotCam="), CamSelector);
	SettleFrames = FMath::Max(1, SettleFrames);

	// Clean plates: suppress game_sign/popup panels so a map-load popup (the tutorial's Loader.exe
	// warning) does not cover every shot. The regression baseline is the world, not gameplay UI.
	if (IConsoleVariable* DrawSigns = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.DrawSigns")))
	{
		DrawSigns->Set(0, ECVF_SetByCode);
	}

	UE_LOG(LogElysiumShots, Log, TEXT("headless screenshot run armed: settle %d frames."), SettleFrames);

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FElysiumShotRun::Tick));
}

FElysiumShotRun::~FElysiumShotRun()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
}

UWorld* FElysiumShotRun::GetWorld() const
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const UGameInstance* GI = Sub ? Sub->GetGameInstance() : nullptr;
	return GI ? GI->GetWorld() : nullptr;
}

void FElysiumShotRun::ResolveRunList()
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const FString Map = Sub ? Sub->GetCurrentMapName() : FString();

	ElysiumVantages::Resolve(Map, CamSelector, RunList);
	UE_LOG(LogElysiumShots, Log, TEXT("map %s: %d vantage(s) to capture."), *Map, RunList.Num());
}

void FElysiumShotRun::ArmCamera(int32 InCamIndex)
{
	const FElysiumVantage& Cam = ElysiumVantages::Table[RunList[InCamIndex]];
	if (Cam.bUseSpawn)
	{
		UWorld* World = GetWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (PC)
		{
			PC->GetPlayerViewPoint(PinLoc, PinRot);
		}
	}
	else
	{
		PinLoc = Cam.Loc;
		PinRot = Cam.Rot;
	}
}

void FElysiumShotRun::PinCamera()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!PC || !Pawn)
	{
		return;
	}
	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		Char->GetCharacterMovement()->StopMovementImmediately();
		Char->GetCharacterMovement()->SetMovementMode(MOVE_None);
	}
	Pawn->SetActorLocation(PinLoc, false, nullptr, ETeleportType::TeleportPhysics);
	PC->SetControlRotation(PinRot);
}

void FElysiumShotRun::BeginCapture()
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const FString Map = Sub ? Sub->GetCurrentMapName() : TEXT("unknown");
	const FString CamName = ElysiumVantages::Table[RunList[CamIndex]].Name;

	// tools/out/_shots/<map>/<map>_<cam>.png — gitignored, derived from the user's own install.
	const FString Path = FElysiumContentPaths::Root() / TEXT("_shots") / Map
		/ (FString::Printf(TEXT("%s_%s.png"), *Map, *CamName));

	bAwaitingCapture = true;
	bCaptureOk = false;

	const bool bRequested = ElysiumScreenshot::Request(
		[this, Path, CamName](int32 Width, int32 Height, const TArray<FColor>& Bitmap)
		{
			FShot Shot;
			Shot.CamName = CamName;
			Shot.File = Path;
			Shot.Width = Width;
			Shot.Height = Height;
			Shot.bOk = (Width > 0 && Height > 0) && ElysiumScreenshot::SavePng(Width, Height, Bitmap, Path);
			Shots.Add(Shot);

			if (Shot.bOk)
			{
				UE_LOG(LogElysiumShots, Log, TEXT("captured %s (%dx%d)"), *Path, Width, Height);
			}
			else
			{
				UE_LOG(LogElysiumShots, Warning, TEXT("capture FAILED for cam=%s"), *CamName);
			}
			bCaptureOk = Shot.bOk;
			bAwaitingCapture = false;
		});

	if (!bRequested)
	{
		UE_LOG(LogElysiumShots, Warning, TEXT("no viewport to capture; aborting."));
		bAwaitingCapture = false;
	}
}

void FElysiumShotRun::Finish()
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const FString Map = Sub ? Sub->GetCurrentMapName() : TEXT("unknown");

	// A manifest beside the shots, so tools/ (a future image-diff) knows what was captured.
	FString Json;
	Json += TEXT("{\n");
	Json += FString::Printf(TEXT("  \"map\": \"%s\",\n"), *Map);
	Json += FString::Printf(TEXT("  \"sm6\": %s,\n"),
		(GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM6) ? TEXT("true") : TEXT("false"));
	Json += TEXT("  \"shots\": [\n");
	for (int32 i = 0; i < Shots.Num(); ++i)
	{
		const FShot& S = Shots[i];
		Json += TEXT("    {");
		Json += FString::Printf(TEXT("\"cam\": \"%s\", "), *S.CamName);
		Json += FString::Printf(TEXT("\"file\": \"%s\", "), *FPaths::GetCleanFilename(S.File));
		Json += FString::Printf(TEXT("\"width\": %d, \"height\": %d, "), S.Width, S.Height);
		Json += FString::Printf(TEXT("\"ok\": %s"), S.bOk ? TEXT("true") : TEXT("false"));
		Json += (i + 1 < Shots.Num()) ? TEXT("},\n") : TEXT("}\n");
	}
	Json += TEXT("  ]\n");
	Json += TEXT("}\n");

	const FString OutPath = FElysiumContentPaths::Root() / TEXT("_shots") / Map / TEXT("manifest.json");
	if (FFileHelper::SaveStringToFile(Json, *OutPath))
	{
		UE_LOG(LogElysiumShots, Log, TEXT("wrote %s (%d shots)"), *OutPath, Shots.Num());
	}

	UE_LOG(LogElysiumShots, Log, TEXT("screenshot run complete; exiting."));
	FPlatformMisc::RequestExit(false);
}

bool FElysiumShotRun::Tick(float /*DeltaSeconds*/)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const AElysiumMapActor* MapActor = Sub ? Sub->GetCurrentMap() : nullptr;

	switch (Phase)
	{
	case EPhase::WaitReady:
	{
		if (PC && Pawn && MapActor && MapActor->IsSpawnDone())
		{
			if (++FrameInPhase >= BootSettleFrames)
			{
				ResolveRunList();
				if (RunList.Num() == 0)
				{
					UE_LOG(LogElysiumShots, Warning,
						TEXT("no vantages configured for this map; exiting without capture."));
					Finish();
					Phase = EPhase::Done;
					return false;
				}
				ArmCamera(CamIndex);
				PinCamera();
				Phase = EPhase::Settle;
				FrameInPhase = 0;
			}
		}
		break;
	}

	case EPhase::Settle:
	{
		PinCamera();
		if (++FrameInPhase >= SettleFrames)
		{
			BeginCapture();
			Phase = EPhase::Await;
			FrameInPhase = 0;
		}
		break;
	}

	case EPhase::Await:
	{
		// Hold the camera while the deferred capture resolves (next rendered frame), so the shot
		// is of the pinned pose even though the callback lands a frame or two later.
		PinCamera();
		if (!bAwaitingCapture)
		{
			if (++CamIndex < RunList.Num())
			{
				ArmCamera(CamIndex);
				PinCamera();
				Phase = EPhase::Settle;
				FrameInPhase = 0;
			}
			else
			{
				Finish();
				Phase = EPhase::Done;
				return false;
			}
		}
		break;
	}

	case EPhase::Capture:
	case EPhase::Done:
	default:
		return false;
	}

	return true;
}
