#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class UElysiumMapSubsystem;

// P2.9 — the headless screenshot-regression harness, sibling to FElysiumProfileRun. Activated by
// `-ElysiumShots`. It removes the human from "load the map, walk to each vantage, take a shot":
// once the boot map's pawn has settled, it visits each vantage in ElysiumVantages.h, pins the
// camera stock-still, lets the frame settle (Lumen temporal accumulation + first-run shader
// compile), captures the viewport as a PNG under $ELYSIUM_EXPORT_ROOT/_shots/<map>/, writes a manifest, and
// exits. No interaction.
//
// Unlike the profiler, a real RHI is REQUIRED and there is no null-RHI guard beyond a warning: a
// screenshot under -nullrhi is a blank frame, so the harness self-cancels if the null RHI is
// active. Baselines are derived from the user's own VtMB install, so they live under the gitignored
// $ELYSIUM_EXPORT_ROOT and are never committed.
class FElysiumShotRun
{
public:
	explicit FElysiumShotRun(UElysiumMapSubsystem* InSubsystem);
	~FElysiumShotRun();

	// True when -ElysiumShots was passed and a real RHI is present.
	static bool IsRequested();

private:
	bool Tick(float DeltaSeconds);
	class UWorld* GetWorld() const;

	void ResolveRunList();
	void ArmCamera(int32 InCamIndex);
	void PinCamera();
	void BeginCapture();
	void Finish();

	enum class EPhase : uint8 { WaitReady, Settle, Capture, Await, Done };

	TWeakObjectPtr<UElysiumMapSubsystem> Subsystem;
	FTSTicker::FDelegateHandle TickHandle;

	EPhase Phase = EPhase::WaitReady;
	int32 CamIndex = 0;         // index into the resolved run list
	int32 FrameInPhase = 0;

	// Frames to hold the pinned camera before the shot, so Lumen has accumulated and shaders have
	// compiled. Tunable via -ShotSettle=.
	int32 SettleFrames = 90;

	// Raw -ShotCam= value (index/name), applied when the run list is resolved.
	FString CamSelector;
	// Acceptance runs opt into advancing the game clock behind the otherwise paused front-end
	// harness. The rain command itself still arrives through the authored timer I/O chain.
	bool bWeatherAcceptance = false;
	double LastObservedWeatherClock = -1.0;

	// The vantages this run will visit (indices into ElysiumVantages::Table).
	TArray<int32> RunList;

	FVector  PinLoc = FVector::ZeroVector;
	FRotator PinRot = FRotator::ZeroRotator;

	// The capture in flight for the current vantage (set false by the async callback).
	bool bAwaitingCapture = false;
	bool bCaptureOk = false;

	struct FShot
	{
		FString CamName;
		FString File;      // absolute path written
		int32   Width = 0;
		int32   Height = 0;
		bool    bOk = false;
	};
	TArray<FShot> Shots;
};
