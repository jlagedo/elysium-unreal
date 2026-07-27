#include "Debug/ElysiumProfiler.h"

#include "ElysiumContentPaths.h"
#include "ElysiumMapActor.h"
#include "Visual/ElysiumMapVisuals.h"
#include "ElysiumMapSubsystem.h"
#include "Debug/ElysiumVantages.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "DynamicRHI.h"
#include "RHI.h"
#include "RenderCore.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumProfile, Log, All);

// The fixed camera vantages live in ElysiumVantages.h, shared with the screenshot harness so a
// perf sample and a screenshot come from the exact same viewpoint.
namespace
{
	// Number of frames after the spawn settles before the first warmup begins, to let
	// the first-frame stalls (async collision cook, streaming) drain out of the average.
	constexpr int32 SettleFrames = 30;

	// Frames to keep ticking after the final capture so the async CSV write flushes to
	// disk before the process exits (otherwise the last file is truncated).
	constexpr int32 DrainFrames = 60;
}

bool FElysiumProfileRun::IsRequested()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("ElysiumProfile")))
	{
		return false;
	}
	if (GUsingNullRHI)
	{
		UE_LOG(LogElysiumProfile, Warning,
			TEXT("-ElysiumProfile ignored: GPU timings are meaningless under the null RHI."));
		return false;
	}
	return true;
}

FElysiumProfileRun::FElysiumProfileRun(UElysiumMapSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
	FParse::Value(FCommandLine::Get(), TEXT("ProfileWarmup="), WarmupFrames);
	FParse::Value(FCommandLine::Get(), TEXT("ProfileFrames="), CaptureFrames);
	FParse::Value(FCommandLine::Get(), TEXT("ProfileCam="), CamSelector);
	WarmupFrames = FMath::Max(1, WarmupFrames);
	CaptureFrames = FMath::Max(1, CaptureFrames);

	UE_LOG(LogElysiumProfile, Log,
		TEXT("headless profile armed: warmup %d, capture %d frames. RHI=%s (%s) SM6=%s"),
		WarmupFrames, CaptureFrames,
		GDynamicRHI ? GDynamicRHI->GetName() : TEXT("?"),
		*GRHIAdapterName,
		(GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM6) ? TEXT("yes") : TEXT("NO (features off!)"));

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FElysiumProfileRun::Tick));
}

FElysiumProfileRun::~FElysiumProfileRun()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
}

UWorld* FElysiumProfileRun::GetWorld() const
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const UGameInstance* GI = Sub ? Sub->GetGameInstance() : nullptr;
	return GI ? GI->GetWorld() : nullptr;
}

void FElysiumProfileRun::ResolveRunList()
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const FString Map = Sub ? Sub->GetCurrentMapName() : FString();

	ElysiumVantages::Resolve(Map, CamSelector, RunList);
	UE_LOG(LogElysiumProfile, Log, TEXT("map %s: %d vantage(s) to profile."), *Map, RunList.Num());
}

void FElysiumProfileRun::ArmCamera(int32 InCamIndex)
{
	const FElysiumVantage& Cam = ElysiumVantages::Table[RunList[InCamIndex]];
	if (Cam.bUseSpawn)
	{
		// Hold wherever the .spawn point placed the pawn, looking along its view.
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

void FElysiumProfileRun::PinCamera()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!PC || !Pawn)
	{
		return;
	}
	// Freeze the body and stamp the pinned pose every frame so nothing drifts the shot.
	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		Char->GetCharacterMovement()->StopMovementImmediately();
		Char->GetCharacterMovement()->SetMovementMode(MOVE_None);
	}
	Pawn->SetActorLocation(PinLoc, false, nullptr, ETeleportType::TeleportPhysics);
	PC->SetControlRotation(PinRot);
}

void FElysiumProfileRun::BeginCsvCapture()
{
	SumGameMs = SumRenderMs = SumGpuMs = 0.0;
	CapturedFrames = 0;
	bProfileGpuDumped = false;

	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const FString Map = Sub ? Sub->GetCurrentMapName() : TEXT("unknown");
	const FString CamName = ElysiumVantages::Table[RunList[CamIndex]].Name;

#if CSV_PROFILER
	const FString Folder = FElysiumContentPaths::Root() / TEXT("_profile");
	const FString File = FString::Printf(TEXT("elysium_%s_%s"), *Map, *CamName);
	FCsvProfiler::Get()->BeginCapture(CaptureFrames, Folder, File);
#endif

	UE_LOG(LogElysiumProfile, Log, TEXT("capture start: map=%s cam=%s (%d frames)"),
		*Map, *CamName, CaptureFrames);
}

void FElysiumProfileRun::PushRow()
{
	const FElysiumVantage& Cam = ElysiumVantages::Table[RunList[CamIndex]];
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const FString Map = Sub ? Sub->GetCurrentMapName() : TEXT("unknown");

	FRow Row;
	Row.CamName  = Cam.Name;
	Row.Loc      = PinLoc;
	Row.Rot      = PinRot;
	const int32 N = FMath::Max(1, CapturedFrames);
	Row.GameMs   = SumGameMs / N;
	Row.RenderMs = SumRenderMs / N;
	Row.GpuMs    = SumGpuMs / N;
	Row.CsvFile  = FString::Printf(TEXT("elysium_%s_%s"), *Map, Cam.Name);
	Rows.Add(Row);

	UE_LOG(LogElysiumProfile, Log,
		TEXT("capture done: cam=%s  game %.2f ms  render %.2f ms  gpu %.2f ms  (avg over %d frames)"),
		Cam.Name, Row.GameMs, Row.RenderMs, Row.GpuMs, CapturedFrames);
}

