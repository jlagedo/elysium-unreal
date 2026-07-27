#include "Debug/ElysiumProbeRun.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumLightProbe.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumProbe, Log, All);

namespace
{
	// Let the map settle before tracing. The spawn pass is done by then, but prop bodies and
	// collision cook on the first frames; probing into a half-built scene would report "nothing
	// near" for lights whose fixture had not landed yet.
	constexpr int32 ProbeSettleFrames = 30;
}

bool FElysiumProbeRun::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("ElysiumProbe"));
}

FElysiumProbeRun::FElysiumProbeRun(UElysiumMapSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
	FParse::Value(FCommandLine::Get(), TEXT("ProbeRays="), NumRays);
	NumRays = FMath::Clamp(NumRays, 8, 512);
	UE_LOG(LogElysiumProbe, Log, TEXT("headless light probe armed: %d rays."), NumRays);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FElysiumProbeRun::Tick));
}

FElysiumProbeRun::~FElysiumProbeRun()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
}

bool FElysiumProbeRun::Tick(float /*DeltaSeconds*/)
{
	if (bDone)
	{
		return false;
	}
	UElysiumMapSubsystem* Sub = Subsystem.Get();
	const UGameInstance* GI = Sub ? Sub->GetGameInstance() : nullptr;
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	AElysiumMapActor* MapActor = Sub ? Sub->GetCurrentMap() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;

	if (!(PC && MapActor && MapActor->IsSpawnDone()))
	{
		return true;
	}
	if (++FrameInPhase < ProbeSettleFrames)
	{
		return true;
	}

	const int32 N = ElysiumLightProbe::Run(World, MapActor, NumRays);
	if (N < 0)
	{
		UE_LOG(LogElysiumProbe, Warning, TEXT("probe found no light rig on %s"), *MapActor->MapName);
	}
	else
	{
		UE_LOG(LogElysiumProbe, Log, TEXT("probed %d lights on %s; exiting."), N, *MapActor->MapName);
	}
	bDone = true;
	FPlatformMisc::RequestExit(false);
	return false;
}

#endif // !UE_BUILD_SHIPPING
