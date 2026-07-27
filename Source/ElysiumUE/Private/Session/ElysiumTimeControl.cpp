#include "ElysiumTimeControl.h"

#include "ElysiumGameClock.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumTime, Log, All);

UWorld* FElysiumTimeControl::ResolveWorld() const
{
	const UGameInstance* GI = GameInstance.Get();
	return GI ? GI->GetWorld() : nullptr;
}

double FElysiumTimeControl::AdvanceFrame(double DeltaSeconds)
{
	// Bounded exactly as `Host_FilterTime` bounds it, with the same constant the mover uses: a
	// hitch must not fire a whole interval's thinks and queued I/O in one frame, and game time must
	// not disagree with player motion about how long the frame was.
	return Clock.Advance(ElysiumFrame::ClampFrameDelta(DeltaSeconds));
}

void FElysiumTimeControl::EndFrame()
{
	if (PendingSteps <= 0)
	{
		return;
	}
	if (--PendingSteps <= 0)
	{
		PendingSteps = 0;
		ApplyPaused(true);
	}
}

void FElysiumTimeControl::ApplyPaused(bool bPaused)
{
	Clock.SetPaused(bPaused);
	if (UWorld* World = ResolveWorld())
	{
		// Needs a player controller; between worlds it reports false and the clock hold stands
		// alone, which is the correct half to keep (the substrate is what is running).
		UGameplayStatics::SetGamePaused(World, bPaused);
	}
}

void FElysiumTimeControl::SetPaused(bool bPaused)
{
	// A hand pause/resume outranks a step countdown — the steps were only holding the world open.
	PendingSteps = 0;
	ApplyPaused(bPaused);
}

bool FElysiumTimeControl::IsPaused() const
{
	// The clock is the authority; engine pause is its mirror.
	return Clock.IsPaused();
}

void FElysiumTimeControl::SetScale(double Scale)
{
	double Applied = FMath::Max(0.0, Scale);
	if (UWorld* World = ResolveWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(World, static_cast<float>(Applied));
		// Read the value back: AWorldSettings clamps to [MinGlobalTimeDilation,
		// MaxGlobalTimeDilation], and a recorded scale that differs from the dilation the frame
		// delta actually carries would double-count on the next save/restore.
		if (const AWorldSettings* Settings = World->GetWorldSettings())
		{
			Applied = Settings->TimeDilation;
		}
	}
	Clock.SetScale(Applied);
	UE_LOG(LogElysiumTime, Verbose, TEXT("time scale %.4fx"), Applied);
}

double FElysiumTimeControl::GetScale() const
{
	return Clock.GetScale();
}

void FElysiumTimeControl::StepFrames(int32 NumFrames)
{
	// Stepping is only meaningful against a held world: release it for N frames, and let the
	// post-move tick (the tail of a released frame) count them back down.
	if (NumFrames <= 0 || !IsPaused())
	{
		return;
	}
	PendingSteps = NumFrames;
	ApplyPaused(false);
}

void FElysiumTimeControl::ResetClock(double StartSeconds)
{
	PendingSteps = 0;
	Clock.Reset(StartSeconds);
	ApplyToWorld();
}

void FElysiumTimeControl::ApplyToWorld()
{
	UWorld* World = ResolveWorld();
	if (!World)
	{
		return;
	}
	UGameplayStatics::SetGlobalTimeDilation(World, static_cast<float>(Clock.GetScale()));
	UGameplayStatics::SetGamePaused(World, Clock.IsPaused());
}