void FElysiumProfileRun::Finish()
{
	const UElysiumMapSubsystem* Sub = Subsystem.Get();
	const FString Map = Sub ? Sub->GetCurrentMapName() : TEXT("unknown");
	const AElysiumMapActor* MapActor = Sub ? Sub->GetCurrentMap() : nullptr;
	const UElysiumMapVisuals* Visuals = MapActor ? MapActor->GetVisuals() : nullptr;
	const int32 LightCount = Visuals ? Visuals->WorldLightCount : 0;
	const bool bSM6 = (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM6);

	// Machine-readable summary; tools/profile_report.py joins it with the per-pass CSVs.
	FString Json;
	Json += TEXT("{\n");
	Json += FString::Printf(TEXT("  \"map\": \"%s\",\n"), *Map);
	Json += FString::Printf(TEXT("  \"rhi\": \"%s\",\n"), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("?"));
	Json += FString::Printf(TEXT("  \"adapter\": \"%s\",\n"), *GRHIAdapterName.ReplaceCharWithEscapedChar());
	Json += FString::Printf(TEXT("  \"sm6\": %s,\n"), bSM6 ? TEXT("true") : TEXT("false"));
	Json += FString::Printf(TEXT("  \"world_lights\": %d,\n"), LightCount);
	Json += FString::Printf(TEXT("  \"warmup_frames\": %d,\n"), WarmupFrames);
	Json += FString::Printf(TEXT("  \"capture_frames\": %d,\n"), CaptureFrames);
	Json += TEXT("  \"vantages\": [\n");
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const FRow& R = Rows[i];
		Json += TEXT("    {");
		Json += FString::Printf(TEXT("\"cam\": \"%s\", "), *R.CamName);
		Json += FString::Printf(TEXT("\"loc\": [%.1f, %.1f, %.1f], "), R.Loc.X, R.Loc.Y, R.Loc.Z);
		Json += FString::Printf(TEXT("\"rot\": [%.1f, %.1f, %.1f], "), R.Rot.Pitch, R.Rot.Yaw, R.Rot.Roll);
		Json += FString::Printf(TEXT("\"game_ms\": %.3f, \"render_ms\": %.3f, \"gpu_ms\": %.3f, "),
			R.GameMs, R.RenderMs, R.GpuMs);
		Json += FString::Printf(TEXT("\"csv\": \"%s\""), *R.CsvFile);
		Json += (i + 1 < Rows.Num()) ? TEXT("},\n") : TEXT("}\n");
	}
	Json += TEXT("  ]\n");
	Json += TEXT("}\n");

	const FString OutPath = FElysiumContentPaths::Root() / TEXT("_profile") / (Map + TEXT("_summary.json"));
	if (FFileHelper::SaveStringToFile(Json, *OutPath))
	{
		UE_LOG(LogElysiumProfile, Log, TEXT("wrote %s"), *OutPath);
	}
	else
	{
		UE_LOG(LogElysiumProfile, Warning, TEXT("failed to write %s"), *OutPath);
	}

	if (!bSM6)
	{
		UE_LOG(LogElysiumProfile, Error,
			TEXT("NOT running SM6 — Lumen/MegaLights/VSM are all OFF. Baseline is invalid; force -dx12 / PCD3D_SM6."));
	}

	UE_LOG(LogElysiumProfile, Log, TEXT("profile run complete; exiting."));
	FPlatformMisc::RequestExit(false);
}

bool FElysiumProfileRun::Tick(float /*DeltaSeconds*/)
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
		// Do not touch the scene until the pawn exists and the map actor has released the
		// spawn hold (collision cooked). Then let a few frames settle before warming up.
		if (PC && Pawn && MapActor && MapActor->IsSpawnDone())
		{
			if (++FrameInPhase >= SettleFrames)
			{
				ResolveRunList();
				if (RunList.Num() == 0)
				{
					UE_LOG(LogElysiumProfile, Warning,
						TEXT("no vantages configured for this map; exiting without capture."));
					Finish();
					Phase = EPhase::Done;
					return false;
				}
				ArmCamera(CamIndex);
				PinCamera();
				Phase = EPhase::Warmup;
				FrameInPhase = 0;
			}
		}
		break;
	}

	case EPhase::Warmup:
	{
		PinCamera();
		if (++FrameInPhase >= WarmupFrames)
		{
			BeginCsvCapture();
			Phase = EPhase::Capture;
			FrameInPhase = 0;
		}
		break;
	}

	case EPhase::Capture:
	{
		PinCamera();

		// stat-unit equivalents straight from the engine's frame-time globals (cycles->ms).
		SumGameMs   += FPlatformTime::ToMilliseconds(GGameThreadTime);
		SumRenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		SumGpuMs    += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
		++CapturedFrames;

		// One hierarchical GPU breakdown to the log, mid-window, as a human-readable backup
		// to the averaged CSV columns.
		if (!bProfileGpuDumped && FrameInPhase == CaptureFrames / 2 && GEngine)
		{
			GEngine->Exec(World, TEXT("ProfileGPU"));
			bProfileGpuDumped = true;
		}

		if (++FrameInPhase >= CaptureFrames)
		{
			PushRow();
			FrameInPhase = 0;
			if (++CamIndex < RunList.Num())
			{
				ArmCamera(CamIndex);
				PinCamera();
				Phase = EPhase::Warmup;
			}
			else
			{
				// Let the final capture's async CSV write flush before exiting.
				Phase = EPhase::Drain;
			}
		}
		break;
	}

	case EPhase::Drain:
	{
		if (++FrameInPhase >= DrainFrames)
		{
			Finish();
			Phase = EPhase::Done;
			return false;   // unregister the ticker
		}
		break;
	}

	case EPhase::Done:
	default:
		return false;
	}

	return true;
}
