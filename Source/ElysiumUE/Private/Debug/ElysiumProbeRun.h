#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

#if !UE_BUILD_SHIPPING

class UElysiumMapSubsystem;

// Headless light-probe harness (`-ElysiumProbe`), the third of the self-driving runs beside
// -ElysiumProfile and -ElysiumShots. It waits for the map to finish its spawn pass, runs
// ElysiumLightProbe over the whole rig, and exits — so `dev/elysium.ps1 probe` can walk every exported map
// unattended instead of the attribution data needing a hand-driven session per map.
//
// One map per launch, like the other two harnesses: the probe reads the built scene, and a fresh
// process is the cheapest way to guarantee a clean one.
class FElysiumProbeRun
{
public:
	static bool IsRequested();

	explicit FElysiumProbeRun(UElysiumMapSubsystem* InSubsystem);
	~FElysiumProbeRun();

private:
	bool Tick(float DeltaSeconds);

	TWeakObjectPtr<UElysiumMapSubsystem> Subsystem;
	FTSTicker::FDelegateHandle TickHandle;
	int32 FrameInPhase = 0;
	int32 NumRays = 64;
	bool bDone = false;
};

#endif // !UE_BUILD_SHIPPING
