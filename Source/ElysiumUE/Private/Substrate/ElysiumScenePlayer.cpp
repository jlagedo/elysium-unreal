#include "Substrate/ElysiumScenePlayer.h"

float FElysiumScenePlayer::EffectiveStart(const FElysiumSceneEvent& Event) const
{
	return Event.Type == EElysiumChoreoEvent::Speak ? Event.StartTime - Latency : Event.StartTime;
}

void FElysiumScenePlayer::Begin(TSharedPtr<const FElysiumSceneData> InScene, float InLatency, float InMaxDuration)
{
	Scene = MoveTemp(InScene);
	Latency = FMath::Max(0.f, InLatency);

	const int32 Num = Scene.IsValid() ? Scene->Events.Num() : 0;
	bStarted.Init(false, Num);
	bActive.Init(false, Num);
	CurrentTime = 0.f;
	ActiveCount = 0;

	EffectiveLatestTime = 0.f;
	if (Scene.IsValid())
	{
		for (const FElysiumSceneEvent& Ev : Scene->Events)
		{
			if (!Ev.bActive)
			{
				continue;
			}
			const float End = Ev.bHasEnd ? Ev.EndTime : Ev.StartTime;
			const float Extended = End + (Ev.Type == EElysiumChoreoEvent::Speak ? Latency : 0.f);
			EffectiveLatestTime = FMath::Max(EffectiveLatestTime, Extended);
		}
		// A handful of authored ranges are nonsense — one event runs to 1.5 million seconds. Left
		// alone, such a scene never reports finished and its OnCompletion never fires; on
		// sp_tutorial_1 that is a soft-lock, because the alley fight's completion gates map flow.
		// The events still play; only the scene's own end is bounded.
		if (InMaxDuration > 0.f)
		{
			EffectiveLatestTime = FMath::Min(EffectiveLatestTime, InMaxDuration);
		}
	}
}

void FElysiumScenePlayer::Reset()
{
	bStarted.Init(false, bStarted.Num());
	bActive.Init(false, bActive.Num());
	CurrentTime = 0.f;
	ActiveCount = 0;
}

void FElysiumScenePlayer::CaptureLatches(TArray<uint8>& OutStarted, TArray<uint8>& OutActive) const
{
	OutStarted.SetNumUninitialized(bStarted.Num());
	OutActive.SetNumUninitialized(bActive.Num());
	for (int32 i = 0; i < bStarted.Num(); ++i)
	{
		OutStarted[i] = bStarted[i] ? 1 : 0;
		OutActive[i] = bActive[i] ? 1 : 0;
	}
}

void FElysiumScenePlayer::RestoreLatches(float SceneTime, const TArray<uint8>& Started,
	const TArray<uint8>& Active, IElysiumChoreoCallback& Callback)
{
	if (!Scene.IsValid() || Started.Num() != Scene->Events.Num() || Active.Num() != Scene->Events.Num())
	{
		RestoreTo(SceneTime, Callback);
		return;
	}
	CurrentTime = SceneTime;
	ActiveCount = 0;
	for (int32 i = 0; i < Scene->Events.Num(); ++i)
	{
		const FElysiumSceneEvent& Ev = Scene->Events[i];
		bStarted[i] = Ev.bActive && Started[i] != 0;
		bActive[i] = bStarted[i] && Active[i] != 0;
		if (bActive[i])
		{
			++ActiveCount;
			if (Ev.bHasEnd)
			{
				Callback.RestoreEvent(*Scene, Ev, SceneTime);
			}
		}
	}
}

void FElysiumScenePlayer::RestoreTo(float SceneTime, IElysiumChoreoCallback& Callback)
{
	if (!Scene.IsValid())
	{
		return;
	}
	CurrentTime = SceneTime;
	ActiveCount = 0;
	for (int32 i = 0; i < Scene->Events.Num(); ++i)
	{
		const FElysiumSceneEvent& Ev = Scene->Events[i];
		const float Start = EffectiveStart(Ev);
		const float End = Ev.bHasEnd ? FMath::Max(Ev.EndTime, Start) : Start;
		bStarted[i] = Ev.bActive && Start <= SceneTime;
		bActive[i] = bStarted[i] && Ev.bHasEnd && SceneTime <= End;
		if (bActive[i])
		{
			++ActiveCount;
			Callback.RestoreEvent(*Scene, Ev, SceneTime);
		}
	}
}

void FElysiumScenePlayer::PreLatchTo(float SceneTime)
{
	if (!Scene.IsValid())
	{
		return;
	}
	CurrentTime = SceneTime;
	ActiveCount = 0;
	for (int32 i = 0; i < Scene->Events.Num(); ++i)
	{
		const FElysiumSceneEvent& Ev = Scene->Events[i];
		const bool bPassed = Ev.bActive && EffectiveStart(Ev) <= SceneTime;
		bStarted[i] = bPassed;
		bActive[i] = false;
	}
}

void FElysiumScenePlayer::AdvanceTo(float SceneTime, IElysiumChoreoCallback& Callback)
{
	if (!Scene.IsValid())
	{
		return;
	}
	CurrentTime = SceneTime;

	// Events are stored sorted by start time, so walking in order dispatches in start order — which
	// is what decides which of two triggers on the same frame fires first.
	for (int32 i = 0; i < Scene->Events.Num(); ++i)
	{
		const FElysiumSceneEvent& Ev = Scene->Events[i];
		if (!Ev.bActive)
		{
			continue;
		}
		const float Start = EffectiveStart(Ev);
		// An event with no end time occupies a single instant. Holding End at Start makes it start
		// and end within one AdvanceTo, which is exactly what the 24 firetriggers need.
		const float End = Ev.bHasEnd ? FMath::Max(Ev.EndTime, Start) : Start;

		if (!bStarted[i] && Start <= CurrentTime)
		{
			// Latched on "has its start passed", not on "did its start fall inside this frame's
			// window". On a monotonic clock the two agree; the latch additionally means a long
			// frame or a restored save cannot silently skip an event, which is the difference
			// between a scene that completes and one whose OnCompletion never fires.
			bStarted[i] = true;
			bActive[i] = true;
			++ActiveCount;
			Callback.StartEvent(*Scene, Ev, CurrentTime);
			// One disposition per event per frame: VtMB's classifier answers START *or* CONTINUE
			// *or* STOP, never two. So an event is only continued on frames after the one that
			// started it, and an instantaneous event (end == start) is ended on the next frame
			// rather than the one that fired it.
			continue;
		}

		if (bActive[i])
		{
			if (CurrentTime > End)
			{
				bActive[i] = false;
				--ActiveCount;
				Callback.EndEvent(*Scene, Ev, CurrentTime);
			}
			else
			{
				Callback.ProcessEvent(*Scene, Ev, CurrentTime);
			}
		}
	}
}

void FElysiumScenePlayer::StopActiveEvents(IElysiumChoreoCallback& Callback)
{
	if (!Scene.IsValid())
	{
		return;
	}
	for (int32 i = 0; i < Scene->Events.Num(); ++i)
	{
		if (bActive[i])
		{
			bActive[i] = false;
			Callback.EndEvent(*Scene, Scene->Events[i], CurrentTime);
		}
	}
	ActiveCount = 0;
}
