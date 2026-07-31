#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class UElysiumMapSubsystem;

// Headless profiling harness for roadmap task 0.1 (profiling baseline) and 0.2
// (MegaLights engagement check). Activated by the -ElysiumProfile command-line switch.
//
// It removes the human from the loop that normally types `stat GPU` / `ProfileGPU` in a
// live session. Once the boot map's pawn has settled (collision cooked), it drives a fixed
// sequence per configured vantage: pin the camera stock-still, warm up N frames (Lumen
// temporal accumulation + first-run shader compile settle), then capture M frames through
// the CSV profiler — per-pass GPU stats land in the CSV when the process is launched with
// -csvGpuStats — plus a one-frame ProfileGPU dump to the log as a human-readable backup.
// Averaged stat-unit frame times, the SM6/DX12 confirmation, and the rig light count are
// written to a JSON summary under $ELYSIUM_EXPORT_ROOT/_profile/, then the process exits.
//
// A real RHI is required (GPU timings are meaningless under -nullrhi); the run self-cancels
// if the null RHI is active. pipeline/src/elysium_pipeline/validation/profile_report.py turns the CSVs into the roadmap table.
class FElysiumProfileRun
{
public:
	explicit FElysiumProfileRun(UElysiumMapSubsystem* InSubsystem);
	~FElysiumProfileRun();

	// True when -ElysiumProfile was passed and a real RHI is present.
	static bool IsRequested();

private:
	bool Tick(float DeltaSeconds);
	class UWorld* GetWorld() const;

	// Resolve RunList from the static cam table once the boot map is known: keep the
	// vantages whose map matches (or the map-agnostic "spawn"), honoring -ProfileCam=.
	void ResolveRunList();
	// Record the target vantage into PinLoc/PinRot for the given cam index, reading the
	// pawn's current transform for the special "spawn" vantage.
	void ArmCamera(int32 InCamIndex);
	// Re-assert the pinned transform and freeze the pawn (called every warmup/capture frame).
	void PinCamera();
	void BeginCsvCapture();
	void PushRow();
	void Finish();

	enum class EPhase : uint8 { WaitReady, Warmup, Capture, Drain, Done };

	TWeakObjectPtr<UElysiumMapSubsystem> Subsystem;
	FTSTicker::FDelegateHandle TickHandle;

	EPhase Phase = EPhase::WaitReady;
	int32 CamIndex = 0;         // index into the resolved run list
	int32 FrameInPhase = 0;

	// Tunable via -ProfileWarmup= / -ProfileFrames=.
	int32 WarmupFrames = 120;
	int32 CaptureFrames = 300;

	// Raw -ProfileCam= value (a single index/name), applied when the run list is resolved.
	// Empty means "every vantage for this map".
	FString CamSelector;

	// The vantages this run will visit (indices into the static cam table), resolved once
	// the current map is known (see ResolveRunList).
	TArray<int32> RunList;

	// The transform currently being held.
	FVector  PinLoc = FVector::ZeroVector;
	FRotator PinRot = FRotator::ZeroRotator;

	// Per-vantage accumulators (reset when a capture window opens).
	double SumGameMs = 0.0;
	double SumRenderMs = 0.0;
	double SumGpuMs = 0.0;
	int32  CapturedFrames = 0;
	bool   bProfileGpuDumped = false;

	struct FRow
	{
		FString  CamName;
		FVector  Loc;
		FRotator Rot;
		double   GameMs = 0.0;
		double   RenderMs = 0.0;
		double   GpuMs = 0.0;
		FString  CsvFile;
	};
	TArray<FRow> Rows;
};
